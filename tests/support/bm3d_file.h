// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the tests read of the .bm3d file the program writes, through the reader of src/format
// (the one the specification is implemented in; scripts/dump.py is a second, independent one).

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "paths.h"

namespace bm3dtest {

// What the tests read of the file: the header, the encoded node textures, the texture of each
// node and which nodes are leaves
struct Bm3dFile {
    uint32_t formatVersion = 0;
    uint64_t nvert = 0;      // the vertices of the nodes together (the header's vertexCount)
    uint64_t nface = 0;
    uint32_t nNodes = 0;     // with the sink, the last node
    uint32_t nPatches = 0;
    uint32_t nTextures = 0;
    bool hasColors = false;  // the vertices carry a colour
    std::vector<std::vector<uint8_t>> textures;  // one per texture, exactly the bytes of the image
    std::vector<int> nodeTexture;                // the texture of each node but the sink, -1 for none
    std::vector<bool> leaf;                      // nodes whose patches all point to the sink
    std::vector<float> error;                    // the error of each node but the sink
    std::vector<std::array<float, 3>> center;    // the center of the bounding sphere of each node but the sink
    std::string contentHash;                     // SHA-256 of the file but for the metadata block
};

// Reads the file; a test failure (and what could be read) when it is not a file of the format
Bm3dFile readBm3dFile(const fs::path& path);

// The geometry of one node, decoded with corto (the library the program compresses it with)
struct NodeMesh {
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;   // empty when the file holds none
    std::vector<std::array<uint8_t, 4>> colors;  // red, green, blue, alpha; empty when the file holds none
    std::vector<uint32_t> indices;               // three per triangle
};

// Decodes node `node` of the file (a failure of the test when it cannot)
NodeMesh readNode(const fs::path& path, uint32_t node = 0);

// The metadata block of the file, a JSON object (a failure of the test when it cannot be read)
std::string readMetadata(const fs::path& path);

// The encoded texture is a WebP image
bool isWebp(const std::vector<uint8_t>& texture);

}  // namespace bm3dtest
