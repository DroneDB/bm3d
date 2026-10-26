// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// Reader of the bm3d file format, version 1. Written from docs/FORMAT.md alone: the section
// numbers in the comments are those of that document. This module does no input or output of its
// own: it turns the bytes of the header, of the index and of the metadata block into the tables the
// viewer uses, and checks them. A `source` (source.js) brings the bytes, with HTTP Range requests.

export const FORMAT_VERSION = 1;
export const HEADER_SIZE = 128;
export const NODE_RECORD_SIZE = 56;
export const PATCH_RECORD_SIZE = 12;
export const TEXTURE_RECORD_SIZE = 16;
export const NO_TEXTURE = 0xffffffff;
export const MAX_METADATA_LENGTH = 1 << 20;

export const FLAG_CORTO_COMPRESSED = 0x1;
export const FLAG_TEXTURE_WEBP = 0x2;

const MAGIC = [0x42, 0x4d, 0x33, 0x44]; // "BM3D"
const NEXUS_MAGIC = [0x20, 0x73, 0x78, 0x4e]; // the Nexus container (format 0): 0x4E787320, little endian
const HIGH_HALF_LIMIT = 0x1fffff; // a u64 above this is not a safe integer (section 10.3)

// The attribute types (section 3.2)
const BYTE = 1;
const UNSIGNED_BYTE = 2;
const SHORT = 3;
const UNSIGNED_SHORT = 4;
const FLOAT = 7;
const SLOT_COUNT = 8;

/** The names of the errors of section 11, as the `code` of a Bm3dError. */
export const ErrorCode = Object.freeze({
	FILE_SIZE_UNKNOWN: 'file size unknown (HTTP)',
	TRUNCATED_HEADER: 'truncated header',
	BAD_MAGIC: 'bad magic',
	UNSUPPORTED_VERSION: 'unsupported version',
	BAD_HEADER_SIZE: 'bad header size',
	COMPRESSION_FLAG_MISSING: 'compression flag missing',
	UNKNOWN_FLAGS: 'unknown flags',
	UNSUPPORTED_LAYOUT: 'unsupported layout',
	BAD_COUNTS: 'bad counts',
	OUT_OF_RANGE: 'out of range',
	OVERLAPPING_SECTIONS: 'overlapping sections',
	BAD_INDEX: 'bad index',
	VALUE_TOO_LARGE: 'value too large (JavaScript)',
	CORRUPT_NODE: 'corrupt node',
	CORRUPT_TEXTURE: 'corrupt texture',
	BAD_METADATA: 'bad metadata',
	// Not in the format: the transport failed (status other than 206, a reply of the wrong size), or
	// the decoder (worker, corto.wasm) could not be started.
	HTTP: 'http error',
	DECODER_UNAVAILABLE: 'decoder unavailable',
});

/** An error of the format or of the transport; `code` is one of ErrorCode, `detail` says what is wrong. */
export class Bm3dError extends Error {
	constructor(code, detail, options) {
		super(`${code}: ${detail}`, options);
		this.name = 'Bm3dError';
		this.code = code;
		this.detail = detail;
	}
}

function fail(code, message) {
	throw new Bm3dError(code, message);
}

function hex(value) {
	return '0x' + value.toString(16);
}

/** A u64 as a Number; a value that is not a safe integer is an error (section 10.3). */
function readU64(view, offset, what) {
	const low = view.getUint32(offset, true);
	const high = view.getUint32(offset + 4, true);
	if (high > HIGH_HALF_LIMIT) {
		fail(ErrorCode.VALUE_TOO_LARGE, `${what} is ${hex(high)}${low.toString(16).padStart(8, '0')}, above ${Number.MAX_SAFE_INTEGER}`);
	}
	return high * 4294967296 + low;
}

function readLayout(view, offset) {
	const slots = [];
	for (let i = 0; i < SLOT_COUNT; i++) slots.push({ type: view.getUint8(offset + 2 * i), number: view.getUint8(offset + 2 * i + 1) });
	return slots;
}

function isSlot(slot, type, number) {
	return slot.type === type && slot.number === number;
}

function isEmpty(slot) {
	return isSlot(slot, 0, 0);
}

