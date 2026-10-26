// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The file the library writes, read as the specification says (docs/FORMAT.md): every node decodes
// to the counts and the groups its record and patches say, the counts are the encoder's, and the
// metadata block holds what the builder read of the input and the members the caller added.

#include <cmath>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "bm3d_file.h"
#include "format/bm3d_reader.h"
#include "format/json_object.h"
#include "model_files.h"
#include "pictures.h"
#include "temp_dir.h"
#include "version_file.h"

namespace {

using namespace bm3dtest;
using bm3d::detail::Bm3dReader;

// The raw text of a top-level member of the metadata block, or "" when it has none
std::string member(const std::string& metadata, const std::string& name) {
    std::vector<bm3d::JsonMember> members;
    EXPECT_EQ(bm3d::jsonObjectMembers(metadata, members), "") << metadata;
    for (const auto& m : members)
        if (m.name == name)
            return m.rawValue;
    return {};
}

void build(const fs::path& input, const fs::path& output, const std::string& metadata = {}) {
    bm3d::BuildOptions options;
    options.threads = 2;
    options.metadata = metadata;
    bm3d::build(input, output, options);
}

// Every real node decodes to the counts of its record, its groups end where its patches end, and
// the header counts the nodes together
void expectEveryNodeConsistent(const fs::path& path) {
    Bm3dReader reader = Bm3dReader::open(pathToUtf8(path));
    uint64_t vertices = 0, faces = 0;
    for (uint32_t n = 0; n < reader.sink(); n++) {
        const auto& node = reader.nodes()[n];
        bm3d::detail::DecodedNode decoded;
        try {
            decoded = reader.decodeNode(n);
        } catch (const std::exception& e) {
            ADD_FAILURE() << "node " << n << ": " << e.what();
            continue;
        }
        EXPECT_EQ(decoded.positions.size(), 3u * node.vertexCount) << n;
        EXPECT_EQ(decoded.indices.size(), 3u * node.faceCount) << n;
        for (uint16_t index : decoded.indices)
            EXPECT_LT(index, node.vertexCount) << n;
        std::vector<uint32_t> ends;
        for (uint32_t p = reader.patchBegin(n); p < reader.patchEnd(n); p++)
            ends.push_back(reader.patches()[p].triangleOffset);
        EXPECT_EQ(decoded.groupEnds, ends) << n;
        vertices += node.vertexCount;
        faces += node.faceCount;
    }
    EXPECT_EQ(reader.header().vertexCount, vertices);
    EXPECT_EQ(reader.header().faceCount, faces);
}

TEST(FormatOutput, aModelOfSeveralLevelsHasNodesThatDecodeToTheirRecords) {
    TempDir dir;
    writeBinaryFile(dir.file("wave.stl"), binaryStl(waveTriangles(150)));
    build(dir.file("wave.stl"), dir.file("wave.bm3d"));
    const Bm3dFile f = readBm3dFile(dir.file("wave.bm3d"));
    EXPECT_GT(f.nNodes, 3u);
    expectEveryNodeConsistent(dir.file("wave.bm3d"));
}

TEST(FormatOutput, aTexturedModelOfSeveralLevelsHasNodesThatDecodeToTheirRecords) {
    TempDir dir;
    writeBmp(dir.file("texture.bmp"), makeRaster(512, 512, 3, [](int band, int x, int y) { return double((x * 3 + y * 5 + band * 40) % 256); }));
    writeGridModel(dir.path(), 72, "texture.bmp");
    build(dir.file("grid.obj"), dir.file("grid.bm3d"));
    const Bm3dFile f = readBm3dFile(dir.file("grid.bm3d"));
    EXPECT_GT(f.nNodes, 3u);
    EXPECT_GE(f.nTextures, 3u);
    expectEveryNodeConsistent(dir.file("grid.bm3d"));
    Bm3dReader reader = Bm3dReader::open(pathToUtf8(dir.file("grid.bm3d")));
    EXPECT_EQ(reader.header().flags, 3u);
    EXPECT_TRUE(reader.layout().texCoords);
}

// A model with degenerate triangles, in every node and every patch: the encoder drops them, the
// counts of the file are the encoder's, and the decoded counts are the counts of the records
TEST(FormatOutput, theTrianglesTheEncoderDropsAreNotInTheCountsOfTheFile) {
    TempDir dir;
    std::vector<Triangle> triangles = waveTriangles(120);
    const size_t input = triangles.size();
    size_t degenerate = 0;
    for (size_t i = 11; i < triangles.size(); i += 97) {
        triangles[i][1] = triangles[i][0];  // two corners at the same point
        degenerate++;
    }
    ASSERT_GT(degenerate, 100u);
    writeBinaryFile(dir.file("wave.stl"), binaryStl(triangles));
    build(dir.file("wave.stl"), dir.file("wave.bm3d"));
    expectEveryNodeConsistent(dir.file("wave.bm3d"));

    Bm3dReader reader = Bm3dReader::open(pathToUtf8(dir.file("wave.bm3d")));
    // the leaves together hold the triangles of the input but the degenerate ones; the coarser
    // levels hold the simplified surface, so only the leaves are counted
    uint64_t leafFaces = 0;
    for (uint32_t n = 0; n < reader.sink(); n++)
        if (reader.isLeaf(n))
            leafFaces += reader.nodes()[n].faceCount;
    EXPECT_EQ(leafFaces, input - degenerate);
    const std::string metadata = readMetadata(dir.file("wave.bm3d"));
    EXPECT_EQ(member(metadata, "source").find("\"faces\":" + std::to_string(input)) != std::string::npos, true) << metadata;
}

TEST(FormatOutput, theMetadataHoldsWhatWasReadOfTheInput) {
    TempDir dir;
    struct Case {
        std::string file;
        std::string content;
        std::string source;
    };
    const std::vector<Case> cases = {
        {"triangle.obj", kTriangleObj, "{\"name\":\"triangle.obj\",\"vertices\":3,\"faces\":1,\"bounds\":{\"min\":[0,0,0],\"max\":[1,1,0]}}"},
        // a quad is cut into two triangles; the file defines eight vertices
        {"cube.obj", "v 0 0 0\nv 2 0 0\nv 2 3 0\nv 0 3 0\nv 0 0 4\nv 2 0 4\nv 2 3 4\nv 0 3 4\n"
                     "f 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 2 3 7 6\nf 3 4 8 7\nf 4 1 5 8\n",
         "{\"name\":\"cube.obj\",\"vertices\":8,\"faces\":12,\"bounds\":{\"min\":[0,0,0],\"max\":[2,3,4]}}"},
        {"fan.ply", "ply\nformat ascii 1.0\nelement vertex 5\nproperty float x\nproperty float y\nproperty float z\n"
                    "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
                    "-1 -2 -3\n1 0 0\n1 1 0\n0 1 0\n-1 1 5.5\n5 0 1 2 3 4\n",
         "{\"name\":\"fan.ply\",\"vertices\":5,\"faces\":3,\"bounds\":{\"min\":[-1,-2,-3],\"max\":[1,1,5.5]}}"},
        // an STL file defines three vertices for each facet
        {"plain.stl", "solid p\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1.5 0\nendloop\nendfacet\n"
                      "facet normal 0 0 1\nouter loop\nvertex 1 0 0\nvertex 1 1.5 0\nvertex 0 1.5 0\nendloop\nendfacet\nendsolid p\n",
         "{\"name\":\"plain.stl\",\"vertices\":6,\"faces\":2,\"bounds\":{\"min\":[0,0,0],\"max\":[1,1.5,0]}}"},
    };
    for (const Case& c : cases) {
        writeTextFile(dir.file(c.file), c.content);
        build(dir.file(c.file), dir.file(c.file + ".bm3d"));
        const std::string metadata = readMetadata(dir.file(c.file + ".bm3d"));
        EXPECT_EQ(member(metadata, "source"), c.source) << c.file;
        EXPECT_EQ(member(metadata, "generator"), "\"bm3d " + versionFileText() + "\"") << c.file;
    }
}

TEST(FormatOutput, theMetadataRecordsTheSettingsOfTheBuild) {
    TempDir dir;
    writeTextFile(dir.file("t.obj"), kTriangleObj);
    build(dir.file("t.obj"), dir.file("t.bm3d"));
    const std::string options = member(readMetadata(dir.file("t.bm3d")), "buildOptions");
    EXPECT_EQ(options,
              "{\"threads\":2,\"nodeFaces\":16384,\"topNodeFaces\":4096,\"textureQuality\":90,\"maxNodeTextureSide\":1024,"
              "\"scaling\":0.5,\"adaptive\":0.333,\"skipLevels\":-1,\"errorQ\":0.1,\"normalBits\":10,\"lumaBits\":6,"
              "\"chromaBits\":6,\"alphaBits\":5,\"texStep\":0.25}");
}

TEST(FormatOutput, theNameOfTheInputHasNoFolder) {
    TempDir dir;
    dir.makeDir("some/folder");
    writeTextFile(dir.file("some/folder/модель.obj"), kTriangleObj);
    build(dir.file("some/folder/модель.obj"), dir.file("out.bm3d"));
    EXPECT_EQ(member(readMetadata(dir.file("out.bm3d")), "source").rfind("{\"name\":\"модель.obj\"", 0), 0u);
}

TEST(FormatOutput, theMembersTheCallerAddsFollowTheOnesOfTheBuilder) {
    TempDir dir;
    writeTextFile(dir.file("t.obj"), kTriangleObj);
    build(dir.file("t.obj"), dir.file("t.bm3d"), "{\"crs\":\"EPSG:32633\",\"origin\":[1, 2.5, -3]}");
    const std::string metadata = readMetadata(dir.file("t.bm3d"));
    std::vector<bm3d::JsonMember> members;
    ASSERT_EQ(bm3d::jsonObjectMembers(metadata, members), "");
    ASSERT_EQ(members.size(), 5u);
    EXPECT_EQ(members[3].name, "crs");
    EXPECT_EQ(members[3].rawValue, "\"EPSG:32633\"");
    EXPECT_EQ(members[4].rawValue, "[1, 2.5, -3]");
}

TEST(FormatOutput, theMetadataChangesTheFileButNotItsContentHash) {
    TempDir dir;
    writeTextFile(dir.file("t.obj"), kTriangleObj);
    build(dir.file("t.obj"), dir.file("a.bm3d"));
    build(dir.file("t.obj"), dir.file("b.bm3d"), "{\"crs\":\"EPSG:32633\"}");
    EXPECT_NE(readBinaryFile(dir.file("a.bm3d")), readBinaryFile(dir.file("b.bm3d")));
    EXPECT_EQ(readBm3dFile(dir.file("a.bm3d")).contentHash, readBm3dFile(dir.file("b.bm3d")).contentHash);
}

// --- what is refused before anything is built ----------------------------------------------------

bm3d::ErrorKind failureOf(const fs::path& input, const fs::path& output, const std::string& metadata, std::string* message) {
    try {
        build(input, output, metadata);
    } catch (const bm3d::Error& e) {
        *message = e.what();
        return e.kind();
    }
    ADD_FAILURE() << "the build succeeded";
    return bm3d::ErrorKind::BuildFailed;
}

TEST(FormatOutput, aReservedOrRepeatedKeyIsAnInvalidArgumentAndNothingIsBuilt) {
    TempDir dir;
    writeTextFile(dir.file("t.obj"), kTriangleObj);
    dir.makeDir("out");
    for (const char* bad : {"{\"generator\":1}", "{\"buildOptions\":{}}", "{\"source\":[]}", "{\"bm3d-extra\":1}", "{\"a\":1,\"a\":2}",
                            "[1]", "{\"a\":"}) {
        std::string message;
        EXPECT_EQ(failureOf(dir.file("t.obj"), dir.file("out/t.bm3d"), bad, &message), bm3d::ErrorKind::InvalidArgument) << bad;
        EXPECT_FALSE(message.empty());
        EXPECT_TRUE(dir.list("out").empty()) << bad;  // not even a scratch file
        EXPECT_EQ(bm3d::metadataError(bad), message) << bad;
    }
    std::string message;
    failureOf(dir.file("t.obj"), dir.file("out/t.bm3d"), "{\"bm3d-extra\":1}", &message);
    EXPECT_NE(message.find("bm3d-extra"), std::string::npos) << message;
}

TEST(FormatOutput, aBlockThatDoesNotFitIsAnInvalidArgumentBeforeTheInputIsRead) {
    TempDir dir;
    // the input does not exist: the metadata is refused first
    const std::string large = "{\"k\":\"" + std::string(1048576, 'x') + "\"}";
    std::string message;
    EXPECT_EQ(failureOf(dir.file("absent.obj"), dir.file("t.bm3d"), large, &message), bm3d::ErrorKind::InvalidArgument);
    EXPECT_NE(message.find("1048576"), std::string::npos) << message;
    EXPECT_FALSE(bm3d::metadataError(large).empty());
    EXPECT_EQ(bm3d::metadataError("{\"k\":\"" + std::string(1000, 'x') + "\"}"), "");
    EXPECT_EQ(bm3d::metadataError(""), "");
}

}  // namespace
