// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BM3D_JPEG_CODEC_H
#define BM3D_JPEG_CODEC_H

#include "image_decoder.h"
#include "image_encoder.h"

namespace bm3d::detail {

//JPEG files (libjpeg-turbo): any color space, CMYK included, decoded to opaque RGB
class JpegDecoder: public ImageDecoder {
public:
	bool matches(const unsigned char *data, size_t size) const override;
	bool decode(const unsigned char *data, size_t size, Image &image, std::string &error) const override;
};

//The image as a JPEG file: 4:2:0 chroma, 100 dpi. optimize chooses the Huffman tables for the
//image and progressive writes it in several scans.
class JpegEncoder: public ImageEncoder {
public:
	//quality 0..100 (a negative one is 75)
	explicit JpegEncoder(int quality, bool optimize = false, bool progressive = false):
		quality_(quality), optimize_(optimize), progressive_(progressive) {}
	std::vector<unsigned char> encode(const Image &image) const override;

private:
	int quality_;
	bool optimize_;
	bool progressive_;
};

} //namespace

#endif // BM3D_JPEG_CODEC_H
