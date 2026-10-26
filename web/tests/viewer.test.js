// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa

import { afterEach, describe, expect, it } from 'vitest';
import { Matrix4, PerspectiveCamera } from 'three';

import { Bm3dObject } from '../src/bm3d-object.js';
import { getContext, setTargetError } from '../src/context.js';
import { uploadTexture } from '../src/gl-upload.js';
import { Instance } from '../src/instance.js';
import { READY } from '../src/mesh.js';
import { matInv, matMul } from '../src/math.js';
import { PriorityQueue } from '../src/priority-queue.js';
import { fakeGl } from './support/fake-gl.js';
import { model, modelSpec } from './support/models.js';
import { startServer } from './support/server.js';
import { buildBm3d } from './support/writer.js';

const servers = [];
async function serve(bytes) {
	const server = await startServer((path) => (path === '/model.bm3d' ? { data: bytes } : null));
	servers.push(server);
	return `${server.url}/model.bm3d`;
}
afterEach(async () => {
	await Promise.all(servers.splice(0).map((server) => server.close()));
});

describe('PriorityQueue', () => {
	it('pops the largest error first', () => {
		const queue = new PriorityQueue(100);
		const errors = Array.from({ length: 100 }, (_, i) => ((i * 37) % 101) / 7);
		errors.forEach((error, i) => queue.push(i, error));
		const popped = [];
		while (queue.size) {
			popped.push(queue.error[0]);
			queue.pop();
		}
		expect(popped).toEqual(popped.slice().sort((a, b) => b - a).map(Math.fround));
		expect(popped.length).toBe(100);
	});
});

describe('matrices', () => {
	it('multiplies and inverts as three.js does', () => {
		const a = new Matrix4().makeRotationFromEuler({ x: 0.3, y: 0.5, z: 0.1, order: 'XYZ', isEuler: true }).setPosition(1, 2, 3);
		const b = new Matrix4().makeScale(2, 3, 4).setPosition(-1, 0, 5);
		const product = new Float32Array(16);
		matMul(a.elements, b.elements, product);
		const expected = new Matrix4().multiplyMatrices(a, b).elements;
		for (let i = 0; i < 16; i++) expect(product[i]).toBeCloseTo(expected[i], 4);

		const inverse = new Float32Array(16);
		matInv(a.elements, inverse);
		const identity = new Float32Array(16);
		matMul(a.elements, inverse, identity);
		for (let i = 0; i < 16; i++) expect(identity[i]).toBeCloseTo(i % 5 === 0 ? 1 : 0, 5);
	});
});

/** A camera looking at the origin from (0, 0, distance), and the model view matrix of an object at the origin. */
function view(distance) {
	const camera = new PerspectiveCamera(45, 1, 0.1, 10000);
	camera.position.set(0, 0, distance);
	camera.updateMatrixWorld();
	return { camera, modelView: camera.matrixWorldInverse.clone() };
}

async function openInstance(bytes) {
	const gl = fakeGl();
	const instance = new Instance(gl);
	const url = await serve(bytes);
	const loaded = new Promise((resolve, reject) => {
		instance.onLoad = resolve;
		instance.onError = reject;
	});
	instance.open(url);
	await loaded;
	return instance;
}

function look(instance, distance, pixels = 800) {
	const { camera, modelView } = view(distance);
	instance.updateView([0, 0, pixels, pixels], camera.projectionMatrix.elements, modelView.elements);
	instance.traversal();
}

