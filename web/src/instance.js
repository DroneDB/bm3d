// SPDX-License-Identifier: MIT
//
// Derived from html/js/nexus.js of Nexus (https://github.com/cnr-isti-vclab/nexus, commit
// d805432), modified by Digipa for bm3d-viewer.
// Nexus: Copyright (c) 2012-2020, Visual Computing Lab, ISTI - CNR. All rights reserved.
// MIT licence; the licence text is in the LICENSE file of this package.

// A view of a model: where the camera is, which nodes it wants (the traversal), and the drawing
// of the nodes that are on the GPU.

import { getContext } from './context.js';
import { matInv, matMul } from './math.js';
import { PriorityQueue } from './priority-queue.js';
import { ABSENT, Bm3dMesh, READY } from './mesh.js';

const MAX_PENDING_REQUESTS = 3; // nodes asked for in one traversal
const MAX_BLOCKED = 3;
const DRAW_BUDGET = 5 * (1 << 20); // vertices (about) drawn in a frame

export class Instance {
	constructor(gl) {
		this.gl = gl;
		this.onLoad = () => {};
		this.onUpdate = null;
		this.onError = null;
		this.drawBudget = DRAW_BUDGET;
		// The locations of the attributes in the shader program, set by the host before render()
		this.attributes = { position: 0, normal: 1, color: 2, uv: 3, map: 0 };
		this.mesh = null;
		this.selected = null;
		this.visitQueue = null;
		this.nblocked = 0;
		this.sameResolution = false;
		this.currentResolution = 0;
	}

	/** Opens the model at `url`; `onLoad` is called when its index is known, `onError` when it cannot be opened. */
	open(url, options = {}) {
		const t = this;
		t.context = getContext(t.gl);

		t.modelMatrix = new Float32Array(16);
		t.viewMatrix = new Float32Array(16);
		t.projectionMatrix = new Float32Array(16);
		t.modelView = new Float32Array(16);
		t.modelViewInv = new Float32Array(16);
		t.modelViewProj = new Float32Array(16);
		t.modelViewProjInv = new Float32Array(16);
		t.planes = new Float32Array(24);
		t.viewport = new Float32Array(4);
		t.viewpoint = new Float32Array(4);

		// Instances of the same model on the same context share it
		t.mesh = t.context.meshes.find((m) => m.url === url) || null;
		if (!t.mesh) {
			t.mesh = new Bm3dMesh(url, options);
			t.context.meshes.push(t.mesh);
		}
		const fail = (error) => {
			const index = t.context.meshes.indexOf(t.mesh);
			if (index >= 0) t.context.meshes.splice(index, 1);
			if (t.onError) t.onError(error);
			else console.error(`bm3d: ${url} cannot be opened:`, error);
		};
		t.mesh.users++;
		t.mesh.ready.then(() => t.onLoad(), fail);
	}

	/** Stops showing the model; the last instance to go frees the model's nodes. */
	close() {
		const t = this;
		if (!t.mesh) return;
		if (--t.mesh.users === 0) {
			if (t.mesh.isReady) t.mesh.flush(t.context);
			const index = t.context.meshes.indexOf(t.mesh);
			if (index >= 0) t.context.meshes.splice(index, 1);
		}
		t.mesh = null;
	}

	get isReady() {
		return !!this.mesh && this.mesh.isReady;
	}

	get datasetRadius() {
		return this.isReady ? this.mesh.sphere.radius : 1.0;
	}

	get datasetCenter() {
		return this.isReady ? this.mesh.sphere.center : [0, 0, 0];
	}

