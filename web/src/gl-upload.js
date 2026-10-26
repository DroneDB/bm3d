// SPDX-License-Identifier: MIT
//
// Derived from html/js/nexus.js of Nexus (https://github.com/cnr-isti-vclab/nexus, commit
// d805432), modified by Digipa for bm3d-viewer.
// Nexus: Copyright (c) 2012-2020, Visual Computing Lab, ISTI - CNR. All rights reserved.
// MIT licence; the licence text is in the LICENSE file of this package.

// What goes to the GPU: the vertices and triangles of a node, and the image of a texture.

const MAX_ANISOTROPY = 8;

/**
 * Puts a decoded node (`model`, as corto-worker.js returns it) in two buffers. The vertex buffer
 * holds the arrays one after the other, which is the layout renderNodes points the attributes at:
 * positions, then texture coordinates, colours and normals, each only when the layout has it.
 * The buffer bindings are put back as they were: a node arrives between two frames, while the host
 * has its own vertex array bound, and the element buffer binding belongs to that vertex array.
 */
export function uploadNode(gl, model) {
	const previousArray = gl.getParameter(gl.ARRAY_BUFFER_BINDING);
	const previousElements = gl.getParameter(gl.ELEMENT_ARRAY_BUFFER_BINDING);
	const vertexBytes = model.position.byteLength + (model.uv ? model.uv.byteLength : 0) + (model.color ? model.color.byteLength : 0) + (model.normal ? model.normal.byteLength : 0);
	const vbo = gl.createBuffer();
	gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
	gl.bufferData(gl.ARRAY_BUFFER, vertexBytes, gl.STATIC_DRAW);
	let offset = 0;
	for (const array of [model.position, model.uv, model.color, model.normal]) {
		if (!array) continue;
		gl.bufferSubData(gl.ARRAY_BUFFER, offset, array);
		offset += array.byteLength;
	}
	const ibo = gl.createBuffer();
	gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
	gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, model.index, gl.STATIC_DRAW);
	gl.bindBuffer(gl.ARRAY_BUFFER, previousArray);
	gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, previousElements);
	return { vbo, ibo };
}

function isPowerOfTwo(n) {
	return n && (n & (n - 1)) === 0;
}

/**
 * Makes a texture of a decoded image (anything texImage2D takes). The image is flipped, because
 * the texture coordinates of the format have v = 0 at the bottom row (docs/FORMAT.md section 3.2).
 * Minified with trilinear filtering and up to MAX_ANISOTROPY x anisotropy, so that a model seen from
 * afar or at a slant is smooth instead of grainy.
 */
export function uploadTexture(gl, image) {
	const previous = gl.getParameter(gl.TEXTURE_BINDING_2D);
	const flip = gl.getParameter(gl.UNPACK_FLIP_Y_WEBGL);
	gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
	const texture = gl.createTexture();
	gl.bindTexture(gl.TEXTURE_2D, texture);

	gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);

	// WebGL 1 makes mipmaps of power-of-two images only
	const webgl1 = typeof WebGLRenderingContext !== 'undefined' && gl instanceof WebGLRenderingContext;
	const mipmaps = !webgl1 || (isPowerOfTwo(image.width) && isPowerOfTwo(image.height));
	if (mipmaps) {
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
		const anisotropy = gl.getExtension('EXT_texture_filter_anisotropic');
		if (anisotropy) {
			gl.texParameterf(gl.TEXTURE_2D, anisotropy.TEXTURE_MAX_ANISOTROPY_EXT, Math.min(MAX_ANISOTROPY, gl.getParameter(anisotropy.MAX_TEXTURE_MAX_ANISOTROPY_EXT)));
		}
		gl.generateMipmap(gl.TEXTURE_2D);
	} else {
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
	}

	gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, flip);
	gl.bindTexture(gl.TEXTURE_2D, previous);
	return texture;
}

/** Decodes the bytes of a WebP image with the browser's decoder. */
export function decodeImage(bytes) {
	const url = URL.createObjectURL(new Blob([bytes], { type: 'image/webp' }));
	const image = new Image();
	image.src = url;
	return image.decode().then(
		() => {
			URL.revokeObjectURL(url);
			return image;
		},
		(error) => {
			URL.revokeObjectURL(url);
			throw error;
		},
	);
}
