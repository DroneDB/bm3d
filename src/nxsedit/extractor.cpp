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
#include <cstring>
#include <filesystem>
#include <sstream>

#include "extractor.h"

#include "../common/error.h"
#include "../common/pathutil.h"

#include <corto/corto.h>

#include <vcg/space/triangle3.h>
using namespace std;
using namespace nx;

Extractor::Extractor(NexusData *nx):
	transform(false),
	max_size(0), current_size(0),
	min_error(0),current_error(0),
	max_triangles(0), current_triangles(0) {

	nexus = nx;
	selected.resize(nexus->header.n_nodes, true);
	selected.back() = false;
}

int Extractor::sinkDistance(int node) {
	int dist = 0;
	int sink = nexus->header.n_nodes - 1;
	while(node != sink) {
		Node &nodeData = nexus->nodes[node];
		node = nexus->patches[nodeData.first_patch].node;
		dist++;
	}
	return dist;

}
int Extractor::levelCount() {
	return sinkDistance(0);
}

void Extractor::setMatrix(vcg::Matrix44f m) {
	transform = true;
	matrix = m;
}

void Extractor::selectBySize(uint64_t size) {
	max_size = size;
	traverse(nexus);
}

void Extractor::selectByError(float error) {
	min_error = error;
	traverse(nexus);
}

void Extractor::selectByTriangles(uint64_t triangles) {
	max_triangles = triangles;
	traverse(nexus);
}

void Extractor::selectByLevel(int level) {
	nlevels = levelCount();
	if(level >= nlevels)
		throw Error("Extracted level must be < than the number of levels: " + std::to_string(nlevels));
	max_level = level;
	traverse(nexus);
} //select up to level included (zero based)

void Extractor::dropLevel() {
	selected.resize(nexus->header.n_nodes, true);
	uint32_t n_nodes = nexus->header.n_nodes;
	uint32_t sink = n_nodes-1;
	//unselect nodes which output is the sink (definition of last level :)
	for(unsigned int i = 0; i < n_nodes-1; i++) {
		nx::Node &node = nexus->nodes[i];
		if(nexus->patches[node.first_patch].node == sink)
			selected[i] = false;

	}
	selected.back() = false; //sink unselection purely for coherence
}

void Extractor::save(const std::string &output, nx::Signature &signature) {
	OutputFile file;
	file.open(output);

	nx::Header header = nexus->header;
	header.signature = signature;
	header.nvert = 0;
	header.nface = 0;

	if(transform)
		header.sphere.Center() = matrix * header.sphere.Center();

	vector<nx::Node> nodes;
	vector<nx::Patch> patches;
	vector<nx::Texture> textures;

	unsigned int n_nodes = nexus->header.n_nodes;
	vector<int> node_remap(n_nodes, -1);


	for(unsigned int i = 0; i < n_nodes-1; i++) {
		if(!selected[i]) continue;
		nx::Node node = nexus->nodes[i];
		if(transform)
			node.sphere.Center() = matrix * node.sphere.Center();
		header.nvert += node.nvert;
		header.nface += node.nface;

		node_remap[i] = nodes.size();

		node.first_patch = patches.size();
		for(unsigned int k = nexus->nodes[i].first_patch; k < nexus->nodes[i].last_patch(); k++) {
			nx::Patch patch = nexus->patches[k];
			patches.push_back(patch);
		}

		nodes.push_back(node);
	}

	nx::Node sink = nexus->nodes[n_nodes -1];
	sink.first_patch = patches.size();
	nodes.push_back(sink);

	for(unsigned int i = 0; i < patches.size(); i++) {
		nx::Patch &patch = patches[i];
		int remapped = node_remap[patch.node];
		if(remapped == -1)
			patch.node = nodes.size()-1;
		else
			patch.node = remapped;
		assert(patch.node < nodes.size());
	}

	//cout << "Textures: " << nexus->header.n_textures << endl;
	textures.resize(nexus->header.n_textures);

	header.n_nodes = nodes.size();
	header.n_patches = patches.size();
	header.n_textures = textures.size();

	uint64_t size = sizeof(nx::Header)  +
			nodes.size()*sizeof(nx::Node) +
			patches.size()*sizeof(nx::Patch) +
			textures.size()*sizeof(nx::Texture);
	size = pad(size);

	for(unsigned int i = 0; i < nodes.size(); i++) {
		nodes[i].offset += size/NEXUS_PADDING;
	}

	//TODO should actually remove textures not used anymore.

	file.write(&header, sizeof(header));
	file.write(nodes.data(), sizeof(nx::Node)*nodes.size());
	file.write(patches.data(), sizeof(nx::Patch)*patches.size());
	file.write(textures.data(), sizeof(nx::Texture)*textures.size());
	file.padTo(size);

	for(unsigned int i = 0; i < node_remap.size()-1; i++) {
		int n = node_remap[i];
		if(n == -1) continue;
		nx::Node &node = nodes[n];
		node.offset = file.pos()/NEXUS_PADDING;

		nexus->loadRam(i);

		NodeData &data = nexus->nodedata[i];
		char *memory = data.memory;
		int data_size = nexus->nodes[i].getSize();
		if(transform) {
			memory = new char[node.getSize()];
			memcpy(memory, data.memory, data_size);

			for(int k = 0; k < node.nvert; k++) {
				vcg::Point3f &v = ((vcg::Point3f *)memory)[k];
				v = matrix * v;
			}
		}
		if(signature.isCompressed())
			compress(file, signature, node, nexus->nodedata[i], &*patches.begin());
		else
			file.write(memory, data_size);
		if(transform)
			delete []memory;

		nexus->dropRam(i);
	}
	nodes.back().offset = file.pos()/NEXUS_PADDING;

	//save textures
	if(textures.size()) {
		for(uint32_t i = 0; i < textures.size()-1; i++) {
			Texture &in = nexus->textures[i];
			Texture &out = textures[i] = in;

			uint64_t start = in.getBeginOffset();
			uint64_t size = in.getSize();
			char *memory = (char *)nexus->file->map(start, size);
			if(!memory)
				throw Error("could not read texture " + std::to_string(i) + " of the nexus file");
			
			out.offset = file.pos()/NEXUS_PADDING;
			file.write(memory, size);

			nexus->file->unmap(memory);
		}
		textures.back().offset = file.pos()/NEXUS_PADDING;
	}

	file.seek(sizeof(nx::Header));
	file.write(nodes.data(), sizeof(nx::Node)*nodes.size());
	file.write(patches.data(), sizeof(nx::Patch)*patches.size());
	file.write(textures.data(), sizeof(nx::Texture)*textures.size());
	file.close();
}


