// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pictures.h"

#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

#include <jpeglib.h>
#include <png.h>
#include <tiffio.h>
#include <webp/decode.h>
#include <webp/encode.h>

// The stb writer is used through its callbacks: the bytes land in a buffer that is written to a
// file opened from a path of any alphabet, which the narrow file name of the stb functions cannot
// hold on Windows. The functions are static: the program has the stb reader's own copy.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

namespace bm3dtest {

namespace {

void writeBytes(const fs::path& file, const void* data, size_t size) {
    std::ofstream out(file, std::ios::binary);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    out.close();
    if (!out)
        throw std::runtime_error("cannot write " + pathToUtf8(file));
}

void collect(void* context, void* data, int size) {
    auto* bytes = static_cast<std::vector<uint8_t>*>(context);
    const auto* p = static_cast<const uint8_t*>(data);
    bytes->insert(bytes->end(), p, p + size);
}

// libjpeg reports an error by calling error_exit, which must not return: it jumps back to the
// setjmp of writeJpeg
struct JpegErrorJump : jpeg_error_mgr {
    std::jmp_buf jump;
};

extern "C" void jumpOnJpegError(j_common_ptr info) {
    std::longjmp(static_cast<JpegErrorJump*>(info->err)->jump, 1);
}

// Runs one of the stb *_to_func writers and writes what it produced
template <typename Encode>
void writeWithStb(const fs::path& file, const Raster& raster, Encode encode) {
    std::vector<uint8_t> bytes;
    if (!encode(&collect, &bytes, raster.width, raster.height, raster.channels, raster.pixels.data()) || bytes.empty())
        throw std::runtime_error("cannot encode " + pathToUtf8(file));
    writeBytes(file, bytes.data(), bytes.size());
}

void requireChannels(const Raster& raster, int channels, const char* format) {
    if (raster.channels != channels)
        throw std::runtime_error(std::string(format) + " takes " + std::to_string(channels) + " channels");
    if (raster.pixels.size() != size_t(raster.width) * size_t(raster.height) * size_t(raster.channels))
        throw std::runtime_error("the raster holds the wrong number of samples");
}

}  // namespace

Raster makeRaster(int width, int height, int channels, const std::function<double(int, int, int)>& value) {
    Raster r;
    r.width = width;
    r.height = height;
    r.channels = channels;
    r.pixels.resize(size_t(width) * size_t(height) * size_t(channels));
    size_t at = 0;
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            for (int band = 1; band <= channels; band++)
                r.pixels[at++] = uint8_t(std::min(255.0, std::max(0.0, std::floor(value(band, x, y) + 0.5))));
    return r;
}

Raster gradientRaster(int channels) {
    return makeRaster(16, 16, channels, [channels](int band, int x, int y) {
        if (channels == 4 && band == 4)
            return 200.0;
        return double((x * 16 + y * 3 + (band - 1) * 40) & 0xff);
    });
}

void paletteColor(int index, uint8_t rgb[3]) {
    rgb[0] = uint8_t(index * 16);
    rgb[1] = uint8_t(255 - index * 16);
    rgb[2] = uint8_t(index * 8);
}

std::vector<uint8_t> paletteIndices() {
    std::vector<uint8_t> indices(16 * 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            indices[size_t(y) * 16 + size_t(x)] = uint8_t(x / 4 + 4 * (y / 4));
    return indices;
}

// libjpeg, not stb: the JPEG writer of stb shifts negative ints left, which C++17 leaves undefined
void writeJpeg(const fs::path& file, const Raster& raster) {
    requireChannels(raster, 3, "JPEG");
    // Made before the jump target below: a jump back does not run the destructors of what was
    // made after it. jpeg_destroy_compress does nothing to a zeroed object that was never created.
    unsigned char* encoded = nullptr;  // malloc'ed by libjpeg's memory destination
    unsigned long size = 0;
    jpeg_compress_struct info{};
    JpegErrorJump errors;
    info.err = jpeg_std_error(&errors);
    errors.error_exit = &jumpOnJpegError;
    if (setjmp(errors.jump)) {
        jpeg_destroy_compress(&info);
        std::free(encoded);
        throw std::runtime_error("cannot encode " + pathToUtf8(file));
    }
    jpeg_create_compress(&info);
    jpeg_mem_dest(&info, &encoded, &size);
    info.image_width = JDIMENSION(raster.width);
    info.image_height = JDIMENSION(raster.height);
    info.input_components = 3;
    info.in_color_space = JCS_RGB;
    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, 90, TRUE);
    jpeg_start_compress(&info, TRUE);
    const size_t stride = size_t(raster.width) * 3;
    while (info.next_scanline < info.image_height) {
        // jpeg_write_scanlines takes rows that are not const and only reads them
        JSAMPROW row = const_cast<JSAMPROW>(raster.pixels.data() + size_t(info.next_scanline) * stride);
        jpeg_write_scanlines(&info, &row, 1);
    }
    jpeg_finish_compress(&info);
    jpeg_destroy_compress(&info);
    std::unique_ptr<unsigned char, decltype(&std::free)> owner(encoded, &std::free);
    writeBytes(file, encoded, size_t(size));
}

void writePng(const fs::path& file, const Raster& raster) {
    requireChannels(raster, raster.channels, "PNG");
    writeWithStb(file, raster, [](auto* func, void* ctx, int w, int h, int c, const uint8_t* data) {
        return stbi_write_png_to_func(func, ctx, w, h, c, data, w * c) != 0;
    });
}

void writeBmp(const fs::path& file, const Raster& raster) {
    requireChannels(raster, 3, "BMP");
    writeWithStb(file, raster, [](auto* func, void* ctx, int w, int h, int c, const uint8_t* data) {
        return stbi_write_bmp_to_func(func, ctx, w, h, c, data) != 0;
    });
}

void writeTiff(const fs::path& file, const Raster& raster) {
    requireChannels(raster, 3, "TIFF");
#ifdef _WIN32
    TIFF* tiff = TIFFOpenW(file.c_str(), "w");
#else
    TIFF* tiff = TIFFOpen(file.c_str(), "w");
#endif
    if (tiff == nullptr)
        throw std::runtime_error("cannot write " + pathToUtf8(file));
    std::unique_ptr<TIFF, decltype(&TIFFClose)> closer(tiff, &TIFFClose);

    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, uint32_t(raster.width));
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, uint32_t(raster.height));
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tiff, 0));

    const size_t stride = size_t(raster.width) * 3;
    for (int y = 0; y < raster.height; y++) {
        // TIFFWriteScanline takes a buffer that is not const
        std::vector<uint8_t> row(raster.pixels.begin() + std::ptrdiff_t(size_t(y) * stride),
                                 raster.pixels.begin() + std::ptrdiff_t(size_t(y + 1) * stride));
        if (TIFFWriteScanline(tiff, row.data(), uint32_t(y), 0) < 0)
            throw std::runtime_error("cannot write " + pathToUtf8(file));
    }
}

