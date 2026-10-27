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
#include "tiff_decoder.h"

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>

#ifdef BM3D_WITH_TIFF
#include <tiffio.h>
#endif

namespace bm3d::detail {

namespace {

#ifdef BM3D_WITH_TIFF

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

#else

//A program built with BM3D_WITH_TIFF=OFF does not link libtiff: a TIFF texture is refused by name
//and not as "unknown image format", so that the way out (another format, another build) is clear.
bool readTiff(const unsigned char *, size_t, Image &, std::string &error) {
	error = "this bm3d was built without TIFF support (BM3D_WITH_TIFF=OFF): convert the TIFF texture to PNG or JPEG";
	return false;
}

#endif

bool startsWith(const unsigned char *data, size_t size, const char *magic, size_t length) {
	return size >= length && std::memcmp(data, magic, length) == 0;
}

} //namespace

bool TiffDecoder::matches(const unsigned char *data, size_t size) const {
	return startsWith(data, size, "II*\0", 4) || startsWith(data, size, "MM\0*", 4) ||
	       startsWith(data, size, "II+\0", 4) || startsWith(data, size, "MM\0+", 4);
}

bool TiffDecoder::decode(const unsigned char *data, size_t size, Image &image, std::string &error) const {
	return readTiff(data, size, image, error);
}

} //namespace
