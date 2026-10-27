# Changelog

All notable changes to bm3d are listed here. The version is in `VERSION`.

## Unreleased

The builder is reorganised; the files it writes are the same (the content hash of every reference
model is unchanged), and the command line and the API keep their meaning.

- **Sources in modules.** `src/format`, `src/build`, `src/io/mesh`, `src/io/image`, `src/compress`,
  `src/common` and `src/api`. The builder is split into `ModelDag` and `DagOps` (the graph of nodes
  and what is done to it once the levels are made), `NodeTextureBaker`, `NodeTextureStore` and
  `TextureEncoderPool` (the textures of the nodes, encoded on several threads and written in order),
  `LevelBuilder`, `HierarchyBuilder`, `ModelSaver`, `ModelBuilder` and `BuildPipeline`, which holds
  the retries as a `FallbackPolicy`. The classes take their settings in their constructors.
- **Loaders and image decoders are found through registries.** `MeshLoaderRegistry` maps the
  extension of a file to its loader (OBJ, PLY, STL); `ImageDecoderRegistry` asks the decoders in
  order and each recognises its format by the first bytes of the file; `ImageEncoder` is the
  interface of the WebP (node textures) and JPEG (tiles of the texture pyramid) encoders.
- **Errors are told apart by type.** The scratch layer throws `ScratchError`, so that a scratch file
  that cannot be created or written is a failed build, and the reading of the input turns the
  other errors of the loaders into `InputError`; the API no longer looks for the names of scratch
  files in the message. The C string that corto throws is turned into an `Error` where corto is
  called.
- **`BuildOptions::logger`.** The warnings of a build (bytes after the triangles of a binary STL,
  textures that could not be packed, nodes made smaller) are given to this function, without the
  `Warning: ` prefix. The library writes nothing to the standard streams; the program prints them
  to standard error as before, with the same text.

## 1.0.0

The first release that writes the `.bm3d` format, version 1 (`docs/FORMAT.md`), and the first that
ships the viewer, the npm package `bm3d-viewer` (a tarball attached to the release). The headline
changes since 0.2.0: the new file format with its metadata block and content hash; the viewer, which
reads it with HTTP Range requests and decodes it in a worker with corto as WebAssembly; every Nexus
name in the code replaced by a bm3d one; the same bytes on every platform and the same content hash
with any number of threads (golden files checked on Linux and Windows); a simplification that keeps
a node on its model and the precision of a small model; and release archives for Linux and Windows
with the licences of what they link. The file of a model is 0.4 % (`TexturedModel`) to 0.9 % (`OfficePlan`) smaller than
the Nexus container of the same input.

- **`scripts/compare.py` compares without the order.** The reference models were built with the order
  of equal keys of the library of the reference program, and bm3d has an order of its own, so the same
  model has its nodes cut, its levels filled and its texture pieces placed a little differently.
  `--order-independent` (the default when the candidate is a `.bm3d` file and every reference a
  format 0 file; `--exact-order` asks for the old comparison) takes what that moves little: the
  counts of nodes, patches, textures and leaves within 1 % and the levels equal, the vertices and
  faces within 1 %, the sums of the errors within 3 % (each level 10 %), the radius of the sphere
  and the error of the root within 5 % and the centre within 30 % of the radius, the texels within
  2 % in all and 25 % in each level, the size within 3 %, and the textures by their colours without
  their places (the PSNR of the sorted pixels of the nodes matched by place, 40 dB for 98 % of
  them). The tolerances are the ones the six reference models needed (the table in the script
  gives the largest deviation measured for each); the bounding sphere of the header is made by
  adding the vertices of the root one by one, so its centre moved by up to 22 % of the radius
  between the two orders. All six models pass with `bm3d --threads 8` and `--threads 1` but for the
  file size of `brighton_beach.obj`, which keeps the colours of its vertices.
- **A model of a single node keeps the precision of its input.** The coordinates are quantised to one
  step for the file, a tenth of half the smallest of the error of the root and the errors of the
  leaves. The error of a root is replaced by its radius, and in a model of a single node the node is
  the root and the leaf, so the step was a twentieth of the radius: for the sphere of 5808 triangles
  of the golden files, rounded to 2^-4 (0.0625), about the length of an edge (0.07), and 264 of its
  triangles came out flipped or flat. The step of a leaf that is a root now comes from the length a
  tenth of the edges of its triangles do not exceed (not the mean, which a chamfer along a long edge
  does not move), so that a part with small features keeps them; the leaves of a larger model keep
  the error they had. The sphere is now 2^-8, with no triangle turned by more than 25 degrees from
  the surface and no vertex off by more than 0.0068 (0.1 mean edge); the chamfers (0.07) of a block
  keep their corners within 0.007. Sizes: the sphere 12 982 -> 11 022 bytes (the estimated normals
  are better predicted from better positions), `brighton_beach.obj` 24 357 -> 25 957 (+6.6 %); the
  other reference models are of several nodes and do not change. The golden hashes of the four
  models of a single node (`wave`, `degenerate`, `book`, `sphere`), and with them the viewer's
  sphere, change; the one of the surface of several nodes (`grid`) changes with the placement of
  the vertices (the entry below).
