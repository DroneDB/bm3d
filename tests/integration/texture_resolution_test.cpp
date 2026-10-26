// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// How much of a model's texture the file keeps. A node holds one texture for all its triangles,
// so a model with few triangles and large textures (what photogrammetry produces) keeps its
// resolution only when the nodes are cut by the texels they cover, at every level, and not by
// their triangle count alone. These tests build a flat grid textured by one large picture and
// read the node textures back from the file.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "model_files.h"
#include "container_file.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

// The longest side the builder gives a node texture
constexpr int kMaxNodeTextureSide = 1024;

using PixelValue = std::function<double(int band, int x, int y)>;

void writeTexture(const fs::path& file, int side, const PixelValue& value) {
    writeBmp(file, makeRaster(side, side, 3, value));
}

fs::path buildGrid(const TempDir& dir, int cells, int textureSide, const PixelValue& value) {
    writeTexture(dir.file("texture.bmp"), textureSide, value);
    writeGridModel(dir.path(), cells, "texture.bmp");
    const fs::path out = dir.file("grid.bm3d");
    EXPECT_NO_THROW(bm3d::build(dir.file("grid.obj"), out));
    return out;
}

double detail(int band, int x, int y) {
    return double((x * 7 + y * 13 + band * 50) % 256);
}

struct TexelCount {
    double leaves = 0;
    double inner = 0;
    int largestSide = 0;
};

TexelCount countTexels(const ContainerFile& n) {
    TexelCount c;
    std::vector<DecodedImage> images;
    for (const auto& t : n.textures)
        images.push_back(decodeWebp(t));
    std::vector<bool> counted(images.size(), false);
    for (size_t node = 0; node < n.nodeTexture.size(); node++) {
        const int t = n.nodeTexture[node];
        if (t < 0 || size_t(t) >= images.size() || counted[size_t(t)])
            continue;
        counted[size_t(t)] = true;
        const double texels = double(images[size_t(t)].width) * images[size_t(t)].height;
        (n.leaf[node] ? c.leaves : c.inner) += texels;
        c.largestSide = std::max({c.largestSide, images[size_t(t)].width, images[size_t(t)].height});
    }
    return c;
}

TEST(TextureResolution, theNodeTexturesAreWebp) {
    TempDir dir;
    const ContainerFile n = readContainerFile(buildGrid(dir, 4, 64, detail));
    ASSERT_FALSE(n.textures.empty());
    for (const auto& t : n.textures)
        EXPECT_TRUE(isWebp(t));
}

// 2048 triangles over 16 million texels: each node must cover only a few hundred triangles for
// its texels to fit in one node texture, far fewer than the triangles a node holds
TEST(TextureResolution, theLeavesKeepTheResolutionOfAFewTrianglesWithALargeTexture) {
    TempDir dir;
    const int side = 4096;
    const ContainerFile n = readContainerFile(buildGrid(dir, 32, side, detail));
    const TexelCount c = countTexels(n);
    EXPECT_GE(c.leaves, 0.9 * side * side) << "the leaves hold " << c.leaves << " texels";
    EXPECT_LE(c.largestSide, kMaxNodeTextureSide);
}

// A leaf is as precise as its texels: the viewer refines down to it only once a texel covers
// enough of the screen. Measured by the length of its triangles' edges, as is done for a node
// about to be simplified, the error would be a third of a grid square (over 40 texels), and the
// viewer would load the finest textures far before it could show them.
TEST(TextureResolution, theErrorOfALeafIsTheSizeOfItsTexels) {
    TempDir dir;
    const int cells = 32, side = 4096;
    const ContainerFile n = readContainerFile(buildGrid(dir, cells, side, detail));
    const double texel = double(cells) / side;  // the grid squares have a side of 1
    bool sawLeaf = false;
    for (size_t node = 0; node < n.leaf.size(); node++) {
        if (!n.leaf[node])
            continue;
        sawLeaf = true;
        EXPECT_LT(n.error[node], 3 * texel) << "node " << node;
    }
    EXPECT_TRUE(sawLeaf);
}

// A texture far finer than the triangles is made coarser for several levels before the
// triangles are simplified: the nodes cut small to keep their textures in size would otherwise
// be simplified into fans around a few vertices. The levels that keep the triangles are all
// made, however few the triangles, so the top carries a coarse texture.
TEST(TextureResolution, theCoarserLevelsKeepTheTrianglesWhileTheTextureIsFinerThanThem) {
    TempDir dir;
    const int cells = 32, triangles = 2 * cells * cells;
    const ContainerFile n = readContainerFile(buildGrid(dir, cells, 4096, detail));
    EXPECT_GE(n.nface, uint64_t(4 * triangles)) << "the levels hold " << n.nface << " triangles";
    const TexelCount c = countTexels(n);
    EXPECT_LT(c.inner, c.leaves) << "the coarser levels hold " << c.inner << " texels";
}

