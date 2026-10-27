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
#ifndef BM3D_COMPRESSOR_H
#define BM3D_COMPRESSOR_H

#include <cstdint>
#include <vector>

#include "../format/bm3d_format.h"

namespace bm3d::detail {

//How corto quantises the attributes of a node
struct CompressionSettings {
	float errorQ = 0.1f;   //the step of the coordinates, as a fraction of the error of the nodes
	int normalBits = 10;
	int colorBits[4] = { 6, 6, 6, 5 };
	float texStep = 0.25f; //in pixels
};

//The coordinate step of the model: errorQ times half the smallest of the error of the root (the node 0)
//and the errors of the leaves (the caller gives, for a leaf that is a root, the length of its short
//edges, as its error is its radius)
float coordinateStep(float errorQ, float rootError, const std::vector<float> &leafErrors);

//The exponent of the coordinate step: log2(step) rounded toward zero (a step of 0.3 gives -1, one of
//3 gives 1, a power of two its own exponent). Read from the bits of the float, not computed with
//log2, whose last bit is not the same in every mathematical library: one bit off at a power of two
//would halve or double the step of every coordinate of the file.
int coordinateExponent(float step);

//One node as corto wrote it. The encoder drops the triangles whose corners are not three
//different vertices, and then the vertices no triangle uses, and moves the end of each group to
//match: the counts and the ends of the groups are the ones to write in the file, not the ones
//the node had when it went in.
struct EncodedNode {
	std::vector<unsigned char> stream;
	uint16_t vertexCount = 0;
	uint16_t faceCount = 0;
	std::vector<uint32_t> groupEnds;   //one for each patch of the node, in order
};

class NodeCompressor {
public:
	//coordinateExponent: the positions are quantised to steps of 2 to this power
	NodeCompressor(const VertexLayout &layout, const CompressionSettings &settings, int coordinateExponent);

	//memory holds what a node of the builder holds: the positions, then the texture coordinates,
	//the normals and the colours the layout has, then the triangles (three 16-bit indexes each);
	//patchEnds are the ends of the groups of triangles, one for each patch.
	EncodedNode compress(const unsigned char *memory, uint32_t nvert, uint32_t nface,
	                     const std::vector<uint32_t> &patchEnds) const;

private:
	VertexLayout layout_;
	CompressionSettings settings_;
	int coordinateExponent_;
};

} //namespace

#endif // BM3D_COMPRESSOR_H
