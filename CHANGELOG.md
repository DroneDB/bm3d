# Changelog

All notable changes to bm3d are listed here. The version is in `VERSION`.

## 0.2.0

- Eigen is gone: neither the program nor the library needs it, and `eigen3` is no longer in
  `vcpkg.json`, in the CMake files or in the CMake package. The simplification of a mesh without
  texture was its only user: it solved a 3x3 system with Eigen's `FullPivLU`. `SolveFullPivot3x3`
  (`third_party/vcg-patches/0007`) does the same operations in the same order, so the vertex it
  places is the same to the last bit; against Eigen it gave identical results, signs of zeros
  included, for 20 million quadrics (planes in general position, integer and coplanar ones, scales
  from 1e-160 to 1e160, infinities and NaNs), and the byte gate is the same on all eight reference
  models (STL, PLY and OBJ, 8 threads). The patch removes the VCGLib code that named Eigen and that
  a triangle pipeline does not call (an SVD placement option, matrix inverses, polygon principal
  components, two mesh algorithms; see `PROVENANCE.md`), and `fitting3.h`, `point_matching.h` and
  `inertia.h` are no longer taken (93 files of VCGLib instead of 96).
- The image libraries were chosen by measurement (`scripts/bench.py`: interleaved builds of one
  model with several programs, median and spread of the wall time, peak memory, size and SHA-256 of
  the file). On the large photogrammetry model (224k triangles, 72 PNG textures of 1.56
  gigapixels, `--threads 8`, three builds each, machine load 1.7 to 2.9) stb decodes the PNG
  textures 2.3% faster than libpng (median 109.7 s against 112.3 s, ranges 109.6 to 111.6 and 111.5
  to 113.0) and writes the same file (SHA-256 339f0276...a780, 358,059,008 bytes), with 6% more peak
  memory (4583 MiB against 4326): **PNG is now read with stb and the program no longer links
  libpng** (it stays in the tests, which write palette, 16 bit and interlaced PNGs with it). The
  16 bit samples are scaled to 8 bits with rounding as libpng did (stb drops the low byte) and the
  transparent gray of a gray PNG of 2 to 8 bits is still black, so the pictures are the same as
  before. The same textures as JPEG (quality 90, 4:2:0) decode as fast with stb (99.2 s against 99.0
  s, +0.1%) but into a different file (stb's inverse DCT and chroma upsampling round differently,
  350,339,840 bytes against 350,393,600), so **JPEG stays with libjpeg-turbo**, which the JPEG tiles
  of the texture cache need anyway. The program is 6% smaller (3.53 MB against 3.75 MB).
- New tests for the decoders: a gray PNG with alpha, a 16 bit PNG and a CMYK JPEG (Adobe style,
  inverted components) read to the exact pixels and built into a node texture of the expected
  color; the transparent gray of a gray PNG, a 1 bit PNG, a palette with transparent entries and an
  interlaced PNG read to the exact pixels (the same on libpng, which read them before). The
  mutation loop of the robustness tests also damages a PNG texture (a sixth of the runs, under the
  sanitizers too). The tests that used a TIFF as a convenient uncompressed picture use a BMP.
- The option `BM3D_WITH_TIFF` (on by default). Off, libtiff is not found, not linked and not
  compiled in (the program is 2.6 MB against 3.5 MB), a `.tif` texture is refused as invalid input
  (exit code 2) with a message that says this bm3d was built without TIFF support, `tiff` is a
  default feature of `vcpkg.json` that `-DBM3D_WITH_TIFF=OFF` drops from a standalone build (a
  parent project lists its own dependencies), the CMake package asks for TIFF only when it is on,
  and `ci.yml` builds and tests a Linux GCC leg with it off.
- Robustness tests (`ctest -L robustness`, `tests/robustness`): the PLY face-list bound and every
  count and index type, long header comments, garbage OBJ faces, texture coordinates with only u or
  not usable, coincident and zero-area triangles, truncated and `solid`-prefixed STL, and the pieces
  of corto that the sanitizers found at fault; and a seeded mutation loop over an OBJ, a PLY (text
  and binary) and an STL (text and binary), 500 runs by default (`BM3D_MUTATION_RUNS`,
  `BM3D_MUTATION_SEED`). Each run must end in time, not crash, report nothing to a sanitizer, and
  either build a file (exit 0) or refuse the input (exit 2) with no output left; exit 1 only when
  memory or a scratch file runs out. `ci.yml` runs 100 runs, `sanitizers.yml` 500 with ASan and UBSan
  and no suppression. Moved here from the integration tests: the PLY face list, long comment,
  coincident triangle and garbage OBJ face cases.
