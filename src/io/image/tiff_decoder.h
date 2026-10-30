// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BM3D_TIFF_DECODER_H
#define BM3D_TIFF_DECODER_H

#include <cstddef>
#include <string>

#include "image_decoder.h"

namespace bm3d::detail {

//TIFF files (libtiff). Gray and RGB images of 16 bit integers or 32 bit floats, and RGB images of
//fewer than three samples (read as gray), are read by their samples, the wide ones mapped to 8 bits
//with a ToneMap; the others through the RGBA interface of libtiff. When the program is built
//without BM3D_WITH_TIFF the decoder still recognises a TIFF, so as to refuse it by name: a texture
//in a format that is not read must not be reported as an unknown one.
class TiffDecoder: public ImageDecoder {
public:
	bool matches(const unsigned char *data, size_t size) const override;
	bool decode(const unsigned char *data, size_t size, Image &image, std::string &error) const override;
	bool addWideSamples(const unsigned char *data, size_t size, SampleHistogram &samples) const override;
	bool decodeToneMapped(const unsigned char *data, size_t size, const ToneMap &toneMap, Image &image,
	                      std::string &error) const override;
};

} //namespace

#endif // BM3D_TIFF_DECODER_H
