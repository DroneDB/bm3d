// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from TMesh::splitSeams of Nexus (Copyright (C) 2012 Federico Ponchio, ISTI - Italian
// National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#ifndef BM3D_VERTEX_SPLIT_H
#define BM3D_VERTEX_SPLIT_H

#include <cstddef>
#include <vector>

namespace bm3d::detail {

//Splits the vertices of the mesh where the faces that share one disagree on what its corner holds:
//cornerKey(face, k) is what corner k of the face holds (texture and texture coordinates, colour),
//apply(vertex, key) writes it in a vertex. Each vertex gets a copy for every other key of its
//corners, appended after the vertices of the mesh in the order the faces first reach them; a mesh
//whose corners agree everywhere keeps its vertices as they are. The faces are pointed at the
//copies; the positions (and every other attribute) of the copies are those of the vertex.
template<class Key, class MeshType, class CornerKey, class Apply>
void splitVertices(MeshType &mesh, CornerKey cornerKey, Apply apply) {
	using VertexType = typename MeshType::VertexType;
	const size_t count = mesh.vert.size();
	std::vector<int> next(count, -1);       //the next copy of a vertex, -1 for none
	std::vector<VertexType> split(count);
	std::vector<Key> keys(count);
	std::vector<bool> reached(count, false);
	std::vector<int> corners;
	corners.reserve(mesh.face.size() * 3);
	for(auto &f: mesh.face) {
		for(int k = 0; k < 3; k++) {
			int index = int(f.V(k) - &*mesh.vert.begin());
			const Key key = cornerKey(f, k);
			while(true) {
				if(!reached[size_t(index)]) { //the first corner that reaches the vertex
					split[size_t(index)] = *f.V(k);
					apply(split[size_t(index)], key);
					keys[size_t(index)] = key;
					reached[size_t(index)] = true;
					break;
				}
				if(keys[size_t(index)] == key)
					break;
				if(next[size_t(index)] == -1) { //a key this vertex has no copy for yet
					const int copy = int(split.size());
					VertexType vertex = split[size_t(index)];
					apply(vertex, key);
					split.push_back(vertex);
					keys.push_back(key);
					reached.push_back(true);
					next.push_back(-1);
					next[size_t(index)] = copy;
					index = copy;
					break;
				}
				index = next[size_t(index)];
			}
			corners.push_back(index);
		}
	}
	mesh.vert = split;
	mesh.vn = int(mesh.vert.size());
	for(size_t i = 0; i < corners.size(); i += 3) {
		auto &f = mesh.face[i / 3];
		for(int k = 0; k < 3; k++)
			f.V(k) = &mesh.vert[size_t(corners[i + size_t(k)])];
	}
}

} //namespace

#endif // BM3D_VERTEX_SPLIT_H
