# Golden files

`golden_test.cpp` builds six small models with the program (a cube from an OBJ, a coloured pyramid
from a PLY, a wavy surface from a binary STL, a square with a degenerate triangle, a textured
square, a surface of several nodes) and compares the content hash of each file (the SHA-256 of
every byte but the metadata block, `docs/FORMAT.md` section 9) with the line of `hashes.txt` that
names it. A change of the code that alters a file outside its metadata fails there, so it is a
decision and not an accident; the failure prints the hash to put in `hashes.txt`.

The bytes of a file depend on the standard library the program is built with when a node holds more
than a few triangles: the builder sorts the triangles of a node by their node with `std::sort`, and
the libraries order triangles with equal keys differently. Builds with one library are repeatable,
so `hashes.txt` has a `name@library` line for each of the two libraries of the CI (libstdc++ and
the one of MSVC) for the surface of several nodes, and one line for the small models, whose bytes
are the same with both.

The files are the fixtures of the other readers of the format (the viewer's tests read every
`.bm3d` file of the folder they are given). They are not committed: the test copies each one to
the folder named by the environment variable `BM3D_GOLDEN_DIR`, by default `golden/` in the build
folder, with `<name>.sha256` beside it. To write them without running the other tests:

    cmake --build build --target bm3d_golden_fixtures
    BM3D_GOLDEN_DIR=/some/folder build/tests/bm3d_golden_tests      # or into another folder

`scripts/dump.py <file>` reads one with an implementation of its own and prints its content hash.