- **A simplified node stays on the model.** The vertex that replaces the two ends of an edge was put
  at the minimum of the sum of their quadrics even where the planes of the quadric are nearly parallel
  (a flat or gently curved part with the noise of the input), where the minimum is far from the edge
  on a part of the plane that is not there. On `OfficePlan` 30 900 collapses of edges a few thousandths
  long put the vertex more than twice the length of the edge outside the box of its ends (one 1.5 away
  from an edge 0.002 long), and a node had a vertex at z = -6.4 where the input does not go under -2.5.
  The minimum is now used only when its position lies in the box of the two ends made larger by the
  length of the edge; otherwise the vertex takes the end or the middle where the quadric is the
  smallest (VCGLib patch `0011`). The texture coordinates are not checked: the minimum of a texture
  chart is often far from its ends (an edge across a seam, a wall), and the end or the middle that
  replaced it made the pieces of the node textures long and thin, with 4.6 times the texels in the
  coarser levels of one of the reference models. The plain collapse also used a minimum that was
  reported as not found without being set. After the change the vertices of every node of `OfficePlan`
  are within 0.07 of the box of the input (0.4 % of its diagonal; z from -2.50 to 6.25, the input
  -2.50 to 6.24).
- **The same bytes on every platform.** Programs built with libstdc++ and with the library of MSVC
  wrote different files for the same input, because several orders were left to the standard
  library where keys are equal. Each now has an order of its own: the triangles of a node are sorted
  by the node of their patch with a stable sort (they keep their order, which the simplification of
  a mesh without texture then reads); the border vertices whose normals are made the same are
  ordered by point, node and index; the edge collapses of equal cost are done in the order they were
  made (VCGLib patch `0008`); pieces of texture of the same size are packed in their order (`0009`);
  corto orders symbols of equal probability by symbol and the edges of the same vertices by face and
  side (corto patch `0006`). And nothing the builder writes comes any more from a function of the
  mathematical library that is not exactly rounded: the rings of the normal cone are found with
  constant cosines and sines (not `acos` and `cos`), the exponent of the coordinate step is read from
  the bits of the float (not `log2`), the levels a texture needs are counted by multiplying by 4
  (not with `log`), the quadric scale is `1e8 / diag^6` by multiplications (not `pow`; `0010`).
  `tests/golden/hashes.txt` has one hash for each model, checked on Linux and on Windows, and four
  more golden models: a textured surface of thousands of pieces of texture of the same size, 40
  triangles on one edge, a textured terrain and a sphere with vertex colours (the last two are the
  fixtures of the viewer). Unit tests give each order many equal keys and fail with the order of
  the library. Built with the input of every one of these sorts and heaps reversed (another library's
  order of equal keys), four of the reference models and three of the golden inputs give the same
  bytes; all seven differed before. The output changes: the meshes
  without texture are simplified in another order, and the pieces of the node textures sit at other
  places (the same pieces: the textures of a node have the same size and, sorted, the same pixels).
- **The checks of section 11, stated and the same in every reader.** A sphere of the header that
  is not finite is the new error `bad sphere`, and a node whose `error`, sphere or `tightRadius` is
  not finite is `bad index`. The rules of `bad index` are a numbered list, checked rule by rule, each
  over the whole table before the next, so that every reader reports the same rule and the same
  node or patch; the sink is checked byte by byte (a `-0` in it is an error). `Bm3dReader`,
  `scripts/dump.py` and the viewer's reader follow it, with tests in each (`scripts/tests/` for
  the Python reader, run by the CI on the fixtures and on the golden files).
- Size of the new container against the Nexus one, measured with `--threads 8` on the file of bm3d
  0.2.0 as of the commit that renamed the code (`dd62d72`, the last program that wrote format 0)
  and the file of this version: the benchmark model (`TexturedModel`) 358 059 008 -> 356 635 232
  bytes (-0.40 %), `OfficePlan` 761 587 200 -> 754 451 498 (-0.94 %), about 288 bytes less for each
  node and texture (`docs/FORMAT.md`, section 14). The figures compare the two files: the builder
  also changed between the two programs (the entries above).
- **Viewer.** The default `decoderPath` is found from `import.meta.url` when a decoder starts, not
  with `new URL('.', import.meta.url)`, which webpack takes for a file to bundle and stops on
  ("Can't resolve '.'"); a test bundles the package with rollup and the rule of those bundlers.
- The fixtures of the viewer's tests are written by the command line (`scripts/web-fixtures.py`:
  two of the golden files, a textured terrain and a sphere with vertex colours) instead of being
  re-packed from format 0 files, and a test checks their content hashes against
  `tests/golden/hashes.txt`.
- The Web workflow builds the command line and writes the golden files; the Python reader, the
  tests in node and the render in Chromium read and draw them. The CI workflows share the set-up of
  vcpkg and its binary cache (`.github/actions/vcpkg` and `vcpkg-save`).

