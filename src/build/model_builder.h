// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#ifndef BM3D_MODEL_BUILDER_H
#define BM3D_MODEL_BUILDER_H

#include <memory>
#include <string>
#include <vector>

#include "build_settings.h"
#include "dag_ops.h"
#include "hierarchy_builder.h"
#include "level_builder.h"
#include "model_dag.h"
#include "model_saver.h"
#include "node_texture_baker.h"
#include "node_texture_store.h"
#include "texture_encoder_pool.h"
#include "../common/logger.h"
#include "../io/image/webp_codec.h"

class KDTreeSoup;
class StreamSoup;

namespace bm3d::detail {

//The parts that make a model, put together for one build: the graph, the textures of the nodes and
//the threads that encode them, the maker of a level and the one of the whole hierarchy, and the
//saver. It runs them in the order they depend on each other: the textures are read, the levels are
//made, the graph is put in its final order, and the model is written.
class ModelBuilder {
public:
	//layout: the attributes of the vertices of the model. scratchDir, when not empty, is the folder where
	//the out-of-core caches (the chunks, the textures of the nodes, the tiles of the texture pyramid)
	//are made: an absolute path keeps the (possibly multi-GB) scratch on the same volume as the
	//output and lets it be cleaned up as a unit. When empty the caches fall back to the system
	//temporary folder, the textures of the nodes to the working directory.
	//threads: the threads the build keeps busy, the one that makes the nodes and threads - 1 that
	//encode their textures meanwhile.
	ModelBuilder(const BuildSettings &settings, int threads, const VertexLayout &layout,
	             const std::string &scratchDir, const Logger &logger);

	//Reads the textures the materials of the input name into the pyramid; throws Error when one
	//cannot be read. Nothing to do for a model without textures.
	void addTextures(std::vector<LoadTexture> &textures);

	//Makes the levels from the triangles of `stream` and puts the graph in its final order. Throws
	//Error when the input holds no valid geometry.
	void build(KDTreeSoup &tree, StreamSoup &stream);

	//Writes the model to the file `output`, under another name until it is complete, with the
	//metadata block
	void save(const std::string &output, const std::string &metadata);

private:
	BuildSettings settings_;
	VertexLayout layout_;
	WebpEncoder encoder_;
	ModelDag dag_;
	NodeTextureStore store_;
	//only for a model with textures
	std::unique_ptr<NodeTextureBaker> baker_;
	std::unique_ptr<TextureEncoderPool> encoders_;
	std::unique_ptr<NodeTextures> textures_;
	LevelBuilder levels_;
	HierarchyBuilder hierarchy_;
	ModelSaver saver_;
};

} //namespace

#endif // BM3D_MODEL_BUILDER_H
