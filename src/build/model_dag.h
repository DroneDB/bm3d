// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#ifndef BM3D_MODEL_DAG_H
#define BM3D_MODEL_DAG_H

#include <cstdint>
#include <string>
#include <vector>

#include <vcg/space/box3.h>

#include "node.h"
#include "../common/virtualarray.h"
#include "../format/bm3d_format.h"

class KDTree;

namespace bm3d::detail {

//The box of the kd-tree cell a node was cut from, with the axes of the tree level, to tell which
//of the vertices of a node lie on its border
class NodeBox {
public:
	NodeBox() {}
	NodeBox(KDTree *tree, uint32_t block);

	const vcg::Box3f &box() const { return box_; }
	bool isIn(vcg::Point3f &p);
	//which of the nvert vertices p are on a triangle (of the nface of f) with a vertex outside the box
	std::vector<bool> markBorders(const Node &node, vcg::Point3f *p, uint16_t *f);

private:
	vcg::Point3f axes_[3];
	vcg::Box3f box_;
};

//The multiresolution graph being built: the nodes (the sink, which every leaf points to, comes
//first while the levels are made; DagOps::reverse turns the order round), the patches that join a
//node to its children, the box of each node, and the chunks, the out-of-core store of the vertices
//and triangles of the nodes as they were before they were simplified.
class ModelDag {
public:
	//chunkPrefix: where the chunks are kept (see VirtualMemory); the graph starts with the sink
	ModelDag(const VertexLayout &layout, const std::string &chunkPrefix);

	const VertexLayout &layout() const { return layout_; }
	//the memory the chunks may keep mapped
	void setMaxMemory(uint64_t memory) { chunks_.setMaxMemory(memory); }

	//Adds a node to the end of the graph and returns its index. `patches` are those of the node, in
	//the order the triangles were sorted into them; they are reversed here, as the graph is reversed
	//when it is complete (the local order matters for triangleOffset).
	uint32_t addNode(Node node, uint32_t chunk, float error, std::vector<PatchRecord> patches, const NodeBox &box);

	uint32_t nodeCount() const { return uint32_t(nodes_.size()); }
	uint32_t sink() const { return nodeCount() - 1; }
	std::vector<Node> &nodes() { return nodes_; }
	const std::vector<Node> &nodes() const { return nodes_; }
	std::vector<PatchRecord> &patches() { return patches_; }
	const std::vector<PatchRecord> &patches() const { return patches_; }
	std::vector<NodeBox> &boxes() { return boxes_; }
	VirtualChunks &chunks() { return chunks_; }

private:
	VertexLayout layout_;
	VirtualChunks chunks_;
	std::vector<NodeBox> boxes_;
	std::vector<Node> nodes_;
	std::vector<PatchRecord> patches_;
};

} //namespace

#endif // BM3D_MODEL_DAG_H
