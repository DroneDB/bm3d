// SPDX-License-Identifier: MIT
//
// Derived from html/js/nexus.js of Nexus (https://github.com/cnr-isti-vclab/nexus, commit
// d805432), modified by Digipa for bm3d-viewer.
// Nexus: Copyright (c) 2012-2020, Visual Computing Lab, ISTI - CNR. All rights reserved.
// MIT licence; the licence text is in the LICENSE file of this package.

// A model on one WebGL context: its index (the tables of the file), and for each node whether it
// is on the GPU, being loaded, or absent. The traversal (instance.js) says which nodes it wants;
// the cache rule (context.js) chooses; this class loads and drops them.

import { Bm3dError, checkTexture, ErrorCode } from './format.js';
import { getDecoder } from './decoder-client.js';
import { decodeImage, uploadNode, uploadTexture } from './gl-upload.js';
import { openModelFile } from './model-file.js';

export const ABSENT = 0;
export const READY = 1;
export const LOADING = 2;

const MAX_RETRIES = 2; // a request that fails is made again this many times before the node is given up
const RETRY_AFTER = 10000; // ms before a node whose requests kept failing is asked for again
// The GPU size of a texture is not known before it is decoded. The cache budget counts this many
// times the size of its file.
const TEXTURE_SIZE_FACTOR = 10;

export class Bm3dMesh {
	/** `options`: decoderPath, cache, fetch, requestInit (see Bm3dObject), onError(error, nodeId). */
	constructor(url, options = {}) {
		this.url = url;
		this.options = options;
		this.isReady = false;
		this.users = 0; // the instances that show it
		this.ready = this.open();
	}

	async open() {
		const { source, index } = await openModelFile(this.url, this.options);
		this.source = source;
		this.index = index;

		const { nodes } = index;
		const n = index.nodeCount;
		this.nodeCount = n;
		this.sink = index.sink;
		this.rootCount = index.rootCount;
		this.sphere = index.sphere;
		this.hasNormals = index.hasNormals;
		this.hasColors = index.hasColors;
		this.hasUvs = index.hasUvs;
		this.vertexCounts = nodes.vertexCount;
		this.faceCounts = nodes.faceCount;
		this.geometricError = nodes.error;
		this.spheres = nodes.spheres; // per node: centre x, y, z, radius of the saturated sphere, tight radius
		this.firstPatch = nodes.firstPatch;
		this.patches = index.patches; // per patch: child node, end in the triangle list, texture

		// The estimate of the memory of a node, for the cache budget
		const vertexSize = 12 + (this.hasNormals ? 6 : 0) + (this.hasColors ? 4 : 0) + (this.hasUvs ? 8 : 0);
		const faceSize = 6;
		this.nsize = new Float32Array(n);
		for (let i = 0; i < n - 1; i++) {
			this.nsize[i] = vertexSize * this.vertexCounts[i] + faceSize * this.faceCounts[i];
			if (this.hasUvs) this.nsize[i] += TEXTURE_SIZE_FACTOR * index.textures.length[this.textureOf(i)];
		}

		this.status = new Uint8Array(n);
		this.frames = new Uint32Array(n); // the frame in which the view last wanted the node
		this.errors = new Float32Array(n); // the largest error the view found in the node
		this.failedUntil = new Float64Array(n); // not asked for before this time (ms), Infinity for good
		this.loads = new Array(n).fill(null);
		this.vbo = new Array(n).fill(null);
		this.ibo = new Array(n).fill(null);
		this.texids = new Array(index.textureCount).fill(null);
		this.texref = new Uint32Array(index.textureCount); // nodes that hold each texture
		this.textureLoads = new Map();
		this.root = null; // positions and triangles of node 0, for ray casting

		this.isReady = true;
	}

	/** The texture of a node: all its patches name the same one. */
	textureOf(id) {
		return this.patches[this.firstPatch[id] * 3 + 2];
	}

	get decoder() {
		return getDecoder(this.options.decoderPath);
	}

	/** False for a node that failed and is not to be asked for yet. */
	isAvailable(id) {
		return this.failedUntil[id] <= performance.now();
	}

	/** Starts loading node `id` for `context`; `instance.onUpdate` is called when it is there. */
	request(context, id, instance) {
		const load = { controller: new AbortController(), waiting: 0, retries: 0, instance };
		this.loads[id] = load;
		this.status[id] = LOADING;
		context.pending++;
		context.cacheSize += this.nsize[id];

		this.start(context, id, load, (signal) => this.loadGeometry(context, id, load, signal));
		if (this.hasUvs) {
			this.texref[this.textureOf(id)]++;
			this.start(context, id, load, () => this.ensureTexture(context, this.textureOf(id)));
		}
	}

