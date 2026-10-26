// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The three mesh formats the program reads, each through the library: the same surface written
// as OBJ, PLY, ASCII STL and binary STL must give a model of the same triangles. The models are
// made here (the sample files of the project are large and live elsewhere).

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "model_files.h"
#include "nexus_file.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

// A surface of 20 x 20 squares: 800 triangles, fewer than a node holds
constexpr int kSquares = 20;

NexusFile buildFile(const TempDir& dir, const std::string& input, const std::string& output) {
    EXPECT_NO_THROW(bm3d::build(dir.file(input), dir.file(output)));
    return readNexusFile(dir.file(output));
}

TEST(MeshFormats, anObjBuildsTheTrianglesItHolds) {
    TempDir dir;
    const auto triangles = waveTriangles(kSquares);
    writeTextFile(dir.file("wave.obj"), objOf(triangles));
    const NexusFile h = buildFile(dir, "wave.obj", "wave.nxz");
    EXPECT_EQ(h.magic, kNexusMagic);
    EXPECT_EQ(h.nface, triangles.size());
    EXPECT_EQ(h.nTextures, 0u);
}

TEST(MeshFormats, anAsciiPlyBuildsTheTrianglesItHolds) {
    TempDir dir;
    const auto triangles = waveTriangles(kSquares);
    writeTextFile(dir.file("wave.ply"), plyOf(triangles));
    const NexusFile h = buildFile(dir, "wave.ply", "wave.nxz");
    EXPECT_EQ(h.magic, kNexusMagic);
    EXPECT_EQ(h.nface, triangles.size());
}

TEST(MeshFormats, anAsciiStlBuildsTheTrianglesItHolds) {
    TempDir dir;
    const auto triangles = waveTriangles(kSquares);
    writeTextFile(dir.file("wave.stl"), asciiStl(triangles));
    const NexusFile h = buildFile(dir, "wave.stl", "wave.nxz");
    EXPECT_EQ(h.magic, kNexusMagic);
    EXPECT_EQ(h.nface, triangles.size());
}

TEST(MeshFormats, aBinaryStlBuildsTheTrianglesItHolds) {
    TempDir dir;
    const auto triangles = waveTriangles(kSquares);
    writeBinaryFile(dir.file("wave.stl"), binaryStl(triangles));
    const NexusFile h = buildFile(dir, "wave.stl", "wave.nxz");
    EXPECT_EQ(h.magic, kNexusMagic);
    EXPECT_EQ(h.nface, triangles.size());
}

// The extension is read in any case: a model kept as MODEL.OBJ is built like the same model in lower
// case
TEST(MeshFormats, theExtensionIsReadInAnyCase) {
    TempDir dir;
    const auto triangles = waveTriangles(kSquares);
    const std::string obj = objOf(triangles);
    const std::string ply = plyOf(triangles);
    const std::string ascii = asciiStl(triangles);
    const std::string binary = binaryStl(triangles);

    const struct {
        const char* lower;
        const char* upper;
        const char* mixed;
        const std::string& bytes;
    } cases[] = {
        {"wave.obj", "WAVE.OBJ", "wave.Obj", obj},
        {"wave.ply", "WAVE.PLY", "wave.pLy", ply},
        {"wave.stl", "WAVE.STL", "wave.StL", ascii},
        {"binary.stl", "BINARY.STL", "binary.Stl", binary},
    };
    for (const auto& c : cases) {
        writeBinaryFile(dir.file(c.lower), c.bytes);
        writeBinaryFile(dir.file(c.upper), c.bytes);
        writeBinaryFile(dir.file(c.mixed), c.bytes);
        const NexusFile lower = buildFile(dir, c.lower, std::string(c.lower) + ".nxz");
        for (const char* name : {c.upper, c.mixed}) {
            const NexusFile other = buildFile(dir, name, std::string(name) + ".nxz");
            EXPECT_EQ(other.nface, triangles.size()) << name;
            EXPECT_EQ(other.nface, lower.nface) << name;
            EXPECT_EQ(other.nvert, lower.nvert) << name;
            EXPECT_EQ(other.nNodes, lower.nNodes) << name;
            // the same loader reads it, so the file is the same byte for byte
            EXPECT_EQ(readBinaryFile(dir.file(std::string(name) + ".nxz")), readBinaryFile(dir.file(std::string(c.lower) + ".nxz")))
                << name;
        }
    }
}

TEST(MeshFormats, theSameSurfaceInEveryFormatGivesTheSameModel) {
    TempDir dir;
    const auto triangles = waveTriangles(kSquares);
    writeTextFile(dir.file("wave.obj"), objOf(triangles));
    writeTextFile(dir.file("wave.ply"), plyOf(triangles));
    writeTextFile(dir.file("ascii.stl"), asciiStl(triangles));
    writeBinaryFile(dir.file("binary.stl"), binaryStl(triangles));

    const NexusFile obj = buildFile(dir, "wave.obj", "obj.nxz");
    for (const char* input : {"wave.ply", "ascii.stl", "binary.stl"}) {
        const NexusFile other = buildFile(dir, input, std::string(input) + ".nxz");
        EXPECT_EQ(other.nface, obj.nface) << input;
        EXPECT_EQ(other.nvert, obj.nvert) << input;
        EXPECT_EQ(other.nNodes, obj.nNodes) << input;
    }
}

