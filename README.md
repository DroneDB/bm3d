# bm3d

bm3d is a builder, a file format and a web viewer for multiresolution 3D models. It turns a mesh
(OBJ, PLY or STL), with its textures, into one compressed file from which a viewer streams only
the detail it needs, so a model of many millions of triangles and gigapixels of texture opens in
a web page.

Status of this version (0.1.0): the builder and its command line are in place. The output is
still the Nexus container ("format 0"), always corto-compressed. The `.bm3d` format, its
specification and the web viewer arrive in later releases.

## 1. Derived from Nexus

bm3d is derived from [Nexus](https://github.com/cnr-isti-vclab/nexus) by Federico Ponchio and
the Visual Computing Lab, ISTI-CNR, and uses [VCGLib](https://github.com/cnr-isti-vclab/vcglib)
by Paolo Cignoni and the Visual Computing Lab, ISTI-CNR, and
[corto](https://github.com/cnr-isti-vclab/corto) by Federico Ponchio. The web viewer will be
based on the MIT-licensed JavaScript of Nexus. We thank the authors: the multiresolution
structure, the mesh compression and the library underneath are their work. The starting point
is the DroneDB fork of Nexus, which `PROVENANCE.md` describes together with the exact upstream
commits.

## 2. What changed

- No Qt: images, files and paths are handled with the C++ standard library and a small set of
  image libraries.
- Its own file format, version 1 (not readable by the Nexus tools). Version 0.1.0 still writes
  the Nexus container.
- Full-resolution WebP textures, sized by texels instead of capped at 1024 pixels.
- Hardened loaders for OBJ, PLY and STL.
- Deterministic output: the same input gives the same bytes.
- UTF-8 paths on every platform, and atomic output: the file is written under a temporary name
  and renamed only when the build succeeds.

## 3. Building

Requirements: a C++17 compiler (GCC, Clang or MSVC), CMake and [vcpkg](https://vcpkg.io). The
dependencies (Eigen, libjpeg-turbo, libwebp, libtiff, stb) are listed in `vcpkg.json` and
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

### Dependencies

What reads the pictures of a model (textures are told apart by their first bytes, not by the
extension):

| Format | Library | Why |
| --- | --- | --- |
| PNG, BMP, GIF, TGA | stb_image (header only, from the `stb` port) | For PNG it decoded the 72 textures of a large photogrammetry model (1.56 gigapixels) 2.3% faster than libpng and the output file was byte for byte the same (below). One header replaces libpng in the program. |
| JPEG | libjpeg-turbo | stb's JPEG decoder is as fast but not the same: its inverse DCT and chroma upsampling round differently, so the output changed. libjpeg-turbo is also needed to write the JPEG tiles the texture cache spills to disk. |
| WebP | libwebp | Reads WebP textures and writes every node texture. |
| TIFF | libtiff | Reads TIFF textures. |

Eigen is used by the VCGLib simplification code; corto (mesh compression) and VCGLib are in
`third_party/`. libpng is only used by the tests, to write the PNG pictures stb cannot (a palette,
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
- `--metadata <file.json>` is a JSON object to store in the file. Version 0.1.0 checks it and
  does not store it yet.
- `--version` prints `bm3d <version> (format <N>)`; for 0.1.0 that is `bm3d 0.1.0 (format 0)`,
  where 0 means the Nexus container.
- The output is always corto-compressed; there is no option to turn compression off.
- The input type is chosen by the extension, in any letter case.
- While it works, bm3d prints a progress line to standard output when it starts and then every
  15 seconds.
- Exit codes: 0 the file was written; 1 the build failed after the input was read, or memory or
  scratch space ran out while it was read; 2 the input (the mesh or a texture its materials
  name) is missing, unreadable or cannot be parsed; 64 wrong command line.
- The output file appears only when the build succeeded.

## 5. Format and viewer

The `.bm3d` format will be described in `docs/FORMAT.md` and the viewer will live in `web/`
(npm package `bm3d-viewer`), both published with the first release that writes the format.

## 6. Licences

The C++ code is distributed under GPL-3.0 (`LICENSE`): the files written for bm3d are
GPL-3.0-or-later, and the two that derive from DroneDB's libnexus (`src/api/bm3d.cpp`,
`cli/main.cpp`) are GPL-3.0-only, as libnexus is. The Nexus, VCGLib and corto sources it contains
keep their own headers and licences (`licenses/`, `NOTICE`). The web viewer will be MIT. Where each file
comes from is in `PROVENANCE.md`. The imported sources can be checked against their upstream
commits with `scripts/sync-vcg.py --check`, `scripts/sync-corto.py --check` and
`scripts/import-nexus.py --check`.
