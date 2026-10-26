// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A reader of the file the program writes (format 0, the Nexus container), kept apart from the
// builder so that the tests do not trust the code they check.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "paths.h"

namespace bm3dtest {

// What the tests read of the file: the header, the encoded node textures, the texture of each
// node and which nodes are leaves
struct NexusFile {
    uint32_t magic = 0;  // 'N' 'x' 's' ' ' in a little-endian word: 0x4e787320
    uint64_t nvert = 0;
    uint64_t nface = 0;
    uint32_t nNodes = 0;     // with the sink, the last node
    uint32_t nPatches = 0;
    uint32_t nTextures = 0;  // with the end marker, the last texture
    std::vector<std::vector<uint8_t>> textures;  // one per texture, as stored (padding included)
    std::vector<int> nodeTexture;                // the texture of each node but the sink
    std::vector<bool> leaf;                      // nodes whose patches all point to the sink
    std::vector<float> error;                    // the error of each node but the sink
    std::vector<std::array<float, 3>> center;    // the center of the bounding sphere of each node but the sink
};

constexpr uint32_t kNexusMagic = 0x4e787320u;

// Reads the file; a test failure, and what could be read, when it is too short
NexusFile readNexusFile(const fs::path& path);

// The encoded texture is a WebP image
bool isWebp(const std::vector<uint8_t>& texture);

}  // namespace bm3dtest
