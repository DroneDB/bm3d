// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// The loading of nodes (request, decode in the worker, upload, release), with a recording WebGL
// context and the real worker and wasm: what happens to a node that is dropped while it loads, to
// a corrupt payload, to a request that fails.

import { readFileSync } from 'node:fs';
import { afterAll, afterEach, beforeAll, describe, expect, it } from 'vitest';

import { GlContext, getContext } from '../src/context.js';
import { getDecoder } from '../src/decoder-client.js';
import { ErrorCode } from '../src/format.js';
import { ABSENT, Bm3dMesh, READY } from '../src/mesh.js';
import { fakeGl } from './support/fake-gl.js';
import { NodeWorker } from './support/node-worker.js';
import { startServer } from './support/server.js';

const read = (path) => readFileSync(new URL(path, import.meta.url));

let server;
let decoderPath;
beforeAll(async () => {
	// The browser's Worker and Image, as far as the loader uses them
	globalThis.Worker = class {
		constructor(url) {
			return new NodeWorker(url);
		}
	};
	globalThis.Image = class {
		constructor() {
			this.width = 512;
			this.height = 512;
		}
		decode() {
			return Promise.resolve();
		}
	};
	const files = {
		'/terrain.bm3d': { data: read('./fixtures/terrain.bm3d') },
		'/decoder/corto-worker.js': { data: read('../dist/corto-worker.js'), type: 'text/javascript' },
		'/decoder/corto.wasm': { data: read('../dist/corto.wasm'), type: 'application/wasm' },
	};
	server = await startServer((path) => files[path] || null);
	decoderPath = `${server.url}/decoder/`;
});
afterAll(async () => {
	await getDecoder(decoderPath).terminate();
	await server.close();
	delete globalThis.Worker;
	delete globalThis.Image;
});
afterEach(() => {
	server.requests.length = 0;
});

const count = (gl, name) => gl.calls.filter((c) => c.name === name).length;

/** A mesh of the terrain, a context, and promises for what happens to it. */
async function setup({ fetch } = {}) {
	const gl = fakeGl({ returns: { createBuffer: () => ({}), createTexture: () => ({}) } });
	const context = getContext(gl);
	const errors = [];
	const mesh = new Bm3dMesh(`${server.url}/terrain.bm3d`, { decoderPath, fetch, onError: (error, node) => errors.push({ error, node }) });
	await mesh.ready;
	const arrived = [];
	const instance = { onUpdate: () => arrived.splice(0).forEach((resolve) => resolve()) };
	const next = () => new Promise((resolve) => arrived.push(resolve));
	/** Resolves when a node arrives or fails. */
	const settled = (id) =>
		new Promise((resolve) => {
			arrived.push(resolve);
			const original = mesh.options.onError;
			mesh.options.onError = (error, node) => {
				original(error, node);
				if (node === id) resolve();
			};
		});
	return { gl, context, mesh, instance, errors, next, settled };
}

/** A fetch that gives the response of `change(range, response)` for the requests it matches. */
function tamperedFetch(change) {
	return async (url, init) => {
		const response = await fetch(url, init);
		return (await change(init.headers.Range, response, init)) || response;
	};
}

