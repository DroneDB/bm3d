// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#include "model_dag.h"

#include <algorithm>

#include "kdtree.h"

namespace bm3d::detail {

NodeBox::NodeBox(KDTree *tree, uint32_t block) {
	for(int k = 0; k < 3; k++)
		axes_[k] = tree->axes[k];
	box_ = tree->block_boxes[block];
}

bool NodeBox::isIn(vcg::Point3f &p) {
	return KDTree::isIn(axes_, box_, p);
}

std::vector<bool> NodeBox::markBorders(const Node &node, vcg::Point3f *p, uint16_t *f) {
	std::vector<bool> border(node.nvert, false);
	for(int i = 0; i < node.nface; i++) {
		bool outside = false;
		for(int k = 0; k < 3; k++) {
			uint16_t index = f[i*3 + k];
			outside |= !isIn(p[index]);
		}
		if(outside)
			for(int k = 0; k < 3; k++) {
				uint16_t index = f[i*3 + k];
				border[index] = true;
			}
	}
	return border;
}

ModelDag::ModelDag(const VertexLayout &layout, const std::string &chunkPrefix):
	layout_(layout), chunks_(chunkPrefix) {
	Node sink;
	sink.first_patch = 0;
	nodes_.push_back(sink);
}

uint32_t ModelDag::addNode(Node node, uint32_t chunk, float error, std::vector<PatchRecord> patches, const NodeBox &box) {
	//patches will be reverted later, but the local order is important because of triangleOffset
	const uint32_t patchOffset = patches_.size();
	std::reverse(patches.begin(), patches.end());
	patches_.insert(patches_.end(), patches.begin(), patches.end());

	const uint32_t current = nodes_.size();
	node.chunk = chunk;           //which chunk belongs to which node
	node.error = error;
	node.first_patch = patchOffset;

	nodes_.push_back(node);
	boxes_.push_back(box);
	return current;
}

} //namespace
