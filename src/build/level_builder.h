// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#ifndef BM3D_LEVEL_BUILDER_H
#define BM3D_LEVEL_BUILDER_H

#include <cstdint>
#include <vector>

#include "model_dag.h"
#include "node_texture_baker.h"
#include "node_texture_store.h"
#include "texture_encoder_pool.h"

class KDTreeSoup;
class StreamSoup;
class Mesh;
class TMesh;

namespace bm3d::detail {

//What a level of a model with textures needs besides the graph: the maker of the texture of a node,
//where the textures are kept once they are made, and the threads that encode them. The three belong
//to whoever builds the model, which keeps them alive for the levels.
struct NodeTextures {
	NodeTextureBaker &baker;
	NodeTextureStore &store;
	TextureEncoderPool &encoders;
};

//Makes one level of the model. The cells of the kd-tree are the nodes of the level: each is
//serialized into a chunk of the graph (with the texture it is given, if the model has textures),
//added to the graph, and simplified; its simplified triangles go to the stream that the kd-tree
//of the next level is made from.
class LevelBuilder {
public:
	//scaling: the share of the triangles of a node kept by the simplification, as the nodes of
	//the next level hold them. textures: null for a model without textures.
	LevelBuilder(ModelDag &dag, float scaling, NodeTextures *textures);

	//Makes the nodes of `level` from the cells of `input` and streams their simplified triangles to
	//`output`. textureScale: the side of the textures of this level relative to the level below.
	//keepTriangles: the level keeps the triangles of the level below and only halves its textures,
	//for a model whose texture is much finer than its triangles.
	void buildLevel(KDTreeSoup &input, StreamSoup &output, int level, float textureScale, bool keepTriangles);

private:
	void buildPlainNode(KDTreeSoup &input, StreamSoup &output, unsigned int block);
	void buildTexturedNode(KDTreeSoup &input, StreamSoup &output, unsigned int block, int level, bool keepTriangles);
	//moves the serialized node to a chunk of the graph and returns its index
	uint32_t storeChunk(const std::vector<unsigned char> &serialized);

	ModelDag &dag_;
	float scaling_;
	NodeTextures *textures_;
};

} //namespace

#endif // BM3D_LEVEL_BUILDER_H
