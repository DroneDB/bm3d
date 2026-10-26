// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nexus_file.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

#include <gtest/gtest.h>

namespace bm3dtest {

namespace {

// The layout of the file: an 88 byte header, then a table of 44 byte nodes, one of 12 byte
// patches and one of 68 byte textures (whose first word is the offset of the texture in 256 byte
// units)
constexpr size_t kHeaderSize = 88, kNodeSize = 44, kPatchSize = 12, kTextureSize = 68;

template <typename T>
T readAt(const std::vector<uint8_t>& b, size_t at) {
    T v{};
    if (at + sizeof(T) <= b.size())
        std::memcpy(&v, b.data() + at, sizeof(T));
    return v;
}

}  // namespace

NexusFile readNexusFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    NexusFile f;
    EXPECT_GE(b.size(), kHeaderSize) << pathToUtf8(path);
    if (b.size() < kHeaderSize)
        return f;
    f.magic = readAt<uint32_t>(b, 0);
    f.nvert = readAt<uint64_t>(b, 8);
    f.nface = readAt<uint64_t>(b, 16);
    f.nNodes = readAt<uint32_t>(b, 60);
    f.nPatches = readAt<uint32_t>(b, 64);
    f.nTextures = readAt<uint32_t>(b, 68);

    const size_t nodes = kHeaderSize, patches = nodes + kNodeSize * f.nNodes,
                 textures = patches + kPatchSize * f.nPatches;
    std::vector<uint64_t> offsets;
    for (uint32_t t = 0; t < f.nTextures; t++)
        offsets.push_back(uint64_t(readAt<uint32_t>(b, textures + kTextureSize * t)) * 256);
    for (size_t t = 0; t + 1 < offsets.size(); t++) {
        const uint64_t end = std::min<uint64_t>(offsets[t + 1], b.size());
        const uint64_t begin = std::min<uint64_t>(offsets[t], end);
        f.textures.emplace_back(b.begin() + std::ptrdiff_t(begin), b.begin() + std::ptrdiff_t(end));
    }

    const uint32_t sink = f.nNodes > 0 ? f.nNodes - 1 : 0;
    for (uint32_t n = 0; n < sink; n++) {
        const auto first = readAt<uint32_t>(b, nodes + kNodeSize * n + 40);
        const auto end = readAt<uint32_t>(b, nodes + kNodeSize * (n + 1) + 40);
        bool leaf = true;
        for (uint32_t p = first; p < end; p++)
            leaf = leaf && readAt<uint32_t>(b, patches + kPatchSize * p) == sink;
        f.leaf.push_back(leaf);
        f.error.push_back(readAt<float>(b, nodes + kNodeSize * n + 8));
        f.center.push_back({readAt<float>(b, nodes + kNodeSize * n + 20), readAt<float>(b, nodes + kNodeSize * n + 24),
                            readAt<float>(b, nodes + kNodeSize * n + 28)});
        f.nodeTexture.push_back(f.nTextures > 0 ? int(readAt<uint32_t>(b, patches + kPatchSize * first + 8)) : -1);
    }
    return f;
}

bool isWebp(const std::vector<uint8_t>& texture) {
    return texture.size() >= 12 && std::memcmp(texture.data(), "RIFF", 4) == 0 &&
           std::memcmp(texture.data() + 8, "WEBP", 4) == 0;
}

}  // namespace bm3dtest
