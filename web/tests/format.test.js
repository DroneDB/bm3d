// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa

import { describe, expect, it } from 'vitest';

import { Bm3dError, ErrorCode, openBm3d, openHeader, parseMetadata, readMetadata } from '../src/format.js';
import { bufferSource } from '../src/source.js';
import { workedExample } from './support/doc.js';
import { model, modelSpec } from './support/models.js';
import { buildBm3d, HEADER, NODE, recordOffsets } from './support/writer.js';

const open = (bytes) => openBm3d(bufferSource(bytes));

/** Opens `bytes` and returns the error, which must be a Bm3dError. */
async function openError(bytes) {
	try {
		await open(bytes);
	} catch (error) {
		expect(error).toBeInstanceOf(Bm3dError);
		return error;
	}
	throw new Error('the file was opened');
}

function edit(bytes, change) {
	const copy = Uint8Array.from(bytes);
	change(new DataView(copy.buffer), copy, recordOffsets(copy));
	return copy;
}

function setU64(view, at, high, low) {
	view.setUint32(at, low, true);
	view.setUint32(at + 4, high, true);
}

describe('the worked example of the document', () => {
	it('is written byte for byte, and read back', async () => {
		const example = workedExample();
		const spec = {
			nodes: [
				{
					payload: new Uint8Array(120).fill(0xa5), // "imagined to be 120 bytes long"
					error: Math.fround(0.70710677),
					vertexCount: 4,
					faceCount: 2,
					cone: [0, 0, 32766, 32760],
					sphere: [0.5, 0.5, 0, Math.fround(0.70710677)],
					tightRadius: Math.fround(0.70710677),
					patches: [{ node: -1, triangleOffset: 2 }],
				},
			],
			sphere: { center: [0.5, 0.5, 0], radius: Math.fround(0.70710677) },
			metadata: example.metadata,
		};
		const bytes = buildBm3d(spec);
		expect(bytes.length).toBe(739);
		expect(Array.from(bytes.subarray(0, 128))).toEqual(Array.from(example.header));
		expect(Array.from(bytes.subarray(128, 184))).toEqual(Array.from(example.node));
		// the sink: 52 zero bytes and firstPatch = 1
		expect(Array.from(bytes.subarray(184, 240))).toEqual([...new Array(52).fill(0), 1, 0, 0, 0]);
		// the patch, then 4 bytes of padding up to the texture table
		expect(Array.from(bytes.subarray(240, 256))).toEqual([1, 0, 0, 0, 2, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0]);
		expect(new TextDecoder().decode(bytes.subarray(376))).toBe(example.metadata);

		const file = await open(bytes);
		expect(file).toMatchObject({ formatVersion: 1, nodeCount: 2, patchCount: 1, textureCount: 0, rootCount: 1, sink: 1, indexOffset: 128, indexLength: 128, metadataOffset: 376, metadataLength: 363 });
		expect(file.vertexLayout.slice(0, 4)).toEqual([{ type: 7, number: 3 }, { type: 3, number: 3 }, { type: 0, number: 0 }, { type: 0, number: 0 }]);
		expect(file.hasNormals).toBe(true);
		expect(file.hasColors).toBe(false);
		expect(file.hasUvs).toBe(false);
		expect(file.nodes.payloadOffset[0]).toBe(256);
		expect(file.nodes.payloadLength[0]).toBe(120);
		expect(Array.from(file.nodes.cone.subarray(0, 4))).toEqual([0, 0, 32766, 32760]);
		expect(file.nodes.error[0]).toBeCloseTo(0.70710677, 7);
		expect(Array.from(file.patches)).toEqual([1, 2, 0xffffffff]);
		expect(file.sphere.center).toEqual([0.5, 0.5, 0]);
		expect(JSON.parse(example.metadata).source.name).toBe('quad.obj');
		expect(await readMetadata(bufferSource(bytes), file)).toEqual(JSON.parse(example.metadata));
	});
});

