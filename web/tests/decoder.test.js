// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa

import { readFileSync } from 'node:fs';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';

import { Bm3dError, ErrorCode, openBm3d } from '../src/format.js';
import { bufferSource } from '../src/source.js';
import { createCortoDecoder } from '../src/corto-worker.js';
import { CortoClient, decoderUrls } from '../src/decoder-client.js';
import { NodeWorker } from './support/node-worker.js';
import { startServer } from './support/server.js';

const WASM = new URL('../corto-wasm/out/corto.wasm', import.meta.url);
const DIST = new URL('../dist/', import.meta.url);
const fixture = (name) => new Uint8Array(readFileSync(new URL(`./fixtures/${name}`, import.meta.url)));

/** Opens a fixture and gives the payload and the expected counts of each of its nodes. */
async function nodesOf(name) {
	const bytes = fixture(name);
	const file = await openBm3d(bufferSource(bytes));
	const nodes = [];
	for (let i = 0; i < file.sink; i++) {
		const start = file.nodes.payloadOffset[i];
		nodes.push({
			payload: bytes.slice(start, start + file.nodes.payloadLength[i]),
			expected: { vertexCount: file.nodes.vertexCount[i], faceCount: file.nodes.faceCount[i], normal: file.hasNormals, color: file.hasColors, uv: file.hasUvs },
			sphere: file.nodes.spheres.subarray(i * 5, i * 5 + 5),
		});
	}
	return { file, nodes };
}

let decoder;
beforeAll(async () => {
	decoder = await createCortoDecoder(readFileSync(WASM));
});

describe('the corto decoder (corto.wasm)', () => {
	it('decodes every node of a textured model into the arrays of the layout', async () => {
		const { nodes } = await nodesOf('terrain.bm3d');
		expect(nodes.length).toBe(10);
		for (const { payload, expected, sphere } of nodes) {
			const model = decoder.decode(payload, expected);
			expect(Object.keys(model).sort()).toEqual(['color', 'index', 'normal', 'position', 'uv']);
			expect(model.position).toBeInstanceOf(Float32Array);
			expect(model.position.length).toBe(3 * expected.vertexCount);
			expect(model.index).toBeInstanceOf(Uint16Array);
			expect(model.index.length).toBe(3 * expected.faceCount);
			expect(model.normal).toBeInstanceOf(Int16Array);
			expect(model.normal.length).toBe(3 * expected.vertexCount);
			expect(model.color).toBeInstanceOf(Uint8Array);
			expect(model.color.length).toBe(4 * expected.vertexCount);
			expect(model.uv).toBeInstanceOf(Float32Array);
			expect(model.uv.length).toBe(2 * expected.vertexCount);

			expect(Math.max(...model.index)).toBeLessThan(expected.vertexCount);
			// the vertices are in the node's sphere, the texture coordinates in [0, 1], the normals unit vectors
			for (let v = 0; v < expected.vertexCount; v++) {
				const [x, y, z] = model.position.subarray(3 * v, 3 * v + 3);
				expect(Math.hypot(x - sphere[0], y - sphere[1], z - sphere[2])).toBeLessThanOrEqual(sphere[3] * 1.001);
				const [u, w] = model.uv.subarray(2 * v, 2 * v + 2);
				expect(u).toBeGreaterThanOrEqual(-0.001);
				expect(u).toBeLessThanOrEqual(1.001);
				expect(w).toBeGreaterThanOrEqual(-0.001);
				expect(w).toBeLessThanOrEqual(1.001);
				const [nx, ny, nz] = Array.from(model.normal.subarray(3 * v, 3 * v + 3), (n) => n / 32767);
				expect(Math.hypot(nx, ny, nz)).toBeCloseTo(1, 1);
			}
		}
	});

	it('decodes a model with vertex colours and no texture, whose counts are those the encoder wrote', async () => {
		const { nodes } = await nodesOf('sphere.bm3d');
		const { payload, expected } = nodes[0];
		expect(expected).toMatchObject({ vertexCount: 3010, faceCount: 6016, uv: false, color: true });
		const model = decoder.decode(payload, expected);
		expect(Object.keys(model).sort()).toEqual(['color', 'index', 'normal', 'position']);
		// The points of a unit sphere, quantised by the encoder (a step of 1/16 of the model)
		for (let v = 0; v < expected.vertexCount; v++) {
			const radius = Math.hypot(...model.position.subarray(3 * v, 3 * v + 3));
			expect(radius).toBeGreaterThan(0.85);
			expect(radius).toBeLessThan(1.01);
		}
		// The colour follows the position (red = 127.5 + 127 x), and the alpha is opaque, to 5 bits
		for (let v = 0; v < expected.vertexCount; v++) {
			expect(Math.abs(model.color[4 * v] - (127.5 + 127 * model.position[3 * v]))).toBeLessThan(24);
			expect(model.color[4 * v + 3]).toBeGreaterThanOrEqual(248);
		}
	});

	it('decodes the same node to the same arrays each time', async () => {
		const { nodes } = await nodesOf('terrain.bm3d');
		const first = decoder.decode(nodes[4].payload, nodes[4].expected);
		decoder.decode(nodes[1].payload, nodes[1].expected);
		const again = decoder.decode(nodes[4].payload, nodes[4].expected);
		for (const key of Object.keys(first)) expect(again[key]).toEqual(first[key]);
	});

	it('refuses a node whose counts differ from the index', async () => {
		const { nodes } = await nodesOf('sphere.bm3d');
		const { payload, expected } = nodes[0];
		for (const wrong of [{ vertexCount: expected.vertexCount + 1 }, { faceCount: expected.faceCount - 1 }]) {
			expect(() => decoder.decode(payload, { ...expected, ...wrong })).toThrowError(expect.objectContaining({ code: ErrorCode.CORRUPT_NODE }));
		}
		expect(() => decoder.decode(payload, { ...expected, faceCount: 7 })).toThrowError(/3010 vertices and 6016 triangles/);
	});

	it('refuses a stream whose attributes are not those of the layout', async () => {
		const { nodes } = await nodesOf('sphere.bm3d');
		const { payload, expected } = nodes[0];
		expect(() => decoder.decode(payload, { ...expected, uv: true })).toThrowError(expect.objectContaining({ code: ErrorCode.CORRUPT_NODE }));
		expect(() => decoder.decode(payload, { ...expected, color: false })).toThrowError(expect.objectContaining({ code: ErrorCode.CORRUPT_NODE }));
	});

	it('refuses what is not a corto stream', async () => {
		const { nodes } = await nodesOf('sphere.bm3d');
		const { payload, expected } = nodes[0];
		const wrongMagic = Uint8Array.from(payload);
		wrongMagic[1] ^= 0xff;
		const newer = Uint8Array.from(payload);
		newer[4] = 2;
		for (const bad of [wrongMagic, newer, payload.slice(0, 5), new Uint8Array(0), new Uint8Array(200)]) {
			expect(() => decoder.decode(bad, expected)).toThrowError(expect.objectContaining({ code: ErrorCode.CORRUPT_NODE }));
		}
	});

	it('survives streams that are cut short or damaged, and goes on decoding valid ones', async () => {
		const { nodes } = await nodesOf('terrain.bm3d');
		const { payload, expected } = nodes[3];
		let refused = 0;
		for (const length of [12, 40, 200, 2000]) {
			try {
				decoder.decode(payload.slice(0, length), expected);
			} catch (error) {
				expect(error).toBeInstanceOf(Bm3dError);
				expect(error.code).toBe(ErrorCode.CORRUPT_NODE);
				refused++;
			}
		}
		expect(refused).toBeGreaterThan(0);
		// a valid stream, after the failures
		const model = decoder.decode(payload, expected);
		expect(model.position.length).toBe(3 * expected.vertexCount);
	});
});

