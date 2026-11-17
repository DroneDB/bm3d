// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Textures that cut texels out: a material whose `map_d` names its own texture (the alpha of the
// texture), or a mask (its alpha, or its luminance when it has none), as the OBJ a masked glTF
// material is converted to does. The node textures keep the alpha, cut at half, so that a viewer draws
// the texels where it is 255 and leaves out those where it is 0. These tests build a grid whose
// texture is drawn on its left half (u < 0.5) and cut out on its right half, read every node back,
// and check the alpha of the node texture in the middle of each triangle.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "bm3d_file.h"
#include "model_files.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

constexpr int kTextureSide = 256;
// Enough triangles for several nodes and levels
constexpr int kCells = 200;
// The triangles within this of the edge of the cutout (u = 0.5) are not checked: the levels filter it
constexpr double kEdgeMargin = 0.05;

// Drawn (1) on the left half of the texture, cut out (0) on the right half
double drawn(int x) {
    return x < kTextureSide / 2 ? 1.0 : 0.0;
}

// The colours of the texture: the same on both halves, so that only the alpha tells them
double textureColour(int band, int, int y) {
    return band == 1 ? 200.0 : band == 2 ? double(y % 256) : 50.0;
}

struct CutoutCase {
    const char* name;
    // writes the texture "texture.png" and what the material names in map_d, and returns that name
    std::string (*write)(const TempDir& dir);
};

void PrintTo(const CutoutCase& c, std::ostream* out) {
    *out << c.name;
}

std::string ownAlpha(const TempDir& dir) {
    writePng(dir.file("texture.png"), makeRaster(kTextureSide, kTextureSide, 4, [](int band, int x, int y) {
                 return band == 4 ? 255.0 * drawn(x) : textureColour(band, x, y);
             }));
    return "texture.png";
}

std::string grayMask(const TempDir& dir) {
    writePng(dir.file("texture.png"), makeRaster(kTextureSide, kTextureSide, 3, textureColour));
    writePng(dir.file("mask.png"), makeRaster(kTextureSide, kTextureSide, 1, [](int, int x, int) { return 255.0 * drawn(x); }));
    return "mask.png";
}

// A mask with an alpha: its alpha, not its colour (black, which as a luminance would cut it all out)
std::string maskWithAlpha(const TempDir& dir) {
    writePng(dir.file("texture.png"), makeRaster(kTextureSide, kTextureSide, 3, textureColour));
    writePng(dir.file("mask.png"), makeRaster(kTextureSide, kTextureSide, 4, [](int band, int x, int) {
                 return band == 4 ? 255.0 * drawn(x) : 0.0;
             }));
    return "mask.png";
}

// A mask of another size than the texture is read at the nearest pixel
std::string smallerMask(const TempDir& dir) {
    writePng(dir.file("texture.png"), makeRaster(kTextureSide, kTextureSide, 3, textureColour));
    writePng(dir.file("mask.png"), makeRaster(16, 16, 1, [](int, int x, int) { return x < 8 ? 255.0 : 0.0; }));
    return "mask.png";
}

// The node texture at (u, v), the nearest texel
size_t texelAt(const DecodedImage& image, double u, double v) {
    const int x = std::clamp(int(std::floor(u * image.width)), 0, image.width - 1);
    const int y = std::clamp(int(std::floor((1.0 - v) * image.height)), 0, image.height - 1);
    return size_t(y) * size_t(image.width) + size_t(x);
}

