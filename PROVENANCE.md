# Provenance

Where every part of this repository comes from, and how to check or repeat the import. People are
named by their commit names.

## Sources

| Part | Upstream | Fork or pin used here |
|---|---|---|
| Nexus builder | https://github.com/cnr-isti-vclab/nexus, `master` at `f29e8fd1182b2a1c0c512add99c7edfc8e146680` | https://github.com/DroneDB/nexus at `847f024bf7cc0beb078868749bc7669748c0aa3d` |
| VCGLib | https://github.com/cnr-isti-vclab/vcglib, `devel`, commit `03484542632225739ca96b28afcde38267561323` | the same upstream commit plus 10 patches (`third_party/vcg-patches/`) |
| corto | https://github.com/cnr-isti-vclab/corto, `master`, commit `d240e69306a39417c098475d7002eb16b7264248` | the same upstream commit plus 6 patches (`third_party/corto-patches/`) |
| API and command line wrapper | none | https://github.com/DroneDB/libnexus at `18acb8c161fd85d1eb614d8be0f8b4dae5fe16f1` (`nxs.cpp`, `nxs.h`, `ddb_nexus_main.cpp`) |
| Web viewer (added with the release that ships `web/`) | the JavaScript in `html/js` of Nexus, commit `d805432` | the same, with fixes recorded in `web/` |

### The Nexus fork

`DroneDB/nexus` is Nexus plus 49 commits on top of upstream `f29e8fd`: 29 by HeDo, 14 by Piero
Toffanin and 6 by Luca Di Leo. In short, they remove the Qt dependency from the builder, read and
write textures without Qt, harden the OBJ, PLY and STL loaders, make long Windows paths and
temporary files work, check nodes, patches, textures and streams while building, and keep the
resolution of the model's textures as WebP node textures. The DroneDB fork of libnexus, which
wrapped the builder for DroneDB, has its own history by the same three authors (Piero Toffanin
started it in 2022; the 2026 changes are by HeDo). Its wrapper was relicensed from AGPL-3.0 to
GPL-3.0 (version 3 only) in October 2026, before it was used here.

### The corto patches

The 6 patches in `third_party/corto-patches/` are ours (HeDo, `git format-patch d240e69..`). The
first 5 were made to fix what AddressSanitizer and UndefinedBehaviorSanitizer reported in the encoder when the
tests and the mutation loop of `tests/robustness` ran with no suppression. They touch
`include/corto/normal_attribute.h` (`toOcta` of a float vector returns (0, 0) for a vector of
length zero, as the integer form does), `include/corto/cstream.h` (`push` of an empty array copies
nothing; `write<T>` stores with `memcpy`, not through an unaligned pointer), `src/bitstream.cpp`
(`read` masks with an unsigned word; `reserve` frees the buffer it replaces and allocates nothing
for zero words, which was a leak of one allocation per node of fewer than 4 vertices). Only the
first can change the bytes written, and only in the nodes with a vertex whose normals sum to zero
(a vertex of degenerate triangles only): the encoder predicted that vertex's normal from
`INT_MIN` instead of from (0, 0), so its stored difference was larger; the normals read back are
the same. The sixth (`0006`) gives two sorts of the encoder an order for equal keys, which they
left to the standard library (libstdc++ and MSVC order them differently, so the streams differed
between platforms): `src/tunstall.cpp` orders the symbols of equal probability by symbol, and
`src/encoder.cpp` the topology edges of the same two vertices by face and side. The decoder reads
the Tunstall table from the stream, so it is not touched.

### The VCGLib patches

