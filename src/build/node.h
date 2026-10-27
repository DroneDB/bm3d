// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A node of the hierarchy while it is being built. The file keeps less of it and in another
// form (NodeRecord in src/format); the builder writes the one from the other when it saves.

#ifndef BM3D_NODE_H
#define BM3D_NODE_H

#include <cstdint>

#include <vcg/space/sphere3.h>

#include "../common/cone.h"

namespace bm3d::detail {

//A sphere with nothing in it: the radius -1 of vcg's own empty sphere, and a centre at the origin.
//vcg leaves the centre of an empty sphere unset, and the sink node at the end of the table keeps
//the one it has, so an unset centre would put whatever the stack held at that moment in the file.
inline vcg::Sphere3f emptySphere() { return vcg::Sphere3f(vcg::Point3f(0, 0, 0), -1); }

//Every field has a value from the start: the sink node at the end of the table sets only
//first_patch.
struct Node {
	uint32_t chunk = 0;        //the chunk of the builder that holds the vertices and the triangles of the node
	uint16_t nvert = 0;
	uint16_t nface = 0;
	float error = 0;
	float shortEdge = 0;       //the length a tenth of the edges of the triangles of the node, as built, do not exceed (shortedge.h)
	Cone3s cone;               //cone of normals
	vcg::Sphere3f sphere = emptySphere(); //saturated sphere (extraction)
	float tight_radius = 0;    //tight sphere (frustum culling)
	uint32_t first_patch = 0;  //index of the first patch in the array of patches.
	uint32_t last_patch() { return (this + 1)->first_patch; } //this node is ALWAYS inside an array.
	vcg::Sphere3f tightSphere() { return vcg::Sphere3f(sphere.Center(), tight_radius); }
};

}//namespace

#endif // BM3D_NODE_H
