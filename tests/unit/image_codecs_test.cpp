// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The image decoders are chosen by the first bytes of the data, not by the name of the file, and the
// encoders are interchangeable behind one interface. The decoders themselves are tested with real
// pictures of every format through the builder (tests/integration).

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/error.h"
#include "io/image/image_decoder.h"
#include "io/image/image_encoder.h"
#include "io/image/jpeg_codec.h"
#include "io/image/stb_decoder.h"
#include "io/image/tiff_decoder.h"
#include "io/image/webp_codec.h"

namespace {

using namespace bm3d::detail;

// A decoder that takes the data starting with `magic` and makes a 1x1 image of `color`
class FakeDecoder : public ImageDecoder {
public:
    FakeDecoder(std::string magic, uint32_t color) : magic_(std::move(magic)), color_(color) {}
    bool matches(const unsigned char* data, size_t size) const override {
        return size >= magic_.size() && std::memcmp(data, magic_.data(), magic_.size()) == 0;
    }
    bool decode(const unsigned char*, size_t, Image& image, std::string&) const override {
        image = Image(1, 1);
        image.fill(color_);
        return true;
    }

private:
    std::string magic_;
    uint32_t color_;
};

const unsigned char* bytes(const std::string& text) {
    return reinterpret_cast<const unsigned char*>(text.data());
}

Image gradient(int width, int height) {
    Image image(width, height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            image.scanLine(y)[x] = Image::rgb(x * 255 / width, y * 255 / height, 100);
    return image;
}

TEST(ImageDecoderRegistry, theDecoderIsChosenByTheFirstBytes) {
    ImageDecoderRegistry registry;
    registry.add(std::unique_ptr<ImageDecoder>(new FakeDecoder("AAA", Image::rgb(1, 0, 0))));
    registry.add(std::unique_ptr<ImageDecoder>(new FakeDecoder("BBB", Image::rgb(0, 2, 0))));
    Image image;
    std::string error;
    const std::string b = "BBB and the rest";
    ASSERT_TRUE(registry.decode(bytes(b), b.size(), image, error)) << error;
    EXPECT_EQ(image.scanLine(0)[0], Image::rgb(0, 2, 0));
    const std::string a = "AAA";
    ASSERT_TRUE(registry.decode(bytes(a), a.size(), image, error)) << error;
    EXPECT_EQ(image.scanLine(0)[0], Image::rgb(1, 0, 0));
}

TEST(ImageDecoderRegistry, whenTwoDecodersMatchTheFirstAddedReadsTheData) {
    ImageDecoderRegistry registry;
    registry.add(std::unique_ptr<ImageDecoder>(new FakeDecoder("AA", Image::rgb(1, 0, 0))));
    registry.add(std::unique_ptr<ImageDecoder>(new FakeDecoder("AAA", Image::rgb(0, 2, 0))));
    Image image;
    std::string error;
    const std::string a = "AAAA";
    ASSERT_TRUE(registry.decode(bytes(a), a.size(), image, error));
    EXPECT_EQ(image.scanLine(0)[0], Image::rgb(1, 0, 0));
}

TEST(ImageDecoderRegistry, dataNoDecoderMatchesIsAnUnknownFormat) {
    ImageDecoderRegistry registry;
    registry.add(std::unique_ptr<ImageDecoder>(new FakeDecoder("AAA", 0)));
    Image image;
    std::string error;
    const std::string other = "something else";
    EXPECT_EQ(registry.find(bytes(other), other.size()), nullptr);
    EXPECT_FALSE(registry.decode(bytes(other), other.size(), image, error));
    EXPECT_EQ(error, "unknown image format");
    EXPECT_FALSE(registry.decode(nullptr, 0, image, error));
}

TEST(ImageDecoderRegistry, theStandardRegistryTellsTheFormatsByTheirSignature) {
    const ImageDecoderRegistry& registry = ImageDecoderRegistry::standard();
    const std::vector<unsigned char> jpeg = JpegEncoder(90).encode(gradient(16, 16));
    const std::vector<unsigned char> webp = WebpEncoder(90).encode(gradient(16, 16));
    EXPECT_NE(dynamic_cast<const JpegDecoder*>(registry.find(jpeg.data(), jpeg.size())), nullptr);
    EXPECT_NE(dynamic_cast<const WebpDecoder*>(registry.find(webp.data(), webp.size())), nullptr);
    for (const char* tiff : {"II*\0", "MM\0*", "II+\0", "MM\0+"})
        EXPECT_NE(dynamic_cast<const TiffDecoder*>(registry.find(bytes(std::string(tiff, 4)), 4)), nullptr);
    // PNG, BMP, GIF and TGA, and whatever else, are stb's
    const std::string png = "\x89PNG\r\n\x1a\n";
    EXPECT_NE(dynamic_cast<const StbDecoder*>(registry.find(bytes(png), png.size())), nullptr);
    const std::string unknown = "not an image";
    EXPECT_NE(dynamic_cast<const StbDecoder*>(registry.find(bytes(unknown), unknown.size())), nullptr);
}

TEST(ImageDecoderRegistry, aRiffFileThatIsNotWebpIsNotWebp) {
    const std::string wave = std::string("RIFF\0\0\0\0WAVE", 12);
    EXPECT_FALSE(WebpDecoder().matches(bytes(wave), wave.size()));
    EXPECT_FALSE(JpegDecoder().matches(bytes(wave), wave.size()));
}

TEST(ImageDecoderRegistry, whatTheEncodersWriteIsReadBackByTheStandardRegistry) {
    const ImageDecoderRegistry& registry = ImageDecoderRegistry::standard();
    const Image source = gradient(32, 24);
    for (const std::unique_ptr<ImageEncoder>& encoder :
         {std::unique_ptr<ImageEncoder>(new JpegEncoder(95)), std::unique_ptr<ImageEncoder>(new WebpEncoder(95))}) {
        const std::vector<unsigned char> data = encoder->encode(source);
        Image back;
        std::string error;
        ASSERT_TRUE(registry.decode(data.data(), data.size(), back, error)) << error;
        EXPECT_EQ(back.width(), 32);
        EXPECT_EQ(back.height(), 24);
    }
}

TEST(ImageEncoder, anImageThatIsNullIsRefused) {
    EXPECT_THROW(WebpEncoder(90).encode(Image()), Error);
    EXPECT_THROW(JpegEncoder(90).encode(Image()), Error);
}

TEST(ImageEncoder, theWebpQualityChangesTheSizeAndAnOutOfRangeOneIsBroughtIn) {
    const Image source = gradient(64, 64);
    const size_t low = WebpEncoder(10).encode(source).size();
    const size_t high = WebpEncoder(100).encode(source).size();
    EXPECT_LT(low, high);
    EXPECT_EQ(WebpEncoder(1000).encode(source), WebpEncoder(100).encode(source));
    EXPECT_EQ(WebpEncoder(-5).encode(source), WebpEncoder(0).encode(source));
}

}  // namespace
