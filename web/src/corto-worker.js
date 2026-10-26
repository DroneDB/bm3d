// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// Derived from html/js/emscripten/corto.em.proto.js of corto (https://github.com/cnr-isti-vclab/corto),
// modified by Digipa for bm3d-viewer.
// Corto: Copyright (c) 2015-2022, Visual Computing Lab, ISTI - CNR. All rights reserved.
// MIT licence; the licence text is in the LICENSE file of this package.
//
// The worker that decodes the corto stream of a node (docs/FORMAT.md section 5) with corto.wasm.
// It is a module worker: `new Worker(url, { type: 'module' })`.
//
// Messages to the worker
//   { type: 'init', wasmUrl }                            once, first: where corto.wasm is (absolute URL)
//   { request, buffer, vertexCount, faceCount, normal, color, uv }
//                                                       decode `buffer` (an ArrayBuffer, transferred); the
//                                                       counts are those of the node's record and the
//                                                       booleans say which attributes the layout names
// Messages from the worker
//   { request, model }   model: { position, index, normal?, color?, uv? } as FORMAT.md section 3.2 lists
//   { request, error: { code, detail } }

import { Bm3dError, ErrorCode } from './format.js';

const CORTO_MAGIC = 0x787a6300;
const CORTO_VERSION = 1;

const ENOSYS = 52;

function corrupt(detail) {
	return new Bm3dError(ErrorCode.CORRUPT_NODE, detail);
}

/** A copy of `count` elements of type `Type` at byte `pointer` of the wasm memory. */
function copyOut(memory, Type, pointer, count) {
	return new Type(memory.buffer, pointer, count).slice();
}

/**
 * The corto decoder on top of corto.wasm. `wasm` is the file (a BufferSource) or a compiled
 * WebAssembly.Module. Returns `{ decode(bytes, expected) }`.
 *
 * The decoder runs without exceptions and trusts the stream it is given: a damaged one can write
 * where it should not, in the memory of the wasm instance, without any error to show for it. So each
 * node is decoded by an instance of its own (instantiating the compiled module is cheap), and what
 * a bad stream does to its memory ends with it.
 */
export async function createCortoDecoder(wasm) {
	const module = wasm instanceof WebAssembly.Module ? wasm : await WebAssembly.compile(wasm);

	function instantiate() {
		const imports = { env: { emscripten_notify_memory_growth: () => {}, proc_exit: () => ENOSYS } };
		const instance = new WebAssembly.Instance(module, imports);
		if (instance.exports._initialize) instance.exports._initialize();
		return instance.exports;
	}

	function decodeWith(e, source, expected) {
		const allocate = (bytes) => {
			const pointer = e.malloc(bytes);
			if (!pointer) throw corrupt(`the decoder could not allocate ${bytes} bytes`);
			return pointer;
		};
		const input = allocate(source.length);
		new Uint8Array(e.memory.buffer).set(source, input);
		const decoder = e.newDecoder(source.length, input);
		if (!decoder) throw corrupt('the decoder refused the stream');

		const nvert = e.nvert(decoder);
		const nface = e.nface(decoder);
		if (nvert !== expected.vertexCount || nface !== expected.faceCount) {
			throw corrupt(`the stream holds ${nvert} vertices and ${nface} triangles, the index says ${expected.vertexCount} and ${expected.faceCount}`);
		}
		const has = { normal: !!e.hasNormal(decoder), color: !!e.hasColor(decoder), uv: !!e.hasUv(decoder) };
		for (const name of ['normal', 'color', 'uv']) {
			if (has[name] !== !!expected[name]) throw corrupt(`the stream ${has[name] ? 'has' : 'has no'} ${name}s and the layout says the opposite`);
		}

		const position = allocate(nvert * 12);
		const index = allocate(nface * 6);
		e.setPositions(decoder, position);
		e.setIndex16(decoder, index);
		let uv = 0;
		let normal = 0;
		let color = 0;
		if (has.uv) e.setUvs(decoder, (uv = allocate(nvert * 8)));
		if (has.normal) e.setNormals16(decoder, (normal = allocate(nvert * 6)));
		if (has.color) e.setColors(decoder, (color = allocate(nvert * 4)), 4);
		e.decode(decoder);

		// The memory may have grown while decoding: e.memory.buffer is read again for each copy
		const model = { position: copyOut(e.memory, Float32Array, position, nvert * 3), index: copyOut(e.memory, Uint16Array, index, nface * 3) };
		if (has.uv) model.uv = copyOut(e.memory, Float32Array, uv, nvert * 2);
		if (has.normal) model.normal = copyOut(e.memory, Int16Array, normal, nvert * 3);
		if (has.color) model.color = copyOut(e.memory, Uint8Array, color, nvert * 4);
		return model;
	}

	return {
		decode(bytes, expected) {
			const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
			if (bytes.byteLength < 8 || view.getUint32(0, true) !== CORTO_MAGIC || view.getUint32(4, true) !== CORTO_VERSION) {
				throw corrupt('the payload is not a version 1 corto stream');
			}
			try {
				return decodeWith(instantiate(), bytes, expected);
			} catch (error) {
				if (error instanceof Bm3dError) throw error;
				throw new Bm3dError(ErrorCode.CORRUPT_NODE, `the decoder failed (${error.message})`, { cause: error });
			}
		},
	};
}

function transferList(model) {
	return Object.values(model).map((array) => array.buffer);
}

/** Answers the messages of the page on `scope` (a DedicatedWorkerGlobalScope, or anything shaped like one). */
export function serveCortoWorker(scope) {
	let decoder = null;
	scope.onmessage = async (message) => {
		const data = message.data;
		if (data.type === 'init') {
			decoder = (async () => {
				const response = await fetch(data.wasmUrl);
				if (!response.ok) throw new Bm3dError(ErrorCode.DECODER_UNAVAILABLE, `${data.wasmUrl} answered ${response.status}`);
				return createCortoDecoder(await response.arrayBuffer());
			})();
			decoder.catch(() => {}); // reported to each request, below
			return;
		}
		try {
			if (!decoder) throw new Bm3dError(ErrorCode.DECODER_UNAVAILABLE, 'the worker was not told where corto.wasm is');
			const model = (await decoder).decode(new Uint8Array(data.buffer), data);
			scope.postMessage({ request: data.request, model }, transferList(model));
		} catch (error) {
			const code = error instanceof Bm3dError ? error.code : ErrorCode.DECODER_UNAVAILABLE;
			scope.postMessage({ request: data.request, error: { code, detail: error instanceof Bm3dError ? error.detail : String(error.message || error) } });
		}
	};
}

if (typeof WorkerGlobalScope !== 'undefined' && globalThis instanceof WorkerGlobalScope) serveCortoWorker(globalThis);
