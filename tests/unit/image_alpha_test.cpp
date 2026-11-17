// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The alpha of a texture that cuts texels out: kept by the decoders when it is asked for, averaged by
// the downscaling with the colours weighed by it, cut at half, taken from a mask, written by the WebP
// encoder and kept by the tiles of the texture pyramid that are spilled to disk. An opaque image goes
// through all of it as it did before there was an alpha.

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "build/texpyramid.h"
#include "io/image/image.h"
#include "io/image/image_decoder.h"
#include "io/image/webp_codec.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3d::detail;
using bm3dtest::TempDir;

// The left half of the image drawn in `color`, the right half cut out (alpha 0) in `hidden`
Image halfCutOut(int width, int height, uint32_t color, uint32_t hidden) {
    Image image(width, height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            image.scanLine(y)[x] = x < width / 2 ? color : (hidden & 0x00ffffffu);
    return image;
}

std::vector<uint32_t> pixelsOf(const Image& image) {
    std::vector<uint32_t> pixels;
    for (int y = 0; y < image.height(); y++)
        pixels.insert(pixels.end(), image.scanLine(y), image.scanLine(y) + image.width());
    return pixels;
}

TEST(ImageAlpha, anImageIsOpaqueWhenEveryAlphaIsFull) {
    Image image(4, 4);
    image.fill(Image::rgb(1, 2, 3));
    EXPECT_TRUE(image.isOpaque());
    image.scanLine(3)[3] = Image::rgba(1, 2, 3, 254);
    EXPECT_FALSE(image.isOpaque());
    image.setOpaque();
    EXPECT_TRUE(image.isOpaque());
    EXPECT_EQ(image.scanLine(3)[3], Image::rgb(1, 2, 3));
    EXPECT_TRUE(Image().isOpaque());
}

TEST(ImageAlpha, theAlphaIsCutAtHalfAndTheColoursAreKept) {
    Image image(4, 1);
    const unsigned alphas[4] = {0, 127, 128, 255};
    for (int x = 0; x < 4; x++)
        image.scanLine(0)[x] = Image::rgba(10, 20, 30, alphas[x]);
    image.binarizeAlpha();
    EXPECT_EQ(image.scanLine(0)[0], Image::rgba(10, 20, 30, 0));
    EXPECT_EQ(image.scanLine(0)[1], Image::rgba(10, 20, 30, 0));
    EXPECT_EQ(image.scanLine(0)[2], Image::rgba(10, 20, 30, 255));
    EXPECT_EQ(image.scanLine(0)[3], Image::rgba(10, 20, 30, 255));
}

// The texels that are cut out do not tint the ones that are drawn: a pixel of the half size that
// covers drawn and cut texels has the colour of the drawn ones, and the alpha of their share
TEST(ImageAlpha, aDownscaledCutoutKeepsTheColourOfWhatIsDrawn) {
    Image image(4, 2);
    for (int x = 0; x < 4; x++)
        for (int y = 0; y < 2; y++)
            image.scanLine(y)[x] = x == 0 ? Image::rgb(200, 100, 50) : Image::rgba(0, 0, 255, 0);
    const Image half = image.downscaled(2, 1);
    EXPECT_EQ(half.scanLine(0)[0], Image::rgba(200, 100, 50, 128));  // half of the area is drawn
    // nothing is drawn: the colour of the texels, so that a filter that reads it is not darkened
    EXPECT_EQ(half.scanLine(0)[1], Image::rgba(0, 0, 255, 0));
}

TEST(ImageAlpha, aDownscaledOpaqueImageIsOpaqueAndAsBefore) {
    Image image(5, 3);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 5; x++)
            image.scanLine(y)[x] = Image::rgb(x * 50, y * 80, (x * y * 30) % 256);
    const Image small = image.downscaled(2, 2);
    EXPECT_TRUE(small.isOpaque());
    // the box filter of the colours: (0, 0) covers 2.5 x 1.5 pixels
    const uint32_t p = small.scanLine(0)[0];
    EXPECT_EQ((p >> 16) & 0xff, 40u);  // (0 + 50 + 0.5 * 100) / 2.5, in both rows
    EXPECT_EQ((p >> 8) & 0xff, 27u);   // (0 + 0.5 * 80) / 1.5
}

TEST(ImageAlpha, theAlphaOfAMaskIsItsAlphaWhenItHasOne) {
    Image texture(4, 4);
    texture.fill(Image::rgb(9, 9, 9));
    Image mask(4, 4);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            mask.scanLine(y)[x] = Image::rgba(0, 0, 0, x < 2 ? 255 : 0);  // black: its luminance would cut it all
    texture.setAlphaFrom(mask);
    EXPECT_EQ(texture.scanLine(1)[1], Image::rgba(9, 9, 9, 255));
    EXPECT_EQ(texture.scanLine(1)[3], Image::rgba(9, 9, 9, 0));
}

