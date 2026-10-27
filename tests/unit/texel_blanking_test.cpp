// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The texels of a node texture that no triangle of the node draws, or reads when it filters, are
// given one color, so that they cost nothing to store; and a model whose texture is much finer than
// its triangles keeps its triangles for the first levels while the texture comes down to size.

#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "build/node_texture_baker.h"
#include "build/texel_blanking.h"

namespace {

using namespace bm3d::detail;

constexpr int kSide = 64;

// The triangle of the texels (10, 10), (30, 10), (10, 30) of an image of kSide texels, as the texture
// coordinates of its corners (row 0 is at the top, v = 1)
UvTriangle triangleOfTexels() {
    auto uv = [](float x, float y) { return vcg::Point2f(x / kSide, 1.0f - y / kSide); };
    return {uv(10, 10), uv(30, 10), uv(10, 30)};
}

// Each texel its own color, so that a texel that is changed is told from one that is not
Image distinctImage() {
    Image image(kSide, kSide);
    for (int y = 0; y < kSide; y++)
        for (int x = 0; x < kSide; x++)
            image.scanLine(y)[x] = Image::rgb(x * 4, y * 4, (x + y) % 7);
    return image;
}

uint32_t original(int x, int y) {
    return Image::rgb(x * 4, y * 4, (x + y) % 7);
}

TEST(TexelBlanking, aTexelInsideATriangleIsKept) {
    Image image = distinctImage();
    blankUndrawnTexels(image, {triangleOfTexels()});
    EXPECT_EQ(image.scanLine(15)[15], original(15, 15));
    EXPECT_EQ(image.scanLine(12)[11], original(11, 12));
}

TEST(TexelBlanking, aTexelFarFromEveryTriangleGetsTheFlatColor) {
    Image image = distinctImage();
    blankUndrawnTexels(image, {triangleOfTexels()});
    const uint32_t flat = image.scanLine(60)[60];
    EXPECT_NE(flat, original(60, 60));
    EXPECT_EQ(image.scanLine(0)[0], flat);
    EXPECT_EQ(image.scanLine(50)[5], flat);
    EXPECT_EQ(image.scanLine(5)[50], flat);
}

TEST(TexelBlanking, theTexelsWithinFourOfADrawnTexelAreKeptAndTheNextOnesAreNot) {
    // the texel under the corner (30, 10) is drawn even though its center is not inside the triangle,
    // and it is the last of its row; the first row drawn is 10, and the corner (10, 30) gives the
    // last column and the last row
    Image image = distinctImage();
    blankUndrawnTexels(image, {triangleOfTexels()});
    EXPECT_EQ(image.scanLine(10)[34], original(34, 10));
    EXPECT_NE(image.scanLine(10)[35], original(35, 10));
    EXPECT_EQ(image.scanLine(6)[12], original(12, 6));
    EXPECT_NE(image.scanLine(5)[12], original(12, 5));
    EXPECT_EQ(image.scanLine(34)[10], original(10, 34));
    EXPECT_NE(image.scanLine(35)[10], original(10, 35));
}

TEST(TexelBlanking, theFlatColorIsTheMeanOfTheKeptTexels) {
    Image image = distinctImage();
    blankUndrawnTexels(image, {triangleOfTexels()});
    uint64_t sum[3] = {0, 0, 0}, kept = 0;
    uint32_t flat = 0;
    bool haveFlat = false;
    for (int y = 0; y < kSide; y++)
        for (int x = 0; x < kSide; x++) {
            const uint32_t pixel = image.scanLine(y)[x];
            if (pixel == original(x, y)) {
                sum[0] += (pixel >> 16) & 0xff;
                sum[1] += (pixel >> 8) & 0xff;
                sum[2] += pixel & 0xff;
                kept++;
            } else if (!haveFlat) {
                flat = pixel;
                haveFlat = true;
            } else {
                ASSERT_EQ(pixel, flat) << "at " << x << ", " << y;  // the texels that are not kept are one color
            }
        }
    ASSERT_TRUE(haveFlat);
    ASSERT_GT(kept, 0u);
    EXPECT_EQ(flat, Image::rgb(unsigned(sum[0] / kept), unsigned(sum[1] / kept), unsigned(sum[2] / kept)));
}

TEST(TexelBlanking, anImageWithNoTriangleIsLeftAsItIs) {
    Image image = distinctImage();
    blankUndrawnTexels(image, {});
    for (int y = 0; y < kSide; y++)
        for (int x = 0; x < kSide; x++)
            ASSERT_EQ(image.scanLine(y)[x], original(x, y));
}

TEST(TexelBlanking, anImageNoLargerThanTwiceTheMarginIsLeftAsItIs) {
    Image image(8, 8);
    image.fill(Image::rgb(1, 2, 3));
    image.scanLine(7)[7] = Image::rgb(9, 9, 9);
    blankUndrawnTexels(image, {UvTriangle{vcg::Point2f(0.1f, 0.9f), vcg::Point2f(0.2f, 0.9f), vcg::Point2f(0.1f, 0.8f)}});
    EXPECT_EQ(image.scanLine(7)[7], Image::rgb(9, 9, 9));
}

TEST(TexelBlanking, theCornersOfATriangleWithNoAreaMarkTheirTexels) {
    Image image = distinctImage();
    const vcg::Point2f corner(20.5f / kSide, 1.0f - 20.5f / kSide);
    blankUndrawnTexels(image, {UvTriangle{corner, corner, corner}});
    EXPECT_EQ(image.scanLine(20)[20], original(20, 20));
    EXPECT_EQ(image.scanLine(24)[24], original(24, 24));
    EXPECT_NE(image.scanLine(26)[20], original(20, 26));
}

TEST(TextureOnlyLevels, aTextureNoFinerThanItsTrianglesNeedsNoLevel) {
    EXPECT_EQ(NodeTextureBaker::textureOnlyLevels(0, 0.5f), 0);
    EXPECT_EQ(NodeTextureBaker::textureOnlyLevels(std::sqrt(3.0) / 4.0 * 10.0, 0.5f), 0);
}

TEST(TextureOnlyLevels, eachLevelKeepsAQuarterOfTheTexels) {
    const double balanced = std::sqrt(3.0) / 4.0 * 10.0;
    EXPECT_EQ(NodeTextureBaker::textureOnlyLevels(balanced * 1.01, 0.5f), 1);
    EXPECT_EQ(NodeTextureBaker::textureOnlyLevels(balanced * 4, 0.5f), 1);  // a power of 4 is exact
    EXPECT_EQ(NodeTextureBaker::textureOnlyLevels(balanced * 4 * 1.01, 0.5f), 2);
    EXPECT_EQ(NodeTextureBaker::textureOnlyLevels(balanced * 64, 0.5f), 3);
    EXPECT_EQ(NodeTextureBaker::textureOnlyLevels(balanced * 256 * 1.01, 0.5f), 5);
}

TEST(TextureOnlyLevels, aTextureBeyondWhatCanBeHalvedIsCountedUpToSixtyFourLevels) {
    EXPECT_EQ(NodeTextureBaker::textureOnlyLevels(std::numeric_limits<double>::infinity(), 0.5f), 64);
    EXPECT_EQ(NodeTextureBaker::textureOnlyLevels(1e300, 0.5f), 64);
}

TEST(TextureOnlyLevels, aScalingThatDoesNotSimplifyKeepsNoLevel) {
    for (float scaling : {0.0f, -1.0f, 1.0f, 1.5f})
        EXPECT_EQ(NodeTextureBaker::textureOnlyLevels(1e6, scaling), 0) << scaling;
}

}  // namespace
