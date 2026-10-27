// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#include "dag_ops.h"

#include <algorithm>

namespace bm3d::detail {

void DagOps::reverse() {
	std::vector<Node> &nodes = dag_.nodes();
	std::vector<PatchRecord> &patches = dag_.patches();

	std::reverse(nodes.begin(), nodes.end());
	std::reverse(dag_.boxes().begin(), dag_.boxes().end());
	std::reverse(patches.begin(), patches.end());

	//first reversal: but we point now to the last_patch, not the first
	for(unsigned int i = 0; i < nodes.size(); i++)
		nodes[i].first_patch = patches.size() -1 - nodes[i].first_patch;

	//get the previous node last +1 to become the first
	for(unsigned int i = nodes.size()-1; i >= 1; i--)
		nodes[i].first_patch = nodes[i-1].first_patch +1;
	nodes[0].first_patch  = 0;

	//and reversed the order of the nodes.
	for(unsigned int i = 0; i < patches.size(); i++) {
		patches[i].node = nodes.size() - 1 - patches[i].node;
	}
}

void DagOps::saturate() {
	std::vector<Node> &nodes = dag_.nodes();
	//we do not have the 'backlinks' so we make a depth first traversal
	for(int node = nodes.size()-2; node >= 0; node--)
		saturateNode(node);

	nodes.back().error = 0;
}

//include sphere of the children and ensure error s bigger.
void DagOps::saturateNode(uint32_t n) {
	const float epsilon = 1.01f;

	std::vector<Node> &nodes = dag_.nodes();
	std::vector<PatchRecord> &patches = dag_.patches();
	Node &node = nodes[n];
	for(uint32_t i = node.first_patch; i < node.last_patch(); i++) {
		PatchRecord &patch = patches[i];
		if(patch.node == nodes.size()-1) //sink, get out
			return;

		Node &child = nodes[patch.node];
		if(node.error <= child.error)
			node.error = child.error*epsilon;

		//we cannot just add the sphere, because it moves the center and the tight radius will be wrong
		if(!node.sphere.IsIn(child.sphere)) {
			float dist = (child.sphere.Center() - node.sphere.Center()).Norm();
			dist += child.sphere.Radius();
			if(dist > node.sphere.Radius())
				node.sphere.Radius() = dist;
		}
	}
	node.sphere.Radius() *= epsilon;
}

/* extracts vertices in origin which intersects destination box */
void DagOps::appendBorderVertices(uint32_t origin, uint32_t destination, std::vector<NVertex> &vertices) {
	const VertexLayout &layout = dag_.layout();
	Node &node = dag_.nodes()[origin];
	uint32_t chunk = node.chunk;

	//if origin != destination do not flush cache, it would invalidate pointers.
	unsigned char *buffer = dag_.chunks().getChunk(chunk, origin != destination);

	vcg::Point3f *point = (vcg::Point3f *)buffer;
	int size = sizeof(vcg::Point3f) + layout.texCoords*sizeof(vcg::Point2f);
	vcg::Point3s *normal = (vcg::Point3s *)(buffer + size * node.nvert);
	uint16_t *face = (uint16_t *)(buffer + decodedVertexBytes(layout)*node.nvert);

	NodeBox &nodebox = dag_.boxes()[origin];

	std::vector<bool> border = nodebox.markBorders(node, point, face);
	for(int i = 0; i < node.nvert; i++) {
		if(border[i])
			vertices.push_back(NVertex(origin, i, point[i], normal + i));
	}
}

void DagOps::uniformNormals() {
	/*
	level 0: for each node in the lowest level:
			load the neighboroughs
			find common vertices (use lock to find the vertices)
			average normals

	level > 0: for every other node (goind backwards)
			load child nodes
			find common vertices (use lock, it's way faster!)
			copy normal
	*/
	std::vector<Node> &nodes = dag_.nodes();
	std::vector<PatchRecord> &patches = dag_.patches();
	std::vector<NodeBox> &boxes = dag_.boxes();

	std::vector<NVertex> vertices;

	uint32_t sink = nodes.size()-1;
	for(int t = sink-1; t > 0; t--) {
		Node &target = nodes[t];

		vcg::Box3f box = boxes[t].box();
		box.Offset(box.Diag()/10);

		vertices.clear();
		appendBorderVertices(t, t, vertices);

		bool last_level = (patches[target.first_patch].node == sink);

		if(last_level) {//find neighboroughs among the same level

			for(int n = t-1; n >= 0; n--) {
				Node &node = nodes[n];
				if(patches[node.first_patch].node != sink) continue;
				if(!box.Collide(boxes[n].box())) continue;

				appendBorderVertices(n, t, vertices);
			}

		} else { //again among childrens.

			for(unsigned int p = target.first_patch; p < target.last_patch(); p++) {
				unsigned int n = patches[p].node;

				appendBorderVertices(n, t, vertices);
			}
		}

		if(!vertices.size()) { //this is possible, there might be no border at all.
			continue;
		}

		std::sort(vertices.begin(), vertices.end());

		unsigned int start = 0;
		while(start < vertices.size()) {
			NVertex &v = vertices[start];

			unsigned int last = start+1;
			while(last < vertices.size() && vertices[last].point == v.point)
				last++;

			if(last_level && last - start > 1) { //average all normals
				vcg::Point3f normalf(0, 0, 0);
				for(unsigned int k = start; k < last; k++) {
					for(int l = 0; l < 3; l++)
						normalf[l] += (*vertices[k].normal)[l];
				}
				normalf.Normalize();
				//convert back to shorts
				vcg::Point3s normals;
				for(int l = 0; l < 3; l++)
					normals[l] = (short)(normalf[l]*32766);

				for(unsigned int k = start; k < last; k++)
					*vertices[k].normal = normals;

			} else //just copy from first one (coming from lower level due to sorting
				for(unsigned int k = start; k < last; k++)
					*vertices[k].normal =*v.normal;

			start = last;
		}
	}
}

} //namespace
