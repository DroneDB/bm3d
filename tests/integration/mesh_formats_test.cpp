// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The three mesh formats the program reads, each through the library: the same surface written
// as OBJ, PLY, ASCII STL and binary STL must give a model of the same triangles. The models are
// made here (the sample files of the project are large and live elsewhere).

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
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

// --- STL

// The same bytes with the lines ended like Windows does
std::string withCrLf(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '\n')
            out += '\r';
        out += c;
    }
    return out;
}

void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    for (size_t i = 0; (i = text.find(from, i)) != std::string::npos; i += to.size())
        text.replace(i, from.size(), to);
}

std::string upperCase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return text;
}

// The text with a blank line after every line and a tab for each indent
std::string spacedOut(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        if (text.compare(i, 2, "  ") == 0) {
            out += '\t';
            i++;
        } else if (text[i] == '\n') {
            out += "\n   \n";
        } else {
            out += text[i];
        }
    }
    return out;
}

// A binary STL is told by its size: 84 bytes and 50 for each triangle. One that starts with
// "solid", as many do, is still binary.
TEST(StlFormats, aBinaryStlWhoseHeaderStartsWithSolidIsReadAsBinary) {
    TempDir dir;
    const auto triangles = waveTriangles(kSquares);
    writeBinaryFile(dir.file("solid.stl"), binaryStl(triangles, "solid wave exported by a program that writes binary"));
    const NexusFile h = buildFile(dir, "solid.stl", "solid.nxz");
    EXPECT_EQ(h.nface, triangles.size());
}

// Bytes after the last triangle are ignored (some programs pad the file), with a warning
TEST(StlFormats, bytesAfterTheLastTriangleOfABinaryStlAreIgnored) {
    TempDir dir;
    const auto triangles = waveTriangles(kSquares);
    writeBinaryFile(dir.file("padded.stl"), binaryStl(triangles) + std::string(7, 'x'));
    EXPECT_EQ(buildFile(dir, "padded.stl", "padded.nxz").nface, triangles.size());

    // the header counts fewer triangles than the file holds: the ones it counts are read
    auto bytes = binaryStl(triangles);
    bytes[80] = char(100);
    bytes[81] = bytes[82] = bytes[83] = 0;
    writeBinaryFile(dir.file("counted.stl"), bytes);
    EXPECT_EQ(buildFile(dir, "counted.stl", "counted.nxz").nface, 100u);
}

// A text file that starts with "solid" is ASCII whatever its layout: any white space, blank lines,
// Windows line ends, keywords in any case, and more than one solid
TEST(StlFormats, anAsciiStlIsReadWhateverItsLayout) {
    TempDir dir;
    const auto triangles = waveTriangles(kSquares);
    const std::string plain = asciiStl(triangles);
    const std::vector<std::pair<const char*, std::string>> layouts = {
        {"crlf.stl", withCrLf(plain)},
        {"upper.stl", upperCase(plain)},
        {"spaced.stl", spacedOut(plain)},
        {"spaced-crlf.stl", withCrLf(spacedOut(upperCase(plain)))},
        {"no-final-newline.stl", plain.substr(0, plain.size() - 1)},
        {"one-line-per-facet.stl", [&] {
             std::string text = plain;  // a facet on a line: from "facet" to "endfacet"
             replaceAll(text, "\n    ", " ");
             replaceAll(text, "\n  endfacet", " endfacet");
             return text;
         }()},
    };
    for (const auto& layout : layouts) {
        writeBinaryFile(dir.file(layout.first), layout.second);
        EXPECT_EQ(buildFile(dir, layout.first, std::string(layout.first) + ".nxz").nface, triangles.size())
            << layout.first;
    }
}

TEST(StlFormats, anAsciiStlWithSeveralSolidsHoldsTheTrianglesOfAll) {
    TempDir dir;
    const auto first = waveTriangles(5);
    auto second = waveTriangles(4);
    for (auto& t : second)
        for (auto& c : t)
            c[0] += 3.0f;  // beside the first one, so that nothing is welded
    writeTextFile(dir.file("two.stl"), asciiStl(first) + "\n" + asciiStl(second));
    EXPECT_EQ(buildFile(dir, "two.stl", "two.nxz").nface, first.size() + second.size());
}

// Materialise Magics keeps a colour for the faces without one in the header ("COLOR=" and red,
// green, blue, alpha) and, for each face, 15 bits of its own: red in the low five, then green, then
// blue, and the highest bit set when the face has a colour
// (the alpha of the header is not used: MeshLab writes a space, 32, where it belongs)
std::string materialiseHeader() {
    return std::string("Materialise COLOR=") + char(200) + char(100) + char(50) + char(32);
}