// Builds the grid with the material `mtl` and checks every node: its texture has an alpha of 0 and
// 255 only (or none, when it cuts nothing out), 255 in the middle of the triangles of the left half
// and 0 in those of the right half
void expectCutOutRightHalf(const TempDir& dir, const std::string& mtl) {
    writeGridModel(dir.path(), kCells, "texture.png");
    writeTextFile(dir.file("grid.mtl"), mtl);
    const fs::path out = dir.file("grid.bm3d");
    ASSERT_NO_THROW(bm3d::build(dir.file("grid.obj"), out));

    const Bm3dFile file = readBm3dFile(out);
    ASSERT_GT(file.nodeTexture.size(), 2u);
    int drawnChecked = 0, cutChecked = 0, withAlpha = 0;
    for (size_t node = 0; node < file.nodeTexture.size(); node++) {
        ASSERT_GE(file.nodeTexture[node], 0) << "node " << node;
        const DecodedImage image = decodeWebp(file.textures[size_t(file.nodeTexture[node])]);
        // a node texture that cuts nothing out has no alpha
        const bool hasAlpha = image.bands.size() == 4;
        withAlpha += hasAlpha ? 1 : 0;
        if (hasAlpha)
            for (uint8_t alpha : image.bands[3])
                ASSERT_TRUE(alpha == 0 || alpha == 255) << "node " << node << ": an alpha of " << int(alpha);
        const NodeMesh mesh = readNode(out, uint32_t(node));
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            double low = 1, high = 0, u = 0, v = 0;
            for (int k = 0; k < 3; k++) {
                const uint32_t vertex = mesh.indices[i + size_t(k)];
                const double modelU = mesh.positions[vertex][0] / kCells;
                low = std::min(low, modelU);
                high = std::max(high, modelU);
                u += mesh.uvs[vertex][0] / 3;
                v += mesh.uvs[vertex][1] / 3;
            }
            const int alpha = hasAlpha ? image.bands[3][texelAt(image, u, v)] : 255;
            if (high < 0.5 - kEdgeMargin) {
                EXPECT_EQ(alpha, 255) << "node " << node << ", a triangle of the left half is cut out";
                drawnChecked++;
            } else if (low > 0.5 + kEdgeMargin) {
                EXPECT_EQ(alpha, 0) << "node " << node << ", a triangle of the right half is drawn";
                cutChecked++;
            }
        }
    }
    EXPECT_GT(withAlpha, 0);
    EXPECT_GT(drawnChecked, 1000);
    EXPECT_GT(cutChecked, 1000);
}

class CutoutTexture : public ::testing::TestWithParam<CutoutCase> {};

TEST_P(CutoutTexture, theNodeTexturesCutOutWhatTheAlphaCutsOut) {
    TempDir dir;
    const std::string alpha = GetParam().write(dir);
    expectCutOutRightHalf(dir, "newmtl m\nKd 1 1 1\nmap_Kd texture.png\nmap_d " + alpha + "\n");
}

INSTANTIATE_TEST_SUITE_P(
    Alpha, CutoutTexture,
    ::testing::Values(CutoutCase{"ownAlpha", ownAlpha}, CutoutCase{"grayMask", grayMask},
                      CutoutCase{"maskWithAlpha", maskWithAlpha}, CutoutCase{"smallerMask", smallerMask}),
    [](const ::testing::TestParamInfo<CutoutCase>& i) { return std::string(i.param.name); });

// The keyword is read in any case, and the name without the quotes some exporters put round it
TEST(CutoutTexture, theKeywordIsReadInAnyCaseAndTheNameWithoutQuotes) {
    TempDir dir;
    ownAlpha(dir);
    expectCutOutRightHalf(dir, "newmtl m\nKd 1 1 1\nmap_Kd texture.png\nMAP_D \"texture.png\"\n");
}

// Without map_d the alpha of the texture is dropped, as it always was: the node textures are opaque
TEST(CutoutTexture, theAlphaOfATextureWithoutMapDIsDropped) {
    TempDir dir;
    ownAlpha(dir);
    writeGridModel(dir.path(), 20, "texture.png");
    const fs::path out = dir.file("grid.bm3d");
    ASSERT_NO_THROW(bm3d::build(dir.file("grid.obj"), out));
    const Bm3dFile file = readBm3dFile(out);
    ASSERT_FALSE(file.textures.empty());
    for (const auto& texture : file.textures)
        EXPECT_EQ(decodeWebp(texture).bands.size(), 3u);
}

}  // namespace
