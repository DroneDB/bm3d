// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa

import { afterEach, describe, expect, it } from 'vitest';

import { cachedSource, fileKey } from '../src/cache.js';
import { Bm3dError, ErrorCode, openBm3d } from '../src/format.js';
import { loadMetadata } from '../src/metadata.js';
import { openModelFile } from '../src/model-file.js';
import { bufferSource, contentRangeTotal, openUrl } from '../src/source.js';
import { model } from './support/models.js';
import { startServer } from './support/server.js';

const servers = [];
async function serve(bytes, options) {
	const server = await startServer((path) => (path === '/model.bm3d' ? { data: bytes } : null), options);
	servers.push(server);
	return server;
}
afterEach(async () => {
	await Promise.all(servers.splice(0).map((server) => server.close()));
});

const get = (server) => server.requests.filter((r) => r.method === 'GET').map((r) => r.range);

describe('contentRangeTotal', () => {
	it('reads the total of a Content-Range value', () => {
		expect(contentRangeTotal('bytes 0-127/739')).toBe(739);
		expect(contentRangeTotal('bytes */0')).toBe(0);
		expect(contentRangeTotal('bytes 0-127/*')).toBeNull();
		expect(contentRangeTotal('items 0-1/5')).toBeNull();
		expect(contentRangeTotal(null)).toBeNull();
	});
});

describe('a file read with Range requests', () => {
	const bytes = model();

	it('is opened with one request for the header and one for the index, and nothing more', async () => {
		const server = await serve(bytes);
		const file = await openBm3d(await openUrl(`${server.url}/model.bm3d`));
		expect(file.nodeCount).toBe(6);
		expect(get(server)).toEqual(['bytes=0-127', `bytes=${file.indexOffset}-${file.indexOffset + file.indexLength - 1}`]);
		expect(server.requests.some((r) => r.method === 'HEAD')).toBe(false);
	});

	it('reads a payload with a request for its bytes', async () => {
		const server = await serve(bytes);
		const source = await openUrl(`${server.url}/model.bm3d`);
		const file = await openBm3d(source);
		const start = file.nodes.payloadOffset[3];
		const length = file.nodes.payloadLength[3];
		const payload = new Uint8Array(await source.read(start, start + length));
		expect(Array.from(payload)).toEqual(Array.from(bytes.subarray(start, start + length)));
		expect(get(server).at(-1)).toBe(`bytes=${start}-${start + length - 1}`);
		expect(get(server).length).toBe(3);
	});

	it('learns the size from a HEAD request when the Content-Range is hidden', async () => {
		const server = await serve(bytes, { contentRange: false });
		const source = await openUrl(`${server.url}/model.bm3d`);
		expect(source.size).toBe(bytes.length);
		expect(server.requests.map((r) => r.method)).toEqual(['GET', 'HEAD']);
		expect((await openBm3d(source)).nodeCount).toBe(6);
	});

	it('reports a size nobody gives', async () => {
		const answer = (status, headers) => new Response(status === 206 ? new Uint8Array(128) : null, { status, headers });
		const fetch = async (url, init) => (init.method === 'HEAD' ? answer(200, {}) : answer(206, {}));
		await expect(openUrl('http://example.test/model.bm3d', { fetch })).rejects.toMatchObject({ code: ErrorCode.FILE_SIZE_UNKNOWN });
	});

	it('refuses a server that ignores the Range header', async () => {
		const server = await serve(bytes, { ranges: false });
		await expect(openUrl(`${server.url}/model.bm3d`)).rejects.toMatchObject({ code: ErrorCode.HTTP, detail: expect.stringContaining('does not support Range') });
	});

	it('refuses an answer that is not a success', async () => {
		const server = await serve(bytes);
		await expect(openUrl(`${server.url}/missing.bm3d`)).rejects.toMatchObject({ code: ErrorCode.HTTP, detail: expect.stringContaining('404') });
	});

	it('refuses a body of the wrong size', async () => {
		const answer = (range) => new Response(new Uint8Array(range === 'bytes=0-127' ? 128 : 3), { status: 206, headers: { 'Content-Range': `bytes 0-127/${bytes.length}` } });
		const source = await openUrl('http://example.test/model.bm3d', { fetch: async (url, init) => answer(init.headers.Range) });
		await expect(source.read(200, 300)).rejects.toMatchObject({ code: ErrorCode.HTTP, detail: expect.stringContaining('expected 100') });
	});

	it('reports a truncated header for a file shorter than the header, and for an empty one', async () => {
		for (const length of [100, 0]) {
			const server = await serve(new Uint8Array(length));
			await expect(openBm3d(await openUrl(`${server.url}/model.bm3d`))).rejects.toMatchObject({ code: ErrorCode.TRUNCATED_HEADER });
		}
	});

	it('sends the request options of the caller', async () => {
		let seen;
		const fetch = async (url, init) => {
			seen = init;
			return new Response(bytes.subarray(0, 128), { status: 206, headers: { 'Content-Range': `bytes 0-127/${bytes.length}` } });
		};
		await openUrl('http://example.test/model.bm3d', { fetch, requestInit: { credentials: 'include', headers: { Authorization: 'Bearer x' } } });
		expect(seen.credentials).toBe('include');
		expect(seen.headers).toEqual({ Authorization: 'Bearer x', Range: 'bytes=0-127' });
	});
});

