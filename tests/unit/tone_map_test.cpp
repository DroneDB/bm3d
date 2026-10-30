// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The samples wider than 8 bits (the 16 bit and float TIFFs of multispectral and thermal models)
// are mapped to the 8 bits of a texture by a linear stretch between the 2nd and the 98th percentile
// of the samples that are not zero. The decoders are tested with real pictures through the builder
// (tests/integration); here, the one stretch of the textures of a model.

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "build/texpyramid.h"
#include "io/image/tone_map.h"
#include "paths.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3d::detail;
using namespace bm3dtest;

TEST(SampleHistogram, zeroAndValuesThatAreNotFiniteAreNotCounted) {
    SampleHistogram samples;
    samples.add(0.0f);
    samples.add(-0.0f);
    samples.add(std::numeric_limits<float>::quiet_NaN());
    samples.add(std::numeric_limits<float>::infinity());
    samples.add(-std::numeric_limits<float>::infinity());
    EXPECT_EQ(samples.count(), 0u);
    EXPECT_EQ(samples.quantile(1, 2), 0.0f);
    samples.add(7.0f);
    EXPECT_EQ(samples.count(), 1u);
}

TEST(SampleHistogram, aQuantileIsWithinOnePercentOfTheSampleItStandsFor) {
    // the values of 16 bit reflectance and of float reflectance alike
    for (const float scale : {1.0f, 1.0f / 65535.0f}) {
        SampleHistogram samples;
        for (int value = 1; value <= 1000; value++)
            samples.add(float(value * 40) * scale);
        const float median = samples.quantile(1, 2);
        EXPECT_NEAR(median, 20000.0f * scale, 20000.0f * scale * 0.01f) << "scale " << scale;
        EXPECT_LE(samples.quantile(2, 100), samples.quantile(98, 100));
    }
}

TEST(SampleHistogram, negativeValuesSortBeforePositiveOnes) {
    SampleHistogram samples;
    for (int i = 0; i < 50; i++) {
        samples.add(-0.5f);
        samples.add(-0.001f);
        samples.add(0.25f);
        samples.add(4.0f);
    }
    EXPECT_NEAR(samples.quantile(1, 4), -0.5f, 0.005f);
    EXPECT_NEAR(samples.quantile(2, 4), -0.001f, 0.00001f);
    EXPECT_NEAR(samples.quantile(3, 4), 0.25f, 0.0025f);
    EXPECT_NEAR(samples.quantile(4, 4), 4.0f, 0.04f);
}

TEST(SampleHistogram, histogramsAddUpAndKeepTheLargestFullScale) {
    SampleHistogram a, b;
    a.add(100.0f);
    a.addFullScale(1.0f);
    b.add(200.0f);
    b.add(300.0f);
    b.addFullScale(65535.0f);
    a.add(b);
    EXPECT_EQ(a.count(), 3u);
    EXPECT_EQ(a.fullScale(), 65535.0f);
}

TEST(ToneMap, theLowValueAndBelowAre0AndTheHighOneAndAbove255) {
    const ToneMap map(1000, 3000);
    EXPECT_EQ(map(0), 0);
    EXPECT_EQ(map(1000), 0);
    EXPECT_EQ(map(2000), 128);  // 127.5, rounded
    EXPECT_EQ(map(3000), 255);
    EXPECT_EQ(map(60000), 255);
    EXPECT_EQ(map(std::nan("")), 0);
}

TEST(ToneMap, theStretchGoesFromThe2ndToThe98thPercentile) {
    // 6000 to 38000: where the 16 bit reflectance of a multispectral camera lies, in a small part of
    // the range of the type; its high byte would be 23 to 148
    SampleHistogram samples;
    for (int value = 6000; value < 38000; value += 4)
        samples.add(float(value));
    const ToneMap map = ToneMap::stretch(samples);
    EXPECT_NEAR(map.low(), 6640, 6640 * 0.01);
    EXPECT_NEAR(map.high(), 37360, 37360 * 0.01);
    EXPECT_EQ(map(6000), 0);
    EXPECT_EQ(map(38000), 255);
    EXPECT_NEAR(map(22000), 128, 3);
}

TEST(ToneMap, aFlatImageIsStretchedFromZeroToTheFullScaleOfItsType) {
    SampleHistogram integers;
    integers.addFullScale(65535.0f);
    for (int i = 0; i < 100; i++)
        integers.add(0x7B40);
    EXPECT_EQ(ToneMap::stretch(integers)(0x7B40), 123);  // 0x7B40 / 65535 * 255 = 122.6

    SampleHistogram floats;
    floats.addFullScale(1.0f);
    for (int i = 0; i < 100; i++)
        floats.add(0.5f);
    EXPECT_EQ(ToneMap::stretch(floats)(0.5), 128);

    // nothing counted: the zeros stay black
    SampleHistogram none;
    none.addFullScale(65535.0f);
    EXPECT_EQ(ToneMap::stretch(none)(0), 0);
}

#ifdef BM3D_WITH_TIFF
int red(const Image& image, int x, int y) {
    return int((image.scanLine(y)[x] >> 16) & 0xff);
}

TEST(TexAtlas, theTexturesOfAModelShareOneStretch) {
    // two textures of 16 x 16 flat halves: the first 10000 and 30000, the second 30000 and 50000.
    // Stretched each on its own, 30000 is white in the first and black in the second; stretched
    // together it is the same gray in both, as a surface across two textures of an atlas must be.
    TempDir dir;
    TiffSpec spec;
    spec.width = spec.height = 16;
    std::vector<LoadTexture> textures;
    for (const auto& [left, right] : {std::pair<double, double>{10000, 30000}, {30000, 50000}}) {
        std::vector<double> samples;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                samples.push_back(x < 8 ? left : right);
        const fs::path file = dir.file("texture" + std::to_string(textures.size()) + ".tif");
        writeRawTiff(file, spec, samples);
        textures.emplace_back(pathToUtf8(file));
    }

    TexAtlas atlas;
    atlas.storage_prefix = pathToUtf8(dir.file("scratch"));
    ASSERT_TRUE(atlas.addTextures(textures));
    const Image first = atlas.read(0, 0, 0, 0, 16, 16);
    const Image second = atlas.read(1, 0, 0, 0, 16, 16);
    ASSERT_FALSE(first.isNull());
    ASSERT_FALSE(second.isNull());
    for (int y = 0; y < 16; y++) {
        EXPECT_EQ(red(first, 0, y), 0);
        EXPECT_EQ(red(second, 15, y), 255);
        EXPECT_EQ(first.scanLine(y)[15], second.scanLine(y)[0]) << "30000 at row " << y;
        EXPECT_NEAR(red(first, 15, y), 128, 4);
    }
}
#endif

}  // namespace