	updateView(viewport, projection, modelView) {
		const t = this;

		for (let i = 0; i < 16; i++) {
			t.projectionMatrix[i] = projection[i];
			t.modelView[i] = modelView[i];
		}
		for (let i = 0; i < 4; i++) t.viewport[i] = viewport[i];

		matMul(t.projectionMatrix, t.modelView, t.modelViewProj);
		matInv(t.modelViewProj, t.modelViewProjInv);

		matInv(t.modelView, t.modelViewInv);
		t.viewpoint[0] = t.modelViewInv[12];
		t.viewpoint[1] = t.modelViewInv[13];
		t.viewpoint[2] = t.modelViewInv[14];
		t.viewpoint[3] = 1.0;

		const m = t.modelViewProj;
		const mi = t.modelViewProjInv;
		const p = t.planes;

		// frustum planes Ax + By + Cz + D = 0
		p[0] = m[0] + m[3]; p[1] = m[4] + m[7]; p[2] = m[8] + m[11]; p[3] = m[12] + m[15]; // left
		p[4] = -m[0] + m[3]; p[5] = -m[4] + m[7]; p[6] = -m[8] + m[11]; p[7] = -m[12] + m[15]; // right
		p[8] = m[1] + m[3]; p[9] = m[5] + m[7]; p[10] = m[9] + m[11]; p[11] = m[13] + m[15]; // bottom
		p[12] = -m[1] + m[3]; p[13] = -m[5] + m[7]; p[14] = -m[9] + m[11]; p[15] = -m[13] + m[15]; // top
		p[16] = -m[2] + m[3]; p[17] = -m[6] + m[7]; p[18] = -m[10] + m[11]; p[19] = -m[14] + m[15]; // near
		p[20] = -m[2] + m[3]; p[21] = -m[6] + m[7]; p[22] = -m[10] + m[11]; p[23] = -m[14] + m[15]; // far

		// normalise the planes to get also correct distances
		for (let i = 0; i < 24; i += 4) {
			const l = Math.sqrt(p[i] * p[i] + p[i + 1] * p[i + 1] + p[i + 2] * p[i + 2]);
			p[i] /= l; p[i + 1] /= l; p[i + 2] /= l; p[i + 3] /= l;
		}

		// the side is M'(1,0,0,1) - M'(-1,0,0,1), and they lie on the planes
		const r3 = mi[3] + mi[15];
		const r0 = (mi[0] + mi[12]) / r3;
		const r1 = (mi[1] + mi[13]) / r3;
		const r2 = (mi[2] + mi[14]) / r3;

		const l3 = -mi[3] + mi[15];
		const l0 = (-mi[0] + mi[12]) / l3 - r0;
		const l1 = (-mi[1] + mi[13]) / l3 - r1;
		const l2 = (-mi[2] + mi[14]) / l3 - r2;

		const side = Math.sqrt(l0 * l0 + l1 * l1 + l2 * l2);

		// the centre of the scene is M'*(0, 0, 0, 1)
		const c0 = mi[12] / mi[15] - t.viewpoint[0];
		const c1 = mi[13] / mi[15] - t.viewpoint[1];
		const c2 = mi[14] / mi[15] - t.viewpoint[2];
		const dist = Math.sqrt(c0 * c0 + c1 * c1 + c2 * c2);

		// The length of a pixel at distance 1, in the pixels of the viewport it is given
		const resolution = (2 * side / dist) / t.viewport[2];
		t.sameResolution = t.currentResolution === resolution;
		t.currentResolution = resolution;
	}

