// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The colours of faces: the colour of a material (OBJ `Kd`) or of an STL face is the colour of every
// corner of its faces. Where faces of two colours share a vertex, the vertex is split, so that each
// triangle is drawn in its own colour on both sides of the border: a vertex of one colour drew the
// triangles of the other side with a gradient towards it, a sawtooth along the border. The colours of
// vertices (a PLY, `v x y z r g b`) vary over the surface and are not split.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "bm3d_file.h"
#include "model_files.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

using Colour = std::array<int, 3>;
const Colour kRed = {255, 0, 0};
const Colour kBlue = {0, 0, 255};
// the colours are compressed with a few bits each
constexpr int kTolerance = 16;

bool near(const std::array<uint8_t, 4>& got, const Colour& want) {
    for (int k = 0; k < 3; k++)
        if (std::abs(int(got[size_t(k)]) - want[size_t(k)]) > kTolerance)
            return false;
    return true;
}

// A flat grid of cells x cells squares whose left half is red and right half blue, two materials that
// share the vertices of the column between them. `textured` adds a triangle with a texture, away from
// the grid, which makes the model one with textures.
void writeTwoColourGrid(const TempDir& dir, int cells, bool textured) {
    std::ostringstream obj;
    obj << "mtllib grid.mtl\n";
    for (int y = 0; y <= cells; y++)
        for (int x = 0; x <= cells; x++)
            obj << "v " << x << " " << y << " 0\n";
    auto index = [cells](int x, int y) { return y * (cells + 1) + x + 1; };
    for (const char* half : {"red", "blue"}) {
        obj << "usemtl " << half << "\n";
        for (int y = 0; y < cells; y++)
            for (int x = 0; x < cells; x++) {
                if ((x < cells / 2) != (std::string(half) == "red"))
                    continue;
                const int a = index(x, y), b = index(x + 1, y), c = index(x + 1, y + 1), d = index(x, y + 1);
                obj << "f " << a << " " << b << " " << c << "\nf " << a << " " << c << " " << d << "\n";
            }
    }
    std::string mtl = "newmtl red\nKd 1 0 0\nnewmtl blue\nKd 0 0 1\n";
    if (textured) {
        const int first = (cells + 1) * (cells + 1) + 1;
        obj << "v " << cells + 10 << " 0 0\nv " << cells + 11 << " 0 0\nv " << cells + 10 << " 1 0\n"
            << "vt 0 0\nvt 1 0\nvt 0 1\nusemtl textured\n"
            << "f " << first << "/1 " << first + 1 << "/2 " << first + 2 << "/3\n";
        mtl += "newmtl textured\nKd 1 1 1\nmap_Kd texture.png\n";
        writePng(dir.file("texture.png"), gradientRaster(3));
    }
    writeTextFile(dir.file("grid.obj"), obj.str());
    writeTextFile(dir.file("grid.mtl"), mtl);
}

// Every triangle of every node of the file is drawn in one colour, red or blue (or, for the textured
// triangle, white); returns how many triangles are red and how many blue
std::array<int, 2> expectOneColourPerTriangle(const fs::path& file) {
    const Bm3dFile h = readBm3dFile(file);
    EXPECT_TRUE(h.hasColors);
    std::array<int, 2> counts = {0, 0};
    for (uint32_t node = 0; node + 1 < h.nNodes; node++) {
        const NodeMesh mesh = readNode(file, node);
        EXPECT_EQ(mesh.colors.size(), mesh.positions.size()) << "node " << node;
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const auto& a = mesh.colors[mesh.indices[i]];
            const auto& b = mesh.colors[mesh.indices[i + 1]];
            const auto& c = mesh.colors[mesh.indices[i + 2]];
            for (const Colour& colour : {kRed, kBlue, Colour{255, 255, 255}}) {
                if (!near(a, colour))
                    continue;
                EXPECT_TRUE(near(b, colour) && near(c, colour))
                    << "node " << node << ", triangle " << i / 3 << " has corners of two colours";
                if (colour == kRed)
                    counts[0]++;
                else if (colour == kBlue)
                    counts[1]++;
            }
        }
    }
    return counts;
}

TEST(FaceColours, theTrianglesOfTwoMaterialsThatShareVerticesKeepTheirColours) {
    TempDir dir;
    writeTwoColourGrid(dir, 4, false);
    ASSERT_NO_THROW(bm3d::build(dir.file("grid.obj"), dir.file("grid.bm3d")));
    const Bm3dFile h = readBm3dFile(dir.file("grid.bm3d"));
    ASSERT_EQ(h.nNodes, 2u);
    EXPECT_EQ(h.nvert, 25u + 5u);  // the five vertices of the border are there in both colours
    const auto counts = expectOneColourPerTriangle(dir.file("grid.bm3d"));
    EXPECT_EQ(counts[0], 16);
    EXPECT_EQ(counts[1], 16);
}

// Several nodes and levels: the simplified triangles keep the colour of their faces
TEST(FaceColours, theSimplifiedLevelsKeepOneColourPerTriangle) {
    TempDir dir;
    writeTwoColourGrid(dir, 200, false);
    ASSERT_NO_THROW(bm3d::build(dir.file("grid.obj"), dir.file("grid.bm3d")));
    ASSERT_GT(readBm3dFile(dir.file("grid.bm3d")).nNodes, 3u);
    const auto counts = expectOneColourPerTriangle(dir.file("grid.bm3d"));
    EXPECT_GT(counts[0], 1000);
    EXPECT_GT(counts[1], 1000);
}

