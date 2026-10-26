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
#define _FILE_OFFSET_BITS 64

#include "reader.h"
#include "stdfile.h"
#include "error.h"
#include <vcg/space/line3.h>
#include <vcg/space/intersection3.h>

#include <corto/decoder.h>

//#if _MSC_VER >= 1800
#include <random>
//#endif

#include <iostream>
using namespace std;
using namespace vcg;
using namespace bm3d::detail;

uint16_t *NodeData::faces(Signature &sig, uint32_t nvert, char *mem) {
	return (uint16_t  *)(mem + nvert*sig.vertex.size());
}


Reader::Reader(): nodes(0), patches(0), textures(0), nodedata(0), texturedata(0), nroots(0) {
	file = new StdFile();
}

Reader::~Reader() {
	close();
	delete file;
}

bool Reader::open(const char *_uri) {

	file->setFileName(_uri);
	if(!file->open(File::Read))
		//file = fopen(_uri, "rb+");
		//if(!file)
		return false;
	loadHeader();
	loadIndex();
	return true;
}

void Reader::close() {
	flush();
}

void Reader::flush() {
	//a node is still loaded when the code between its loadRam and dropRam threw (an compressor
	//that could not write the output); its memory is mapped or allocated, never a single new
	if(nodedata)
		for(unsigned int i = 0; i < header.n_nodes; i++)
			if(nodedata[i].memory)
				releaseNodeMemory(nodedata[i]);

	delete []nodes;
	delete []patches;
	delete []textures;
	delete []nodedata;
	delete []texturedata;
	//close() and then the destructor flush twice
	nodes = nullptr;
	patches = nullptr;
	textures = nullptr;
	nodedata = nullptr;
	texturedata = nullptr;
}

void Reader::loadHeader() {
	//fread(&header, sizeof(Header), 1, file);
	int readed = file->read((char *)&header, sizeof(Header));
	if (readed != sizeof(Header))
		throw Error("could not read header, file too short");
	if(header.magic != 0x4E787320)
		throw Error("could not read header, probably not a bm3d file");
}

void Reader::loadHeader(char *buffer) {
	header = *(Header *)buffer;
	if(header.magic != 0x4E787320)
		throw Error("could not read header, probably not a bm3d file");
}

void Reader::countRoots() {
	nroots = countDagRoots(nodes, patches, header.n_nodes);
}

uint64_t Reader::indexSize() {
	return header.n_nodes * sizeof(Node) +
			header.n_patches * sizeof(Patch) +
			header.n_textures * sizeof(Texture);
}

void Reader::initIndex() {
	nodes = new Node[header.n_nodes];
	patches = new Patch[header.n_patches];
	textures = new Texture[header.n_textures];

	nodedata = new NodeData[header.n_nodes];
	texturedata = new TextureData[header.n_textures];
}

void Reader::loadIndex() {
	initIndex();

	//fread(nodes, sizeof(Node), header.n_nodes, file);
	//fread(patches, sizeof(Patch), header.n_patches, file);
	//fread(textures, sizeof(Texture), header.n_textures, file);
	file->read((char *)nodes, sizeof(Node)*header.n_nodes);
	file->read((char *)patches, sizeof(Patch)*header.n_patches);
	file->read((char *)textures, sizeof(Texture)*header.n_textures);
	countRoots();
}

void Reader::loadIndex(char *buffer) {
	initIndex();

	uint32_t size = sizeof(Node)*header.n_nodes;
	memcpy(nodes, buffer, size);
	buffer += size;

	size = sizeof(Patch)*header.n_patches;
	memcpy(patches, buffer, size);
	buffer += size;

	size = sizeof(Texture)*header.n_textures;
	memcpy(textures, buffer, size);

	countRoots();
}

uint32_t Reader::size(uint32_t node) {
	return nodes[node].getSize();
}