describe('the traversal', () => {
	it('chooses the nodes by their error in pixels', async () => {
		const instance = await openInstance(model({ textures: false }));
		instance.mesh.status.fill(READY);
		setTargetError(instance.gl, 2);

		look(instance, 1000); // far: the root alone is more precise than a pixel can tell
		expect(Array.from(instance.selected)).toEqual([1, 0, 0, 0, 0, 0]);

		look(instance, 4); // near: the leaves
		expect(Array.from(instance.selected).slice(0, 5)).toEqual([1, 1, 1, 1, 1]);
	});

	it('asks for the nodes it lacks, the larger error first', async () => {
		const instance = await openInstance(model({ textures: false }));
		const context = getContext(instance.gl);
		look(instance, 4);
		expect(context.candidates.map((c) => c.id)).toContain(0);
		expect(context.candidates.length).toBeLessThanOrEqual(3);
		for (const candidate of context.candidates) expect(candidate.mesh.status[candidate.id]).toBe(0);
	});

	it('measures the error in the pixels of the screen it is given', async () => {
		const instance = await openInstance(model({ textures: false }));
		instance.mesh.status.fill(READY);
		setTargetError(instance.gl, 2);
		// A distance where the children are one pixel short of the target error in an 800 pixel view...
		look(instance, 160, 800);
		const coarse = instance.selected.reduce((sum, s) => sum + s, 0);
		// ... and over it with twice the pixels, as a screen with a device pixel ratio of 2 has
		look(instance, 160, 1600);
		const fine = instance.selected.reduce((sum, s) => sum + s, 0);
		expect(fine).toBeGreaterThan(coarse);
	});
});

/** A renderer as far as Bm3dObject uses one. */
function fakeRenderer(gl, { css = [800, 800], pixels = [1600, 1600] } = {}) {
	return {
		getContext: () => gl,
		getSize: (target) => target.set(...css),
		getDrawingBufferSize: (target) => target.set(...pixels),
		info: { render: { frame: 7 } },
	};
}

async function loadObject(bytes, material, renderer = fakeRenderer(fakeGl())) {
	const url = await serve(bytes);
	const object = await new Promise((resolve, reject) => new Bm3dObject(url, resolve, null, renderer, material, { onError: reject }));
	return { object, renderer };
}

describe('Bm3dObject', () => {
	it('measures the view in device pixels', async () => {
		const { object, renderer } = await loadObject(model({ textures: false }));
		const { camera } = view(100);
		object.updateMatrixWorld();
		object.modelViewMatrix.multiplyMatrices(camera.matrixWorldInverse, object.matrixWorld);
		object.onAfterRender(renderer, null, camera, object.geometry, object.material);
		expect(Array.from(object.geometry.instance.viewport)).toEqual([0, 0, 1600, 1600]);
		expect(getContext(renderer.getContext()).frame).toBe(7);
	});

	it('makes a material that shows vertex colours as unsigned bytes and a texture, without the removed three.js constants', async () => {
		const { object } = await loadObject(model({ textures: true, colors: true }));
		expect(object.material.vertexColors).toBe(true);
		expect(object.material.map.format).toBe(1023); // RGBAFormat
		const color = object.geometry.getAttribute('color');
		expect(color.array).toBeInstanceOf(Uint8Array);
		expect(color.normalized).toBe(true);
		expect(color.itemSize).toBe(4);
		expect(object.geometry.getAttribute('uv').itemSize).toBe(2);
		expect(object.geometry.getAttribute('normal').itemSize).toBe(3);
		expect(object.geometry.boundingSphere.radius).toBeCloseTo(1.5);
	});

	it('uses the material it is given, and a basic one for a model without normals', async () => {
		const given = { isMaterial: true };
		expect((await loadObject(model({ textures: false }), given)).object.material).toBe(given);
		const { object } = await loadObject(model({ textures: false, normals: false }));
		expect(object.material.isMeshBasicMaterial).toBe(true);
		expect(object.geometry.getAttribute('normal')).toBeUndefined();
	});

	it('has a bounding box around the leaves', async () => {
		const spec = modelSpec({ textures: false });
		spec.nodes[3].sphere = [5, 0, 0, 1];
		spec.nodes[3].tightRadius = 0.5;
		spec.nodes[4].sphere = [-2, 1, 0, 1];
		spec.nodes[4].tightRadius = 0.5;
		const { object } = await loadObject(buildBm3d(spec));
		const box = object.computeBoundingBox();
		expect(box.min.toArray()).toEqual([-2.5, -0.5, -0.5]);
		expect(box.max.toArray()).toEqual([5.5, 1.5, 0.5]);
	});

	it('reports a file it cannot open', async () => {
		const bytes = model();
		bytes[0] = 0;
		const url = await serve(bytes);
		const error = await new Promise((resolve) => new Bm3dObject(url, null, null, fakeRenderer(fakeGl()), undefined, { onError: resolve }));
		expect(error.code).toBe('bad magic');
	});

	it('frees the nodes of a model that is disposed', async () => {
		const gl = fakeGl();
		const { object } = await loadObject(model({ textures: false }), undefined, fakeRenderer(gl));
		const mesh = object.geometry.instance.mesh;
		mesh.status[2] = READY;
		mesh.vbo[2] = {};
		mesh.ibo[2] = {};
		getContext(gl).cacheSize = mesh.nsize[2];
		object.dispose();
		expect(gl.calls.filter((c) => c.name === 'deleteBuffer').length).toBe(2);
		expect(getContext(gl).cacheSize).toBe(0);
		expect(getContext(gl).meshes).toEqual([]);
	});

	it('keeps the model while another object shows it', async () => {
		const gl = fakeGl();
		const renderer = fakeRenderer(gl);
		const url = await serve(model({ textures: false }));
		const open = () => new Promise((resolve) => new Bm3dObject(url, resolve, null, renderer));
		const [one, two] = await Promise.all([open(), open()]);
		expect(getContext(gl).meshes.length).toBe(1);
		one.dispose();
		expect(getContext(gl).meshes.length).toBe(1);
		two.dispose();
		expect(getContext(gl).meshes.length).toBe(0);
	});
});

