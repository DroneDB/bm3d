// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mesh_loader_registry.h"

#include "objloader.h"
#include "plyloader.h"
#include "stlloader.h"
#include "../../common/error.h"
#include "../../common/pathutil.h"

namespace bm3d::detail {

namespace {

//"obj, ply and stl": the extensions without their dot
std::string namesOf(const std::vector<std::string> &extensions) {
	std::string names;
	for(size_t i = 0; i < extensions.size(); i++) {
		if(i > 0)
			names += i + 1 == extensions.size() ? " and " : ", ";
		names += extensions[i].substr(extensions[i].empty() ? 0 : 1);
	}
	return names;
}

MeshLoaderRegistry makeStandard() {
	MeshLoaderRegistry registry;
	registry.add(".obj", [](const std::string &file, const MeshLoaderContext &context) {
		return std::unique_ptr<MeshLoader>(new ObjLoader(file, context.materialLibrary));
	});
	registry.add(".ply", [](const std::string &file, const MeshLoaderContext &context) {
		return std::unique_ptr<MeshLoader>(new PlyLoader(file, context.scratchPrefix));
	});
	registry.add(".stl", [](const std::string &file, const MeshLoaderContext &context) {
		return std::unique_ptr<MeshLoader>(new STLLoader(file, context.logger));
	});
	return registry;
}

} //namespace

void MeshLoaderRegistry::add(const std::string &extension, Factory factory) {
	entries_.push_back({extension, std::move(factory)});
}

std::unique_ptr<MeshLoader> MeshLoaderRegistry::create(const std::string &file, const MeshLoaderContext &context) const {
	for(const Entry &entry: entries_)
		if(endsWithNoCase(file, entry.extension))
			return entry.factory(file, context);
	throw Error("unsupported input file " + file + ": the formats read are " + namesOf(extensions()) +
	            ", told by the extension of the name");
}

std::vector<std::string> MeshLoaderRegistry::extensions() const {
	std::vector<std::string> extensions;
	for(const Entry &entry: entries_)
		extensions.push_back(entry.extension);
	return extensions;
}

const MeshLoaderRegistry &MeshLoaderRegistry::standard() {
	static const MeshLoaderRegistry registry = makeStandard();
	return registry;
}

} //namespace
