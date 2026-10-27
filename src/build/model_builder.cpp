// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#include "model_builder.h"

#include "kdtree.h"
#include "../common/error.h"
#include "../io/mesh/meshstream.h"

namespace bm3d::detail {

namespace {

//The name of a scratch file in the folder of the caches; without a folder the bare name, which
//the scratch layer places in the system's temporary folder (see VirtualMemory)
std::string cacheName(const std::string &scratchDir, const char *name) {
	return scratchDir.empty() ? std::string(name) : (scratchDir + "/" + name);
}

std::unique_ptr<NodeTextureBaker> makeBaker(const BuildSettings &settings, const std::string &scratchDir, const Logger &logger) {
	NodeTextureBaker::Settings baker;
	baker.scaling = settings.scaling;
	baker.maxNodeTextureSide = settings.maxNodeTextureSide;
	baker.maxMemory = settings.buildMemory();
	//the texture tiles that do not fit in memory are kept next to the other caches too
	if(!scratchDir.empty())
		baker.scratchPrefix = cacheName(scratchDir, "cache_atlas");
	return std::unique_ptr<NodeTextureBaker>(new NodeTextureBaker(baker, logger));
}

HierarchyBuilder::Settings hierarchySettings(const BuildSettings &settings, const VertexLayout &layout) {
	HierarchyBuilder::Settings hierarchy;
	hierarchy.scaling = settings.scaling;
	hierarchy.skipSimplifyLevels = settings.skipLevels;
	hierarchy.topNodeFaces = settings.topNodeFaces;
	hierarchy.hasTextures = layout.texCoords;
	return hierarchy;
}

} //namespace

ModelBuilder::ModelBuilder(const BuildSettings &settings, int threads, const VertexLayout &layout,
                           const std::string &scratchDir, const Logger &logger):
	settings_(settings), layout_(layout), encoder_(settings.textureQuality),
	dag_(layout, cacheName(scratchDir, "cache_chunks")),
	store_(cacheName(scratchDir, "cache_tex")),
	baker_(layout.texCoords ? makeBaker(settings, scratchDir, logger) : nullptr),
	encoders_(layout.texCoords ? new TextureEncoderPool(encoder_, threads, [this](size_t texture, const std::vector<unsigned char> &encoded) {
		store_.store(texture, encoded);
	}) : nullptr),
	textures_(layout.texCoords ? new NodeTextures{*baker_, store_, *encoders_} : nullptr),
	levels_(dag_, settings.scaling, textures_.get()),
	hierarchy_(hierarchySettings(settings, layout), dag_, levels_),
	saver_(settings.compression) {
	dag_.setMaxMemory(settings.buildMemory());
}

void ModelBuilder::addTextures(std::vector<LoadTexture> &textures) {
	if(baker_ && !baker_->addTextures(textures))
		throw Error("Failed to initialize texture atlas");
}

void ModelBuilder::build(KDTreeSoup &tree, StreamSoup &stream) {
	hierarchy_.build(tree, stream);
	if(encoders_)
		encoders_->finish();

	//only the sink: every block was empty (no valid triangles or points in the input)
	if(dag_.nodeCount() < 2)
		throw Error("no valid geometry in the input: the multiresolution dag would be empty");

	DagOps ops(dag_);
	ops.reverse();
	ops.saturate();
}

void ModelBuilder::save(const std::string &output, const std::string &metadata) {
	if(layout_.normals)
		DagOps(dag_).uniformNormals();
	saver_.save(dag_, store_, output, metadata);
}

} //namespace
