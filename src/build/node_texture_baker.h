// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#ifndef BM3D_NODE_TEXTURE_BAKER_H
#define BM3D_NODE_TEXTURE_BAKER_H

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "texpyramid.h"
#include "../common/logger.h"
#include "../io/image/image.h"
#include "../io/mesh/meshloader.h"

class TMesh;

namespace bm3d::detail {

//The texture of a node and how precise the node is with it
struct NodeTexture {
	Image image;          //the texture as stored, row 0 at the top (v = 1)
	float error = 0;      //the error of the node, in the units of the model
	float pixelsPerEdge = 0; //the mean length of an edge of the node, in texels
};

//Makes the texture of a node out of the pyramid of the model's textures: the pieces of texture its
//triangles use are cut out, packed in one image, the texture coordinates of the node are changed
//to point into it, and the texels no triangle draws are blanked. It owns the pyramid, which has a
//level for each level of the model (see beginLevel), and the rule that a model whose texture is
//much finer than its triangles spends its first levels making the texture coarser (textureOnlyLevels).
class NodeTextureBaker {
public:
	struct Settings {
		//the simplification ratio of the triangles from one level to the next: a level of simplified
		//triangles has the side of its textures sqrt(scaling) times that of the level below
		float scaling = 0.5f;
		//the longest side of a node texture, in pixels: a node whose texture would be larger gets it
		//downscaled to this size. 0 keeps every node texture at the resolution of its source
		int maxNodeTextureSide = 0;
		//the memory the pyramid may keep before it spills textures to disk
		uint64_t maxMemory = 2000000000;
		//where the pyramid spills them: the name of a scratch file (see ScratchFile::create); empty
		//is the system's temporary folder
		std::string scratchPrefix;
	};

	NodeTextureBaker(const Settings &settings, const Logger &logger);

	//Reads the textures of the model into the pyramid. False when they cannot all be read (a texture
	//that cannot be decoded throws Error). Fills in the size of each texture.
	bool addTextures(std::vector<LoadTexture> &textures);

	//The side of a level of simplified triangles relative to the one below, when the simplification
	//keeps `scaling` of the triangles: each triangle keeps its texels
	static float simplifiedScale(float scaling) { return float(std::sqrt(double(scaling))); }

	//Makes the textures of `level` of the pyramid, textureScale times the side of the level below,
	//and lets go of the level below (the nodes of the level before are done)
	void beginLevel(int level, float textureScale);

	//The texture of the node `mesh` of `level`. simplified: the node's triangles are simplified
	//after this, not kept as they are (its error counts at most a few texels along an edge). Changes
	//the texture coordinates of the mesh to point into the texture it returns. Throws
	//TexturePackingError when the pieces do not fit in a texture of 16K a side.
	NodeTexture bake(TMesh &mesh, int level, bool simplified);

	//The number of levels, from the finest, that keep the triangles and only halve their texture,
	//for a model whose texture has `texelsPerTriangle` texels for each triangle (the median)
	//when `scaling` of the triangles are kept from one level to the next: as many as it takes the
	//texture to come down to a few texels per triangle edge, 0 for a texture that is no finer than
	//its triangles or for a scaling that does not simplify.
	static int textureOnlyLevels(double texelsPerTriangle, float scaling);

private:
	TexAtlas atlas_;
	int maxNodeTextureSide_;
	Logger logger_;
};

} //namespace

#endif // BM3D_NODE_TEXTURE_BAKER_H
