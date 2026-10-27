// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#include "model_saver.h"

#include "../common/error.h"
#include "../format/bm3d_writer.h"

namespace bm3d::detail {

void ModelSaver::save(ModelDag &dag, NodeTextureStore &textures, const std::string &filename, const std::string &metadata) const {
	std::vector<Node> &nodes = dag.nodes();
	std::vector<PatchRecord> &patches = dag.patches();

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
	const float step = coordinateStep(compression_.errorQ, nodes[0].error, leafErrors);
	const NodeCompressor compressor(dag.layout(), compression_, coordinateExponent(step));

	Bm3dWriter writer(filename, dag.layout(), {sink, static_cast<uint32_t>(patches.size()), static_cast<uint32_t>(textures.count())});

	//NODES
	for(uint32_t i = 0; i < sink; i++) {
		const Node &node = nodes[i];
		std::vector<uint32_t> patchEnds;
		for(uint32_t p = node.first_patch; p < nodes[i + 1].first_patch; p++)
			patchEnds.push_back(patches[p].triangleOffset);

		const unsigned char *memory = dag.chunks().getChunk(node.chunk);
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
	for(size_t texture = 0; texture < textures.count(); texture++) {
		textures.read(texture, image);
		writer.addTexture(image.data(), image.size());
	}

	SphereRecord sphere;
	for(int k = 0; k < 3; k++)
		sphere.center[k] = modelSphere.Center()[k];
	sphere.radius = modelSphere.Radius();
	writer.finish(sphere, metadata);
}

} //namespace
