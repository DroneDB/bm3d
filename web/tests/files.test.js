// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// Every .bm3d file of the fixtures, and of the golden files of the command line when there are some
// (BM3D_GOLDEN_DIR, by default tests/golden of the repository), is opened with every check of the
// format, its metadata is read, and each of its nodes and textures is decoded as a viewer would.
// Its content hash is the one in <name>.sha256 beside it, and the one tests/golden/hashes.txt has
// for its name: the fixtures are what the command line writes today.

import { createHash } from 'node:crypto';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { basename, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

import { checkTexture, openBm3d, readMetadata } from '../src/format.js';
import { createCortoDecoder } from '../src/corto-worker.js';
import { bufferSource } from '../src/source.js';

const here = (path) => fileURLToPath(new URL(path, import.meta.url));
const directories = [here('./fixtures'), process.env.BM3D_GOLDEN_DIR || here('../../tests/golden')].filter(existsSync);
const files = directories.flatMap((directory) => readdirSync(directory, { recursive: true }).filter((name) => name.endsWith('.bm3d')).map((name) => join(directory, name)));

/** name -> content hash, from tests/golden/hashes.txt */
const goldenHashes = new Map(
	readFileSync(here('../../tests/golden/hashes.txt'), 'utf8')
		.split('\n')
		.filter((line) => line && !line.startsWith('#'))
		.map((line) => line.split(/\s+/)),
);

/** The content hash of docs/FORMAT.md, section 9: every byte but the metadata block, with metadataLength read as 0. */
function contentHash(bytes) {
	const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	const metadataOffset = Number(view.getBigUint64(64, true));
	const metadataLength = Number(view.getBigUint64(72, true));
	return createHash('sha256')
		.update(bytes.subarray(0, 72))
		.update(new Uint8Array(8))
		.update(bytes.subarray(80, metadataOffset))
		.update(bytes.subarray(metadataOffset + metadataLength))
		.digest('hex');
}

describe('the fixtures', () => {
	it('are the golden files of the command line: their content hashes are the lines of hashes.txt', () => {
		const fixtures = readdirSync(here('./fixtures')).filter((name) => name.endsWith('.bm3d'));
		expect(fixtures.sort()).toEqual(['sphere.bm3d', 'terrain.bm3d']);
		for (const name of fixtures) {
			const hash = contentHash(new Uint8Array(readFileSync(here(`./fixtures/${name}`))));
			expect(hash, `${name}: write the fixtures again (web/tests/fixtures/README.md)`).toBe(goldenHashes.get(basename(name, '.bm3d')));
		}
	});
});

describe.each(files.length ? files : [null])('%s', (path) => {
	it.skipIf(path === null)('opens, reads its metadata and decodes every node and texture', async () => {
		const bytes = new Uint8Array(readFileSync(path));
		const recorded = path.replace(/\.bm3d$/, '.sha256');
		if (existsSync(recorded)) expect(contentHash(bytes)).toBe(readFileSync(recorded, 'utf8').split(/\s+/)[0]);
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
