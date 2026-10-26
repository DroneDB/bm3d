// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The same input gives the same file, byte for byte, however many times it is built and however
// many threads build it. Each build is a process of its own, so that whatever the memory of a
// process holds that differs from one run to the next (the stack, the addresses) is different in
// every build.
//
// Before this was fixed, the sink node that ends the node table was written as it lay on the
// stack: the same model built twice gave files that differed in a few bytes of that node.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cli_support.h"
#include "model_files.h"
#include "nexus_file.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

constexpr int kBuilds = 3;                // builds of each model at each thread count
constexpr size_t kHeaderSize = 88, kNodeSize = 44;

std::vector<uint8_t> readAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The files of one model built with each thread count
struct Builds {
    std::vector<std::vector<uint8_t>> threads1, threads4;
};

std::vector<uint8_t> build(const TempDir& dir, const std::string& input, int threads, int run) {
    const fs::path out = dir.file("out-" + std::to_string(threads) + "-" + std::to_string(run) + ".nxz");
    const ProcessResult r = runBm3d({"--threads", std::to_string(threads), pathToUtf8(dir.file(input)), pathToUtf8(out)});
    EXPECT_EQ(r.exitCode, 0) << r.output();
    return readAll(out);
}

Builds buildTwice(const TempDir& dir, const std::string& input) {
    Builds b;
    for (int run = 0; run < kBuilds; run++) {
        b.threads1.push_back(build(dir, input, 1, run));
        b.threads4.push_back(build(dir, input, 4, run));
    }
    return b;
}

// Where the two files first differ, for the message of a failure
std::string firstDifference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size())
        return "sizes " + std::to_string(a.size()) + " and " + std::to_string(b.size());
    for (size_t i = 0; i < a.size(); i++)
        if (a[i] != b[i])
            return "first byte that differs: " + std::to_string(i) + " of " + std::to_string(a.size());
    return "identical";
}

void expectSameBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, const std::string& what) {
    EXPECT_TRUE(a == b) << what << ": " << firstDifference(a, b);
}

void expectDeterministic(const Builds& b) {
    ASSERT_FALSE(b.threads1.front().empty());
    for (int run = 1; run < kBuilds; run++) {
        expectSameBytes(b.threads1[0], b.threads1[run], "--threads 1, build " + std::to_string(run + 1));
        expectSameBytes(b.threads4[0], b.threads4[run], "--threads 4, build " + std::to_string(run + 1));
    }
    // The output does not depend on the number of threads either: the nodes are built in the
    // same order, and their textures are written in that order, whichever thread encodes them
    expectSameBytes(b.threads1[0], b.threads4[0], "--threads 1 against --threads 4");
}

template <typename T>
T readAt(const std::vector<uint8_t>& b, size_t at) {
    T v{};
    if (at + sizeof(T) <= b.size())
        std::memcpy(&v, b.data() + at, sizeof(T));
    return v;
}

// The last node of the table is not a node: it ends the table, and the leaves link to it. All it
// holds is where the node data ends and the number of patches, and the rest is zero, with the
// radius -1 of an empty sphere.
void expectDefinedSink(const std::vector<uint8_t>& file) {
    ASSERT_GE(file.size(), kHeaderSize);
    const uint32_t nNodes = readAt<uint32_t>(file, 60), nPatches = readAt<uint32_t>(file, 64);
    ASSERT_GE(nNodes, 2u);
    const size_t sink = kHeaderSize + kNodeSize * (nNodes - 1);
    EXPECT_EQ(readAt<uint16_t>(file, sink + 4), 0) << "nvert";
    EXPECT_EQ(readAt<uint16_t>(file, sink + 6), 0) << "nface";
    EXPECT_EQ(readAt<float>(file, sink + 8), 0.0f) << "error";
    for (size_t i = 0; i < 4; i++)
        EXPECT_EQ(readAt<int16_t>(file, sink + 12 + 2 * i), 0) << "cone " << i;
    for (size_t i = 0; i < 3; i++)
        EXPECT_EQ(readAt<float>(file, sink + 20 + 4 * i), 0.0f) << "sphere centre " << i;
    EXPECT_EQ(readAt<float>(file, sink + 32), -1.0f) << "sphere radius";
    EXPECT_EQ(readAt<float>(file, sink + 36), 0.0f) << "tight radius";
    EXPECT_EQ(readAt<uint32_t>(file, sink + 40), nPatches) << "first patch";
}

// Enough triangles for several levels of nodes (a node holds a few thousand)
constexpr int kWaveSquares = 200;  // 80000 triangles
constexpr int kGridCells = 72;     // 10368 triangles, over a texture of 1024 pixels

TEST(Determinism, aTexturedObjIsBuiltTheSameEveryTime) {
    TempDir dir;
    writeBmp(dir.file("texture.bmp"), makeRaster(1024, 1024, 3, [](int band, int x, int y) {
        return double((x * 7 + y * 13 + band * 50) % 256);
    }));
    writeGridModel(dir.path(), kGridCells, "texture.bmp");

    const Builds b = buildTwice(dir, "grid.obj");
    expectDeterministic(b);
    expectDefinedSink(b.threads1[0]);

    // the model is large enough to be worth the test: several levels, each node with its texture
    const NexusFile f = readNexusFile(dir.file("out-1-0.nxz"));
    EXPECT_GT(f.nNodes, 8u);
    EXPECT_GT(f.nTextures, 8u);
}

TEST(Determinism, aPlyIsBuiltTheSameEveryTime) {
    TempDir dir;
    writeTextFile(dir.file("wave.ply"), plyOf(waveTriangles(kWaveSquares)));

    const Builds b = buildTwice(dir, "wave.ply");
    expectDeterministic(b);
    expectDefinedSink(b.threads1[0]);

    const NexusFile f = readNexusFile(dir.file("out-1-0.nxz"));
    EXPECT_GT(f.nNodes, 8u);
}

}  // namespace
