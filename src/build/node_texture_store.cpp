// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#include "node_texture_store.h"

namespace bm3d::detail {

NodeTextureStore::NodeTextureStore(const std::string &prefix) {
	file_.create(prefix);
}

size_t NodeTextureStore::reserve() {
	records_.push_back(TextureRecord());
	return records_.size() - 1;
}

void NodeTextureStore::store(size_t texture, const std::vector<unsigned char> &encoded) {
	records_[texture].offset = file_.append(encoded.data(), encoded.size());
	records_[texture].length = encoded.size();
}

void NodeTextureStore::read(size_t texture, std::vector<unsigned char> &into) {
	const TextureRecord &record = records_[texture];
	into.resize(record.length);
	file_.read(record.offset, into.data(), into.size());
}

} //namespace
