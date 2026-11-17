// SPDX-License-Identifier: MIT
//
// Derived from html/js/nexus_three.js (and nexus.js) of Nexus (https://github.com/cnr-isti-vclab/nexus, commit
// d805432), modified by Digipa for bm3d-viewer.
// Nexus: Copyright (c) 2012-2020, Visual Computing Lab, ISTI - CNR. All rights reserved.
// MIT licence; the licence text is in the LICENSE file of this package.

// The three.js object that draws a model: a Mesh with a placeholder geometry, whose draw hook
// hands the frame to the traversal and draws the nodes itself with the shader program three
// chose for the material.

import { Box3, BufferAttribute, BufferGeometry, DataTexture, DoubleSide, Matrix4, Mesh, MeshBasicMaterial, MeshLambertMaterial, Ray, RGBAFormat, Sphere, Vector2, Vector3 } from 'three';

import { Instance } from './instance.js';
import { principalAxes } from './shape.js';

function noCentering() {
	throw new Error('bm3d: centering, and in general applying a matrix to the geometry, is not supported.');
}

/**
 * The alpha test of a model whose textures cut texels out: a texel of the file is drawn (alpha 255) or
 * cut out (alpha 0), and between them, where the texture is filtered, the edge of the cutout is
 * where the alpha is half.
 */
export const CUTOUT_ALPHA_TEST = 0.5;

/** A 1x1 white texture: three needs a `map` to put a sampler in the shader; the nodes bring their own. */
function placeholderTexture() {
	const texture = new DataTexture(new Uint8Array([255, 255, 255, 255]), 1, 1, RGBAFormat);
	texture.needsUpdate = true;
	return texture;
}

/**
 * The material for what the model holds, when the caller gave none. It draws both sides of the
 * triangles: the winding of a mesh says which side is the front only when the program that wrote it
 * kept it consistent, and many do not (scans, terrain patches wound clockwise, meshes of mixed
 * winding), and an open surface is seen from behind as soon as the camera passes under it. Culling
 * the back faces would leave holes there; three.js turns the normal of a back face to the camera, so
 * it is lit as the front. The alpha test that draws the cutouts is set when the first texture that has
 * one arrives (see syncCutout).
 */
function createMaterial(mesh) {
	const Material = mesh.hasNormals ? MeshLambertMaterial : MeshBasicMaterial;
	const parameters = { color: 0xffffff, side: DoubleSide };
	if (mesh.hasColors) parameters.vertexColors = true;
	if (mesh.hasUvs) parameters.map = placeholderTexture();
	return new Material(parameters);
}

/**
 * Adds to the placeholder geometry the attributes the model has, so that the shader reads them. The
 * material the object makes reads the colours as red, green and blue: it draws the model opaque, and
 * the alpha of the vertices (the opacity of a material, which it does not blend) does not cut the
 * model out where the alpha test of a cutout texture runs. A material of the caller reads the alpha too.
 */
function addAttributes(geometry, mesh, colorAlpha) {
	if (mesh.hasNormals) geometry.setAttribute('normal', new BufferAttribute(new Float32Array(3), 3));
	if (mesh.hasUvs) geometry.setAttribute('uv', new BufferAttribute(new Float32Array(2), 2));
	const colorSize = colorAlpha ? 4 : 3;
	if (mesh.hasColors) geometry.setAttribute('color', new BufferAttribute(new Uint8Array(colorSize), colorSize, true));
}

/**
 * Draw hook of three, before it chooses the shader program of the frame: the material the object
 * made gets the alpha test once a texture with a cutout has arrived, so the nodes that bring it are
 * drawn with it from their first frame.
 */
function syncCutout() {
	const { instance } = this.geometry;
	if (this.autoMaterial && instance && instance.isReady && instance.mesh.hasCutout && this.material.alphaTest !== CUTOUT_ALPHA_TEST) {
		this.material.alphaTest = CUTOUT_ALPHA_TEST;
	}
}

/** Draw hook of three: runs after three drew the placeholder, with its shader program still in use. */
function afterRender(renderer, scene, camera, geometry) {
	const instance = geometry.instance;
	if (!instance || !instance.isReady) return;
	const gl = renderer.getContext();

	// The error is measured in the pixels of the screen, not in CSS pixels
	const size = renderer.getDrawingBufferSize(new Vector2());
	instance.updateView([0, 0, size.x, size.y], camera.projectionMatrix.elements, this.modelViewMatrix.elements);

	const program = gl.getParameter(gl.CURRENT_PROGRAM);
	const attr = instance.attributes;
	attr.position = gl.getAttribLocation(program, 'position');
	attr.normal = gl.getAttribLocation(program, 'normal');
	attr.color = gl.getAttribLocation(program, 'color');
	attr.uv = gl.getAttribLocation(program, 'uv');
	const mapLocation = gl.getUniformLocation(program, 'map');
	attr.map = mapLocation ? gl.getUniform(program, mapLocation) : 0;

	instance.context.frame = renderer.info.render.frame;
	instance.render();
	instance.context.updateCache();
}

/**
 * A bm3d model as a three.js object.
 *
 *   new Bm3dObject(url, onLoad, onUpdate, renderer, material, options)
 *
 * `onLoad(object)` is called when the model has been opened (its index read), `onUpdate()` when a
 * node has arrived (render again). `material` is optional: without one, a white Lambert (or basic,
 * for a model without normals) material is made that shows the vertex colours and the textures, on
 * both sides of the triangles, and cuts out the texels the textures cut out (see `hasCutout`).
 * `options`: decoderPath, cache, fetch, requestInit, onError (see the README).
 */
