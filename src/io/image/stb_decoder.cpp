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
#include "stb_decoder.h"

#include <cstring>

//stb decodes PNG and the formats that are too small to need a library of their own (and no
//others). It decoded the PNG textures of the benchmark model 2% faster than libpng and gave the
//same file; JPEG stays with libjpeg-turbo, whose output stb's does not reproduce.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#include <stb_image.h>

namespace bm3d::detail {

namespace {


const char PngSignature[] = "\x89PNG\r\n\x1a\n";

//A sample of 16 bits is scaled to 8 with rounding, as libpng does (stb would drop the low byte)
inline unsigned toByte(unsigned char sample) { return sample; }
inline unsigned toByte(unsigned short sample) { return (unsigned(sample) + 128) / 257; }

//A gray PNG of 2 to 8 bits with a transparent color: the pixels of that gray have always been
//black in the textures (the palette Qt made of the image had black for its transparent entry);
//the other images of the kind (palette, RGB, 16 bit, ...) keep their colors. stb gives the pixels
//of the transparent gray an alpha of 0, which is what tells them here.
bool isGrayPngOfFewBits(const unsigned char *data, size_t size) {
	//the header chunk comes first: width, height, bit depth, color type at 16, 20, 24, 25
	return size >= 26 && std::memcmp(data, PngSignature, 8) == 0 && data[25] == 0 && data[24] >= 2 && data[24] <= 8;
}

//The pixels of RGBA samples, rows from the top; the alpha is dropped
template <typename Sample>
void copyPixels(const Sample *from, Image &image, bool transparentIsBlack) {
	for(int y = 0; y < image.height(); y++) {
		uint32_t *pixels = image.scanLine(y);
		for(int x = 0; x < image.width(); x++, from += 4)
			pixels[x] = (transparentIsBlack && from[3] == 0) ? Image::rgb(0, 0, 0)
			                                                  : Image::rgb(toByte(from[0]), toByte(from[1]), toByte(from[2]));
	}
}

//Takes over the samples stb allocated and frees them
template <typename Sample>
void makeImage(Sample *rgba, int width, int height, bool transparentIsBlack, Image &image) {
	try {
		image = Image(width, height);
	} catch(...) {
		stbi_image_free(rgba);
		throw;
	}
	copyPixels(rgba, image, transparentIsBlack);
	stbi_image_free(rgba);
}

bool readStb(const unsigned char *data, size_t size, Image &image, std::string &error) {
	if(size > 0x7fffffffu) {
		error = "the file is too large";
		return false;
	}
	int width = 0, height = 0, components = 0;
	if(stbi_is_16_bit_from_memory(data, int(size))) {
		stbi_us *rgba = stbi_load_16_from_memory(data, int(size), &width, &height, &components, 4);
		if(rgba) {
			makeImage(rgba, width, height, false, image);
			return true;
		}
	} else {
		stbi_uc *rgba = stbi_load_from_memory(data, int(size), &width, &height, &components, 4);
		if(rgba) {
			makeImage(rgba, width, height, isGrayPngOfFewBits(data, size), image);
			return true;
		}
	}
	const char *reason = stbi_failure_reason();
	error = reason ? reason : "unknown image format";
	return false;
}

} //namespace

bool StbDecoder::matches(const unsigned char *, size_t) const {
	return true;
}

bool StbDecoder::decode(const unsigned char *data, size_t size, Image &image, std::string &error) const {
	return readStb(data, size, image, error);
}

} //namespace