	traversal() {
		const t = this;
		const mesh = t.mesh;
		const n = mesh.nodeCount;

		if (t.sameResolution && t.selected !== null) {
			// Nothing to do when the view did not change, every node chosen is there and none is waited for
			let allOk = true;
			for (let i = 0; i < n; i++) {
				if (t.selected[i] && mesh.status[i] === ABSENT) {
					allOk = false;
					break;
				}
			}
			if (allOk && !t.visitQueue.size && !t.nblocked) return;
		}

		t.selected = new Uint8Array(n);
		t.visited = new Uint8Array(n);
		t.blocked = new Uint8Array(n);

		t.visitQueue = new PriorityQueue(n);
		for (let i = 0; i < mesh.rootCount; i++) t.insertNode(i);

		t.currentError = t.context.currentError;
		t.drawSize = 0;
		t.nblocked = 0;

		let requested = 0;
		while (t.visitQueue.size && t.nblocked < MAX_BLOCKED) {
			const error = t.visitQueue.error[0];
			const node = t.visitQueue.pop();
			if (requested < MAX_PENDING_REQUESTS && mesh.status[node] === ABSENT && mesh.isAvailable(node)) {
				t.context.candidates.push({ id: node, instance: t, mesh, frame: t.context.frame, error });
				requested++;
			}
			const selected = t.expandNode(node, error);
			const blocked = t.blocked[node] || !selected;

			if (blocked) t.nblocked++;
			else t.selected[node] = 1;
			t.insertChildren(node, blocked);
		}
	}

	insertNode(node) {
		const t = this;
		t.visited[node] = 1;

		const error = t.nodeError(node);
		if (node > 0 && error < t.currentError) return;

		const { errors, frames } = t.mesh;
		if (frames[node] !== t.context.frame || errors[node] < error) {
			errors[node] = error;
			frames[node] = t.context.frame;
		}
		t.visitQueue.push(node, error);
	}

	insertChildren(node, block) {
		const t = this;
		const { firstPatch, patches, sink } = t.mesh;
		for (let i = firstPatch[node]; i < firstPatch[node + 1]; ++i) {
			const child = patches[i * 3];
			if (child === sink) return;
			if (block) t.blocked[child] = 1;
			if (!t.visited[child]) t.insertNode(child);
		}
	}

	expandNode(node, error) {
		const t = this;
		if (node > 0 && error < t.currentError) return false;
		if (t.drawSize > t.drawBudget) return false;
		if (t.mesh.status[node] !== READY) return false;

		const sp = t.mesh.spheres;
		const off = node * 5;
		// The expanded radius; half of the new faces are added (and the vertices are used, so twice the faces)
		if (t.isVisible(sp[off], sp[off + 1], sp[off + 2], sp[off + 3])) t.drawSize += t.mesh.vertexCounts[node] * 0.8;
		return true;
	}

	/** The error of node `n` in pixels, as seen from the viewpoint; `tight` for the radius of its own geometry. */
	nodeError(n, tight) {
		const t = this;
		const spheres = t.mesh.spheres;
		const b = t.viewpoint;
		const off = n * 5;
		const cx = spheres[off];
		const cy = spheres[off + 1];
		const cz = spheres[off + 2];
		const r = tight ? spheres[off + 4] : spheres[off + 3];
		const d0 = b[0] - cx;
		const d1 = b[1] - cy;
		const d2 = b[2] - cz;
		let dist = Math.sqrt(d0 * d0 + d1 * d1 + d2 * d2) - r;
		if (dist < 0.1) dist = 0.1;

		// the resolution is how long a pixel is at distance 1
		let error = t.mesh.geometricError[n] / (t.currentResolution * dist);

		if (!t.isVisible(cx, cy, cz, spheres[off + 4])) error /= 1000.0;
		return error;
	}

	isVisible(x, y, z, r) {
		const p = this.planes;
		for (let i = 0; i < 24; i += 4) {
			if (p[i] * x + p[i + 1] * y + p[i + 2] * z + p[i + 3] + r < 0) return false; // the plane is ax+by+cz+d = 0
		}
		return true;
	}