/** A slot that is either empty (null) or exactly `type` x `number`; undefined when it is neither. */
function optionalSlot(slot, type, number) {
	if (isEmpty(slot)) return null;
	return isSlot(slot, type, number) ? slot : undefined;
}

/**
 * The layouts of version 1 (section 3.2), or an unsupported layout error. Returns what a node holds
 * once decoded: `{ normal, color, uv }`, each a boolean (position and index are always there).
 */
function checkLayouts(vertexLayout, faceLayout, textureCount) {
	const bad = (what) => fail(ErrorCode.UNSUPPORTED_LAYOUT, what);
	if (!isSlot(vertexLayout[0], FLOAT, 3)) bad(`vertex slot 0 (position) is type ${vertexLayout[0].type} x ${vertexLayout[0].number}, not FLOAT x 3`);
	const normal = optionalSlot(vertexLayout[1], SHORT, 3);
	if (normal === undefined) bad(`vertex slot 1 (normal) is type ${vertexLayout[1].type} x ${vertexLayout[1].number}, not SHORT x 3 or empty`);
	const color = optionalSlot(vertexLayout[2], UNSIGNED_BYTE, 4);
	if (color === undefined) bad(`vertex slot 2 (colour) is type ${vertexLayout[2].type} x ${vertexLayout[2].number}, not UNSIGNED_BYTE x 4 or empty`);
	const uv = optionalSlot(vertexLayout[3], FLOAT, 2);
	if (uv === undefined) bad(`vertex slot 3 (texture coordinates) is type ${vertexLayout[3].type} x ${vertexLayout[3].number}, not FLOAT x 2 or empty`);
	for (let i = 4; i < SLOT_COUNT; i++) if (!isEmpty(vertexLayout[i])) bad(`vertex slot ${i} is not empty`);
	if (!isSlot(faceLayout[0], UNSIGNED_SHORT, 3)) bad(`face slot 0 (indexes) is type ${faceLayout[0].type} x ${faceLayout[0].number}, not UNSIGNED_SHORT x 3`);
	for (let i = 1; i < SLOT_COUNT; i++) if (!isEmpty(faceLayout[i])) bad(`face slot ${i} is not empty`);
	if ((uv !== null) !== (textureCount > 0)) bad(`the texture coordinates are ${uv ? 'present' : 'absent'} and textureCount is ${textureCount}`);
	return { normal: normal !== null, color: color !== null, uv: uv !== null };
}

/** The size in bytes of the index and the start of its texture table (section 4). */
export function indexGeometry(nodeCount, patchCount, textureCount) {
	const textureTableStart = Math.ceil((NODE_RECORD_SIZE * nodeCount + PATCH_RECORD_SIZE * patchCount) / 8) * 8;
	return { textureTableStart, indexLength: textureTableStart + TEXTURE_RECORD_SIZE * textureCount };
}

/**
 * Parses and checks the header (the steps of section 11 that need only the first bytes of the
 * file): from the magic number to the ranges of the index and of the metadata block.
 * `bytes` is at least the first 128 bytes of the file, `fileSize` its size in bytes.
 */
