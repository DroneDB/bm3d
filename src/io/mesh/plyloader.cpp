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
#include "plyloader.h"
#include <math.h>
#include <algorithm>
#include "../../common/error.h"
#include "../../common/pathutil.h"
#include "../../common/textparse.h"
#include "finite.h"
using namespace vcg;
using namespace vcg::ply;

using namespace std;

//Ugly ply stuff from vcg.

//The most vertices (and the most u, v pairs) a face may have. plylib reads a list into f and t
//as long as the file says it is (an ASCII file, or a binary one whose lists have int lengths, can
//say anything), so the descriptors below tell it how much room there is and a longer list fails
//the read.
static const int MAX_FACE_VERTICES = 255;

struct PlyFace {
	uint32_t f[MAX_FACE_VERTICES];
	float t[2*MAX_FACE_VERTICES]; //a u, v pair per vertex
	uint32_t texNumber;
	int n;  //number of vertex indices in the file, read into f when it fits
	int nt; //number of texture coordinates in the file, read into t when it fits
};

struct PlyVertex {
	double v[3]; //read as a double whatever the type in the file: every scalar type of PLY converts to it exactly
	float t[2]; //texture
	float n[3];
	unsigned char c[4]; //colors
};

//The colour channels of a vertex: an uchar each, under either set of names (the first set that the
//file has a red of is the one read; a channel the file lacks is left as it is, see cacheVertices)
static const char *const vertexColorNames[][4] = {
	{ "red", "green", "blue", "alpha" },
	{ "diffuse_red", "diffuse_green", "diffuse_blue", nullptr }
};

//The names a texture coordinate of a vertex goes by in PLY files; the first pair that the file has
//is the one read
static const char *const vertexUvNames[][2] = {
	{ "s", "t" },
	{ "u", "v" },
	{ "texture_u", "texture_v" }
};

PropDescriptor plyprop5[1]=	{
	{"face", "texnumber",T_INT,T_INT,offsetof(PlyFace, texNumber), 0,0,0,0,0,0}
};


static bool isIntegerType(int type) {
	return type == T_CHAR || type == T_UCHAR || type == T_SHORT || type == T_USHORT || type == T_INT || type == T_UINT;
}

static bool isRealType(int type) {
	return type == T_FLOAT || type == T_DOUBLE;
}

//The property `name` of the element `element`, or null when the file has no such element or the
//element no such property
static PlyProperty *findProperty(PlyFile &pf, const char *element, const char *name) {
	for(PlyElement &e: pf.elements)
		if(e.name == element)
			return e.FindProp(name);
	return nullptr;
}

//A vertex property is read as the type the header declares, whatever its place among the others:
//plylib reads a property only when it is told the type of the file, and a coordinate may be any
//scalar type (a double x, a float y and a short z are one valid vertex). memtype is the type it is
//stored as at offset in PlyVertex (T_FLOAT or T_DOUBLE, which every type of the file converts to;
//T_UCHAR for a colour, read only from an uchar of the file: a float colour is not a byte).
//False when the vertex has no such scalar property, or it cannot be stored as memtype.
static bool addVertexScalar(PlyFile &pf, const char *name, int memtype, size_t offset) {
	const PlyProperty *property = findProperty(pf, "vertex", name);
	if(!property || property->islist)
		return false;
	const bool storable = isRealType(memtype) ? (isIntegerType(property->tipo) || isRealType(property->tipo))
	                                          : property->tipo == memtype;
	if(!storable)
		return false;
	return pf.AddToRead("vertex", name, property->tipo, memtype, offset, 0, 0, 0, 0, 0) == vcg::ply::E_NOERROR;
}

