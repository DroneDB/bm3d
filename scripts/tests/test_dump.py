#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of the reader of scripts/dump.py: it opens the files the command line writes, and refuses
damaged copies of them with the error, and the node or patch, that docs/FORMAT.md section 11 names.

The files are the fixtures of the viewer (web/tests/fixtures/, written by the command line), and the
golden files in BM3D_GOLDEN_DIR when it is set.

  python3 -m unittest discover -s scripts/tests
"""
import glob
import math
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
import dump  # noqa: E402

FIXTURES = os.path.join(ROOT, "web", "tests", "fixtures")
NAN = float("nan")
INF = float("inf")


class File:
    """The bytes of a .bm3d file, and where its records are"""

    def __init__(self, path):
        with open(path, "rb") as handle:
            self.data = bytearray(handle.read())
        self.index = struct.unpack_from("<Q", self.data, 80)[0]
        self.nodes, self.patch_count = struct.unpack_from("<II", self.data, 96)
        self.sink = self.nodes - 1

    def node(self, n):
        return self.index + 56 * n

    def patch(self, p):
        return self.index + 56 * self.nodes + 12 * p

    def first_patch(self, n):
        return struct.unpack_from("<I", self.data, self.node(n) + 52)[0]

    def patches_of(self, n):
        return range(self.first_patch(n), self.first_patch(n + 1))

    def put_float(self, at, value):
        struct.pack_into("<f", self.data, at, value)

    def put_u32(self, at, value):
        struct.pack_into("<I", self.data, at, value)


def open_bytes(data):
    with tempfile.TemporaryDirectory() as folder:
        path = os.path.join(folder, "damaged.bm3d")
        with open(path, "wb") as handle:
            handle.write(data)
        return dump.Bm3dFile(path)


class ReaderTest(unittest.TestCase):
    def setUp(self):
        self.terrain = os.path.join(FIXTURES, "terrain.bm3d")

    def refused(self, data, kind, text):
        with self.assertRaises(dump.Bm3dError) as caught:
            open_bytes(bytes(data))
        self.assertEqual(caught.exception.kind, kind, str(caught.exception))
        self.assertIn(text, str(caught.exception))

    def test_the_files_of_the_command_line_open_and_have_their_content_hash(self):
        folders = [FIXTURES] + ([os.environ["BM3D_GOLDEN_DIR"]] if os.environ.get("BM3D_GOLDEN_DIR") else [])
        paths = sorted(p for folder in folders for p in glob.glob(os.path.join(folder, "*.bm3d")))
        self.assertGreaterEqual(len(paths), 2)
        for path in paths:
            with self.subTest(path=path):
                dump.Bm3dFile(path)
                with open(path, "rb") as handle:
                    hash_ = dump.content_hash(handle.read())
                with open(path[:-len(".bm3d")] + ".sha256") as handle:
                    self.assertEqual(hash_, handle.read().split()[0])

    def test_a_sphere_of_the_header_that_is_not_finite_is_a_bad_sphere(self):
        for at, value in ((108, NAN), (112, INF), (116, -INF), (120, NAN), (120, INF)):
            with self.subTest(at=at, value=value):
                f = File(self.terrain)
                f.put_float(at, value)
                self.refused(f.data, "bad sphere", "not finite")

    def test_the_counts_are_checked_before_the_sphere_and_the_sphere_before_the_ranges(self):
        f = File(self.terrain)
        f.put_float(120, NAN)
        f.put_u32(96, 1)
        self.refused(f.data, "bad counts", "1 nodes")
        f.put_u32(96, f.nodes)
        struct.pack_into("<Q", f.data, 80, 129)  # an index offset that is not a multiple of 8
        self.refused(f.data, "bad sphere", "not finite")

    def test_a_value_of_a_node_that_is_not_finite_is_a_bad_index(self):
        for field, at in (("error", 16), ("centre", 36), ("radius", 44), ("tight radius", 48)):
            for value in (NAN, INF, -INF):
                with self.subTest(field=field, value=value):
                    f = File(self.terrain)
                    f.put_float(f.node(2) + at, value)
                    self.refused(f.data, "bad index", "node 2 has a value that is not finite")

    def test_every_byte_of_the_sink_but_its_first_patch_is_zero(self):
        for at in (16, 36, 44, 48):
            with self.subTest(at=at):
                f = File(self.terrain)
                f.put_float(f.node(f.sink) + at, -0.0)
                self.refused(f.data, "bad index", "sink")

    def test_each_rule_of_the_index_is_checked_over_the_whole_table_before_the_next(self):
        f = File(self.terrain)
        early = next(n for n in range(f.sink) if len(f.patches_of(n)) >= 2)
        late = f.sink - 1
        self.assertLess(early, late)
        first_of_early = f.patches_of(early)[0]
        second_of_early = f.patches_of(early)[1]
        last_of_late = f.patches_of(late)[-1]
        texture = struct.unpack_from("<I", f.data, f.patch(second_of_early) + 8)[0]
        f.put_u32(f.patch(second_of_early) + 8, texture + 1)  # early: patches of different textures (14)
        f.put_u32(f.patch(last_of_late) + 8, 999)               # late: a texture beyond the table (13)
        f.put_u32(f.patch(first_of_early) + 4, 65535)          # early: a triangle offset above the next (11)
        f.put_u32(f.patch(last_of_late), early)                # late: a patch that names a node before it (9)
        f.put_float(f.node(late) + 16, NAN)                     # late: an error that is not a number (7)
        self.refused(f.data, "bad index", "node %d has a value that is not finite" % late)
        f.put_float(f.node(late) + 16, 0.5)
        self.refused(f.data, "bad index", "patch %d of node %d names node %d" % (last_of_late, late, early))
        f.put_u32(f.patch(last_of_late), f.sink)
        self.refused(f.data, "bad index", "patch %d of node %d" % (second_of_early, early))
        f.put_u32(f.patch(first_of_early) + 4, 0)
        self.refused(f.data, "bad index", "patch %d of node %d names texture 999" % (last_of_late, late))
        f.put_u32(f.patch(last_of_late) + 8, texture)
        self.refused(f.data, "bad index", "node %d has patches with different textures" % early)


if __name__ == "__main__":
    unittest.main()
