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
#include <assert.h>

#include "../common/error.h"
#include "../common/numeric.h"

#include "tmesh.h"
#include "nodepatches.h"
#include "shortedge.h"
#include "vertex_split.h"


using namespace std;

//using namespace vcg;

void TMesh::load(Soup &soup, bm3d::detail::ColorSource colors) {
	vcg::tri::Allocator<TMesh>::AddVertices(*this, soup.size() * 3);
	vcg::tri::Allocator<TMesh>::AddFaces(*this, soup.size());

	for(unsigned int i = 0; i < soup.size(); i++) {
		Triangle &triangle = soup[i];
		TFace &f = face[i];
		for(unsigned int k = 0; k < 3; k++) {
			Vertex &in = triangle.vertices[k];
			TVertex &v = vert[i*3 + k];
			v.P()= vcg::Point3f(in.v[0], in.v[1], in.v[2]);
			v.C() = vcg::Color4b(in.c[0], in.c[1], in.c[2], in.c[3]);
			//v.position = i*3 + k;
			f.V(k) = &v;
			f.WT(k).U() = in.t[0];
			f.WT(k).V() = in.t[1];
			assert(!isnan(in.t[0]));
			assert(!isnan(in.t[1]));
		}
		f.node = triangle.node;
		f.tex = triangle.tex;
		f.faceColor = bm3d::detail::FaceColor::of(triangle, colors);
	}

	//UNIFICATION PROCESS
	/*
	std::sort(vert.begin(), vert.end(), TVertexCompare());

	std::vector<int> remap(vert.size());
	remap[0] = 0;
	vn = 1;
	for(unsigned int i = 1; i < vert.size(); i++) {
		TVertex &current = vert[i];
		TVertex &previous = vert[vn-1];

		if(current.P() != previous.P()) {
			vert[vn++] = current;
		}
		remap[current.position] = vn-1;
	}
	vert.resize(vn);

	TVertex *begin = &*vert.begin();
	for(unsigned int i = 0; i < face.size(); i++) {
		TFace &f = face[i];
		for(int k = 0; k < 3; k++)
			f.V(k) = begin + remap[f.V(k) - begin];
	}

	savePly("test.ply"); */
	vcg::tri::Clean<TMesh>::RemoveDuplicateVertex(*this);
	vcg::tri::Allocator<TMesh>::CompactVertexVector(*this);
	vcg::tri::Allocator<TMesh>::CompactFaceVector(*this);
	vcg::tri::UpdateNormal<TMesh>::PerVertex(*this);
}

void TMesh::lock(std::vector<bool> &locked) {
	for(unsigned int i = 0; i < face.size(); i++)
		if(locked[i])
			face[i].ClearW();
}


void TMesh::save(Soup &soup, uint32_t node) {
	for(unsigned int i = 0; i < face.size(); i++) {
		Triangle triangle;
		TFace &t = face[i];
		for(unsigned int k = 0; k < 3; k++) {
			Vertex &vertex = triangle.vertices[k];
			TVertex &v = *t.V(k);
			vertex.v[0] = v.P()[0];
			vertex.v[1] = v.P()[1];
			vertex.v[2] = v.P()[2];
			bm3d::detail::writeCornerColor(t, k, vertex);
		}
		triangle.node = node;
		triangle.tex = t.tex;
		soup.push_back(triangle);
	}
}

void TMesh::getTriangles(Triangle *triangles, uint32_t node) {
	int count = 0;
	for(unsigned int i = 0; i < face.size(); i++) {
		TFace &t= face[i];
		if(t.IsD()) continue;

		Triangle &triangle = triangles[count++];
		for(unsigned int k = 0; k < 3; k++) {
			Vertex &vertex = triangle.vertices[k];
			TVertex &v = *t.V(k);
			vertex.v[0] = v.P()[0];
			vertex.v[1] = v.P()[1];
			vertex.v[2] = v.P()[2];
			bm3d::detail::writeCornerColor(t, k, vertex);
			vertex.t[0] = t.WT(k).U();
			vertex.t[1] = t.WT(k).V();
		}
		triangle.node = node;
		triangle.tex = t.tex;
	}
}

