// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa

import { cachedSource, createIndexedDbStore, fileKey } from './cache.js';
import { openHeader, readIndex } from './format.js';
import { openUrl } from './source.js';

/** The store the `cache` option names: false/absent for none, true for IndexedDB, or a store of the host. */
function resolveStore(cache) {
	if (!cache) return null;
	return cache === true ? createIndexedDbStore() : cache;
}

/**
 * Opens the model at `url`: reads the header and the index (every check of docs/FORMAT.md
 * section 11 is made) and returns `{ source, index }`; the source reads the payloads and
 * the textures later, through the cache when `options.cache` asks for one and the server gives an ETag.
 *   options.cache        false (default) | true (IndexedDB) | a store (see cache.js)
 *   options.fetch, options.requestInit    how to request (see source.js)
 */
export async function openModelFile(url, options = {}) {
	const network = await openUrl(url, options);
	const header = await openHeader(network);
	const store = resolveStore(options.cache);
	const source = store && network.etag ? cachedSource(network, store, url, fileKey(url, header.formatVersion, network.etag)) : network;
	const index = await readIndex(source, header);
	return { source, index };
}
