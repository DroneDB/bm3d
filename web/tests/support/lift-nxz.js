// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// Makes a .bm3d file of the nodes of a format 0 file (the Nexus container, "bm3d 0.2.0 (format 0)"
// builds one): the corto stream of a node and the WebP image of a texture are the same in both
// formats, only the container differs. Used to make the committed fixtures while the command line
// still writes format 0; the fixtures of the command line itself (BM3D_GOLDEN_DIR) replace it.
//
//   node tests/support/lift-nxz.js model.nxz model.bm3d [source name]

import { readFileSync, writeFileSync } from 'node:fs';
import { basename } from 'node:path';
import { buildBm3d } from './writer.js';

const PADDING = 256;
const NO_TEXTURE = 0xffffffff;

export function liftNxz(file, sourceName) {
	const view = new DataView(file.buffer, file.byteOffset, file.byteLength);
	if (view.getUint32(0, true) !== 0x4e787320) throw new Error('not a format 0 (Nexus) file');
	const flags = view.getUint32(56, true);
	if ((flags & 4) === 0) throw new Error('the nodes are not corto streams');
	const nodeCount = view.getUint32(60, true);
	const patchCount = view.getUint32(64, true);
	const textureRows = view.getUint32(68, true); // a sink ends the table: the textures are one fewer
	const textureCount = Math.max(textureRows - 1, 0);
	const vertexSlot = (i) => [view.getUint8(24 + 2 * i), view.getUint8(24 + 2 * i + 1)];
	const normals = vertexSlot(1)[1] === 3;
	const colors = vertexSlot(2)[1] === 4;
	const sphere = { center: [0, 1, 2].map((i) => view.getFloat32(72 + 4 * i, true)), radius: view.getFloat32(84, true) };

	const nodeAt = (i) => 88 + 44 * i;
	const patchAt = (i) => 88 + 44 * nodeCount + 12 * i;
	const textureAt = (i) => 88 + 44 * nodeCount + 12 * patchCount + 68 * i;
	const nodeOffset = (i) => view.getUint32(nodeAt(i), true) * PADDING;
	const textureOffset = (i) => view.getUint32(textureAt(i), true) * PADDING;

	const nodes = [];
	for (let i = 0; i < nodeCount - 1; i++) {
		const at = nodeAt(i);
		const first = view.getUint32(at + 40, true);
		const next = view.getUint32(nodeAt(i + 1) + 40, true);
		const patches = [];
		for (let p = first; p < next; p++) {
			const child = view.getUint32(patchAt(p), true);
			const texture = view.getUint32(patchAt(p) + 8, true);
			patches.push({ node: child === nodeCount - 1 ? -1 : child, triangleOffset: view.getUint32(patchAt(p) + 4, true), texture: texture === NO_TEXTURE ? undefined : texture });
		}
		nodes.push({
			payload: file.slice(nodeOffset(i), nodeOffset(i + 1)),
			error: view.getFloat32(at + 8, true),
			vertexCount: view.getUint16(at + 4, true),
			faceCount: view.getUint16(at + 6, true),
			cone: [0, 1, 2, 3].map((k) => view.getInt16(at + 12 + 2 * k, true)),
			sphere: [0, 1, 2, 3].map((k) => view.getFloat32(at + 20 + 4 * k, true)),
			tightRadius: view.getFloat32(at + 36, true),
			patches,
		});
	}

	// A WebP file says its own size (bytes 4 to 7 of the RIFF header); the padding after it is not part of the image
	const textures = [];
	for (let i = 0; i < textureCount; i++) {
		const start = textureOffset(i);
		const length = 8 + new DataView(file.buffer, file.byteOffset + start + 4, 4).getUint32(0, true);
		textures.push(file.slice(start, start + length));
	}

	const metadata = { generator: 'bm3d 0.2.0', source: { name: sourceName, vertices: Number(view.getBigUint64(8, true)), faces: Number(view.getBigUint64(16, true)) } };
	return buildBm3d({ nodes, textures, normals, colors, sphere, metadata });
}

const [input, output, name] = process.argv.slice(2);
if (input && output) writeFileSync(output, liftNxz(readFileSync(input), name || basename(input)));
