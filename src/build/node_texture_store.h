// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#ifndef BM3D_NODE_TEXTURE_STORE_H
#define BM3D_NODE_TEXTURE_STORE_H

#include <cstddef>
#include <vector>

#include "../common/scratchfile.h"
#include "../format/bm3d_format.h"

namespace bm3d::detail {

//The encoded textures of the nodes, kept in a scratch file until the model is written. A texture is
//reserved when its node is made, so that the patches of the node can name it, and stored when it
//has been encoded; where each one is in the file is recorded.
class NodeTextureStore {
public:
	//prefix: where the scratch file goes (see ScratchFile::create)
	explicit NodeTextureStore(const std::string &prefix);

	//The index of a new texture, which has no content yet
	size_t reserve();
	//Gives the texture its content. Throws ScratchError when the file cannot be written.
	void store(size_t texture, const std::vector<unsigned char> &encoded);

	size_t count() const { return records_.size(); }
	//The content of the texture, in `into`, whose size is set
	void read(size_t texture, std::vector<unsigned char> &into);

private:
	ScratchFile file_;
	std::vector<TextureRecord> records_; //where the image of each node is in the file
};

} //namespace

#endif // BM3D_NODE_TEXTURE_STORE_H
