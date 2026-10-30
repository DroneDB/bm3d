// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pictures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

#include <jpeglib.h>
#include <png.h>
#ifdef BM3D_WITH_TIFF
#include <tiffio.h>
#endif
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

// libjpeg, not stb: the JPEG writer of stb shifts negative ints left, which C++17 leaves undefined.
// `samples` holds `components` bytes per pixel, rows from the top, in the color space `space` of
// the input (the file keeps it, or the YCbCr libjpeg chooses for RGB).
static void writeJpegSamples(const fs::path& file, int width, int height, int components, J_COLOR_SPACE space,
                             const std::vector<uint8_t>& samples, int quality) {
    if (samples.size() != size_t(width) * size_t(height) * size_t(components))
        throw std::runtime_error("the raster holds the wrong number of samples");
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
    info.image_width = JDIMENSION(width);
    info.image_height = JDIMENSION(height);
    info.input_components = components;
    info.in_color_space = space;
    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, quality, TRUE);
    jpeg_start_compress(&info, TRUE);
    const size_t stride = size_t(width) * size_t(components);
    while (info.next_scanline < info.image_height) {
        // jpeg_write_scanlines takes rows that are not const and only reads them
        JSAMPROW row = const_cast<JSAMPROW>(samples.data() + size_t(info.next_scanline) * stride);
        jpeg_write_scanlines(&info, &row, 1);
    }
    jpeg_finish_compress(&info);
    jpeg_destroy_compress(&info);
    std::unique_ptr<unsigned char, decltype(&std::free)> owner(encoded, &std::free);
    writeBytes(file, encoded, size_t(size));
}

void writeJpeg(const fs::path& file, const Raster& raster) {
    requireChannels(raster, 3, "JPEG");
    writeJpegSamples(file, raster.width, raster.height, 3, JCS_RGB, raster.pixels, 90);
}