describe('a valid file', () => {
	it('is opened, with its tables', async () => {
		const bytes = model();
		const file = await open(bytes);
		expect(file).toMatchObject({ nodeCount: 6, patchCount: 8, textureCount: 3, rootCount: 1, sink: 5, vertexCount: 74, faceCount: 46, fileSize: bytes.length });
		expect(file.hasUvs).toBe(true);
		expect(Array.from(file.nodes.vertexCount)).toEqual([10, 12, 12, 20, 20, 0]);
		expect(Array.from(file.nodes.firstPatch)).toEqual([0, 2, 4, 5, 6, 8]);
		expect(Array.from(file.patches.subarray(0, 6))).toEqual([1, 3, 0, 2, 6, 0]);
		expect(Array.from(file.textures.length)).toEqual([40, 53, 64]);
		for (const offset of [...file.nodes.payloadOffset.subarray(0, 5), ...file.textures.offset]) expect(offset % 8).toBe(0);
	});

	it('reads the optional attributes of the layouts', async () => {
		const plain = await open(model({ textures: false, colors: true, normals: false }));
		expect(plain).toMatchObject({ hasNormals: false, hasColors: true, hasUvs: false, textureCount: 0 });
	});

	it('finds the roots as the document derives them', async () => {
		// two roots: node 0 and node 1 are not the children of any node
		const spec = modelSpec({ textures: false });
		spec.nodes[0].patches = [{ node: 2, triangleOffset: 6 }];
		spec.nodes[1].patches = [{ node: 3, triangleOffset: 8 }];
		spec.nodes[2].patches = [{ node: -1, triangleOffset: 8 }];
		spec.nodes[3].patches = [{ node: -1, triangleOffset: 12 }];
		spec.nodes[4].patches = [{ node: -1, triangleOffset: 12 }];
		expect((await open(buildBm3d(spec))).rootCount).toBe(2);
	});

	it('accepts a header longer than 128 bytes and bytes after the metadata block', async () => {
		const bytes = model();
		const longer = new Uint8Array(bytes.length + 5);
		longer.set(bytes);
		expect((await open(longer)).metadataLength).toBe(bytes.length - new DataView(bytes.buffer).getUint32(HEADER.metadataOffset, true));
	});

	it('takes a metadata block of length 0 as the empty object', async () => {
		const bytes = model({}, (spec) => (spec.metadata = null));
		const file = await open(bytes);
		expect(file.metadataLength).toBe(0);
		expect(await readMetadata(bufferSource(bytes), file)).toEqual({});
	});
});

