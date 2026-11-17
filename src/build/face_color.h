// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The colours of the corners of the faces of a node, which the meshes of the builder (Mesh, TMesh)
// keep in the same way.

#ifndef BM3D_FACE_COLOR_H
#define BM3D_FACE_COLOR_H

#include <vcg/space/color4.h>

#include "../io/mesh/trianglesoup.h"

namespace bm3d::detail {

//A colour of the input belongs to a vertex (a colour that varies over the surface: the vertex
//colours of a scan) or to a face (the colour of a material, or of an STL face: every corner of the
//face has it). The meshes merge their vertices by position, which the simplification needs, so a
//vertex shared by faces of two colours (on the border between two materials) keeps one of them,
//and the border is drawn as a sawtooth of the two. A face whose corners have one colour in the
//input keeps it as its own, through the simplification, and the vertices are split by these
//colours when the node is written, as they are by texture coordinates: the border is then where
//the faces meet. The positions are the same on both sides of a border, so there is no crack.
//
//Where the colours of a model come from, as its loaders tell. The colours of vertices are left to
//the vertices: a scan with a region of one colour, whose triangles there have corners of one colour,
//would get split vertices along the border of the region in its simplified levels, where the
//vertices have moved.
enum class ColorSource { Vertices, Faces };

struct FaceColor {
	vcg::Color4b color;
	bool own = false; //the face is drawn in `color`, and not in the colours of its vertices

	//The colour of a triangle of the input: its own when the colours of the model are those of faces
	//and its three corners have the same (a triangle of the simplified levels does)
	static FaceColor of(const Triangle &triangle, ColorSource source) {
		const unsigned char *c = triangle.vertices[0].c;
		FaceColor face;
		face.color = vcg::Color4b(c[0], c[1], c[2], c[3]);
		face.own = source == ColorSource::Faces;
		for(int k = 1; k < 3; k++)
			for(int j = 0; j < 4; j++)
				face.own = face.own && triangle.vertices[k].c[j] == c[j];
		return face;
	}
};

//The colour corner k of face f is drawn in
template<class Face>
vcg::Color4b cornerColor(const Face &f, int k) {
	return f.faceColor.own ? f.faceColor.color : f.cV(k)->cC();
}

//Writes the colour of corner k of face f in a vertex of the triangle soup
template<class Face>
void writeCornerColor(const Face &f, int k, Vertex &vertex) {
	const vcg::Color4b color = cornerColor(f, k);
	for(int j = 0; j < 4; j++)
		vertex.c[j] = color[j];
}

} //namespace

#endif // BM3D_FACE_COLOR_H
