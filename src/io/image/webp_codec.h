/*
Nexus

Copyright (C) 2026 Digipa

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License (http://www.gnu.org/licenses/gpl.txt)
for more details.
*/
#ifndef BM3D_WEBP_CODEC_H
#define BM3D_WEBP_CODEC_H

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