// A model with textures splits its vertices by texture coordinates too: the colours of the faces
// without a texture are split the same way
TEST(FaceColours, aModelWithTexturesKeepsTheColoursOfItsFaces) {
    TempDir dir;
    writeTwoColourGrid(dir, 200, true);
    ASSERT_NO_THROW(bm3d::build(dir.file("grid.obj"), dir.file("grid.bm3d")));
    const Bm3dFile h = readBm3dFile(dir.file("grid.bm3d"));
    ASSERT_GT(h.nTextures, 0u);
    ASSERT_GT(h.nNodes, 3u);
    const auto counts = expectOneColourPerTriangle(dir.file("grid.bm3d"));
    EXPECT_GT(counts[0], 1000);
    EXPECT_GT(counts[1], 1000);
}

// The faces without a texture of a model with textures read their node texture too: white texels, so
// that they show the colour of their vertices in every node (they read the texel at (0, 0), another
// colour in each node, which tinted them and drew the borders between the nodes)
TEST(FaceColours, theFacesWithoutATextureOfATexturedModelReadWhiteTexels) {
    TempDir dir;
    writeTwoColourGrid(dir, 200, true);
    ASSERT_NO_THROW(bm3d::build(dir.file("grid.obj"), dir.file("grid.bm3d")));
    const Bm3dFile h = readBm3dFile(dir.file("grid.bm3d"));
    int checked = 0;
    for (uint32_t node = 0; node + 1 < h.nNodes; node++) {
        const DecodedImage image = decodeWebp(h.textures[size_t(h.nodeTexture[node])]);
        const NodeMesh mesh = readNode(dir.file("grid.bm3d"), node);
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const uint32_t vertex = mesh.indices[i];
            if (!near(mesh.colors[vertex], kRed) && !near(mesh.colors[vertex], kBlue))
                continue;  // the textured triangle
            const auto& uv = mesh.uvs[vertex];
            const int x = std::clamp(int(uv[0] * image.width), 0, image.width - 1);
            const int y = std::clamp(int((1 - uv[1]) * image.height), 0, image.height - 1);
            for (int band = 0; band < 3; band++)
                ASSERT_GE(image.bands[size_t(band)][size_t(y) * size_t(image.width) + size_t(x)], 240)
                    << "node " << node << ", band " << band;
            checked++;
        }
    }
    EXPECT_GT(checked, 1000);
}

// The 15 bit colour of the faces of a binary STL of Materialise: red for x below one half, blue above
std::vector<uint16_t> halfColourAttributes(const std::vector<Triangle>& triangles) {
    std::vector<uint16_t> attributes;
    for (const Triangle& t : triangles) {
        const float x = (t[0][0] + t[1][0] + t[2][0]) / 3;
        attributes.push_back(uint16_t(0x8000 | (x < 0.5f ? 31 : (31 << 10))));
    }
    return attributes;
}

TEST(FaceColours, theColoursOfTheFacesOfAnStlKeepTheirBorder) {
    TempDir dir;
    const auto triangles = waveTriangles(100);
    writeBinaryFile(dir.file("wave.stl"),
                    binaryStl(triangles, std::string("Materialise COLOR=") + char(200) + char(100) + char(50) + char(32),
                              halfColourAttributes(triangles)));
    ASSERT_NO_THROW(bm3d::build(dir.file("wave.stl"), dir.file("wave.bm3d")));
    const auto counts = expectOneColourPerTriangle(dir.file("wave.bm3d"));
    EXPECT_GT(counts[0], 1000);
    EXPECT_GT(counts[1], 1000);
}

// The colours of vertices are not split: each position of a node is one vertex, whatever its colour,
// in the simplified levels too (the regions of one colour have triangles whose corners have one colour)
TEST(FaceColours, theColoursOfVerticesAreNotSplit) {
    TempDir dir;
    const int cells = 200;
    std::ostringstream ply;
    ply << "ply\nformat ascii 1.0\nelement vertex " << (cells + 1) * (cells + 1)
        << "\nproperty float x\nproperty float y\nproperty float z\n"
           "property uchar red\nproperty uchar green\nproperty uchar blue\n"
           "element face "
        << 2 * cells * cells << "\nproperty list uchar int vertex_indices\nend_header\n";
    for (int y = 0; y <= cells; y++)
        for (int x = 0; x <= cells; x++)  // squares of 20 x 20 cells of one colour, over a bumpy surface
            ply << x << " " << y << " " << (x * y % 7) * 0.01 << " " << ((x / 20) % 2) * 255 << " "
                << ((y / 20) % 2) * 255 << " 0\n";
    for (int y = 0; y < cells; y++)
        for (int x = 0; x < cells; x++) {
            const int a = y * (cells + 1) + x, b = a + 1, c = a + cells + 2, d = a + cells + 1;
            ply << "3 " << a << " " << b << " " << c << "\n3 " << a << " " << c << " " << d << "\n";
        }
    writeTextFile(dir.file("grid.ply"), ply.str());
    ASSERT_NO_THROW(bm3d::build(dir.file("grid.ply"), dir.file("grid.bm3d")));
    const Bm3dFile h = readBm3dFile(dir.file("grid.bm3d"));
    ASSERT_GT(h.nNodes, 3u);
    for (uint32_t node = 0; node + 1 < h.nNodes; node++) {
        std::vector<std::array<float, 3>> positions = readNode(dir.file("grid.bm3d"), node).positions;
        std::sort(positions.begin(), positions.end());
        EXPECT_EQ(std::unique(positions.begin(), positions.end()) - positions.begin(), std::ptrdiff_t(positions.size()))
            << "node " << node << " has a position twice";
    }
}

}  // namespace