The 4 commits of the DroneDB fork of VCGLib (https://github.com/DroneDB/vcglib, by HeDo) on top
of upstream `0348454` touch only `wrap/ply/plylib.cpp` and `wrap/ply/plylib.h`:
`57ef561` opens UTF-8 file names on Windows, `1d4aaa8` bounds the length of a list read into
memory the caller allocated, `3306f50` keeps `interpret_texture_name` inside its buffer and adds a
`std::string` form, and `42b35dd` reads the length of a binary list as the type the header
declares. `third_party/vcg-patches/` holds them as made by `git format-patch 0348454..42b35dd`,
with their authorship. Two more patches are ours (HeDo, `0005` and `0006`), found by the mutation
loop of `tests/robustness` run under the sanitizers; both are in `PlyFile::OpenRead`: the check of
the count after `element <name>` tested the name instead of the count, so a header that ended inside
that line crashed, and the count is now read with `strtoll` and refused when it is negative, beyond
an int or not a number (`atoi` took a minus sign and `PlyElement` asserts it away).

A seventh patch (HeDo, `0007`) takes Eigen out of the headers bm3d reads, which the simplification
of a mesh without texture was the only user of: `math/quadric.h` solves its 3x3 system with
`SolveFullPivot3x3`, plain code in the order of operations of Eigen's `FullPivLU::solve`, so the
vertex it places is the same to the last bit (checked against Eigen on 20 million quadrics, and by
the byte gate on all the reference models). Other code of those headers that named Eigen and that
a triangle mesh pipeline does not call is removed: `Quadric::MinimumClosestToPoint` and the
`SVDPlacement` option, the `Inverse` of `Matrix33` and `Matrix44` (the determinant of `Matrix44` is
a plain expansion), the `Allocator` functions that take an Eigen matrix, the polygon functions that
fit a plane, take the principal components or align a template, `Stat::ComputeMeshVolume` and
`Clean::SelectFoldedFaceFromOneRingFaces`. `fitting3.h`, `point_matching.h` and `inertia.h` are not
read any more and are no longer taken (93 files of VCGLib instead of 96).

Three more (HeDo, `0008` to `0010`) make the output the same with every standard library and
mathematical library: `complex/algorithms/local_optimization.h` numbers each element of the heap
of the local optimization when it is made and does the operations of equal priority in that order
(the heap functions of the libraries leave equal elements in orders of their own);
`space/rect_packer.h` orders rectangles of the same size by their index; and
`complex/algorithms/local_optimization/tri_edge_collapse_quadric.h` computes the scale of the
quadrics, `1e8 / diag^6`, with multiplications instead of `pow`, whose last bit differs between
libraries.

## Files taken and how to repeat it

| Directory | List and manifest | Script |
|---|---|---|
| `third_party/vcg/` (only the VCGLib files the build reads) | `FILES`, `VCG_SOURCE` | `scripts/sync-vcg.py` |
| `third_party/corto/` (the 8 compiled sources and the headers they include) | `FILES`, `CORTO_SOURCE` | `scripts/sync-corto.py` |
| `src/common`, `src/build`, `src/compress` (the files were imported into `src/nxsbuild`, `src/nxsedit` and `src/nxszip`; see Renamed files) | `NEXUS_SOURCE` | `scripts/import-nexus.py` |

The lists come from `scripts/closure.py`, which reads the compiler's dependency files (GCC and
Clang) and the `/showIncludes` output of MSVC and keeps the files that lie under the three source
trees. Run it again against this repository's own build to see which imported files are still
used.

- The files are read from git objects (`git show <commit>:<path>`), never from a working tree,
  and `.gitattributes` marks them `-text`, so they are byte for byte the upstream files. Several
  upstream files have CRLF line endings, and they keep them.
- `scripts/sync-vcg.py --check` and `scripts/sync-corto.py --check` verify that the committed files
  are exactly the listed ones and that their SHA-256 hashes match the manifest. With
  `--source <clone of the upstream repository>` they also derive the files again from the pinned
  commit (and, for VCGLib, the patches) and compare. To move to another commit, change the commit
  in the script, edit `FILES` if the set of used files changed, and run the script with
  `--source`.
- The Nexus files are ours to edit and to move after the import. `scripts/import-nexus.py --check`
  lists the files that have been renamed, changed or gone since, with the hashes of the import as
  the reference; with `--source <clone of the fork>` it also checks that the recorded hashes are
  those of the pinned commit.

## Renamed files

Every Nexus name in the code is a bm3d name. The output did not change (the byte gate on the eight
reference models was the same before and after). `src/NEXUS_SOURCE` still holds the hash of every
file as imported, under its upstream path, and a `moved` section that maps each upstream path to
the path the file has now, so that every imported file can be traced; `scripts/import-nexus.py
--check` reports the moved files as renamed and the edited ones as modified (all of them are
modified, since the namespace and the type names changed).

