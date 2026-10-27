# bm3d

bm3d is a builder, a file format and a web viewer for multiresolution 3D models. It turns a mesh
(OBJ, PLY or STL), with its textures, into one compressed file from which a viewer streams only
the detail it needs, so a model of many millions of triangles and gigapixels of texture opens in
a web page.

Status of this version (1.1.1): the builder and its command line write a `.bm3d` file, format
version 1, always corto-compressed, specified in [`docs/FORMAT.md`](docs/FORMAT.md), and the web
viewer (`bm3d-viewer`, in `web/`) reads it. Programs for Linux and Windows, the viewer package and
the source are attached to each [release](https://github.com/DroneDB/bm3d/releases).

## 1. Derived from Nexus

bm3d is derived from [Nexus](https://github.com/cnr-isti-vclab/nexus) by Federico Ponchio and
the Visual Computing Lab, ISTI-CNR, and uses [VCGLib](https://github.com/cnr-isti-vclab/vcglib)
by Paolo Cignoni and the Visual Computing Lab, ISTI-CNR, and
[corto](https://github.com/cnr-isti-vclab/corto) by Federico Ponchio. The web viewer is
based on the MIT-licensed JavaScript of Nexus. We thank the authors: the multiresolution
structure, the mesh compression and the library underneath are their work. The starting point
is the DroneDB fork of Nexus, which `PROVENANCE.md` describes together with the exact upstream
commits.

## 2. What changed

- No Qt: images, files and paths are handled with the C++ standard library and a small set of
  image libraries.
- Its own file format, version 1 (`docs/FORMAT.md`; not readable by the Nexus tools, and the magic
  number says so). Version 0.2.0 and earlier wrote the Nexus container; 1.0.0 is the first release that writes this one. The new file has offsets
  in bytes aligned to 8 instead of units of 256, a metadata block (the program and its version,
  the settings of the build, the input, and any JSON object given with `--metadata`), and a
  content hash that ignores the metadata, so that two builds of the same input have the same hash.
- Full-resolution WebP textures, sized by texels instead of capped at 1024 pixels.
- Hardened loaders for OBJ, PLY and STL.
- Deterministic output: the same input and options give the same bytes on every platform (GCC,
  Clang and MSVC builds write the same file; the golden tests check it), and the same content hash
  (docs/FORMAT.md, section 9) with any number of threads.
- UTF-8 paths on every platform, and atomic output: the file is written under a temporary name
  and renamed only when the build succeeds.

## 3. Building

Requirements: a C++17 compiler (GCC, Clang or MSVC), CMake and [vcpkg](https://vcpkg.io). The
dependencies (libjpeg-turbo, libwebp, libtiff, stb) are listed in `vcpkg.json` and
installed by vcpkg (see Dependencies below). Point `VCPKG_ROOT` at a vcpkg checkout; when bm3d is
the top-level project it uses vcpkg's toolchain file from there.

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

The program is `bm3d` in the build directory; a standalone build is a Release build unless you
choose another type. `CMakePresets.json` has `release` and `debug` presets
(`cmake --preset release`). `cmake --install build --prefix <dir>` installs the program; the
static library, its header and a CMake package (`find_package(bm3d)`, target `bm3d::bm3d`) come
with it in a standalone build, and are left out when bm3d is added to another CMake project with
`add_subdirectory`. In that case it uses the toolchain of that project, sets no compiler flag
and leaves the output directories alone. The options `BM3D_BUILD_TESTS` and
`BM3D_INSTALL_DEVELOPMENT` are on for a standalone build and off for a subproject.

The version is the text of `VERSION`; `vcpkg.json` repeats it and must be kept in step by hand.

`BM3D_WITH_TIFF` (on by default) reads TIFF textures with libtiff. Off, libtiff is neither found nor
linked (`-DBM3D_WITH_TIFF=OFF`; vcpkg then does not install it either, because `tiff` is a default
feature of `vcpkg.json` and the option turns the defaults off), the program is a quarter smaller, and a
`.tif` texture is refused as invalid input (exit code 2) with a message that says this bm3d was built
without TIFF support. Photogrammetry software writes PNG or JPEG textures, so most uses do not need it.

### Dependencies

What reads the pictures of a model (textures are told apart by their first bytes, not by the
extension):

| Format | Library | Why |
| --- | --- | --- |
| PNG, BMP, GIF, TGA | stb_image (header only, from the `stb` port) | For PNG it decoded the 72 textures of a large photogrammetry model (1.56 gigapixels) 2.3% faster than libpng and the output file was byte for byte the same (below). One header replaces libpng in the program. |
| JPEG | libjpeg-turbo | stb's JPEG decoder is as fast but not the same: its inverse DCT and chroma upsampling round differently, so the output changed. libjpeg-turbo is also needed to write the JPEG tiles the texture cache spills to disk. |
| WebP | libwebp | Reads WebP textures and writes every node texture. |
| TIFF | libtiff (optional, `BM3D_WITH_TIFF`) | The largest of the image libraries and the one few models need. |

The mesh code needs no library: corto (mesh compression) and VCGLib are in `third_party/` (VCGLib
without Eigen, see `PROVENANCE.md`). libpng is only used by the tests, to write the PNG pictures stb cannot (a palette,
16 bit samples, an interlaced image).

How the decoders were chosen: `scripts/bench.py` builds a model several times with several programs,
interleaved, and prints the median wall time, the spread, the peak memory, the size of the file and
its SHA-256. A library is replaced by stb only if the median build is at most 3% slower and the file
is identical. The measurements, on a 32 thread machine with `--threads 8` and a machine load between
1.7 and 2.9, three builds each:

| Model | Decoder | Median | Range | Peak memory | File | Same file |
| --- | --- | --- | --- | --- | --- | --- |
| 72 PNG textures, 224k triangles | libpng | 112.3 s | 111.5 to 113.0 s | 4326 MiB | 358,059,008 bytes | reference |
| | stb | 109.7 s (-2.3%) | 109.6 to 111.6 s | 4583 MiB (+6%) | 358,059,008 bytes | yes |
| the same textures as JPEG (quality 90) | libjpeg-turbo | 99.0 s | 99.0 to 99.0 s | 4286 MiB | 350,393,600 bytes | reference |
| | stb | 99.2 s (+0.1%) | 99.1 to 99.2 s | 4542 MiB (+6%) | 350,339,840 bytes | no |

stb keeps the whole decoded picture in memory next to the one it hands to the builder, which is the
6% more memory; libpng wrote its rows into the picture directly. stb is also the decoder of pictures
that come from the user, so the robustness tests damage a PNG texture at random, and the sanitizer
workflow runs them.

## 4. Command line

    bm3d [--threads N] [--metadata <file.json>] <in.obj|ply|stl> <out.bm3d>
    bm3d --version
    bm3d --help

- `--threads N` limits the build to N threads, from 1 to 4096. The default is one thread per core.
- `--metadata <file.json>` is a JSON object whose members are stored in the metadata block of the
  file, after the ones bm3d writes (`generator`, `buildOptions` and `source`), in their order and
  with their values unchanged. The top-level keys `generator`, `buildOptions`, `source` and any
  that starts with `bm3d` are reserved, a key cannot be given twice, and the whole block is at
  most 1 MiB: anything else is a wrong command line (exit code 64), refused before the build starts.
- `--version` prints `bm3d <version> (format <N>)`, for example `bm3d 1.1.1 (format 1)`.
- The output is always corto-compressed; there is no option to turn compression off.
- The input type is chosen by the extension, in any letter case.
- While it works, bm3d prints a progress line to standard output when it starts and then every
  15 seconds.
- Exit codes: 0 the file was written; 1 the build failed after the input was read, or memory or
  scratch space ran out while it was read; 2 the input (the mesh or a texture its materials
  name) is missing, unreadable or cannot be parsed; 64 wrong command line.
- The output file appears only when the build succeeded.

## 5. Format and viewer

The `.bm3d` format is described in [`docs/FORMAT.md`](docs/FORMAT.md): the header, the index, the
corto payloads and WebP textures, the metadata block, the version rule and the content hash. The
C++ reader and writer are in `src/format/`; `scripts/dump.py` is a second reader, in Python, that
prints a file and checks it, and compares it with the Nexus container of the same build.

The viewer is the npm package `bm3d-viewer` in `web/` (ES modules, three.js 0.160 or later as a peer
dependency; MIT). It is not on npm: it is the file `bm3d-viewer-<version>.tgz` of each release, which
carries the bundles, the corto decoder (`corto-worker.js`, `corto.wasm`) and its licences. Install it
from the release:

    npm install https://github.com/DroneDB/bm3d/releases/download/v1.1.1/bm3d-viewer-1.1.1.tgz

then `import { Bm3dObject } from 'bm3d-viewer'` and give it the URL of a `.bm3d` file; the server
must answer HTTP Range requests. The options (where the decoder is served from, the cache) and the
build of the package are in [`web/README.md`](web/README.md).

## 6. Licences

The C++ code is distributed under GPL-3.0 (`LICENSE`): the files written for bm3d are
GPL-3.0-or-later, and the two that derive from DroneDB's libnexus (`src/api/bm3d.cpp`,
`cli/main.cpp`) are GPL-3.0-only, as libnexus is. The Nexus, VCGLib and corto sources it contains
keep their own headers and licences (`licenses/`, `NOTICE`). The web viewer is MIT (`web/LICENSE`). Where each file
comes from is in `PROVENANCE.md`. The imported sources can be checked against their upstream
commits with `scripts/sync-vcg.py --check`, `scripts/sync-corto.py --check` and
`scripts/import-nexus.py --check`.

The release archives of the program carry `LICENSE`, `NOTICE`, the licence texts of Nexus, VCGLib
and corto (`licenses/`) and the licence of each vcpkg library the program links (`licenses/ports/`).
The Linux program links libstdc++ and libgcc statically under the GCC Runtime Library Exception,
and the Windows program links the Microsoft runtime statically (`NOTICE`).