- Inputs that crashed or made a sanitizer report, found by the mutation loop of the new robustness
  tests and now refused with exit code 2 and a message that names the file:
  - a PLY header that ends inside the line `element vertex`, or whose element count is negative, beyond
    an int or not a number (plylib: two patches in `third_party/vcg-patches/`);
  - a PLY or OBJ texture coordinate that is a NaN, an infinity or beyond 16777216 in magnitude (the
    PLY ones were not checked, and the OBJ ones only for being finite; a huge coordinate made the
    simplification compute with infinities);
  - an OBJ face index of 19 digits or more (overflow of the parser).
  A PLY header line longer than 511 characters (a long comment) is refused with that reason, not
  with `Error: 5`, and the PLY and OBJ messages name the file.
- corto no longer has the undefined behaviour and the leak the sanitizers found in it, so the
  sanitizer workflow runs with no suppression. Five patches (`third_party/corto-patches/`, applied
  by `scripts/sync-corto.py` like the VCGLib ones): the octahedron encoding of a normal of length
  zero is (0, 0) (it divided 0 by 0 and converted the NaN to an int, which made the deltas and bit
  widths of the normals overflow); an empty array is not copied (`memcpy` of a null pointer); values are
  stored with `memcpy` instead of through unaligned pointers; the decoder's bit mask is unsigned; and
  `BitStream::reserve` frees what it replaces and allocates nothing for zero words (a node of fewer
  than 4 vertices leaked one allocation). Nodes that had a vertex whose normals add up to zero are a
  few hundred bytes smaller (those vertices cost less to store); every node reads back the same.
- Removed the code that exported a mesh as PLY or STL for debugging (`Mesh::savePly`,
  `TMesh::savePly`, `TMesh::savePlyTex`, and `Extractor::savePly`, `saveStl`, `saveUnifiedPly`
  with the code only they used) and the VCGLib export header that came with it. Output does not
  change.
- Removed MECO, the compression Nexus used before corto: the encoder and decoder
  (`src/nxszip`), the flag name and the unused error factor of the extractor. The program has
  always written corto only; flag value 0x2 stays unused in the container header. Output does not
  change.
- The program reads three formats, OBJ, PLY and STL, and says so. The extension is read in any
  case (`MODEL.OBJ` is an OBJ). A file with another extension (`.ts`, `.tsp`, `.off`, none) is
  invalid input, exit code 2, with a message that names `obj`, `ply` and `stl`; it used to be tried
  with a VCGLib importer. The loaders for Nexus' `.ts` and `.tsp` files, its colormap and the
  VCGLib importer loader are removed, and with them `TMesh::loadPly`. Output does not change.
- Removed the code paths the program never takes, and the options that nothing set:
  - point clouds: the cloud stream and kd-tree, the loaders' vertex reading, the cloud levels of the
    builder and the cloud simplification of the meshes;
  - deepzoom: nodes and textures in the files of a folder beside the container (the flag value 0x8
    stays unused in the header);
  - node textures that are the original image files, node textures rounded up to a power of two, and
    JPEG node textures (the node textures have been WebP). JPEG stays for reading input textures
    and for the texture tiles the builder spills to disk;
  - the origin, scale and vertex quantization of the loaders, which stayed at no offset, no scale
    and no quantization. The ASCII STL loader multiplied the x coordinate by the third scale
    instead of the z by it, which did nothing at a scale of 1 and is gone with the scale.
  Output does not change.
