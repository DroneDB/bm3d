#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2022, Visual Computing Lab, ISTI - CNR (corto, html/js/emscripten/build.sh)
# Modified for bm3d-viewer by Digipa.
#
# Builds corto.wasm, the corto decoder that the viewer's worker runs, from the sources of
# third_party/corto (the patched copy bm3d builds its encoder from) and emcorto.cpp.
#
#   build.sh [output-directory]       default: <this directory>/out
#
# Needs emcc of the emsdk version named in EMSDK_VERSION (CI installs that tag; locally
# docker-build.sh runs this script in the emscripten/emsdk image of the same tag). The flags are
# corto's own: no exceptions (the decoder is given a stream that was checked by the caller), no
# entry point (a library, called from JavaScript), memory that grows, a small stack. Differences
# from corto's script: the settings TOTAL_MEMORY and TOTAL_STACK were renamed in emscripten
# (INITIAL_MEMORY and STACK_SIZE), the stack is 64 KiB instead of 24 KiB, and the output is the
# .wasm file itself instead of a hexadecimal string pasted into a script.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
corto="${CORTO_DIR:-$here/../../third_party/corto}"
out="${1:-$here/out}"
mkdir -p "$out"

emcc -std=c++11 "$here/emcorto.cpp" \
  "$corto/src/cstream.cpp" \
  "$corto/src/bitstream.cpp" \
  "$corto/src/tunstall.cpp" \
  "$corto/src/normal_attribute.cpp" \
  "$corto/src/color_attribute.cpp" \
  "$corto/src/decoder.cpp" \
  -I"$corto/include/corto" \
  -fno-exceptions \
  -O2 -DNDEBUG -DNO_EXCEPTIONS \
  -sDISABLE_EXCEPTION_CATCHING \
  --no-entry \
  -sEXPORTED_FUNCTIONS='["_newDecoder","_nvert","_nface","_hasNormal","_hasColor","_hasUv","_setPositions","_setNormals16","_setColors","_setUvs","_setIndex16","_decode","_deleteDecoder","_malloc","_free"]' \
  -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=65536 -sINITIAL_MEMORY=1048576 \
  -o "$out/corto.wasm"
