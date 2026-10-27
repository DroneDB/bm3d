// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BM3D_STB_DECODER_H
#define BM3D_STB_DECODER_H

#include <cstddef>
#include <string>

#include "image_decoder.h"

namespace bm3d::detail {

//The formats that stb reads: PNG, BMP, GIF and TGA. They have no signature in common (a TGA has
//none), so this decoder matches every file and is the last of a registry.
class StbDecoder: public ImageDecoder {
public:
	bool matches(const unsigned char *data, size_t size) const override;
	bool decode(const unsigned char *data, size_t size, Image &image, std::string &error) const override;
};

} //namespace

#endif // BM3D_STB_DECODER_H