export function parseHeader(bytes, fileSize) {
	if (bytes.byteLength < HEADER_SIZE) {
		fail(ErrorCode.TRUNCATED_HEADER, `${bytes.byteLength} bytes could be read, the header has ${HEADER_SIZE}`);
	}
	const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

	const magic = [0, 1, 2, 3].map((i) => view.getUint8(i));
	if (magic.some((byte, i) => byte !== MAGIC[i])) {
		const nexus = magic.every((byte, i) => byte === NEXUS_MAGIC[i]);
		fail(ErrorCode.BAD_MAGIC, `the first four bytes are ${magic.map((b) => b.toString(16).padStart(2, '0')).join(' ')}, not 42 4d 33 44 (BM3D)${nexus ? '; this is a Nexus file (format 0), which this reader does not read' : ''}`);
	}

	const formatVersion = view.getUint32(4, true);
	if (formatVersion === 0 || formatVersion > FORMAT_VERSION) {
		fail(ErrorCode.UNSUPPORTED_VERSION, `the file is version ${formatVersion} and this reader reads up to version ${FORMAT_VERSION}`);
	}

	const headerSize = view.getUint32(8, true);
	if (headerSize < HEADER_SIZE || headerSize % 8 !== 0 || headerSize > fileSize) {
		fail(ErrorCode.BAD_HEADER_SIZE, `headerSize is ${headerSize}: it must be a multiple of 8, at least ${HEADER_SIZE} and not larger than the file (${fileSize} bytes)`);
	}

	const flags = view.getUint32(12, true);
	if ((flags & FLAG_CORTO_COMPRESSED) === 0) {
		fail(ErrorCode.COMPRESSION_FLAG_MISSING, `flags are ${hex(flags)}: the CORTO_COMPRESSED bit (0x1) is not set`);
	}
	const textureCount = view.getUint32(104, true);
	const knownFlags = FLAG_CORTO_COMPRESSED | FLAG_TEXTURE_WEBP;
	if ((flags & ~knownFlags) !== 0) {
		fail(ErrorCode.UNKNOWN_FLAGS, `flags are ${hex(flags)}: bits ${hex((flags & ~knownFlags) >>> 0)} are not known to version 1`);
	}
	if (((flags & FLAG_TEXTURE_WEBP) !== 0) !== (textureCount > 0)) {
		fail(ErrorCode.UNKNOWN_FLAGS, `flags are ${hex(flags)} and textureCount is ${textureCount}: TEXTURE_WEBP (0x2) is set exactly when there are textures`);
	}

	const vertexLayout = readLayout(view, 16);
	const faceLayout = readLayout(view, 32);
	const attributes = checkLayouts(vertexLayout, faceLayout, textureCount);

	const nodeCount = view.getUint32(96, true);
	const patchCount = view.getUint32(100, true);
	if (nodeCount < 2) {
		fail(ErrorCode.BAD_COUNTS, `nodeCount is ${nodeCount}: it is at least 2 (a node and the sink)`);
	}

	const vertexCount = readU64(view, 48, 'vertexCount');
	const faceCount = readU64(view, 56, 'faceCount');
	const metadataOffset = readU64(view, 64, 'metadataOffset');
	const metadataLength = readU64(view, 72, 'metadataLength');
	const indexOffset = readU64(view, 80, 'indexOffset');
	const indexLength = readU64(view, 88, 'indexLength');

	const expected = indexGeometry(nodeCount, patchCount, textureCount);
	const outOfRange = (what) => fail(ErrorCode.OUT_OF_RANGE, what);
	if (indexOffset % 8 !== 0 || indexOffset < headerSize) {
		outOfRange(`indexOffset is ${indexOffset}: it must be a multiple of 8 and at least headerSize (${headerSize})`);
	}
	if (indexLength !== expected.indexLength) {
		outOfRange(`indexLength is ${indexLength} and ${nodeCount} nodes, ${patchCount} patches and ${textureCount} textures make ${expected.indexLength}`);
	}
	if (indexOffset + indexLength > fileSize) {
		outOfRange(`the index ends at ${indexOffset + indexLength}, after the end of the file (${fileSize} bytes): the file is truncated`);
	}
	if (metadataOffset % 8 !== 0 || metadataOffset < indexOffset + indexLength) {
		outOfRange(`metadataOffset is ${metadataOffset}: it must be a multiple of 8 and at least the end of the index (${indexOffset + indexLength})`);
	}
	if (metadataOffset + metadataLength > fileSize) {
		outOfRange(`the metadata ends at ${metadataOffset + metadataLength}, after the end of the file (${fileSize} bytes): the file is truncated`);
	}

	return {
		formatVersion,
		headerSize,
		flags,
		vertexLayout,
		faceLayout,
		// What a decoded node holds besides positions and indexes
		hasNormals: attributes.normal,
		hasColors: attributes.color,
		hasUvs: attributes.uv,
		vertexCount,
		faceCount,
		metadataOffset,
		metadataLength,
		indexOffset,
		indexLength,
		nodeCount,
		patchCount,
		textureCount,
		textureTableStart: expected.textureTableStart,
		sphere: {
			center: [view.getFloat32(108, true), view.getFloat32(112, true), view.getFloat32(116, true)],
			radius: view.getFloat32(120, true),
		},
		fileSize,
	};
}