describe('loadMetadata', () => {
	it('reads the header and the metadata block only', async () => {
		const server = await serve(model());
		const metadata = await loadMetadata(`${server.url}/model.bm3d`);
		expect(metadata.generator).toBe('bm3d 1.0.0');
		expect(get(server).length).toBe(2);
		expect(get(server)[0]).toBe('bytes=0-127');
	});

	it('reports a block that is not JSON', async () => {
		const server = await serve(model({}, (spec) => (spec.metadata = 'not json')));
		await expect(loadMetadata(`${server.url}/model.bm3d`)).rejects.toMatchObject({ code: ErrorCode.BAD_METADATA });
	});
});

/** A store of the cache in memory, as the cache.js interface says. */
function memoryStore() {
	const entries = new Map();
	const store = {
		entries,
		evicted: [],
		async get(key) {
			return entries.get(key)?.bytes;
		},
		async put(key, url, file, bytes) {
			entries.set(key, { url, file, bytes });
		},
		async evict(url, keep) {
			store.evicted.push([url, keep]);
			for (const [key, entry] of entries) if (entry.url === url && entry.file !== keep) entries.delete(key);
		},
	};
	return store;
}

describe('the cache', () => {
	const bytes = model();

	it('keeps what is read, keyed by URL, format version and ETag', async () => {
		const server = await serve(bytes, { etag: '"abc"' });
		const url = `${server.url}/model.bm3d`;
		const store = memoryStore();
		const { source, index } = await openModelFile(url, { cache: store });
		const start = index.nodes.payloadOffset[1];
		const payload = await source.read(start, start + index.nodes.payloadLength[1]);
		const key = fileKey(url, 1, '"abc"');
		expect(key).toBe(`${url}|v1|"abc"`);
		expect([...store.entries.values()].every((entry) => entry.file === key)).toBe(true);
		expect(store.entries.size).toBe(2); // the index and the payload
		expect(store.entries.has(`${key}#${start}-${start + payload.byteLength}`)).toBe(true);
	});

	it('answers the second time from the store: only the header goes to the server', async () => {
		const server = await serve(bytes, { etag: '"abc"' });
		const url = `${server.url}/model.bm3d`;
		const store = memoryStore();
		const first = await openModelFile(url, { cache: store });
		const start = first.index.nodes.payloadOffset[2];
		const end = start + first.index.nodes.payloadLength[2];
		const payload = new Uint8Array(await first.source.read(start, end));
		server.requests.length = 0;

		const second = await openModelFile(url, { cache: store });
		expect(Array.from(new Uint8Array(await second.source.read(start, end)))).toEqual(Array.from(payload));
		expect(second.index.nodeCount).toBe(6);
		expect(get(server)).toEqual(['bytes=0-127']);
	});

	it('does not answer from the bytes of another version of the file', async () => {
		const store = memoryStore();
		const old = await serve(bytes, { etag: '"old"' });
		const url = `${old.url}/model.bm3d`;
		await openModelFile(url, { cache: store });
		const oldKeys = [...store.entries.keys()];
		expect(oldKeys.length).toBeGreaterThan(0);

		// the same URL, replaced: the ETag is another
		old.options.etag = '"new"';
		old.requests.length = 0;
		await openModelFile(url, { cache: store });
		expect(get(old).length).toBe(2); // the index came from the server
		for (const key of oldKeys) expect(store.entries.has(key)).toBe(false);
		expect([...store.entries.values()].every((entry) => entry.file === fileKey(url, 1, '"new"'))).toBe(true);
	});

	it('does not cache a file that has no ETag', async () => {
		const server = await serve(bytes, { etag: false });
		const store = memoryStore();
		const { index } = await openModelFile(`${server.url}/model.bm3d`, { cache: store });
		expect(index.nodeCount).toBe(6);
		expect(store.entries.size).toBe(0);
		expect(store.evicted).toEqual([]);
	});

	it('goes on when the store fails', async () => {
		const failing = { get: async () => Promise.reject(new Error('full')), put: async () => Promise.reject(new Error('full')), evict: async () => Promise.reject(new Error('full')) };
		const source = cachedSource(bufferSource(bytes), failing, 'u', 'k');
		expect((await source.read(0, 128)).byteLength).toBe(128);
	});

	it('keeps a copy of the bytes: the caller may hand them to a worker', async () => {
		const store = memoryStore();
		const source = cachedSource(bufferSource(bytes), store, 'u', 'k');
		const buffer = await source.read(0, 128);
		structuredClone(buffer, { transfer: [buffer] }); // detaches it, as postMessage does
		await Promise.resolve();
		expect(store.entries.get('k#0-128').bytes.byteLength).toBe(128);
	});

	it('is not used when the option is off', async () => {
		const server = await serve(bytes);
		const { index } = await openModelFile(`${server.url}/model.bm3d`, {});
		expect(index.nodeCount).toBe(6);
	});
});

it('reports a Bm3dError for every failure of the transport', async () => {
	const server = await serve(model());
	const error = await openUrl(`${server.url}/nothing`).catch((e) => e);
	expect(error).toBeInstanceOf(Bm3dError);
});
