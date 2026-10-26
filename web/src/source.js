// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// Where the bytes of a file come from. A source is `{ size, etag, read(start, end, options) }`:
// `read` returns an ArrayBuffer with exactly the bytes `start` .. `end - 1` of the file, or throws.
// `size` is the size of the file in bytes (docs/FORMAT.md section 2); `etag` is the entity tag of
// the file, or null when the server does not give one (or does not let the page read it).

import { Bm3dError, ErrorCode } from './format.js';

/** A source over bytes that are in memory (tests, and callers that hold the whole file). */
export function bufferSource(data) {
	const bytes = data instanceof ArrayBuffer ? new Uint8Array(data) : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
	return {
		size: bytes.byteLength,
		etag: null,
		async read(start, end) {
			if (start < 0 || end < start || end > bytes.byteLength) {
				throw new Bm3dError(ErrorCode.OUT_OF_RANGE, `the bytes ${start} to ${end} are not in a file of ${bytes.byteLength} bytes`);
			}
			return bytes.slice(start, end).buffer;
		},
	};
}

/** The total of a `Content-Range: bytes 0-127/739` header, or null when it is `*` or not there. */
export function contentRangeTotal(headerValue) {
	const match = /^bytes\s+(?:\d+-\d+|\*)\/(\d+)$/i.exec((headerValue || '').trim());
	return match ? Number(match[1]) : null;
}

function rangeHeader(start, end) {
	return `bytes=${start}-${end - 1}`;
}

function httpError(url, response, what) {
	return new Bm3dError(ErrorCode.HTTP, `${what} ${url} answered ${response.status} ${response.statusText}`.trim());
}

/**
 * A source over a URL, read with Range requests. Learns the size of the file from the response to
 * the request for the header (its `Content-Range`) and, when that does not say, from a HEAD request.
 * A server that answers a Range request with the whole file is refused.
 * `options.fetch` replaces the global fetch (tests, or a host that adds credentials).
 */
export async function openUrl(url, options = {}) {
	const fetchFn = options.fetch || globalThis.fetch.bind(globalThis);
	const requestInit = options.requestInit || {};

	const request = (start, end, signal) =>
		fetchFn(url, { ...requestInit, signal, headers: { ...requestInit.headers, Range: rangeHeader(start, end) } });

	const first = await request(0, 128);
	let size = null;
	if (first.status === 416) {
		// An empty file has no byte 0
		size = contentRangeTotal(first.headers.get('Content-Range')) ?? 0;
	} else if (first.status === 206) {
		size = contentRangeTotal(first.headers.get('Content-Range'));
	} else if (first.status === 200) {
		throw new Bm3dError(ErrorCode.HTTP, `${url} answered a Range request with 200 and the whole body: the server does not support Range requests`);
	} else {
		throw httpError(url, first, 'GET');
	}
	const etag = first.headers.get('ETag');
	const header = first.status === 206 ? new Uint8Array(await first.arrayBuffer()) : new Uint8Array(0);

	if (size === null) {
		const head = await fetchFn(url, { ...requestInit, method: 'HEAD', headers: requestInit.headers });
		const length = head.ok ? head.headers.get('Content-Length') : null;
		size = length !== null && /^\d+$/.test(length) ? Number(length) : null;
	}
	if (size === null) {
		throw new Bm3dError(ErrorCode.FILE_SIZE_UNKNOWN, `neither the Content-Range of the reply nor the Content-Length of a HEAD request for ${url} gives the size of the file (for a file on another origin the server names Content-Range in Access-Control-Expose-Headers)`);
	}

	return {
		url,
		size,
		etag,
		async read(start, end, readOptions = {}) {
			if (end === start) return new ArrayBuffer(0);
			if (start === 0 && end <= header.byteLength) return header.slice(0, end).buffer;
			const response = await request(start, end, readOptions.signal);
			const expected = end - start;
			if (response.status !== 206 && !(response.status === 200 && start === 0 && end === size)) {
				throw response.status === 200
					? new Bm3dError(ErrorCode.HTTP, `${url} answered a Range request with 200 and the whole body: the server does not support Range requests`)
					: httpError(url, response, `GET bytes ${start}-${end - 1} of`);
			}
			const body = await response.arrayBuffer();
			if (body.byteLength !== expected) {
				throw new Bm3dError(ErrorCode.HTTP, `${url} gave ${body.byteLength} bytes for the range ${start}-${end - 1}, expected ${expected}`);
			}
			return body;
		},
	};
}