describe('the texture filter', () => {
	const image = { width: 300, height: 200 };
	const filter = (gl, parameter) => gl.calls.filter((c) => c.name === 'texParameteri' && c.args[1] === gl[parameter]);

	it('is trilinear, with up to 8x anisotropy', () => {
		const extension = { TEXTURE_MAX_ANISOTROPY_EXT: 0x84fe, MAX_TEXTURE_MAX_ANISOTROPY_EXT: 0x84ff };
		for (const [supported, used] of [[16, 8], [4, 4]]) {
			const gl = fakeGl({ returns: { getExtension: (name) => (name === 'EXT_texture_filter_anisotropic' ? extension : null), getParameter: (p) => (p === extension.MAX_TEXTURE_MAX_ANISOTROPY_EXT ? supported : 0) } });
			uploadTexture(gl, image);
			expect(filter(gl, 'TEXTURE_MIN_FILTER').map((c) => c.args[2])).toEqual([gl.LINEAR_MIPMAP_LINEAR]);
			expect(gl.calls.filter((c) => c.name === 'texParameterf').map((c) => c.args)).toEqual([[gl.TEXTURE_2D, extension.TEXTURE_MAX_ANISOTROPY_EXT, used]]);
			expect(gl.calls.some((c) => c.name === 'generateMipmap')).toBe(true);
		}
	});

	it('is trilinear without the extension', () => {
		const gl = fakeGl({ returns: { getExtension: null } });
		uploadTexture(gl, image);
		expect(filter(gl, 'TEXTURE_MIN_FILTER').map((c) => c.args[2])).toEqual([gl.LINEAR_MIPMAP_LINEAR]);
		expect(gl.calls.some((c) => c.name === 'texParameterf')).toBe(false);
	});

	it('flips the image and puts the texture binding back', () => {
		const previous = {};
		const gl = fakeGl({ returns: { getParameter: (p) => (p === gl.TEXTURE_BINDING_2D ? previous : false), getExtension: null } });
		uploadTexture(gl, image);
		const flips = gl.calls.filter((c) => c.name === 'pixelStorei').map((c) => c.args[1]);
		expect(flips).toEqual([true, false]);
		expect(gl.calls.at(-1)).toEqual({ name: 'bindTexture', args: [gl.TEXTURE_2D, previous] });
	});

	it('does not make mipmaps of an image that WebGL 1 cannot', () => {
		class WebGLRenderingContext {}
		globalThis.WebGLRenderingContext = WebGLRenderingContext;
		try {
			const gl = fakeGl();
			Object.setPrototypeOf(gl, WebGLRenderingContext.prototype);
			uploadTexture(gl, image);
			expect(filter(gl, 'TEXTURE_MIN_FILTER').map((c) => c.args[2])).toEqual([gl.LINEAR]);
		} finally {
			delete globalThis.WebGLRenderingContext;
		}
	});
});