/** The tables of the index, read field by field (little endian, whatever the machine). */
function readTables(bytes, header) {
	const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	const { nodeCount, patchCount, textureCount } = header;

	const nodes = {
		payloadOffset: new Float64Array(nodeCount),
		payloadLength: new Float64Array(nodeCount),
		error: new Float32Array(nodeCount),
		vertexCount: new Uint16Array(nodeCount),
		faceCount: new Uint16Array(nodeCount),
		cone: new Int16Array(nodeCount * 4),
		spheres: new Float32Array(nodeCount * 5), // x, y, z, radius of the saturated sphere, tight radius
		firstPatch: new Uint32Array(nodeCount),
	};
	let at = 0;
	for (let i = 0; i < nodeCount; i++, at += NODE_RECORD_SIZE) {
		nodes.payloadOffset[i] = readU64(view, at, `node ${i} payloadOffset`);
		nodes.payloadLength[i] = readU64(view, at + 8, `node ${i} payloadLength`);
		nodes.error[i] = view.getFloat32(at + 16, true);
		nodes.vertexCount[i] = view.getUint16(at + 20, true);
		nodes.faceCount[i] = view.getUint16(at + 22, true);
		for (let k = 0; k < 4; k++) nodes.cone[i * 4 + k] = view.getInt16(at + 24 + 2 * k, true);
		for (let k = 0; k < 4; k++) nodes.spheres[i * 5 + k] = view.getFloat32(at + 32 + 4 * k, true);
		nodes.spheres[i * 5 + 4] = view.getFloat32(at + 48, true);
		nodes.firstPatch[i] = view.getUint32(at + 52, true);
	}

	const patches = new Uint32Array(patchCount * 3); // child node, end of the patch in the triangle list, texture
	for (let i = 0; i < patchCount * 3; i++, at += 4) patches[i] = view.getUint32(at, true);

	const textures = { offset: new Float64Array(textureCount), length: new Float64Array(textureCount) };
	at = header.textureTableStart;
	for (let i = 0; i < textureCount; i++, at += TEXTURE_RECORD_SIZE) {
		textures.offset[i] = readU64(view, at, `texture ${i} offset`);
		textures.length[i] = readU64(view, at + 8, `texture ${i} length`);
	}
	return { nodes, patches, textures };
}

/**
 * Every payload and texture is a non-empty range at a multiple of 8, between the end of the index and
 * the start of the metadata block. Returns the ranges of all the sections, the header, the index and the
 * metadata block first, as `starts`/`ends` and a function that names one.
 */
function checkPayloadRanges(header, tables) {
	const { nodes, textures } = tables;
	const first = header.indexOffset + header.indexLength;
	const last = header.metadataOffset;
	const fixed = ['the header', 'the index', 'the metadata block'];
	const starts = [0, header.indexOffset, header.metadataOffset];
	const ends = [header.headerSize, first, header.metadataOffset + header.metadataLength];
	const names = [];
	const check = (what, offset, length) => {
		if (offset % 8 !== 0) fail(ErrorCode.OUT_OF_RANGE, `${what} starts at ${offset}, which is not a multiple of 8`);
		if (length === 0) fail(ErrorCode.OUT_OF_RANGE, `${what} has length 0`);
		if (offset < first || offset + length > last) {
			fail(ErrorCode.OUT_OF_RANGE, `${what} is the range ${offset} to ${offset + length}, outside the data area ${first} to ${last}`);
		}
		starts.push(offset);
		ends.push(offset + length);
		names.push(what);
	};
	for (let i = 0; i < header.nodeCount - 1; i++) check(`the payload of node ${i}`, nodes.payloadOffset[i], nodes.payloadLength[i]);
	for (let i = 0; i < header.textureCount; i++) check(`texture ${i}`, textures.offset[i], textures.length[i]);
	return { starts, ends, name: (i) => (i < fixed.length ? fixed[i] : names[i - fixed.length]) };
}

/** No two of the sections overlap (a range of length 0 overlaps nothing). */
function checkNoOverlap({ starts, ends, name }) {
	const order = Uint32Array.from(starts, (_, i) => i)
		.filter((i) => ends[i] > starts[i])
		.sort((a, b) => starts[a] - starts[b] || ends[a] - ends[b]);
	for (let k = 1; k < order.length; k++) {
		const previous = order[k - 1];
		const current = order[k];
		if (starts[current] < ends[previous]) {
			fail(ErrorCode.OVERLAPPING_SECTIONS, `${name(previous)} (${starts[previous]} to ${ends[previous]}) and ${name(current)} (${starts[current]} to ${ends[current]}) overlap`);
		}
	}
}