- `third_party/vcg` holds 96 files, not 123: the ones that only the removed code read are no longer
  copied (VCGLib's importers and exporters, its kd-tree and a few mesh algorithms). The files left
  are the same bytes as before.
- A vertex with a NaN, an infinity or a number beyond the range of a float as a coordinate is
  invalid input (exit code 2) in every format, with the file and the vertex or triangle in the
  message. Only OBJ refused it before; a PLY, a binary STL or an ASCII STL with one made the build
  run for ever. The PLY loader also starts each vertex it reads from zero, not from the stack: the
  alpha of the colour of a PLY that has red, green and blue and no alpha was whatever the stack
  held (0 in the files built so far).
- STL files are read by their size, not by how they start. A file of 84 + 50 n bytes, n being the
  count in its header, is binary, whatever its header says (a binary STL that begins with `solid`,
  which many programs write, was read as ASCII and gave garbage or no model); otherwise a file that
  starts with `solid` and a facet is ASCII; otherwise a longer file is binary and its n triangles
  are read, with a warning for the bytes after them; a shorter one is refused as truncated (exit
  code 2) instead of read as far as it goes. The numbers are read as little-endian floats from the
  bytes, without the unaligned loads UBSan reported. The ASCII reader takes words, not lines: any
  white space, blank lines, Windows line ends, keywords in any case and several solids in a file;
  an error names the line. A binary STL whose header holds `COLOR=` (Materialise Magics, and the
  header MeshLab writes) gives vertex colours when some triangle has a colour of its own: bit 15
  of the two bytes after a triangle set, then red in bits 0-4, green in 5-9 and blue in 10-14; the
  other triangles get the colour of the header. The bit order of VisCAM and SolidView, which is
  the opposite, is not read. The two sample STL files give the same bytes as before.
- A file of random bytes named `.stl` no longer keeps the program busy for ever. The cause was not
  the kd-tree: the search for the bounding sphere of a node (`vcg::Sphere3::CreateTight`) loops until
  its step is smaller than its radius, and with coordinates of the size of the floats (random bytes
  give them) the distances overflow, the radius becomes NaN, and the comparison is never true.
  The same model of 1e12 units across crashed in the cone of the normals instead, where the product
  of two edges overflows. A model whose bounding box is more than 1e9 units across (a city is 1e7
  millimetres) is now refused as invalid input, in every format, with its size in the message.
- PLY: the texture coordinates of a vertex are read from `u`, `v` or from `texture_u`, `texture_v`
  as well as from `s`, `t` (the first pair the file has). A colour without an `alpha` property is
  opaque, 255; the alpha was whatever the stack held (0 in the files built so far), so the
  vertices of `brighton.ply` have an alpha of 255 now, and nothing else of its output changes.
  The loader no longer takes the fractional part of the texture coordinates (`modf`): the builder
  does it for the coordinates of every format when it assigns the vertices to textures, so a
  coordinate beyond 1 gives the same model as before, and the PLY loader no longer rewrites the
  coordinate it keeps for all the faces that share a vertex.
- OBJ: `v x y z r g b` (exactly three numbers after the position) is the colour of the vertex,
  from 0 to 1 unless a component of that vertex is more than 1, then from 0 to 255; `v x y z w`
  (a weight) and any other count of numbers is not a colour. The colour of a vertex wins over the
  colour of the material (`Kd`) of the faces that use it; a vertex without one gets the colour of
  the material, or grey when the model has colours on some vertices and no material. An OBJ
  with the colours of MeshLab (`brighton_beach.obj` of the test data) used to lose them; its
  output holds them now (its geometry is as before).

## 0.1.0

First version. It is the multiresolution mesh builder of Nexus, imported from the DroneDB fork
of Nexus, with the fork's hardened loaders and its full-resolution WebP node textures, behind
the bm3d command line and library interface.

- Input: OBJ, PLY and STL.
- Output: the Nexus container, always corto-compressed. This is "format 0", and `bm3d --version`
  reports it as `bm3d 0.1.0 (format 0)`. Files of format 0 are readable by the Nexus tools; the
  `.bm3d` format starts with the first release that writes it.
- `--metadata <file.json>` is accepted and checked to be a JSON object, but the Nexus container
  has no place for it, so it is not written yet.
- VCGLib and corto are copied byte for byte from their upstream repositories at pinned commits
  (`scripts/sync-vcg.py`, `scripts/sync-corto.py`); the Nexus files are recorded with their
  hashes (`scripts/import-nexus.py`). See `PROVENANCE.md`.
- The same input gives the same file, byte for byte, whatever the run and the number of threads
  (`--threads 1` and `--threads 4` give the same bytes). The node that ends the node table was
  written as it lay in memory, so the same model built twice differed in a few bytes of that node;
  its fields now have fixed values (zero, and the radius -1 of an empty sphere). The file layout and
  everything else it holds are as before. The tests of the `determinism` label check it.
