#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read the index of a format 0 file (.nxs / .nxz, the container Nexus wrote) and decode its
node textures.

bm3d wrote this container until version 0.2.0 and writes the one of docs/FORMAT.md since (read by
dump.py). This reader stays for the comparisons with the files the Nexus builder and bm3d 0.2.0
wrote (scripts/compare.py, and dump.py's comparison of the two containers).

It does not decode the geometry (a .nxz keeps it compressed); it reads what a structural
comparison needs: the header, the node, patch and texture tables, and the bytes of the node
textures. What the two formats have in common is in modelfile.py.

File layout (little endian, as the Nexus builder wrote it):

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

A patch links its node to the node that refines it; the leaves link to the sink.
"""
import io
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from modelfile import ModelFile, ModelFileError, NO_TEXTURE  # noqa: E402,F401

MAGIC = 0x4E787320
PADDING = 256
HEADER_SIZE = 88

NODE_DTYPE = np.dtype([
    ("offset", "<u4"), ("nvert", "<u2"), ("nface", "<u2"), ("error", "<f4"),
    ("cone", "<i2", (4,)), ("center", "<f4", (3,)), ("radius", "<f4"),
    ("tight_radius", "<f4"), ("first_patch", "<u4"),
])
PATCH_DTYPE = np.dtype([("node", "<u4"), ("triangle_offset", "<u4"), ("texture", "<u4")])
TEXTURE_DTYPE = np.dtype([("offset", "<u4"), ("matrix", "<f4", (16,))])
assert NODE_DTYPE.itemsize == 44 and PATCH_DTYPE.itemsize == 12 and TEXTURE_DTYPE.itemsize == 68

ATTRIBUTE_TYPES = ("none", "byte", "ubyte", "short", "ushort", "int", "uint", "float", "double")


class Format0Error(ModelFileError):
    pass


class Format0File(ModelFile):
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
            (self.flags, self.n_nodes_header, self.n_patches_header, self.n_textures) = struct.unpack_from("<IIII", head, 56)
            self.sphere = np.array(struct.unpack_from("<4f", head, 72), dtype=np.float64)

            def table(dtype, count):
                data = handle.read(dtype.itemsize * count)
                if len(data) != dtype.itemsize * count:
                    raise Format0Error("%s: truncated index" % path)
                return np.frombuffer(data, dtype=dtype)

            self.nodes = table(NODE_DTYPE, self.n_nodes_header)
            self.patches = table(PATCH_DTYPE, self.n_patches_header)
            self.textures = table(TEXTURE_DTYPE, self.n_textures)
        self._derive()

    # --- layout ---------------------------------------------------------------------------
    @property
    def layout(self):
        """What must be equal in two models: whether the vertices have normals, colours and
        texture coordinates (the compression of the nodes is not a property of the model)."""
        return {"normals": self.vertex_attributes[2] != 0, "colors": self.vertex_attributes[4] != 0,
                "uvs": self.vertex_attributes[6] != 0}

    @property
    def raw_layout(self):
        """The magic, version, flags and attribute layout as the header spells them."""
        return {
            "magic": self.magic, "version": self.version, "flags": self.flags,
            "vertex_attributes": [(ATTRIBUTE_TYPES[t] if t < len(ATTRIBUTE_TYPES) else t, n)
                                  for t, n in zip(self.vertex_attributes[0::2], self.vertex_attributes[1::2])],
            "face_attributes": [(ATTRIBUTE_TYPES[t] if t < len(ATTRIBUTE_TYPES) else t, n)
                                for t, n in zip(self.face_attributes[0::2], self.face_attributes[1::2])],
        }

    # --- textures ---------------------------------------------------------------------------
    @property
    def n_real_textures(self):
        """The texture table ends with a sink, so texture t spans offset[t] .. offset[t + 1]."""
        return max(0, self.n_textures - 1)

    def texture_span(self, index):
        begin = int(self.textures["offset"][index]) * PADDING
        end = int(self.textures["offset"][index + 1]) * PADDING
        return begin, min(end, self.size)
