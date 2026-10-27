// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#ifndef BM3D_MODEL_SAVER_H
#define BM3D_MODEL_SAVER_H

#include <string>

#include "model_dag.h"
#include "node_texture_store.h"
#include "../compress/compressor.h"

namespace bm3d::detail {

//Writes the finished graph to a .bm3d file: finds the roots, sets the coordinate step of the model
//from the errors of the roots and of the leaves, compresses each node with corto, and hands the
//nodes, the patches and the textures to the Bm3dWriter, which names the file only once it is complete.
class ModelSaver {
public:
	explicit ModelSaver(const CompressionSettings &compression): compression_(compression) {}

	//Writes the model to the file `filename`, nodes compressed as the settings say; `metadata` is the
	//JSON object of the metadata block (see Bm3dWriter::finish). The file is written under another
	//name and renamed when it is complete. The graph is reversed and saturated; the errors of its
	//roots are replaced by their radius.
	void save(ModelDag &dag, NodeTextureStore &textures, const std::string &filename, const std::string &metadata) const;

private:
	CompressionSettings compression_;
};

} //namespace

#endif // BM3D_MODEL_SAVER_H