describe('decoderUrls', () => {
	it('defaults to the folder of the module', () => {
		const { workerUrl, wasmUrl } = decoderUrls();
		expect(workerUrl).toBe(new URL('../src/corto-worker.js', import.meta.url).href);
		expect(wasmUrl).toBe(new URL('../src/corto.wasm', import.meta.url).href);
	});

	it('takes a folder, with or without the slash at its end', () => {
		for (const path of ['https://cdn.example/viewer/', 'https://cdn.example/viewer', 'https://cdn.example/viewer//']) {
			expect(decoderUrls(path)).toEqual({ workerUrl: 'https://cdn.example/viewer/corto-worker.js', wasmUrl: 'https://cdn.example/viewer/corto.wasm' });
		}
	});

	it('resolves a path against the page', () => {
		const location = globalThis.location;
		globalThis.location = { href: 'https://hub.example/app/index.html' };
		try {
			expect(decoderUrls('/wasm/bm3d/').wasmUrl).toBe('https://hub.example/wasm/bm3d/corto.wasm');
			expect(decoderUrls('assets').workerUrl).toBe('https://hub.example/app/assets/corto-worker.js');
		} finally {
			globalThis.location = location;
		}
	});
});

describe('the worker and the wasm file, served from a decoderPath that is not the default', () => {
	let server;
	let client;
	beforeAll(async () => {
		// The built worker and wasm, under a path of their own
		const files = {
			'/deep/assets/bm3d/corto-worker.js': { data: readFileSync(new URL('corto-worker.js', DIST)), type: 'text/javascript' },
			'/deep/assets/bm3d/corto.wasm': { data: readFileSync(new URL('corto.wasm', DIST)), type: 'application/wasm' },
		};
		server = await startServer((path) => files[path] || null);
		client = new CortoClient(decoderUrls(`${server.url}/deep/assets/bm3d`), (url) => new NodeWorker(url));
	});
	afterAll(async () => {
		await client.terminate();
		await server.close();
	});

	it('decodes a node', async () => {
		const { nodes } = await nodesOf('sphere.bm3d');
		const { payload, expected } = nodes[0];
		const buffer = payload.buffer.slice(payload.byteOffset, payload.byteOffset + payload.byteLength);
		const model = await client.decode(buffer, expected);
		expect(model.position.length).toBe(3 * expected.vertexCount);
		expect(buffer.byteLength).toBe(0); // taken over by the worker
		expect(server.requests.map((r) => r.path).sort()).toEqual(['/deep/assets/bm3d/corto-worker.js', '/deep/assets/bm3d/corto.wasm']);
	});

	it('reports a corrupt node with the error of the format', async () => {
		const { nodes } = await nodesOf('sphere.bm3d');
		const { payload, expected } = nodes[0];
		const buffer = payload.buffer.slice(payload.byteOffset, payload.byteOffset + payload.byteLength);
		await expect(client.decode(buffer, { ...expected, vertexCount: 5 })).rejects.toMatchObject({ name: 'Bm3dError', code: ErrorCode.CORRUPT_NODE });
	});

	it('reports a decoder that cannot start', async () => {
		const missing = new CortoClient(decoderUrls(`${server.url}/elsewhere/`), (url) => new NodeWorker(url));
		const { nodes } = await nodesOf('sphere.bm3d');
		const buffer = nodes[0].payload.buffer.slice(0);
		await expect(missing.decode(buffer, nodes[0].expected)).rejects.toMatchObject({ code: ErrorCode.DECODER_UNAVAILABLE });
		await missing.terminate();
	});
});
