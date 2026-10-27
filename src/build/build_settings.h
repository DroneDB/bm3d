// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a build is made of, whoever asks for it: the size of the nodes, the quality of the textures,
// the memory the build may take, and how corto quantises the nodes. A model is built the same way
// every time; only the number of threads varies (see Attempt).

#ifndef BM3D_BUILD_SETTINGS_H
#define BM3D_BUILD_SETTINGS_H

#include <cstdint>

#include "../compress/compressor.h"

namespace bm3d::detail {

struct BuildSettings {
	int nodeFaces = 1 << 14;        //triangles per node
	int topNodeFaces = 4096;        //triangles of the root node
	int textureQuality = 90;        //WebP quality of the node textures
	int maxNodeTextureSide = 1024;  //longest side of a node texture
	int ramBufferMb = 16000;        //approximate RAM budget
	float scaling = 0.5f;           //simplification ratio per level
	int skipLevels = -1;            //levels that keep the geometry and halve the texture (-1: as the texture needs)
	float adaptive = 0.333f;        //kd-tree adaptive factor
	CompressionSettings compression;

	//the memory the chunks of the nodes and the texture pyramid may keep mapped
	uint64_t buildMemory() const { return (uint64_t(1) << 20) * static_cast<uint64_t>(ramBufferMb) / 4; }
	//the memory the kd-tree may keep mapped
	uint64_t treeMemory() const { return (uint64_t(1) << 20) * static_cast<uint64_t>(ramBufferMb) / 2; }

	//The weight of a texel in the kd-tree, which splits a node when the weight of its texels passes
	//the triangles of a node: a node holds the texels that fill its texture without downscaling, at
	//most maxNodeTextureSide on a side. The charts of a node fill about this share of its texture
	//once packed: on a photogrammetry model (224k triangles, 1.6 gigapixels of texture) nodes cut at
	//this share of the largest node texture left 4% of the texels to the downscaling that makes them fit.
	float texelWeight(int faces) const {
		const double kFill = 0.35;
		const double side = double(maxNodeTextureSide);
		return float(double(faces) / (side * side * kFill));
	}
};

} //namespace

#endif // BM3D_BUILD_SETTINGS_H
