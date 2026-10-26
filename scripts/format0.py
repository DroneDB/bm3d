#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read the index of a format 0 file (.nxs / .nxz, the container Nexus wrote) and decode its
node textures.

This is the reader behind scripts/compare.py. It does not decode the geometry (a .nxz keeps it
compressed); it reads what a structural comparison needs: the header, the node, patch and
texture tables, and the bytes of the node textures.

File layout (little endian, as the Nexus builder wrote it; the byte offsets are the ones of the
structs in src/common/dag.h and signature.h):

  header, 88 bytes
      0 u32 magic 0x4e787320     4 u32 version        8 u64 nvert       16 u64 nface
     24 vertex attributes: 8 x (u8 type, u8 number)   40 face attributes: the same
     56 u32 flags (0x1 ptexture, 0x2 meco, 0x4 corto, 0x8 deepzoom)
     60 u32 n_nodes  64 u32 n_patches  68 u32 n_textures
     72 bounding sphere: 3 x f32 centre, f32 radius
  node table, n_nodes x 44 bytes; the last entry is a sink that ends the table
      u32 offset (units of 256 bytes)  u16 nvert  u16 nface  f32 error  4 x i16 cone
      3 x f32 sphere centre  f32 sphere radius  f32 tight radius  u32 first_patch
  patch table, n_patches x 12 bytes: u32 node (destination), u32 triangle_offset, u32 texture
  texture table, n_textures x 68 bytes: u32 offset (units of 256 bytes), 16 x f32 matrix;
      the last entry is a sink, so texture t spans offset[t] .. offset[t + 1]

