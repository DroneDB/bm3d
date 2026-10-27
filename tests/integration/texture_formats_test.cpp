// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The program reads the textures of a model with image libraries of its own and the faces of a
// PLY with a parser of its own. These tests build small models made inside the test, one
// texture file format at a time, and read the header of what was written: the formats the
// textures come in (the node textures written are WebP whatever they were), the polygons of a
// PLY, and the failures that must leave no file behind. The inputs that are not what the loaders
// expect are in the robustness tests.

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "io/image/image_decoder.h"
#include "model_files.h"
#include "bm3d_file.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

// The reader opened the file, so it begins with BM3D; this is the version it says
void expectFormatVersion(const Bm3dFile& h) {
    EXPECT_EQ(h.formatVersion, 1u);
}

// The pictures the decoders have to get right and not only to read: a gray PNG with an alpha
// channel (the gray is the color and the alpha is dropped, even where it is 0), a PNG of 16 bit
// samples (scaled to 8 bits with rounding) and a CMYK JPEG written the Adobe way
Raster grayAlphaRaster() {
    return makeRaster(16, 16, 2, [](int band, int x, int y) { return band == 1 ? x * 16.0 : y * 16.0; });
}

// 0x20F0 is 32.9 of 255 in 8 bits: 33 when rounded, 32 when the low byte is dropped
const std::array<uint16_t, 3> kSample16 = {0x20F0, 0x7B40, 0xE0A0};
const std::array<int, 3> kColor16 = {33, 123, 224};

std::vector<uint16_t> flat16Bit() {
    std::vector<uint16_t> rgb;
    for (int i = 0; i < 16 * 16; i++)
        rgb.insert(rgb.end(), kSample16.begin(), kSample16.end());
    return rgb;
}

// Stored (inverted) c, m, y, k; the color is each of the first three times k over 255
const std::array<uint8_t, 4> kStoredCmyk = {240, 120, 60, 200};
const std::array<int, 3> kColorCmyk = {240 * 200 / 255, 120 * 200 / 255, 60 * 200 / 255};

std::vector<uint8_t> flatCmyk() {
    std::vector<uint8_t> samples;
    for (int i = 0; i < 16 * 16; i++)
        samples.insert(samples.end(), kStoredCmyk.begin(), kStoredCmyk.end());
    return samples;
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

    const fs::path out = dir.file("model.bm3d");
    ASSERT_NO_THROW(bm3d::build(dir.file("model.obj"), out));

    const Bm3dFile h = readBm3dFile(out);
    expectFormatVersion(h);
    EXPECT_EQ(h.nface, 2u);
    EXPECT_EQ(h.nvert, 4u);
    ASSERT_GE(h.nTextures, 1u) << "no texture in the file";
    ASSERT_FALSE(h.textures.empty());
    EXPECT_TRUE(isWebp(h.textures.front()));
}

// The formats the textures come in; TIFF only when the program can read it (a preprocessor
// directive cannot sit inside the arguments of the instantiation macro)
std::vector<TextureCase> textureCases() {
    std::vector<TextureCase> cases{
        {"jpeg", "jpg", [](const fs::path& f) { writeJpeg(f, gradientRaster(3)); }},
        {"pngRgb", "png", [](const fs::path& f) { writePng(f, gradientRaster(3)); }},
        {"pngRgba", "png", [](const fs::path& f) { writePng(f, gradientRaster(4)); }},
        {"pngPaletted", "png", [](const fs::path& f) { writePalettedPng(f, 16, 16, paletteIndices()); }},
        {"pngGray", "png", [](const fs::path& f) { writePng(f, gradientRaster(1)); }},
        {"pngGrayAlpha", "png", [](const fs::path& f) { writePng(f, grayAlphaRaster()); }},
        {"png16Bit", "png", [](const fs::path& f) { writeRgb16Png(f, 16, 16, flat16Bit()); }},
        {"jpegCmyk", "jpg", [](const fs::path& f) { writeCmykJpeg(f, 16, 16, flatCmyk()); }},
        {"webp", "webp", [](const fs::path& f) { writeWebp(f, gradientRaster(3)); }},
        {"bmp", "bmp", [](const fs::path& f) { writeBmp(f, gradientRaster(3)); }},
        // The one picture that is a file of the repository: nothing here writes a GIF
        {"gif", "gif", [](const fs::path& f) { fs::copy_file(std::string(BM3D_TEST_FIXTURES_DIR) + "/texture.gif", f); }},
    };
#ifdef BM3D_WITH_TIFF
    cases.push_back({"tiff", "tif", [](const fs::path& f) { writeTiff(f, gradientRaster(3)); }});
#endif
    return cases;
}