	/** One part of a node (its geometry, its texture): run `task`, again if the request failed. */
	start(context, id, load, task) {
		load.waiting++;
		const attempt = () =>
			task(load.controller.signal).then(
				() => this.partDone(context, id, load),
				(error) => this.partFailed(context, id, load, error, attempt),
			);
		attempt();
	}

	partDone(context, id, load) {
		if (this.loads[id] !== load) return; // the node was dropped meanwhile
		if (--load.waiting > 0) return;
		this.loads[id] = null;
		this.status[id] = READY;
		context.pending--;
		if (load.instance.onUpdate) load.instance.onUpdate();
		context.updateCache();
	}

	partFailed(context, id, load, error, again) {
		if (this.loads[id] !== load) return;
		const dataError = error instanceof Bm3dError && (error.code === ErrorCode.CORRUPT_NODE || error.code === ErrorCode.CORRUPT_TEXTURE);
		if (!dataError && load.retries < MAX_RETRIES) {
			load.retries++;
			again();
			return;
		}
		// Corrupt bytes stay corrupt; for a failing network the node is tried again later
		this.release(context, id);
		this.failedUntil[id] = dataError ? Infinity : performance.now() + RETRY_AFTER;
		this.reportError(error, id);
	}

	reportError(error, id) {
		if (this.options.onError) this.options.onError(error, id);
		else console.error(`bm3d: node ${id} of ${this.url}:`, error);
	}

	async loadGeometry(context, id, load, signal) {
		const start = this.index.nodes.payloadOffset[id];
		const bytes = await this.source.read(start, start + this.index.nodes.payloadLength[id], { signal });
		if (this.loads[id] !== load) return;
		const model = await this.decoder.decode(bytes, {
			vertexCount: this.vertexCounts[id],
			faceCount: this.faceCounts[id],
			normal: this.hasNormals,
			color: this.hasColors,
			uv: this.hasUvs,
		});
		if (this.loads[id] !== load) return;
		const { vbo, ibo } = uploadNode(context.gl, model);
		this.vbo[id] = vbo;
		this.ibo[id] = ibo;
		if (id === 0) this.root = { positions: model.position, indices: model.index };
	}

	/** Resolves when texture `tex` is on the GPU; several nodes that share it wait for one request. */
	ensureTexture(context, tex) {
		if (this.texids[tex]) return Promise.resolve();
		let loading = this.textureLoads.get(tex);
		if (!loading) {
			loading = this.loadTexture(context, tex).finally(() => this.textureLoads.delete(tex));
			this.textureLoads.set(tex, loading);
		}
		return loading;
	}

	async loadTexture(context, tex) {
		const start = this.index.textures.offset[tex];
		const bytes = new Uint8Array(await this.source.read(start, start + this.index.textures.length[tex]));
		checkTexture(bytes, tex);
		let image;
		try {
			image = await decodeImage(bytes);
		} catch (cause) {
			throw new Bm3dError(ErrorCode.CORRUPT_TEXTURE, `texture ${tex} could not be decoded by the browser`, { cause });
		}
		if (this.texref[tex] === 0) return; // every node that wanted it has gone
		this.texids[tex] = uploadTexture(context.gl, image);
	}

	/** Drops node `id`: stops its requests and frees what it holds on the GPU. */
	release(context, id) {
		if (this.status[id] === ABSENT) return;
		const gl = context.gl;
		const load = this.loads[id];
		if (load) {
			load.controller.abort();
			this.loads[id] = null;
			context.pending--;
		}
		this.status[id] = ABSENT;
		context.cacheSize -= this.nsize[id];
		gl.deleteBuffer(this.vbo[id]);
		gl.deleteBuffer(this.ibo[id]);
		this.vbo[id] = this.ibo[id] = null;

		if (!this.hasUvs) return;
		const tex = this.textureOf(id);
		if (--this.texref[tex] === 0 && this.texids[tex]) {
			gl.deleteTexture(this.texids[tex]);
			this.texids[tex] = null;
		}
	}

	/** Drops every node. */
	flush(context) {
		for (let i = 0; i < this.nodeCount; i++) this.release(context, i);
	}
}