//The face lists are read as the types the header declares, the lengths included: most files give
//the length as an uchar, but a list of any integer length type (or of any integer index type) is
//valid PLY. False when the face has no such list, or its items or length are not of the kind
//asked for (memtype says which: T_UINT for vertex indices, T_FLOAT for texture coordinates).
static bool addFaceList(PlyFile &pf, const char *name, int memtype, size_t offset, size_t offsetLength, size_t maxlist) {
	const PlyProperty *property = findProperty(pf, "face", name);
	if(!property || !property->islist || !isIntegerType(property->tipoindex))
		return false;
	if(memtype == T_UINT ? !isIntegerType(property->tipo) : !isRealType(property->tipo))
		return false;
	return pf.AddToRead("face", name, property->tipo, memtype, offset, 1, 0,
	                    property->tipoindex, T_INT, offsetLength, maxlist) == vcg::ply::E_NOERROR;
}

//what an error of PlyFile::Open says about the file: plylib reads the header a line at a time
//into a buffer of 512 bytes, so a longer line (a long comment) is cut in two and the rest is
//not a header line
static std::string headerProblem(int error) {
	switch(error) {
	case vcg::ply::E_CANTOPEN:
		return "cannot be opened";
	case vcg::ply::E_NOTHEADER:
		return "does not start with \"ply\"";
	case vcg::ply::E_UNESPECTEDEOF:
		return "ends inside its header";
	case vcg::ply::E_NOFORMAT:
		return "has no format line";
	case vcg::ply::E_SYNTAX:
		return "has a header line that is not valid, or is longer than 511 characters";
	default:
		return "has a header that is not valid (plylib error " + std::to_string(error) + ")";
	}
}

PlyLoader::PlyLoader(const std::string &filename, const std::string &cachePrefix, const bm3d::detail::Logger &logger):
	filename(filename),
	vertices_element(-1),
	faces_element(-1),
	vertices(cachePrefix),
	n_vertices(0),
	n_triangles(0),
	current_triangle(0),
	face(new PlyFace()) {

	int val = pf.Open(bm3d::detail::systemPath(filename).c_str(), PlyFile::MODE_READ);
	if(val == -1)
		throw bm3d::detail::Error("ply file " + filename + " " + headerProblem(pf.GetError()));
	for(const std::string &line: pf.ignoredLines)
		bm3d::detail::warn(logger, "ply file " + filename + " has a line in its header that is not a keyword of the format; "
		                   "it is ignored: " + line);
	init();
	for(int co = 0; co < int(pf.comments.size()); ++co) {
		std::string TFILE = "TEXTUREFILE";
		std::string &c = pf.comments[co];
		std::string bufstr,bufclean;
		int i,n;

		std::string start = c.substr(0,TFILE.length());
		for_each(start.begin(), start.end(), [](char& in){ in = ::toupper(in); });

		if( TFILE == start )
		{
			bufstr = c.size() > TFILE.length() ? c.substr(TFILE.length()+1) : std::string();
			n = static_cast<int>(bufstr.length());
			for(i=0;i<n;i++)
				if(bufstr[i]!='\t' && bufstr[i]>=32 && bufstr[i]<=126 )	bufclean.push_back(bufstr[i]);

			//a string, not a buffer: the comment and the name of the file are any length
			LoadTexture tex;
			tex.filename = bm3d::detail::text::trimmed(ply::interpret_texture_name(bufclean, filename));
			texture_filenames.push_back(tex);
		}
	}
	if(has_textures && texture_filenames.size() == 0)
		has_textures = false;

	for(auto &tex: texture_filenames) {
		sanitizeTextureFilepath(tex.filename);
		resolveTextureFilepath(filename, tex.filename);
	}
}

PlyLoader::~PlyLoader() {
	pf.Destroy();
}

