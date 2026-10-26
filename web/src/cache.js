// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// The opt-in cache of the bytes a model is made of (its index, node payloads and textures), in the
// browser's IndexedDB. An entry belongs to one version of one file: the key is the URL, the format
// version and the ETag of the file, so a file that is replaced (new ETag) is never answered from
// the old bytes, and the entries of the old version are deleted when the new one is opened.
// A file whose server gives no ETag is not cached: there would be no way to know it is current.
//
// A store is `{ get(key), put(key, url, fileKey, buffer), evict(url, keepFileKey) }`, all asynchronous;
// the IndexedDB store below is the one the viewer makes, and a host can pass its own.

const DATABASE = 'bm3d-viewer';
const STORE = 'ranges';

function promisify(request) {
	return new Promise((resolve, reject) => {
		request.onsuccess = () => resolve(request.result);
		request.onerror = () => reject(request.error);
	});
}

function transactionDone(transaction) {
	return new Promise((resolve, reject) => {
		transaction.oncomplete = () => resolve();
		transaction.onerror = () => reject(transaction.error);
		transaction.onabort = () => reject(transaction.error);
	});
}

/** The store in IndexedDB, or null where the browser has none. */
export function createIndexedDbStore(indexedDB = globalThis.indexedDB) {
	if (!indexedDB) return null;
	const opening = new Promise((resolve, reject) => {
		const request = indexedDB.open(DATABASE, 1);
		request.onupgradeneeded = () => {
			const store = request.result.createObjectStore(STORE, { keyPath: 'key' });
			store.createIndex('url', 'url');
		};
		request.onsuccess = () => resolve(request.result);
		request.onerror = () => reject(request.error);
	});
	const objectStore = async (mode) => (await opening).transaction(STORE, mode);

	return {
		async get(key) {
			const record = await promisify((await objectStore('readonly')).objectStore(STORE).get(key));
			return record ? record.bytes : undefined;
		},
		async put(key, url, fileKey, bytes) {
			const transaction = await objectStore('readwrite');
			transaction.objectStore(STORE).put({ key, url, fileKey, bytes });
			await transactionDone(transaction);
		},
		async evict(url, keepFileKey) {
			const transaction = await objectStore('readwrite');
			const cursorRequest = transaction.objectStore(STORE).index('url').openCursor(IDBKeyRange.only(url));
			cursorRequest.onsuccess = () => {
				const cursor = cursorRequest.result;
				if (!cursor) return;
				if (cursor.value.fileKey !== keepFileKey) cursor.delete();
				cursor.continue();
			};
			await transactionDone(transaction);
		},
	};
}

/** The key of one version of one file. */
export function fileKey(url, formatVersion, etag) {
	return `${url}|v${formatVersion}|${etag}`;
}

/**
 * A source that answers from `store` what it has and keeps what it reads, for a file whose
 * URL, version and ETag make `key`. A failure of the store (full disk, private browsing) is
 * ignored: the cache is an optimisation and the bytes come from the network.
 */
export function cachedSource(source, store, url, key) {
	store.evict(url, key).catch(() => {});
	return {
		...source,
		async read(start, end, options) {
			const entry = `${key}#${start}-${end}`;
			const hit = await store.get(entry).catch(() => undefined);
			if (hit) return hit;
			const bytes = await source.read(start, end, options);
			// A copy: the caller may hand `bytes` to a worker (which detaches it) before the store clones it
			store.put(entry, url, key, bytes.slice(0)).catch(() => {});
			return bytes;
		},
	};
}
