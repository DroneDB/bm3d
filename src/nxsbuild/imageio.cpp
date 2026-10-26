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
#include "imageio.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include <tiffio.h>
#include <webp/decode.h>
#include <webp/encode.h>

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

#include "../common/error.h"
#include "../common/mappedfile.h"

namespace nx {

namespace {

// ---------------------------------------------------------------------------------------
// TIFF
// ---------------------------------------------------------------------------------------

struct TiffReader {
	const unsigned char *data = nullptr;
	uint64_t size = 0;
	uint64_t position = 0;
	std::string error;
};

tmsize_t tiffRead(thandle_t handle, void *where, tmsize_t length) {
	TiffReader *reader = static_cast<TiffReader *>(handle);
	if(length < 0 || reader->position > reader->size)
		return -1;
	const uint64_t available = reader->size - reader->position;
	const uint64_t count = uint64_t(length) < available ? uint64_t(length) : available;
	std::memcpy(where, reader->data + reader->position, size_t(count));
	reader->position += count;
	return tmsize_t(count);
}

tmsize_t tiffWrite(thandle_t, void *, tmsize_t) { return -1; }

toff_t tiffSeek(thandle_t handle, toff_t offset, int whence) {
	TiffReader *reader = static_cast<TiffReader *>(handle);
	uint64_t base = 0;
	if(whence == SEEK_CUR)
		base = reader->position;
	else if(whence == SEEK_END)
		base = reader->size;
	reader->position = base + offset;
	return reader->position;
}

int tiffClose(thandle_t) { return 0; }

toff_t tiffSize(thandle_t handle) { return static_cast<TiffReader *>(handle)->size; }

int tiffMap(thandle_t handle, void **base, toff_t *size) {
	TiffReader *reader = static_cast<TiffReader *>(handle);
	*base = const_cast<unsigned char *>(reader->data);
	*size = reader->size;
	return 1;
}

void tiffUnmap(thandle_t, void *, toff_t) {}

int tiffMessage(TIFF *, void *user, const char *module, const char *format, va_list arguments) {
	TiffReader *reader = static_cast<TiffReader *>(user);
	if(reader->error.empty()) {
		char text[512];
		vsnprintf(text, sizeof(text), format, arguments);
		reader->error = module ? std::string(module) + ": " + text : text;
	}
	return 1;
}

int tiffIgnore(TIFF *, void *, const char *, const char *, va_list) { return 1; }

//An RGBA raster, the way libtiff builds it: bytes R, G, B, A. When the file defines an alpha
//channel the colors come multiplied by it, and they are divided by it again here, with the
//integer reciprocal of the portable code path (Qt's SSE4 path rounds with an approximate float
//one, which depends on the processor).
uint32_t tiffPixel(uint32_t abgr, bool premultiplied) {
	unsigned r = TIFFGetR(abgr), g = TIFFGetG(abgr), b = TIFFGetB(abgr);
	const unsigned a = TIFFGetA(abgr);
	if(premultiplied && a != 255) {
		if(a == 0) {
			r = g = b = 0;
		} else {
			const unsigned inverse = ((255u << 16) + a / 2) / a;
			r = (r * inverse + 0x8000) >> 16;
			g = (g * inverse + 0x8000) >> 16;
			b = (b * inverse + 0x8000) >> 16;
		}
	}
	return Image::rgb(r, g, b);
}

bool readTiff(const unsigned char *data, size_t size, Image &image, std::string &error) {
	TiffReader reader;
	reader.data = data;
	reader.size = size;

	TIFFOpenOptions *options = TIFFOpenOptionsAlloc();
	if(!options) {
		error = "no memory for the TIFF decoder";
		return false;
	}
	TIFFOpenOptionsSetErrorHandlerExtR(options, tiffMessage, &reader);
	TIFFOpenOptionsSetWarningHandlerExtR(options, tiffIgnore, &reader);
	TIFF *tiff = TIFFClientOpenExt("texture", "rm", &reader, tiffRead, tiffWrite, tiffSeek, tiffClose, tiffSize,
	                               tiffMap, tiffUnmap, options);
	TIFFOpenOptionsFree(options);
	if(!tiff) {
		error = reader.error.empty() ? "not a TIFF image" : reader.error;
		return false;
	}

	bool ok = false;
	uint32_t width = 0, height = 0;
	char message[1024] = {0};
	if(!TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width) || !TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height) ||
	   width == 0 || height == 0 || width > 0x7fffffffu || height > 0x7fffffffu) {
		error = "the TIFF image has no size";
	} else if(!TIFFRGBAImageOK(tiff, message)) {
		error = message;
	} else {
		//the raster keeps the orientation the file declares: the pixels are not turned
		uint16_t orientation = ORIENTATION_TOPLEFT;
		if(!TIFFGetField(tiff, TIFFTAG_ORIENTATION, &orientation) || orientation < ORIENTATION_TOPLEFT ||
		   orientation > ORIENTATION_LEFTBOT)
			orientation = ORIENTATION_TOPLEFT;

		uint16_t extras = 0, *extraSamples = nullptr;
		const bool premultiplied = TIFFGetField(tiff, TIFFTAG_EXTRASAMPLES, &extras, &extraSamples) && extras &&
		                           extraSamples[0] != EXTRASAMPLE_UNSPECIFIED;

		image = Image(int(width), int(height));
		if(!TIFFReadRGBAImageOriented(tiff, width, height, reinterpret_cast<uint32_t *>(image.scanLine(0)),
		                              orientation, 1)) {
			error = reader.error.empty() ? "could not read the TIFF image" : reader.error;
			image = Image();
		} else {
			for(int y = 0; y < image.height(); y++) {
				uint32_t *pixels = image.scanLine(y);
				for(int x = 0; x < image.width(); x++)
					pixels[x] = tiffPixel(pixels[x], premultiplied);
			}
			ok = true;
		}
	}
	TIFFClose(tiff);
	return ok;
}