describe('the open-time errors, in the order of the document', () => {
	const good = model();

	const cases = [
		// [name, code, change, text of the message]
		['fewer than 128 bytes', ErrorCode.TRUNCATED_HEADER, () => good.slice(0, 100), '100 bytes'],
		['an empty file', ErrorCode.TRUNCATED_HEADER, () => new Uint8Array(0), '0 bytes'],
		['a wrong magic number', ErrorCode.BAD_MAGIC, () => edit(good, (v, b) => b.set([0x58, 0x4d, 0x33, 0x44], 0)), '58 4d 33 44'],
		['the magic number of a Nexus file', ErrorCode.BAD_MAGIC, () => edit(good, (v, b) => b.set([0x20, 0x73, 0x78, 0x4e], 0)), 'Nexus'],
		['a newer version', ErrorCode.UNSUPPORTED_VERSION, () => edit(good, (v) => v.setUint32(HEADER.version, 2, true)), 'version 2 and this reader reads up to version 1'],
		['version 0', ErrorCode.UNSUPPORTED_VERSION, () => edit(good, (v) => v.setUint32(HEADER.version, 0, true)), 'version 0'],
		['a huge version', ErrorCode.UNSUPPORTED_VERSION, () => edit(good, (v) => v.setUint32(HEADER.version, 0xffffffff, true)), '4294967295'],
		['a header size below 128', ErrorCode.BAD_HEADER_SIZE, () => edit(good, (v) => v.setUint32(HEADER.headerSize, 120, true)), '120'],
		['a header size that is not a multiple of 8', ErrorCode.BAD_HEADER_SIZE, () => edit(good, (v) => v.setUint32(HEADER.headerSize, 132, true)), '132'],
		['a header larger than the file', ErrorCode.BAD_HEADER_SIZE, () => edit(good, (v) => v.setUint32(HEADER.headerSize, good.length + 8, true)), String(good.length)],
		['no CORTO_COMPRESSED', ErrorCode.COMPRESSION_FLAG_MISSING, () => edit(good, (v) => v.setUint32(HEADER.flags, 2, true)), 'CORTO_COMPRESSED'],
		['no flags at all', ErrorCode.COMPRESSION_FLAG_MISSING, () => edit(good, (v) => v.setUint32(HEADER.flags, 0, true)), '0x0'],
		['an unknown flag', ErrorCode.UNKNOWN_FLAGS, () => edit(good, (v) => v.setUint32(HEADER.flags, 3 | 4, true)), '0x4'],
		['the highest flag', ErrorCode.UNKNOWN_FLAGS, () => edit(good, (v) => v.setUint32(HEADER.flags, 0x80000003, true)), '0x80000000'],
		['textures without TEXTURE_WEBP', ErrorCode.UNKNOWN_FLAGS, () => edit(good, (v) => v.setUint32(HEADER.flags, 1, true)), 'TEXTURE_WEBP'],
		['TEXTURE_WEBP without textures', ErrorCode.UNKNOWN_FLAGS, () => edit(model({ textures: false }), (v) => v.setUint32(HEADER.flags, 3, true)), 'TEXTURE_WEBP'],
		['a position that is not FLOAT x 3', ErrorCode.UNSUPPORTED_LAYOUT, () => edit(good, (v, b) => (b[HEADER.vertexLayout] = 8)), 'position'],
		['a normal that is not SHORT x 3', ErrorCode.UNSUPPORTED_LAYOUT, () => edit(good, (v, b) => (b[HEADER.vertexLayout + 2] = 7)), 'normal'],
		['a colour with 3 components', ErrorCode.UNSUPPORTED_LAYOUT, () => edit(model({ colors: true }), (v, b) => (b[HEADER.vertexLayout + 5] = 3)), 'colour'],
		['texture coordinates of the wrong type', ErrorCode.UNSUPPORTED_LAYOUT, () => edit(good, (v, b) => (b[HEADER.vertexLayout + 6] = 5)), 'texture coordinates'],
		['texture coordinates missing with textures', ErrorCode.UNSUPPORTED_LAYOUT, () => edit(good, (v, b) => b.fill(0, HEADER.vertexLayout + 6, HEADER.vertexLayout + 8)), 'textureCount is 3'],
		['a fifth vertex attribute', ErrorCode.UNSUPPORTED_LAYOUT, () => edit(good, (v, b) => b.set([7, 1], HEADER.vertexLayout + 8)), 'slot 4'],
		['32-bit face indexes', ErrorCode.UNSUPPORTED_LAYOUT, () => edit(good, (v, b) => (b[HEADER.faceLayout] = 6)), 'indexes'],
		['a second face attribute', ErrorCode.UNSUPPORTED_LAYOUT, () => edit(good, (v, b) => b.set([7, 3], HEADER.faceLayout + 2)), 'face slot 1'],
		['a slot with a type and no components', ErrorCode.UNSUPPORTED_LAYOUT, () => edit(good, (v, b) => b.set([0, 3], HEADER.vertexLayout + 2)), 'normal'],
		['one node', ErrorCode.BAD_COUNTS, () => edit(good, (v) => v.setUint32(HEADER.nodeCount, 1, true)), 'nodeCount is 1'],
		['no node', ErrorCode.BAD_COUNTS, () => edit(good, (v) => v.setUint32(HEADER.nodeCount, 0, true)), 'nodeCount is 0'],
	];
	for (const [name, code, change, text] of cases) {
		it(`${code}: ${name}`, async () => {
			const error = await openError(change());
			expect(error.code).toBe(code);
			expect(error.message).toContain(text);
		});
	}

	it('reports the first error when there are several', async () => {
		const bad = edit(good, (v, b) => {
			b[0] = 0; // magic
			v.setUint32(HEADER.version, 7, true);
			v.setUint32(HEADER.flags, 0, true);
		});
		expect((await openError(bad)).code).toBe(ErrorCode.BAD_MAGIC);
		const flags = edit(good, (v, b) => {
			v.setUint32(HEADER.flags, 0, true);
			b[HEADER.vertexLayout] = 9;
			v.setUint32(HEADER.nodeCount, 0, true);
		});
		expect((await openError(flags)).code).toBe(ErrorCode.COMPRESSION_FLAG_MISSING);
	});
});

