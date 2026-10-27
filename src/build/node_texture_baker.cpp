// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#include "node_texture_baker.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>

#include <vcg/space/rect_packer.h>

#include "tmesh.h"
#include "texel_blanking.h"
#include "../common/error.h"

namespace bm3d::detail {

namespace {

//The node error of a simplified node counts at most this many squared texels along a triangle
//edge: a texture finer than that does not make the node more precise than its triangles, which
//are about to be simplified.
const float MAX_SQUARED_TEXELS_PER_EDGE = 10.0f;

class UnionFind {
public:
	std::vector<int> parents;
	void init(int size) {
		parents.resize(size);
		for(int i = 0; i < size; i++)
			parents[i] = i;
	}

	int root(int p) {
		while(p != parents[p])
			p = parents[p] = parents[parents[p]];
		return p;
	}
	void link(int p0, int p1) {
		int r0 = root(p0);
		int r1 = root(p1);
		parents[r1] = r0;
	}
	int compact(std::vector<int> &node_component) { //change numbering of the connected components, return number
		node_component.resize(parents.size());
		std::map<int, int> remap;// inser root here and order them.
		for(size_t i = 0; i < parents.size(); i++) {
			int root = i;
			while(root != parents[root])
				root = parents[root];
			parents[i] = root;
			node_component[i] = remap.emplace(root, remap.size()).first->second;
		}
		return remap.size();
	}
};

//The pieces of texture a node uses: the connected pieces of its triangles that use one texture.
//Each has the box its texture coordinates span, in the unit square of the texture.
struct Charts {
	std::vector<vcg::Box2f> boxes;
	std::vector<int> texture;      //the texture each chart belongs to
	std::vector<int> ofVertex;     //the chart of each vertex of the mesh, -1 for none
};

//Where each chart is cut from its texture, in the texels of the level, and the box it takes
struct ChartRects {
	std::vector<vcg::Point2i> sizes;
	std::vector<vcg::Point2i> origins;
};

//Where each chart goes in the node texture
struct Packing {
	std::vector<vcg::Point2i> mapping;
	vcg::Point2i finalSize;
};

} //namespace

NodeTextureBaker::NodeTextureBaker(const Settings &settings, const Logger &logger):
	maxNodeTextureSide_(settings.maxNodeTextureSide), logger_(logger) {
	atlas_.scale = simplifiedScale(settings.scaling);
	atlas_.cache_max = settings.maxMemory;
	//the texture tiles that do not fit in memory are kept next to the other caches
	atlas_.storage_prefix = settings.scratchPrefix;
}

bool NodeTextureBaker::addTextures(std::vector<LoadTexture> &textures) {
	if(textures.size()) {
		bool success = atlas_.addTextures(textures);
		if(!success)
			return false;
	}
	return true;
}

void NodeTextureBaker::beginLevel(int level, float textureScale) {
	atlas_.buildLevel(level, textureScale);
	if(level > 0)
		atlas_.flush(level-1);
}

//The levels a model needs before its texture is no finer than its triangles: each of these
//levels keeps a quarter of the texels (half the side), and a triangle whose edges span
//sqrt(MAX_SQUARED_TEXELS_PER_EDGE) texels covers about sqrt(3)/4 of their square. Until then the
//levels keep the triangles and only make the texture coarser: simplifying a node cut small to
//keep its texture within maxNodeTextureSide would collapse its inside onto a few vertices, its
//border being locked.
int NodeTextureBaker::textureOnlyLevels(double texelsPerTriangle, float scaling) {
	if(scaling <= 0 || scaling >= 1)
		return 0;
	const double balanced = std::sqrt(3.0) / 4.0 * MAX_SQUARED_TEXELS_PER_EDGE;
	const double density = texelsPerTriangle;
	if(density <= balanced)
		return 0;
	//a texture cannot be halved more than 64 times (2^64 texels along a side), and the density of a
	//model whose texture coordinates are huge is an infinity, or too large to count the levels of.
	//The levels are counted by multiplying by 4, which is exact, and not with log(density)/log(4):
	//the logarithm of the mathematical library is not the same in the last bit on every platform,
	//and a density that is a power of 4 times the balanced one would round up on some.
	const int kMaxLevels = 64;
	int levels = 0;
	for(double reached = balanced; reached < density && levels < kMaxLevels; reached *= 4)
		levels++;
	return levels;
}

namespace {

//Finds the charts of the mesh. The texture coordinates are brought into the unit square (the
//fractional part), and a chart's box leaves out the vertices at (0, 0), which stand for no
//coordinate.
Charts findCharts(TMesh &mesh, const Logger &logger) {
	Charts charts;
	std::vector<int> vertex_to_tex(mesh.vert.size(), -1);
	std::vector<int> &vertex_to_box = charts.ofVertex;

	//find connected pieces of triangles belonging to the same texture
	//iterate over the triangles and connect the vertices.
	UnionFind components;
	components.init(mesh.vert.size());

	for(auto &face: mesh.face) {
		int v[3];
		for(int i = 0; i < 3; i++) {
			v[i] = face.V(i) - &*mesh.vert.begin();

			int &t = vertex_to_tex[v[i]];

			if(t != -1 && t != face.tex) warn(logger, "Missing vertex replication across seams");
			t = face.tex;
		}
		components.link(v[0], v[1]);
		components.link(v[0], v[2]);
		assert(components.root(v[1]) == components.root(v[2]));
	}
	int n_boxes = components.compact(vertex_to_box);

	//assign a texture to each vertex (we already split)
	for(auto &face: mesh.face) {
		for(int i = 0; i < 3; i++) {
			int v = face.V(i) - &*mesh.vert.begin();
			vertex_to_tex[v] = face.tex;
		}
	}

	//assign all boxes to a tex (and remove boxes where the tex is -1

	//compute boxes
	std::vector<vcg::Box2f> &boxes = charts.boxes;
	std::vector<int> &box_texture = charts.texture;
	boxes.resize(n_boxes);
	box_texture.resize(n_boxes, -1);
	for(size_t i = 0; i < mesh.vert.size(); i++) {
		int b = vertex_to_box[i];
		int tex = vertex_to_tex[i];
		if(tex < 0) continue; //vertex not assigned.

		vcg::Box2f &box = boxes[b];
		box_texture[b] = tex;
		auto &t = mesh.vert[i].T().P();
		if(t[0] != 1.0)
			t[0] = std::fmod(t[0], 1.0);
		if(t[1] != 1.0)
			t[1] = std::fmod(t[1], 1.0);

		if(t[0] != 0.0f || t[1] != 0.0f)
			box.Add(t);
	}

	//erase boxes assigned to no texture, and remap vertex_to_box
	int count = 0;
	std::vector<int> remap(n_boxes, -1);
	for(int i = 0; i < n_boxes; i++) {
		if(box_texture[i] == -1)
			continue;
		boxes[count] = boxes[i];
		box_texture[count] = box_texture[i];
		remap[i] = count++;
	}
	boxes.resize(count);
	box_texture.resize(count);
	for(int &b: vertex_to_box) {
		if(b >= 0 && b < n_boxes)
			b = remap[b];
		else
			b = -1;
	}
	return charts;
}

//The rectangle of the texels of `level` that each chart is cut from: its box with a texel more on
//every side, snapped outwards to whole texels
ChartRects chartRects(const Charts &charts, TexAtlas &atlas, int level) {
	ChartRects rects;
	rects.sizes.resize(charts.boxes.size());
	rects.origins.resize(charts.boxes.size());
	for(size_t b = 0; b < charts.boxes.size(); b++) {
		vcg::Box2f box = charts.boxes[b];
		int tex = charts.texture[b];
		if(tex < 0) continue;

		float w = atlas.width(tex, level); //img->size().width();
		float h = atlas.height(tex, level); //img->size().height();
		if(w <= 0 || h <= 0) continue;
		float px = 1/(float)w;
		float py = 1/(float)h;
		box.Offset(vcg::Point2f(px, py));
		//snap to higher pix (clamped by 0 and 1 anyway)
		vcg::Point2i &size = rects.sizes[b];
		vcg::Point2i &origin = rects.origins[b];
		origin[0] = std::max(0.0f, std::floor(box.min[0]/px));
		origin[1] = std::max(0.0f, std::floor(box.min[1]/py));
		if(origin[0] >= w) origin[0] = w-1;
		if(origin[1] >= h) origin[1] = h-1;

		assert(origin[0] >= 0);

		size[0] = std::min(w, std::ceil(box.max[0]/px)) - origin[0];
		size[1] = std::min(h, std::ceil(box.max[1]/py)) - origin[1];
		if(size[0] <= 0) size[0] = 1;
		if(size[1] <= 0) size[1] = 1;
	}
	return rects;
}

//Packs the rectangles in one image, in a square of 1096 texels on a side or, if they do not fit, in
//one twice as large, up to four times
Packing packRects(const std::vector<vcg::Point2i> &sizes) {
	Packing packing;
	vcg::Point2i maxSize(1096, 1096);
	bool success = false;
	for(int i = 0; i < 5; i++, maxSize[0]*= 2, maxSize[1]*= 2) {
		if(sizes.size() == 0) { //no texture!
			packing.finalSize = vcg::Point2i(1, 1);
			success = true;
			break;
		}
		bool too_large = false;
		for(auto s: sizes) {
			if(s[0] >= maxSize[0] || s[1] >= maxSize[1])
				too_large = true;
		}
		if(too_large) { //TODO the packer should simply return false
			continue;
		}
		packing.mapping.clear(); //TODO this should be done inside the packer
		success = vcg::RectPacker<float>::PackInt(sizes, maxSize, packing.mapping, packing.finalSize);
		if(success)
			break;
	}
	if(!success) {
		//an error, not a blank node texture: the texture coordinates of the node are not
		//remapped on this path, so its texture would be the size of the failed packing
		//(over 16K a side, gigabytes of memory) and its pixels unset.
		throw TexturePackingError("Failed packing: the texture in a single bm3d node would be > 16K\n"
		                              "Try to reduce the size of the nodes using -t (default is 4096)");
	}
	return packing;
}

//The size the node texture is stored at: the packed rectangles, made smaller to fit in
//maxNodeTextureSide when it is set
vcg::Point2i storedSizeOf(const vcg::Point2i &finalSize, int maxNodeTextureSide) {
	vcg::Point2i storedSize = finalSize;
	const int packedSide = std::max(finalSize[0], finalSize[1]);
	if(maxNodeTextureSide > 0 && packedSide > maxNodeTextureSide) {
		const double ratio = double(maxNodeTextureSide) / double(packedSide);
		storedSize[0] = std::max(1, int(std::lround(finalSize[0] * ratio)));
		storedSize[1] = std::max(1, int(std::lround(finalSize[1] * ratio)));
	}
	return storedSize;
}

//Points the texture coordinates of the mesh into the node texture, where each chart has been
//packed
void remapTexCoords(TMesh &mesh, const Charts &charts, const ChartRects &rects, const Packing &packing,
                    TexAtlas &atlas, int level) {
	float pdx = 1/(float)packing.finalSize[0];
	float pdy = 1/(float)packing.finalSize[1];

	for(size_t i = 0; i < mesh.vert.size(); i++) {
		auto &p = mesh.vert[i];
		auto &uv = p.T().P();
		int b = charts.ofVertex[i];
		if(b == -1 || b >= (int)rects.origins.size() || b >= (int)packing.mapping.size() || b >= (int)charts.texture.size()) {
			uv = vcg::Point2f(0.0f, 0.0f);
			continue;
		}
		const vcg::Point2i &o = rects.origins[b];
		vcg::Point2i m = packing.mapping[b];

		int tex = charts.texture[b];
		if(tex < 0) {
			uv = vcg::Point2f(0.0f, 0.0f);
			continue;
		}
		float w = atlas.width(tex, level);
		float h = atlas.height(tex, level);
		if(w <= 0 || h <= 0) {
			uv = vcg::Point2f(0.0f, 0.0f);
			continue;
		}
		float px = 1/(float)w;
		float py = 1/(float)h;

		if(uv[0] < 0.0f)
			uv[0] = 0.0f;
		if(uv[1] < 0.0f)
			uv[1] = 0.0f;

		float dx = uv[0]/px - o[0];
		float dy = uv[1]/py - o[1];
		if(dx < 0.0f) dx = 0.0f;
		if(dy < 0.0f) dy = 0.0f;

		uv[0] = (m[0] + dx)*pdx; //how many pixels from the origin
		uv[1] = (m[1] + dy)*pdy; //how many pixels from the origin

		assert(!std::isnan(uv[0]));
		assert(!std::isnan(uv[1]));
	}
}

//The error of the node, in the pixels of the texture as stored: the texture coordinates are
//relative to its size, so they need no change when it is made smaller, but a pixel covers more of
//the surface. Each axis counts the pixels of its own side.
void nodeError(TMesh &mesh, const vcg::Point2i &storedSize, bool simplified, NodeTexture &result) {
	const float storedWidth = float(storedSize[0]);
	const float storedHeight = float(storedSize[1]);
	float error = 0.0;
	float pixelXedge = 0.0f;
	for(auto &face: mesh.face) {
		for(int k = 0; k < 3; k++) {
			int j = (k==2)?0:k+1;

			float edge = vcg::SquaredNorm(face.P(k) - face.P(j));
			const vcg::Point2f uv = face.V(k)->T().P() - face.V(j)->T().P();
			float pixel = uv[0]*storedWidth*uv[0]*storedWidth + uv[1]*storedHeight*uv[1]*storedHeight;
			pixelXedge += pixel;
			//a node whose triangles are kept is as precise as its texels, however many there are
			if(simplified && pixel > MAX_SQUARED_TEXELS_PER_EDGE) pixel = MAX_SQUARED_TEXELS_PER_EDGE;
			if(pixel < 1)
				error += edge;
			else
				error += edge/pixel;
		}
	}
	pixelXedge = std::sqrt(pixelXedge/mesh.face.size()*3);
	//the mean over the edges of the squared size of a texel on the surface
	const float meanSquaredTexel = error/(3.0f*mesh.face.size());
	//A node whose triangles are kept is as precise as its texels. A simplified node keeps the
	//error the builder has always given it, three times that size (the sum was divided by the faces
	//and multiplied by 3, instead of divided by the edges): the error targets of the viewers are
	//set for it.
	result.error = (simplified ? 3.0f : 1.0f) * std::sqrt(meanSquaredTexel);
	result.pixelsPerEdge = pixelXedge;
}

} //namespace

NodeTexture NodeTextureBaker::bake(TMesh &mesh, int level, bool simplified) {
	const Charts charts = findCharts(mesh, logger_);
	const ChartRects rects = chartRects(charts, atlas_, level);
	const Packing packing = packRects(rects.sizes);
	const vcg::Point2i storedSize = storedSizeOf(packing.finalSize, maxNodeTextureSide_);

	Image image(packing.finalSize[0], packing.finalSize[1]);
	image.fill(Image::rgb(127, 127, 127));

	remapTexCoords(mesh, charts, rects, packing, atlas_, level);

	NodeTexture result;
	nodeError(mesh, storedSize, simplified, result);

	//copy the charts into the image using the mapping
	for(int i = 0; i < (int)charts.boxes.size(); i++) {
		int source = charts.texture[i];
		if(source < 0) continue;
		const vcg::Point2i &o = rects.origins[i];
		const vcg::Point2i &s = rects.sizes[i];

		Image rect = atlas_.read(source, level, o[0], o[1], s[0], s[1]);
		if(i < (int)packing.mapping.size())
			image.draw(packing.mapping[i][0], packing.mapping[i][1], rect);
	}

	image = image.mirrored().downscaled(storedSize[0], storedSize[1]);
	std::vector<UvTriangle> triangles;
	triangles.reserve(mesh.face.size());
	for(auto &face: mesh.face) {
		if(face.IsD()) continue;
		triangles.push_back({face.V(0)->T().P(), face.V(1)->T().P(), face.V(2)->T().P()});
	}
	blankUndrawnTexels(image, triangles);
	result.image = image;
	return result;
}

} //namespace
