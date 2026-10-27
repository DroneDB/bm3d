// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "build_pipeline.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

#include "bm3d/version.h"
#include "input_reader.h"
#include "kdtree.h"
#include "model_builder.h"
#include "../common/error.h"
#include "../common/pathutil.h"
#include "../format/bm3d_writer.h"
#include "../format/format_error.h"
#include "../format/metadata.h"
#include "../io/mesh/meshstream.h"

namespace bm3d::detail {

namespace {

//What the builder read of the input
SourceInfo sourceOf(const std::string &input, StreamSoup *stream) {
	SourceInfo source;
	source.name = pathToUtf8(pathFromUtf8(input).filename());
	if(!stream)
		return source;
	source.vertices = stream->vertexCount();
	source.faces = stream->size();
	for(int k = 0; k < 3; k++) {
		source.boundsMin[k] = stream->box.min[k];
		source.boundsMax[k] = stream->box.max[k];
	}
	return source;
}

} //namespace

BuildPipeline::BuildPipeline(const BuildSettings &settings, const Logger &logger, const MeshLoaderRegistry &loaders,
                             const TryFunction &makeTry):
	settings_(settings), logger_(logger), loaders_(loaders), makeTry_(makeTry) {
}

std::string BuildPipeline::composeMetadata(const Attempt &attempt, const std::string &input, StreamSoup *stream,
                                           const std::vector<bm3d::JsonMember> &extraMetadata) const {
	const std::vector<BuildOption> options = {
		BuildOption::whole("threads", attempt.threads),
		BuildOption::whole("nodeFaces", attempt.nodeFaces),
		BuildOption::whole("topNodeFaces", settings_.topNodeFaces),
		BuildOption::whole("textureQuality", settings_.textureQuality),
		BuildOption::whole("maxNodeTextureSide", settings_.maxNodeTextureSide),
		BuildOption::fractional("scaling", settings_.scaling),
		BuildOption::fractional("adaptive", settings_.adaptive),
		BuildOption::whole("skipLevels", settings_.skipLevels),
		BuildOption::fractional("errorQ", settings_.compression.errorQ),
		BuildOption::whole("normalBits", settings_.compression.normalBits),
		BuildOption::whole("lumaBits", settings_.compression.colorBits[0]),
		BuildOption::whole("chromaBits", settings_.compression.colorBits[1]),
		BuildOption::whole("alphaBits", settings_.compression.colorBits[3]),
		BuildOption::fractional("texStep", settings_.compression.texStep),
	};

	const std::string metadata = bm3d::detail::composeMetadata(std::string("bm3d ") + BM3D_VERSION, options,
	                                                           sourceOf(input, stream), extraMetadata);
	try {
		checkMetadataLength(metadata.size());
	} catch(const MetadataTooLargeError &e) {
		throw ArgumentError(e.what());
	}
	return metadata;
}

std::string BuildPipeline::metadataProblem(const std::vector<bm3d::JsonMember> &extraMetadata) const {
	Attempt attempt;
	attempt.nodeFaces = settings_.nodeFaces;
	try {
		composeMetadata(attempt, std::string(), nullptr, extraMetadata);
	} catch(const ArgumentError &e) {
		return e.what();
	}
	return std::string();
}

void BuildPipeline::buildOnce(const std::string &input, const std::string &output, const Attempt &attempt,
                              const std::vector<bm3d::JsonMember> &extraMetadata) const {
	const uint64_t maxMemory = settings_.buildMemory();

	// Build a somewhat unique base name for cache files.
	// Place the out-of-core cache (stream + kd-tree) in the same directory as
	// the output, which under the DroneDB build pipeline is the per-build temp
	// folder. This keeps the (potentially multi-GB) scratch on the same large
	// volume as the result and lets it be reclaimed together with that folder,
	// instead of defaulting to the system temp dir. VirtualMemory uses an
	// absolute template prefix as-is.
	using Clock = std::chrono::steady_clock;
	const auto now = Clock::now().time_since_epoch().count();
	const std::string cacheDir = absoluteParentPath(output);
	const std::string cacheBase = cacheDir + "/bm3d_cache_" + std::to_string(static_cast<unsigned long long>(now));

	// Declared before the model builder, so that it outlives it as it always did
	std::unique_ptr<KDTreeSoup> tree;

	StreamSoup stream(cacheBase + "_stream", loaders_, logger_);
	stream.setMaxMemory(maxMemory);
	readMesh(stream, input);

	VertexLayout layout;
	layout.normals = true;
	layout.colors = stream.hasColors();
	layout.texCoords = stream.hasTextures() && attempt.withTextures;

	// Do not keep textures if they are not going to be used
	if(!layout.texCoords)
		stream.textures.clear();

	// The block is made before the hierarchy, so that one that does not fit is refused at once
	const std::string metadata = composeMetadata(attempt, input, &stream, extraMetadata);

	// Place the model's out-of-core caches (chunks + per-node texture file) in the same directory
	// as the output (the per-build temp folder), so they do not leak into the process working
	// directory and are reclaimed with it.
	ModelBuilder model(settings_, attempt.threads, layout, cacheDir, logger_);
	readingInput([&] { model.addTextures(stream.textures); });

	tree.reset(new KDTreeSoup(cacheBase + "_tree", settings_.adaptive));
	tree->setMaxMemory(settings_.treeMemory());
	tree->setMaxWeight(attempt.nodeFaces);
	tree->texelWeight = settings_.texelWeight(attempt.nodeFaces);
	tree->setTrianglesPerBlock(attempt.nodeFaces);

	// Build the hierarchy
	model.build(*tree, stream);

	// The file is written under another name in its folder and given its name only once it is
	// complete: a save that fails (or a process killed while saving) leaves no half file under
	// the output name, and does not remove one an earlier build left there.
	model.save(output, metadata);
}

void BuildPipeline::build(const std::string &input, const std::string &output, int threads,
                          const std::vector<bm3d::JsonMember> &extraMetadata) const {
	Attempt attempt;
	attempt.threads = threads > 0 ? threads : int(std::max(1u, std::thread::hardware_concurrency()));
	attempt.nodeFaces = settings_.nodeFaces;
	const FallbackPolicy policy(attempt);
	const TryFunction makeTry = makeTry_ ? makeTry_ : TryFunction([&](const Attempt &next) {
		buildOnce(input, output, next, extraMetadata);
	});
	for(;;) {
		try {
			makeTry(attempt);
			break;
		} catch(const TexturePackingError &error) {
			const auto retry = policy.after(attempt, error);
			if(!retry)
				throw;
			warn(logger_, retry->warning);
			attempt = retry->attempt;
		} catch(const NodeTooLargeError &error) {
			const FallbackPolicy::Retry retry = policy.after(attempt, error);
			warn(logger_, retry.warning);
			attempt = retry.attempt;
		}
	}

	for(const std::string &warning: policy.summary(attempt))
		warn(logger_, warning);
}

} //namespace
