// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The program reads the textures of a model with image libraries of its own and the faces of a
// PLY with a parser of its own. These tests build small models made inside the test, one
// texture file format at a time, and read the header of what was written: the formats the
// textures come in (the node textures written are WebP whatever they were), the polygons of a
// PLY, an OBJ with one-dimensional texture coordinates, and the failures that must leave no
// file behind.

#include <chrono>
#include <cstdint>
#include <functional>
#include <ostream>
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

void expectNexusMagic(const NexusFile& h) {
    // 'N' 'x' 's' ' ' in a little-endian word
    EXPECT_EQ(h.magic, kNexusMagic);
}

// What a failed build leaves in the folder of the output: nothing, no half file and no scratch
void expectNothingLeftBehind(const TempDir& dir, const std::string& folder) {
    EXPECT_TRUE(dir.list(folder).empty()) << "left in " << folder << ": " << ::testing::PrintToString(dir.list(folder));
}

struct TextureCase {
    const char* name;
    const char* extension;
    std::function<void(const fs::path&)> write;
};

// What the test list shows for the case: the default is a dump of the bytes of the object,
// which holds pointers and so is not the same from one run to the next
void PrintTo(const TextureCase& c, std::ostream* out) {
    *out << c.name;
}

class TextureFormat : public ::testing::TestWithParam<TextureCase> {};

TEST_P(TextureFormat, theTextureEndsUpInTheFile) {
    const TextureCase& c = GetParam();
    TempDir dir;
    const std::string textureName = std::string("texture.") + c.extension;
    c.write(dir.file(textureName));
    writeTexturedModel(dir.path(), textureName);

    const fs::path out = dir.file("model.nxz");
    ASSERT_NO_THROW(bm3d::build(dir.file("model.obj"), out));

    const NexusFile h = readNexusFile(out);
    expectNexusMagic(h);
    EXPECT_EQ(h.nface, 2u);
    EXPECT_EQ(h.nvert, 4u);
    ASSERT_GE(h.nTextures, 1u) << "no texture in the file";
    ASSERT_FALSE(h.textures.empty());
    EXPECT_TRUE(isWebp(h.textures.front()));
}

INSTANTIATE_TEST_SUITE_P(
    Formats,
    TextureFormat,
    ::testing::Values(
        TextureCase{"jpeg", "jpg", [](const fs::path& f) { writeJpeg(f, gradientRaster(3)); }},
        TextureCase{"pngRgb", "png", [](const fs::path& f) { writePng(f, gradientRaster(3)); }},
        TextureCase{"pngRgba", "png", [](const fs::path& f) { writePng(f, gradientRaster(4)); }},
        TextureCase{"pngPaletted", "png", [](const fs::path& f) { writePalettedPng(f, 16, 16, paletteIndices()); }},
        TextureCase{"pngGray", "png", [](const fs::path& f) { writePng(f, gradientRaster(1)); }},
        TextureCase{"tiff", "tif", [](const fs::path& f) { writeTiff(f, gradientRaster(3)); }},
        TextureCase{"webp", "webp", [](const fs::path& f) { writeWebp(f, gradientRaster(3)); }},
        TextureCase{"bmp", "bmp", [](const fs::path& f) { writeBmp(f, gradientRaster(3)); }},
        // The one picture that is a file of the repository: nothing here writes a GIF
        TextureCase{"gif", "gif", [](const fs::path& f) { fs::copy_file(std::string(BM3D_TEST_FIXTURES_DIR) + "/texture.gif", f); }}),
    [](const ::testing::TestParamInfo<TextureCase>& i) { return std::string(i.param.name); });

TEST(TextureFormats, aModelWithoutTexturesHasNone) {
    TempDir dir;
    writeTextFile(dir.file("model.obj"), kTriangleObj);
    const fs::path out = dir.file("model.nxz");
    ASSERT_NO_THROW(bm3d::build(dir.file("model.obj"), out));
    const NexusFile h = readNexusFile(out);
    expectNexusMagic(h);
    EXPECT_EQ(h.nface, 1u);
    EXPECT_EQ(h.nTextures, 0u);
}

TEST(TextureFormats, textureCoordinatesWithOnlyUAreAccepted) {
    TempDir dir;
    writePng(dir.file("texture.png"), gradientRaster(3));
    writeTexturedModel(dir.path(), "texture.png", "vt 0\nvt 1\nvt 1\nvt 0\n");
    const fs::path out = dir.file("model.nxz");
    ASSERT_NO_THROW(bm3d::build(dir.file("model.obj"), out));
    const NexusFile h = readNexusFile(out);
    expectNexusMagic(h);
    EXPECT_EQ(h.nface, 2u);
}

TEST(TextureFormats, aPlyQuadHasTheFacesOfTwoTriangles) {
    TempDir dir;
    writeTextFile(dir.file("quad.ply"), plyHeader(1) + "4 0 1 2 3\n");
    writeTextFile(dir.file("tris.ply"), plyHeader(2) + "3 0 1 2\n3 0 2 3\n");

    ASSERT_NO_THROW(bm3d::build(dir.file("quad.ply"), dir.file("quad.nxz")));
    ASSERT_NO_THROW(bm3d::build(dir.file("tris.ply"), dir.file("tris.nxz")));
    const NexusFile quad = readNexusFile(dir.file("quad.nxz"));
    const NexusFile tris = readNexusFile(dir.file("tris.nxz"));
    expectNexusMagic(quad);
    EXPECT_EQ(quad.nface, 2u);
    EXPECT_EQ(quad.nface, tris.nface);
    EXPECT_EQ(quad.nvert, tris.nvert);
}