describe('a node that is requested', () => {
	it('is decoded, put on the GPU with its texture, and dropped again', async () => {
		const { gl, context, mesh, instance, next } = await setup();
		const arrival = next();
		mesh.request(context, 0, instance);
		expect(mesh.status[0]).not.toBe(ABSENT);
		expect(context.pending).toBe(1);
		await arrival;

		expect(mesh.status[0]).toBe(READY);
		expect(context.pending).toBe(0);
		expect(context.cacheSize).toBe(mesh.nsize[0]);
		expect(count(gl, 'createBuffer')).toBe(2);
		expect(count(gl, 'createTexture')).toBe(1);
		expect(mesh.vbo[0]).not.toBeNull();
		expect(mesh.texids[mesh.textureOf(0)]).not.toBeNull();
		expect(mesh.root.positions.length).toBe(3 * mesh.vertexCounts[0]);
		// the vertex buffer holds positions, texture coordinates, colours and normals, one after the other
		const nv = mesh.vertexCounts[0];
		expect(gl.calls.find((c) => c.name === 'bufferData' && c.args[0] === gl.ARRAY_BUFFER).args[1]).toBe(nv * (12 + 8 + 4 + 6));

		mesh.release(context, 0);
		expect(mesh.status[0]).toBe(ABSENT);
		expect(context.cacheSize).toBe(0);
		expect(count(gl, 'deleteBuffer')).toBe(2);
		expect(count(gl, 'deleteTexture')).toBe(1);
		expect(mesh.texref[mesh.textureOf(0)]).toBe(0);
	});

	it('is not uploaded when it is dropped while it loads', async () => {
		let armed = false;
		const requests = [];
		const fetchSpy = (url, init) => {
			if (!armed) return fetch(url, init);
			const request = fetch(url, init);
			requests.push(request.catch((error) => error));
			return request;
		};
		const { gl, context, mesh, instance, errors } = await setup({ fetch: fetchSpy });
		const calls = gl.calls.length;
		armed = true;
		mesh.request(context, 1, instance);
		const textures = [...mesh.textureLoads.values()];
		mesh.release(context, 1);
		expect(mesh.status[1]).toBe(ABSENT);
		expect(context.pending).toBe(0);
		expect(context.cacheSize).toBe(0);
		expect(mesh.texref[mesh.textureOf(1)]).toBe(0);
		// the geometry is aborted; the texture is a request nobody waits for, which ends without being used
		const outcomes = await Promise.all(requests);
		expect(outcomes.filter((outcome) => outcome.name === 'AbortError').length).toBe(1);
		await Promise.allSettled(textures);
		await new Promise((resolve) => setImmediate(resolve)); // the continuations of the aborted request have run
		// nothing was created or uploaded (the release itself deleted the nothing there was)
		expect(gl.calls.slice(calls).map((c) => c.name).filter((name) => name !== 'deleteBuffer')).toEqual([]);
		expect(errors).toEqual([]);
		expect(mesh.isAvailable(1)).toBe(true);
	});

	it('is a failure that is reported, and not asked for again, when its stream is corrupt', async () => {
		const { context, mesh, instance, errors, settled } = await setup();
		const start = mesh.index.nodes.payloadOffset[2];
		const length = mesh.index.nodes.payloadLength[2];
		mesh.source.read = ((read) => async (from, to, options) => {
			const bytes = await read(from, to, options);
			return from === start ? new Uint8Array(length).buffer : bytes;
		})(mesh.source.read.bind(mesh.source));
		const done = settled(2);
		mesh.request(context, 2, instance);
		await done;

		expect(errors.length).toBe(1);
		expect(errors[0].node).toBe(2);
		expect(errors[0].error.code).toBe(ErrorCode.CORRUPT_NODE);
		expect(mesh.status[2]).toBe(ABSENT);
		expect(context.pending).toBe(0);
		expect(context.cacheSize).toBe(0);
		expect(mesh.texref[mesh.textureOf(2)]).toBe(0);
		expect(mesh.failedUntil[2]).toBe(Infinity);
		expect(mesh.isAvailable(2)).toBe(false);
	});

	it('is a corrupt texture when the image is not WebP', async () => {
		const { context, mesh, instance, errors, settled } = await setup();
		const tex = mesh.textureOf(3);
		const start = mesh.index.textures.offset[tex];
		mesh.source.read = ((read) => async (from, to, options) => {
			const bytes = await read(from, to, options);
			return from === start ? new Uint8Array(to - from).buffer : bytes;
		})(mesh.source.read.bind(mesh.source));
		const done = settled(3);
		mesh.request(context, 3, instance);
		await done;
		expect(errors[0].error.code).toBe(ErrorCode.CORRUPT_TEXTURE);
		expect(mesh.status[3]).toBe(ABSENT);
		expect(mesh.isAvailable(3)).toBe(false);
	});

	/** A fetch that fails the next `failures.left` requests for a range of the file (not the header) once armed. */
	function flakyFetch(failures) {
		return tamperedFetch(async (range) => {
			if (!failures.armed || failures.left === 0 || range === 'bytes=0-127') return null;
			failures.left--;
			failures.count++;
			throw new TypeError('network down');
		});
	}

	it('is requested again when the network fails', async () => {
		const failures = { armed: false, left: 2, count: 0 };
		const { context, mesh, instance, errors, next } = await setup({ fetch: flakyFetch(failures) });
		failures.armed = true;
		const arrival = next();
		mesh.request(context, 4, instance);
		await arrival;
		expect(failures.count).toBe(2);
		expect(mesh.status[4]).toBe(READY);
		expect(errors).toEqual([]);
	});

	it('is given up when the network stays down, and tried again later', async () => {
		const failures = { armed: false, left: 100, count: 0 };
		const { context, mesh, instance, errors, settled } = await setup({ fetch: flakyFetch(failures) });
		failures.armed = true;
		const done = settled(5);
		mesh.request(context, 5, instance);
		await done;
		// The geometry and the texture are requested at the same time, each is made again twice
		expect(failures.count).toBeGreaterThanOrEqual(3);
		expect(errors.length).toBe(1);
		expect(errors[0].error.name).toBe('TypeError');
		expect(mesh.status[5]).toBe(ABSENT);
		expect(mesh.isAvailable(5)).toBe(false);
		expect(Number.isFinite(mesh.failedUntil[5])).toBe(true);
		expect(context.pending).toBe(0);
		expect(context.cacheSize).toBe(0);
	});
});

describe('the cache rule', () => {
	/** A mesh that only records what is asked of it. */
	function stubMesh(status, errors) {
		const log = [];
		return {
			log,
			nodeCount: status.length,
			status: Uint8Array.from(status),
			errors: Float32Array.from(errors),
			frames: new Uint32Array(status.length),
			release: (context, id) => {
				log.push(`release ${id}`);
				context.cacheSize -= 100;
			},
			request: (context, id) => log.push(`request ${id}`),
		};
	}

	it('drops the node with the smallest error to make room for the one with the largest, and asks for it', () => {
		const context = new GlContext(fakeGl());
		const mesh = stubMesh([1, 1, 1, 0, 0], [5, 1, 3, 0, 0]);
		context.meshes.push(mesh);
		context.maxCacheSize = 250;
		context.cacheSize = 300;
		context.candidates = [{ id: 3, mesh, error: 2, frame: 0 }, { id: 4, mesh, error: 9, frame: 0 }];
		context.updateCache();
		expect(mesh.log).toEqual(['release 1', 'request 4']);
		expect(context.candidates).toEqual([]);
	});

	it('keeps the nodes that this frame needs more than the candidate', () => {
		const context = new GlContext(fakeGl());
		const mesh = stubMesh([1, 0], [5, 0]);
		context.meshes.push(mesh);
		context.maxCacheSize = 50;
		context.cacheSize = 100;
		context.candidates = [{ id: 1, mesh, error: 2, frame: 0 }];
		context.updateCache();
		expect(mesh.log).toEqual([]);
	});

	it('asks for no more than three nodes at a time', () => {
		const context = new GlContext(fakeGl());
		const mesh = stubMesh([0, 0], [0, 0]);
		context.pending = 3;
		context.candidates = [{ id: 0, mesh, error: 2, frame: 0 }];
		context.updateCache();
		expect(mesh.log).toEqual([]);
	});
});
