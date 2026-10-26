<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Digipa -->

# The bm3d file format, version 1

A `.bm3d` file holds a multiresolution 3D model, textured or not, that a client can stream: it reads a
small header and an index first, then only the nodes (and their textures) that the view needs, each with
one HTTP Range request. This document is the one written specification of the format. The C++ reader and
writer (`src/format/`) and the JavaScript reader of the viewer (`web/src/format.js`) are both written from
it, and the golden files in the tests are what keeps them equal.

The format replaces the container of the Nexus project (magic `Nxs `, called format 0 in this
repository), whose multiresolution structure and node compression it keeps. A reader of this format does
not read Nexus files and a Nexus viewer does not read this one: the magic number differs on purpose, so
that neither misreads the other.

| | |
|---|---|
| Extension | `.bm3d` |
| MIME type | `application/vnd.dronedb.bm3d` |
| Magic number | the four bytes `42 4D 33 44` (ASCII `BM3D`) |
| Format version | 1 |

The words MUST, MUST NOT, SHOULD and MAY are used as in RFC 2119.

## 1. Conventions

* Every multi-byte value is **little endian**. There is no padding inside a record other than the fields
  listed: a record is its fields, in the order of its table, back to back.
* Types: `u8`, `u16`, `u32`, `u64` are unsigned integers of 8, 16, 32 and 64 bits; `i16` is a signed 16-bit
  integer (two's complement); `f32` is an IEEE 754 binary32. Every `f32` of the header and of the node
  table (a centre, a radius, an error) is finite: a NaN or an infinity there is an error (section 11, "bad
  sphere" and "bad index").
* **Offsets and lengths are byte counts**, `u64`, counted from the first byte of the file. There is no
  unit of 256 bytes as in the Nexus container.
* **Alignment.** Every section, node payload and texture starts at an offset that is a multiple of 8. The
  bytes between the end of one item and the next 8-byte boundary are zero. The alignment lets a reader map
  the file and hand a payload to a decoder that wants its input aligned (corto wants 4 bytes) without
  copying.
* Item numbers (node, patch, texture) are zero based `u32` indexes into their table.
* A reader MUST check every offset and length (section 11) when it opens the file, before it uses any of
  them, with arithmetic that cannot wrap (64-bit unsigned in C++; in JavaScript the rule of section 10.3).

## 2. File structure

```
offset               what                                      written by a v1 writer at
0                    header                                    0, headerSize bytes (128 in v1)
indexOffset          index: node table, patch table,           indexOffset = headerSize (rounded up to 8)
                     texture table (section 4)
dataStart            node payloads, in node order, each        dataStart = indexOffset + indexLength
                     starting on a multiple of 8               (a multiple of 8)
                     texture payloads, in texture order, each
                     starting on a multiple of 8
metadataOffset       metadata block (UTF-8 JSON)               the end of the last payload, rounded up to 8
metadataOffset +
metadataLength       end of the file
```

The sections do not overlap. A reader finds each of them from the header and does not assume that the
layout above is the only one possible; the rules it MUST check are in section 11. A v1 writer always
produces exactly this layout, with zero bytes in every gap, and no byte after the metadata block.

**The metadata block is last on purpose.** Its length is not known until the build has finished and it
changes when only a key or a version string changes. Placed last, it moves no other section: every offset
in the header and in the index is the same whatever the metadata holds, which is what lets the content
identity (section 9) ignore it, and lets a tool replace the metadata of a finished file by truncating the
file at `metadataOffset`, appending the new block and updating `metadataLength`.

**Reading a file, in three kinds of request** (the file is served with HTTP Range requests; a server that
answers a range request with `200` and the whole body is not usable and the client reports it):

1. `Range: bytes=0-127`: the header. A header longer than 128 bytes is not needed: a v1 reader reads the
   first 128 bytes only. The client also needs the **file size**, for the range checks of section 11, and
   learns it this way:
   * from the `Content-Range` header of this response (`bytes 0-127/739`), when the total after the `/` is a
     number and the header can be read. For a file on another origin the server has to name it in
     `Access-Control-Expose-Headers: Content-Range`, because a browser hides it from the script otherwise;
   * otherwise from the `Content-Length` of a `HEAD` request for the same URL;
   * when neither gives a number, the client reports an error ("file size unknown") and reads nothing more.
     It never reads the file with unchecked ranges.
2. `Range: bytes=indexOffset-(indexOffset+indexLength-1)`: the whole index, in one request.
3. Lazily, for each node the view needs, `bytes=payloadOffset-(payloadOffset+payloadLength-1)` of its record,
   and, when the node has a texture, the same for the texture record. The metadata block is read only on
   request, with the same kind of range. Every one of these ranges has already been checked when the file
   was opened (section 10.1); only the decoding of what they hold is left for later.

## 3. Header

The header of version 1 is 128 bytes (`headerSize = 128`).

| Offset | Size | Type | Field | Meaning |
|---:|---:|---|---|---|
| 0 | 4 | u8[4] | `magic` | `42 4D 33 44` (`BM3D`) |
| 4 | 4 | u32 | `formatVersion` | 1 |
| 8 | 4 | u32 | `headerSize` | bytes in the header, a multiple of 8, at least 128. A reader that finds more than it knows skips the extra bytes (section 8) |
| 12 | 4 | u32 | `flags` | section 3.1 |
| 16 | 16 | 8 x (u8 type, u8 number) | `vertexLayout` | section 3.2 |
| 32 | 16 | 8 x (u8 type, u8 number) | `faceLayout` | section 3.2 |
| 48 | 8 | u64 | `vertexCount` | the sum of `vertexCount` of the real nodes (every level, not the full-resolution count) |
| 56 | 8 | u64 | `faceCount` | the sum of `faceCount` of the real nodes |
| 64 | 8 | u64 | `metadataOffset` | start of the metadata block (a multiple of 8) |
| 72 | 8 | u64 | `metadataLength` | bytes of the metadata block, 0 for none |
| 80 | 8 | u64 | `indexOffset` | start of the index (a multiple of 8, at least `headerSize`) |
| 88 | 8 | u64 | `indexLength` | bytes of the index, exactly as section 4 computes it |
| 96 | 4 | u32 | `nodeCount` | rows of the node table, **the sink included** (so at least 2) |
| 100 | 4 | u32 | `patchCount` | rows of the patch table |
| 104 | 4 | u32 | `textureCount` | rows of the texture table, 0 for a model without textures |
| 108 | 4 | f32 | `sphere.x` | the bounding sphere of the model: centre |
| 112 | 4 | f32 | `sphere.y` | |
| 116 | 4 | f32 | `sphere.z` | |
| 120 | 4 | f32 | `sphere.radius` | radius |
| 124 | 4 | u8[4] | reserved | zero when written; a reader ignores them |

The bounding sphere is the smallest sphere the writer finds that holds the tight spheres (section 4.1) of
the root nodes. It is meant to frame the model; the viewer does not need it to draw.

**Index placement.** One `indexOffset`/`indexLength` pair, and not one offset per table, because the three
tables are always needed together before the first node can be chosen, so they are read with one request;
the position of each table inside the index follows from the counts (section 4) and is not stored twice.

### 3.1 Flags

| Bit | Mask | Name | Meaning |
|---:|---:|---|---|
| 0 | `0x00000001` | `CORTO_COMPRESSED` | the node payloads are corto streams (section 5). Version 1 writers always set it; **a version 1 reader rejects a file without it**: there is no uncompressed node form in this format |
| 1 | `0x00000002` | `TEXTURE_WEBP` | the texture payloads are WebP images (section 6). Set when `textureCount` is greater than 0 and clear when it is 0 |

Every other bit MUST be zero. A reader rejects a file with any other bit set: a bit it does not know could
change what the payloads are, and a change of that kind raises `formatVersion` (section 8), so in a version
1 file no other bit is ever set. Adding a texture codec later means a new bit, a new version, and a viewer
that reads it first.

### 3.2 Vertex and face layout

The layout says what a node holds **after it has been decoded**; the stored form is the corto stream, which
describes itself (section 5). The viewer and any other client need the layout to size its buffers and to
know which attributes to ask the decoder for, before it has fetched anything.

A layout is 8 slots of two bytes: `type` and `number` (how many components). A slot with `type` 0 is empty
and has `number` 0. The attribute types:

| Code | Name | Size |
|---:|---|---:|
| 0 | none | 0 |
| 1 | BYTE (`i8`) | 1 |
| 2 | UNSIGNED_BYTE | 1 |
| 3 | SHORT (`i16`) | 2 |
| 4 | UNSIGNED_SHORT | 2 |
| 5 | INT (`i32`) | 4 |
| 6 | UNSIGNED_INT | 4 |
| 7 | FLOAT (`f32`) | 4 |
| 8 | DOUBLE (`f64`) | 8 |

Version 1 allows exactly these layouts; a reader rejects anything else as an unsupported layout (a layout
with more attributes or other types is a change that raises the version):

| `vertexLayout` slot | Attribute | Version 1 value |
|---:|---|---|
| 0 | position | FLOAT x 3, required |
| 1 | normal | SHORT x 3, or empty |
| 2 | colour | UNSIGNED_BYTE x 4 (red, green, blue, alpha), or empty |
| 3 | texture coordinates | FLOAT x 2, or empty |
| 4 to 7 | | empty |

| `faceLayout` slot | Attribute | Version 1 value |
|---:|---|---|
| 0 | vertex indexes of a triangle | UNSIGNED_SHORT x 3, required |
| 1 to 7 | | empty |

(The Nexus container wrote the colour as BYTE; the colours are unsigned and version 1 says so.)

Consistency: the texture coordinates slot is not empty **if and only if** `textureCount` is greater than 0.

What the decoder gives for one node, with `nv = vertexCount` and `nf = faceCount` of its record:

| Array | Type | Length | Present when |
|---|---|---|---|
| position | f32 | 3 x nv | always |
| uv | f32 | 2 x nv | the texture coordinates slot is not empty |
| normal | i16 | 3 x nv | the normal slot is not empty |
| color | u8 | 4 x nv | the colour slot is not empty |
| index | u16 | 3 x nf | always |

The arrays are separate; no combined buffer layout is part of the format. A normal is a signed normalised
vector: the value divided by 32767. A colour is non-premultiplied RGBA. Texture coordinates are in
[0, 1] with the origin at the lower left: `v = 0` is the **bottom** row of the texture image, so a client
that uploads the decoded image top row first flips it (WebGL: `UNPACK_FLIP_Y_WEBGL`). The decoded size of
a node is `nv x (12 + 8 uv + 6 normal + 4 colour) + nf x 6` bytes, each term only when present.

## 4. Index

The index is three tables one after the other, in this order, and its length follows from the counts of the
header:

```
nodeTable     at indexOffset                          nodeCount    x 56 bytes
patchTable    at indexOffset + 56 x nodeCount          patchCount   x 12 bytes
textureTable  at indexOffset + textureTableStart       textureCount x 16 bytes

textureTableStart = (56 x nodeCount + 12 x patchCount) rounded up to a multiple of 8
indexLength       = textureTableStart + 16 x textureCount
```

`56 x nodeCount` is a multiple of 8, so the node table starts and the patch table starts on 8-byte
boundaries; the zero padding between the patch table and the texture table (0 or 4 bytes) is part of the
index, and with `textureCount = 0` the index ends with it. A reader checks that `indexLength` is exactly
this value.

### 4.1 Node record, 56 bytes

| Offset | Size | Type | Field | Meaning |
|---:|---:|---|---|---|
| 0 | 8 | u64 | `payloadOffset` | start of the corto stream of the node; a multiple of 8 |
| 8 | 8 | u64 | `payloadLength` | exact bytes of the stream (the padding after it is not counted) |
| 16 | 4 | f32 | `error` | the geometric error of the node, in the length units of the model: how far the node's surface can be from the surface of full resolution. A client draws a node's children instead of the node when `error` divided by the length of a pixel at the node's distance is larger than the error it accepts. A root has `error` equal to its `tightRadius`. The error of a node is normally larger than that of each child, but a reader MUST NOT rely on it |
| 20 | 2 | u16 | `vertexCount` | vertices of the node |
| 22 | 2 | u16 | `faceCount` | triangles of the node |
| 24 | 8 | i16[4] | `cone` | normal cone (below) |
| 32 | 4 | f32 | `sphere.x` | **saturated sphere**: centre (also the centre of the tight sphere) |
| 36 | 4 | f32 | `sphere.y` | |
| 40 | 4 | f32 | `sphere.z` | |
| 44 | 4 | f32 | `sphere.radius` | radius of a sphere that holds the node's own geometry and the saturated spheres of all its children. If the saturated sphere is out of the view, no descendant is in it |
| 48 | 4 | f32 | `tightRadius` | radius, around the same centre, of the sphere that holds the node's own geometry only; used for culling the node and for the distance in the error |
| 52 | 4 | u32 | `firstPatch` | index of the first patch of the node (section 4.2) |

`vertexCount` and `faceCount` are 16-bit because the triangles of a node index their vertices with 16
bits. They are the counts of the **encoded** node, the `nvert` and `nface` that its corto stream decodes to
(section 5), which can be lower than the counts the builder gave the encoder. A real node has at least one
triangle (`faceCount` of 1 or more), so at least 3 vertices, and at least one patch. There are no nodes of
points in this format: a corto stream with no triangle decodes as a point cloud, which bm3d does not
build.

`cone` is the normal cone of the node's triangles, kept for back-face culling. The builder computes a
scaled axis `s`: its direction is the mean of the unit normals of the node's triangles (triangles too thin
to have a normal are left out), and its length is `1 / sin(a)`, with `a = pi x (k + 1) / 50` and `k` the
smallest integer for which at least 95 % of those normals have `floor(49 x angle / pi)` no larger than `k`
(`angle` is the angle between the normal and the mean, in radians). `s` is the
zero vector when no triangle has a normal, when the mean of the normals is zero, or when `a` is at least
`pi/2 - 0.1` (the cone is too wide to cull anything). The four values written are:

* when the length `|s|` is above 0.001: `cone[i] = trunc(32766 x c)` for `i` = 0, 1, 2, where `c` is
  component `i` of `s / |s|` clamped to [-1, 1], and `cone[3] = trunc(3276 x min(|s|, 10))`;
* otherwise all four are 0.

The viewer of version 1 does not use the cone; a reader may ignore it.

**The sink.** The last row of the node table (index `nodeCount - 1`) is not a node. It ends the table, so
that the patches of the node before it end where the sink's `firstPatch` begins, and the leaves of the
hierarchy link to it (section 4.2). All its fields are 0 except `firstPatch`, which is `patchCount`: its
first 52 bytes are zero bytes (so a float field is +0, not -0), and a reader checks that they are
(section 11). A reader MUST NOT read a payload, a sphere or an error from the
sink.

### 4.2 Patch record, 12 bytes

The hierarchy is a directed acyclic graph, not a tree: a node is refined by other nodes (its *children*),
each one replacing a part of its triangles, and a child can be the child of several nodes. The patches of
node `i` are the rows `firstPatch[i]` up to, but not including, `firstPatch[i + 1]` of the patch table; that
is why the sink exists.

| Offset | Size | Type | Field | Meaning |
|---:|---:|---|---|---|
| 0 | 4 | u32 | `node` | the child: the node that replaces the triangles of this patch. The sink (`nodeCount - 1`) for the leaves of the hierarchy |
| 4 | 4 | u32 | `triangleOffset` | the **end** (exclusive) of the patch in the triangle list of the node; it begins where the previous patch of the node ends, the first patch at triangle 0. The unit is triangles. A patch MAY be empty (its end equals the previous one) |
| 8 | 4 | u32 | `texture` | index into the texture table; `0xFFFFFFFF` for no texture |

A client draws a node by drawing the triangle ranges of its patches whose child is **not** among the nodes
it is drawing; a range whose child is drawn is left out, because the child draws that part in finer detail.

Rules (a writer guarantees them; a reader checks them as section 11 lists):

* The patches of a real node are at least one, `firstPatch[0]` is 0, the values of `firstPatch` do not
  decrease along the table and the sink's is `patchCount`.
* `triangleOffset` does not decrease along the patches of a node, and the last one equals the node's
  `faceCount`. Equal values (empty patches) are allowed: the encoder drops degenerate triangles
  (section 12), and a patch whose triangles were all degenerate is left empty.
* A node is a **leaf** when its patches name the sink. Either all the patches of a node name the sink or
  none of them does.
* The nodes are stored **parents before children**: a child that is not the sink has an index larger than the
  node's.
* All the patches of a node have the same `texture`. When the texture coordinates are present it is a valid
  texture index, otherwise it is `0xFFFFFFFF`. A client may take the texture of a node from its first patch.
  Several nodes MAY share a texture, so a reader does not assume that a texture belongs to one node.

**Roots.** The roots are the nodes a traversal starts from; they are derived, not stored. `rootCount` is
computed as:

```
rootCount = nodeCount - 1
for j = 0 while j < rootCount:
    for each patch of node j:
        if patch.node < rootCount: rootCount = patch.node
    j = j + 1
```

The roots are the nodes `0 .. rootCount - 1`. A model built by bm3d has one root, node 0.

### 4.3 Texture record, 16 bytes

| Offset | Size | Type | Field | Meaning |
|---:|---:|---|---|---|
| 0 | 8 | u64 | `offset` | start of the image; a multiple of 8 |
| 8 | 8 | u64 | `length` | exact bytes of the image (the padding after it is not counted); not 0 |

There is no sentinel row in the texture table (the Nexus container had one, and derived the length of a
texture from the next offset). The projection matrix that the Nexus texture record carried is not stored:
no writer set it and no reader used it.

## 5. Node payload: the corto stream

`CORTO_COMPRESSED` is always set, so the bytes `payloadOffset .. payloadOffset + payloadLength - 1` of a
real node are one **corto** stream, as written by the corto encoder. The stream begins with the four bytes
`00 63 7A 78` (the number `0x787A6300`, little endian) and the stream version, `u32` 1. The codec is
corto, <https://github.com/cnr-isti-vclab/corto>, at commit `d240e69306a39417c098475d7002eb16b7264248` with
the patches of `third_party/corto-patches/` of this repository (they fix undefined behaviour and a leak and do
not change the stream). A decoder built from that commit, or from any later corto that reads version 1
streams, decodes the payload.

The stream carries the attributes the layout names, the triangles, and **one group per patch** of the node:
the group ends of the stream are equal to the `triangleOffset` values of the node's patches, in order
(section 12 says why the writer takes both from the encoder). The viewer
draws from the patch table of the index and does not use the groups. The corto decoder requires its input
buffer to be aligned on 4 bytes: a payload offset that is a multiple of 8 gives that when the file is
mapped, and a client that reads the payload into its own buffer allocates it aligned.

The decoder is asked for the types of the table in section 3.2:

| Call of the corto decoder | Output |
|---|---|
| `setPositions(float *)` | position, f32 x 3 |
| `setUvs(float *)` | uv, f32 x 2 |
| `setNormals(int16_t *)` | normal, i16 x 3 (not the float form) |
| `setColors(uchar *, 4)` | color, u8 x 4 (RGBA, not RGB) |
| `setIndex(uint16_t *)` | index, u16 x 3 (not the 32-bit form) |

Only the calls for the attributes the layout names are made. The decoded `nvert` and `nface` (read from the
stream before the arrays are decoded) equal the node's `vertexCount` and `faceCount`; a reader checks it
before it decodes into buffers sized from the record, and reports a corrupt node when they differ. The corto
encoder quantises the positions, normals, colours and texture coordinates; the decoded values are the
dequantised ones, and the quantisation steps are chosen by the writer (they are recorded, for information,
in the `buildOptions` of the metadata) and are not needed to decode. The order of the vertices and the
order of the triangles inside a patch are the encoder's; only the patch ranges are meaningful.

## 6. Texture payload

A texture is one **WebP** image (`TEXTURE_WEBP`), exactly `length` bytes at `offset`: a RIFF container that
starts with `52 49 46 46` and holds `WEBP`. It is decoded by any WebP decoder to 8-bit RGBA, in the sRGB
colour space; its width and height are those of the image and are not stored in the index. The decoded
image is addressed as section 3.2 says (`v = 0` is the bottom row). The writer of bm3d makes every side at
most 1024 pixels; the format does not limit it. When a reader reads a texture it checks, before it decodes,
that `length` is at least 12, that bytes 0 to 3 are `RIFF` and that bytes 8 to 11 are `WEBP`; otherwise, or
when the WebP decoder refuses the image, it reports a corrupt texture (section 11).

## 7. Metadata block

The block is `metadataLength` bytes at `metadataOffset`: **one JSON object** as RFC 8259 defines it,
encoded in UTF-8, without a byte order mark and without a trailing zero byte. A `metadataLength` of 0 means
there is no block, and a reader treats it as the empty object. A writer refuses to write a block longer than
1 MiB (1 048 576 bytes), with a message that gives the length, and does not write the file; a reader
accepts a block of up to 1 MiB and MAY refuse a larger one.

Reserved keys, written by the builder (all three are always written by a version 1 writer, and a reader
does not require them):

| Key | Type | Meaning |
|---|---|---|
| `generator` | string | the program and its version, `"bm3d 1.0.0"` |
| `buildOptions` | object | the settings of the build, for information; a reader does not interpret them. The writer of version 1 records `threads`, `nodeFaces`, `topNodeFaces`, `textureQuality`, `maxNodeTextureSide`, `scaling`, `adaptive`, `skipLevels`, `errorQ`, `normalBits`, `lumaBits`, `chromaBits`, `alphaBits` and `texStep` (numbers) |
| `source` | object | `name` (string, the file name of the input without its directory), `vertices` and `faces` (integers, the counts of the input mesh as it was read, triangles after triangulation), `bounds` (`{"min": [x, y, z], "max": [x, y, z]}`, the bounding box of the input in its own units) |

**Extra keys.** The `--metadata <file>` option of the command line (and of the library, where it has one) supplies a JSON
object whose members are copied as members of the top-level object, after the reserved ones, in their
original order and with their values unchanged. They cannot replace a reserved key: the writer **refuses the
build** with a message that names the key, before it builds anything, when the supplied object has a
top-level key that is `generator`, `buildOptions` or `source`, or one that starts with `bm3d` (reserved for
later versions), or when it names the same top-level key twice (the readers of JSON disagree on which of
two equal keys wins). Keys nested inside a supplied value are not examined. This is how a caller adds
information such as a coordinate reference system or an origin without a new format version.

**Readers ignore every key they do not know** and every key they know but cannot use. The block is
parsed only when the metadata is asked for: a block that is not valid JSON, or not an object, is reported
then, as an error of the metadata, and the file remains usable for everything else.

The metadata block is **not** part of the content identity (section 9): the generator version, the build
options, the input name and the extra keys can change and the content hash, and so the output, stays the
same.

## 8. Version rule

`formatVersion` is one integer, 1 for this document.

* A reader accepts every version it implements and **rejects a newer one with a clear error** that says
  the file is version N and the reader reads up to M. It does not try to read it, and it does not retry.
  Version 0 does not exist and is rejected, as is the Nexus magic number (a reader may say that the file
  is a Nexus file).
* **An additive change does not change the version**, because an older reader still reads the file: more
  bytes at the end of the header (the reader reads the fields it knows and uses `headerSize` to know where
  the header ends, and checks that no section starts inside it), a new key in the metadata block. A new
  header field is never a new meaning of a field of this document.
* **A change that an older reader cannot read raises the version**: a new flag bit, a new texture codec, a
  new attribute or type in a layout, a different record size, a different meaning of a field, a different
  alignment rule. The Hub then ships a viewer that reads the new version before DroneDB takes a bm3d that
  writes it.

## 9. Content identity

Two files are the **same output** when their **content hashes**, defined below, are equal. The content
hash covers every byte of the file except the metadata block, and it reads the `metadataLength` field of the
header as zero: that field lies outside the block but depends on it, so two files that differ only in their
metadata are the same output although their bytes 72 to 79 differ. The determinism tests, the golden tests
and the checks that a change of the code leaves the output alone all compare content hashes.

The C++ reader MUST expose the content hash of the file it opened. A JavaScript reader MAY expose it when
it holds the whole file; a reader that fetches ranges lazily does not download the payloads only to hash
them, and need not offer it.

The metadata block sits last and every other offset is independent of its content, so only two things in
the file depend on the metadata: the block itself and the `metadataLength` field of the header (bytes 72 to
79). The hash is therefore defined as the SHA-256 of the concatenation of four parts, in this order:

1. the bytes `0 .. 71` of the file;
2. eight zero bytes, standing for `metadataLength` (bytes 72 to 79);
3. the bytes `80 .. metadataOffset - 1`;
4. the bytes from `metadataOffset + metadataLength` to the end of the file (none in a file written by a
   version 1 writer; they are included so that nothing outside the block is left unprotected).

(Parts 1 to 3 are the file from byte 0 up to the metadata block, with the length field read as zero; part 4
is what follows the block.) `metadataOffset` itself, at bytes 64 to 71, **is** hashed: it is the end of the
payloads rounded up to 8, so it does not depend on the metadata. Header fields not known to a version 1
reader, and every padding byte, are hashed as they are. The result is written as 64 lower-case hexadecimal
digits.

Consequences: the metadata can be replaced (a different generator version, build options, input name, extra
keys, a longer or shorter block) without changing the hash; any change in the header, the index, a payload or
a padding byte changes it. The reference computation, for a file in memory `f`:

```python
import hashlib, struct

def content_hash(f: bytes) -> str:
    metadata_offset, metadata_length = struct.unpack_from("<QQ", f, 64)
    h = hashlib.sha256()
    h.update(f[:72])
    h.update(bytes(8))
    h.update(f[80:metadata_offset])
    h.update(f[metadata_offset + metadata_length:])
    return h.hexdigest()
```

A reader validates `metadataOffset` and `metadataLength` (section 11) before it hashes.

## 10. Readers

### 10.1 Order of operations

Opening a file runs **every** check of the header and of the index, the ranges and the overlaps included,
in the order of the table of section 11, and stops at the first failure; a file that has been opened has
passed all of them. Only two things are left until they are needed: the corto decoding of a node (and the
check of its decoded counts) and the decoding of a texture (and the check of its WebP signature).

1. Learn the file size (section 2) and read the first 128 bytes. Check the magic, the version,
   `headerSize`, the flags and the layouts, then the counts and the ranges of the index and of the
   metadata block.
2. Read the index and check the tables, the ranges of every payload and texture and their overlaps.
3. Read payloads and textures when they are needed, each from its own range, and decode them. Ranges of
   neighbouring nodes or textures are contiguous in a v1 file, so a client MAY merge requests, but nothing
   requires it.

A file served by HTTP SHOULD carry `Accept-Ranges: bytes`, a `Content-Length` and an `ETag` or
`Last-Modified`. A client that caches the data of a file keys the cache on the URL, the `formatVersion` and the
`ETag`.

### 10.2 What a reader exposes

The header fields, the node, patch and texture tables, the derived `rootCount`, the metadata (parsed on
request) and, as section 9 says, the content hash. Which child a patch names, whether a node is a leaf (its
patches name the sink), the list of triangle ranges of a node and which texture it uses all follow from the
tables.

### 10.3 JavaScript

A JavaScript number is exact up to `Number.MAX_SAFE_INTEGER` (2^53 - 1). A reader reads a `u64` as its two
`u32` halves, low then high, and **rejects the value with an error when the high half is larger than
`0x1FFFFF`** (the value would not be a safe integer), instead of rounding it; it never turns it into a
`BigInt` silently. After the conversion it adds and compares offsets as numbers and checks that each sum is
no larger than `Number.MAX_SAFE_INTEGER` and no larger than the file size, so a sum can neither lose
precision nor wrap. The `f32` fields are read with `DataView.getFloat32(offset, true)`, the others with
`getUint16`, `getUint32`, `getInt16` and the little endian flag `true`. The buffer that the
request for the index returns begins with the node table, so the patch table is at byte `56 x nodeCount`
of it and the texture table at `textureTableStart`; both are multiples of 8, so typed arrays
(`Uint32Array` for the patches) may be created over the buffer without a copy.

## 11. Errors a reader reports

A reader checks, when it opens the file, in this order, and stops at the first failure. Each failure is an
error with a message that says what is wrong and the value found; none is repaired or retried.

| Error | When |
|---|---|
| file size unknown (HTTP) | neither the `Content-Range` total nor the `Content-Length` of a `HEAD` request gives the size of the file (section 2) |
| truncated header | fewer than 128 bytes could be read |
| bad magic | `magic` is not `BM3D` |
| unsupported version | `formatVersion` is 0 or larger than the highest the reader implements (section 8) |
| bad header size | `headerSize` is below 128 or not a multiple of 8, or larger than the file |
| compression flag missing | `CORTO_COMPRESSED` is not set (section 3.1) |
| unknown flags | a bit other than the two of section 3.1 is set; or `TEXTURE_WEBP` does not match `textureCount > 0` |
| unsupported layout | `vertexLayout` or `faceLayout` is not one of section 3.2, or the texture coordinates slot disagrees with `textureCount` |
| bad counts | `nodeCount` below 2 |
| bad sphere | a coordinate of the centre of the header's `sphere`, or its `radius`, is not finite (a NaN or an infinity) |
| out of range | any of the following does not hold, with all sums and comparisons exact (no wrap): `indexOffset` and `indexLength` as section 4 says (offset a multiple of 8 and at least `headerSize`, length equal to the formula); the index inside the file; `metadataOffset` a multiple of 8, at least `indexOffset + indexLength`, and `metadataOffset + metadataLength` at most the file size (this is also the **truncated file** error when the file is shorter than the header says); every real node's `payloadOffset` a multiple of 8 and `payloadLength` above 0, and every texture's `offset` a multiple of 8 and `length` above 0, the range inside `[indexOffset + indexLength, metadataOffset)` |
| overlapping sections | two of these ranges overlap: the header `[0, headerSize)`, the index, the payload of each real node, each texture, the metadata block (a range of length 0 overlaps nothing) |
| bad index | one of the rules of the index below is broken |
| value too large (JavaScript) | a `u64` above `Number.MAX_SAFE_INTEGER` (section 10.3) |

**The rules of the index**, for "bad index". They are checked **rule by rule, in this order**: each rule is
checked over the whole table (every real node, or every patch, from the first to the last) before the next
rule is checked, and the error names the first node or patch, in the order of the table, that breaks the
first rule that is broken. So every reader reports the same rule and the same node or patch for the same
file.

1. `firstPatch` of node 0 is not 0.
2. `firstPatch` decreases from a node to the next one (the sink included).
3. `firstPatch` of the sink is not `patchCount`.
4. A byte of the first 52 of the sink (every field but `firstPatch`) is not zero.
5. A real node has no patch (its `firstPatch` equals the next one's).
6. A real node has `faceCount` 0 or `vertexCount` below 3.
7. A real node has an `error`, a coordinate of its `sphere` centre, a `sphere.radius` or a `tightRadius`
   that is not finite (a NaN or an infinity).
8. A patch names a node at or beyond `nodeCount`.
9. A patch names a real node that is not after the node the patch belongs to.
10. A real node has some patches that name the sink and some that do not.
11. A `triangleOffset` is below the one of the patch before it in the same node.
12. The `triangleOffset` of the last patch of a real node is not the node's `faceCount`.
13. A patch has a `texture` that is neither `0xFFFFFFFF` nor below `textureCount`.
14. The patches of a real node name different textures.
15. The layout has texture coordinates and a patch has no texture (`0xFFFFFFFF`), or the layout has none
    and a patch has one.

The `u64` fields are converted when they are read, so in JavaScript the last error can stop any of the
steps that read one. A reader reports the node or the texture by its index. A file whose size is larger
than `metadataOffset + metadataLength` is not an error (the bytes after the block are hashed, section 9).

Three errors are reported later, when what they concern is used, and never stop the file from opening:

| Error | When |
|---|---|
| corrupt node | when the node is decoded: the corto decoder refuses the payload, or the `nvert` and `nface` of the stream differ from the node's `vertexCount` and `faceCount` (section 5) |
| corrupt texture | when the texture is read: `length` is below 12, bytes 0 to 3 are not `RIFF`, bytes 8 to 11 are not `WEBP`, or the WebP decoder refuses the image (section 6) |
| bad metadata | when the metadata is asked for: the block is not valid UTF-8 JSON, is not an object, or is larger than the reader accepts (section 7) |

## 12. Writers

A version 1 writer produces the layout of section 2 and satisfies every rule above. In particular:

* it writes the header with `formatVersion = 1`, `headerSize = 128`, `flags` with `CORTO_COMPRESSED` and,
  if there are textures, `TEXTURE_WEBP`, the layouts of section 3.2 and zero in every reserved byte;
* it writes the index at `indexOffset = 128`, then the payloads of the real nodes in node order starting at
  `dataStart` (each starting on a multiple of 8, the gap filled with zero bytes), then the textures in
  texture order in the same way, then the metadata at the next multiple of 8, and no byte after it;
* it writes the sink with all fields 0 except `firstPatch = patchCount`;
* **it takes the counts of a node from the encoder, after encoding.** The corto encoder changes them while
  it encodes: it drops the degenerate triangles (two equal vertex indexes) and moves the end of every group
  down to match, and it drops the vertices that no remaining triangle uses. So the node's `vertexCount`,
  `faceCount` and the `triangleOffset` of each of its patches are written from the encoder once
  `encode()` has returned (its `nvert`, its `nface` and its index group ends, in patch order), never from
  the counts the builder gave it; the header's `vertexCount` and `faceCount` are the sums of these. The
  decoded counts of every node then equal its record. The golden tests include a model with a degenerate
  triangle, whose written counts are lower than those of its input, to keep this so;
* when the encoder leaves a node with no triangle, the writer reports an error and does not write the file
  (there are no nodes of points, section 4.1);
* it writes the `cone` exactly as section 4.1 defines it, with all four values 0 when `|s|` is not above
  0.001;
* the exact `payloadLength` and texture `length` are the bytes of the stream and of the image, not the
  padded sizes;
* it writes the metadata of section 7, refuses reserved keys in the extra metadata, and refuses a block
  longer than 1 MiB;
* the file appears only when it is complete (it is written under another name and renamed).

## 13. A worked example

A model of one square in the plane z = 0 (`quad.obj`: 4 vertices, 2 triangles, with normals, no colours,
no textures), built into one real node and the sink. The corto stream of the node is imagined to be 120
bytes long. The numbers of the layout follow from the rules:

| Quantity | Value |
|---|---|
| `nodeCount`, `patchCount`, `textureCount` | 2, 1, 0 |
| node table | 112 bytes at 128 |
| patch table | 12 bytes at 240 |
| `textureTableStart` | 124 rounded up to 8 = 128 |
| index | `indexOffset` 128, `indexLength` 128 + 0 x 16 = 128, ends at 256 |
| node 0 payload | at 256, 120 bytes, ends at 376 |
| `metadataOffset` | 376; the metadata is 363 bytes, so the file is 739 bytes |

The header (128 bytes, in hexadecimal; the offsets are those of the table of section 3):

```
0000  42 4d 33 44 01 00 00 00 80 00 00 00 01 00 00 00   magic, formatVersion 1, headerSize 128, flags 1
0010  07 03 03 03 00 00 00 00 00 00 00 00 00 00 00 00   vertexLayout: FLOAT x3, SHORT x3, then empty
0020  04 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00   faceLayout: UNSIGNED_SHORT x3, then empty
0030  04 00 00 00 00 00 00 00 02 00 00 00 00 00 00 00   vertexCount 4, faceCount 2
0040  78 01 00 00 00 00 00 00 6b 01 00 00 00 00 00 00   metadataOffset 376, metadataLength 363
0050  80 00 00 00 00 00 00 00 80 00 00 00 00 00 00 00   indexOffset 128, indexLength 128
0060  02 00 00 00 01 00 00 00 00 00 00 00 00 00 00 3f   nodeCount 2, patchCount 1, textureCount 0, sphere.x 0.5
0070  00 00 00 3f 00 00 00 00 f3 04 35 3f 00 00 00 00   sphere.y 0.5, sphere.z 0, radius 0.70710677, reserved
```

Node 0 (a leaf: its single patch names the sink), the first record of the index at 128, 56 bytes. Neither
triangle is degenerate, so the encoder keeps all 4 vertices and 2 triangles and the record holds the
counts of the input. Both triangles face +z, so the mean normal is (0, 0, 1), `a` is `pi/50`, `|s|` is
`1 / sin(pi/50)`, about 15.9, and `cone[3]` is `trunc(3276 x 10)` = 32760:

```
0000  00 01 00 00 00 00 00 00 78 00 00 00 00 00 00 00   payloadOffset 256, payloadLength 120
0010  f3 04 35 3f 04 00 02 00 00 00 00 00 fe 7f f8 7f   error 0.70710677 (a root: its tightRadius), vertexCount 4, faceCount 2, cone 0 0 32766 32760
0020  00 00 00 3f 00 00 00 3f 00 00 00 00 f3 04 35 3f   sphere centre 0.5 0.5 0, radius 0.70710677
0030  f3 04 35 3f 00 00 00 00                           tightRadius 0.70710677, firstPatch 0
```

The sink, the second record, at 184: 52 zero bytes, then `01 00 00 00` (`firstPatch` = `patchCount` = 1).
The patch, at 240: `01 00 00 00 | 02 00 00 00 | ff ff ff ff`, that is child 1 (the sink), the triangles
up to 2 (equal to the node's `faceCount`), and no texture. The 4 zero bytes at 252 pad the patch table up to
the start of the texture table (`textureTableStart` = 128, at 256), which is empty because `textureCount`
is 0; the index ends at 256, where the payload begins.

The metadata, 363 bytes:

```json
{"generator":"bm3d 1.0.0","buildOptions":{"threads":1,"nodeFaces":16384,"topNodeFaces":4096,"textureQuality":90,"maxNodeTextureSide":1024,"scaling":0.5,"adaptive":0.333,"skipLevels":-1,"errorQ":0.1,"normalBits":10,"lumaBits":6,"chromaBits":6,"alphaBits":5,"texStep":0.25},"source":{"name":"quad.obj","vertices":4,"faces":2,"bounds":{"min":[0,0,0],"max":[1,1,0]}}}
```

If the metadata is replaced (say by one with an extra key `"crs":"EPSG:32633"`), only `metadataLength`
(bytes 72 to 79) and the block change; the content hash, which reads those eight bytes as zero and does not
read the block, stays the same.

## 14. Size against the Nexus container

The Nexus container rounded every node payload and every texture up to a multiple of 256 bytes, and
version 1 rounds to 8. A payload or a texture of a random size wastes on average about 128 bytes in the
first case and about 4 in the second, so the saving from alignment is about **128 bytes for each node
and for each texture** (about 124 bytes after the 4 that remain). The index changes size too: a node
record is 56 bytes instead of 44 (12 more for each node), a texture record is 16 instead of 68 (52 fewer
for each texture, and no sentinel row), the header is 128 bytes instead of 88, and the metadata block adds
its length (a few hundred bytes). For a textured model, with one texture for each node, the total change
is therefore about

```
saving  ~=  nodes x (124 + 124 + 52 - 12)  -  (128 - 88)  +  68  -  metadataLength
        =   nodes x 288  +  28  -  metadataLength            (bytes, textured, one texture per node)
```

(the 68 is the sentinel row of the Nexus texture table), and without textures about
`nodes x 112 - 40 - metadataLength`. Measured with the same builder writing both containers:

| Model | Nexus container | version 1 | Change |
|---|---:|---:|---:|
| the benchmark model (a textured photogrammetry mesh, `TexturedModel`) | 358 059 008 | 356 620 936 | -1 438 072 B (-0.40 %) |
| a large textured model (`OfficePlan`) | 761 587 200 | 754 435 514 | -7 151 686 B (-0.94 %) |

The version 1 files are those of a builder whose order of equal keys (in its sorts, its
simplification and its texture packing) is no longer the one the standard library gave; that moves
a few hundred bytes of the compressed payloads and textures (208 bytes on the benchmark model), next
to the 1.4 MB and 7.2 MB the container saves.

## 15. Summary of the fixed sizes

| Item | Bytes |
|---|---:|
| header | 128 |
| node record | 56 |
| patch record | 12 |
| texture record | 16 |
| attribute slot | 2 |
