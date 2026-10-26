// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// A writer of the bm3d format, version 1, written from docs/FORMAT.md for the tests (the C++
// writer is the real one). It takes the nodes with their payloads already encoded and lays out
// the file as section 2 and section 12 say. The parts of a file a test wants to break are
// changed afterwards, in the bytes, with the offsets below.

export const HEADER = { magic: 0, version: 4, headerSize: 8, flags: 12, vertexLayout: 16, faceLayout: 32, vertexCount: 48, faceCount: 56, metadataOffset: 64, metadataLength: 72, indexOffset: 80, indexLength: 88, nodeCount: 96, patchCount: 100, textureCount: 104, sphere: 108 };
export const NODE = { payloadOffset: 0, payloadLength: 8, error: 16, vertexCount: 20, faceCount: 22, cone: 24, sphere: 32, tightRadius: 48, firstPatch: 52 };
export const NODE_SIZE = 56;
export const PATCH_SIZE = 12;
export const TEXTURE_SIZE = 16;

const align8 = (n) => Math.ceil(n / 8) * 8;

/** The layout of section 3.2: `[type, number]` pairs, padded with empty slots to 8. */
function layout(slots) {
	const bytes = new Uint8Array(16);
	slots.forEach(([type, number], i) => {
		bytes[2 * i] = type;
		bytes[2 * i + 1] = number;
	});
	return bytes;
}

/**
 * spec:
 *   nodes      the real nodes: { payload: Uint8Array, error, vertexCount, faceCount, sphere: [x, y, z, radius],
 *              tightRadius, cone?: [4 numbers], patches: [{ node, triangleOffset, texture? }] }
 *              (a `node` of -1 names the sink)
 *   textures   Uint8Array[] (WebP images)
 *   normals, colors   whether the vertex layout has them (uvs follow from the textures)
 *   sphere     { center: [x, y, z], radius } of the model
 *   metadata   an object (written as JSON), a string, bytes, or null/undefined for none
 * Returns the file as a Uint8Array.
 */
