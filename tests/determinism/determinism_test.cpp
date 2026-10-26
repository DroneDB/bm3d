// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The same input gives the same output, however many times it is built and however many threads
// build it: the files have the same content hash (docs/FORMAT.md, section 9: every byte but the
// metadata block, which holds the number of threads), and the files of builds with the same
// number of threads are the same byte for byte. Each build is a process of its own, so that
// whatever the memory of a process holds that differs from one run to the next (the stack, the
// addresses) is different in every build.
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
#include "format/bm3d_reader.h"
#include "model_files.h"
#include "bm3d_file.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

constexpr int kBuilds = 3;                // builds of each model at each thread count

std::vector<uint8_t> readAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Built {
    std::vector<uint8_t> bytes;
    std::string contentHash;
};

// The files of one model built with each thread count
struct Builds {
    std::vector<Built> threads1, threads4;
};

Built build(const TempDir& dir, const std::string& input, int threads, int run) {
    const fs::path out = dir.file("out-" + std::to_string(threads) + "-" + std::to_string(run) + ".bm3d");
    const ProcessResult r = runBm3d({"--threads", std::to_string(threads), pathToUtf8(dir.file(input)), pathToUtf8(out)});
    EXPECT_EQ(r.exitCode, 0) << r.output();
    return {readAll(out), readBm3dFile(out).contentHash};
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

void expectSameFile(const Built& a, const Built& b, const std::string& what) {
    EXPECT_EQ(a.contentHash, b.contentHash) << what;
    EXPECT_TRUE(a.bytes == b.bytes) << what << ": " << firstDifference(a.bytes, b.bytes);
}

void expectDeterministic(const Builds& b) {
    ASSERT_FALSE(b.threads1.front().bytes.empty());
    EXPECT_EQ(b.threads1[0].contentHash.size(), 64u);
    for (int run = 1; run < kBuilds; run++) {
        expectSameFile(b.threads1[0], b.threads1[run], "--threads 1, build " + std::to_string(run + 1));
        expectSameFile(b.threads4[0], b.threads4[run], "--threads 4, build " + std::to_string(run + 1));
    }
    // The output does not depend on the number of threads either: the nodes are built in the
    // same order, and their textures are written in that order, whichever thread encodes them.
    // The metadata block records the threads, so the bytes differ there and nowhere else.
    EXPECT_EQ(b.threads1[0].contentHash, b.threads4[0].contentHash) << "--threads 1 against --threads 4";
    EXPECT_NE(b.threads1[0].bytes, b.threads4[0].bytes) << "the metadata names the threads";
}

// The last node of the table is not a node: it ends the table, and the leaves link to it. All it
// holds is the number of patches, and the rest is zero. The gaps between the items of the file
// are zero too, so that no byte of the file is whatever the memory held.
void expectDefinedSink(const fs::path& path) {
    bm3d::detail::Bm3dReader reader = bm3d::detail::Bm3dReader::open(pathToUtf8(path));
    const bm3d::detail::NodeRecord& sink = reader.nodes()[reader.sink()];
    EXPECT_EQ(sink.payloadOffset, 0u);
    EXPECT_EQ(sink.payloadLength, 0u);
    EXPECT_EQ(sink.vertexCount, 0) << "vertexCount";
    EXPECT_EQ(sink.faceCount, 0) << "faceCount";
    EXPECT_EQ(sink.error, 0.0f) << "error";
    for (size_t i = 0; i < 4; i++)
        EXPECT_EQ(sink.cone[i], 0) << "cone " << i;
    for (size_t i = 0; i < 3; i++)
        EXPECT_EQ(sink.sphere.center[i], 0.0f) << "sphere centre " << i;
    EXPECT_EQ(sink.sphere.radius, 0.0f) << "sphere radius";
    EXPECT_EQ(sink.tightRadius, 0.0f) << "tight radius";
    EXPECT_EQ(sink.firstPatch, reader.header().patchCount) << "first patch";

    // between the end of an item and the next multiple of 8
    const std::vector<uint8_t> file = readAll(path);
    std::vector<std::pair<uint64_t, uint64_t>> items;
    for (uint32_t n = 0; n < reader.sink(); n++)
        items.push_back({reader.nodes()[n].payloadOffset, reader.nodes()[n].payloadLength});
    for (const auto& t : reader.textures())
        items.push_back({t.offset, t.length});
    for (const auto& [offset, length] : items)
        for (uint64_t i = offset + length; i < bm3d::detail::alignUp(offset + length); i++)
            ASSERT_EQ(file[size_t(i)], 0) << "padding byte " << i;
    for (uint64_t i = 128 + reader.header().patchCount * 12ull + 56ull * reader.nodeCount(); i < 128 + bm3d::detail::indexLayout(reader.nodeCount(), reader.header().patchCount, reader.header().textureCount).textureTable; i++)
        ASSERT_EQ(file[size_t(i)], 0) << "index padding byte " << i;
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
    expectDefinedSink(dir.file("out-1-0.bm3d"));

    // the model is large enough to be worth the test: several levels, each node with its texture
    const Bm3dFile f = readBm3dFile(dir.file("out-1-0.bm3d"));
    EXPECT_GT(f.nNodes, 8u);
    EXPECT_GE(f.nTextures, 8u);
}

TEST(Determinism, aPlyIsBuiltTheSameEveryTime) {
    TempDir dir;
    writeTextFile(dir.file("wave.ply"), plyOf(waveTriangles(kWaveSquares)));

    const Builds b = buildTwice(dir, "wave.ply");
    expectDeterministic(b);
    expectDefinedSink(dir.file("out-1-0.bm3d"));

    const Bm3dFile f = readBm3dFile(dir.file("out-1-0.bm3d"));
    EXPECT_GT(f.nNodes, 8u);
}

}  // namespace
