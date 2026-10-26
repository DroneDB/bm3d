/*
Nexus

Copyright(C) 2012 - Federico Ponchio
ISTI - Italian National Research Council - Visual Computing Lab

Copyright (C) 2026 Digipa

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License (http://www.gnu.org/licenses/gpl.txt)
for more details.
*/
#include "compressor.h"

#include <cmath>

#include "../common/error.h"

#include <corto/corto.h>

namespace bm3d::detail {

float coordinateStep(float errorQ, float rootError, const std::vector<float> &leafErrors) {
	float step = errorQ * rootError / 2.0f;
	for(float error: leafErrors) {
		const double e = errorQ * error / 2.0;
		if(e < step && e > 0.0)
			step = static_cast<float>(e);
	}

	//the logarithm of a step that is not positive is not a number
	if(step <= 0.0f)
		step = 1.0f;
	return step;
}

int coordinateExponent(float step) {
	int exponent = std::ilogb(step);   //floor(log2(step)), exact
	//log2 of a negative exponent that is not a power of two is rounded up toward zero
	if(exponent < 0 && std::ldexp(1.0f, exponent) != step)
		exponent++;
	return exponent;
}

NodeCompressor::NodeCompressor(const VertexLayout &layout, const CompressionSettings &settings, int coordinateExponent):
	layout_(layout), settings_(settings), coordinateExponent_(coordinateExponent) {
}

EncodedNode NodeCompressor::compress(const unsigned char *memory, uint32_t nvert, uint32_t nface,
                                     const std::vector<uint32_t> &patchEnds) const {
	if(nface == 0)
		throw Error("a node without triangles cannot be compressed");

	//the arrays of the node, one after the other
	const float *coords = reinterpret_cast<const float *>(memory);
	const unsigned char *next = memory + size_t(nvert) * 12;
	const float *uvs = nullptr;
	if(layout_.texCoords) {
		uvs = reinterpret_cast<const float *>(next);
		next += size_t(nvert) * 8;
	}
	const int16_t *normals = nullptr;
	if(layout_.normals) {
		normals = reinterpret_cast<const int16_t *>(next);
		next += size_t(nvert) * 6;
	}
	const unsigned char *colors = nullptr;
	if(layout_.colors) {
		colors = next;
		next += size_t(nvert) * 4;
	}
	const uint16_t *faces = reinterpret_cast<const uint16_t *>(next);

	crt::Encoder encoder(nvert, nface);
	for(uint32_t end: patchEnds)
		encoder.addGroup(end);

	encoder.addPositions(coords, faces, static_cast<float>(std::ldexp(1.0, coordinateExponent_)));
	if(normals)
		encoder.addNormals(normals, settings_.normalBits, crt::NormalAttr::ESTIMATED);
	if(colors)
		encoder.addColors(colors, settings_.colorBits[0], settings_.colorBits[1], settings_.colorBits[2], settings_.colorBits[3]);
	if(uvs)
		encoder.addUvs(uvs, settings_.texStep/512);
	encoder.encode();

	EncodedNode out;
	out.stream.assign(encoder.stream.data(), encoder.stream.data() + encoder.stream.size());
	out.vertexCount = static_cast<uint16_t>(encoder.nvert);
	out.faceCount = static_cast<uint16_t>(encoder.nface);
	for(const crt::Group &group: encoder.index.groups)
		out.groupEnds.push_back(group.end);
	return out;
}

} //namespace
