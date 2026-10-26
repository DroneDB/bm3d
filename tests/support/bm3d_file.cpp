// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bm3d_file.h"

#include <cstring>

#include <gtest/gtest.h>

#include "format/bm3d_reader.h"

namespace bm3dtest {

namespace {

using bm3d::detail::Bm3dReader;

// Opens the file, or fails the test with what the reader says
bool open(const fs::path& path, std::unique_ptr<Bm3dReader>& reader) {
    try {
        reader = std::make_unique<Bm3dReader>(Bm3dReader::open(pathToUtf8(path)));
        return true;
    } catch (const std::exception& e) {
        ADD_FAILURE() << pathToUtf8(path) << ": " << e.what();
        return false;
    }
}

}  // namespace

Bm3dFile readBm3dFile(const fs::path& path) {
    Bm3dFile f;
    std::unique_ptr<Bm3dReader> reader;
    if (!open(path, reader))
        return f;
    const auto& h = reader->header();
    f.formatVersion = h.formatVersion;
    f.nvert = h.vertexCount;
    f.nface = h.faceCount;
    f.nNodes = h.nodeCount;
    f.nPatches = h.patchCount;
    f.nTextures = h.textureCount;
    f.hasColors = reader->layout().colors;
    f.contentHash = reader->contentHash();
    for (uint32_t t = 0; t < h.textureCount; t++)
        f.textures.push_back(reader->readTexture(t));
    for (uint32_t n = 0; n + 1 < h.nodeCount; n++) {
        const auto& node = reader->nodes()[n];
        f.leaf.push_back(reader->isLeaf(n));
        f.error.push_back(node.error);
        f.center.push_back({node.sphere.center[0], node.sphere.center[1], node.sphere.center[2]});
        f.nodeTexture.push_back(int(reader->nodeTexture(n)));
    }
    return f;
}

NodeMesh readNode(const fs::path& path, uint32_t node) {
    NodeMesh mesh;
    std::unique_ptr<Bm3dReader> reader;
    if (!open(path, reader))
        return mesh;
    EXPECT_LT(node + 1, reader->nodeCount()) << pathToUtf8(path);  // the last node is the sink
    if (node + 1 >= reader->nodeCount())
        return mesh;
    try {
        const bm3d::detail::DecodedNode decoded = reader->decodeNode(node);
        for (size_t i = 0; i + 2 < decoded.positions.size(); i += 3)
            mesh.positions.push_back({decoded.positions[i], decoded.positions[i + 1], decoded.positions[i + 2]});
        // a normal is a signed normalised vector: the value over 32767
        for (size_t i = 0; i + 2 < decoded.normals.size(); i += 3)
            mesh.normals.push_back({decoded.normals[i] / 32767.0f, decoded.normals[i + 1] / 32767.0f, decoded.normals[i + 2] / 32767.0f});
        for (size_t i = 0; i + 3 < decoded.colors.size(); i += 4)
            mesh.colors.push_back({decoded.colors[i], decoded.colors[i + 1], decoded.colors[i + 2], decoded.colors[i + 3]});
        mesh.indices.assign(decoded.indices.begin(), decoded.indices.end());
    } catch (const std::exception& e) {
        ADD_FAILURE() << pathToUtf8(path) << ": " << e.what();
    }
    return mesh;
}

std::string readMetadata(const fs::path& path) {
    std::unique_ptr<Bm3dReader> reader;
    if (!open(path, reader))
        return {};
    try {
        return reader->metadata();
    } catch (const std::exception& e) {
        ADD_FAILURE() << pathToUtf8(path) << ": " << e.what();
        return {};
    }
}

bool isWebp(const std::vector<uint8_t>& texture) {
    return texture.size() >= 12 && std::memcmp(texture.data(), "RIFF", 4) == 0 &&
           std::memcmp(texture.data() + 8, "WEBP", 4) == 0;
}

}  // namespace bm3dtest
