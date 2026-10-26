// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The golden files: small models built by the program, whose content hashes (docs/FORMAT.md,
// section 9) are in hashes.txt. A change of the code that alters the bytes of a file outside its
// metadata changes a hash and fails here, so it is a decision and not an accident. The files are
// also the fixtures of the other readers of the format: each test copies its file to the folder
// BM3D_GOLDEN_DIR (by default <build folder>/golden), with the hash in <name>.sha256 next to it.
//
// A hash that has to change on purpose is replaced in hashes.txt by the one the failure prints.
// The inputs are written by the tests with integer coordinates, so that nothing in them depends
// on the machine.

#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "bm3d_file.h"
#include "cli_support.h"
#include "format/bm3d_reader.h"
#include "format/json_object.h"
#include "model_files.h"
#include "pictures.h"
#include "temp_dir.h"
#include "version_file.h"

namespace {

using namespace bm3dtest;

fs::path fixtureFolder() {
    const char* folder = std::getenv("BM3D_GOLDEN_DIR");
    return pathFromUtf8(folder && *folder ? folder : BM3D_GOLDEN_DEFAULT_DIR);
}

// name -> content hash, from hashes.txt
std::map<std::string, std::string> expectedHashes() {
    std::map<std::string, std::string> hashes;
    std::ifstream in(BM3D_GOLDEN_HASHES);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream words(line);
        std::string name, hash;
        words >> name >> hash;
        hashes[name] = hash;
    }
    return hashes;
}

// Builds `input` with the program, checks the file with the reader and against the hash of
// hashes.txt, and leaves a copy in the folder of the fixtures. Returns the path of the file.
fs::path buildGolden(const TempDir& dir, const std::string& name, const fs::path& input) {
    const fs::path output = dir.file(name + ".bm3d");
    const ProcessResult r = runBm3d({"--threads", "1", pathToUtf8(input), pathToUtf8(output)});
    EXPECT_EQ(r.exitCode, 0) << r.output();

    // every node decodes to the counts of its record, and every texture is a WebP image
    bm3d::detail::Bm3dReader reader = bm3d::detail::Bm3dReader::open(pathToUtf8(output));
    uint64_t vertices = 0, faces = 0;
    for (uint32_t n = 0; n + 1 < reader.nodeCount(); n++) {
        const bm3d::detail::DecodedNode decoded = reader.decodeNode(n);
        EXPECT_EQ(decoded.positions.size(), 3u * reader.nodes()[n].vertexCount) << name << " node " << n;
        EXPECT_EQ(decoded.indices.size(), 3u * reader.nodes()[n].faceCount) << name << " node " << n;
        vertices += reader.nodes()[n].vertexCount;
        faces += reader.nodes()[n].faceCount;
    }
    for (uint32_t t = 0; t < reader.header().textureCount; t++)
        EXPECT_TRUE(isWebp(reader.readTexture(t))) << name << " texture " << t;
    EXPECT_EQ(reader.header().vertexCount, vertices) << name;
    EXPECT_EQ(reader.header().faceCount, faces) << name;

    const std::string hash = reader.contentHash();
    const auto expected = expectedHashes();
    const auto found = expected.find(name);
    if (found == expected.end())
        ADD_FAILURE() << "hashes.txt has no hash for " << name << "; add the line\n" << name << " " << hash;
    else
        EXPECT_EQ(hash, found->second) << name << ": the content of the file changed; if it was meant to, hashes.txt gets\n"
                                       << name << " " << hash;

    const fs::path folder = fixtureFolder();
    std::error_code ec;
    fs::create_directories(folder, ec);
    fs::copy_file(output, folder / (name + ".bm3d"), fs::copy_options::overwrite_existing, ec);
    EXPECT_FALSE(ec) << ec.message();
    writeTextFile(folder / (name + ".sha256"), hash + "  " + name + ".bm3d\n");
    return output;
}

// --- the inputs -----------------------------------------------------------------------------------

// A cube of side 2 with square faces (the OBJ loader cuts each into two triangles)
const char* kCubeObj =
    "v 0 0 0\nv 2 0 0\nv 2 2 0\nv 0 2 0\nv 0 0 2\nv 2 0 2\nv 2 2 2\nv 0 2 2\n"
    "f 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 2 3 7 6\nf 3 4 8 7\nf 4 1 5 8\n";