nx::Traversal::Action Extractor::expand(nx::Traversal::HeapNode h) {
	nx::Node &node = nexus->nodes[h.node];
	current_size += node.getEndOffset() - node.getBeginOffset();
	current_triangles += node.nface;

	//cout << "Max size: " << max_size << " Current size: " << current_size << endl;
	if(max_triangles && current_triangles > max_triangles)
		return STOP;
	if(max_size && current_size > max_size)
		return STOP;
	if(min_error && node.error < min_error)
		return STOP;
	if(max_level >= 0 && nlevels- sinkDistance(h.node) > max_level)
		return STOP;

	return EXPAND;
}

float Extractor::nodeError(uint32_t node, bool &visible) {
	if(max_level >= 0) return sinkDistance(node);
	return Traversal::nodeError(node, visible);
}


uint64_t Extractor::pad(uint64_t s) {
	const uint64_t padding = NEXUS_PADDING;
	uint64_t m = (s-1) & ~(padding -1);
	return m + padding;
}

void Extractor::compress(OutputFile &file, nx::Signature &signature, nx::Node &node, nx::NodeData &data, Patch *patches) {

	if(signature.flags & Signature::CORTO) {

		crt::Encoder encoder(node.nvert, node.nface);

		for(uint32_t p = node.first_patch; p < node.last_patch(); p++)
			encoder.addGroup(patches[p].triangle_offset);

		if(node.nface == 0)
			encoder.addPositions((float *)data.coords(), pow(2, coord_q));
		else
			encoder.addPositions((float *)data.coords(), data.faces(signature, node.nvert), pow(2, coord_q));

		if(signature.vertex.hasNormals()) {
			encoder.addNormals((int16_t *)data.normals(signature, node.nvert), norm_bits,
							   node.nface == 0? crt::NormalAttr::DIFF : crt::NormalAttr::ESTIMATED);
		}

		if(signature.vertex.hasColors())
			encoder.addColors((unsigned char *)data.colors(signature, node.nvert), color_bits[0], color_bits[1], color_bits[2], color_bits[3]);

		if(signature.vertex.hasTextures())
			encoder.addUvs((float *)data.texCoords(signature, node.nvert), tex_step/512);
		encoder.encode();

		/*
		int nvert = encoder.nvert;
		int nface = encoder.nface;
		//cout << "Nvert: " << nvert << " Nface: " << nface << " (was: nv: " << node.nvert << " nf: " << node.nface << endl;
		//cout << "Compressed to: " << encoder.stream.size() << endl;
		//cout << "Ratio: " << 100.0f*encoder.stream.size()/(nvert*12 + nface*12) << "%" << endl;
		//cout << "Bpv: " << 8.0f*encoder.stream.size()/nvert << endl << endl;

		//cout << "Header: " << encoder.header_size << " bpv: " << (float)encoder.header_size/nvert << endl;

		crt::VertexAttribute *coord = encoder.data["position"];
		//cout << "Coord bpv; " << 8.0f*coord->size/nvert << " size: " << coord->size << endl;
		//cout << "Coord q: " << coord->q << " bits: " << coord->bits << endl << endl;

		crt::VertexAttribute *norm = encoder.data["normal"];
		if(norm) {
			//cout << "Normal bpv; " << 8.0f*norm->size/nvert << " size: " << norm->size << endl;
			//cout << "Normal q: " << norm->q << " bits: " << norm->bits << endl << endl;
		}

		crt::ColorAttr *color = dynamic_cast<crt::ColorAttr *>(encoder.data["color"]);
		if(color) {
			//cout << "Color bpv; " << 8.0f*color->size/nvert << " size: " << color->size << endl;
			//cout << "Color q: " << color->qc[0] << " " << color->qc[1] << " " << color->qc[2] << " " << color->qc[3] << endl;
		}

		crt::GenericAttr<int> *uv = dynamic_cast<crt::GenericAttr<int> *>(encoder.data["uv"]);
		if(uv) {
			//cout << "Uv bpv; " << 8.0f*uv->size/nvert << endl;
			//cout << "Uv q: " << uv->q << " bits: " << uv->bits << endl << endl;
		}

		//cout << "Face bpv; " << 8.0f*encoder.index.size/nvert << " size: " << encoder.index.size << endl; */


		if(encoder.stream.size() > 0)
			file.write((char *)&*encoder.stream.data(), encoder.stream.size());
		file.padTo(pad(file.pos()));
	}
}