INSTANTIATE_TEST_SUITE_P(
    Formats,
    TextureFormat,
    ::testing::ValuesIn(textureCases()),
    [](const ::testing::TestParamInfo<TextureCase>& i) { return std::string(i.param.name); });

// --- the colors of the pictures that are easy to get wrong

// The pixel of the image the decoders hand to the builder, 0xffRRGGBB
bm3d::detail::Image decodeFile(const fs::path& file) {
    std::string error;
    bm3d::detail::Image image = bm3d::detail::loadImage(pathToUtf8(file), error);
    EXPECT_FALSE(image.isNull()) << error;
    return image;
}

TEST(TextureDecoding, aGrayPngWithAlphaGivesTheGrayEverywhereWhateverTheAlpha) {
    TempDir dir;
    writePng(dir.file("ga.png"), grayAlphaRaster());
    const bm3d::detail::Image image = decodeFile(dir.file("ga.png"));
    ASSERT_EQ(image.width(), 16);
    ASSERT_EQ(image.height(), 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            ASSERT_EQ(image.scanLine(y)[x], bm3d::detail::Image::rgb(x * 16, x * 16, x * 16)) << "at " << x << ", " << y;
}

TEST(TextureDecoding, aPngOf16BitSamplesIsScaledTo8BitsWithRounding) {
    TempDir dir;
    writeRgb16Png(dir.file("deep.png"), 16, 16, flat16Bit());
    const bm3d::detail::Image image = decodeFile(dir.file("deep.png"));
    ASSERT_EQ(image.width(), 16);
    ASSERT_EQ(image.height(), 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            ASSERT_EQ(image.scanLine(y)[x], bm3d::detail::Image::rgb(kColor16[0], kColor16[1], kColor16[2])) << "at " << x << ", " << y;
}

TEST(TextureDecoding, aCmykJpegGivesTheColorOfItsInvertedComponents) {
    TempDir dir;
    writeCmykJpeg(dir.file("cmyk.jpg"), 16, 16, flatCmyk());
    const bm3d::detail::Image image = decodeFile(dir.file("cmyk.jpg"));
    ASSERT_EQ(image.width(), 16);
    ASSERT_EQ(image.height(), 16);
    // at quality 100 the flat color survives the DCT to within a level
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            const uint32_t pixel = image.scanLine(y)[x];
            const int channels[3] = {int((pixel >> 16) & 0xff), int((pixel >> 8) & 0xff), int(pixel & 0xff)};
            for (int c = 0; c < 3; c++)
                ASSERT_NEAR(channels[c], kColorCmyk[size_t(c)], 1) << "channel " << c << " at " << x << ", " << y;
        }
}

// Every pixel of a 16 x 16 image is what `expected` says
void expectPixels(const bm3d::detail::Image& image, const std::function<uint32_t(int x, int y)>& expected) {
    ASSERT_EQ(image.width(), 16);
    ASSERT_EQ(image.height(), 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            ASSERT_EQ(image.scanLine(y)[x], expected(x, y)) << "at " << x << ", " << y;
}

bm3d::detail::Image decodePng(const PngSpec& spec, const std::vector<uint8_t>& rows) {
    TempDir dir;
    writeRawPng(dir.file("picture.png"), spec, rows);
    return decodeFile(dir.file("picture.png"));
}

std::vector<uint8_t> grayValues(const std::function<int(int x, int y)>& value) {
    std::vector<uint8_t> values;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            values.push_back(uint8_t(value(x, y)));
    return values;
}

// The pixels of a gray image of 2 to 8 bits whose color is the transparent one have always been
// black in the textures; the other transparent pixels (a palette entry, a color of an RGB image)
// keep their colors, and the alpha is dropped
TEST(TextureDecoding, theTransparentGrayOfAGrayPngIsBlack) {
    PngSpec spec;
    spec.width = spec.height = 16;
    spec.bitDepth = 4;
    spec.transparentGray = 5;
    expectPixels(decodePng(spec, packSamples(16, 16, 4, grayValues([](int x, int y) { return (x + y) % 16; }))),
                 [](int x, int y) { const int v = (x + y) % 16; return v == 5 ? bm3d::detail::Image::rgb(0, 0, 0) : bm3d::detail::Image::rgb(v * 17, v * 17, v * 17); });

    spec.bitDepth = 8;
    spec.transparentGray = 0x70;
    expectPixels(decodePng(spec, packSamples(16, 16, 8, grayValues([](int x, int y) { return x * 16 + y; }))),
                 [](int x, int y) { const int v = x * 16 + y; return v == 0x70 ? bm3d::detail::Image::rgb(0, 0, 0) : bm3d::detail::Image::rgb(v, v, v); });
}

TEST(TextureDecoding, aGrayPngOfOneBitIsBlackAndWhite) {
    PngSpec spec;
    spec.width = spec.height = 16;
    spec.bitDepth = 1;
    expectPixels(decodePng(spec, packSamples(16, 16, 1, grayValues([](int x, int y) { return (x + y) % 2; }))),
                 [](int x, int y) { const int v = (x + y) % 2 * 255; return bm3d::detail::Image::rgb(v, v, v); });
}

TEST(TextureDecoding, aPalettePngKeepsTheColorsOfItsTransparentEntries) {
    PngSpec spec;
    spec.width = spec.height = 16;
    spec.bitDepth = 4;
    spec.colorType = PngSpec::Palette;
    for (int i = 0; i < 16; i++) {
        std::array<uint8_t, 3> color;
        paletteColor(i, color.data());
        spec.palette.push_back(color);
    }
    spec.paletteAlpha = {0, 128, 255, 0};  // the first four entries, 0 to 3
    const std::vector<uint8_t> indices = paletteIndices();
    expectPixels(decodePng(spec, packSamples(16, 16, 4, indices)), [&indices](int x, int y) {
        uint8_t rgb[3];
        paletteColor(indices[size_t(y) * 16 + size_t(x)], rgb);
        return bm3d::detail::Image::rgb(rgb[0], rgb[1], rgb[2]);
    });
}

TEST(TextureDecoding, anInterlacedPngIsTheSamePictureAsThePlainOne) {
    const Raster raster = gradientRaster(3);
    PngSpec spec;
    spec.width = spec.height = 16;
    spec.colorType = PngSpec::Rgb;
    spec.interlaced = true;
    expectPixels(decodePng(spec, raster.pixels), [&raster](int x, int y) {
        const uint8_t* p = &raster.pixels[(size_t(y) * 16 + size_t(x)) * 3];
        return bm3d::detail::Image::rgb(p[0], p[1], p[2]);
    });
}

// --- and the same pictures through a whole build: the node texture holds the color

struct ColorCase {
    const char* name;
    const char* extension;
    std::function<void(const fs::path&)> write;
    std::array<int, 3> expected;
};

void PrintTo(const ColorCase& c, std::ostream* out) {
    *out << c.name;
}

class TextureColor : public ::testing::TestWithParam<ColorCase> {};

TEST_P(TextureColor, theNodeTextureHasTheColorOfThePicture) {
    const ColorCase& c = GetParam();
    TempDir dir;
    const std::string textureName = std::string("texture.") + c.extension;
    c.write(dir.file(textureName));
    writeTexturedModel(dir.path(), textureName);
    const fs::path out = dir.file("model.bm3d");
    ASSERT_NO_THROW(bm3d::build(dir.file("model.obj"), out));

    const Bm3dFile h = readBm3dFile(out);
    ASSERT_FALSE(h.textures.empty());
    const DecodedImage texture = decodeWebp(h.textures.front());
    ASSERT_GT(texture.width, 3);
    ASSERT_GT(texture.height, 3);
    // the middle of the texture is covered by the two triangles; the WebP is lossy
    for (int band = 0; band < 3; band++) {
        double sum = 0;
        int count = 0;
        for (int y = texture.height / 4; y < 3 * texture.height / 4; y++)
            for (int x = texture.width / 4; x < 3 * texture.width / 4; x++, count++)
                sum += texture.bands[size_t(band)][size_t(y) * size_t(texture.width) + size_t(x)];
        EXPECT_NEAR(sum / count, c.expected[size_t(band)], 8.0) << "band " << band;
    }
}

INSTANTIATE_TEST_SUITE_P(
    Colors,
    TextureColor,
    ::testing::Values(
        ColorCase{"pngGrayAlphaTransparent", "png",
                  [](const fs::path& f) { writePng(f, makeRaster(16, 16, 2, [](int band, int, int) { return band == 1 ? 100.0 : 0.0; })); },
                  {100, 100, 100}},
        ColorCase{"png16Bit", "png", [](const fs::path& f) { writeRgb16Png(f, 16, 16, flat16Bit()); }, kColor16},
        ColorCase{"jpegCmyk", "jpg", [](const fs::path& f) { writeCmykJpeg(f, 16, 16, flatCmyk()); }, kColorCmyk}),
    [](const ::testing::TestParamInfo<ColorCase>& i) { return std::string(i.param.name); });

TEST(TextureFormats, aModelWithoutTexturesHasNone) {
    TempDir dir;
    writeTextFile(dir.file("model.obj"), kTriangleObj);
    const fs::path out = dir.file("model.bm3d");
    ASSERT_NO_THROW(bm3d::build(dir.file("model.obj"), out));
    const Bm3dFile h = readBm3dFile(out);
    expectFormatVersion(h);
    EXPECT_EQ(h.nface, 1u);
    EXPECT_EQ(h.nTextures, 0u);
}

#ifndef BM3D_WITH_TIFF
// A program built without libtiff refuses a TIFF texture by name, as invalid input, and says that
// the program was built without it, not that the format is unknown. The first bytes decide, so
// the file needs no libtiff to be written.
TEST(TextureFormats, aTiffTextureIsRefusedByAProgramBuiltWithoutTiffSupport) {
    for (const std::string& magic : {std::string("II*\0\x08\0\0\0", 8), std::string("MM\0*\0\0\0\x08", 8)}) {
        TempDir dir;
        writeBinaryFile(dir.file("texture.tif"), magic);
        writeTexturedModel(dir.path(), "texture.tif");
        const fs::path out = dir.file("model.bm3d");
        try {
            bm3d::build(dir.file("model.obj"), out);
            ADD_FAILURE() << "the build should have failed";
        } catch (const bm3d::Error& e) {
            EXPECT_EQ(e.kind(), bm3d::ErrorKind::InvalidInput);
            const std::string message = e.what();
            EXPECT_NE(message.find("texture.tif"), std::string::npos) << message;
            EXPECT_NE(message.find("without TIFF support"), std::string::npos) << message;
        }
        EXPECT_FALSE(fs::exists(out));
    }
}
#endif

TEST(TextureFormats, aPlyQuadHasTheFacesOfTwoTriangles) {
    TempDir dir;
    writeTextFile(dir.file("quad.ply"), plyHeader(1) + "4 0 1 2 3\n");
    writeTextFile(dir.file("tris.ply"), plyHeader(2) + "3 0 1 2\n3 0 2 3\n");

    ASSERT_NO_THROW(bm3d::build(dir.file("quad.ply"), dir.file("quad.bm3d")));
    ASSERT_NO_THROW(bm3d::build(dir.file("tris.ply"), dir.file("tris.bm3d")));
    const Bm3dFile quad = readBm3dFile(dir.file("quad.bm3d"));
    const Bm3dFile tris = readBm3dFile(dir.file("tris.bm3d"));
    expectFormatVersion(quad);
    EXPECT_EQ(quad.nface, 2u);
    EXPECT_EQ(quad.nface, tris.nface);
    EXPECT_EQ(quad.nvert, tris.nvert);
}

TEST(TextureFormats, aTextureThatCannotBePackedBuildsWithoutTextures) {
    TempDir dir;
    writeBmp(dir.file("wide.bmp"), makeRaster(20000, 8, 3, [](int band, int x, int y) { return double((x + y * 30 + band) & 0xff); }));
    writeTexturedModel(dir.path(), "wide.bmp", "vt 0 0\nvt 0.99 0\nvt 0.99 0.99\nvt 0 0.99\n");
    const fs::path out = dir.file("model.bm3d");
    // The warning "textures could not be packed" goes to stderr: only the outcome is asserted here
    ASSERT_NO_THROW(bm3d::build(dir.file("model.obj"), out));
    const Bm3dFile h = readBm3dFile(out);
    expectFormatVersion(h);
    EXPECT_EQ(h.nface, 2u);
    EXPECT_EQ(h.nTextures, 0u);
}

}  // namespace