void writeCmykJpeg(const fs::path& file, int width, int height, const std::vector<uint8_t>& samples) {
    // libjpeg marks a 4 component JPEG as Adobe's, whose CMYK is stored inverted: the samples go
    // in as given
    writeJpegSamples(file, width, height, 4, JCS_CMYK, samples, 100);
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

#ifdef BM3D_WITH_TIFF
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

namespace {

// One sample in the bytes of the file, in the byte order of the machine (libtiff swaps them)
void putSample(const TiffSpec& spec, double value, uint8_t* at) {
    if (spec.floatingPoint) {
        const float f = float(value);
        std::memcpy(at, &f, sizeof(f));
    } else if (spec.bitsPerSample == 16) {
        const uint16_t v = uint16_t(std::lround(value));
        std::memcpy(at, &v, sizeof(v));
    } else {
        *at = uint8_t(std::lround(value));
    }
}

}  // namespace

void writeRawTiff(const fs::path& file, const TiffSpec& spec, const std::vector<double>& samples) {
    const size_t perPixel = size_t(spec.samplesPerPixel);
    if (samples.size() != size_t(spec.width) * size_t(spec.height) * perPixel)
        throw std::runtime_error("writeRawTiff takes samplesPerPixel samples per pixel");
#ifdef _WIN32
    TIFF* tiff = TIFFOpenW(file.c_str(), "w");
#else
    TIFF* tiff = TIFFOpen(file.c_str(), "w");
#endif
    if (tiff == nullptr)
        throw std::runtime_error("cannot write " + pathToUtf8(file));
    std::unique_ptr<TIFF, decltype(&TIFFClose)> closer(tiff, &TIFFClose);

    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, uint32_t(spec.width));
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, uint32_t(spec.height));
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, uint16_t(spec.samplesPerPixel));
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, uint16_t(spec.bitsPerSample));
    TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, uint16_t(spec.floatingPoint ? SAMPLEFORMAT_IEEEFP : SAMPLEFORMAT_UINT));
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, uint16_t(spec.separatePlanes ? PLANARCONFIG_SEPARATE : PLANARCONFIG_CONTIG));
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, uint16_t(spec.photometric));
    TIFFSetField(tiff, TIFFTAG_COMPRESSION, uint16_t(spec.deflate ? COMPRESSION_ADOBE_DEFLATE : COMPRESSION_NONE));
    if (spec.samplesPerPixel == 2 || spec.samplesPerPixel == 4) {
        const uint16_t alpha = EXTRASAMPLE_UNASSALPHA;
        TIFFSetField(tiff, TIFFTAG_EXTRASAMPLES, uint16_t(1), &alpha);
    }

    // A chunk (a strip or a tile) of one plane, from its top left pixel, `width` pixels a row; what
    // falls outside the image is 0
    const size_t bytes = size_t(spec.bitsPerSample / 8);
    const size_t chunkPerPixel = spec.separatePlanes ? 1 : perPixel;
    auto chunk = [&](int x0, int y0, int width, int height, int plane) {
        std::vector<uint8_t> data(size_t(width) * size_t(height) * chunkPerPixel * bytes, 0);
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                for (size_t s = 0; s < chunkPerPixel; s++) {
                    if (x0 + x >= spec.width || y0 + y >= spec.height)
                        continue;
                    const size_t sample = spec.separatePlanes ? size_t(plane) : s;
                    const size_t from = (size_t(y0 + y) * size_t(spec.width) + size_t(x0 + x)) * perPixel + sample;
                    putSample(spec, samples[from], &data[((size_t(y) * size_t(width) + size_t(x)) * chunkPerPixel + s) * bytes]);
                }
        return data;
    };
    const int planes = spec.separatePlanes ? spec.samplesPerPixel : 1;
    bool ok = true;
    if (spec.tileSide > 0) {
        TIFFSetField(tiff, TIFFTAG_TILEWIDTH, uint32_t(spec.tileSide));
        TIFFSetField(tiff, TIFFTAG_TILELENGTH, uint32_t(spec.tileSide));
        for (int plane = 0; plane < planes; plane++)
            for (int y = 0; y < spec.height; y += spec.tileSide)
                for (int x = 0; x < spec.width; x += spec.tileSide) {
                    std::vector<uint8_t> data = chunk(x, y, spec.tileSide, spec.tileSide, plane);
                    ok = ok && TIFFWriteEncodedTile(tiff, TIFFComputeTile(tiff, uint32_t(x), uint32_t(y), 0, uint16_t(plane)),
                                                    data.data(), tmsize_t(data.size())) >= 0;
                }
    } else {
        const int rows = spec.rowsPerStrip > 0 ? spec.rowsPerStrip : spec.height;
        if (spec.rowsPerStrip > 0)
            TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, uint32_t(rows));
        for (int plane = 0; plane < planes; plane++)
            for (int y = 0; y < spec.height; y += rows) {
                std::vector<uint8_t> data = chunk(0, y, spec.width, std::min(rows, spec.height - y), plane);
                ok = ok && TIFFWriteEncodedStrip(tiff, TIFFComputeStrip(tiff, uint32_t(y), uint16_t(plane)), data.data(),
                                                 tmsize_t(data.size())) >= 0;
            }
    }
    if (!ok)
        throw std::runtime_error("cannot write " + pathToUtf8(file));
}
#endif

void writeWebp(const fs::path& file, const Raster& raster) {
    requireChannels(raster, 3, "WebP");
    uint8_t* encoded = nullptr;
    const size_t size = WebPEncodeLosslessRGB(raster.pixels.data(), raster.width, raster.height, raster.width * 3, &encoded);
    if (size == 0 || encoded == nullptr)
        throw std::runtime_error("cannot encode " + pathToUtf8(file));
    std::unique_ptr<uint8_t, decltype(&WebPFree)> owner(encoded, &WebPFree);
    writeBytes(file, encoded, size);
}