The patches of node n are first_patch[n] .. first_patch[n + 1]. A patch links its node to the
node that refines it; the leaves link to the sink. Nodes are stored parents before children, so
a node's level (its depth below the roots) is one more than the deepest parent, computed in
one pass over the nodes in index order.
"""
import io
import struct

import numpy as np

MAGIC = 0x4E787320
PADDING = 256
HEADER_SIZE = 88
NO_TEXTURE = 0xFFFFFFFF

NODE_DTYPE = np.dtype([
    ("offset", "<u4"), ("nvert", "<u2"), ("nface", "<u2"), ("error", "<f4"),
    ("cone", "<i2", (4,)), ("center", "<f4", (3,)), ("radius", "<f4"),
    ("tight_radius", "<f4"), ("first_patch", "<u4"),
])
PATCH_DTYPE = np.dtype([("node", "<u4"), ("triangle_offset", "<u4"), ("texture", "<u4")])
TEXTURE_DTYPE = np.dtype([("offset", "<u4"), ("matrix", "<f4", (16,))])
assert NODE_DTYPE.itemsize == 44 and PATCH_DTYPE.itemsize == 12 and TEXTURE_DTYPE.itemsize == 68

ATTRIBUTE_TYPES = ("none", "byte", "ubyte", "short", "ushort", "int", "uint", "float", "double")


class Format0Error(ValueError):
    pass


class Format0File:
    """The tables of one format 0 file; node textures are read from the file on demand."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as handle:
            handle.seek(0, io.SEEK_END)
            self.size = handle.tell()
            handle.seek(0)
            head = handle.read(HEADER_SIZE)
            if len(head) < HEADER_SIZE:
                raise Format0Error("%s: shorter than the 88 byte header" % path)
            (self.magic, self.version, self.nvert, self.nface) = struct.unpack_from("<IIQQ", head, 0)
            if self.magic != MAGIC:
                raise Format0Error("%s: bad magic 0x%08x" % (path, self.magic))
            self.vertex_attributes = tuple(struct.unpack_from("<16B", head, 24))
            self.face_attributes = tuple(struct.unpack_from("<16B", head, 40))
            (self.flags, self.n_nodes, self.n_patches, self.n_textures) = struct.unpack_from("<IIII", head, 56)
            self.sphere = np.array(struct.unpack_from("<4f", head, 72), dtype=np.float64)

            def table(dtype, count):
                data = handle.read(dtype.itemsize * count)
                if len(data) != dtype.itemsize * count:
                    raise Format0Error("%s: truncated index" % path)
                return np.frombuffer(data, dtype=dtype)

            self.nodes = table(NODE_DTYPE, self.n_nodes)
            self.patches = table(PATCH_DTYPE, self.n_patches)
            self.textures = table(TEXTURE_DTYPE, self.n_textures)
        if self.n_nodes < 1:
            raise Format0Error("%s: no nodes" % path)
        self._handle = None
        self._derive()

    # --- layout ---------------------------------------------------------------------------
    @property
    def layout(self):
        """What must be equal in two files built by the same program: magic, version, flags
        and the vertex and face attribute layout."""
        return {
            "magic": self.magic, "version": self.version, "flags": self.flags,
            "vertex_attributes": [(ATTRIBUTE_TYPES[t] if t < len(ATTRIBUTE_TYPES) else t, n)
                                  for t, n in zip(self.vertex_attributes[0::2], self.vertex_attributes[1::2])],
            "face_attributes": [(ATTRIBUTE_TYPES[t] if t < len(ATTRIBUTE_TYPES) else t, n)
                                for t, n in zip(self.face_attributes[0::2], self.face_attributes[1::2])],
        }

    # --- the DAG --------------------------------------------------------------------------
    def _derive(self):
        sink = self.n_nodes - 1
        self.sink = sink
        real = self.nodes[:sink]
        first = self.nodes["first_patch"].astype(np.int64)
        if np.any(np.diff(first) < 0) or (self.n_nodes and first[-1] > self.n_patches):
            raise Format0Error("%s: patch ranges are not increasing" % self.path)
        level = np.zeros(sink, dtype=np.int32)
        leaf = np.ones(sink, dtype=bool)
        patch_node = self.patches["node"]
        for n in range(sink):
            lo, hi = first[n], first[n + 1]
            if hi > lo:
                children = patch_node[lo:hi]
                real_children = children[children < sink]
                if real_children.size and int(real_children.min()) <= n:
                    raise Format0Error("%s: node %d links to a node stored before it" % (self.path, n))
                if real_children.size:
                    leaf[n] = False
                    np.maximum.at(level, real_children, level[n] + 1)
        self.level = level
        self.leaf = leaf
        self.real_nodes = real

    @property
    def n_real_nodes(self):
        return self.n_nodes - 1

    @property
    def n_levels(self):
        return int(self.level.max()) + 1 if self.level.size else 0

    def node_textures(self, node):
        """Distinct texture indexes the patches of a node use."""
        lo, hi = int(self.nodes["first_patch"][node]), int(self.nodes["first_patch"][node + 1])
        seen = []
        for t in self.patches["texture"][lo:hi]:
            t = int(t)
            if t != NO_TEXTURE and t < self.n_textures - 1 and t not in seen:
                seen.append(t)
        return seen

    # --- texture bytes --------------------------------------------------------------------
    def texture_span(self, index):
        begin = int(self.textures["offset"][index]) * PADDING
        end = int(self.textures["offset"][index + 1]) * PADDING
        return begin, min(end, self.size)

    def texture_bytes(self, index, limit=None):
        """The encoded texture (its first `limit` bytes when given)."""
        if self._handle is None:
            self._handle = open(self.path, "rb")
        begin, end = self.texture_span(index)
        if limit is not None:
            end = min(end, begin + limit)
        self._handle.seek(begin)
        return self._handle.read(max(0, end - begin))

    def texture_image(self, index):
        """The decoded texture as a PIL image (WebP or JPEG; whatever Pillow reads)."""
        from PIL import Image
        return Image.open(io.BytesIO(self.texture_bytes(index)))

    def texture_size(self, index):
        """(width, height) from the start of the encoded texture, without decoding the pixels.

        WebP keeps its size in the first 30 bytes; a JPEG has it in the frame header that
        follows its metadata, so the read grows until Pillow finds it."""
        from PIL import Image, UnidentifiedImageError
        begin, end = self.texture_span(index)
        for limit in (4096, 65536, end - begin):
            try:
                with Image.open(io.BytesIO(self.texture_bytes(index, limit))) as image:
                    return image.size
            except (UnidentifiedImageError, OSError, SyntaxError):
                if limit >= end - begin:
                    raise

    def close(self):
        if self._handle is not None:
            self._handle.close()
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