// The coarser levels hold a third of the texels of the leaves: the levels that only make a
// texture finer than its triangles coarser halve its side, as mipmaps do, and the simplified
// levels above them keep the texels of their triangles. Cut by triangles alone, a coarser node
// would get the texels of many leaves, more than its texture holds; with levels that halve the
// texels instead of the side, the coarser levels would hold as many as the leaves.
TEST(TextureResolution, theCoarserLevelsHoldAThirdOfTheTexelsOfTheLeaves) {
    TempDir dir;
    const int side = 4096;
    const ContainerFile n = readContainerFile(buildGrid(dir, 72, side, detail));
    const TexelCount c = countTexels(n);
    EXPECT_GE(c.leaves, 0.9 * side * side) << "the leaves hold " << c.leaves << " texels";
    EXPECT_GE(c.inner, 0.2 * c.leaves) << "the coarser levels hold " << c.inner << " texels";
    EXPECT_LE(c.inner, 0.45 * c.leaves) << "the coarser levels hold " << c.inner << " texels";
    EXPECT_LE(c.largestSide, kMaxNodeTextureSide);
}

// The texels of a node texture that no triangle draws are one flat color, past a margin of a few
// texels along the triangles: they hold whatever the model's texture had around the triangles,
// and cost bytes. One triangle over the lower left half of a detailed texture keeps the detail
// there and nothing in the upper right half (rows run from v = 1 down).
TEST(TextureResolution, theTexelsNoTriangleDrawsAreBlank) {
    TempDir dir;
    writeTexture(dir.file("texture.bmp"), 256, detail);
    writeTextFile(dir.file("half.obj"),
                  "mtllib half.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                  "vt 0 0\nvt 1 0\nvt 0 1\nusemtl m\nf 1/1 2/2 3/3\n");
    writeTextFile(dir.file("half.mtl"), "newmtl m\nKd 1 1 1\nmap_Kd texture.bmp\n");
    const fs::path out = dir.file("half.bm3d");
    ASSERT_NO_THROW(bm3d::build(dir.file("half.obj"), out));
    const ContainerFile n = readContainerFile(out);
    ASSERT_FALSE(n.textures.empty());
    const DecodedImage img = decodeWebp(n.textures.front());
    ASSERT_GE(img.width, 64);
    ASSERT_GE(img.height, 64);

    auto deviation = [&img](bool drawnHalf) {
        double sum = 0, squares = 0, count = 0;
        for (int y = 0; y < img.height; y++)
            for (int x = 0; x < img.width; x++) {
                const double u = (x + 0.5) / img.width, v = 1.0 - (y + 0.5) / img.height;
                if (drawnHalf ? u + v > 0.85 : u + v < 1.15)
                    continue;
                const double p = img.bands[0][size_t(y) * size_t(img.width) + size_t(x)];
                sum += p;
                squares += p * p;
                count++;
            }
        const double mean = sum / count;
        return std::sqrt(std::max(0.0, squares / count - mean * mean));
    };
    EXPECT_LT(deviation(false), 2.0) << "the half no triangle draws is not blank";
    EXPECT_GT(deviation(true), 20.0) << "the half the triangle draws lost its detail";
}

// The node textures are encoded on several threads and written in the order of the nodes: each
// node gets its own texture. Over a texture whose red grows with u and green with v, the mean
// color of each node's texture tells where the node lies on the grid.
TEST(TextureResolution, eachNodeHasTheTextureOfItsPlace) {
    TempDir dir;
    const int cells = 72, side = 2048;
    // bands count from 1, and texture rows run from v = 1 at the top
    const ContainerFile n = readContainerFile(buildGrid(dir, cells, side, [side](int band, int x, int y) {
        return band == 1 ? 255.0 * x / side : band == 2 ? 255.0 * (side - 1 - y) / side : 128.0;
    }));
    ASSERT_GT(n.textures.size(), 4u);
    for (size_t node = 0; node < n.nodeTexture.size(); node++) {
        const DecodedImage img = decodeWebp(n.textures[size_t(n.nodeTexture[node])]);
        double red = 0, green = 0;
        for (size_t i = 0; i < img.bands[0].size(); i++) {
            red += img.bands[0][i];
            green += img.bands[1][i];
        }
        red /= double(img.bands[0].size());
        green /= double(img.bands[1].size());
        EXPECT_NEAR(red, 255.0 * n.center[node][0] / cells, 30.0) << "node " << node;
        EXPECT_NEAR(green, 255.0 * n.center[node][1] / cells, 30.0) << "node " << node;
    }
}

// The texture of a coarser level is the average of the one below: a checkerboard of single
// pixels becomes a gray, not a scattering of the black and white pixels a nearest neighbour
// resize picks
TEST(TextureResolution, aCoarserTextureAveragesTheDetailsOfTheFinerOne) {
    TempDir dir;
    const ContainerFile n =
        readContainerFile(buildGrid(dir, 72, 1024, [](int, int x, int y) { return ((x + y) % 2) ? 255.0 : 0.0; }));
    bool sawInner = false;
    for (size_t node = 0; node < n.nodeTexture.size(); node++) {
        if (n.leaf[node])
            continue;
        sawInner = true;
        const DecodedImage img = decodeWebp(n.textures[size_t(n.nodeTexture[node])]);
        const std::vector<uint8_t>& red = img.bands[0];
        double sum = 0, squares = 0;
        for (uint8_t v : red) {
            sum += v;
            squares += double(v) * v;
        }
        const double count = double(red.size());
        const double mean = sum / count;
        const double deviation = std::sqrt(std::max(0.0, squares / count - mean * mean));
        EXPECT_NEAR(mean, 127.5, 20.0) << "node " << node;
        EXPECT_LT(deviation, 60.0) << "node " << node;
    }
    EXPECT_TRUE(sawInner) << "the model has no coarser level";
}

}  // namespace
