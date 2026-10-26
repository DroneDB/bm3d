# Provenance

Where every part of this repository comes from, and how to check or repeat the import. People are
named by their commit names.

## Sources

| Part | Upstream | Fork or pin used here |
|---|---|---|
| Nexus builder | https://github.com/cnr-isti-vclab/nexus, `master` at `f29e8fd1182b2a1c0c512add99c7edfc8e146680` | https://github.com/DroneDB/nexus at `847f024bf7cc0beb078868749bc7669748c0aa3d` |
| VCGLib | https://github.com/cnr-isti-vclab/vcglib, `devel`, commit `03484542632225739ca96b28afcde38267561323` | the same upstream commit plus 4 patches (`third_party/vcg-patches/`) |
| corto | https://github.com/cnr-isti-vclab/corto, `master`, commit `d240e69306a39417c098475d7002eb16b7264248` | the upstream commit unchanged |
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

### The VCGLib patches

The 4 commits of the DroneDB fork of VCGLib (https://github.com/DroneDB/vcglib, by HeDo) on top
of upstream `0348454` touch only `wrap/ply/plylib.cpp` and `wrap/ply/plylib.h`:
`57ef561` opens UTF-8 file names on Windows, `1d4aaa8` bounds the length of a list read into
memory the caller allocated, `3306f50` keeps `interpret_texture_name` inside its buffer and adds a
`std::string` form, and `42b35dd` reads the length of a binary list as the type the header
declares. `third_party/vcg-patches/` holds them as made by `git format-patch 0348454..42b35dd`,
with their authorship.

## Files taken and how to repeat it

| Directory | List and manifest | Script |
|---|---|---|
| `third_party/vcg/` (only the VCGLib files the build reads) | `FILES`, `VCG_SOURCE` | `scripts/sync-vcg.py` |
| `third_party/corto/` (the 8 compiled sources and the headers they include) | `FILES`, `CORTO_SOURCE` | `scripts/sync-corto.py` |
| `src/common`, `src/nxsbuild`, `src/nxsedit` (and `src/nxszip`, since removed) | `NEXUS_SOURCE` | `scripts/import-nexus.py` |

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
- The Nexus files are ours to edit after the import. `scripts/import-nexus.py --check` lists the
  files that have changed or gone since, with the hashes of the import as the reference; with
  `--source <clone of the fork>` it also checks that the recorded hashes are those of the pinned
  commit.

## Changes made to the imported files

- `src/nxsbuild/nexusbuilder.cpp`: the vertex cache optimisation is removed. Its function
  `optimizeNode` begins with `return;` in the imported code, so the optimisation never ran
  upstream, and corto reorders the faces while compressing anyway. The include of
  `vertex_cache_optimizer.h` and the dead call go with it. Output does not change.
- `third_party/vcg/wrap/ply/plylib.{cpp,h}`: the 4 patches above.
- `src/common/dag.h`: every field of `Node` has a value from the start, and the empty sphere of
  `Header` and `Node` has its centre at the origin (`emptySphere()`). The sink node that ends the
  node table set only `first_patch`, so the file got the rest of it as it lay on the stack, and
  two builds of the same input differed in a few bytes. The file keeps its layout.
- `src/common/cone.h`: `Cone3s::n` starts at zero, for the same reason (the cone of the sink node).
- `src/nxsbuild/nexusbuilder.cpp`: the bounding sphere of the header starts from `emptySphere()`
  instead of vcg's empty sphere, whose centre is not set.
- `src/nxsbuild/mesh.{h,cpp}`, `src/nxsbuild/tmesh.{h,cpp}`: the debug export of a mesh to a PLY
  file (`savePly`, `savePlyTex`) and the include of VCGLib's `export.h` that served it are removed;
  nothing called them.
- `src/nxsedit/extractor.{h,cpp}`: `savePly`, `saveStl`, `saveUnifiedPly` and `countElements`
  (which only they used) are removed, with the small PLY structures of `saveUnifiedPly`. The
  program never wrote an extracted mesh.
- `src/nxszip/*` (`abitstream.cpp`, `atunstall.cpp`, `meshcoder.{h,cpp}`, `meshdecoder.{h,cpp}`,
  `bitstream.h`, `cstream.h`, `fpu_precision.h`, `tunstall.h`, `zpoint.h`): removed, the whole
  directory. It held MECO, the compression Nexus used before corto, which the program never
  selects; its two call sites (`extractor.cpp`, `nexusdata.cpp`), the `MECO` name in
  `src/common/signature.h` and the extractor's `error_factor`, which only MECO read, go with it.
  The flag values of the container are unchanged.
- `src/nxsbuild/tsloader.{h,cpp}`, `tsploader.{h,cpp}`, `colormap.{h,cpp}`, `vcgloader.h`,
  `vcgloadermesh.h`: removed. They read Nexus' `.ts` and `.tsp` files, coloured a `.ts` by a
  colormap, and fell back on VCGLib's importers for any other extension; bm3d reads OBJ, PLY and
  STL only.
- `src/nxsbuild/meshstream.{h,cpp}`: the loader is chosen by the extension in any case, and an
  unknown one is an error that names the formats read; the colormap of the stream goes.
- `src/nxsbuild/tmesh.{h,cpp}`: `loadPly` (uncalled) and the include of VCGLib's `import.h` that
  served it are removed.
- Paths the program never takes, and options that nothing set, are removed from `src/nxsbuild/`
  `kdtree.{h,cpp}`, `mesh.{h,cpp}`, `tmesh.{h,cpp}`, `meshloader.{h,cpp}`, `meshstream.{h,cpp}`,
  `nexusbuilder.{h,cpp}`, `objloader.{h,cpp}`, `plyloader.{h,cpp}`, `stlloader.{h,cpp}`,
  `trianglesoup.h`, from `src/nxsedit/extractor.cpp` and from `src/common/` `nexusdata.{h,cpp}`,
  `nexusfile.h`, `stdnexusfile.{h,cpp}`, `signature.h`:
  - point clouds: `KDTreeCloud`, `StreamCloud`, `Stream::getBox`, `MeshLoader::getVertices` and its
    implementations, `createCloudLevel` and the cloud branch of `createLevel` (which was a choice
    between the two and is gone; `NexusBuilder::create` takes the triangle kd-tree and stream),
    `Mesh::load(Cloud)`, `getVertices`, `simplifyCloud` and `averageDistance` (and `TMesh`'s), the
    `Splat` and `Cloud` types, and the shuffle of the points of a compressed cloud in
    `NexusData::loadRam`;
  - deepzoom: the folder of files in `NexusBuilder::save` and `Extractor::save`, `loadDZNode`,
    `dropDZNode`, `loadDZTex`, `dropDZTex` and `readDZFile` of `NexusFile` and `StdNexusFile`, and
    `Signature::isDeepzoom`. The flag value stays as the enum entry `DEEPZOOM`;
  - `NexusBuilder` options that nothing set: `useNodeTex` (false made the container hold the
    original texture files, named in `images`), `createPowTwoTex` (and `nextPowerOf2`),
    `deepzoom`, `max_node_triangles`, the unused pixel counters, and `texFormat` with its enum
    (the JPEG node textures; `encodeJpeg` stays, the texture tiles that spill to disk use it) and
    the constructor taking a `Signature`, which nothing called;
  - `origin`, `scale`, `box` and the vertex quantization of `MeshLoader` and `Stream`, used by the
    loaders and `Stream::getBox`. They kept their defaults (no offset, scale 1, no quantization),
    for which the arithmetic was exact, so the vertices are unchanged. With the scale the ASCII STL
    loader loses a line that multiplied x by the z scale instead of z by it (`stlloader.cpp`).
  Output does not change.

Edits made after the import appear in `scripts/import-nexus.py --check`; larger ones are listed
here as they happen.

## Licence headers

The headers below were checked file by file, and those of the upstream files against upstream
Nexus at `f29e8fd`.

### Files added by the DroneDB fork of Nexus (Digipa headers)

`src/common/` `error.h`, `numeric.h`, `mappedfile.{h,cpp}`, `pathutil.h`, `scratchfile.{h,cpp}`,
`textfile.{h,cpp}`, `textparse.h` and `src/nxsbuild/` `image.{h,cpp}`, `imageio.{h,cpp}`,
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

- `vertex_cache_optimizer.h` (`nxsbuild`) had no notice and has no known author; it came with the
  2017 import of Nexus from Subversion. It was never imported and never was in the history of
  this repository.
- `src/common/stdnexusfile.{h,cpp}` were written for the Qt-free fork (by HeDo, on the model of
  upstream's `nexusfile.h`) and have no header; they are covered by the licence of this
  repository.
- `src/nxsbuild/` `texpyramid.{h,cpp}`, `stlloader.{h,cpp}`, `src/nxsedit/extractor.h` and
  `src/common/nexusfile.h` are upstream Nexus files (Federico Ponchio and the Nexus contributors)
  whose upstream version has no header either; they are covered by Nexus' licence
  (`licenses/Nexus.LICENSE`).
- `third_party/corto/` `src/corto_codec.{h,cpp}` and `include/corto/corto.h` have no header; the
  other corto files carry a header by Federico Ponchio that names the GNU Lesser General Public
  License, version 3 or later. corto's repository is under the MIT licence
  (`licenses/corto.LICENSE`), which covers them all.
- `third_party/vcg/wrap/io_trimesh/precision.h` has no header in VCGLib; VCGLib's licence
  applies.