describe('out of range', () => {
	const good = model();
	const header = (field, value) => edit(good, (v) => v.setUint32(field, value, true));
	const node = (i, field, value, size = 8) => edit(good, (v, b, at) => (size === 8 ? setU64(v, at.node(i) + field, 0, value) : v.setUint32(at.node(i) + field, value, true)));
	const texture = (i, field, value) => edit(good, (v, b, at) => setU64(v, at.texture(i) + field, 0, value));
	const fileSize = good.length;
	const metadataOffset = new DataView(good.buffer).getUint32(HEADER.metadataOffset, true);

	const cases = [
		['indexOffset that is not a multiple of 8', header(HEADER.indexOffset, 132), 'indexOffset'],
		['indexOffset inside the header', header(HEADER.indexOffset, 120), 'indexOffset'],
		['indexLength that the counts do not make', header(HEADER.indexLength, new DataView(good.buffer).getUint32(HEADER.indexLength, true) + 8), 'indexLength'],
		['an index that ends after the file', good.slice(0, 300), 'truncated'],
		['a metadata block that ends after the file', good.slice(0, fileSize - 4), 'truncated'],
		['metadataOffset that is not a multiple of 8', header(HEADER.metadataOffset, metadataOffset + 4), 'metadataOffset'],
		['metadataOffset inside the index', header(HEADER.metadataOffset, 200), 'metadataOffset'],
		['a payload that starts at a number that is not a multiple of 8', node(1, NODE.payloadOffset, new DataView(good.buffer).getUint32(recordOffsets(good).node(1), true) + 4), 'node 1'],
		['a payload of length 0', node(2, NODE.payloadLength, 0), 'node 2'],
		['a payload that ends after the data area', node(3, NODE.payloadLength, 100000), 'node 3'],
		['a payload inside the index', node(0, NODE.payloadOffset, 128), 'node 0'],
		['a payload in the metadata block', node(4, NODE.payloadOffset, metadataOffset), 'node 4'],
		['a texture that is not aligned', texture(1, 0, new DataView(good.buffer).getUint32(recordOffsets(good).texture(1), true) + 1), 'texture 1'],
		['a texture of length 0', texture(2, 8, 0), 'texture 2'],
		['a texture after the data area', texture(0, 0, metadataOffset), 'texture 0'],
	];
	for (const [name, bytes, text] of cases) {
		it(name, async () => {
			const error = await openError(bytes);
			expect(error.code).toBe(ErrorCode.OUT_OF_RANGE);
			expect(error.message).toContain(text);
		});
	}

	it('compares sums that are not safe integers without rounding them in', async () => {
		// payloadOffset 2^53 - 8 (the highest safe multiple of 8) and a length of 16: the sum is not exact, and is above the file
		const bytes = edit(good, (v, b, at) => {
			setU64(v, at.node(1) + NODE.payloadOffset, 0x1fffff, 0xfffffff8);
			setU64(v, at.node(1) + NODE.payloadLength, 0, 16);
		});
		expect((await openError(bytes)).code).toBe(ErrorCode.OUT_OF_RANGE);
	});
});

describe('overlapping sections', () => {
	const good = model();
	const payload = (i, bytes) => new DataView(bytes.buffer).getUint32(recordOffsets(bytes).node(i), true);

	it('two payloads at the same place', async () => {
		const bytes = edit(good, (v, b, at) => setU64(v, at.node(2) + NODE.payloadOffset, 0, payload(1, good)));
		const error = await openError(bytes);
		expect(error.code).toBe(ErrorCode.OVERLAPPING_SECTIONS);
		expect(error.message).toContain('node 1');
		expect(error.message).toContain('node 2');
	});

	it('a payload that starts inside the previous one', async () => {
		const bytes = edit(good, (v, b, at) => setU64(v, at.node(1) + NODE.payloadOffset, 0, payload(0, good) + 8));
		expect((await openError(bytes)).code).toBe(ErrorCode.OVERLAPPING_SECTIONS);
	});

	it('a texture over a payload', async () => {
		const bytes = edit(good, (v, b, at) => setU64(v, at.texture(0), 0, payload(3, good)));
		const error = await openError(bytes);
		expect(error.code).toBe(ErrorCode.OVERLAPPING_SECTIONS);
		expect(error.message).toContain('texture 0');
	});

	it('is reported after the ranges, which come first', async () => {
		const bytes = edit(good, (v, b, at) => {
			setU64(v, at.node(2) + NODE.payloadOffset, 0, payload(1, good)); // overlap
			setU64(v, at.node(4) + NODE.payloadLength, 0, 0); // out of range
		});
		expect((await openError(bytes)).code).toBe(ErrorCode.OUT_OF_RANGE);
	});
});