// A pyramid with a colour for each vertex
const char* kPyramidPly =
    "ply\nformat ascii 1.0\nelement vertex 5\n"
    "property float x\nproperty float y\nproperty float z\n"
    "property uchar red\nproperty uchar green\nproperty uchar blue\nproperty uchar alpha\n"
    "element face 5\nproperty list uchar int vertex_indices\nend_header\n"
    "0 0 0 255 0 0 255\n4 0 0 0 255 0 255\n4 4 0 0 0 255 255\n0 4 0 255 255 0 255\n2 2 3 255 255 255 255\n"
    "4 0 3 2 1\n3 0 1 4\n3 1 2 4\n3 2 3 4\n3 3 0 4\n";

// A square, and a triangle whose first two corners are the same point: the encoder drops it, and
// the corner that only it has
std::vector<Triangle> degenerateTriangles() {
    return {
        {{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}}},
        {{{0, 0, 0}, {4, 4, 0}, {0, 4, 0}}},
        {{{4, 4, 0}, {4, 4, 0}, {9, 9, 9}}},
    };
}

TEST(GoldenFixtures, aCubeOfSquareFacesFromAnObj) {
    TempDir dir;
    writeTextFile(dir.file("cube.obj"), kCubeObj);
    const fs::path file = buildGolden(dir, "cube", dir.file("cube.obj"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);  // one node and the sink
    EXPECT_EQ(f.nface, 12u);
    EXPECT_EQ(f.nTextures, 0u);
    EXPECT_FALSE(f.hasColors);

    // the metadata names the program and the input
    const std::string metadata = readMetadata(file);
    EXPECT_NE(metadata.find("\"generator\":\"bm3d " + versionFileText() + "\""), std::string::npos) << metadata;
    EXPECT_NE(metadata.find("\"name\":\"cube.obj\""), std::string::npos) << metadata;
}

TEST(GoldenFixtures, aPyramidWithColoursFromAPly) {
    TempDir dir;
    writeTextFile(dir.file("pyramid.ply"), kPyramidPly);
    const fs::path file = buildGolden(dir, "pyramid-colors", dir.file("pyramid.ply"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);
    EXPECT_EQ(f.nface, 6u);
    EXPECT_TRUE(f.hasColors);
}

TEST(GoldenFixtures, aWavySurfaceFromABinaryStl) {
    TempDir dir;
    writeBinaryFile(dir.file("wave.stl"), binaryStl(waveTriangles(4)));
    const fs::path file = buildGolden(dir, "wave", dir.file("wave.stl"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);
    EXPECT_EQ(f.nface, 32u);
}

// The encoder drops a triangle with two equal corners, and the vertex only it uses: the counts the
// file holds are the encoder's, lower than the ones of the input
TEST(GoldenFixtures, aDegenerateTriangleIsLeftOutOfTheCountsOfTheFile) {
    TempDir dir;
    writeTextFile(dir.file("degenerate.stl"), asciiStl(degenerateTriangles()));
    const fs::path file = buildGolden(dir, "degenerate", dir.file("degenerate.stl"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);
    EXPECT_EQ(f.nface, 2u);  // the input has 3 triangles
    EXPECT_EQ(f.nvert, 4u);  // and 5 different points

    std::vector<bm3d::JsonMember> members;
    ASSERT_EQ(bm3d::jsonObjectMembers(readMetadata(file), members), "");
    std::string source;
    for (const auto& m : members)
        if (m.name == "source")
            source = m.rawValue;
    EXPECT_NE(source.find("\"faces\":3"), std::string::npos) << source;  // what was read, before the encoder
}

TEST(GoldenFixtures, aTexturedSquareFromAnObj) {
    TempDir dir;
    writePng(dir.file("texture.png"), gradientRaster(3));
    writeTexturedModel(dir.path(), "texture.png");
    const fs::path file = buildGolden(dir, "textured", dir.file("model.obj"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);
    EXPECT_EQ(f.nTextures, 1u);
    EXPECT_EQ(f.nface, 2u);
}

// More triangles than a node holds: a hierarchy of several nodes, whose patches name children
TEST(GoldenFixtures, aSurfaceOfSeveralNodesFromABinaryStl) {
    TempDir dir;
    writeBinaryFile(dir.file("grid.stl"), binaryStl(waveTriangles(100)));
    const fs::path file = buildGolden(dir, "grid", dir.file("grid.stl"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_GT(f.nNodes, 3u);
    bm3d::detail::Bm3dReader reader = bm3d::detail::Bm3dReader::open(pathToUtf8(file));
    EXPECT_EQ(reader.rootCount(), 1u);
    EXPECT_FALSE(reader.isLeaf(0));
}

}  // namespace