	renderNodes() {
		const t = this;
		const m = t.mesh;
		const gl = t.gl;
		const attr = t.attributes;

		const vertexEnabled = gl.getVertexAttrib(attr.position, gl.VERTEX_ATTRIB_ARRAY_ENABLED);
		const normalEnabled = attr.normal >= 0 ? gl.getVertexAttrib(attr.normal, gl.VERTEX_ATTRIB_ARRAY_ENABLED) : false;
		const colorEnabled = attr.color >= 0 ? gl.getVertexAttrib(attr.color, gl.VERTEX_ATTRIB_ARRAY_ENABLED) : false;
		const uvEnabled = attr.uv >= 0 ? gl.getVertexAttrib(attr.uv, gl.VERTEX_ATTRIB_ARRAY_ENABLED) : false;

		let rendered = 0;
		let lastTexture = -1;

		t.realError = 0.0;
		for (let n = 0; n < m.nodeCount; n++) {
			if (!t.selected[n]) continue;

			// A node whose patches are all drawn by their children has nothing to draw
			let skip = true;
			for (let p = m.firstPatch[n]; p < m.firstPatch[n + 1]; p++) {
				if (!t.selected[m.patches[p * 3]]) {
					skip = false;
					break;
				}
			}
			if (skip) continue;

			const sp = m.spheres;
			const off = n * 5;
			if (!t.isVisible(sp[off], sp[off + 1], sp[off + 2], sp[off + 4])) continue; // the tight radius

			t.realError = Math.max(t.nodeError(n, true), t.realError);

			gl.bindBuffer(gl.ARRAY_BUFFER, m.vbo[n]);
			gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, m.ibo[n]);

			gl.vertexAttribPointer(attr.position, 3, gl.FLOAT, false, 12, 0);
			gl.enableVertexAttribArray(attr.position);

			// The vertex buffer holds the arrays one after the other (uploadNode)
			const nv = m.vertexCounts[n];
			let offset = nv * 12;
			if (m.hasUvs) {
				if (attr.uv >= 0) {
					gl.vertexAttribPointer(attr.uv, 2, gl.FLOAT, false, 8, offset);
					gl.enableVertexAttribArray(attr.uv);
				}
				offset += nv * 8;
			}
			if (m.hasColors) {
				if (attr.color >= 0) {
					gl.vertexAttribPointer(attr.color, 4, gl.UNSIGNED_BYTE, true, 4, offset);
					gl.enableVertexAttribArray(attr.color);
				}
				offset += nv * 4;
			}
			if (m.hasNormals && attr.normal >= 0) {
				gl.vertexAttribPointer(attr.normal, 3, gl.SHORT, true, 6, offset);
				gl.enableVertexAttribArray(attr.normal);
			}

			// Draw the triangle ranges of the patches whose child is not drawn, joining neighbouring ranges
			let start = 0;
			let end = 0;
			const last = m.firstPatch[n + 1] - 1;
			for (let p = m.firstPatch[n]; p < m.firstPatch[n + 1]; ++p) {
				const child = m.patches[p * 3];

				if (!t.selected[child]) {
					end = m.patches[p * 3 + 1];
					if (p < last) continue;
				}
				if (end > start) {
					if (m.hasUvs) {
						const texid = m.patches[p * 3 + 2];
						if (texid !== lastTexture) {
							gl.activeTexture(gl.TEXTURE0 + attr.map);
							gl.bindTexture(gl.TEXTURE_2D, m.texids[texid]);
							lastTexture = texid;
						}
					}
					gl.drawElements(gl.TRIANGLES, (end - start) * 3, gl.UNSIGNED_SHORT, start * 6);
					rendered += end - start;
				}
				start = m.patches[p * 3 + 1];
			}
		}

		t.context.rendered += rendered;
		t.context.realError = Math.max(t.context.realError, t.realError);

		if (!vertexEnabled) gl.disableVertexAttribArray(attr.position);
		if (!normalEnabled && attr.normal >= 0) gl.disableVertexAttribArray(attr.normal);
		if (!colorEnabled && attr.color >= 0) gl.disableVertexAttribArray(attr.color);
		if (!uvEnabled && attr.uv >= 0) gl.disableVertexAttribArray(attr.uv);

		gl.bindBuffer(gl.ARRAY_BUFFER, null);
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);
	}

	render() {
		if (!this.isReady) return;
		this.traversal();
		this.renderNodes();
	}
}
