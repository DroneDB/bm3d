// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BM3D_IMAGE_DECODER_H
#define BM3D_IMAGE_DECODER_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "image.h"
#include "tone_map.h"

namespace bm3d::detail {

//Decodes one family of image formats from data in memory. The alpha channel of an image is
//dropped and its colors kept as they are stored (a pixel that is transparent keeps the color it has
//in the file).
//
//A decoder that reads samples wider than 8 bits as data (the 16 bit and float samples of a TIFF,
//which hold reflectance or radiance) maps them to 8 bits with a ToneMap: decode() with a stretch
//of the samples of the image, decodeToneMapped() with the one it is given, which the builder makes
//from every texture of a model so that the pieces of a texture atlas get the same brightness.
class ImageDecoder {
public:
	virtual ~ImageDecoder() {}

	//Whether the data starts the way this decoder's formats do. Only the first bytes are looked at.
	virtual bool matches(const unsigned char *data, size_t size) const = 0;
	//Decodes data into image. False, and the reason in error (UTF-8), when it cannot be decoded;
	//throws std::bad_alloc when there is no memory for the pixels.
	virtual bool decode(const unsigned char *data, size_t size, Image &image, std::string &error) const = 0;

	//Adds the samples of data that are wider than 8 bits to samples. False, and nothing added, when
	//the image has none or cannot be read (decoding it says why); the decoders of the formats that
	//have none keep this default.
	virtual bool addWideSamples(const unsigned char *data, size_t size, SampleHistogram &samples) const;
	//decode(), with the samples wider than 8 bits mapped by toneMap; the default is decode().
	virtual bool decodeToneMapped(const unsigned char *data, size_t size, const ToneMap &toneMap, Image &image,
	                              std::string &error) const;
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
	//The same, with the samples wider than 8 bits mapped by toneMap
	bool decode(const unsigned char *data, size_t size, const ToneMap &toneMap, Image &image, std::string &error) const;
	//ImageDecoder::addWideSamples of the decoder that matches data; false when none does
	bool addWideSamples(const unsigned char *data, size_t size, SampleHistogram &samples) const;

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
//The same, with the samples wider than 8 bits mapped by toneMap
Image loadImage(const std::string &path, const ToneMap &toneMap, std::string &error,
                const ImageDecoderRegistry &registry = ImageDecoderRegistry::standard());
//Adds the samples of the texture file that are wider than 8 bits to samples (see
//ImageDecoder::addWideSamples). False when it has none or the file cannot be read: loading it then
//says why.
bool addWideSamples(const std::string &path, SampleHistogram &samples,
                    const ImageDecoderRegistry &registry = ImageDecoderRegistry::standard());

} //namespace

#endif // BM3D_IMAGE_DECODER_H
