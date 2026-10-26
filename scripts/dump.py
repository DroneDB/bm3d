#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read a .bm3d file (docs/FORMAT.md, version 1), and compare one with the format 0 file of the
same build.

  dump.py FILE [--nodes] [--limit N] [--metadata] [--hash]
      print what is in the file. Opening it runs every check of the format (section 11), in the
      order of the specification, so a file that this prints is a file a reader opens; the
      geometry stays compressed and is not decoded, but the header of each corto stream is read
      and must agree with the index (section 5: the counts, and the ends of the groups, which are
      the ends of the patches).
  dump.py compare OLD.nxz NEW.bm3d
      node by node, whether the two files are the same model in two containers: the node table
      (errors, spheres, patches, cones), the bytes of every corto stream and of every texture.
      Exit status 0 when only the container differs, 1 when something else does, 2 for a file
      that cannot be read.

This is a second reader, written from the specification with no code of src/format/, so that the
golden files are read by an implementation of their own (the viewer's reader in web/src/format.js
is the third). It needs numpy.

In the comparison, a node count that differs is explained when the corto stream of the new file is
byte for byte the one of the old, and the new file's count is the one the stream declares: the
Nexus container wrote the counts the builder gave the encoder, the new one writes what the
encoder left (it drops degenerate triangles and the vertices only they use; section 12).
"""
import argparse
import hashlib
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from modelfile import ModelFile, ModelFileError, NO_TEXTURE  # noqa: E402

MAGIC = b"BM3D"
FORMAT_VERSION = 1
HEADER_SIZE = 128
ALIGNMENT = 8
FLAG_CORTO = 0x1
FLAG_WEBP = 0x2
MAX_METADATA = 1 << 20
SINK_FIELDS = ("payload_offset", "payload_length", "error", "nvert", "nface", "radius", "tight_radius")

NODE_DTYPE = np.dtype([
    ("payload_offset", "<u8"), ("payload_length", "<u8"), ("error", "<f4"),
    ("nvert", "<u2"), ("nface", "<u2"), ("cone", "<i2", (4,)), ("center", "<f4", (3,)),
    ("radius", "<f4"), ("tight_radius", "<f4"), ("first_patch", "<u4"),
])
PATCH_DTYPE = np.dtype([("node", "<u4"), ("triangle_offset", "<u4"), ("texture", "<u4")])
TEXTURE_DTYPE = np.dtype([("offset", "<u8"), ("length", "<u8")])
assert NODE_DTYPE.itemsize == 56 and PATCH_DTYPE.itemsize == 12 and TEXTURE_DTYPE.itemsize == 16

# header: magic, version, headerSize, flags, vertexLayout, faceLayout, vertexCount, faceCount,
# metadataOffset, metadataLength, indexOffset, indexLength, nodeCount, patchCount, textureCount,
# sphere (x, y, z, radius), reserved
HEADER_STRUCT = struct.Struct("<4sIII16s16sQQQQQQIII4f4s")
assert HEADER_STRUCT.size == HEADER_SIZE

ATTRIBUTE_NAMES = ("none", "byte", "ubyte", "short", "ushort", "int", "uint", "float", "double")


class Bm3dError(ModelFileError):
    """A file that is not a .bm3d file of a version this reads; `kind` is the name of the error in
    section 11 of the specification."""

    def __init__(self, kind, message):
        super().__init__("%s: %s" % (kind, message))
        self.kind = kind


def _slots(raw):
    return [(raw[i], raw[i + 1]) for i in range(0, 16, 2)]


def _align(value):
    return (value + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT


def content_hash(data):
    """The content hash of section 9, of the bytes of a whole file."""
    metadata_offset, metadata_length = struct.unpack_from("<QQ", data, 64)
    h = hashlib.sha256()
    h.update(data[:72])
    h.update(bytes(8))
    h.update(data[80:metadata_offset])
    h.update(data[metadata_offset + metadata_length:])
    return h.hexdigest()


def corto_header(stream):
    """The part of a corto stream (section 5) the index must agree with: the attributes it holds,
    the counts of vertices and triangles, and the end of each group."""
    pos = 0

    def take(size):
        nonlocal pos
        if pos + size > len(stream):
            raise ValueError("the stream ends inside its header")
        chunk = stream[pos:pos + size]
        pos += size
        return chunk

    def string():
        (length,) = struct.unpack("<H", take(2))
        return bytes(take(length)).rstrip(b"\0").decode("utf-8", "replace")

    magic, version = struct.unpack("<II", take(8))
    if magic != 0x787A6300:
        raise ValueError("not a corto stream (magic 0x%08x)" % magic)
    if version != 1:
        raise ValueError("corto stream version %d" % version)
    take(1)  # entropy
    (exif,) = struct.unpack("<I", take(4))
    for _ in range(exif):
        string()
        string()
    (nattr,) = struct.unpack("<i", take(4))
    attributes = []
    for _ in range(nattr):
        attributes.append(string())
        take(4 + 4 + 3)  # codec, q, components, format, strategy
    nvert, nface = struct.unpack("<ii", take(8))
    (ngroups,) = struct.unpack("<I", take(4))
    ends = []
    for _ in range(ngroups):
        (end,) = struct.unpack("<I", take(4))
        ends.append(end)
        (props,) = struct.unpack("<B", take(1))
        for _ in range(props):
            string()
            string()
    return {"attributes": attributes, "nvert": nvert, "nface": nface, "group_ends": ends}


class Bm3dFile(ModelFile):
    """The tables of one .bm3d file, read and checked as section 11 says; payloads, textures and
    the metadata are read from the file on demand."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as handle:
            handle.seek(0, os.SEEK_END)
            self.size = handle.tell()
            handle.seek(0)
            head = handle.read(HEADER_SIZE)
            self._read_header(head)
            index = self._check_header_ranges(handle)
        self._read_index(index)
        self._check_ranges()
        self._check_index()
        self._derive()
        self.roots = self._root_count()

    # --- the header ---------------------------------------------------------------------------
    def _read_header(self, head):
        if len(head) < HEADER_SIZE:
            raise Bm3dError("truncated header", "the file has %d bytes and the header takes %d" % (self.size, HEADER_SIZE))
        (magic, self.version, self.header_size, self.flags, vertex_layout, face_layout, self.nvert, self.nface,
         self.metadata_offset, self.metadata_length, self.index_offset, self.index_length, self.node_count,
         self.patch_count, self.texture_count, x, y, z, radius, self.reserved) = HEADER_STRUCT.unpack(head)
        self.sphere = np.array([x, y, z, radius], dtype=np.float64)
        if magic != MAGIC:
            nexus = magic == bytes([0x20, 0x73, 0x78, 0x4E])
            raise Bm3dError("bad magic", "the file is a Nexus file (format 0), not a .bm3d file" if nexus
                            else "the file begins with %r, not BM3D" % magic)
        if self.version == 0 or self.version > FORMAT_VERSION:
            raise Bm3dError("unsupported version", "the file is version %d and this reader reads up to version %d"
                            % (self.version, FORMAT_VERSION))
        if self.header_size < HEADER_SIZE or self.header_size % ALIGNMENT or self.header_size > self.size:
            raise Bm3dError("bad header size", "the header size is %d (at least 128, a multiple of 8, no more than the "
                            "%d bytes of the file)" % (self.header_size, self.size))
        if not self.flags & FLAG_CORTO:
            raise Bm3dError("compression flag missing", "the nodes are not corto streams (flags 0x%08x)" % self.flags)
        if self.flags & ~(FLAG_CORTO | FLAG_WEBP):
            raise Bm3dError("unknown flags", "the flags 0x%08x set bits that version 1 does not define" % self.flags)
        if bool(self.flags & FLAG_WEBP) != (self.texture_count > 0):
            raise Bm3dError("unknown flags", "the WebP flag and the %d textures disagree" % self.texture_count)
        self.vertex_layout, self.face_layout = _slots(vertex_layout), _slots(face_layout)
        v, f = self.vertex_layout, self.face_layout
        float3, short3, ubyte4, float2 = (7, 3), (3, 3), (2, 4), (7, 2)
        ok = (v[0] == float3 and v[1] in ((0, 0), short3) and v[2] in ((0, 0), ubyte4) and v[3] in ((0, 0), float2)
              and all(slot == (0, 0) for slot in v[4:]) and f[0] == (4, 3) and all(slot == (0, 0) for slot in f[1:]))
        if not ok:
            raise Bm3dError("unsupported layout", "the vertex or face layout is not one of version 1")
        self.has_normals, self.has_colors, self.has_uvs = v[1] != (0, 0), v[2] != (0, 0), v[3] != (0, 0)
        if self.has_uvs != (self.texture_count > 0):
            raise Bm3dError("unsupported layout", "the texture coordinates and the %d textures disagree" % self.texture_count)
        if self.node_count < 2:
            raise Bm3dError("bad counts", "the file has %d nodes and the sink is one of them" % self.node_count)

    def _index_layout(self):
        patch_table = NODE_DTYPE.itemsize * self.node_count
        texture_table = _align(patch_table + PATCH_DTYPE.itemsize * self.patch_count)
        return patch_table, texture_table, texture_table + TEXTURE_DTYPE.itemsize * self.texture_count

    def _check_header_ranges(self, handle):
        _, _, length = self._index_layout()
        if self.index_offset % ALIGNMENT or self.index_offset < self.header_size:
            raise Bm3dError("out of range", "the index starts at %d" % self.index_offset)
        if self.index_length != length:
            raise Bm3dError("out of range", "the index is %d bytes and the counts need %d" % (self.index_length, length))
        if self.index_offset + self.index_length > self.size:
            raise Bm3dError("out of range", "the index is not inside the file of %d bytes" % self.size)
        data_start = self.index_offset + self.index_length
        if self.metadata_offset % ALIGNMENT or self.metadata_offset < data_start:
            raise Bm3dError("out of range", "the metadata starts at %d" % self.metadata_offset)
        if self.metadata_offset + self.metadata_length > self.size:
            raise Bm3dError("out of range", "the metadata is not inside the file of %d bytes (the file is truncated)" % self.size)
        handle.seek(self.index_offset)
        return handle.read(self.index_length)

    def _read_index(self, index):
        patch_table, texture_table, _ = self._index_layout()
        self.nodes = np.frombuffer(index, dtype=NODE_DTYPE, count=self.node_count)
        self.patches = np.frombuffer(index, dtype=PATCH_DTYPE, count=self.patch_count, offset=patch_table)
        self.textures = np.frombuffer(index, dtype=TEXTURE_DTYPE, count=self.texture_count, offset=texture_table)

    def _check_ranges(self):
        data_start = self.index_offset + self.index_length
        real = self.node_count - 1
        for what, offsets, lengths in (("the payload of node", self.nodes["payload_offset"][:real], self.nodes["payload_length"][:real]),
                                       ("texture", self.textures["offset"], self.textures["length"])):
            # exact arithmetic on Python integers: no uint64 wrap
            for i, (offset, length) in enumerate(zip(offsets.tolist(), lengths.tolist())):
                if offset % ALIGNMENT:
                    raise Bm3dError("out of range", "%s %d starts at %d, not a multiple of 8" % (what, i, offset))
                if length == 0:
                    raise Bm3dError("out of range", "%s %d is empty" % (what, i))
                if offset < data_start or offset + length > self.metadata_offset:
                    raise Bm3dError("out of range", "%s %d [%d, %d + %d) is not inside the data [%d, %d)"
                                    % (what, i, offset, offset, length, data_start, self.metadata_offset))
        ranges = [(0, self.header_size, "the header"), (self.index_offset, self.index_offset + self.index_length, "the index")]
        ranges += [(o, o + n, "the payload of node %d" % i)
                   for i, (o, n) in enumerate(zip(self.nodes["payload_offset"][:real].tolist(), self.nodes["payload_length"][:real].tolist()))]
        ranges += [(o, o + n, "texture %d" % i)
                   for i, (o, n) in enumerate(zip(self.textures["offset"].tolist(), self.textures["length"].tolist()))]
        if self.metadata_length:
            ranges.append((self.metadata_offset, self.metadata_offset + self.metadata_length, "the metadata"))
        ranges.sort(key=lambda r: r[0])
        for before, after in zip(ranges, ranges[1:]):
            if after[0] < before[1]:
                raise Bm3dError("overlapping sections", "%s [%d, %d) and %s [%d, %d) overlap" % (before[2], before[0], before[1], after[2], after[0], after[1]))

    def _check_index(self):
        n, sink = self.node_count, self.node_count - 1
        first = self.nodes["first_patch"].astype(np.int64)
        patches = self.patches

        def bad(message):
            raise Bm3dError("bad index", message)

        if first[0] != 0:
            bad("the first patch of node 0 is %d and not 0" % first[0])
        decreasing = np.nonzero(np.diff(first) < 0)[0]
        if decreasing.size:
            bad("the first patch decreases from node %d to node %d" % (decreasing[0], decreasing[0] + 1))
        if first[sink] != self.patch_count:
            bad("the first patch of the sink is %d and the file has %d patches" % (first[sink], self.patch_count))
        s = self.nodes[sink]
        if any(s[f] != 0 for f in SINK_FIELDS) or s["cone"].any() or s["center"].any():
            bad("the sink (node %d) has a field other than the first patch that is not 0" % sink)
        counts = np.diff(first)[:sink]
        empty = np.nonzero(counts == 0)[0]
        if empty.size:
            bad("node %d has no patch" % empty[0])
        real = self.nodes[:sink]
        small = np.nonzero((real["nface"] == 0) | (real["nvert"] < 3))[0]
        if small.size:
            bad("node %d has %d vertices and %d triangles" % (small[0], real["nvert"][small[0]], real["nface"][small[0]]))
        owner = np.repeat(np.arange(sink), counts)  # the node each patch belongs to
        child = patches["node"].astype(np.int64)
        beyond = np.nonzero(child >= n)[0]
        if beyond.size:
            bad("patch %d of node %d names node %d and the file has %d" % (beyond[0], owner[beyond[0]], child[beyond[0]], n))
        before = np.nonzero((child != sink) & (child <= owner))[0]
        if before.size:
            bad("patch %d of node %d names node %d, which is not stored after it" % (before[0], owner[before[0]], child[before[0]]))
        to_sink = (child == sink)
        per_node = np.add.reduceat(to_sink.astype(np.int64), first[:sink]) if sink else np.array([], dtype=np.int64)
        mixed = np.nonzero((per_node != 0) & (per_node != counts))[0]
        if mixed.size:
            bad("node %d has patches that name the sink and patches that do not" % mixed[0])
        ends = patches["triangle_offset"].astype(np.int64)
        for i in range(sink):
            lo, hi = first[i], first[i + 1]
            if np.any(np.diff(ends[lo:hi]) < 0):
                bad("the triangle offsets of node %d decrease" % i)
            if ends[hi - 1] != real["nface"][i]:
                bad("the last triangle offset of node %d is %d and the node has %d triangles" % (i, ends[hi - 1], real["nface"][i]))
        texture = patches["texture"].astype(np.int64)
        wrong = np.nonzero((texture != NO_TEXTURE) & (texture >= self.texture_count))[0]
        if wrong.size:
            bad("patch %d names texture %d and the file has %d" % (wrong[0], texture[wrong[0]], self.texture_count))
        for i in range(sink):
            t = texture[first[i]:first[i + 1]]
            if np.any(t != t[0]):
                bad("node %d has patches with different textures" % i)
            if (t[0] != NO_TEXTURE) != self.has_uvs:
                bad("node %d %s" % (i, "has a texture and the layout has no texture coordinates" if t[0] != NO_TEXTURE
                                    else "has no texture and the layout has texture coordinates"))

    def _root_count(self):
        first = self.nodes["first_patch"].tolist()
        child = self.patches["node"].tolist()
        roots = self.node_count - 1
        j = 0
        while j < roots:
            for p in range(first[j], first[j + 1]):
                roots = min(roots, child[p])
            j += 1
        return roots

    # --- what the comparison reads ------------------------------------------------------------
    @property
    def layout(self):
        return {"normals": self.has_normals, "colors": self.has_colors, "uvs": self.has_uvs}

    @property
    def n_real_textures(self):
        return self.texture_count

    def texture_span(self, index):
        begin = int(self.textures["offset"][index])
        return begin, begin + int(self.textures["length"][index])

    # --- the bytes ------------------------------------------------------------------------------
    def read(self, offset, length):
        if self._handle is None:
            self._handle = open(self.path, "rb")
        self._handle.seek(offset)
        return self._handle.read(length)

    def payload(self, node):
        return self.read(int(self.nodes["payload_offset"][node]), int(self.nodes["payload_length"][node]))

    def stream_header(self, node):
        """The header of the corto stream of a node, which the index must agree with (section 5)."""
        return corto_header(self.payload(node))

    def metadata(self):
        """The metadata block as a dict; Bm3dError (bad metadata) when it is not a JSON object."""
        if self.metadata_length == 0:
            return {}
        if self.metadata_length > MAX_METADATA:
            raise Bm3dError("bad metadata", "the metadata is %d bytes, more than %d" % (self.metadata_length, MAX_METADATA))
        text = self.read(self.metadata_offset, self.metadata_length)
        if text.startswith(b"\xef\xbb\xbf"):
            raise Bm3dError("bad metadata", "the metadata begins with a byte order mark")
        try:
            value = json.loads(text.decode("utf-8"), object_pairs_hook=_no_repeats)
        except (ValueError, UnicodeDecodeError) as error:
            raise Bm3dError("bad metadata", "the metadata is not valid JSON: %s" % error)
        if not isinstance(value, dict):
            raise Bm3dError("bad metadata", "the metadata is not a JSON object")
        return value

    def content_hash(self):
        with open(self.path, "rb") as handle:
            return content_hash(handle.read())

    def check_streams(self):
        """Every corto stream agrees with the index: the counts, the ends of the groups (the ends
        of the patches), and the attributes the layout names. Returns the problems, a list."""
        problems = []
        first = self.nodes["first_patch"]
        wanted = ["position"] + (["normal"] if self.has_normals else []) + (["color"] if self.has_colors else []) + (["uv"] if self.has_uvs else [])
        for node in range(self.n_real_nodes):
            try:
                h = self.stream_header(node)
            except (ValueError, struct.error) as error:
                problems.append("node %d: %s" % (node, error))
                continue
            record = self.nodes[node]
            ends = self.patches["triangle_offset"][first[node]:first[node + 1]].tolist()
            if (h["nvert"], h["nface"]) != (int(record["nvert"]), int(record["nface"])):
                problems.append("node %d: the stream has %d vertices and %d triangles, the index %d and %d"
                                % (node, h["nvert"], h["nface"], record["nvert"], record["nface"]))
            if h["group_ends"] != ends:
                problems.append("node %d: the groups of the stream end at %s and the patches at %s" % (node, h["group_ends"][:8], ends[:8]))
            if sorted(h["attributes"]) != sorted(wanted):
                problems.append("node %d: the stream holds %s and the layout names %s" % (node, h["attributes"], wanted))
        return problems


