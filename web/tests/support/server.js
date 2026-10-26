// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// A small HTTP server for the tests that answers Range requests as a file server does, records
// what it was asked, and can misbehave the ways a real one does (no Content-Range, no Range support,
// no ETag).

import { createServer } from 'node:http';

/**
 * `resolve(path)` returns `{ data, type }` (data: a Buffer or Uint8Array) or null.
 * options: etag (a string, or false for none), ranges (false: answer 200 with everything),
 * contentRange (false: leave the header out, as a cross-origin server that does not expose it).
 * Returns `{ url, requests, close(), options }`; `requests` lists `{ method, path, range }`.
 */
export async function startServer(resolve, options = {}) {
	const settings = { etag: '"v1"', ranges: true, contentRange: true, ...options };
	const requests = [];
	const server = createServer((request, response) => {
		const path = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
		requests.push({ method: request.method, path, range: request.headers.range || null });
		const entry = resolve(path);
		if (!entry) {
			response.writeHead(404).end();
			return;
		}
		const data = Buffer.from(entry.data);
		const headers = { 'Content-Type': entry.type || 'application/octet-stream', 'Accept-Ranges': 'bytes' };
		if (settings.etag) headers.ETag = settings.etag;
		const range = /^bytes=(\d+)-(\d+)$/.exec(request.headers.range || '');
		if (request.method === 'HEAD' || !range || !settings.ranges) {
			response.writeHead(200, { ...headers, 'Content-Length': data.length });
			response.end(request.method === 'HEAD' ? undefined : data);
			return;
		}
		const start = Number(range[1]);
		if (start >= data.length) {
			response.writeHead(416, { ...headers, 'Content-Range': `bytes */${data.length}` }).end();
			return;
		}
		const end = Math.min(Number(range[2]), data.length - 1);
		if (settings.contentRange) headers['Content-Range'] = `bytes ${start}-${end}/${data.length}`;
		response.writeHead(206, { ...headers, 'Content-Length': end - start + 1 });
		response.end(data.subarray(start, end + 1));
	});
	await new Promise((done) => server.listen(0, '127.0.0.1', done));
	return {
		url: `http://127.0.0.1:${server.address().port}`,
		requests,
		options: settings,
		close: () => new Promise((done) => server.close(done)),
	};
}
