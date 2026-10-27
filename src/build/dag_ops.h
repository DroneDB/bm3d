// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#ifndef BM3D_DAG_OPS_H
#define BM3D_DAG_OPS_H

#include <cstdint>
#include <vector>

#include <vcg/space/point2.h>
#include <vcg/space/point3.h>

#include "model_dag.h"

namespace bm3d::detail {

//A vertex on the border of a node, with the normal it holds in the chunk of the node
class NVertex {
public:
	NVertex(uint32_t b, uint32_t i, vcg::Point3f p, vcg::Point3s *n):
		node(b), index(i), point(p), normal(n) {}
	uint32_t node;
	uint32_t index;
	vcg::Point3f point;
	vcg::Point3s *normal;

	//A total order: by point, then the highest node first (the leafest node), then by index, so that
	//no two vertices compare equal. With equal points and nodes left equal, the order the sort gave
	//them was the library's own (libstdc++ and MSVC differ), and it decides which normal is copied
	//and in what order the normals of a point are added up.
	bool operator<(const NVertex &v) const {
		if(point != v.point)
			return point < v.point;
		if(node != v.node)
			return node > v.node;
		return index < v.index;
	}
};

//The operations on the graph once all its levels are made, in the order they are applied:
//reverse() puts the nodes in the order of the file (a parent before its children), saturate()
//makes a node's error and sphere cover its children's, uniformNormals() gives the vertices that
//several nodes share one normal.
class DagOps {
public:
	explicit DagOps(ModelDag &dag): dag_(dag) {}

	//The levels were made from the leaves up, with the sink first: this reverses the nodes, the
	//boxes and the patches, and renumbers what points to them.
	void reverse();

	//Makes the error of every node larger than its children's and its sphere contain theirs. The
	//graph must be reversed; the nodes are ordered such that a child comes always after its parent.
	void saturate();

	//Gives the same normal to the vertices at one point that lie in different nodes. In the leaves
	//the normals of all the vertices there are averaged; above them the vertex of the child
	//(the leafest node) is copied.
	void uniformNormals();

private:
	void saturateNode(uint32_t node);
	//Appends the vertices of `origin` that lie on its border and, if `destination` is another node, are
	//inside the box of `destination`
	void appendBorderVertices(uint32_t origin, uint32_t destination, std::vector<NVertex> &vertices);

	ModelDag &dag_;
};

} //namespace

#endif // BM3D_DAG_OPS_H