float TMesh::simplify(uint32_t target_faces, Simplification method) {

	//lock border triangles
	for(unsigned int i = 0; i < face.size(); i++)
		if(!face[i].IsW())
			for(int k = 0; k < 3; k++)
				face[i].V(k)->ClearW();

	float error = -1;
	switch(method) {
	case RANDOM: error = randomSimplify(target_faces); break;
	case QUADRICS: error = quadricSimplify(target_faces); break;
	default: throw bm3d::detail::Error("unknown simplification method");
	}

	//unlock everything
	for(unsigned int i = 0; i < vert.size(); i++)
		vert[i].SetW();
	for(unsigned int i = 0; i < face.size(); i++)
		face[i].SetW();

	return error;
}

bm3d::detail::Node TMesh::getNode()
{
	bm3d::detail::Node node;
	node.chunk = -1;
	node.nface = face.size();
	node.nvert = vert.size();
	node.error = -1;
	node.shortEdge = bm3d::detail::shortEdgeLength(*this);
	node.first_patch = -1;
	node.sphere = boundingSphere();
	node.tight_radius = node.sphere.Radius();
	node.cone = normalsCone();
	return node;
}
namespace {

//What the corner of a face holds in a node: its texture, its texture coordinates and its colour
struct TexturedCorner {
	uint32_t tex = 0;
	vcg::TexCoord2f uv;
	vcg::Color4b color;
	bool operator==(const TexturedCorner &other) const {
		return tex == other.tex && uv == other.uv && color == other.color;
	}
};

} //namespace

//we have textures stored on wedge texture coordinates, we split vertices where wedges coords are different.
void TMesh::splitSeams(const bm3d::detail::VertexLayout &layout) {
	if(layout.normals)
		vcg::tri::UpdateNormal<TMesh>::PerVertexNormalized(*this);

	const bool colors = layout.colors;
	bm3d::detail::splitVertices<TexturedCorner>(*this,
		[colors](const TFace &f, int k) {
			TexturedCorner corner;
			corner.tex = f.tex;
			corner.uv = f.cWT(k);
			corner.color = colors ? bm3d::detail::cornerColor(f, k) : vcg::Color4b(0, 0, 0, 0);
			return corner;
		},
		[colors](TVertex &v, const TexturedCorner &corner) {
			v.T() = corner.uv;
			if(colors)
				v.C() = corner.color;
		});
}

void TMesh::copyFrom(TMesh &other) {
	vcg::tri::Append<TMesh, TMesh>::MeshCopy(*this, other);
	for(size_t i = 0; i < face.size(); i++) {
		face[i].node = other.face[i].node;
		face[i].tex = other.face[i].tex;
		face[i].faceColor = other.face[i].faceColor;
	}
}

uint32_t TMesh::serializedSize(const bm3d::detail::VertexLayout &layout) {
	//This should take into account duplicated vertices due to texture seams.
	//let's created the replicated vertices.
	assert(vn == (int)vert.size());
	assert(fn == (int)face.size());
	uint16_t nvert = vn;
	uint16_t nface = fn;
	uint32_t size = nvert*bm3d::detail::decodedVertexBytes(layout) + nface*bm3d::detail::kDecodedTriangleBytes;
	return size;
}

void TMesh::serialize(unsigned char *buffer, const bm3d::detail::VertexLayout &layout, std::vector<bm3d::detail::PatchRecord> &patches) {
	assert(vn == (int)vert.size());
	assert(fn == (int)face.size());

	sortFacesIntoPatches(face, patches);

	vcg::Point3f *c = (vcg::Point3f *)buffer;
	for(int i = 0; i < vn; i++)
		c[i] = vert[i].P();
	buffer += vert.size()*sizeof(vcg::Point3f);

	if(layout.texCoords) {
		vcg::Point2f *tstart = (vcg::Point2f *)buffer;
		for(int i = 0; i < vn; i++) {
			//assert(vert[i].T().P()[0] >= 0.0 && vert[i].T().P()[0] <= 1.0);
			tstart[i] = vert[i].T().P();
		}
		buffer += vert.size() * sizeof(vcg::Point2f);
	}

	if(layout.normals) {
		vcg::Point3s *nstart = (vcg::Point3s *)buffer;
		for(int i = 0; i < vn; i++) {
			vcg::Point3f nf = vert[i].N();
			nf.Normalize();
			vcg::Point3s &ns = nstart[i];
			for(int k = 0; k < 3; k++)
				ns[k] = (short)(nf[k]*32766);
		}
		buffer += vert.size() * sizeof(vcg::Point3s);
	}

	if(layout.colors) {
		vcg::Color4b *cstart = (vcg::Color4b *)buffer;
		for(int i = 0; i < vn; i++)
			cstart[i] = vert[i].C();
		buffer += vert.size() * sizeof(vcg::Color4b);
	}

	uint16_t *faces = (uint16_t *)buffer;
	for(int i = 0; i < fn; i++) {
		TFace &f = face[i];
		for(int k = 0; k < 3; k++) {
			TVertex *v = f.V(k);
			faces[i*3 + k] = v - &*vert.begin();
		}
		//cout << endl;
	}
}

