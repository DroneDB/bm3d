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
#ifndef BM3D_MESHLOADER_H
#define BM3D_MESHLOADER_H

#include <string>
#include <cstdint>
#include <vector>
#include "trianglesoup.h"

class LoadTexture {
public:
	LoadTexture(const std::string &name = std::string(), const std::string &alpha = std::string()):
		filename(name), alphaFilename(alpha) {}
	std::string filename; //UTF-8
	//The image that cuts the texture out (UTF-8): empty for an opaque texture, the texture itself
	//for its own alpha, or a mask, whose alpha (or luminance, when it is opaque) is the alpha
	std::string alphaFilename;
	uint32_t width = 0;
	uint32_t height = 0;

	bool isCutout() const { return !alphaFilename.empty(); }
	bool operator==(const LoadTexture &other) const {
		return filename == other.filename && alphaFilename == other.alphaFilename;
	}
};

class MeshLoader {
public:
	MeshLoader(): has_colors(false), has_normals(false), has_textures(false) {}
	virtual ~MeshLoader() {}

	virtual void setMaxMemory(uint64_t max_memory) = 0;
	//returns actual number of triangles to pointers, memory must be allocated //if null, ignore.
	virtual uint32_t getTriangles(uint32_t size, Triangle *triangle) = 0;
	virtual bool hasColors() { return has_colors; } //call this
	virtual bool hasNormals() { return has_normals; } //call this
	virtual bool hasTextures() { return has_textures; }
	//Whether the colours are those of faces (a material, the colour of an STL face), which every
	//corner of a face has, rather than of vertices, which vary over the surface; once the whole mesh
	//has been read
	virtual bool hasFaceColors() const { return false; }
	//the vertices the file defines, once the whole mesh has been read (an STL file repeats a vertex
	//for each of the triangles that has it, and defines three for each)
	virtual uint64_t vertexCount() const = 0;
	
	//the textures the materials of the file name, once the loader has been made
	const std::vector<LoadTexture> &textureFilenames() const { return texture_filenames; }
	//the index in the stream of the first texture of this loader: the triangles it returns refer
	//to its textures by their index plus this
	void setTextureOffset(int offset) { texOffset = offset; }

protected:
	std::vector<LoadTexture> texture_filenames;
	int texOffset = 0;
	bool has_colors;
	bool has_normals;
	bool has_textures;

	void sanitizeTextureFilepath (std::string &textureFilepath);
	void resolveTextureFilepath (const std::string &modelFilepath, std::string &textureFilepath);
};

#endif // BM3D_MESHLOADER_H
