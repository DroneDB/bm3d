/*
Nexus

Copyright(C) 2012 - Federico Ponchio
ISTI - Italian National Research Council - Visual Computing Lab

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License (http://www.gnu.org/licenses/gpl.txt)
for more details.
*/
#ifndef BM3D_TMESH_H
#define BM3D_TMESH_H

#include <vcg/space/point3.h>
#include <vcg/space/color4.h>
#include "trianglesoup.h"
#include <string>

// stuff to define the TMesh
#include <vcg/complex/complex.h>

#include <vcg/math/quadric.h>

// update
#include <vcg/complex/algorithms/clean.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <vcg/complex/algorithms/update/normal.h>

// local optimization
#include <vcg/complex/algorithms/local_optimization.h>
#include <vcg/complex/algorithms/local_optimization/tri_edge_collapse_quadric.h>
#include <vcg/complex/algorithms/local_optimization/tri_edge_collapse_quadric_tex.h>


#include "node.h"
#include "../common/cone.h"
#include "../format/bm3d_format.h"

//Quadrics simplification structures.
class TVertex;
class TEdge;
class TFace;

struct TUsedTypes: public vcg::UsedTypes<vcg::Use<TVertex>::AsVertexType,vcg::Use<TEdge>::AsEdgeType,vcg::Use<TFace>::AsFaceType>{};

class TVertex  : public vcg::Vertex<
		TUsedTypes,
		vcg::vertex::VFAdj,
		vcg::vertex::Coord3f,
		vcg::vertex::Normal3f,
		vcg::vertex::TexCoord2f,
		vcg::vertex::Color4b,
		vcg::vertex::Mark,
		vcg::vertex::BitFlags> {
public:
	uint32_t node;
	TVertex() { q.SetZero(); } //TODO remove this, it's only because of a stupid assert is valid on copy
	//int position;
	vcg::math::Quadric<double> &Qd() {return q;}

private:
	vcg::math::Quadric<double> q;

};

class TVertexNodeCompare {
public:
	bool operator()(const TVertex &va, const TVertex &vb) const {
		return va.node < vb.node;
	}
};

class TVertexCompare {
public:
	bool operator()(const TVertex &va, const TVertex &vb) const {
		const vcg::Point3f &a = va.cP();
		const vcg::Point3f &b = vb.cP();
		if(a[2] < b[2]) return true;
		if(a[2] > b[2]) return false;
		if(a[1] < b[1]) return true;
		if(a[1] > b[1]) return false;
		return a[0] < b[0];
	}
};

class TEdge : public vcg::Edge< TUsedTypes> {};

class TFace: public vcg::Face<
		TUsedTypes,
		vcg::face::VFAdj,
		vcg::face::VertexRef,
		vcg::face::Normal3f,
		vcg::face::WedgeTexCoord2f,
		vcg::face::BitFlags> {
public:
	uint32_t node;
	uint32_t tex; //texture number where the wedge tex coords refer to.
};

class TMesh: public vcg::tri::TriMesh<std::vector<TVertex>, std::vector<TFace> > {
public:

	enum Simplification { QUADRICS, EDGE, CLUSTER, RANDOM };
	void load(Soup &soup);
	void lock(std::vector<bool> &locked);
	void save(Soup &soup, uint32_t node);
	void getTriangles(Triangle *triangles, uint32_t node);

	void splitSeams(const bm3d::detail::VertexLayout &layout);

	float simplify(uint32_t target_faces, Simplification method);

	bm3d::detail::Node getNode();
	uint32_t serializedSize(const bm3d::detail::VertexLayout &layout);
	//appends nodes found in the TMesh
	void serialize(unsigned char *buffer, const bm3d::detail::VertexLayout &layout, std::vector<bm3d::detail::PatchRecord> &patches);

	vcg::Sphere3f boundingSphere();
	bm3d::detail::Cone3s normalsCone();
protected:
	float randomSimplify(uint16_t target_faces);
	float quadricSimplify(uint32_t target_faces);

	float edgeLengthError();
};

typedef vcg::tri::BasicVertexPair<TVertex> TVertexPair;

class MyTriEdgeCollapse:
		public vcg::tri::TriEdgeCollapseQuadric< TMesh, TVertexPair, MyTriEdgeCollapse, vcg::tri::QInfoStandard<TVertex>  > {
public:
	typedef  vcg::tri::TriEdgeCollapseQuadric< TMesh,  TVertexPair, MyTriEdgeCollapse, vcg::tri::QInfoStandard<TVertex>  > TECQ;
	typedef  TMesh::VertexType::EdgeType EdgeType;
	inline MyTriEdgeCollapse(  const TVertexPair &p, int i, vcg::BaseParameterClass *pp) :TECQ(p,i,pp){}
};

class MyTriEdgeCollapseQTex: public vcg::tri::TriEdgeCollapseQuadricTex< TMesh, TVertexPair, MyTriEdgeCollapseQTex, vcg::tri::QuadricTexHelper<TMesh> > {
public:
	typedef  vcg::tri::TriEdgeCollapseQuadricTex< TMesh,  TVertexPair, MyTriEdgeCollapseQTex, vcg::tri::QuadricTexHelper<TMesh> > TECQ;
	inline MyTriEdgeCollapseQTex(  const TVertexPair &p, int i, vcg::BaseParameterClass *pp) :TECQ(p,i,pp){}
};

#endif // BM3D_TMESH_H
