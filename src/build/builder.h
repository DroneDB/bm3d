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

#include <memory>
#include <string>
#include <vector>

#include <vcg/space/box3.h>

#include "../common/scratchfile.h"
#include "../common/virtualarray.h"
#include "../compress/compressor.h"
#include "../format/bm3d_format.h"
#include "model_dag.h"
#include "node_texture_store.h"
#include "texture_encoder_pool.h"
#include "../io/image/webp_codec.h"
#include "node.h"
#include "texpyramid.h"



class KDTree;
class KDTreeSoup;
class StreamSoup;

class TMesh;

namespace bm3d::detail {

class Builder {
public:
	enum Components { NORMALS = 1, COLORS = 2, TEXTURES = 4 };
	// cacheDir, when non-empty, is the directory where the out-of-core caches
	// (chunks and the per-node texture file) are created. An absolute path keeps
	// the (possibly multi-GB) scratch on the same volume as the output and lets it
	// be cleaned up as a unit. When empty, historical defaults are used.
	Builder(uint32_t components, const std::string &cacheDir = std::string());

	bool hasNormals() { return dag.layout().normals; }
	bool hasColors() { return dag.layout().colors; }
	bool hasTextures() const { return dag.layout().texCoords; }

	void initAtlas(std::vector<Image>& textures);
	bool initAtlas(std::vector<LoadTexture>& textures);
	void create(KDTreeSoup *input, StreamSoup *output, unsigned int top_node_size);
	//textureScale: the side of this level's textures relative to the level below
	void createMeshLevel(KDTreeSoup *input, StreamSoup *output, int level, float textureScale);


	void setMaxMemory(uint64_t m) {
		max_memory = m;
		dag.setMaxMemory(m);
		atlas.cache_max = m;
	}
	void setScaling(float s) { scaling = s; atlas.scale = sqrt(scaling); }

	//Writes the model to the file `filename`, nodes compressed with corto as the settings say;
	//metadata is the JSON object of the metadata block (see Bm3dWriter::finish). The file is
	//written under another name and renamed when it is complete.
	void save(const std::string &filename, const CompressionSettings &compression, const std::string &metadata);

	ModelDag dag;
	NodeTextureStore nodeTextures; //the encoded image of each node

	TexAtlas atlas;
	uint64_t max_memory;

	float scaling;
	int tex_quality; //the quality of the WebP node textures (set before the first node is made)
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
	//The threads the build keeps busy: the one that builds the nodes, and textureThreads - 1 more
	//that encode node textures meanwhile. 1 encodes on the building thread. The textures are
	//written to nodeTextures in their order whatever the number of threads.
	int textureThreads = 1;
	void storeNodeTex(size_t texture, const Image &image);
	//writes the node textures still being encoded
	void finishNodeTex();

private:
	//made when the first node texture is stored, from tex_quality and textureThreads; the pool is
	//destroyed before the encoder it uses
	std::unique_ptr<WebpEncoder> textureEncoder;
	std::unique_ptr<TextureEncoderPool> texturePool;
};

} //namespace

#endif // BM3D_BUILDER_H