// ---------------------------------------------------------------------------------------
// WebP
// ---------------------------------------------------------------------------------------

bool readWebp(const unsigned char *data, size_t size, Image &image, std::string &error) {
	WebPBitstreamFeatures features;
	if(WebPGetFeatures(data, size, &features) != VP8_STATUS_OK) {
		error = "not a WebP image";
		return false;
	}
	if(features.has_animation) {
		error = "animated WebP images are not supported";
		return false;
	}
	image = Image(features.width, features.height);
	const size_t stride = size_t(features.width) * 4;
	//B, G, R, A in memory, as the pixels are, with the colors as stored
	uint8_t *decoded = WebPDecodeBGRAInto(data, size, reinterpret_cast<uint8_t *>(image.scanLine(0)),
	                                      stride * size_t(features.height), int(stride));
	if(!decoded) {
		error = "could not decode the WebP image";
		image = Image();
		return false;
	}
	if(features.has_alpha) {
		for(int y = 0; y < image.height(); y++) {
			uint32_t *pixels = image.scanLine(y);
			for(int x = 0; x < image.width(); x++)
				pixels[x] |= Image::Opaque;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------
// PNG, BMP, GIF, TGA
// ---------------------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------------------

bool startsWith(const unsigned char *data, size_t size, const char *magic, size_t length) {
	return size >= length && std::memcmp(data, magic, length) == 0;
}

bool decode(const unsigned char *data, size_t size, Image &image, std::string &error) {
	if(startsWith(data, size, "\xff\xd8\xff", 3)) {
		image = decodeJpeg(data, size, error);
		return !image.isNull();
	}
	if(size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0)
		return readWebp(data, size, image, error);
	if(startsWith(data, size, "II*\0", 4) || startsWith(data, size, "MM\0*", 4) ||
	   startsWith(data, size, "II+\0", 4) || startsWith(data, size, "MM\0+", 4))
		return readTiff(data, size, image, error);
	return readStb(data, size, image, error);
}

} //namespace

Image loadImage(const std::string &path, std::string &error) {
	MappedFile file;
	try {
		file.openReadOnly(path);
		if(file.size() == 0) {
			error = "the file is empty";
			return Image();
		}
		if(file.size() > size_t(-1) / 2) {
			error = "the file is too large";
			return Image();
		}
		const unsigned char *data = file.map(0, file.size());
		if(!data) {
			error = "could not read the file: " + file.lastError();
			return Image();
		}
		Image image;
		if(!decode(data, size_t(file.size()), image, error))
			return Image();
		return image;
	} catch(const std::bad_alloc &) {
		error = "not enough memory to load the image";
	} catch(const Error &e) {
		error = e.what();
	}
	return Image();
}

std::vector<unsigned char> encodeWebp(const Image &image, int quality) {
	if(image.isNull())
		throw Error("there is no image to encode");
	WebPConfig config;
	if(!WebPConfigPreset(&config, WEBP_PRESET_PHOTO, float(std::min(100, std::max(0, quality)))))
		throw Error("could not set up the WebP encoder");
	//effort 2 instead of the default 4: twice as fast on node textures, for 3% more bytes
	config.method = 2;

	WebPPicture picture;
	if(!WebPPictureInit(&picture))
		throw Error("could not set up the WebP encoder");
	picture.width = image.width();
	picture.height = image.height();
	picture.use_argb = 0;
	WebPMemoryWriter writer;
	WebPMemoryWriterInit(&writer);
	picture.writer = WebPMemoryWrite;
	picture.custom_ptr = &writer;

	//B, G, R, A in memory: the alpha byte is ignored, the pixels are opaque
	const bool ok = WebPPictureImportBGRX(&picture, reinterpret_cast<const uint8_t *>(image.scanLine(0)),
	                                      image.width() * 4) &&
	                WebPEncode(&config, &picture);
	const WebPEncodingError status = picture.error_code;
	WebPPictureFree(&picture);
	std::vector<unsigned char> webp;
	if(ok)
		webp.assign(writer.mem, writer.mem + writer.size);
	WebPMemoryWriterClear(&writer);
	if(!ok)
		throw Error("could not encode a WebP image (error " + std::to_string(int(status)) + ")");
	return webp;
}

} //namespace