TEST(ImageAlpha, theAlphaOfAnOpaqueMaskIsItsLuminanceReadAtTheNearestPixel) {
    Image texture(8, 8);
    texture.fill(Image::rgb(9, 9, 9));
    Image mask(2, 1);  // a smaller mask: white on the left, black on the right
    mask.scanLine(0)[0] = Image::rgb(255, 255, 255);
    mask.scanLine(0)[1] = Image::rgb(0, 0, 0);
    texture.setAlphaFrom(mask);
    EXPECT_EQ(Image::alpha(texture.scanLine(7)[3]), 255u);
    EXPECT_EQ(Image::alpha(texture.scanLine(0)[4]), 0u);
    Image gray(1, 1);
    gray.fill(Image::rgb(100, 150, 200));
    Image one(1, 1);
    one.setAlphaFrom(gray);
    EXPECT_EQ(Image::alpha(one.scanLine(0)[0]), 141u);  // 0.299 * 100 + 0.587 * 150 + 0.114 * 200
}

TEST(ImageAlpha, theWebpOfACutoutKeepsItsAlphaAndAnOpaqueImageHasNone) {
    const Image cutout = halfCutOut(32, 16, Image::rgb(200, 100, 50), Image::rgb(10, 20, 30));
    const std::vector<unsigned char> webp = WebpEncoder(90).encode(cutout);
    const bm3dtest::DecodedImage decoded = bm3dtest::decodeWebp(std::vector<uint8_t>(webp.begin(), webp.end()));
    ASSERT_EQ(decoded.bands.size(), 4u);
    for (int x = 0; x < 32; x++)
        EXPECT_EQ(decoded.bands[3][size_t(5 * 32 + x)], x < 16 ? 255 : 0) << x;  // the alpha without loss

    Image opaque(32, 16);
    opaque.fill(Image::rgb(200, 100, 50));
    const std::vector<unsigned char> plain = WebpEncoder(90).encode(opaque);
    EXPECT_EQ(bm3dtest::decodeWebp(std::vector<uint8_t>(plain.begin(), plain.end())).bands.size(), 3u);
}

TEST(ImageAlpha, aLosslessWebpGivesBackEveryPixelAndTheDecoderKeepsTheAlpha) {
    Image image = halfCutOut(16, 8, Image::rgb(200, 100, 50), Image::rgb(10, 20, 30));
    image.scanLine(2)[3] = Image::rgba(1, 2, 3, 77);
    const std::vector<unsigned char> webp = WebpEncoder::lossless().encode(image);
    Image back;
    std::string error;
    ASSERT_TRUE(WebpDecoder().decode(webp.data(), webp.size(), back, error)) << error;
    EXPECT_EQ(pixelsOf(back), pixelsOf(image));  // the colours under an alpha of 0 too
}

TEST(ImageAlpha, aTextureFileIsReadOpaqueUnlessItsAlphaIsAskedFor) {
    TempDir dir;
    bm3dtest::Raster raster = bm3dtest::makeRaster(8, 8, 4, [](int band, int x, int) {
        return band == 4 ? (x < 4 ? 255.0 : 0.0) : 60.0 * band;
    });
    bm3dtest::writePng(dir.file("rgba.png"), raster);
    std::string error;
    const Image colors = loadImage(bm3dtest::pathToUtf8(dir.file("rgba.png")), error);
    ASSERT_FALSE(colors.isNull()) << error;
    EXPECT_TRUE(colors.isOpaque());
    EXPECT_EQ(colors.scanLine(0)[6], Image::rgb(60, 120, 180));
    const Image cutout = loadImage(bm3dtest::pathToUtf8(dir.file("rgba.png")), error, ImageAlpha::Keep);
    EXPECT_EQ(cutout.scanLine(0)[1], Image::rgba(60, 120, 180, 255));
    EXPECT_EQ(cutout.scanLine(0)[6], Image::rgba(60, 120, 180, 0));
}

// A tile that does not fit in memory is written to disk and read back: a tile with alpha comes back
// as it was (JPEG has no alpha, so it is kept as a lossless WebP)
TEST(ImageAlpha, aSpilledTileOfACutoutComesBackAsItWas) {
    TempDir dir;
    TexAtlas atlas;
    atlas.cache_max = 1;  // every tile goes to disk
    atlas.storage_prefix = bm3dtest::pathToUtf8(dir.file("tiles"));
    Image cutout = halfCutOut(40, 30, Image::rgb(200, 100, 50), Image::rgb(10, 20, 30));
    cutout.scanLine(7)[7] = Image::rgba(5, 6, 7, 99);
    std::vector<Image> textures = {cutout};
    atlas.addTextures(textures);
    ASSERT_TRUE(atlas.ram.empty());
    // the pyramid stores the texture bottom row first
    EXPECT_EQ(pixelsOf(atlas.read(0, 0, 0, 0, 40, 30)), pixelsOf(cutout.mirrored()));
}

}  // namespace