export class Bm3dObject extends Mesh {
	constructor(url, onLoad, onUpdate, renderer, material, options = {}) {
		const geometry = new BufferGeometry();
		geometry.center = noCentering;
		geometry.setAttribute('position', new BufferAttribute(new Float32Array(3), 3));
		super(geometry, material);

		this.autoMaterial = !material;
		this.frustumCulled = false;

		const instance = (this.geometry.instance = new Instance(renderer.getContext()));
		instance.onLoad = () => {
			const { mesh } = instance;
			const [x, y, z] = mesh.sphere.center;
			geometry.boundingSphere = new Sphere(new Vector3(x, y, z), mesh.sphere.radius);
			geometry.boundingBox = this.computeBoundingBox();
			addAttributes(geometry, mesh, !this.autoMaterial);
			if (this.autoMaterial) this.material = createMaterial(mesh);
			if (onLoad) onLoad(this);
		};
		instance.onUpdate = () => {
			if (onUpdate) onUpdate(this);
		};
		instance.onError = options.onError || null;
		instance.open(url, options);

		this.onBeforeRender = syncCutout;
		this.onAfterRender = afterRender;
	}

	/**
	 * Whether a texture of the model that has arrived cuts texels out (its alpha). The material the
	 * object makes draws them with an alpha test of CUTOUT_ALPHA_TEST; a material of the caller sets
	 * its own `alphaTest`.
	 */
	get hasCutout() {
		const { instance } = this.geometry;
		return Boolean(instance && instance.isReady && instance.mesh.hasCutout);
	}

	/** Frees what the model holds on the GPU and forgets it. */
	dispose() {
		const instance = this.geometry.instance;
		if (!instance) return;
		instance.close();
		this.geometry.instance = null;
		this.geometry.dispose();
	}

	/** Drops the nodes that are loaded (they are loaded again when the view needs them). */
	flush() {
		const { instance } = this.geometry;
		if (instance && instance.isReady) instance.mesh.flush(instance.context);
	}

	/** The box around the finest nodes, from their tight spheres. */
	computeBoundingBox() {
		const { instance } = this.geometry;
		if (!instance || !instance.isReady) return null;
		const mesh = instance.mesh;
		const min = new Vector3(Infinity, Infinity, Infinity);
		const max = new Vector3(-Infinity, -Infinity, -Infinity);
		for (let i = 0; i < mesh.sink; i++) {
			if (mesh.patches[mesh.firstPatch[i] * 3] !== mesh.sink) continue; // not a leaf
			const [x, y, z, , r] = mesh.spheres.subarray(i * 5, i * 5 + 5);
			min.set(Math.min(min.x, x - r), Math.min(min.y, y - r), Math.min(min.z, z - r));
			max.set(Math.max(max.x, x + r), Math.max(max.y, y + r), Math.max(max.z, z + r));
		}
		return new Box3(min, max);
	}

	/**
	 * The principal axes of the coarsest level (node 0), once it has arrived (null before), in the
	 * coordinates of the model: { center, axes, extents }, the axes from the longest extent to the
	 * shortest. A flat model has a last extent of almost 0, and its last axis is the normal of its
	 * plane: an application that frames the model can look along it rather than along the plane.
	 */
	computePrincipalAxes() {
		const { instance } = this.geometry;
		if (!instance || !instance.isReady || !instance.mesh.root) return null;
		return principalAxes(instance.mesh.root.positions);
	}

	/** Ray casting against the coarsest level (node 0), once it has arrived. */
	raycast(raycaster, intersects) {
		const { instance } = this.geometry;
		if (!instance || !instance.isReady || !instance.mesh.root) return;
		const mesh = instance.mesh;

		const [cx, cy, cz] = mesh.sphere.center;
		const sphere = new Sphere(new Vector3(cx, cy, cz), mesh.sphere.radius).applyMatrix4(this.matrixWorld);
		const point = new Vector3();
		if (!raycaster.ray.intersectSphere(sphere, point)) return;

		const ray = new Ray().copy(raycaster.ray).applyMatrix4(new Matrix4().copy(this.matrixWorld).invert());
		const { positions, indices } = mesh.root;
		const A = new Vector3();
		const B = new Vector3();
		const C = new Vector3();
		let distance = -1;
		let nearest = null;
		for (let j = 0; j < indices.length; j += 3) {
			const a = indices[j] * 3;
			const b = indices[j + 1] * 3;
			const c = indices[j + 2] * 3;
			A.set(positions[a], positions[a + 1], positions[a + 2]);
			B.set(positions[b], positions[b + 1], positions[b + 2]);
			C.set(positions[c], positions[c + 1], positions[c + 2]);
			// Back faces count; the material could say otherwise
			const hit = ray.intersectTriangle(C, B, A, false, point);
			if (!hit) continue;

			// the distances are those of the world
			hit.applyMatrix4(this.matrixWorld);
			const d = hit.distanceTo(raycaster.ray.origin);
			if (d < raycaster.near || d > raycaster.far) continue;
			if (distance === -1 || d < distance) {
				distance = d;
				nearest = hit.clone();
			}
		}
		if (distance === -1) return;
		intersects.push({ distance, point: nearest, object: this });
	}
}
