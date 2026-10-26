// SPDX-License-Identifier: MIT
//
// Derived from html/js/nexus.js of Nexus (https://github.com/cnr-isti-vclab/nexus, commit
// d805432), modified by Digipa for bm3d-viewer.
// Nexus: Copyright (c) 2012-2020, Visual Computing Lab, ISTI - CNR. All rights reserved.
// MIT licence; the licence text is in the LICENSE file of this package.

// What the meshes drawn on one WebGL context share: the budget of memory for nodes, the number of
// requests in flight, the error the view accepts, and the rule that decides which node to load next
// and which to drop to make room. Nodes of different contexts are never shared.

const TARGET_ERROR = 2.0; // in pixels: the error does not go lower than this if the view reaches it
const MAX_CACHE_SIZE = 512 * (1 << 20); // bytes of nodes kept (an estimate: see Bm3dMesh)
export const MAX_PENDING = 3; // nodes being loaded at the same time

export class GlContext {
	constructor(gl) {
		this.gl = gl;
		this.meshes = [];
		this.frame = 0; // the number of the frame being drawn, set by the host
		this.cacheSize = 0;
		this.candidates = [];
		this.pending = 0;
		this.maxCacheSize = MAX_CACHE_SIZE;
		this.targetError = TARGET_ERROR;
		this.currentError = TARGET_ERROR;
		this.rendered = 0;
		this.realError = 0;
	}

	/**
	 * Called after a frame, and whenever a node arrives: loads the candidate with the largest error
	 * (the traversal listed those it wanted and does not have), dropping the nodes with the smallest
	 * error to stay within the budget, as long as fewer than MAX_PENDING nodes are in flight.
	 */
	updateCache() {
		let best = null;
		for (const candidate of this.candidates) {
			if (candidate.mesh.status[candidate.id] === 0 && (!best || candidate.error > best.error)) best = candidate;
		}
		this.candidates = [];
		if (!best) return;

		while (this.cacheSize > this.maxCacheSize) {
			let worst = null;
			for (const mesh of this.meshes) {
				for (let i = 0; i < mesh.nodeCount; i++) {
					if (mesh.status[i] === 1 && (!worst || mesh.errors[i] < worst.error)) worst = { error: mesh.errors[i], frame: mesh.frames[i], mesh, id: i };
				}
			}
			// Nothing to drop, or only nodes the current frame needs more than the candidate
			if (!worst || (worst.error >= best.error && worst.frame === best.frame)) return;
			worst.mesh.release(this, worst.id);
		}

		if (this.pending < MAX_PENDING) {
			best.mesh.request(this, best.id, best.instance);
		}
	}
}

const contexts = new WeakMap();

/** The state shared by everything drawn on `gl`. */
export function getContext(gl) {
	if (!gl.isTexture) throw new TypeError('bm3d: not a WebGL rendering context');
	let context = contexts.get(gl);
	if (!context) {
		context = new GlContext(gl);
		contexts.set(gl, context);
	}
	return context;
}

export const getTargetError = (gl) => getContext(gl).targetError;
export const getMaxCacheSize = (gl) => getContext(gl).maxCacheSize;

export function setTargetError(gl, error) {
	const context = getContext(gl);
	context.targetError = error;
	context.currentError = error;
}

export function setMaxCacheSize(gl, bytes) {
	getContext(gl).maxCacheSize = bytes;
}
