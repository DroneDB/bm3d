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
#ifndef BM3D_IMAGE_ENCODER_H
#define BM3D_IMAGE_ENCODER_H

#include <vector>

#include "image.h"

namespace bm3d::detail {

//Encodes an image to the bytes of a file of one format. An encoder holds its settings and no state
//of its own: encode() can be called from several threads at once.
class ImageEncoder {
public:
	virtual ~ImageEncoder() {}

	//Throws Error when the image is null or the encoder fails
	virtual std::vector<unsigned char> encode(const Image &image) const = 0;
};

} //namespace

#endif // BM3D_IMAGE_ENCODER_H
