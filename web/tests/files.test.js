// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// Every .bm3d file of the fixtures, and of the golden files of the command line when there are some
// (BM3D_GOLDEN_DIR, by default tests/golden of the repository), is opened with every check of the
// format, its metadata is read, and each of its nodes and textures is decoded as a viewer would.

import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { checkTexture, openBm3d, readMetadata } from '../src/format.js';
import { createCortoDecoder } from '../src/corto-worker.js';
import { bufferSource } from '../src/source.js';

const here = (path) => fileURLToPath(new URL(path, import.meta.url));
const directories = [here('./fixtures'), process.env.BM3D_GOLDEN_DIR || here('../../tests/golden')].filter(existsSync);
const files = directories.flatMap((directory) => readdirSync(directory, { recursive: true }).filter((name) => name.endsWith('.bm3d')).map((name) => join(directory, name)));

describe.each(files.length ? files : [null])('%s', (path) => {
	it.skipIf(path === null)('opens, reads its metadata and decodes every node and texture', async () => {
		const bytes = new Uint8Array(readFileSync(path));
		const source = bufferSource(bytes);
		const file = await openBm3d(source);
		expect(file.sink).toBeGreaterThanOrEqual(1);

		const metadata = await readMetadata(source, file);
		if (file.metadataLength > 0) expect(metadata.generator).toMatch(/^bm3d /);

		const decoder = await createCortoDecoder(readFileSync(here('../corto-wasm/out/corto.wasm')));
		let vertices = 0;
		let faces = 0;
		for (let i = 0; i < file.sink; i++) {
			const start = file.nodes.payloadOffset[i];
			const payload = bytes.subarray(start, start + file.nodes.payloadLength[i]);
			const expected = { vertexCount: file.nodes.vertexCount[i], faceCount: file.nodes.faceCount[i], normal: file.hasNormals, color: file.hasColors, uv: file.hasUvs };
			const model = decoder.decode(payload, expected);
			expect(Math.max(...model.index)).toBeLessThan(expected.vertexCount);
			vertices += expected.vertexCount;
			faces += expected.faceCount;
		}
		// The totals of the header are those of the nodes
		expect(vertices).toBe(file.vertexCount);
		expect(faces).toBe(file.faceCount);

		for (let t = 0; t < file.textureCount; t++) {
			const start = file.textures.offset[t];
			checkTexture(bytes.subarray(start, start + file.textures.length[t]), t);
		}
	});
});
