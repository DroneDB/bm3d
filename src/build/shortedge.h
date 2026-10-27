// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// How fine the triangles of a node are, for the step the coordinates are quantised to.

#ifndef BM3D_SHORTEDGE_H
#define BM3D_SHORTEDGE_H

#include <algorithm>
#include <cmath>
#include <vector>

namespace bm3d::detail {

//The length that a tenth of the edges of the triangles of a mesh (the three sides of every face that
//is not deleted, the edges of the faces they share counted once for each) do not exceed, leaving out
//the edges of no length; 0 when none has a length. The step of the coordinates follows the short
//edges and not the mean: the mean of a part with a few small features (a chamfer along a long edge)
//is the length of its large faces, and a step that is a fraction of it rounds the chamfer off. The
//value is one of the lengths and does not depend on the order they are in; the square root is
//exactly rounded, so it is the same in every library.
template <class MeshType>
float shortEdgeLength(MeshType &mesh) {
	std::vector<float> lengths;
	lengths.reserve(mesh.face.size() * 3);
	for(auto &f: mesh.face) {
		if(f.IsD()) continue;
		for(int k = 0; k < 3; k++) {
			const float length = (f.cV(k)->cP() - f.cV((k + 1) % 3)->cP()).Norm();
			if(length > 0 && std::isfinite(length))
				lengths.push_back(length);
		}
	}
	if(lengths.empty()) return 0;
	const size_t tenth = (lengths.size() - 1) / 10;
	std::nth_element(lengths.begin(), lengths.begin() + tenth, lengths.end());
	return lengths[tenth];
}

}//namespace

#endif // BM3D_SHORTEDGE_H