/** The rules of section 4.1 and 4.2, in the order of section 11. */
function checkIndex(header, tables, bytes) {
	const { nodes, patches } = tables;
	const { nodeCount, patchCount, textureCount } = header;
	const sink = nodeCount - 1;
	const bad = (what) => fail(ErrorCode.BAD_INDEX, what);
	const patchesOf = (node) => [nodes.firstPatch[node], nodes.firstPatch[node + 1]];

	if (nodes.firstPatch[0] !== 0) bad(`firstPatch of node 0 is ${nodes.firstPatch[0]}, not 0`);
	for (let i = 1; i < nodeCount; i++) {
		if (nodes.firstPatch[i] < nodes.firstPatch[i - 1]) bad(`firstPatch decreases from ${nodes.firstPatch[i - 1]} (node ${i - 1}) to ${nodes.firstPatch[i]} (node ${i})`);
	}
	if (nodes.firstPatch[sink] !== patchCount) bad(`firstPatch of the sink is ${nodes.firstPatch[sink]}, not patchCount (${patchCount})`);

	// All the fields of the sink but firstPatch are 0 (its first 52 bytes)
	const sinkRecord = bytes.subarray(sink * NODE_RECORD_SIZE, sink * NODE_RECORD_SIZE + 52);
	if (sinkRecord.some((byte) => byte !== 0)) bad('a field of the sink other than firstPatch is not 0');

	for (let i = 0; i < sink; i++) {
		const [from, to] = patchesOf(i);
		if (from === to) bad(`node ${i} has no patch`);
	}
	for (let i = 0; i < sink; i++) {
		if (nodes.faceCount[i] === 0) bad(`node ${i} has no triangle`);
		if (nodes.vertexCount[i] < 3) bad(`node ${i} has ${nodes.vertexCount[i]} vertices, fewer than 3`);
	}
	for (let i = 0; i < sink; i++) {
		const [from, to] = patchesOf(i);
		for (let p = from; p < to; p++) {
			const child = patches[p * 3];
			if (child >= nodeCount) bad(`patch ${p} of node ${i} names node ${child}, and there are ${nodeCount} nodes`);
			if (child !== sink && child <= i) bad(`patch ${p} of node ${i} names node ${child}, which does not come after it`);
		}
	}
	for (let i = 0; i < sink; i++) {
		const [from, to] = patchesOf(i);
		let sinks = 0;
		for (let p = from; p < to; p++) if (patches[p * 3] === sink) sinks++;
		if (sinks !== 0 && sinks !== to - from) bad(`node ${i} has patches that name the sink and patches that do not`);
	}
	for (let i = 0; i < sink; i++) {
		const [from, to] = patchesOf(i);
		for (let p = from + 1; p < to; p++) {
			if (patches[p * 3 + 1] < patches[(p - 1) * 3 + 1]) bad(`triangleOffset decreases from ${patches[(p - 1) * 3 + 1]} to ${patches[p * 3 + 1]} in node ${i} (patch ${p})`);
		}
		if (patches[(to - 1) * 3 + 1] !== nodes.faceCount[i]) bad(`the last triangleOffset of node ${i} is ${patches[(to - 1) * 3 + 1]} and its faceCount is ${nodes.faceCount[i]}`);
	}
	for (let i = 0; i < sink; i++) {
		const [from, to] = patchesOf(i);
		const texture = patches[from * 3 + 2];
		for (let p = from; p < to; p++) {
			const t = patches[p * 3 + 2];
			if (t !== NO_TEXTURE && t >= textureCount) bad(`patch ${p} of node ${i} names texture ${t}, and there are ${textureCount} textures`);
			if (t !== texture) bad(`the patches of node ${i} name different textures (${texture} and ${t})`);
		}
	}
	for (let i = 0; i < sink; i++) {
		const [from, to] = patchesOf(i);
		for (let p = from; p < to; p++) {
			const has = patches[p * 3 + 2] !== NO_TEXTURE;
			if (header.hasUvs && !has) bad(`patch ${p} of node ${i} has no texture and the vertices have texture coordinates`);
			if (!header.hasUvs && has) bad(`patch ${p} of node ${i} has a texture and the vertices have no texture coordinates`);
		}
	}
}