std::vector<uint8_t> packSamples(int width, int height, int bitDepth, const std::vector<uint8_t>& samples) {
    if (bitDepth != 1 && bitDepth != 2 && bitDepth != 4 && bitDepth != 8)
        throw std::runtime_error("packSamples takes 1, 2, 4 or 8 bits");
    if (samples.size() != size_t(width) * size_t(height))
        throw std::runtime_error("one sample per pixel is needed");
    const size_t perByte = size_t(8 / bitDepth);
    const size_t rowBytes = (size_t(width) + perByte - 1) / perByte;
    std::vector<uint8_t> rows(rowBytes * size_t(height), 0);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            const unsigned sample = samples[size_t(y) * size_t(width) + size_t(x)] & ((1u << bitDepth) - 1);
            // the first pixel of a byte is in its high bits
            const size_t shift = (perByte - 1 - size_t(x) % perByte) * size_t(bitDepth);
            rows[size_t(y) * rowBytes + size_t(x) / perByte] |= uint8_t(sample << shift);
        }
    return rows;
}

void writeRawPng(const fs::path& file, const PngSpec& spec, const std::vector<uint8_t>& rows) {
    const size_t channels = spec.colorType == PngSpec::Rgb ? 3 : 1;
    const size_t rowBytes = (size_t(spec.width) * channels * size_t(spec.bitDepth) + 7) / 8;
    if (rows.size() != rowBytes * size_t(spec.height))
        throw std::runtime_error("the raster holds the wrong number of samples");

    // Made before the jump target below: a jump back does not run the destructors of what was
    // made after it
    std::vector<uint8_t> bytes;
    std::vector<png_color> palette;
    for (const auto& color : spec.palette)
        palette.push_back({color[0], color[1], color[2]});
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
    png_set_IHDR(png, info, uint32_t(spec.width), uint32_t(spec.height), spec.bitDepth, int(spec.colorType),
                 spec.interlaced ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    if (!palette.empty())
        png_set_PLTE(png, info, palette.data(), int(palette.size()));
    if (!spec.paletteAlpha.empty())
        png_set_tRNS(png, info, spec.paletteAlpha.data(), int(spec.paletteAlpha.size()), nullptr);
    if (spec.transparentGray >= 0) {
        png_color_16 key{};
        key.gray = png_uint_16(spec.transparentGray);
        png_set_tRNS(png, info, nullptr, 0, &key);
    }
    png_write_info(png, info);
    // an interlaced image is written once for each pass, with the same rows
    const int passes = spec.interlaced ? png_set_interlace_handling(png) : 1;
    for (int pass = 0; pass < passes; pass++)
        for (int y = 0; y < spec.height; y++)
            png_write_row(png, const_cast<png_bytep>(rows.data() + size_t(y) * rowBytes));
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    writeBytes(file, bytes.data(), bytes.size());
}

void writePalettedPng(const fs::path& file, int width, int height, const std::vector<uint8_t>& indices) {
    PngSpec spec;
    spec.width = width;
    spec.height = height;
    spec.bitDepth = 4;
    spec.colorType = PngSpec::Palette;
    for (int i = 0; i < 16; i++) {
        std::array<uint8_t, 3> color;
        paletteColor(i, color.data());
        spec.palette.push_back(color);
    }
    writeRawPng(file, spec, packSamples(width, height, 4, indices));
}

void writeRgb16Png(const fs::path& file, int width, int height, const std::vector<uint16_t>& rgb) {
    if (rgb.size() != size_t(width) * size_t(height) * 3)
        throw std::runtime_error("the raster holds the wrong number of samples");
    std::vector<uint8_t> rows;
    rows.reserve(rgb.size() * 2);
    for (uint16_t sample : rgb) {  // PNG stores the high byte of a sample first
        rows.push_back(uint8_t(sample >> 8));
        rows.push_back(uint8_t(sample & 0xff));
    }
    PngSpec spec;
    spec.width = width;
    spec.height = height;
    spec.bitDepth = 16;
    spec.colorType = PngSpec::Rgb;
    writeRawPng(file, spec, rows);
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