def _no_repeats(pairs):
    seen = {}
    for key, value in pairs:
        if key in seen:
            raise ValueError("the key %r is repeated" % key)
        seen[key] = value
    return seen


def open_model(path):
    """The reader for the container a file is in: Bm3dFile or format0.Format0File."""
    with open(path, "rb") as handle:
        magic = handle.read(4)
    if magic == MAGIC:
        return Bm3dFile(path)
    from format0 import Format0File
    return Format0File(path)


# --- printing ----------------------------------------------------------------------------------
def describe_layout(slots):
    return ", ".join("%s x %d" % (ATTRIBUTE_NAMES[t] if t < len(ATTRIBUTE_NAMES) else t, n) for t, n in slots if t)


def show(model, options):
    flags = [name for bit, name in ((FLAG_CORTO, "CORTO_COMPRESSED"), (FLAG_WEBP, "TEXTURE_WEBP")) if model.flags & bit]
    print("file          %s (%d bytes)" % (model.path, model.size))
    print("content hash  %s" % model.content_hash())
    print("version       %d, header %d bytes, flags 0x%x (%s)" % (model.version, model.header_size, model.flags, " | ".join(flags)))
    print("vertices      %s" % describe_layout(model.vertex_layout))
    print("triangles     %s" % describe_layout(model.face_layout))
    print("counts        %d vertices, %d triangles in %d nodes (and the sink), %d patches, %d textures"
          % (model.nvert, model.nface, model.n_real_nodes, model.n_patches, model.texture_count))
    print("graph         %d root%s, %d levels, %d leaves" % (model.roots, "" if model.roots == 1 else "s", model.n_levels, int(model.leaf.sum())))
    print("sphere        centre (%g, %g, %g), radius %g" % tuple(model.sphere))
    print("index         [%d, %d)" % (model.index_offset, model.index_offset + model.index_length))
    print("metadata      [%d, %d)" % (model.metadata_offset, model.metadata_offset + model.metadata_length))
    problems = model.check_streams()
    print("streams       %s" % ("every corto stream agrees with the index" if not problems else "%d problems" % len(problems)))
    for problem in problems[:20]:
        print("  " + problem)
    if options.metadata:
        try:
            print(json.dumps(model.metadata(), indent=2, ensure_ascii=False))
        except Bm3dError as error:
            print("metadata      %s" % error)
            return 1
    if options.nodes:
        print("\n%6s %5s %4s %6s %6s %12s %12s %9s %8s" % ("node", "level", "leaf", "nvert", "nface", "error", "payload at", "length", "texture"))
        for node in range(min(model.n_real_nodes, options.limit)):
            record = model.nodes[node]
            textures = model.node_textures(node)
            print("%6d %5d %4s %6d %6d %12.6g %12d %9d %8s" % (node, model.level[node], "yes" if model.leaf[node] else "", record["nvert"], record["nface"],
                                                             record["error"], record["payload_offset"], record["payload_length"], textures[0] if textures else "-"))
    return 1 if problems else 0