describe('bad index', () => {
	const good = model();
	const cases = [
		['the first node has a first patch', (v, b, at) => v.setUint32(at.node(0) + NODE.firstPatch, 1, true), 'firstPatch of node 0'],
		['firstPatch that decreases', (v, b, at) => v.setUint32(at.node(2) + NODE.firstPatch, 1, true), 'decreases'],
		['a sink that does not end the patches', (v, b, at) => v.setUint32(at.node(5) + NODE.firstPatch, 7, true), 'sink is 7'],
		['a payload offset in the sink', (v, b, at) => v.setUint32(at.node(5) + NODE.payloadOffset, 8, true), 'sink'],
		['an error in the sink', (v, b, at) => v.setFloat32(at.node(5) + NODE.error, 1, true), 'sink'],
		['a negative zero in the sink', (v, b, at) => v.setFloat32(at.node(5) + NODE.tightRadius, -0, true), 'sink'],
		['a node without patch', (v, b, at) => v.setUint32(at.node(1) + NODE.firstPatch, 0, true), 'node 0 has no patch'],
		['a node without triangle', (v, b, at) => v.setUint16(at.node(3) + NODE.faceCount, 0, true), 'node 3 has no triangle'],
		['a node with two vertices', (v, b, at) => v.setUint16(at.node(3) + NODE.vertexCount, 2, true), 'node 3 has 2 vertices'],
		['a child beyond the table', (v, b, at) => v.setUint32(at.patch(0), 9, true), 'names node 9'],
		['a child that is the node itself', (v, b, at) => v.setUint32(at.patch(2), 1, true), 'does not come after'],
		['a child before the node', (v, b, at) => v.setUint32(at.patch(4), 1, true), 'does not come after'],
		['patches that name the sink and others', (v, b, at) => v.setUint32(at.patch(3), 5, true), 'node 1 has patches that name the sink'],
		['a triangle offset that decreases', (v, b, at) => v.setUint32(at.patch(7) + 4, 3, true), 'triangleOffset decreases from 5 to 3'],
		['a last triangle offset that is not the face count', (v, b, at) => v.setUint32(at.patch(5) + 4, 11, true), 'faceCount'],
		['a texture beyond the table', (v, b, at) => v.setUint32(at.patch(2) + 8, 3, true), 'texture 3'],
		['patches of a node with different textures', (v, b, at) => v.setUint32(at.patch(1) + 8, 2, true), 'different textures'],
		['a node without texture in a model with textures', (v, b, at) => {
			v.setUint32(at.patch(5) + 8, 0xffffffff, true);
			v.setUint32(at.patch(6) + 8, 0xffffffff, true);
			v.setUint32(at.patch(7) + 8, 0xffffffff, true);
		}, 'has no texture'],
	];
	for (const [name, change, text] of cases) {
		it(name, async () => {
			const error = await openError(edit(good, change));
			expect(error.code).toBe(ErrorCode.BAD_INDEX);
			expect(error.message).toContain(text);
		});
	}

	it('a patch with a texture in a model without textures', async () => {
		const bytes = model({ textures: false });
		const error = await openError(edit(bytes, (v, b, at) => v.setUint32(at.patch(1) + 8, 0, true)));
		expect(error.code).toBe(ErrorCode.BAD_INDEX);
		expect(error.message).toContain('texture 0');
	});

	it('checks the rules in the order of the document', async () => {
		// a decreasing firstPatch and a node without triangles: the first rule speaks
		const bytes = edit(good, (v, b, at) => {
			v.setUint32(at.node(2) + NODE.firstPatch, 1, true);
			v.setUint16(at.node(3) + NODE.faceCount, 0, true);
		});
		expect((await openError(bytes)).message).toContain('decreases');
		// a node without triangles, and a child that is not after its node
		const second = edit(good, (v, b, at) => {
			v.setUint16(at.node(3) + NODE.faceCount, 0, true);
			v.setUint32(at.patch(2), 1, true);
		});
		expect((await openError(second)).message).toContain('node 3 has no triangle');
	});

	it('is reported after overlaps', async () => {
		const bytes = edit(good, (v, b, at) => {
			setU64(v, at.node(2) + NODE.payloadOffset, 0, new DataView(good.buffer).getUint32(at.node(1), true));
			v.setUint32(at.node(0) + NODE.firstPatch, 1, true);
		});
		expect((await openError(bytes)).code).toBe(ErrorCode.OVERLAPPING_SECTIONS);
	});
});

