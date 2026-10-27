// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#include "hierarchy_builder.h"

#include "kdtree.h"
#include "../io/mesh/meshstream.h"

namespace bm3d::detail {

HierarchyBuilder::HierarchyBuilder(const Settings &settings, ModelDag &dag, LevelBuilder &levels):
	settings_(settings), dag_(dag), levels_(levels) {
}

void HierarchyBuilder::build(KDTreeSoup &tree, StreamSoup &stream) {
	//the triangles are weighed by the texels they cover at every level, not only at the first:
	//the stream forgets the textures once it has been read, so the tree keeps them, with the
	//texel area of the current level relative to the input
	tree.textures = stream.textures;
	tree.texelScale = 1.0;

	int skipSimplifyLevels = settings_.skipSimplifyLevels;
	const float simplifiedScale = NodeTextureBaker::simplifiedScale(settings_.scaling);
	int level = 0;
	int last_top_level_size = 0;
	size_t levelNodes = 0; //the nodes of the level just made
	float textureScale = 1.0f; //the side of this level's textures relative to the level below
	do {
		tree.clear();
		if(level % 2) tree.setAxesDiagonal();
		else tree.setAxesOrthogonal();

		tree.load(&stream);
		stream.Stream::clear(); //not the clear of the triangles it holds
		if(skipSimplifyLevels < 0)
			skipSimplifyLevels = settings_.hasTextures
				? NodeTextureBaker::textureOnlyLevels(tree.medianTexelsPerTriangle(), settings_.scaling) : 0;

		const size_t nodesBefore = dag_.nodeCount();
		levels_.buildLevel(tree, stream, level, textureScale, skipSimplifyLevels > 0);
		levelNodes = dag_.nodeCount() - nodesBefore;
		level++;
		//a level that keeps the triangles of the level below only makes the texture coarser: it
		//halves its side, as a mipmap does. A level of simplified triangles keeps the texels of
		//each triangle, sqrt(scaling) of the side.
		textureScale = skipSimplifyLevels > 0 ? 0.5f : simplifiedScale;
		tree.texelScale *= double(textureScale) * double(textureScale);
		if(skipSimplifyLevels <= 0 && last_top_level_size != 0 && stream.size()/(float)last_top_level_size > 0.9f) {
			//Larger top level, most probably to high parametrization fragmentation.
			break;
		}
		last_top_level_size = stream.size();
		skipSimplifyLevels--;
		//the levels that only halve the texture go on, however few the triangles, until one node
		//holds the model: the top would otherwise be many nodes of fine texture, all loaded at
		//once. A single node needs no coarser level, its texture is within maxNodeTextureSide.
	} while(stream.size() > settings_.topNodeFaces || (skipSimplifyLevels > 0 && levelNodes > 1));
}

} //namespace