# --- the comparison with a format 0 file --------------------------------------------------------
class Report:
    def __init__(self):
        self.rows = []
        self.failed = False

    def add(self, name, ok, detail=""):
        self.rows.append((name, ok, detail))
        self.failed = self.failed or not ok
        print("%-4s %-34s %s" % ("ok" if ok else "FAIL", name, detail))


def same_bits(a, b):
    """Elementwise equality of two float32 arrays by their bits (a NaN equals the same NaN)."""
    return np.ascontiguousarray(a, dtype="<f4").view("<u4") == np.ascontiguousarray(b, dtype="<f4").view("<u4")


def compare(old, new, report):
    """Whether `new` (a .bm3d file) is the model of `old` (a format 0 file built from the same
    input by the program before the new format): same graph, same errors and spheres, the same
    bytes in every corto stream and every texture."""
    report.add("nodes", old.n_nodes == new.n_nodes, "%d and %d (with the sink)" % (old.n_nodes, new.n_nodes))
    report.add("patches", old.n_patches == new.n_patches, "%d and %d" % (old.n_patches, new.n_patches))
    report.add("textures", old.n_real_textures == new.n_real_textures, "%d and %d" % (old.n_real_textures, new.n_real_textures))
    report.add("layout", old.layout == new.layout, str(new.layout))
    if report.failed:
        return
    report.add("bounding sphere", bool(same_bits(old.sphere, new.sphere).all()), "%s" % (tuple(float(v) for v in new.sphere),))

    n = new.n_real_nodes
    a, b = old.nodes[:n], new.nodes[:n]
    for field in ("error", "radius", "tight_radius", "center"):
        equal = same_bits(a[field], b[field]).reshape(n, -1).all(axis=1)
        report.add("node " + field, bool(equal.all()), "%d of %d nodes differ" % (int((~equal).sum()), n) if not equal.all() else "%d nodes" % n)
    cone = np.all(a["cone"] == b["cone"], axis=1)
    report.add("node cone", bool(cone.all()), "%d of %d nodes differ" % (int((~cone).sum()), n) if not cone.all() else "%d nodes" % n)
    report.add("first patch", bool((old.nodes["first_patch"] == new.nodes["first_patch"]).all()), "")
    report.add("patch children and textures", bool((old.patches["node"] == new.patches["node"]).all() and (old.patches["texture"] == new.patches["texture"]).all()), "%d patches" % old.n_patches)

    # the corto streams: the same bytes, and a zero padding after them in the old container
    changed = []
    differing = []
    old_offsets = np.append(old.nodes["offset"].astype(np.int64) * 256, old.size)
    with open(old.path, "rb") as handle:
        for node in range(n):
            begin = int(old.nodes["offset"][node]) * 256
            end = int(old.nodes["offset"][node + 1]) * 256
            region = _read(handle, begin, end - begin)
            stream = new.payload(node)
            if region[:len(stream)] != stream:
                differing.append(node)
            elif region[len(stream):].strip(b"\0"):
                differing.append(node)  # the old stream is longer than the new one
            if (int(a["nvert"][node]), int(a["nface"][node])) != (int(b["nvert"][node]), int(b["nface"][node])):
                changed.append(node)
    report.add("corto streams", not differing, "%d nodes, %d differ%s" % (n, len(differing), " (first: node %d)" % differing[0] if differing else ""))

    # a count that differs is the encoder's: it agrees with the stream, which is the old one's
    explained = [node for node in changed if node not in differing]
    problems = new.check_streams()
    report.add("counts of the index", not problems and len(explained) == len(changed),
               "%d nodes have the counts the encoder left instead of the ones the builder gave it; %d problems" % (len(changed), len(problems)))
    if changed:
        old_v, new_v = int(a["nvert"][changed].astype(np.int64).sum()), int(b["nvert"][changed].astype(np.int64).sum())
        old_f, new_f = int(a["nface"][changed].astype(np.int64).sum()), int(b["nface"][changed].astype(np.int64).sum())
        print("     (in those nodes %d -> %d vertices, %d -> %d triangles; first: node %d)" % (old_v, new_v, old_f, new_f, changed[0]))
    for problem in problems[:10]:
        print("     " + problem)
    # the ends of the patches differ only in a node whose count changed
    triangle_ends = old.patches["triangle_offset"] != new.patches["triangle_offset"]
    owner = np.repeat(np.arange(n), np.diff(new.nodes["first_patch"].astype(np.int64))[:n])
    stray = sorted(set(owner[triangle_ends].tolist()) - set(changed))
    report.add("ends of the patches", not stray, "%d patches differ, all in nodes whose count changed" % int(triangle_ends.sum()) if not stray
               else "node %d has other ends" % stray[0])

    # the textures
    differing = []
    with open(old.path, "rb") as handle:
        for t in range(new.n_real_textures):
            begin, end = old.texture_span(t)
            region = _read(handle, begin, end - begin)
            image = new.read(int(new.textures["offset"][t]), int(new.textures["length"][t]))
            if region[:len(image)] != image or region[len(image):].strip(b"\0"):
                differing.append(t)
    report.add("textures bytes", not differing, "%d textures, %d differ%s" % (new.n_real_textures, len(differing), " (first: %d)" % differing[0] if differing else ""))


