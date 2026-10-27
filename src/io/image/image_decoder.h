// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BM3D_IMAGE_DECODER_H
#define BM3D_IMAGE_DECODER_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "image.h"

namespace bm3d::detail {

//Decodes one family of image formats from data in memory. The alpha channel of an image is
//dropped and its colors kept as they are stored (a pixel that is transparent keeps the color it has
//in the file).
class ImageDecoder {
public:
	virtual ~ImageDecoder() {}

	//Whether the data starts the way this decoder's formats do. Only the first bytes are looked at.
	virtual bool matches(const unsigned char *data, size_t size) const = 0;
	//Decodes data into image. False, and the reason in error (UTF-8), when it cannot be decoded;
	//throws std::bad_alloc when there is no memory for the pixels.
	virtual bool decode(const unsigned char *data, size_t size, Image &image, std::string &error) const = 0;
};

//The decoders of the texture formats, in the order they are asked: the first whose matches() holds
//reads the data, so the format of a file is told by its content and not by the extension.
class ImageDecoderRegistry {
public:
	void add(std::unique_ptr<ImageDecoder> decoder);

	//The decoder for data, or null when none matches
	const ImageDecoder *find(const unsigned char *data, size_t size) const;

	//Decodes data with the decoder that matches it; false, and the reason in error, when none does
	//or it fails.
	bool decode(const unsigned char *data, size_t size, Image &image, std::string &error) const;

	//JPEG, WebP, TIFF and, for what is left, the decoder of stb (PNG, BMP, GIF, TGA). A TIFF is
	//read only when the program is built with BM3D_WITH_TIFF (the default); without it the
	//file is refused with an error that says so.
	static const ImageDecoderRegistry &standard();

private:
	std::vector<std::unique_ptr<ImageDecoder>> decoders_;
};

//Reads a texture file with the decoders of `registry`. Returns a null image, and the reason in
//error (UTF-8), when the file cannot be opened or read as an image.
Image loadImage(const std::string &path, std::string &error,
                const ImageDecoderRegistry &registry = ImageDecoderRegistry::standard());

} //namespace

#endif // BM3D_IMAGE_DECODER_H