describe('u64 values that a JavaScript number cannot hold', () => {
	const good = model();
	const cases = [
		['vertexCount of the header', (v) => setU64(v, HEADER.vertexCount, 0x200000, 0), 'vertexCount'],
		['indexOffset', (v) => setU64(v, HEADER.indexOffset, 0xffffffff, 0xffffffff), 'indexOffset'],
		['metadataLength', (v) => setU64(v, HEADER.metadataLength, 0x200000, 1), 'metadataLength'],
		['the payloadOffset of a node', (v, b, at) => setU64(v, at.node(1) + NODE.payloadOffset, 0x200000, 8), 'node 1 payloadOffset'],
		['the payloadLength of a node', (v, b, at) => setU64(v, at.node(2) + NODE.payloadLength, 0x80000000, 0), 'node 2 payloadLength'],
		['the length of a texture', (v, b, at) => setU64(v, at.texture(2) + 8, 0x200000, 0), 'texture 2 length'],
	];
	for (const [name, change, text] of cases) {
		it(name, async () => {
			const error = await openError(edit(good, change));
			expect(error.code).toBe(ErrorCode.VALUE_TOO_LARGE);
			expect(error.message).toContain(text);
		});
	}

	it('accepts 2^53 - 1, the highest safe integer, and finds it out of range instead', async () => {
		const error = await openError(edit(good, (v) => setU64(v, HEADER.metadataLength, 0x1fffff, 0xffffffff)));
		expect(error.code).toBe(ErrorCode.OUT_OF_RANGE);
		expect(error.message).toContain('truncated');
	});
});

describe('the metadata block', () => {
	const parse = (text) => parseMetadata(typeof text === 'string' ? new TextEncoder().encode(text) : text);
	const badMetadata = (value) => {
		try {
			parse(value);
		} catch (error) {
			expect(error).toBeInstanceOf(Bm3dError);
			return error;
		}
		throw new Error('the metadata was accepted');
	};

	it('is parsed when asked for', async () => {
		const bytes = model();
		const source = bufferSource(bytes);
		const metadata = await readMetadata(source, await openHeader(source));
		expect(metadata).toEqual({ generator: 'bm3d 1.0.0', source: { name: 'test.obj', vertices: 94, faces: 56 } });
	});

	it('keeps the keys it does not know', () => {
		expect(parse('{"generator":"x","crs":"EPSG:32633","bm3dNext":[1,2]}')).toEqual({ generator: 'x', crs: 'EPSG:32633', bm3dNext: [1, 2] });
	});

	it('does not stop the file from opening, and is reported when asked for', async () => {
		for (const text of ['{"generator": ', '[1, 2]', '3', 'null', '"text"']) {
			const bytes = model({}, (spec) => (spec.metadata = text));
			const source = bufferSource(bytes);
			const file = await open(bytes);
			await expect(readMetadata(source, file)).rejects.toMatchObject({ code: ErrorCode.BAD_METADATA });
		}
	});

	it('refuses what is not UTF-8, a byte order mark and a block above 1 MiB', () => {
		expect(badMetadata(Uint8Array.from([0x7b, 0xff, 0x7d])).code).toBe(ErrorCode.BAD_METADATA);
		expect(badMetadata(Uint8Array.from([0xef, 0xbb, 0xbf, 0x7b, 0x7d])).code).toBe(ErrorCode.BAD_METADATA);
		const big = new Uint8Array((1 << 20) + 1).fill(0x20);
		big[0] = 0x7b;
		big[big.length - 1] = 0x7d;
		expect(badMetadata(big).message).toContain('1048577');
		const justFits = new Uint8Array(1 << 20).fill(0x20);
		justFits[0] = 0x7b;
		justFits[justFits.length - 1] = 0x7d;
		expect(parse(justFits)).toEqual({});
	});

	it('is the empty object when there is none', () => {
		expect(parse(new Uint8Array(0))).toEqual({});
	});
});
