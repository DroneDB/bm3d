#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Digipa
#
# Runs build.sh in the emscripten/emsdk image of the version in EMSDK_VERSION, for a machine
# without emsdk. The repository is mounted read-only; the output directory is the only thing
# written, and it is given back to the calling user.
#
#   docker-build.sh [output-directory]       default: <this directory>/out
#
# The image is not removed afterwards (docker rmi emscripten/emsdk:<version>).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="$(realpath -m "${1:-$here/out}")"
version="$(tr -d '[:space:]' < "$here/EMSDK_VERSION")"
mkdir -p "$out"

docker run --rm \
  -v "$root:/src:ro" -v "$out:/out" \
  "emscripten/emsdk:$version" \
  bash -c "bash /src/web/corto-wasm/build.sh /out && chown -R $(id -u):$(id -g) /out"