| Upstream path (in `src/`) | Now |
|---|---|
| `nxsbuild/` | `src/build/`; `nexusbuilder.{h,cpp}` is `builder.{h,cpp}` |
| `nxsedit/extractor.{h,cpp}` | `src/compress/compressor.{h,cpp}` |
| `common/nexusdata.{h,cpp}`, `common/nexusfile.h`, `common/stdnexusfile.{h,cpp}`, `common/dag.h`, `common/signature.h`, `common/traversal.{h,cpp}` | removed (see "The container" below) |
| `nxszip/` | removed (see below) |
| the other files of `common/` | the same path |

Names in the code: the namespace `nx` is `bm3d::detail` (the classes `Builder` and `Compressor`
live there too; the public API of `include/bm3d/bm3d.h` is in `bm3d`); `NexusBuilder` is
`Builder` and `Extractor` is `NodeCompressor`. The classes of the mesh, the kd-tree, the loaders
and the image code that were in the global namespace stay there until the sources are split into
modules. Outside `src/`: `tests/support/nexus_file.{h,cpp}` is `bm3d_file.{h,cpp}` and
`scripts/nxzread.py` is `scripts/format0.py`. The temporary files of a build are `bm3d_cache_*` and
`<output>.part`. The licence headers of the imported files are unchanged.

### The container

bm3d writes the `.bm3d` format, version 1 (`docs/FORMAT.md`), and no longer the container of Nexus
(format 0). The files that were the container are replaced by `src/format/`, which is written for
bm3d (GPL-3.0-or-later, no Nexus code in it):

| Nexus (upstream `src/`) | Now |
|---|---|
| `common/nexusdata.{h,cpp}` (`NexusData`: header, tables, `loadRam`, `dropRam`, `intersects`) | `src/format/bm3d_reader.{h,cpp}`: `Bm3dReader` reads and checks the new header and tables; nothing renders or picks, so the viewer-side functions are gone |
| `common/dag.h` (`Header`, `Node`, `Patch`, `Texture`, `countDagRoots`) | `src/format/bm3d_format.{h,cpp}` (the records of the file, `countRoots`) and `src/build/node.h` (`Node`, the node while it is being built) |
| `common/signature.h` (`Signature`, `Attribute`, `Element`) | `VertexLayout` and the attribute slots of `src/format/bm3d_format.h` |
| `common/nexusfile.h`, `common/stdnexusfile.{h,cpp}` (`NexusFile`, `StdNexusFile`) | `ByteSource` of `src/format/bm3d_reader.h`; the writer uses `OutputFile` of `src/common/textfile.h` |
| `common/traversal.{h,cpp}` (the selection of nodes by size, level, error or triangles that `Extractor` used) | removed; the program never selected a part of a model |
| `nxsedit/extractor.{h,cpp}` (`Extractor::save`, which wrote format 0 and compressed the nodes) | `src/compress/compressor.{h,cpp}`: only the part that encodes one node with corto (`NodeCompressor`) and the quantisation step of the coordinates; `Builder::save` writes through `Bm3dWriter` |

The builder no longer writes an uncompressed container for the compression to read back: it encodes
each node as it saves and passes it to the writer. The encoding itself (corto, with the same
settings and the same step of the coordinates) is the Nexus code, so the streams are byte for byte
those of the format 0 files (`scripts/dump.py compare` shows it on the reference models).

## Changes made to the imported files

The files of Nexus that were the container (`reader`, `file`, `stdfile`, `dag`, `signature`,
`traversal`) are gone, replaced as "The container" above says; the entries below that name them
describe what was done to them before.

- `src/build/builder.cpp`: the vertex cache optimisation is removed. Its function
  `optimizeNode` begins with `return;` in the imported code, so the optimisation never ran
  upstream, and corto reorders the faces while compressing anyway. The include of
  `vertex_cache_optimizer.h` and the dead call go with it. Output does not change.
- `third_party/vcg/wrap/ply/plylib.{cpp,h}`: the patches `0001` to `0006` above; and `third_party/vcg/vcg/` `math/quadric.h`, `math/matrix33.h`, `math/matrix44.h`, `space/polygon3.h`,
  `complex/allocate.h`, `complex/algorithms/` `stat.h`, `clean.h` and `local_optimization/tri_edge_collapse_quadric.h`: the patch `0007`;
  `complex/algorithms/local_optimization.h`, `space/rect_packer.h` and
  `complex/algorithms/local_optimization/tri_edge_collapse_quadric.h`: the patches `0008` to `0010`.