export function buildBm3d(spec) {
	const nodes = spec.nodes;
	const textures = spec.textures || [];
	const nodeCount = nodes.length + 1;
	const patchCount = nodes.reduce((sum, node) => sum + node.patches.length, 0);
	const textureCount = textures.length;
	const hasUvs = textureCount > 0;

	const textureTableStart = align8(NODE_SIZE * nodeCount + PATCH_SIZE * patchCount);
	const indexLength = textureTableStart + TEXTURE_SIZE * textureCount;
	const indexOffset = 128;

	let cursor = indexOffset + indexLength;
	const payloadOffsets = nodes.map((node) => {
		const offset = cursor;
		cursor = align8(cursor + node.payload.length);
		return offset;
	});
	const textureOffsets = textures.map((texture) => {
		const offset = cursor;
		cursor = align8(cursor + texture.length);
		return offset;
	});
	const metadataOffset = cursor;
	const metadata = encodeMetadata(spec.metadata);
	const size = metadataOffset + metadata.length;

	const bytes = new Uint8Array(size);
	const view = new DataView(bytes.buffer);
	const u64 = (at, value) => {
		view.setUint32(at, value % 4294967296, true);
		view.setUint32(at + 4, Math.floor(value / 4294967296), true);
	};

	// header
	bytes.set([0x42, 0x4d, 0x33, 0x44], HEADER.magic);
	view.setUint32(HEADER.version, 1, true);
	view.setUint32(HEADER.headerSize, 128, true);
	view.setUint32(HEADER.flags, 1 | (hasUvs ? 2 : 0), true);
	const FLOAT = 7;
	const SHORT = 3;
	const UNSIGNED_BYTE = 2;
	const UNSIGNED_SHORT = 4;
	bytes.set(layout([[FLOAT, 3], spec.normals === false ? [0, 0] : [SHORT, 3], spec.colors ? [UNSIGNED_BYTE, 4] : [0, 0], hasUvs ? [FLOAT, 2] : [0, 0]]), HEADER.vertexLayout);
	bytes.set(layout([[UNSIGNED_SHORT, 3]]), HEADER.faceLayout);
	u64(HEADER.vertexCount, nodes.reduce((sum, node) => sum + node.vertexCount, 0));
	u64(HEADER.faceCount, nodes.reduce((sum, node) => sum + node.faceCount, 0));
	u64(HEADER.metadataOffset, metadataOffset);
	u64(HEADER.metadataLength, metadata.length);
	u64(HEADER.indexOffset, indexOffset);
	u64(HEADER.indexLength, indexLength);
	view.setUint32(HEADER.nodeCount, nodeCount, true);
	view.setUint32(HEADER.patchCount, patchCount, true);
	view.setUint32(HEADER.textureCount, textureCount, true);
	const { center, radius } = spec.sphere || { center: [0, 0, 0], radius: 1 };
	center.forEach((c, i) => view.setFloat32(HEADER.sphere + 4 * i, c, true));
	view.setFloat32(HEADER.sphere + 12, radius, true);

	// index: nodes, then patches, then textures
	const sink = nodeCount - 1;
	let patchIndex = 0;
	nodes.forEach((node, i) => {
		const at = indexOffset + NODE_SIZE * i;
		u64(at + NODE.payloadOffset, payloadOffsets[i]);
		u64(at + NODE.payloadLength, node.payload.length);
		view.setFloat32(at + NODE.error, node.error, true);
		view.setUint16(at + NODE.vertexCount, node.vertexCount, true);
		view.setUint16(at + NODE.faceCount, node.faceCount, true);
		(node.cone || [0, 0, 0, 0]).forEach((c, k) => view.setInt16(at + NODE.cone + 2 * k, c, true));
		node.sphere.forEach((c, k) => view.setFloat32(at + NODE.sphere + 4 * k, c, true));
		view.setFloat32(at + NODE.tightRadius, node.tightRadius, true);
		view.setUint32(at + NODE.firstPatch, patchIndex, true);
		patchIndex += node.patches.length;
	});
	view.setUint32(indexOffset + NODE_SIZE * sink + NODE.firstPatch, patchCount, true);

	const patchTable = indexOffset + NODE_SIZE * nodeCount;
	nodes.flatMap((node) => node.patches).forEach((patch, i) => {
		const at = patchTable + PATCH_SIZE * i;
		view.setUint32(at, patch.node < 0 ? sink : patch.node, true);
		view.setUint32(at + 4, patch.triangleOffset, true);
		view.setUint32(at + 8, patch.texture === undefined ? 0xffffffff : patch.texture, true);
	});

	textures.forEach((texture, i) => {
		const at = indexOffset + textureTableStart + TEXTURE_SIZE * i;
		u64(at, textureOffsets[i]);
		u64(at + 8, texture.length);
	});

	// payloads, textures, metadata
	nodes.forEach((node, i) => bytes.set(node.payload, payloadOffsets[i]));
	textures.forEach((texture, i) => bytes.set(texture, textureOffsets[i]));
	bytes.set(metadata, metadataOffset);
	return bytes;
}

function encodeMetadata(metadata) {
	if (metadata === null || metadata === undefined) return new Uint8Array(0);
	if (metadata instanceof Uint8Array) return metadata;
	return new TextEncoder().encode(typeof metadata === 'string' ? metadata : JSON.stringify(metadata));
}

/** Offsets in the file of the parts a test changes: the node, patch and texture records. */
export function recordOffsets(bytes) {
	const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	const indexOffset = Number(view.getBigUint64(HEADER.indexOffset, true));
	const nodeCount = view.getUint32(HEADER.nodeCount, true);
	const patchCount = view.getUint32(HEADER.patchCount, true);
	const textureTableStart = align8(NODE_SIZE * nodeCount + PATCH_SIZE * patchCount);
	return {
		node: (i) => indexOffset + NODE_SIZE * i,
		patch: (i) => indexOffset + NODE_SIZE * nodeCount + PATCH_SIZE * i,
		texture: (i) => indexOffset + textureTableStart + TEXTURE_SIZE * i,
	};
}
