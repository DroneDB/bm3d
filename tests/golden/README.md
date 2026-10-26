# Golden files

`golden_test.cpp` builds ten small models with the program (a cube from an OBJ, a coloured
pyramid from a PLY, a wavy surface from a binary STL, a square with a degenerate triangle, a
textured square, a surface of several nodes, a textured surface of thousands of pieces of texture of
the same size, 40 triangles that share one edge, and the two models of the viewer's tests: a
textured terrain of ten nodes and a sphere with vertex colours) and compares the content hash of each file (the
SHA-256 of every byte but the metadata block, `docs/FORMAT.md` section 9) with the line of
`hashes.txt` that names it. A change of the code that alters a file outside its metadata fails
there, so it is a decision and not an accident; the failure prints the hash to put in `hashes.txt`.

A file is the same bytes with every compiler and standard library, so each model has one hash, and
the CI checks them on Linux (libstdc++) and on Windows (MSVC). Every sort, heap and packing of the
builder and of the encoder orders equal keys by something of the input (a stable sort, or the index,
the order of creation, the symbol as the last key), where `std::sort` and the heap functions would
leave them in the order of the library, and nothing the builder or the encoder writes comes from a
function of the mathematical library that is not exactly rounded (acos, cos, log and pow differ in the
last bit between platforms; sqrt does not). The textures are encoded by libwebp. The surface
of several nodes, the pieces of texture and the shared edge are the models where an order left to
the library shows: equal keys in many triangles of
a node, equal costs of the collapses of a flat region, equal pieces of texture, an edge of more than
two faces. The inputs are written with integer arithmetic for the same reason.

The files are the fixtures of the other readers of the format (the viewer's tests read every
`.bm3d` file of the folder they are given, and `web/tests/fixtures/` holds copies of `terrain` and
`sphere`, which the viewer's tests check against `hashes.txt`). They are not committed: the test copies each one to
the folder named by the environment variable `BM3D_GOLDEN_DIR`, by default `golden/` in the build
folder, with `<name>.sha256` beside it. To write them without running the other tests:

    cmake --build build --target bm3d_golden_fixtures
    BM3D_GOLDEN_DIR=/some/folder build/tests/bm3d_golden_tests      # or into another folder

`scripts/dump.py <file>` reads one with an implementation of its own and prints its content hash.
