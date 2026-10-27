// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The whole of a build: reads the input, makes the model, writes the file, and tries again with
// other settings when the input can be built round (see FallbackPolicy).

#ifndef BM3D_BUILD_PIPELINE_H
#define BM3D_BUILD_PIPELINE_H

#include <functional>
#include <string>
#include <vector>

#include "build_settings.h"
#include "fallback_policy.h"
#include "../common/logger.h"
#include "../format/json_object.h"
#include "../io/mesh/mesh_loader_registry.h"

class StreamSoup;

namespace bm3d::detail {

//The errors it throws tell whose fault a failure is by their type: InputError for the input (a mesh
//or a texture that is missing or cannot be read), ArgumentError for a block of metadata that does
//not fit in a file, and anything else for a build that failed after the input was read, or for
//memory or scratch space that ran out (ScratchError, std::bad_alloc).
class BuildPipeline {
public:
	//One try at the build, given the settings of the try. It throws what a try that fails throws.
	using TryFunction = std::function<void(const Attempt &)>;

	//logger gets the warnings (see Logger). The loaders of `loaders` read the input; it must outlive the pipeline.
	//makeTry: what makes a try, for a test of the retries that has no input which fails in the way
	//wanted; the pipeline builds the model itself when it is empty.
	BuildPipeline(const BuildSettings &settings, const Logger &logger,
	              const MeshLoaderRegistry &loaders = MeshLoaderRegistry::standard(),
	              const TryFunction &makeTry = TryFunction());

	//Builds the model in the file `input` into the file `output`. threads: the threads the build may
	//keep busy, 0 for one per core. extraMetadata: the members to store in the metadata block after
	//the ones bm3d writes. The output is written under another name and renamed when it is complete:
	//nothing is left at its name when the build fails, and a file that was there is left alone.
	void build(const std::string &input, const std::string &output, int threads,
	           const std::vector<bm3d::JsonMember> &extraMetadata) const;

	//What is wrong with the members to store in the metadata block, or an empty string: that they
	//make a block that does not fit in a file whatever the input is (the smallest block bm3d writes,
	//with these members)
	std::string metadataProblem(const std::vector<bm3d::JsonMember> &extraMetadata) const;

private:
	//One try. Each try that fails leaves nothing behind: it fails while building the hierarchy, and
	//its scratch files and partial outputs go with it, so the next one starts from scratch.
	void buildOnce(const std::string &input, const std::string &output, const Attempt &attempt,
	               const std::vector<bm3d::JsonMember> &extraMetadata) const;
	//The metadata block: the settings that made the file, and what was read of the input. It is not
	//part of the content identity of the file (docs/FORMAT.md). Throws ArgumentError when it does not fit
	//in a file, which is the fault of the extra members, since the rest is a few hundred bytes.
	std::string composeMetadata(const Attempt &attempt, const std::string &input, StreamSoup *stream,
	                            const std::vector<bm3d::JsonMember> &extraMetadata) const;

	BuildSettings settings_;
	Logger logger_;
	const MeshLoaderRegistry &loaders_;
	TryFunction makeTry_;
};

} //namespace

#endif // BM3D_BUILD_PIPELINE_H
