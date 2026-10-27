// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#include "level_builder.h"

#include <assert.h>
#include <string.h>
#include <memory>

#include "kdtree.h"
#include "mesh.h"
#include "tmesh.h"
#include "../common/error.h"
#include "../io/mesh/meshstream.h"

namespace bm3d::detail {

namespace {

//the most vertices a node may have: a node indexes its vertices with 16 bits (at most 65535), and
//the builder has always stopped a little below that
const size_t MAX_NODE_VERTICES = 60000;

//Streams the first nface triangles of the simplified mesh, those that are not degenerate, to the
//stream the next level is made from
template<class SimplifiedMesh>
void streamTriangles(SimplifiedMesh &mesh, int nface, uint32_t node, StreamSoup &output) {
	std::unique_ptr<Triangle[]> triangles(new Triangle[nface]);
	mesh.getTriangles(triangles.get(), node);
	for(int i = 0; i < nface; i++) {
		Triangle &t = triangles[i];
		if(!t.isDegenerate())
			output.pushTriangle(triangles[i]);
	}
}

} //namespace

LevelBuilder::LevelBuilder(ModelDag &dag, float scaling, NodeTextures *textures):
	dag_(dag), scaling_(scaling), textures_(textures) {
}

void LevelBuilder::buildLevel(KDTreeSoup &input, StreamSoup &output, int level, float textureScale, bool keepTriangles) {
	if(textures_)
		textures_->baker.beginLevel(level, textureScale);

	for(unsigned int block = 0; block < input.nBlocks(); block++) {
		if(textures_)
			buildTexturedNode(input, output, block, level, keepTriangles);
		else
			buildPlainNode(input, output, block);
	}
}

uint32_t LevelBuilder::storeChunk(const std::vector<unsigned char> &serialized) {
	VirtualChunks &chunks = dag_.chunks();
	const uint32_t chunk = chunks.addChunk(serialized.size());
	unsigned char *where = chunks.getChunk(chunk);
	memcpy(where, serialized.data(), serialized.size());
	chunks.dropChunk(chunk); //no neede anymore
	return chunk;
}

void LevelBuilder::buildPlainNode(KDTreeSoup &input, StreamSoup &output, unsigned int block) {
	Mesh mesh;
	int ntriangles = 0;
	{
		Soup soup = input.get(block);
		assert(soup.size() < (1<<16));
		if(soup.size() == 0) return;

		ntriangles = soup.size();
		mesh.load(soup);
	}

	//no need to mutex the input, it won't change anything.
	input.lock(mesh, block);

	std::vector<unsigned char> serialized(mesh.serializedSize(dag_.layout()));
	std::vector<PatchRecord> patches;
	mesh.serialize(serialized.data(), dag_.layout(), patches);
	const uint32_t chunk = storeChunk(serialized);

	const Node node = mesh.getNode(); //get node data before simplification

	mesh.lockVertices();
	mesh.quadricInit(); //needed only if Mesh::QUADRICS
	const float error = mesh.simplify(ntriangles*scaling_, Mesh::QUADRICS);
	const int nface = mesh.fn;

	const uint32_t current = dag_.addNode(node, chunk, error, patches, NodeBox(&input, block));
	streamTriangles(mesh, nface, current, output);
}

void LevelBuilder::buildTexturedNode(KDTreeSoup &input, StreamSoup &output, unsigned int block, int level, bool keepTriangles) {
	TMesh mesh;
	TMesh tmp; //this is needed saving a mesh with vertices on seams duplicated., and for node tex coordinates to be rearranged

	int ntriangles = 0;
	{
		Soup soup = input.get(block);
		assert(soup.size() < (1<<16));
		if(soup.size() == 0) return;

		ntriangles = soup.size();
		mesh.load(soup);
	}

	input.lock(mesh, block);
	//we need to replicate vertices where textured seams occours
	vcg::tri::Append<TMesh,TMesh>::MeshCopy(tmp,mesh);
	for(int i = 0; i < tmp.face.size(); i++) {
		tmp.face[i].node = mesh.face[i].node;
		tmp.face[i].tex = mesh.face[i].tex;
	}
	tmp.splitSeams(dag_.layout());
	//an error, not a node left out: skipping the block would leave a hole in the model, at
	//this level and at every coarser one built from it
	if(tmp.vert.size() > MAX_NODE_VERTICES)
		throw NodeTooLargeError("a node of " + std::to_string(ntriangles) + " triangles has " +
		                            std::to_string(tmp.vert.size()) + " vertices once its texture seams are "
		                            "split, more than the " + std::to_string(MAX_NODE_VERTICES) +
		                            " a node holds (the texture parametrization is too fragmented)");

	//save node in the temporary structure
	std::vector<unsigned char> serialized(tmp.serializedSize(dag_.layout()));
	std::vector<PatchRecord> patches;

	const NodeTexture baked = textures_->baker.bake(tmp, level, !keepTriangles);
	if(baked.image.isNull())
		throw Error("could not create the texture of a node");
	tmp.serialize(serialized.data(), dag_.layout(), patches);

	const size_t texture = textures_->store.reserve();
	for(PatchRecord &patch: patches)
		patch.texture = texture;
	textures_->encoders.submit(texture, baked.image);

	//VICIUOS TRICK: we could save only a texture every 2 geometry levels since the patch is contained also to a parent node.
	//we could store the texture in the parent nodes i and have it good also for the children node.
	//but only starting from the bottom.

	const uint32_t chunk = storeChunk(serialized);

	const Node node = tmp.getNode();

	int nvert = ntriangles*scaling_;
	if(keepTriangles)
		nvert = ntriangles;
	else if(nvert < 64) //don't simplify too much!
		nvert = 64;
	mesh.simplify(nvert, TMesh::QUADRICS);
	const int nface = mesh.fn;

	const uint32_t current = dag_.addNode(node, chunk, baked.error, patches, NodeBox(&input, block));
	streamTriangles(mesh, nface, current, output);
}

} //namespace