def _read(handle, offset, length):
    handle.seek(offset)
    return handle.read(length)


def run_compare(old_path, new_path):
    from format0 import Format0File
    try:
        old = Format0File(old_path)
        new = Bm3dFile(new_path)
    except (OSError, ModelFileError) as error:
        print("dump.py: %s" % error, file=sys.stderr)
        return 2
    print("old  %s  %d bytes (format 0)" % (old.path, old.size))
    print("new  %s  %d bytes, content hash %s" % (new.path, new.size, new.content_hash()))
    print("size %+d bytes (%+.3f%%)" % (new.size - old.size, 100.0 * (new.size - old.size) / old.size))
    report = Report()
    compare(old, new, report)
    print("\n%s" % ("SAME MODEL: only the container differs" if not report.failed else "DIFFERENT"))
    return 1 if report.failed else 0


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "compare":
        parser = argparse.ArgumentParser(prog="dump.py compare", description="compare a format 0 file with the .bm3d file built from the same input")
        parser.add_argument("old", metavar="OLD.nxz")
        parser.add_argument("new", metavar="NEW.bm3d")
        options = parser.parse_args(argv[1:])
        return run_compare(options.old, options.new)
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("file", metavar="FILE", help="a .bm3d file")
    parser.add_argument("--nodes", action="store_true", help="the table of nodes")
    parser.add_argument("--limit", type=int, default=50, help="the most nodes --nodes prints (default 50)")
    parser.add_argument("--metadata", action="store_true", help="print the metadata block")
    parser.add_argument("--hash", action="store_true", help="print only the content hash")
    options = parser.parse_args(argv)
    try:
        model = Bm3dFile(options.file)
    except (OSError, ModelFileError) as error:
        print("dump.py: %s" % error, file=sys.stderr)
        return 2
    if options.hash:
        print(model.content_hash())
        return 0
    return show(model, options)


if __name__ == "__main__":
    sys.exit(main())