std::array<uint8_t, 4> colorAt(const NodeMesh& mesh, const std::array<float, 3>& at) {
    size_t best = 0;
    float bestDistance = 1e30f;
    for (size_t i = 0; i < mesh.positions.size(); i++) {
        float d = 0;
        for (int k = 0; k < 3; k++)
            d += (mesh.positions[i][k] - at[k]) * (mesh.positions[i][k] - at[k]);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    EXPECT_LT(bestDistance, 0.01f);
    return best < mesh.colors.size() ? mesh.colors[best] : std::array<uint8_t, 4>{};
}

void expectColor(const std::array<uint8_t, 4>& got, const std::array<int, 4>& want, const char* what) {
    // the colours are compressed with a few bits each
    for (int k = 0; k < 4; k++)
        EXPECT_NEAR(got[k], want[k], 16) << what << " channel " << k;
}

TEST(StlFormats, theColoursOfAMaterialiseStlAreTheColoursOfTheVerticesOfItsFaces) {
    TempDir dir;
    // far from each other: no vertex is shared by faces of different colours
    const std::vector<Triangle> triangles = {
        Triangle{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}},
        Triangle{{{5, 0, 0}, {6, 0, 0}, {5, 1, 0}}},
        Triangle{{{0, 5, 0}, {1, 5, 0}, {0, 6, 0}}},
        Triangle{{{5, 5, 0}, {6, 5, 0}, {5, 6, 0}}},
    };
    const uint16_t red = 0x8000 | 31, green = 0x8000 | (31 << 5), blue = 0x8000 | (31 << 10), none = 0;
    writeBinaryFile(dir.file("colored.stl"), binaryStl(triangles, materialiseHeader(), {red, green, blue, none}));
    const NexusFile h = buildFile(dir, "colored.stl", "colored.nxz");
    ASSERT_EQ(h.nNodes, 2u);  // one node and the sink
    ASSERT_TRUE(h.hasColors);
    const NodeMesh mesh = readNode(dir.file("colored.nxz"));
    ASSERT_EQ(mesh.colors.size(), mesh.positions.size());
    expectColor(colorAt(mesh, {0, 0, 0}), {255, 0, 0, 255}, "red face");
    expectColor(colorAt(mesh, {6, 0, 0}), {0, 255, 0, 255}, "green face");
    expectColor(colorAt(mesh, {0, 5, 0}), {0, 0, 255, 255}, "blue face");
    expectColor(colorAt(mesh, {5, 6, 0}), {200, 100, 50, 255}, "face without a colour");
}

// MeshLab writes the header with a grey colour for any binary STL, and no colour in the faces: that is
// the colour of a material, and the model has no colours
TEST(StlFormats, aMaterialiseStlWithNoColourInItsFacesHasNoColours) {
    TempDir dir;
    const auto triangles = waveTriangles(3);
    writeBinaryFile(dir.file("grey.stl"), binaryStl(triangles, materialiseHeader()));
    const NexusFile h = buildFile(dir, "grey.stl", "grey.nxz");
    EXPECT_FALSE(h.hasColors);
    EXPECT_EQ(h.nface, triangles.size());
}

// Without "COLOR=" in the header the two bytes after a face are not a colour (they are zero, or
// whatever the program that wrote them kept there)
TEST(StlFormats, theTwoBytesAfterAFaceAreNotAColourWithoutTheMaterialiseHeader) {
    TempDir dir;
    const auto triangles = waveTriangles(3);
    writeBinaryFile(dir.file("plain.stl"), binaryStl(triangles, "plain", std::vector<uint16_t>(triangles.size(), 0xFFFF)));
    const NexusFile h = buildFile(dir, "plain.stl", "plain.nxz");
    EXPECT_FALSE(h.hasColors);
    EXPECT_EQ(h.nface, triangles.size());
}

// The sample files of the project, kept next to the repository (BM3D_TEST_DATA, or ../test_data)
fs::path sampleModel(const std::string& name) {
    const char* folder = std::getenv("BM3D_TEST_DATA");
    return (folder != nullptr ? fs::path(folder) : fs::path(BM3D_TEST_SOURCE_DIR) / ".." / "test_data") / "3d" / name;
}

TEST(StlFormats, theSampleBinaryStlIsBuilt) {
    const fs::path model = sampleModel("stl_binary.stl");
    if (!fs::exists(model))
        GTEST_SKIP() << pathToUtf8(model) << " is not there";
    TempDir dir;
    ASSERT_NO_THROW(bm3d::build(model, dir.file("out.nxz")));
    const NexusFile h = readNexusFile(dir.file("out.nxz"));
    // 84 bytes of header and count, and 50 for each triangle; a model of many triangles is built in levels,
    // and the file holds the faces of every one
    EXPECT_GE(h.nface, (fs::file_size(model) - 84) / 50);
}

TEST(StlFormats, theSampleAsciiStlIsBuilt) {
    const fs::path model = sampleModel("stl_ascii.stl");
    if (!fs::exists(model))
        GTEST_SKIP() << pathToUtf8(model) << " is not there";
    size_t facets = 0;
    std::ifstream in(model);
    for (std::string line; std::getline(in, line);)
        if (line.find("endfacet") != std::string::npos)
            facets++;
    ASSERT_GT(facets, 0u);
    TempDir dir;
    ASSERT_NO_THROW(bm3d::build(model, dir.file("out.nxz")));
    EXPECT_GE(readNexusFile(dir.file("out.nxz")).nface, facets);
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
