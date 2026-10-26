// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pictures for the tests: made in memory, written in each of the texture file formats the
// program reads (with stb_image_write, libjpeg, libpng, libtiff and libwebp), and a decoder for the WebP
// the program writes in the node textures.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "paths.h"

namespace bm3dtest {

// 8 bit samples, rows from the top, the channels of a pixel one after the other
struct Raster {
    int width = 0;
    int height = 0;
    int channels = 3;  // 1 gray, 2 gray and alpha, 3 RGB, 4 RGBA (a PNG takes all four, the others RGB)
    std::vector<uint8_t> pixels;
};

// value(band, x, y) is the sample of the band (counted from 1) at the pixel, rounded and kept
// within 0..255
Raster makeRaster(int width, int height, int channels, const std::function<double(int band, int x, int y)>& value);

// A 16x16 picture with a gradient, the same in every format; the alpha of 4 channels is 200
Raster gradientRaster(int channels);

// The 16x16 picture of 16 colors: the color of a pixel is the index of its 4x4 block
std::vector<uint8_t> paletteIndices();
// The color of the palette index 0..15
void paletteColor(int index, uint8_t rgb[3]);

// These write the picture to `file` and throw std::runtime_error when they cannot
void writeJpeg(const fs::path& file, const Raster& raster);
void writePng(const fs::path& file, const Raster& raster);
void writeBmp(const fs::path& file, const Raster& raster);  // 24 bit, RGB
void writeTiff(const fs::path& file, const Raster& raster);  // 8 bit, uncompressed, in strips
void writeWebp(const fs::path& file, const Raster& raster);  // lossless, RGB
// A PNG with a palette of 16 colors (4 bits per pixel); `indices` has one entry per pixel
void writePalettedPng(const fs::path& file, int width, int height, const std::vector<uint8_t>& indices);

// What libpng needs to write a PNG that stb cannot: the layout of the samples, the palette and the
// transparency chunk (tRNS)
struct PngSpec {
    enum ColorType { Gray = 0, Rgb = 2, Palette = 3 };
    int width = 0;
    int height = 0;
    int bitDepth = 8;               // 1, 2, 4, 8 or 16 (1 to 8 for a palette)
    ColorType colorType = Gray;
    bool interlaced = false;        // Adam7; the rows given are those of the plain image
    std::vector<std::array<uint8_t, 3>> palette;  // a palette image
    std::vector<uint8_t> paletteAlpha;            // tRNS of a palette image: the alpha of the first entries
    int transparentGray = -1;                     // tRNS of a gray image: the sample value that is transparent
};
// `samples` (one 0..2^bitDepth-1 value per pixel, rows from the top) as the rows of a PNG hold them:
// the first pixel of a byte in its high bits, every row starting on a byte
std::vector<uint8_t> packSamples(int width, int height, int bitDepth, const std::vector<uint8_t>& samples);
// Writes the rows as a PNG stores them (sub-byte samples packed, 16 bit ones high byte first)
void writeRawPng(const fs::path& file, const PngSpec& spec, const std::vector<uint8_t>& rows);
// A PNG of 16 bit RGB samples (`rgb` has three per pixel, rows from the top)
void writeRgb16Png(const fs::path& file, int width, int height, const std::vector<uint16_t>& rgb);
// A JPEG of four components (`samples` has four per pixel, rows from the top) with the Adobe
// marker libjpeg writes for CMYK, whose components are stored inverted: 255 is no ink, so a
// color is (c * k / 255, m * k / 255, y * k / 255) of the stored c, m, y and k
void writeCmykJpeg(const fs::path& file, int width, int height, const std::vector<uint8_t>& samples);

// A decoded image: one plane per band (R, G, B and, when it has one, alpha)
struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<std::vector<uint8_t>> bands;
};

// Decodes a WebP image; throws std::runtime_error when it is not one
DecodedImage decodeWebp(const std::vector<uint8_t>& data);

}  // namespace bm3dtest
