// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// Small models for the tests of the reader. The payloads are not corto streams (nothing here decodes
// them), the textures only look like WebP files (RIFF....WEBP).

import { buildBm3d } from './writer.js';

/** `length` bytes that start like a WebP file. */
export function fakeWebp(length = 40) {
	const bytes = new Uint8Array(length).fill(0x55);
	bytes.set(new TextEncoder().encode('RIFF'), 0);
	new DataView(bytes.buffer).setUint32(4, length - 8, true);
	bytes.set(new TextEncoder().encode('WEBP'), 8);
	return bytes;
}

function fakePayload(seed, length) {
	return Uint8Array.from({ length }, (_, i) => (seed * 31 + i * 7) & 0xff);
}

/**
 * Five real nodes in a hierarchy that is a DAG (nodes 3 and 4 refine both 1 and 2) and the sink:
 *   0 -> 1, 2      1 -> 3, 4      2 -> 3, 4      3, 4 leaves
 * With `textures`, three textures (nodes 1 and 2 share the second). The sizes of the payloads are
 * odd on purpose, so that the padding to 8 is exercised.
 */
export function modelSpec({ textures = true, colors = false, normals = true } = {}) {
	const tex = (n) => (textures ? n : undefined);
	const node = (i, error, vertexCount, faceCount, patches) => ({
		payload: fakePayload(i, 37 + 11 * i),
		error,
		vertexCount,
		faceCount,
		cone: [0, 0, 32766, 32760],
		sphere: [0, 0, 0, 1],
		tightRadius: i === 0 ? error : 1 - 0.1 * i,
		patches,
	});
	return {
		nodes: [
			node(0, 1, 10, 6, [{ node: 1, triangleOffset: 3, texture: tex(0) }, { node: 2, triangleOffset: 6, texture: tex(0) }]),
			node(1, 0.5, 12, 8, [{ node: 3, triangleOffset: 4, texture: tex(1) }, { node: 4, triangleOffset: 8, texture: tex(1) }]),
			node(2, 0.5, 12, 8, [{ node: 3, triangleOffset: 8, texture: tex(1) }]),
			node(3, 0.25, 20, 12, [{ node: -1, triangleOffset: 12, texture: tex(2) }]),
			node(4, 0.25, 20, 12, [{ node: -1, triangleOffset: 5, texture: tex(2) }, { node: -1, triangleOffset: 12, texture: tex(2) }]),
		],
		textures: textures ? [fakeWebp(40), fakeWebp(53), fakeWebp(64)] : [],
		normals,
		colors,
		sphere: { center: [0, 0, 0], radius: 1.5 },
		metadata: { generator: 'bm3d 1.0.0', source: { name: 'test.obj', vertices: 94, faces: 56 } },
	};
}

export function model(options, edit) {
	const spec = modelSpec(options);
	if (edit) edit(spec);
	return buildBm3d(spec);
}
