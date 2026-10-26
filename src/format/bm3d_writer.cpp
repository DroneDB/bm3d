// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bm3d_writer.h"

#include <filesystem>
#include <system_error>

#include "../common/error.h"
#include "../common/pathutil.h"
#include "format_error.h"
#include "metadata.h"

namespace bm3d::detail {

void checkMetadataLength(uint64_t length) {
    if (length > kMaxMetadataLength)
        throw MetadataTooLargeError("the metadata is " + std::to_string(length) + " bytes; a file takes at most " +
                                    std::to_string(kMaxMetadataLength));
}

Bm3dWriter::Bm3dWriter(const std::string& path, const VertexLayout& layout, const Counts& counts)
    : path_(path), partPath_(path + ".part"), layout_(layout), counts_(counts) {
    if (layout.texCoords != (counts.textures > 0))
        throw Error("the texture coordinates of a model are there if and only if it has textures");
    if (counts.nodes == 0)
        throw Error("a model needs at least one node");
    index_ = indexLayout(counts.nodes + 1, counts.patches, counts.textures);
    nodes_.reserve(counts.nodes);
    patches_.reserve(counts.patches);
    textures_.reserve(counts.textures);

    file_.open(partPath_);
    created_ = true;
    try {
        // the room of the header and the index, filled in by finish()
        file_.padTo(kHeaderSize + index_.length);
    } catch (...) {
        discard();
        throw;
    }
}

Bm3dWriter::~Bm3dWriter() {
    if (!finished_)
        discard();
}

void Bm3dWriter::discard() {
    if (!created_)
        return;
    created_ = false;
    // a destructor must not throw, and a file that cannot be removed is no worse than one left by a crash
    try {
        file_.close();
    } catch (...) {
    }
    std::error_code ec;
    std::filesystem::remove(nativePath(partPath_), ec);
}

void Bm3dWriter::padToAlignment() {
    file_.padTo(alignUp(file_.pos()));
}

void Bm3dWriter::addNode(const NodeRecord& node, const PatchRecord* patches, size_t patchCount, const void* payload,
                         uint64_t length) {
    const size_t index = nodes_.size();
    if (index >= counts_.nodes || patches_.size() + patchCount > counts_.patches)
        throw Error("more nodes or patches than the writer was told of");
    if (!textures_.empty())
        throw Error("a node is added after a texture");
    if (node.faceCount == 0 || node.vertexCount < 3)
        throw Error("node " + std::to_string(index) + " has no triangle once encoded (" +
                    std::to_string(node.vertexCount) + " vertices, " + std::to_string(node.faceCount) +
                    " triangles): the encoder dropped them all, and a model has no nodes of points");
    if (patchCount == 0)
        throw Error("node " + std::to_string(index) + " has no patch");
    if (length == 0)
        throw Error("node " + std::to_string(index) + " has an empty payload");
    uint32_t previous = 0;
    for (size_t p = 0; p < patchCount; p++) {
        if (patches[p].triangleOffset < previous)
            throw Error("the triangle offsets of node " + std::to_string(index) + " decrease");
        previous = patches[p].triangleOffset;
    }
    if (previous != node.faceCount)
        throw Error("the last triangle offset of node " + std::to_string(index) + " is " + std::to_string(previous) +
                    " and the node has " + std::to_string(node.faceCount) + " triangles");

    NodeRecord record = node;
    record.payloadOffset = file_.pos();
    record.payloadLength = length;
    record.firstPatch = static_cast<uint32_t>(patches_.size());
    file_.write(payload, static_cast<size_t>(length));
    padToAlignment();

    nodes_.push_back(record);
    patches_.insert(patches_.end(), patches, patches + patchCount);
    vertexCount_ += node.vertexCount;
    faceCount_ += node.faceCount;
}

void Bm3dWriter::addTexture(const void* image, uint64_t length) {
    if (nodes_.size() != counts_.nodes)
        throw Error("a texture is added before the last node");
    if (textures_.size() >= counts_.textures)
        throw Error("more textures than the writer was told of");
    if (length == 0)
        throw Error("texture " + std::to_string(textures_.size()) + " is empty");
    TextureRecord record;
    record.offset = file_.pos();
    record.length = length;
    file_.write(image, static_cast<size_t>(length));
    padToAlignment();
    textures_.push_back(record);
}

void Bm3dWriter::finish(const SphereRecord& sphere, const std::string& metadata) {
    if (nodes_.size() != counts_.nodes || patches_.size() != counts_.patches || textures_.size() != counts_.textures)
        throw Error("the model is not complete: " + std::to_string(nodes_.size()) + " of " +
                    std::to_string(counts_.nodes) + " nodes, " + std::to_string(patches_.size()) + " of " +
                    std::to_string(counts_.patches) + " patches and " + std::to_string(textures_.size()) + " of " +
                    std::to_string(counts_.textures) + " textures");
    checkMetadataLength(metadata.size());
    if (!metadata.empty()) {
        const std::string problem = metadataBlockError(metadata);
        if (!problem.empty())
            throw Error("the metadata is wrong: " + problem);
    }

    HeaderRecord header;
    header.flags = kFlagCortoCompressed | (counts_.textures > 0 ? kFlagTextureWebp : 0);
    layoutToSlots(layout_, header.vertexLayout, header.faceLayout);
    header.vertexCount = vertexCount_;
    header.faceCount = faceCount_;
    header.metadataOffset = file_.pos();
    header.metadataLength = metadata.size();
    header.indexOffset = kHeaderSize;
    header.indexLength = index_.length;
    header.nodeCount = counts_.nodes + 1;
    header.patchCount = counts_.patches;
    header.textureCount = counts_.textures;
    header.sphere = sphere;

    // the sink ends the node table: the patches of the node before it end where its first patch is
    NodeRecord sink;
    sink.firstPatch = counts_.patches;

    std::vector<uint8_t> bytes(kHeaderSize + index_.length, 0);
    encodeHeaderRecord(header, bytes.data());
    uint8_t* at = bytes.data() + kHeaderSize + index_.nodeTable;
    for (const NodeRecord& node : nodes_) {
        encodeNodeRecord(node, at);
        at += kNodeRecordSize;
    }
    encodeNodeRecord(sink, at);
    at = bytes.data() + kHeaderSize + index_.patchTable;
    for (const PatchRecord& patch : patches_) {
        encodePatchRecord(patch, at);
        at += kPatchRecordSize;
    }
    at = bytes.data() + kHeaderSize + index_.textureTable;
    for (const TextureRecord& texture : textures_) {
        encodeTextureRecord(texture, at);
        at += kTextureRecordSize;
    }

    file_.write(metadata);
    file_.seek(0);
    file_.write(bytes.data(), bytes.size());
    file_.close();

    std::error_code ec;
    std::filesystem::rename(nativePath(partPath_), nativePath(path_), ec);
    if (ec)
        throw Error("could not rename " + partPath_ + " to " + path_ + ": " + ec.message());
    finished_ = true;
    created_ = false;
}

}  // namespace bm3d::detail