- `third_party/corto/` `include/corto/normal_attribute.h`, `include/corto/cstream.h`,
  `src/bitstream.cpp`: the first 5 corto patches above; `src/tunstall.cpp`, `src/encoder.cpp`: the
  sixth.
- `src/build/mesh.{h,cpp}`, `src/build/tmesh.{h,cpp}`, `src/build/nodepatches.h`,
  `src/build/builder.h`: the triangles of a node are sorted into patches by `sortFacesIntoPatches`
  (one copy of what both meshes did), with a stable sort instead of `std::sort` on the node alone,
  and the faces lose the `operator<` that sort used; the border vertices (`NVertex`) are ordered by
  point, node and index. Equal keys were left in the order of the standard library, which is not
  the same with libstdc++ and MSVC.
- `src/common/cone.{h,cpp}`: the ring of a normal (`coneRing`) is found by comparing its cosine with
  constant cosines, and the axis is scaled by a constant sine, where `acos` and `cos` were called;
  both `AddNormals` share it. `src/compress/compressor.{h,cpp}` and `src/build/builder.cpp`: the
  exponent of the coordinate step is read from the bits of the float (`coordinateExponent`) instead
  of `log2`, the quantisation step is `ldexp` instead of `pow`, and the levels a texture needs are
  counted by multiplying by 4 instead of with `log`. The last bit of those functions is not the
  same in every mathematical library.
- `src/build/node.h` (`dag.h` of Nexus): every field of `Node` has a value from the start, and the
  empty sphere has its centre at the origin (`emptySphere()`). The sink node that ended the node
  table set only `first_patch`, so the file got the rest of it as it lay on the stack, and two
  builds of the same input differed in a few bytes. The sink of the new format is written by the
  writer: all zeros but its first patch.
- `src/common/cone.{h,cpp}`: `Cone3s::n` starts at zero, for the same reason (the cone of the sink
  node), and `Import` writes four zeros when the axis is not longer than 0.001 (the specification
  says so; a cone has an axis of at least 1 or none, so no cone changes).
- `src/build/builder.cpp`: the bounding sphere of the header starts from `emptySphere()`
  instead of vcg's empty sphere, whose centre is not set.
- `src/build/mesh.{h,cpp}`, `src/build/tmesh.{h,cpp}`: the debug export of a mesh to a PLY
  file (`savePly`, `savePlyTex`) and the include of VCGLib's `export.h` that served it are removed;
  nothing called them.
- `src/compress/compressor.{h,cpp}`: `savePly`, `saveStl`, `saveUnifiedPly` and `countElements`
  (which only they used) are removed, with the small PLY structures of `saveUnifiedPly`. The
  program never wrote an extracted mesh.
- `src/nxszip/*` (`abitstream.cpp`, `atunstall.cpp`, `meshcoder.{h,cpp}`, `meshdecoder.{h,cpp}`,
  `bitstream.h`, `cstream.h`, `fpu_precision.h`, `tunstall.h`, `zpoint.h`): removed, the whole
  directory. It held MECO, the compression Nexus used before corto, which the program never
  selects; its two call sites (`compressor.cpp`, `reader.cpp`), the `MECO` name in
  `src/common/signature.h` and the extractor's `error_factor`, which only MECO read, go with it.
  The flag values of the container are unchanged.
- `src/build/tsloader.{h,cpp}`, `tsploader.{h,cpp}`, `colormap.{h,cpp}`, `vcgloader.h`,
  `vcgloadermesh.h`: removed. They read Nexus' `.ts` and `.tsp` files, coloured a `.ts` by a
  colormap, and fell back on VCGLib's importers for any other extension; bm3d reads OBJ, PLY and
  STL only.
- `src/build/meshstream.{h,cpp}`: the loader is chosen by the extension in any case, and an
  unknown one is an error that names the formats read; the colormap of the stream goes.
- `src/build/tmesh.{h,cpp}`: `loadPly` (uncalled) and the include of VCGLib's `import.h` that
  served it are removed.
