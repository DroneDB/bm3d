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
#ifndef BM3D_STB_DECODER_H
#define BM3D_STB_DECODER_H

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
