// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The patches of a node: its triangles grouped by the node of the next level they belong to, which
// is how the file stores them (a patch record holds the end of its run of triangles).

#ifndef BM3D_NODEPATCHES_H
#define BM3D_NODEPATCHES_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../format/bm3d_format.h"

namespace bm3d::detail {

//Sorts `faces` by their `node` and appends a patch record for each run of one node, in that order.
//The triangles of a node keep the order they had among themselves (a stable sort): the order of
//equal keys is then fixed by the standard, where an unstable sort leaves it to the library, and
//libstdc++ and MSVC order them differently, which gave different files for the same input.
//The non-textured mesh is simplified after this sort, so its order reaches the coarser levels too.
template<class FaceVector>
void sortFacesIntoPatches(FaceVector &faces, std::vector<PatchRecord> &patches) {
	std::stable_sort(faces.begin(), faces.end(),
	                 [](const auto &a, const auto &b) { return a.node < b.node; });
	if(faces.empty())
		return;

	//triangleOffset is the END of the triangles of the patch
	uint32_t current = faces[0].node;
	for(size_t i = 0; i < faces.size(); i++) {
		if(faces[i].node == current)
			continue;
		PatchRecord patch;
		patch.node = current;
		patch.triangleOffset = uint32_t(i);
		patches.push_back(patch);
		current = faces[i].node;
	}
	PatchRecord patch;
	patch.node = current;
	patch.triangleOffset = uint32_t(faces.size());
	patches.push_back(patch);
}

}//namespace

#endif // BM3D_NODEPATCHES_H
