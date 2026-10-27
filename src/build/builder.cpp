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
#include <string.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

#include "builder.h"
#include "kdtree.h"
#include "../io/mesh/meshstream.h"
#include "mesh.h"
#include "tmesh.h"
#include "../common/error.h"
#include "../common/pathutil.h"
#include "../format/bm3d_writer.h"
#include "../io/image/imageio.h"

#include <vcg/math/similarity2.h>
#include <vcg/space/rect_packer.h>

#include <iostream>
using namespace std;

namespace bm3d::detail {

NodeBox::NodeBox(KDTree *tree, uint32_t block) {
	for(int k = 0; k < 3; k++)
		axes[k] = tree->axes[k];
	box = tree->block_boxes[block];
}

bool NodeBox::isIn(vcg::Point3f &p) {
	return KDTree::isIn(axes, box, p);
}

vector<bool> NodeBox::markBorders(Node &node, vcg::Point3f *p, uint16_t *f) {
	vector<bool> border(node.nvert, false);
	for(int i = 0; i < node.nface; i++) {
		bool outside = false;
		for(int k = 0; k < 3; k++) {
			uint16_t index = f[i*3 + k];
			outside |= !isIn(p[index]);
		}
		if(outside)
			for(int k = 0; k < 3; k++) {
				uint16_t index = f[i*3 + k];
				border[index] = true;
			}
	}
	return border;
}

// Build a temporary-file template for the builder's out-of-core caches. When
// cacheDir is non-empty the caches are created there (an absolute path is
// honoured by VirtualMemory and ScratchFile), keeping multi-GB scratch on the
// same volume as the output and reclaimable as a unit. When empty the historical
// names are used: the caches fall back to the system temp dir, the per-node texture
// file to the process working directory.
static std::string cacheTemplate(const std::string &cacheDir, const char *name) {
	return cacheDir.empty() ? std::string(name) : (cacheDir + "/" + name);
}

Builder::Builder(uint32_t components, const std::string &cacheDir):
	chunks(cacheTemplate(cacheDir, "cache_chunks")), scaling(0.5),
	tex_quality(92) {

	layout.normals = components & NORMALS;
	layout.colors = components & COLORS;
	layout.texCoords = components & TEXTURES;

	nodeTex.create(cacheTemplate(cacheDir, "cache_tex"));
	//the texture tiles that do not fit in memory are kept next to the other caches too
	if(!cacheDir.empty())
		atlas.storage_prefix = cacheTemplate(cacheDir, "cache_atlas");
}

void Builder::initAtlas(std::vector<Image>& textures) {
	if(textures.size()) {
		atlas.addTextures(textures);
	}
}

bool Builder::initAtlas(std::vector<LoadTexture> &textures) {
	if(textures.size()) {
		bool success = atlas.addTextures(textures);
		if(!success)
			return false;
	}
	return true;
}

void Builder::create(KDTreeSoup *tree, StreamSoup *stream, unsigned int top_node_size) {
	Node sink;
	sink.first_patch = 0;
	nodes.push_back(sink);

	//the triangles are weighed by the texels they cover at every level, not only at the first:
	//the stream forgets the textures once it has been read, so the tree keeps them, with the
	//texel area of the current level relative to the input
	tree->textures = stream->textures;
	tree->texelScale = 1.0;

	int level = 0;
	int last_top_level_size = 0;
	size_t levelNodes = 0; //the nodes of the level just made
	float textureScale = 1.0f; //the side of this level's textures relative to the level below
	do {
		tree->clear();
		if(level % 2) tree->setAxesDiagonal();
		else tree->setAxesOrthogonal();

		tree->load(stream);
		stream->Stream::clear(); //not the clear of the triangles it holds
		if(skipSimplifyLevels < 0)
			skipSimplifyLevels = textureBoundLevels(tree);

		const size_t nodesBefore = nodes.size();
		createMeshLevel(tree, stream, level, textureScale);
		levelNodes = nodes.size() - nodesBefore;
		level++;
		//a level that keeps the triangles of the level below only makes the texture coarser: it
		//halves its side, as a mipmap does. A level of simplified triangles keeps the texels of
		//each triangle, sqrt(scaling) of the side.
		textureScale = skipSimplifyLevels > 0 ? 0.5f : atlas.scale;
		tree->texelScale *= double(textureScale) * double(textureScale);
		if(skipSimplifyLevels <= 0 && last_top_level_size != 0 && stream->size()/(float)last_top_level_size > 0.9f) {
			// cout << "Stream: " << stream->size() << " Last top level size: " << last_top_level_size << endl;
			// cout << "Larger top level, most probably to high parametrization fragmentation.\n";
			break;
		}
		last_top_level_size = stream->size();
		skipSimplifyLevels--;
		//the levels that only halve the texture go on, however few the triangles, until one node
		//holds the model: the top would otherwise be many nodes of fine texture, all loaded at
		//once. A single node needs no coarser level, its texture is within maxNodeTexSide.
	} while(stream->size() > top_node_size || (skipSimplifyLevels > 0 && levelNodes > 1));
	finishNodeTex();

	//only the sink: every block was empty (no valid triangles or points in the input)
	if(nodes.size() < 2)
		throw Error("no valid geometry in the input: the multiresolution dag would be empty");

	reverseDag();
	saturate();
}

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

//The node error of a simplified node counts at most this many squared texels along a triangle
//edge: a texture finer than that does not make the node more precise than its triangles, which
//are about to be simplified.
static const float MAX_SQUARED_TEXELS_PER_EDGE = 10.0f;

//The levels a model needs before its texture is no finer than its triangles: each of these
//levels keeps a quarter of the texels (half the side), and a triangle whose edges span
//sqrt(MAX_SQUARED_TEXELS_PER_EDGE) texels covers about sqrt(3)/4 of their square. Until then the
//levels keep the triangles and only make the texture coarser: simplifying a node cut small to
//keep its texture within maxNodeTexSide would collapse its inside onto a few vertices, its
//border being locked.
int Builder::textureBoundLevels(KDTreeSoup *tree) const {
	if(!hasTextures() || scaling <= 0 || scaling >= 1)
		return 0;
	const double balanced = std::sqrt(3.0) / 4.0 * MAX_SQUARED_TEXELS_PER_EDGE;
	const double density = tree->medianTexelsPerTriangle();
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

//The texels around the triangles that are kept as they are: bilinear filtering and the first
//mipmap levels read them along the edges of the triangles.
static const int TEXEL_MARGIN = 4;

//A node texture is packed from the bounding boxes of its pieces of texture, so most of it lies
//outside the node's triangles: texels of the model's texture that the node never draws, which
//cost as many bytes as the ones it draws. Those beyond TEXEL_MARGIN get one flat color, the mean
//of the others. image is the node texture as stored (row 0 at the top, v = 1), mesh the node with
//its texture coordinates in that texture.
static void blankUndrawnTexels(Image &image, TMesh &mesh) {
	const int width = image.width(), height = image.height();
	if(width <= 2*TEXEL_MARGIN || height <= 2*TEXEL_MARGIN)
		return;
	std::vector<uint8_t> drawn(size_t(width) * size_t(height), 0);
	//the texels whose center is inside a triangle, in pixel units of the stored image
	for(auto &face: mesh.face) {
		if(face.IsD()) continue;
		float x[3], y[3];
		for(int k = 0; k < 3; k++) {
			const auto &uv = face.V(k)->T().P();
			x[k] = uv[0] * width;
			y[k] = (1.0f - uv[1]) * height;
		}
		const float area = (x[1]-x[0])*(y[2]-y[0]) - (x[2]-x[0])*(y[1]-y[0]);
		const int x0 = std::max(0, int(std::floor(std::min({x[0], x[1], x[2]}))));
		const int x1 = std::min(width - 1, int(std::ceil(std::max({x[0], x[1], x[2]}))));
		const int y0 = std::max(0, int(std::floor(std::min({y[0], y[1], y[2]}))));
		const int y1 = std::min(height - 1, int(std::ceil(std::max({y[0], y[1], y[2]}))));
		for(int py = y0; py <= y1; py++) {
			for(int px = x0; px <= x1; px++) {
				const float cx = px + 0.5f, cy = py + 0.5f;
				//on the same side of the three edges as the triangle's own orientation; a triangle
				//with no area marks the texels its corners fall in
				float w[3];
				for(int k = 0; k < 3; k++) {
					const int j = (k + 1) % 3;
					w[k] = (x[j]-x[k])*(cy-y[k]) - (y[j]-y[k])*(cx-x[k]);
				}
				const bool inside = area >= 0 ? (w[0] >= 0 && w[1] >= 0 && w[2] >= 0)
				                              : (w[0] <= 0 && w[1] <= 0 && w[2] <= 0);
				if(inside)
					drawn[size_t(py) * width + px] = 1;
			}
		}
		for(int k = 0; k < 3; k++) {
			const int px = std::min(width - 1, std::max(0, int(x[k]))), py = std::min(height - 1, std::max(0, int(y[k])));
			drawn[size_t(py) * width + px] = 1;
		}
	}
	//grow by TEXEL_MARGIN: a square around each drawn texel, rows then columns
	std::vector<uint8_t> grown(drawn.size(), 0);
	for(int py = 0; py < height; py++) {
		int last = -2*TEXEL_MARGIN - 1; //the last drawn texel to the left, or beyond reach
		for(int px = 0; px < width + TEXEL_MARGIN; px++) {
			if(px < width && drawn[size_t(py) * width + px])
				last = px;
			const int at = px - TEXEL_MARGIN;
			if(at >= 0 && at < width && (last >= at - TEXEL_MARGIN))
				grown[size_t(py) * width + at] = 1;
		}
	}
	std::fill(drawn.begin(), drawn.end(), 0);
	for(int px = 0; px < width; px++) {
		int last = -2*TEXEL_MARGIN - 1;
		for(int py = 0; py < height + TEXEL_MARGIN; py++) {
			if(py < height && grown[size_t(py) * width + px])
				last = py;
			const int at = py - TEXEL_MARGIN;
			if(at >= 0 && at < height && (last >= at - TEXEL_MARGIN))
				drawn[size_t(at) * width + px] = 1;
		}
	}
	//the mean of the kept texels, then the flat color everywhere else
	uint64_t sum[3] = {0, 0, 0}, kept = 0;
	for(int py = 0; py < height; py++) {
		const uint32_t *row = image.scanLine(py);
		for(int px = 0; px < width; px++) {
			if(!drawn[size_t(py) * width + px]) continue;
			sum[0] += (row[px] >> 16) & 0xff; sum[1] += (row[px] >> 8) & 0xff; sum[2] += row[px] & 0xff;
			kept++;
		}
	}
	if(kept == 0)
		return;
	const uint32_t flat = Image::rgb(unsigned(sum[0] / kept), unsigned(sum[1] / kept), unsigned(sum[2] / kept));
	for(int py = 0; py < height; py++) {
		uint32_t *row = image.scanLine(py);
		for(int px = 0; px < width; px++)
			if(!drawn[size_t(py) * width + px])
				row[px] = flat;
	}
}

Image Builder::extractNodeTex(TMesh &mesh, int level, bool simplified, float &error, float &pixelXedge) {
	std::vector<vcg::Box2f> boxes;
	std::vector<int> box_texture; //which texture each box belongs;
	std::vector<int> vertex_to_tex(mesh.vert.size(), -1);
	std::vector<int> vertex_to_box;


	//find connected pieces of triangles belonging to the same texture
	//iterate over the triangles and connect the vertices.
	UnionFind components;
	components.init(mesh.vert.size());

	for(auto &face: mesh.face) {
		int v[3];
		for(int i = 0; i < 3; i++) {
			v[i] = face.V(i) - &*mesh.vert.begin();


			int &t = vertex_to_tex[v[i]];

			if(t != -1 && t != face.tex) cerr << "Missing vertex replication across seams" << endl;
			t = face.tex;
		}
		components.link(v[0], v[1]);
		components.link(v[0], v[2]);
		assert(components.root(v[1]) == components.root(v[2]));
	}
	int n_boxes = components.compact(vertex_to_box);

	//assign a texture to each vertex (we already split)
	for(auto &face: mesh.face) {
		int v[3];
		for(int i = 0; i < 3; i++) {
			int v = face.V(i) - &*mesh.vert.begin();
			vertex_to_tex[v] = face.tex;
		}
	}

	//assign all boxes to a tex (and remove boxes where the tex is -1

	//compute boxes
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
			t[0] = fmod(t[0], 1.0);
		if(t[1] != 1.0)
			t[1] = fmod(t[1], 1.0);

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

	std::vector<vcg::Point2i> sizes(boxes.size());
	std::vector<vcg::Point2i> origins(boxes.size());
	for(size_t b = 0; b < boxes.size(); b++) {
		auto &box = boxes[b];
		if(box.DimX() > 0.9) {
			for(auto &face: mesh.face) {
				int v[3];
				for(int i = 0; i < 3; i++) {
					auto v = face.V(i);
					int j = (i+1)%3;
				}
			}
		}
		int tex = box_texture[b];
		if(tex < 0) continue;

		float w = atlas.width(tex, level); //img->size().width();
		float h = atlas.height(tex, level); //img->size().height();
		if(w <= 0 || h <= 0) continue;
		float px = 1/(float)w;
		float py = 1/(float)h;
		box.Offset(vcg::Point2f(px, py));
		//snap to higher pix (clamped by 0 and 1 anyway)
		vcg::Point2i &size = sizes[b];
		vcg::Point2i &origin = origins[b];
		origin[0] = std::max(0.0f, floor(box.min[0]/px));
		origin[1] = std::max(0.0f, floor(box.min[1]/py));
		if(origin[0] >= w) origin[0] = w-1;
		if(origin[1] >= h) origin[1] = h-1;

		assert(origin[0] >= 0);

		size[0] = std::min(w, ceil(box.max[0]/px)) - origin[0];
		size[1] = std::min(h, ceil(box.max[1]/py)) - origin[1];
		if(size[0] <= 0) size[0] = 1;
		if(size[1] <= 0) size[1] = 1;
	}

	//pack boxes;
	std::vector<vcg::Point2i> mapping;
	vcg::Point2i maxSize(1096, 1096);
	vcg::Point2i finalSize;
	bool success = false;
	for(int i = 0; i < 5; i++, maxSize[0]*= 2, maxSize[1]*= 2) {
		if(sizes.size() == 0) { //no texture!
			finalSize = vcg::Point2i(1, 1);
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
		mapping.clear(); //TODO this should be done inside the packer
		success = vcg::RectPacker<float>::PackInt(sizes, maxSize, mapping, finalSize);
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

	//the size the node texture is stored at: the packed boxes, made smaller to fit in
	//maxNodeTexSide when it is set
	vcg::Point2i storedSize = finalSize;
	const int packedSide = std::max(finalSize[0], finalSize[1]);
	if(maxNodeTexSide > 0 && packedSide > maxNodeTexSide) {
		const double ratio = double(maxNodeTexSide) / double(packedSide);
		storedSize[0] = std::max(1, int(std::lround(finalSize[0] * ratio)));
		storedSize[1] = std::max(1, int(std::lround(finalSize[1] * ratio)));
	}

	Image image(finalSize[0], finalSize[1]);
	image.fill(Image::rgb(127, 127, 127));
	//copy boxes using mapping

	float pdx = 1/(float)image.width();
	float pdy = 1/(float)image.height();

	for(size_t i = 0; i < mesh.vert.size(); i++) {
		auto &p = mesh.vert[i];
		auto &uv = p.T().P();
		int b = vertex_to_box[i];
		if(b == -1 || b >= (int)origins.size() || b >= (int)mapping.size() || b >= (int)box_texture.size()) {
			uv = vcg::Point2f(0.0f, 0.0f);
			continue;
		}
		vcg::Point2i &o = origins[b];
		vcg::Point2i m = mapping[b];

		int tex = box_texture[b];
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

		assert(!isnan(uv[0]));
		assert(!isnan(uv[1]));
	}
	//compute error, in the pixels of the texture as stored: the texture coordinates are relative
	//to its size, so they need no change when it is made smaller, but a pixel covers more of
	//the surface. Each axis counts the pixels of its own side.
	const float storedWidth = float(storedSize[0]);
	const float storedHeight = float(storedSize[1]);
	error = 0.0;
	pixelXedge = 0.0f;
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
	pixelXedge = sqrt(pixelXedge/mesh.face.size()*3);
	//the mean over the edges of the squared size of a texel on the surface
	const float meanSquaredTexel = error/(3.0f*mesh.face.size());
	//A node whose triangles are kept is as precise as its texels. A simplified node keeps the
	//error the builder has always given it, three times that size (the sum was divided by the faces
	//and multiplied by 3, instead of divided by the edges): the error targets of the viewers are
	//set for it.
	error = (simplified ? 3.0f : 1.0f) * sqrt(meanSquaredTexel);

	double areausage = 0.0;
	//compute area waste
	for(int i = 0; i < mesh.face.size(); i++) {
		auto &face = mesh.face[i];
		int b = vertex_to_box[face.V(0) - &(mesh.vert[0])];
		if(b < 0 || b >= (int)origins.size()) continue;
		vcg::Point2i &o = origins[b];
		vcg::Point2i m = mapping[b];
		auto V0 = face.V(0)->T().P();
		auto V1 = face.V(1)->T().P();
		auto V2 = face.V(2)->T().P();
		areausage += (V2 - V0)^(V2 - V1)/2;
	}

	{
		//convert tex coordinates using mapping
		for(int i = 0; i < boxes.size(); i++) {
			int source = box_texture[i];
			if(source < 0) continue;
			vcg::Point2i &o = origins[i];
			vcg::Point2i &s = sizes[i];

			Image rect = atlas.read(source, level, o[0], o[1], s[0], s[1]);
			if(i < (int)mapping.size())
				image.draw(mapping[i][0], mapping[i][1], rect);
		}
	}

	image = image.mirrored().downscaled(storedSize[0], storedSize[1]);
	blankUndrawnTexels(image, mesh);
	//static int imgcount = 0;
	return image;
}

void Builder::storeNodeTex(size_t texture, const Image &image) {
	if(textureThreads <= 1) {
		appendNodeTex(texture, encodeNodeTex(image));
		return;
	}
	//the building thread is one of the textureThreads
	while(pendingTex.size() >= size_t(textureThreads - 1))
		appendOldestNodeTex();
	//the image shares its pixels with the copy the task holds, and nothing writes them any more
	pendingTex.emplace_back(texture, std::async(std::launch::async, [this, image]() { return encodeNodeTex(image); }));
}

void Builder::appendOldestNodeTex() {
	const size_t texture = pendingTex.front().first;
	const std::vector<unsigned char> encoded = pendingTex.front().second.get();
	pendingTex.pop_front();
	appendNodeTex(texture, encoded);
}

void Builder::finishNodeTex() {
	while(!pendingTex.empty())
		appendOldestNodeTex();
}

void Builder::appendNodeTex(size_t texture, const std::vector<unsigned char> &encoded) {
	textures[texture].offset = nodeTex.append(encoded.data(), encoded.size());
	textures[texture].length = encoded.size();
}

std::vector<unsigned char> Builder::encodeNodeTex(const Image &image) const {
	return encodeWebp(image, tex_quality);
}

//the most vertices a node may have: a node indexes its vertices with 16 bits (at most 65535), and
//the builder has always stopped a little below that
static const size_t MAX_NODE_VERTICES = 60000;

void Builder::processBlock(KDTreeSoup *input, StreamSoup *output, unsigned int block, int level) {
	TMesh mesh;
	TMesh tmp; //this is needed saving a mesh with vertices on seams duplicated., and for node tex coordinates to be rearranged

	Mesh mesh1;
	uint32_t mesh_size;


	int ntriangles = 0;
	{
		Soup soup = input->get(block);
		assert(soup.size() < (1<<16));
		if(soup.size() == 0) return;

		ntriangles = soup.size();
		if(!hasTextures()) {
			mesh1.load(soup);
		} else {
			mesh.load(soup);
		}
	}



	if(!hasTextures()) {
		//no need to mutex the input, it won't change anything.
		input->lock(mesh1, block);
		mesh_size = mesh1.serializedSize(layout);

	} else {
		input->lock(mesh, block);
		//we need to replicate vertices where textured seams occours

		vcg::tri::Append<TMesh,TMesh>::MeshCopy(tmp,mesh);
		for(int i = 0; i < tmp.face.size(); i++) {
			tmp.face[i].node = mesh.face[i].node;
			tmp.face[i].tex = mesh.face[i].tex;
		}
		tmp.splitSeams(layout);
		//an error, not a node left out: skipping the block would leave a hole in the model, at
		//this level and at every coarser one built from it
		if(tmp.vert.size() > MAX_NODE_VERTICES)
			throw NodeTooLargeError("a node of " + std::to_string(ntriangles) + " triangles has " +
			                            std::to_string(tmp.vert.size()) + " vertices once its texture seams are "
			                            "split, more than the " + std::to_string(MAX_NODE_VERTICES) +
			                            " a node holds (the texture parametrization is too fragmented)");

		//save node in the temporary structure
		mesh_size = tmp.serializedSize(layout);
	}
	std::vector<unsigned char> storage(mesh_size);
	unsigned char *buffer = storage.data();

	std::vector<PatchRecord> node_patches;

	float error;
	float pixelXedge;
	if(!hasTextures()) {
		mesh1.serialize(buffer, layout, node_patches);


	} else {

		Image nodetex = extractNodeTex(tmp, level, skipSimplifyLevels <= 0, error, pixelXedge);
		if(nodetex.isNull())
			throw Error("could not create the texture of a node");
		tmp.serialize(buffer, layout, node_patches);

		textures.push_back(TextureRecord());
		const size_t texture = textures.size()-1;
		for(PatchRecord &patch: node_patches)
			patch.texture = texture;
		storeNodeTex(texture, nodetex);

		//VICIUOS TRICK: we could save only a texture every 2 geometry levels since the patch is contained also to a parent node.
		//we could store the texture in the parent nodes i and have it good also for the children node.
		//but only starting from the bottom.

	}
	uint32_t chunk;
	//done serializing, move the data to the chunk.
	{
		chunk = chunks.addChunk(mesh_size);
		unsigned char *chunk_buffer = chunks.getChunk(chunk);
		memcpy(chunk_buffer, buffer, mesh_size);
		chunks.dropChunk(chunk); //no neede anymore
	}



	Node node;
	if(!hasTextures())
		node = mesh1.getNode(); //get node data before simplification
	else
		node = tmp.getNode();


	int nface;
	{

		if(!hasTextures()) {
			mesh1.lockVertices();
			{ //needed only if Mesh::QUADRICS
				mesh1.quadricInit();
			}

			error = mesh1.simplify(ntriangles*scaling, Mesh::QUADRICS);
			nface = mesh1.fn;

		} else {
			int nvert = ntriangles*scaling;

			if(skipSimplifyLevels > 0)
				nvert = ntriangles;

			else if(nvert < 64) //don't simplify too much!
				nvert = 64;

			mesh.simplify(nvert, TMesh::QUADRICS);
			nface = mesh.fn;
		}
	}


	uint32_t current_node;
	{
		//patches will be reverted later, but the local order is important because of triangleOffset
		uint32_t patch_offset = patches.size();
		std::reverse(node_patches.begin(), node_patches.end());
		patches.insert(patches.end(), node_patches.begin(), node_patches.end());

		current_node = nodes.size();
		node.chunk = chunk;           //which chunk belongs to which node
		node.error = error;


		node.first_patch = patch_offset;

		nodes.push_back(node);
		boxes.push_back(NodeBox(input, block));
	}


	//Simplify and stream the meshes





	Triangle *triangles = new Triangle[nface];
	//streaming the output
	if(!hasTextures()) {
		mesh1.getTriangles(triangles, current_node);
	} else {
		mesh.getTriangles(triangles, current_node);
	}

	{
		for(int i = 0; i < nface; i++) {
			Triangle &t = triangles[i];
			if(!t.isDegenerate())
				output->pushTriangle(triangles[i]);
		}
	}
	delete []triangles;
}

void Builder::createMeshLevel(KDTreeSoup *input, StreamSoup *output, int level, float textureScale) {
	atlas.buildLevel(level, textureScale);
	if(level > 0)
		atlas.flush(level-1);

	for(unsigned int block = 0; block < input->nBlocks(); block++)
		this->processBlock(input, output, block, level);
}


void Builder::saturate() {
	//we do not have the 'backlinks' so we make a depth first traversal
	//TODO! BIG assumption: the nodes are ordered such that child comes always after parent
	for(int node = nodes.size()-2; node >= 0; node--)
		saturateNode(node);

	//nodes.front().error = nodes.front().sphere.Radius()/2;
	nodes.back().error = 0;
}

void Builder::testSaturation() {

	//test saturation:
	for(unsigned int n = 0; n < nodes.size()-1; n++) {
		Node &node = nodes[n];
		vcg::Sphere3f &sphere = node.sphere;
		for(unsigned int p = node.first_patch; p < node.last_patch(); p++) {
			PatchRecord &patch = patches[p];
			Node &child = nodes[patch.node];
			vcg::Sphere3f s = child.sphere;
			float dist = (sphere.Center() - s.Center()).Norm();
			float R = sphere.Radius();
			float r = s.Radius();
			assert(sphere.IsIn(child.sphere));
			assert(child.error < node.error);
		}
	}
}

void Builder::reverseDag() {

	std::reverse(nodes.begin(), nodes.end());
	std::reverse(boxes.begin(), boxes.end());
	std::reverse(patches.begin(), patches.end());

	//first reversal: but we point now to the last_patch, not the first
	for(unsigned int i = 0; i < nodes.size(); i++)
		nodes[i].first_patch = patches.size() -1 - nodes[i].first_patch;

	//get the previous node last +1 to become the first
	for(unsigned int i = nodes.size()-1; i >= 1; i--)
		nodes[i].first_patch = nodes[i-1].first_patch +1;
	nodes[0].first_patch  = 0;

	//and reversed the order of the nodes.
	for(unsigned int i = 0; i < patches.size(); i++) {
		patches[i].node = nodes.size() - 1 - patches[i].node;
	}
}


void Builder::save(const std::string &filename, const CompressionSettings &compression, const std::string &metadata) {

	if(layout.normals)
		uniformNormals();

	//find roots and adjust error
	const uint32_t nodeCount = nodes.size();
	const uint32_t sink = nodeCount - 1;
	const uint32_t nroots = countRoots(nodeCount,
		[&](uint32_t n) { return nodes[n].first_patch; },
		[&](uint32_t p) { return patches[p].node; });
	for(uint32_t j = 0; j < nroots; j++)
		nodes[j].error = nodes[j].tight_radius;

	vcg::Sphere3f modelSphere = emptySphere();
	for(uint32_t i = 0; i < nroots; i++)
		modelSphere.Add(nodes[i].tightSphere());

	//The step of the coordinates comes from the errors of the root and of the leaves. The error of a
	//root was just replaced by its radius: when the root is also a leaf (a model of a single node) its
	//error says nothing of how fine its triangles are, and a step of a twentieth of the radius was the
	//length of an edge of a sphere of 5000 triangles, so that the silhouette was jagged. The leaf that is
	//a root keeps the precision of the input instead: its step follows the length of its short edges
	//(the leaves of a larger model have an error that comes from the length of their edges).
	std::vector<float> leafErrors;
	for(uint32_t i = 0; i < sink; i++)
		if(patches[nodes[i].first_patch].node == sink)
			leafErrors.push_back(i < nroots ? nodes[i].shortEdge : nodes[i].error);
	const float step = coordinateStep(compression.errorQ, nodes[0].error, leafErrors);
	const NodeCompressor compressor(layout, compression, coordinateExponent(step));

	Bm3dWriter writer(filename, layout, {sink, static_cast<uint32_t>(patches.size()), static_cast<uint32_t>(textures.size())});

	//NODES
	for(uint32_t i = 0; i < sink; i++) {
		const Node &node = nodes[i];
		std::vector<uint32_t> patchEnds;
		for(uint32_t p = node.first_patch; p < nodes[i + 1].first_patch; p++)
			patchEnds.push_back(patches[p].triangleOffset);

		const unsigned char *memory = chunks.getChunk(node.chunk);
		const EncodedNode encoded = compressor.compress(memory, node.nvert, node.nface, patchEnds);

		//the counts and the ends of the patches are the encoder's: it dropped the degenerate triangles
		NodeRecord record;
		record.error = node.error;
		record.vertexCount = encoded.vertexCount;
		record.faceCount = encoded.faceCount;
		for(int k = 0; k < 4; k++)
			record.cone[k] = node.cone.n[k];
		for(int k = 0; k < 3; k++)
			record.sphere.center[k] = node.sphere.Center()[k];
		record.sphere.radius = node.sphere.Radius();
		record.tightRadius = node.tight_radius;

		std::vector<PatchRecord> nodePatches(patches.begin() + node.first_patch, patches.begin() + nodes[i + 1].first_patch);
		if(nodePatches.size() != encoded.groupEnds.size())
			throw Error("the encoder of node " + std::to_string(i) + " returned " + std::to_string(encoded.groupEnds.size()) +
			            " groups for " + std::to_string(nodePatches.size()) + " patches");
		for(size_t p = 0; p < nodePatches.size(); p++)
			nodePatches[p].triangleOffset = encoded.groupEnds[p];

		writer.addNode(record, nodePatches.data(), nodePatches.size(), encoded.stream.data(), encoded.stream.size());
	}

	//TEXTURES
	std::vector<unsigned char> image;
	for(const TextureRecord &texture: textures) {
		image.resize(texture.length);
		nodeTex.read(texture.offset, image.data(), image.size());
		writer.addTexture(image.data(), image.size());
	}

	SphereRecord sphere;
	for(int k = 0; k < 3; k++)
		sphere.center[k] = modelSphere.Center()[k];
	sphere.radius = modelSphere.Radius();
	writer.finish(sphere, metadata);
}


//include sphere of the children and ensure error s bigger.
void Builder::saturateNode(uint32_t n) {
	const float epsilon = 1.01f;

	Node &node = nodes[n];
	for(uint32_t i = node.first_patch; i < node.last_patch(); i++) {
		PatchRecord &patch = patches[i];
		if(patch.node == nodes.size()-1) //sink, get out
			return;

		Node &child = nodes[patch.node];
		if(node.error <= child.error)
			node.error = child.error*epsilon;

		//we cannot just add the sphere, because it moves the center and the tight radius will be wrong
		if(!node.sphere.IsIn(child.sphere)) {
			float dist = (child.sphere.Center() - node.sphere.Center()).Norm();
			dist += child.sphere.Radius();
			if(dist > node.sphere.Radius())
				node.sphere.Radius() = dist;
		}
	}
	node.sphere.Radius() *= epsilon;
}

/* extracts vertices in origin which intersects destination box */
void Builder::appendBorderVertices(uint32_t origin, uint32_t destination, std::vector<NVertex> &vertices) {
	Node &node = nodes[origin];
	uint32_t chunk = node.chunk;


	//if origin != destination do not flush cache, it would invalidate pointers.
	unsigned char *buffer = chunks.getChunk(chunk, origin != destination);

	vcg::Point3f *point = (vcg::Point3f *)buffer;
	int size = sizeof(vcg::Point3f) + layout.texCoords*sizeof(vcg::Point2f);
	vcg::Point3s *normal = (vcg::Point3s *)(buffer + size * node.nvert);
	uint16_t *face = (uint16_t *)(buffer + decodedVertexBytes(layout)*node.nvert);

	NodeBox &nodebox = boxes[origin];

	vector<bool> border = nodebox.markBorders(node, point, face);
	for(int i = 0; i < node.nvert; i++) {
		if(border[i])
			vertices.push_back(NVertex(origin, i, point[i], normal + i));
	}
}


void Builder::uniformNormals() {
	//cout << "Unifying normals\n";
	/*
	level 0: for each node in the lowest level:
			load the neighboroughs
			find common vertices (use lock to find the vertices)
			average normals

	level > 0: for every other node (goind backwards)
			load child nodes
			find common vertices (use lock, it's way faster!)
			copy normal
	*/

	std::vector<NVertex> vertices;

	uint32_t sink = nodes.size()-1;
	for(int t = sink-1; t > 0; t--) {
		Node &target = nodes[t];

		vcg::Box3f box = boxes[t].box;
		//box.Offset(box.Diag()/100);
		box.Offset(box.Diag()/10);

		vertices.clear();
		appendBorderVertices(t, t, vertices);

		bool last_level = (patches[target.first_patch].node == sink);

		if(last_level) {//find neighboroughs among the same level

			for(int n = t-1; n >= 0; n--) {
				Node &node = nodes[n];
				if(patches[node.first_patch].node != sink) continue;
				if(!box.Collide(boxes[n].box)) continue;

				appendBorderVertices(n, t, vertices);
			}

		} else { //again among childrens.

			for(unsigned int p = target.first_patch; p < target.last_patch(); p++) {
				unsigned int n = patches[p].node;

				appendBorderVertices(n, t, vertices);
			}
		}

		if(!vertices.size()) { //this is possible, there might be no border at all.
			continue;
		}

		sort(vertices.begin(), vertices.end());

		unsigned int start = 0;
		while(start < vertices.size()) {
			NVertex &v = vertices[start];

			unsigned int last = start+1;
			while(last < vertices.size() && vertices[last].point == v.point)
				last++;

			if(last_level && last - start > 1) { //average all normals
				vcg::Point3f normalf(0, 0, 0);
				for(unsigned int k = start; k < last; k++) {
					for(int l = 0; l < 3; l++)
						normalf[l] += (*vertices[k].normal)[l];
				}
				normalf.Normalize();
				//convert back to shorts
				vcg::Point3s normals;
				for(int l = 0; l < 3; l++)
					normals[l] = (short)(normalf[l]*32766);

				for(unsigned int k = start; k < last; k++)
					*vertices[k].normal = normals;

			} else //just copy from first one (coming from lower level due to sorting
				for(unsigned int k = start; k < last; k++)
					*vertices[k].normal =*v.normal;

			start = last;
		}


		/*		for(unsigned int k = 0; k < vertices.size(); k++) {
			NVertex &v = vertices[k];
			if(v.point != previous) {
				//uniform normals;
				if(k - last > 1) {   //more than 1 vertex to unify

					if(last_level) {     //average all normals of the coincident points.

						vcg::Point3f normalf(0, 0, 0);

						for(unsigned int j = last; j < k; j++)
							for(int l = 0; l < 3; l++)
								normalf[l] += (*vertices[j].normal)[l];
						normalf.Normalize();

						//convert back to shorts
						vcg::Point3s normals;
						for(int l = 0; l < 3; l++)
							normals[l] = (short)(normalf[l]*32766);

						for(unsigned int j = last; j < k; j++)
							*vertices[j].normal = normals;

					} else { //just copy the first one (it's from the lowest level, due to sort)
						if(vertices[k-1].node == t) //unless this vertex do not belongs to t.
							*vertices[k-1].normal = *vertices[last].normal;
					}
				}
				previous = v.point;
				last = k;
			}
		}*/
	}
}

} //namespace