void PlyLoader::init() {
	bool has_faces = false;
	for(unsigned int i = 0; i < pf.elements.size(); i++) {

		if(!strcmp(pf.ElemName(i),"vertex")) {
			n_vertices = pf.ElemNumber(i);
			vertices_element = i;

		} else if( !strcmp(pf.ElemName(i),"face") ) {
			n_triangles = pf.ElemNumber(i);
			if (n_triangles) {
				faces_element = i;
				has_faces = true;
			}
		}
	}

	//the coordinates are required, each of any scalar type
	const char *const coordinateNames[3] = { "x", "y", "z" };
	for(int k = 0; k < 3; k++)
		if(!addVertexScalar(pf, coordinateNames[k], T_DOUBLE, offsetof(PlyVertex, v[0]) + k*sizeof(double)))
			throw bm3d::detail::Error("ply file " + filename + " has no x, y and z coordinates for its vertices");

	//the colour, when the file has a red; the alpha is optional: a vertex without one is opaque
	//(see cacheVertices)
	for(const auto &names: vertexColorNames) {
		if(!addVertexScalar(pf, names[0], T_UCHAR, offsetof(PlyVertex, c[0])))
			continue;
		for(int k = 1; k < 4; k++)
			if(names[k])
				addVertexScalar(pf, names[k], T_UCHAR, offsetof(PlyVertex, c[0]) + k);
		has_colors = true;
		break;
	}

	for(const auto &names: vertexUvNames) {
		if(!addVertexScalar(pf, names[0], T_FLOAT, offsetof(PlyVertex, t[0])))
			continue;
		addVertexScalar(pf, names[1], T_FLOAT, offsetof(PlyVertex, t[1]));
		has_textures = true;
		has_vertex_tex_coords = true;
		break;
	}

	if(!has_faces) { //skip normals for triangle mesh
		if(addVertexScalar(pf, "nx", T_FLOAT, offsetof(PlyVertex, n[0]))) {
			addVertexScalar(pf, "ny", T_FLOAT, offsetof(PlyVertex, n[1]));
			addVertexScalar(pf, "nz", T_FLOAT, offsetof(PlyVertex, n[2]));
			has_normals = true;
		}
	}

	for(const char *name: { "vertex_index", "vertex_indices" })
		addFaceList(pf, name, T_UINT, offsetof(PlyFace, f[0]), offsetof(PlyFace, n), MAX_FACE_VERTICES);

	//fails silently if no texture is present
	if (addFaceList(pf, "texcoord", T_FLOAT, offsetof(PlyFace, t[0]), offsetof(PlyFace, nt), 2*MAX_FACE_VERTICES)) {
		has_textures = true;
		has_face_tex_coords = true;
	}

	if (pf.AddToRead(plyprop5[0]) == vcg::ply::E_NOERROR) { }

	pf.SetCurElement(vertices_element);
}

void PlyLoader::readElement(void *element, const char *name, uint64_t index, uint64_t total) {
	if(pf.Read(element) == -1)
		throw bm3d::detail::Error("ply file " + filename + " is truncated or malformed: could not read " + name + " " +
		                std::to_string(index) + " of " + std::to_string(total));
}

void PlyLoader::cacheVertices() {
	vertices.setElementsPerBlock(1<<20);
	vertices.resize(n_vertices);

	//A colour without an alpha property is opaque: the read leaves the alpha as it is here
	PlyVertex vertex = {};
	vertex.c[3] = 255;
	//caching vertices on temporary file
	for(uint64_t i = 0; i < n_vertices; i++) {
		Vertex &v = vertices[i];
		readElement(&vertex, "vertex", i, n_vertices);
		for(int k = 0; k < 3; k++)
			v.v[k] = (float)vertex.v[k];
		bm3d::detail::requireFinite(v.v, [&]() {
			return "vertex " + std::to_string(i) + " of ply file " + filename;
		});
		if(has_colors) {
			v.c[0] = vertex.c[0];
			v.c[1] = vertex.c[1];
			v.c[2] = vertex.c[2];
			v.c[3] = vertex.c[3];
		}
		if(has_textures) {
			v.t[0] = vertex.t[0];
			v.t[1] = vertex.t[1];
			if(has_vertex_tex_coords)
				bm3d::detail::requireUsableTexCoords(v.t, 2, [&]() {
					return "vertex " + std::to_string(i) + " of ply file " + filename;
				});
		}
	}

	pf.SetCurElement(faces_element);
}