TEST(MeshFormats, aTexturedObjKeepsItsTexture) {
    TempDir dir;
    writePng(dir.file("texture.png"), gradientRaster(3));
    writeTexturedModel(dir.path(), "texture.png");
    const NexusFile h = buildFile(dir, "model.obj", "model.nxz");
    EXPECT_EQ(h.nface, 2u);
    EXPECT_GE(h.nTextures, 1u);
}

// A surface that needs more than one node: the file holds the finest level and the coarser ones
TEST(MeshFormats, aSurfaceOfManyTrianglesIsBuiltInLevels) {
    TempDir dir;
    const auto triangles = waveTriangles(150);  // 45000 triangles
    writeBinaryFile(dir.file("big.stl"), binaryStl(triangles));
    const NexusFile h = buildFile(dir, "big.stl", "big.nxz");
    EXPECT_GE(h.nface, triangles.size());
    EXPECT_GT(h.nNodes, 3u);
}

TEST(MeshFormats, theTexturesOfAnObjBesideItAreFoundByTheirMaterial) {
    TempDir dir;
    dir.makeDir("model");
    writeTiff(dir.file("model/texture.tif"), gradientRaster(3));
    writeTexturedModel(dir.makeDir("model"), "texture.tif");
    // the texture is named relative to the folder of the model, not to the one of the output
    const NexusFile h = buildFile(dir, "model/model.obj", "out.nxz");
    EXPECT_GE(h.nTextures, 1u);
}

// A build is the same whatever number of threads it keeps busy
TEST(MeshFormats, theThreadsOfABuildDoNotChangeTheModel) {
    TempDir dir;
    const auto triangles = waveTriangles(150);
    writeBinaryFile(dir.file("big.stl"), binaryStl(triangles));
    const NexusFile reference = buildFile(dir, "big.stl", "default.nxz");
    for (const int threads : {1, 3}) {
        bm3d::BuildOptions options;
        options.threads = threads;
        const fs::path out = dir.file("threads" + std::to_string(threads) + ".nxz");
        ASSERT_NO_THROW(bm3d::build(dir.file("big.stl"), out, options)) << threads;
        const NexusFile h = readNexusFile(out);
        EXPECT_EQ(h.nface, reference.nface) << threads;
        EXPECT_EQ(h.nvert, reference.nvert) << threads;
        EXPECT_EQ(h.nNodes, reference.nNodes) << threads;
    }
}

// A PLY names its texture in a comment; the coordinates come per vertex (s and t) or per face
TEST(MeshFormats, aPlyWithTextureCoordinatesPerVertexKeepsItsTexture) {
    TempDir dir;
    writePng(dir.file("t.png"), gradientRaster(3));
    writeTextFile(dir.file("textured.ply"),
                  "ply\nformat ascii 1.0\ncomment TextureFile t.png\nelement vertex 4\n"
                  "property float x\nproperty float y\nproperty float z\nproperty float s\nproperty float t\n"
                  "element face 2\nproperty list uchar int vertex_indices\nend_header\n"
                  "0 0 0 0 0\n1 0 0 1 0\n1 1 0 1 1\n0 1 0 0 1\n3 0 1 2\n3 0 2 3\n");
    const NexusFile h = buildFile(dir, "textured.ply", "textured.nxz");
    EXPECT_EQ(h.nface, 2u);
    EXPECT_GE(h.nTextures, 1u);
}

TEST(MeshFormats, aPlyWithTextureCoordinatesPerFaceKeepsItsTexture) {
    TempDir dir;
    writePng(dir.file("t.png"), gradientRaster(3));
    writeTextFile(dir.file("textured.ply"),
                  "ply\nformat ascii 1.0\ncomment TextureFile t.png\nelement vertex 4\n"
                  "property float x\nproperty float y\nproperty float z\n"
                  "element face 2\nproperty list uchar int vertex_indices\nproperty list uchar float texcoord\n"
                  "end_header\n0 0 0\n1 0 0\n1 1 0\n0 1 0\n"
                  "3 0 1 2 6 0 0 1 0 1 1\n3 0 2 3 6 0 0 1 1 0 1\n");
    const NexusFile h = buildFile(dir, "textured.ply", "textured.nxz");
    EXPECT_EQ(h.nface, 2u);
    EXPECT_GE(h.nTextures, 1u);
}

// Paths in any alphabet: the folders, the texture and the output
TEST(MeshFormats, aModelAndItsTextureUnderFoldersInOtherAlphabetsAreBuilt) {
    TempDir dir;
    const fs::path folder = dir.makeDir("модель/模型");
    dir.makeDir("выход/出力");
    writePng(folder / pathFromUtf8("текстура.png"), gradientRaster(3));
    writeTexturedModel(folder, "текстура.png");
    const fs::path out = dir.file("выход/出力/результат.nxz");
    ASSERT_NO_THROW(bm3d::build(folder / "model.obj", out));
    const NexusFile h = readNexusFile(out);
    EXPECT_EQ(h.nface, 2u);
    EXPECT_GE(h.nTextures, 1u);
}

}  // namespace
