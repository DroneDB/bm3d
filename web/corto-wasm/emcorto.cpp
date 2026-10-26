// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2022, Visual Computing Lab, ISTI - CNR (corto, emcorto.cpp)
// Modified for bm3d-viewer by Digipa: only the calls the viewer makes are exported, and a
// decoder that cannot be allocated is reported with a null pointer.
//
// The C interface of the corto decoder that corto-worker.js calls. Everything the format asks of
// the decoder is here (docs/FORMAT.md section 5): 16-bit indexes, 16-bit normals, RGBA colours.

#include <emscripten.h>

#include <new>

#include "decoder.h"

using namespace crt;

extern "C" {

Decoder *EMSCRIPTEN_KEEPALIVE newDecoder(int n, const uchar *buffer) {
	return new (std::nothrow) Decoder(n, buffer);
}

int EMSCRIPTEN_KEEPALIVE nvert(Decoder *decoder) { return decoder->nvert; }

int EMSCRIPTEN_KEEPALIVE nface(Decoder *decoder) { return decoder->nface; }

bool EMSCRIPTEN_KEEPALIVE hasNormal(Decoder *decoder) { return decoder->hasAttr("normal"); }

bool EMSCRIPTEN_KEEPALIVE hasColor(Decoder *decoder) { return decoder->hasAttr("color"); }

bool EMSCRIPTEN_KEEPALIVE hasUv(Decoder *decoder) { return decoder->hasAttr("uv"); }

void EMSCRIPTEN_KEEPALIVE setPositions(Decoder *decoder, float *buffer) { decoder->setPositions(buffer); }

void EMSCRIPTEN_KEEPALIVE setNormals16(Decoder *decoder, int16_t *buffer) { decoder->setNormals(buffer); }

void EMSCRIPTEN_KEEPALIVE setColors(Decoder *decoder, uchar *buffer, int components) {
	decoder->setColors(buffer, components);
}

void EMSCRIPTEN_KEEPALIVE setUvs(Decoder *decoder, float *buffer) { decoder->setUvs(buffer); }

void EMSCRIPTEN_KEEPALIVE setIndex16(Decoder *decoder, uint16_t *buffer) { decoder->setIndex(buffer); }

void EMSCRIPTEN_KEEPALIVE decode(Decoder *decoder) { decoder->decode(); }

void EMSCRIPTEN_KEEPALIVE deleteDecoder(Decoder *decoder) { delete decoder; }

}
