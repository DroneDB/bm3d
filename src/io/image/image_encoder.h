// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

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
