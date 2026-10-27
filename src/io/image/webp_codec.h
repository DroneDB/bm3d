// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BM3D_WEBP_CODEC_H
#define BM3D_WEBP_CODEC_H

#include <cstddef>
#include <string>
#include <vector>

#include "image_decoder.h"
#include "image_encoder.h"

namespace bm3d::detail {

//WebP files (libwebp), lossy or lossless; an animated one is refused
class WebpDecoder: public ImageDecoder {
public:
	bool matches(const unsigned char *data, size_t size) const override;
	bool decode(const unsigned char *data, size_t size, Image &image, std::string &error) const override;
};

//The image as a lossy WebP file, without alpha, at encoder effort 2 (of 0..6)
class WebpEncoder: public ImageEncoder {
public:
	//quality 0..100 (a value outside is brought into it)
	explicit WebpEncoder(int quality): quality_(quality) {}
	std::vector<unsigned char> encode(const Image &image) const override;

private:
	int quality_;
};

} //namespace

#endif // BM3D_WEBP_CODEC_H
