<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (C) 2026 Digipa -->

# Fixtures of the viewer tests

Two small models in format 1, written by the bm3d command line. They are two of its golden files
(`tests/golden/` of the repository), and each `.sha256` file holds the content hash
(docs/FORMAT.md, section 9) the golden test wrote next to it:

* `terrain.bm3d`: a textured height field (a 129 x 129 grid of squares, 33 282 triangles, with a
  512 x 512 PNG texture), built into ten nodes and ten textures, with normals and colours.
* `sphere.bm3d`: a sphere of radius 1 with a colour at each vertex (a cube of 22 x 22 squares a
  face, pushed onto the sphere: 5 808 triangles), one node, no texture. At each corner of the cube
  a triangle with two equal corners and a point inside the sphere is dropped by the encoder, so
  the node holds 2 906 vertices and 5 808 triangles, fewer than the input (docs/FORMAT.md,
  section 12).

The inputs are made by `tests/golden/golden_test.cpp` with integer arithmetic and square roots,
so the files are the same bytes on every platform. To write them again, after a change of the
builder that changes their lines in `tests/golden/hashes.txt`:

    cmake --build <build folder> --target bm3d_golden_tests
    python3 scripts/web-fixtures.py --build <build folder>

`tests/files.test.js` checks that the content hash of each fixture is its line of
`tests/golden/hashes.txt`, so fixtures that the command line would no longer write fail the tests.
Only the `generator` of the metadata, which the content hash leaves out, names the version of the
program that wrote them.

The tests also read the golden files in `BM3D_GOLDEN_DIR` (the CI builds the command line and
writes them there; by default `tests/golden/` of the repository, which holds none).
