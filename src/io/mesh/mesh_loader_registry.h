// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The loaders of mesh files by the extension of the name. A format is added by registering its
// loader here; nothing that reads a mesh names a format.

#ifndef BM3D_MESH_LOADER_REGISTRY_H
#define BM3D_MESH_LOADER_REGISTRY_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "meshloader.h"
#include "../../common/logger.h"

namespace bm3d::detail {

//What a loader may need besides the file it reads
struct MeshLoaderContext {
	//OBJ: the material library to use instead of the one the model names; empty: the model's
	std::string materialLibrary;
	//PLY: where the scratch file of the vertices goes (see VirtualMemory)
	std::string scratchPrefix;
	//where the warnings of a loader go
	Logger logger;
};

class MeshLoaderRegistry {
public:
	using Factory = std::function<std::unique_ptr<MeshLoader>(const std::string &file, const MeshLoaderContext &context)>;

	//Adds the loader of the files whose name ends with `extension` (with its dot, ".obj"; any
	//case). When the endings of two overlap, the one added first is used.
	void add(const std::string &extension, Factory factory);

	//The loader for the format of a file, told by the extension of its name. Throws Error, naming
	//the formats that are read, when there is none.
	std::unique_ptr<MeshLoader> create(const std::string &file, const MeshLoaderContext &context) const;

	//the extensions added, in the order they were, with their dot
	std::vector<std::string> extensions() const;

	//OBJ, PLY and STL
	static const MeshLoaderRegistry &standard();

private:
	struct Entry {
		std::string extension;
		Factory factory;
	};
	std::vector<Entry> entries_;
};

} //namespace

#endif // BM3D_MESH_LOADER_REGISTRY_H