vcg::Sphere3f TMesh::boundingSphere() {
	std::vector<vcg::Point3f> vertices(vert.size());
	for(uint32_t i = 0; i < vert.size(); i++)
		vertices[i] = vert[i].P();
	vcg::Sphere3f sphere;
	sphere.CreateTight(vertices);
	return sphere;
}

bm3d::detail::Cone3s TMesh::normalsCone() {
	std::vector<vcg::Point3f> normals;
	normals.reserve(face.size());
	//compute face normals
	for(unsigned int i = 0; i < face.size(); i++) {
		TFace &f = face[i];
		vcg::Point3f &v0 = f.V(0)->P();
		vcg::Point3f &v1 = f.V(1)->P();
		vcg::Point3f &v2 = f.V(2)->P();

		vcg::Point3f norm = (v1 - v0) ^ (v2 - v0);
		float len = norm.Norm();
		float l1 = (v1 - v0).Norm();
		float l2 = (v2 - v0).Norm();
		float bigger = l1>l2?l1:l2;
		//skip very thin faces.
		if(bm3d::detail::fuzzyCompare(bigger, bigger + len))
			continue;
		norm /= len;
		normals.push_back(norm);
	}
	if(normals.size() == 0) {
		return bm3d::detail::Cone3s();
	}
	bm3d::detail::AnchoredCone3f acone;
	acone.AddNormals(normals, 0.95f);

	bm3d::detail::Cone3s cone;
	cone.Import(acone);
	return cone;
}

float TMesh::randomSimplify(uint16_t /*target_faces*/) {
	assert(0);
	return -1;
}

float TMesh::quadricSimplify(uint32_t target) {
	vcg::tri::UpdateNormal<TMesh>::PerFaceNormalized(*this);
	//degenerate norms will create problems in quadri simplification it seems.
	for(auto &f: face) {
		if(f.N().Norm() < 0.01)
			f.N() = vcg::Point3f(1, 0, 0);
	}
	vcg::tri::UpdateTopology<TMesh>::VertexFace(*this);


	vcg::tri::TriEdgeCollapseQuadricTexParameter qparams;
	//vcg::tri::TriEdgeCollapseQuadricParameter qparams;
	qparams.SetDefaultParams();
	qparams.NormalCheck = true;


	vcg::math::Quadric<double> QZero;
	QZero.SetZero();
	vcg::tri::QuadricTexHelper<TMesh>::QuadricTemp TD3(this->vert,QZero);
	vcg::tri::QuadricTexHelper<TMesh>::TDp3()=&TD3;

	std::vector<std::pair<vcg::TexCoord2<float>, vcg::Quadric5<double> > > qv;

	vcg::tri::QuadricTexHelper<TMesh>::Quadric5Temp TD(this->vert,qv);
	vcg::tri::QuadricTexHelper<TMesh>::TDp()=&TD;



	vcg::LocalOptimization<TMesh> DeciSession(*this, &qparams);

	DeciSession.Init<MyTriEdgeCollapseQTex>();
	DeciSession.SetTargetSimplices(target);
	DeciSession.DoOptimization();
	DeciSession.Finalize<MyTriEdgeCollapseQTex>();

	return edgeLengthError();
}

float TMesh::edgeLengthError() {
	if(!face.size()) return 0;
	float error = 0;
	int recount = 0;
	for(unsigned int i = 0; i < face.size(); i++) {
		TFace &f = face[i];
		if(f.IsD()) continue;
		for(int k = 0; k < 3; k++) {
			float err = (f.cV(k)->cP() - f.cV((k+1)%3)->cP()).SquaredNorm();
			error += err;
			recount++;
		}
	}
	return sqrt(error/recount);
}


