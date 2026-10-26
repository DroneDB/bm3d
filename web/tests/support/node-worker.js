// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// A stand-in for `new Worker(url, { type: 'module' })` in node: a worker thread that fetches the
// module at `url` (over HTTP, as a browser would) and runs it as the decoder's worker, so that a test
// can check that the worker and corto.wasm are really taken from the decoderPath it was given.

import { Worker } from 'node:worker_threads';

const BOOT = `
const { parentPort, workerData } = require('node:worker_threads');
(async () => {
	const source = await (await fetch(workerData.url)).text();
	const module = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
	module.serveCortoWorker({
		postMessage: (message, transfer) => parentPort.postMessage(message, transfer),
		set onmessage(handler) { parentPort.on('message', (data) => handler({ data })); },
	});
})().catch((error) => { throw error; });
`;

export class NodeWorker {
	constructor(url) {
		this.onmessage = null;
		this.onerror = null;
		this.thread = new Worker(BOOT, { eval: true, workerData: { url } });
		this.thread.on('message', (data) => this.onmessage && this.onmessage({ data }));
		this.thread.on('error', (error) => this.onerror && this.onerror({ message: error.message }));
	}

	postMessage(message, transfer) {
		this.thread.postMessage(message, transfer);
	}

	terminate() {
		return this.thread.terminate();
	}
}
