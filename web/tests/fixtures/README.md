<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (C) 2026 Digipa -->

# Fixtures of the viewer tests

Two small models in format 1, with corto streams and WebP textures made by the bm3d
command line:

* `terrain.bm3d`: a textured height field (33 282 triangles, ten nodes, ten textures, normals);
  made from a generated OBJ with a 512 x 512 PNG texture.
* `sphere.bm3d`: a sphere with vertex colours (6 144 triangles, one node, no texture); the poles
  hold degenerate triangles, so the counts written for the node (3 010 vertices, 6 016 triangles)
  are lower than those of the input, as docs/FORMAT.md section 12 says.

The command line of version 0.2.0 writes format 0 (the Nexus container) only. These two files
were made with it and re-packed in format 1 by `tests/support/lift-nxz.js`: the corto stream of
a node and the WebP image of a texture are the same in both formats, only the container differs.
(The padding after a corto stream is part of its length in these files, since format 0 does not
record where a stream ends.) When the command line writes format 1, the tests also read the files
in `BM3D_GOLDEN_DIR` (by default `tests/golden/` of the repository, when it exists).
