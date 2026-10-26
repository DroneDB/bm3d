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
#ifndef BM3D_BUILDER_H
#define BM3D_BUILDER_H

#include <deque>
#include <future>
#include <string>
#include <vector>

#include <vcg/space/box3.h>

#include "../common/scratchfile.h"
#include "../common/signature.h"
#include "../common/dag.h"
#include "../common/textfile.h"
#include "../common/virtualarray.h"
#include "texpyramid.h"



class KDTree;
class KDTreeSoup;
class StreamSoup;

class TMesh;

namespace bm3d::detail {

//Structures used for normalization
struct NodeBox {
	vcg::Point3f axes[3];
	vcg::Box3f box;

	NodeBox() {}
	NodeBox(KDTree *tree, uint32_t block);
	bool isIn(vcg::Point3f &p);
	std::vector<bool> markBorders(Node &node, vcg::Point3f *p, uint16_t *f);
};

class NVertex {
public:
	NVertex(uint32_t b, uint32_t i, vcg::Point3f p, vcg::Point3s *n):
		node(b), index(i), point(p), normal(n) {}
	uint32_t node;
	uint32_t index;
	vcg::Point3f point;
	vcg::Point3s *normal;

	bool operator<(const NVertex &v) const {
		if(point == v.point)
			return node > v.node;  //ascending orde: the first will be the highest node order (the leafest node)
		return point < v.point;
	}
};


class Builder {
public:
	enum Components { FACES = 1, NORMALS = 2, COLORS = 4, TEXTURES = 8 };
	// cacheDir, when non-empty, is the directory where the out-of-core caches
	// (chunks and the per-node texture file) are created. An absolute path keeps
	// the (possibly multi-GB) scratch on the same volume as the output and lets it
	// be cleaned up as a unit. When empty, historical defaults are used.
	Builder(uint32_t components, const std::string &cacheDir = std::string());

	bool hasNormals() { return header.signature.vertex.hasNormals(); }
	bool hasColors() { return header.signature.vertex.hasColors(); }
	bool hasTextures() const { return header.signature.vertex.hasTextures(); }

	void initAtlas(std::vector<Image>& textures);
	bool initAtlas(std::vector<LoadTexture>& textures);
	void create(KDTreeSoup *input, StreamSoup *output, unsigned int top_node_size);
	//textureScale: the side of this level's textures relative to the level below
	void createMeshLevel(KDTreeSoup *input, StreamSoup *output, int level, float textureScale);


	void setMaxMemory(uint64_t m) {
		max_memory = m;
		chunks.setMaxMemory(m);
		atlas.cache_max = m;
	}
	void setScaling(float s) { scaling = s; atlas.scale = sqrt(scaling); }

	void saturate();

	void reverseDag();
	void save(const std::string &filename);

	OutputFile file;

	VirtualChunks chunks;
	std::vector<NodeBox> boxes; //a box for each node

	Header header;
	std::vector<Node> nodes;
	std::vector<Patch> patches;
	std::vector<Texture> textures;

	TexAtlas atlas;
	ScratchFile nodeTex; //texure images for each node stored here.
	uint64_t max_memory;

	float scaling;
	int tex_quality; //the quality of the WebP node textures
	//the longest side of a node texture, in pixels: a node whose texture would be larger gets it
	//downscaled to this size. 0 keeps every node texture at the resolution of its source
	int maxNodeTexSide = 0;

	//the levels, from the finest, that keep the triangles of the level below and only halve its
	//texture, for a model whose texture is much finer than its triangles. Negative: as many as
	//the texture needs to come down to a few texels per triangle edge (see textureBoundLevels)
	int skipSimplifyLevels = 0;

	void processBlock(KDTreeSoup *input, StreamSoup *output, unsigned int block, int level);

	Image extractNodeTex(TMesh &mesh, int level, bool simplified, float &error, float &pixelXedge);
	int textureBoundLevels(KDTreeSoup *tree) const;
	std::vector<unsigned char> encodeNodeTex(const Image &image) const;
	//The threads the build keeps busy: the one that builds the nodes, and textureThreads - 1 more
	//that encode node textures meanwhile. 1 encodes on the building thread. The textures are
	//written to nodeTex in their order whatever the number of threads.
	int textureThreads = 1;
	std::deque<std::pair<size_t, std::future<std::vector<unsigned char>>>> pendingTex;
	void storeNodeTex(size_t texture, const Image &image);
	void appendOldestNodeTex();
	void appendNodeTex(size_t texture, const std::vector<unsigned char> &encoded);
	//writes the node textures still being encoded
	void finishNodeTex();
	void invertNodes(); //
	void saturateNode(uint32_t n);
	void uniformNormals();
	void appendBorderVertices(uint32_t origin, uint32_t destination, std::vector<NVertex> &vertices);

	void testSaturation();
};

} //namespace

#endif // BM3D_BUILDER_H
