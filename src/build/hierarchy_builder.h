// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#ifndef BM3D_HIERARCHY_BUILDER_H
#define BM3D_HIERARCHY_BUILDER_H

#include "level_builder.h"

class KDTreeSoup;
class StreamSoup;

namespace bm3d::detail {

//Makes the levels of the model one after the other, from the triangles of the input up, until what
//is left of the triangles fits in the root node. Each level is made from the triangles the one below
//simplified, and the textures follow the same path: a level of simplified triangles keeps the texels
//of each triangle, and a level that keeps the triangles halves its textures.
class HierarchyBuilder {
public:
	struct Settings {
		//the share of the triangles kept from one level to the next
		float scaling = 0.5f;
		//the levels, from the finest, that keep the triangles of the level below and only halve its
		//texture, for a model whose texture is much finer than its triangles. Negative: as many as
		//the texture needs to come down to a few texels per triangle edge
		int skipSimplifyLevels = -1;
		//the triangles the last level may have
		unsigned int topNodeFaces = 4096;
		bool hasTextures = false;
	};

	HierarchyBuilder(const Settings &settings, ModelDag &dag, LevelBuilder &levels);

	//Makes the levels of the model streamed to `stream`, with the kd-tree `tree`, which is cleared
	//and loaded again for each level
	void build(KDTreeSoup &tree, StreamSoup &stream);

private:
	Settings settings_;
	ModelDag &dag_;
	LevelBuilder &levels_;
};

} //namespace

#endif // BM3D_HIERARCHY_BUILDER_H