TEST(TextureFormats, aPlyFaceWithAThousandIndicesFailsAndLeavesNoFile) {
    TempDir dir;
    dir.makeDir("in");
    dir.makeDir("out");
    std::string ply =
        "ply\nformat ascii 1.0\nelement vertex 1000\nproperty float x\nproperty float y\nproperty float z\n"
        "element face 1\nproperty list ushort int vertex_indices\nend_header\n";
    for (int i = 0; i < 1000; i++)
        ply += std::to_string(i) + " " + std::to_string(i * 7 % 13) + " 0\n";
    ply += "1000";
    for (int i = 0; i < 1000; i++)
        ply += " " + std::to_string(i);
    ply += "\n";
    writeTextFile(dir.file("in/big.ply"), ply);

    try {
        bm3d::build(dir.file("in/big.ply"), dir.file("out/big.nxz"));
        FAIL() << "the build should have failed";
    } catch (const bm3d::Error& e) {
        EXPECT_EQ(e.kind(), bm3d::ErrorKind::InvalidInput);
        EXPECT_NE(std::string(e.what()).find("face"), std::string::npos) << e.what();
    }
    expectNothingLeftBehind(dir, "out");
}

TEST(TextureFormats, anObjWithAGarbageFaceFailsCleanly) {
    TempDir dir;
    dir.makeDir("out");
    writeTextFile(dir.file("bad.obj"), "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 two 99999\nf -77 0 3\n");
    EXPECT_THROW(bm3d::build(dir.file("bad.obj"), dir.file("out/bad.nxz")), bm3d::Error);
    expectNothingLeftBehind(dir, "out");
}

TEST(TextureFormats, coincidentTrianglesFailWithTooSmallToDivideAndLeaveNoFile) {
    TempDir dir;
    dir.makeDir("out");
    std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    for (int i = 0; i < 100000; i++)
        obj += "f 1 2 3\n";
    writeTextFile(dir.file("same.obj"), obj);

    const auto start = std::chrono::steady_clock::now();
    try {
        bm3d::build(dir.file("same.obj"), dir.file("out/same.nxz"));
        FAIL() << "the build should have failed";
    } catch (const bm3d::Error& e) {
        EXPECT_EQ(e.kind(), bm3d::ErrorKind::BuildFailed);
        EXPECT_NE(std::string(e.what()).find("too small to divide"), std::string::npos) << e.what();
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    EXPECT_LT(seconds, 10.0);
    expectNothingLeftBehind(dir, "out");
}

TEST(TextureFormats, aTextureThatCannotBePackedBuildsWithoutTextures) {
    TempDir dir;
    writeBmp(dir.file("wide.bmp"), makeRaster(20000, 8, 3, [](int band, int x, int y) { return double((x + y * 30 + band) & 0xff); }));
    writeTexturedModel(dir.path(), "wide.bmp", "vt 0 0\nvt 0.99 0\nvt 0.99 0.99\nvt 0 0.99\n");
    const fs::path out = dir.file("model.nxz");
    // The warning "textures could not be packed" goes to stderr: only the outcome is asserted here
    ASSERT_NO_THROW(bm3d::build(dir.file("model.obj"), out));
    const NexusFile h = readNexusFile(out);
    expectNexusMagic(h);
    EXPECT_EQ(h.nface, 2u);
    EXPECT_EQ(h.nTextures, 0u);
}

TEST(TextureFormats, aPlyFaceListWithAnyCountAndIndexTypeBuildsTheSameModel) {
    TempDir dir;
    writeBinaryPly<uint8_t, int32_t>(dir.file("uchar_int.ply"), "uchar", "int");
    writeBinaryPly<int32_t, int32_t>(dir.file("int_int.ply"), "int", "int");
    writeBinaryPly<uint16_t, uint32_t>(dir.file("ushort_uint.ply"), "ushort", "uint");

    ASSERT_NO_THROW(bm3d::build(dir.file("uchar_int.ply"), dir.file("uchar_int.nxz")));
    const NexusFile reference = readNexusFile(dir.file("uchar_int.nxz"));
    expectNexusMagic(reference);
    EXPECT_EQ(reference.nface, 2u);
    EXPECT_EQ(reference.nvert, 4u);

    for (const char* name : {"int_int", "ushort_uint"}) {
        ASSERT_NO_THROW(bm3d::build(dir.file(std::string(name) + ".ply"), dir.file(std::string(name) + ".nxz"))) << name;
        const NexusFile h = readNexusFile(dir.file(std::string(name) + ".nxz"));
        expectNexusMagic(h);
        EXPECT_EQ(h.nface, reference.nface) << name;
        EXPECT_EQ(h.nvert, reference.nvert) << name;
    }
}

TEST(TextureFormats, aPlyWithAVeryLongTextureFileCommentDoesNotCrash) {
    TempDir dir;
    dir.makeDir("out");
    writeTextFile(dir.file("long.ply"),
                  "ply\nformat ascii 1.0\ncomment TextureFile " + std::string(480, 'a') + ".png\n" +
                      "element vertex 4\nproperty float x\nproperty float y\nproperty float z\n"
                      "element face 2\nproperty list uchar int vertex_indices\nend_header\n"
                      "0 0 0\n1 0 0\n1 1 0\n0 1 0\n3 0 1 2\n3 0 2 3\n");
    try {
        bm3d::build(dir.file("long.ply"), dir.file("out/long.nxz"));
    } catch (const bm3d::Error& e) {
        const std::string m = e.what();
        for (const char* bad : {"signal", "crash", "Segmentation", "SIGSEGV", "SIGABRT"})
            EXPECT_EQ(m.find(bad), std::string::npos) << m;
    }
}

}  // namespace