uint64_t Reader::loadRam(uint32_t n) {

	Signature &sign = header.signature;
	Node &node = nodes[n];
	uint64_t offset = node.getBeginOffset();

	NodeData &d = nodedata[n];
	uint64_t compressed_size = node.getEndOffset() - offset;

	uint64_t size = node.nvert*sign.vertex.size() + node.nface*sign.face.size();



	if(!sign.isCompressed()) {
		d.memory = (char *)file->map(offset, size);
		if(!d.memory)
			throw Error("could not read node " + std::to_string(n) + " of the bm3d file");

	} else {

		char *buffer = new char[compressed_size];
		file->seek(offset);
		int64_t r = file->read(buffer, compressed_size);
		assert(r == (int64_t)compressed_size);

		d.memory = new char[size];

		crt::Decoder decoder(compressed_size, (unsigned char *)buffer);

		decoder.setPositions((float *)d.coords());
		if(sign.vertex.hasNormals())
			decoder.setNormals((int16_t *)d.normals(sign, node.nvert));
		if(sign.vertex.hasColors())
			decoder.setColors((unsigned char *)d.colors(sign, node.nvert));
		if(sign.vertex.hasTextures())
			decoder.setUvs((float *)d.texCoords(sign, node.nvert));
		if(node.nface)
			decoder.setIndex(d.faces(sign, node.nvert));

		decoder.decode();
		delete []buffer;
	}

	assert(d.memory);
	if(header.n_textures) {
		//be sure to load images
		for(uint32_t p = node.first_patch; p < node.last_patch(); p++) {
			uint32_t t = patches[p].texture;
			if(t == 0xffffffff) continue;

			TextureData &data = texturedata[t];
			data.count_ram++;
			if(data.count_ram > 1)
				continue;

			Texture &texture = textures[t];
			data.memory = (char *)file->map(texture.getBeginOffset(), texture.getSize());
			if(!data.memory) {
				cerr << "Failed mapping texture data" << endl;
				//exit(0);
				return 0;
			}

			//loadImageFromData(data, t);
			int imgsize = data.width * data.height * 4;
			size += imgsize;
		}
	}
	return size;
}

void Reader::releaseNodeMemory(NodeData &data) {
	auto &sign = header.signature;
	if(!sign.isCompressed()) {
		file->unmap((unsigned char *)data.memory);
	} else {
		delete []data.memory;
	}
	data.memory = NULL;
}

uint64_t Reader::dropRam(uint32_t n, bool write) {
	Node &node = nodes[n];
	NodeData &data = nodedata[n];

	assert(!write);
	assert(!data.vbo);
	assert(data.memory);

	releaseNodeMemory(data);

	//report RAM size for compressed meshes.
	auto &sign = header.signature;
	uint32_t size = node.nvert * sign.vertex.size() +
				node.nface * sign.face.size();

	if(header.n_textures) {
		for(uint32_t p = node.first_patch; p < node.last_patch(); p++) {
			uint32_t t = patches[p].texture;
			if(t == 0xffffffff) continue;

			TextureData &tdata = texturedata[t];
			tdata.count_ram--;
			if(tdata.count_ram != 0) continue;
			file->unmap((unsigned char *)tdata.memory);

			tdata.memory = NULL;
			size += tdata.width*tdata.height*4;
		}
	}
	return size;
}


//local structure for keeping track of nodes and their distance
class DNode {
public:
	uint32_t node;
	float dist;
	DNode(uint32_t n = 0, float d = 0.0f): node(n), dist(d) {}
	bool operator<(const DNode &n) const {
		return dist < n.dist;
	}
};

bool closest(vcg::Sphere3f &sphere, vcg::Ray3f &ray, float &distance) {
	vcg::Point3f dir = ray.Direction();
	vcg::Line3f line(ray.Origin(), dir.Normalize());

	vcg::Point3f p, q;
	bool success = vcg::IntersectionLineSphere(sphere, line, p, q);
	if(!success) return false;
	p -= ray.Origin();
	q -= ray.Origin();
	float a = p*ray.Direction();
	float b = q*ray.Direction();
	if(a > b) a = b;
	if(b < 0) return false; //behind observer
	if(a < 0) {   //we are inside
		distance = 0;
		return true;
	}
	distance = a;
	return true;
}

bool Reader::intersects(vcg::Ray3f &ray, float &distance) {
	uint32_t sink = header.n_nodes -1;

	vcg::Point3f dir = ray.Direction();
	ray.SetDirection(dir.Normalize());

	if(!closest(header.sphere, ray, distance))
		return false;

	distance = 1e20f;

	//keep track of visited nodes (it is a DAG not a tree!
	std::vector<bool> visited(header.n_nodes, false);
	std::vector<DNode> candidates;      //heap of nodes
	candidates.push_back(DNode(0, distance));

	bool hit = false;

	while(candidates.size()) {
		pop_heap(candidates.begin(), candidates.end());
		DNode candidate = candidates.back();
		candidates.pop_back();

		if(candidate.dist > distance) break;

		Node &node = nodes[candidate.node];

		if(node.first_patch  == sink) {
			if(candidate.dist < distance) {
				distance = candidate.dist;
				hit = true;
			}
			continue;
		}

		//not a leaf, insert children (if they intersect
		for(uint32_t i = node.first_patch; i < node.last_patch(); i++) {
			uint32_t child = patches[i].node;
			if(child == sink) continue;
			if(visited[child]) continue;

			float d;
			if(closest(nodes[child].sphere, ray, d)) {
				candidates.push_back(DNode(child, d));
				push_heap(candidates.begin(), candidates.end());
				visited[child] = true;
			}
		}
	}
	distance = sqrt(distance);
	return hit;
}

