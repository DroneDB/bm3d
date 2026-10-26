// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// The page's side of the decoder: finds corto-worker.js and corto.wasm, starts the worker once
// for each place they are served from, and turns a node's compressed bytes into its arrays.

import { Bm3dError, ErrorCode } from './format.js';

/**
 * The URL of this module, or of the bundle it is part of. It is kept in a variable and the folder is
 * found from it when it is needed: a bundler (webpack, Vite, Parcel) reads `new URL('<file>',
 * import.meta.url)` written out as a file to put in the bundle, and webpack stops on `'.'`, a
 * folder ("Can't resolve '.'"). An application that bundles the viewer gives a `decoderPath`.
 */
const moduleUrl = import.meta.url;

/** The folder the module was loaded from: where corto-worker.js and corto.wasm are by default. */
function defaultDecoderPath() {
	return new URL('./', moduleUrl);
}

/**
 * The URLs of the worker and of the wasm file for a `decoderPath`: the folder that holds both
 * (a URL or a path, absolute or relative to the page; the slash at its end is optional).
 */
export function decoderUrls(decoderPath) {
	let base = defaultDecoderPath();
	if (decoderPath) {
		const folder = String(decoderPath).replace(/\/*$/, '/');
		base = new URL(folder, globalThis.location ? globalThis.location.href : base);
	}
	return { workerUrl: new URL('corto-worker.js', base).href, wasmUrl: new URL('corto.wasm', base).href };
}

const createModuleWorker = (url) => new Worker(url, { type: 'module' });

/** A decoder running in a worker. `createWorker(url)` starts it (tests give another way to start one). */
export class CortoClient {
	constructor({ workerUrl, wasmUrl }, createWorker = createModuleWorker) {
		this.requests = new Map();
		this.count = 0;
		this.worker = createWorker(workerUrl);
		this.worker.onmessage = (message) => this.answer(message.data);
		this.broken = false;
		this.worker.onerror = (event) => {
			this.broken = true;
			this.fail(new Bm3dError(ErrorCode.DECODER_UNAVAILABLE, `the worker ${workerUrl} failed${event && event.message ? ': ' + event.message : ''}`));
		};
		this.worker.postMessage({ type: 'init', wasmUrl });
	}

	/**
	 * Decodes the corto stream in `buffer` (an ArrayBuffer, which the call takes over) of a node of
	 * `expected.vertexCount` vertices and `expected.faceCount` triangles whose layout has
	 * `expected.normal`, `.color` and `.uv`. Resolves to `{ position, index, normal?, color?, uv? }`.
	 */
	decode(buffer, expected) {
		return new Promise((resolve, reject) => {
			const request = this.count++;
			this.requests.set(request, { resolve, reject });
			const { vertexCount, faceCount, normal, color, uv } = expected;
			this.worker.postMessage({ request, buffer, vertexCount, faceCount, normal, color, uv }, [buffer]);
		});
	}

	answer(data) {
		const pending = this.requests.get(data.request);
		if (!pending) return;
		this.requests.delete(data.request);
		if (data.error) pending.reject(new Bm3dError(data.error.code, data.error.detail));
		else pending.resolve(data.model);
	}

	fail(error) {
		for (const pending of this.requests.values()) pending.reject(error);
		this.requests.clear();
	}

	terminate() {
		this.worker.terminate();
		this.fail(new Bm3dError(ErrorCode.DECODER_UNAVAILABLE, 'the decoder was stopped'));
	}
}

const clients = new Map();

/** The decoder for a `decoderPath`; one worker serves every model that asks for the same place. */
export function getDecoder(decoderPath) {
	const urls = decoderUrls(decoderPath);
	if (!clients.has(urls.workerUrl) || clients.get(urls.workerUrl).broken) clients.set(urls.workerUrl, new CortoClient(urls));
	return clients.get(urls.workerUrl);
}