/** The roots of the hierarchy: the nodes 0 .. rootCount - 1 (section 4.2). */
function countRoots(nodeCount, firstPatch, patches) {
	let rootCount = nodeCount - 1;
	for (let j = 0; j < rootCount; j++) {
		for (let p = firstPatch[j]; p < firstPatch[j + 1]; p++) {
			if (patches[p * 3] < rootCount) rootCount = patches[p * 3];
		}
	}
	return rootCount;
}

/**
 * Parses and checks the index (the rest of section 11): `bytes` is the whole index, `header` what
 * parseHeader returned. Returns the header fields with `nodes`, `patches`, `textures`, `rootCount`
 * and `sink`.
 */
export function parseIndex(bytes, header) {
	if (bytes.byteLength !== header.indexLength) {
		fail(ErrorCode.OUT_OF_RANGE, `${bytes.byteLength} bytes of the index could be read, the index has ${header.indexLength}: the file is truncated`);
	}
	const tables = readTables(bytes, header);
	const payloads = checkPayloadRanges(header, tables);
	checkNoOverlap(payloads);
	checkIndex(header, tables, bytes);
	return {
		...header,
		...tables,
		sink: header.nodeCount - 1,
		rootCount: countRoots(header.nodeCount, tables.nodes.firstPatch, tables.patches),
	};
}

/** The metadata block as an object (section 7). A length of 0 is the empty object. */
export function parseMetadata(bytes) {
	if (bytes.byteLength === 0) return {};
	if (bytes.byteLength > MAX_METADATA_LENGTH) {
		fail(ErrorCode.BAD_METADATA, `the block has ${bytes.byteLength} bytes, more than ${MAX_METADATA_LENGTH}`);
	}
	let value;
	try {
		const text = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(bytes);
		value = JSON.parse(text);
	} catch (cause) {
		throw new Bm3dError(ErrorCode.BAD_METADATA, `the block is not valid UTF-8 JSON (${cause.message})`, { cause });
	}
	if (value === null || typeof value !== 'object' || Array.isArray(value)) {
		fail(ErrorCode.BAD_METADATA, 'the block is JSON but not an object');
	}
	return value;
}

/** A texture is a WebP image: RIFF....WEBP (section 6). */
export function checkTexture(bytes, index) {
	const ascii = (from, to) => String.fromCharCode(...bytes.subarray(from, to));
	if (bytes.byteLength < 12 || ascii(0, 4) !== 'RIFF' || ascii(8, 12) !== 'WEBP') {
		fail(ErrorCode.CORRUPT_TEXTURE, `texture ${index} is not a WebP image (${bytes.byteLength} bytes)`);
	}
}

/**
 * Opens a file: the header, then the index, with every check of section 11. `source` is
 * `{ size, read(start, end) }` (see source.js). Nothing else is read.
 */
export async function openBm3d(source) {
	return readIndex(source, await openHeader(source));
}

/** Reads the index of a file whose header has been read, and checks it. */
export async function readIndex(source, header) {
	const bytes = await source.read(header.indexOffset, header.indexOffset + header.indexLength);
	return parseIndex(new Uint8Array(bytes), header);
}

/** Opens a file as far as its header (enough for the metadata block). */
export async function openHeader(source) {
	return parseHeader(await readHeaderBytes(source), source.size);
}

async function readHeaderBytes(source) {
	return new Uint8Array(await source.read(0, Math.min(HEADER_SIZE, source.size)));
}

/** Reads and parses the metadata block of a header (section 7). */
export async function readMetadata(source, header) {
	if (header.metadataLength === 0) return {};
	if (header.metadataLength > MAX_METADATA_LENGTH) {
		fail(ErrorCode.BAD_METADATA, `the block has ${header.metadataLength} bytes, more than ${MAX_METADATA_LENGTH}`);
	}
	const bytes = await source.read(header.metadataOffset, header.metadataOffset + header.metadataLength);
	return parseMetadata(new Uint8Array(bytes));
}