- Paths the program never takes, and options that nothing set, are removed from `src/build/`
  `kdtree.{h,cpp}`, `mesh.{h,cpp}`, `tmesh.{h,cpp}`, `meshloader.{h,cpp}`, `meshstream.{h,cpp}`,
  `builder.{h,cpp}`, `objloader.{h,cpp}`, `plyloader.{h,cpp}`, `stlloader.{h,cpp}`,
  `trianglesoup.h`, from `src/compress/compressor.cpp` and from `src/common/` `reader.{h,cpp}`,
  `file.h`, `stdfile.{h,cpp}`, `signature.h`:
  - point clouds: `KDTreeCloud`, `StreamCloud`, `Stream::getBox`, `MeshLoader::getVertices` and its
    implementations, `createCloudLevel` and the cloud branch of `createLevel` (which was a choice
    between the two and is gone; `Builder::create` takes the triangle kd-tree and stream),
    `Mesh::load(Cloud)`, `getVertices`, `simplifyCloud` and `averageDistance` (and `TMesh`'s), the
    `Splat` and `Cloud` types, and the shuffle of the points of a compressed cloud in
    `Reader::loadRam`;
  - deepzoom: the folder of files in `Builder::save` and `Compressor::save`, `loadDZNode`,
    `dropDZNode`, `loadDZTex`, `dropDZTex` and `readDZFile` of `File` and `StdFile`, and
    `Signature::isDeepzoom`. The flag value stays as the enum entry `DEEPZOOM`;
  - `Builder` options that nothing set: `useNodeTex` (false made the container hold the
    original texture files, named in `images`), `createPowTwoTex` (and `nextPowerOf2`),
    `deepzoom`, `max_node_triangles`, the unused pixel counters, and `texFormat` with its enum
    (the JPEG node textures; `encodeJpeg` stays, the texture tiles that spill to disk use it) and
    the constructor taking a `Signature`, which nothing called;
  - `origin`, `scale`, `box` and the vertex quantization of `MeshLoader` and `Stream`, used by the
    loaders and `Stream::getBox`. They kept their defaults (no offset, scale 1, no quantization),
    for which the arithmetic was exact, so the vertices are unchanged. With the scale the ASCII STL
    loader loses a line that multiplied x by the z scale instead of z by it (`stlloader.cpp`).
  Output does not change.
- `third_party/vcg/`: 27 of the 123 VCGLib files are no longer taken, 96 are. The list was made
  again with `scripts/closure.py` from the dependency files of a full build of this repository
  with GCC (the program, its tests and corto), and `scripts/sync-vcg.py` imported it. The files
  are the VCGLib importers and exporters of `wrap/io_trimesh` (with their `io_*` and `precision.h`
  helpers), the kd-tree and priority queue of `vcg/space/index`, and the algorithms
  `bitquad_support`, `create/platonic`, `refine`, `smooth`, `vertex_interpolation`,
  `update/halfedge_topology`, `update/position`, `update/quality` and `connectors/halfedge_pos`,
  which only the removed code included. Dependency files do not show a header that an `#include`
  reads under `_MSC_VER` or `_WIN32` and GCC skips, so a dropped header is kept if a kept file
  includes it inside such a branch; none is (every dropped name was searched for in an `#include`
  of the kept VCGLib, corto and bm3d files), and the MSVC step of CI lists any header its build
  reads that `FILES` lacks. corto lost no file: its 21 files are all still read. The imported files
  of VCGLib are unchanged byte for byte.
- `src/build/plyloader.{h,cpp}`, `src/build/objloader.cpp`, `src/build/stlloader.{h,cpp}`:
  a vertex whose coordinate is NaN, an infinity or beyond the range of a float is refused with an
  error that names the file and the vertex (`requireFinite` of the new `src/build/finite.h`).
  The ASCII STL loader reads the numbers with `text::parseDouble` instead of `sscanf`, which
  accepted `nan` and `inf`. The loaders keep the name of their file for the message.
  `PlyLoader::cacheVertices` starts its `PlyVertex` from zero (it was uninitialised, and its alpha
  was copied into the colour of a PLY without one).
- `src/build/stlloader.{h,cpp}`: rewritten. The kind of file (binary or ASCII) comes from its
  size and its first words, the count is checked against the size, the records are read with
  explicit little-endian conversions, the ASCII parser reads words and names the line of an error,
  and Materialise colours are read. The triangles are cleared before they are filled (the
  buffer of the stream was uninitialised). `src/common/textparse.h`: `equalsNoCase` has a form
  that takes a pointer and a length.
- `src/build/plyloader.cpp`: `u`/`v` and `texture_u`/`texture_v` are read as texture coordinates
  besides `s`/`t`; the vertex that is read starts opaque (alpha 255) so a colour without alpha is
  opaque; the `modf` of the texture coordinates is removed (`Builder` applies `fmod` to the
  coordinates of every format). `src/build/objloader.{h,cpp}`: `v x y z r g b` is a vertex colour
  that wins over the colour of the material.

- `src/build/finite.h`, `plyloader.cpp`, `objloader.cpp`: a texture coordinate that is a NaN, an
  infinity or beyond 2^24 in magnitude (above which a float has no fractional part to wrap) is
  invalid input in the PLY (per vertex or per face) and the OBJ, with the file and the vertex or
  face in the message (`requireUsableTexCoords`, `isUsableTexCoord`). The messages of the PLY
  loader name the file (`face 3 of 9 in ply file x.ply has ...`), the failure of the header says
  why (`headerProblem`: for example a header line longer than 511 characters, which plylib cuts
  in two), and the OBJ face errors end with `(obj file x.obj)`.
- `src/common/textparse.h`: `parseInteger` refuses the 19th digit before it multiplies, as it
  claimed to (the multiplication overflowed an `int64_t` for a number of 19 digits or more).
- `src/build/builder.cpp`: `textureBoundLevels` returns at most 64 levels; the number of
  levels of a model whose texture density was an infinity or a NaN was converted to an `int`.

- `src/build/imageio.{h,cpp}`: PNG is read by stb (`stbi_load_from_memory`, and the 16 bit
  variant scaled with rounding) instead of libpng, with the old treatment of a gray PNG with a
  transparent gray kept, and the TIFF reader is compiled only when `BM3D_WITH_TIFF` is defined
  (otherwise a TIFF is refused by name). Output does not change.

Edits made after the import appear in `scripts/import-nexus.py --check`; larger ones are listed
here as they happen.

## Licence headers

The headers below were checked file by file, and those of the upstream files against upstream
Nexus at `f29e8fd`.

### Files added by the DroneDB fork of Nexus (Digipa headers)

`src/common/` `error.h`, `numeric.h`, `mappedfile.{h,cpp}`, `pathutil.h`, `scratchfile.{h,cpp}`,
`textfile.{h,cpp}`, `textparse.h` and `src/build/` `image.{h,cpp}`, `imageio.{h,cpp}`,
`imagejpeg.cpp` were written for the Qt-free fork and are not in upstream Nexus. Their header
reads "Copyright (C) 2026 Digipa" and licenses them under the GNU General Public License,
version 2 or (at your option) any later version, the licence of Nexus.

### Files derived from libnexus (GPL 3 only)

`src/api/bm3d.cpp` derives from `nxs.cpp` of the DroneDB fork of libnexus (by Piero Toffanin,
Luca Di Leo and HeDo) and `cli/main.cpp` from its `ddb_nexus_main.cpp` (by HeDo). Those files are
licensed under the GNU General Public License, version 3, without the "or any later version"
option, and the two files derived from them keep that licence (`SPDX-License-Identifier:
GPL-3.0-only`) and the copyright line of `nxs.cpp`. Every other file written for bm3d is
GPL-3.0-or-later; the project as a whole is distributed under version 3 of the GPL (`LICENSE`).

### Files without a licence header

- `vertex_cache_optimizer.h` (upstream `nxsbuild`) had no notice and has no known author; it came with the
  2017 import of Nexus from Subversion. It was never imported and never was in the history of
  this repository.
- `src/build/` `texpyramid.{h,cpp}` and `stlloader.{h,cpp}` are upstream Nexus files (Federico
  Ponchio and the Nexus contributors) whose upstream version has no header either; they are
  covered by Nexus' licence (`licenses/Nexus.LICENSE`). (`src/compress/compressor.h` had none
  upstream either; it has the notice of Nexus now, as the rest of the file derives from it.)
- `third_party/corto/` `src/corto_codec.{h,cpp}` and `include/corto/corto.h` have no header; the
  other corto files carry a header by Federico Ponchio that names the GNU Lesser General Public
  License, version 3 or later. corto's repository is under the MIT licence
  (`licenses/corto.LICENSE`), which covers them all.