void PlyLoader::setMaxMemory(uint64_t max_memory) {
	vertices.setMaxMemory(max_memory);
}

//reads the next face into face, checks it and starts its fan
void PlyLoader::readFace() {
	auto which = [&]() {
		return "face " + std::to_string(current_triangle) + " of " + std::to_string(n_triangles) + " in ply file " +
		       filename;
	};
	auto badIndexCount = [&]() {
		return bm3d::detail::Error(which() + " has " + std::to_string(face->n) + " vertex indices; a face has 3 to " +
		                 std::to_string(MAX_FACE_VERTICES));
	};

	face->n = 0;
	face->nt = 0;
	const bool read = pf.Read(face.get()) != -1;

	//plylib stores the length of a list before it finds the list too long to read, so a face
	//of too many vertices is reported as that rather than as a malformed file. A vertex index
	//list shorter than 3 (or no index list at all) is not a face either.
	if(face->n < 0 || face->n > MAX_FACE_VERTICES || (read && face->n < 3))
		throw badIndexCount();
	if(face->nt < 0 || face->nt > 2*MAX_FACE_VERTICES)
		throw bm3d::detail::Error(which() + " has " + std::to_string(face->nt) + " texture coordinates; a face has at most " +
		                std::to_string(2*MAX_FACE_VERTICES));
	if(!read)
		throw bm3d::detail::Error("ply file " + filename + " is truncated or malformed: could not read face " +
		                std::to_string(current_triangle) + " of " + std::to_string(n_triangles));

	for(int k = 0; k < face->n; k++)
		if(face->f[k] >= n_vertices)
			throw bm3d::detail::Error(which() + " has the vertex index " + std::to_string(face->f[k]) +
			                ", and the file has " + std::to_string(n_vertices) + " vertices");
	bm3d::detail::requireUsableTexCoords(face->t, face->nt, which);

	//a u, v pair for each vertex, or none: a face without them gets 0, 0 (it used to get the
	//pairs of the face read before it)
	if(has_face_tex_coords && face->nt != 0 && face->nt != 2*face->n)
		throw bm3d::detail::Error(which() + " has " + std::to_string(face->n) + " vertex indices and " + std::to_string(face->nt) +
		                " texture coordinates; expected none or " + std::to_string(2*face->n));
	if(face->nt == 0)
		std::fill(face->t, face->t + 2*face->n, 0.0f);

	current_triangle++;
	face_corner = 1;
}

uint32_t PlyLoader::getTriangles(uint32_t size, Triangle *buffer) {
	if(faces_element == -1)
		throw bm3d::detail::Error("ply file " + filename + " has no faces");

	if(current_triangle == 0)
		cacheVertices();

	uint32_t count = 0;

	//a face of n vertices is cut into the n - 2 triangles of a fan around its first vertex,
	//like the OBJ faces; a face whose triangles do not all fit in buffer goes on in the next call
	while(count < size) {
		if(face_corner == 0) {
			if(current_triangle >= n_triangles)
				break;
			readFace();
		}

		const int fan[3] = { 0, face_corner, face_corner + 1 };
		face_corner = face_corner + 2 < face->n ? face_corner + 1 : 0;

		Triangle &current = buffer[count];
		for(int k = 0; k < 3; k++) {
			const int corner = fan[k];
			Vertex &vertex = vertices[face->f[corner]];
			if(!has_vertex_tex_coords) {
				vertex.t[0] = face->t[corner*2];
				vertex.t[1] = face->t[corner*2+1];
			}

			current.vertices[k] = vertex;
		}
		//TODO! detect complicated texture coordinates

		current.node = 0;
		current.tex = face->texNumber + texOffset;

		//ignore degenerate triangles
		if(current.isDegenerate())
			continue;

		count++;
	}
	return count;
}