- **The output is the `.bm3d` format, version 1** (`docs/FORMAT.md`), and `formatVersion()` and
  `bm3d --version` say 1. The Nexus container (format 0) is no longer written, and `bm3d` reads
  neither: its magic number is `Nxs ` and this one is `BM3D`. The differences that matter: offsets
  and lengths are `u64` byte counts and every payload and texture starts at a multiple of 8 (not
  256), a texture has its exact length, there is no sentinel row in the texture table, the colour
  is `UNSIGNED_BYTE`, the header says `CORTO_COMPRESSED` and `TEXTURE_WEBP`, and the file ends with
  a metadata block (one JSON object: `generator`, `buildOptions`, `source` with the name, vertex
  and face counts and bounds of the input, and the members given with `--metadata`).
- **Content identity.** The content hash (SHA-256 of every byte of the file but the metadata
  block, with its length field read as zero; the SHA-256 is `src/format/sha256.cpp`, as no
  library of the build has one) is what the determinism tests, the new golden tests and any
  refactor compare. The metadata holds the number of threads, the name of the input and the
  version of bm3d, so two files of the same model differ in their bytes and not in their hash.
- `--metadata <file.json>` is stored. A top-level key `generator`, `buildOptions`, `source`, one
  that starts with `bm3d`, or one given twice, and a block over 1 MiB, are a wrong command line (exit
  code 64) refused before the build starts. The library has the same in `BuildOptions::metadata`
  (refused with `ErrorKind::InvalidArgument`) and `metadataError()`.
- **The counts of a node are the encoder's.** corto drops the triangles with two equal vertex
  indexes and then the vertices only they used, and moves the end of every group to match. The node
  record and the patches of the new format are written from the encoder after it has run (before,
  from the counts the builder gave it, which a decoder then found to differ), and a node whose
  triangles are all dropped is an error: the format has no nodes of points. The cone of a node is
  four zeros when its axis is not longer than 0.001.
- `src/format/` is the one place that knows the file: the records with their sizes asserted and
  their fields written and read one by one in little endian (`bm3d_format`), `Bm3dWriter` (the
  header, the index, the payloads, the textures and the metadata, written under `<output>.part` and
  renamed), `Bm3dReader` (every check of section 11 of the specification in its order, lazy access
  to nodes and textures, the metadata and the content hash), the metadata block (`metadata`),
  the JSON checker (moved there from `src/api`) and the SHA-256. The builder saves through the
  writer, node by node, with no intermediate file: the uncompressed container that the builder
  wrote for the compression to read back (`<output>.tmp.bm3d`) is gone with the format 0 writer,
  and so are `dag.h`, `signature.h`, `file.h`, `stdfile`, `reader` and `traversal` of `src/common`.
  The tests read the files with `Bm3dReader` (`tests/support/bm3d_file`).
- New tests: unit tests of the records (byte layout, sizes), the writer (the worked example of the
  specification byte for byte, the round trip), the reader (every error of section 11 with a
  hand-made bad file, in order; the version rule; the metadata; the content hash), SHA-256 against
  the standard's vectors, the metadata block, the compressor and the cone; integration tests of the
  output (every node decodes to its record, the metadata, the refused keys); CLI tests of
  `--metadata`; and the `golden` label: six small models built by the program whose content hashes
  are in `tests/golden/hashes.txt`, written to `BM3D_GOLDEN_DIR` (by default `<build>/golden`; the
  target `bm3d_golden_fixtures` writes them alone) for the other readers of the format. The
  determinism tests compare content hashes, and bytes between builds with the same thread count.
- `scripts/dump.py` reads a `.bm3d` file with no code of `src/format/` (every check of section 11,
  the headers of the corto streams against the index), prints it, and `dump.py compare OLD.nxz
  NEW.bm3d` compares a format 0 file with the file built from the same input node by node: the
  index, every corto stream and every texture. `scripts/compare.py` reads either format
  (`modelfile.py` is what the two readers share; `format0.py` is the reader of the Nexus
  container, kept for the comparisons).

- Every Nexus name in the code is a bm3d name; the output does not change (the byte gate is the
  same on all eight reference models). The namespace `nx` is `bm3d::detail` (the public API stays
  in `bm3d`), `NexusBuilder` is `Builder`, `NexusData` is `Reader`, `NexusFile` is `File`,
  `Extractor` is `Compressor`; `src/nxsbuild` is `src/build` and `src/nxsedit` is `src/compress`,
  with the files renamed to match (`nexusbuilder` to `builder`, `nexusdata` to `reader`,
  `extractor` to `compressor`, `nexusfile` and `stdnexusfile` to `file` and `stdfile`); the scratch
  files of a build are named `bm3d_cache_*` and `<output>.tmp.bm3d`. The licence headers of the
  imported files, the attribution to Nexus and the bytes of the container (format 0) are unchanged.
- `scripts/import-nexus.py` keeps tracing the imported files after the move: `src/NEXUS_SOURCE`
  has a `moved` section (upstream path to current path), `--check` reports a moved file as
  renamed and an edited one as modified instead of removed, and `--move OLD NEW` records a move.
  `PROVENANCE.md` has a table of the renamed files. `scripts/nxzread.py` is `scripts/format0.py`.
- `scripts/check-spdx.py` skips build directories only at the top of the repository, so that
  `src/build/` is checked.

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