void writeWebp(const fs::path& file, const Raster& raster) {
    requireChannels(raster, 3, "WebP");
    uint8_t* encoded = nullptr;
    const size_t size = WebPEncodeLosslessRGB(raster.pixels.data(), raster.width, raster.height, raster.width * 3, &encoded);
    if (size == 0 || encoded == nullptr)
        throw std::runtime_error("cannot encode " + pathToUtf8(file));
    std::unique_ptr<uint8_t, decltype(&WebPFree)> owner(encoded, &WebPFree);
    writeBytes(file, encoded, size);
}

void writePalettedPng(const fs::path& file, int width, int height, const std::vector<uint8_t>& indices) {
    if (indices.size() != size_t(width) * size_t(height))
        throw std::runtime_error("one palette index per pixel is needed");

    std::vector<uint8_t> bytes;
    // Made before the jump target below: a jump back does not run the destructors of what was
    // made after it
    const size_t rowBytes = (size_t(width) + 1) / 2;  // two pixels in a byte, the first in the high nibble
    std::vector<uint8_t> row(rowBytes);
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png != nullptr ? png_create_info_struct(png) : nullptr;
    if (png == nullptr || info == nullptr)
        throw std::runtime_error("cannot start the PNG writer");

    // libpng reports an error by jumping back here
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        throw std::runtime_error("cannot encode " + pathToUtf8(file));
    }
    png_set_write_fn(
        png, &bytes,
        [](png_structp p, png_bytep data, png_size_t length) {
            auto* out = static_cast<std::vector<uint8_t>*>(png_get_io_ptr(p));
            out->insert(out->end(), data, data + length);
        },
        [](png_structp) {});
    png_set_IHDR(png, info, uint32_t(width), uint32_t(height), 4, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_color palette[16];
    for (int i = 0; i < 16; i++) {
        uint8_t rgb[3];
        paletteColor(i, rgb);
        palette[i] = {rgb[0], rgb[1], rgb[2]};
    }
    png_set_PLTE(png, info, palette, 16);
    png_write_info(png, info);

    for (int y = 0; y < height; y++) {
        std::fill(row.begin(), row.end(), 0);
        for (int x = 0; x < width; x++) {
            const uint8_t index = indices[size_t(y) * size_t(width) + size_t(x)] & 0x0f;
            row[size_t(x) / 2] = uint8_t(row[size_t(x) / 2] | (x % 2 == 0 ? index << 4 : index));
        }
        png_write_row(png, row.data());
    }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    writeBytes(file, bytes.data(), bytes.size());
}

DecodedImage decodeWebp(const std::vector<uint8_t>& data) {
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(data.data(), data.size(), &features) != VP8_STATUS_OK)
        throw std::runtime_error("not a WebP image");
    int width = 0, height = 0;
    std::unique_ptr<uint8_t, decltype(&WebPFree)> rgba(WebPDecodeRGBA(data.data(), data.size(), &width, &height), &WebPFree);
    if (!rgba)
        throw std::runtime_error("cannot decode the WebP image");

    DecodedImage image;
    image.width = width;
    image.height = height;
    const int bands = features.has_alpha ? 4 : 3;
    const size_t pixels = size_t(width) * size_t(height);
    image.bands.assign(size_t(bands), std::vector<uint8_t>(pixels));
    for (size_t i = 0; i < pixels; i++)
        for (int b = 0; b < bands; b++)
            image.bands[size_t(b)][i] = rgba.get()[i * 4 + size_t(b)];
    return image;
}

}  // namespace bm3dtest
