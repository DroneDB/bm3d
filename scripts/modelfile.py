#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""What the readers of the two container formats share (format0.py, dump.py).

A model file holds a directed acyclic graph of nodes: a table of nodes (the last one is a sink
that ends it), a table of patches (a patch links a node to the node that refines it, the leaves
link to the sink) and a table of textures. The subclasses read the tables of their format into
the arrays named below and say where a texture lies in the file; this class derives the rest
(the levels of the graph, which nodes are leaves, the textures a node uses) and decodes the
textures. scripts/compare.py works on this interface, so that it can read either format.

The patches of node n are first_patch[n] .. first_patch[n + 1]. Nodes are stored parents before
children, so a node's level (its depth below the roots) is one more than the deepest parent,
computed in one pass over the nodes in index order.
"""
import io

import numpy as np

NO_TEXTURE = 0xFFFFFFFF


class ModelFileError(ValueError):
    pass


class ModelFile:
    """The tables of one file. A subclass sets, before it calls _derive():

      path, size                  the file name and its size in bytes
      nvert, nface                the totals of the header
      sphere                      the bounding sphere (x, y, z, radius) as float64
      nodes                       a structured array, the sink last, with the fields nvert, nface,
                                  error, center (3 floats), radius, tight_radius and first_patch
      patches                     a structured array with the fields node, triangle_offset, texture

    and implements n_real_textures, texture_span(index) and layout."""

    # --- the DAG --------------------------------------------------------------------------
    def _derive(self):
        sink = len(self.nodes) - 1
        if sink < 1:
            raise ModelFileError("%s: no nodes" % self.path)
        self.sink = sink
        self.real_nodes = self.nodes[:sink]
        first = self.nodes["first_patch"].astype(np.int64)
        if np.any(np.diff(first) < 0) or first[-1] > len(self.patches):
            raise ModelFileError("%s: patch ranges are not increasing" % self.path)
        level = np.zeros(sink, dtype=np.int32)
        leaf = np.ones(sink, dtype=bool)
        patch_node = self.patches["node"]
        for n in range(sink):
            lo, hi = first[n], first[n + 1]
            if hi > lo:
                children = patch_node[lo:hi]
                real_children = children[children < sink]
                if real_children.size and int(real_children.min()) <= n:
                    raise ModelFileError("%s: node %d links to a node stored before it" % (self.path, n))
                if real_children.size:
                    leaf[n] = False
                    np.maximum.at(level, real_children, level[n] + 1)
        self.level = level
        self.leaf = leaf
        self._handle = None

    @property
    def n_nodes(self):
        """The nodes of the table, the sink included."""
        return len(self.nodes)

    @property
    def n_patches(self):
        return len(self.patches)

    @property
    def n_real_nodes(self):
        return len(self.nodes) - 1

    @property
    def n_levels(self):
        return int(self.level.max()) + 1 if self.level.size else 0

    def node_textures(self, node):
        """Distinct texture indexes the patches of a node use."""
        lo, hi = int(self.nodes["first_patch"][node]), int(self.nodes["first_patch"][node + 1])
        seen = []
        for t in self.patches["texture"][lo:hi]:
            t = int(t)
            if t != NO_TEXTURE and t < self.n_real_textures and t not in seen:
                seen.append(t)
        return seen

    # --- texture bytes --------------------------------------------------------------------
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
