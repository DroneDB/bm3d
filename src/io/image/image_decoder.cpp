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
#include "image_decoder.h"

#include <new>

#include "jpeg_codec.h"
#include "stb_decoder.h"
#include "tiff_decoder.h"
#include "webp_codec.h"
#include "../../common/error.h"
#include "../../common/mappedfile.h"

namespace bm3d::detail {

void ImageDecoderRegistry::add(std::unique_ptr<ImageDecoder> decoder) {
	decoders_.push_back(std::move(decoder));
}

const ImageDecoder *ImageDecoderRegistry::find(const unsigned char *data, size_t size) const {
	for(const auto &decoder: decoders_)
		if(decoder->matches(data, size))
			return decoder.get();
	return nullptr;
}

bool ImageDecoderRegistry::decode(const unsigned char *data, size_t size, Image &image, std::string &error) const {
	const ImageDecoder *decoder = find(data, size);
	if(!decoder) {
		error = "unknown image format";
		return false;
	}
	return decoder->decode(data, size, image, error);
}

const ImageDecoderRegistry &ImageDecoderRegistry::standard() {
	static const ImageDecoderRegistry registry = [] {
		ImageDecoderRegistry made;
		made.add(std::unique_ptr<ImageDecoder>(new JpegDecoder));
		made.add(std::unique_ptr<ImageDecoder>(new WebpDecoder));
		made.add(std::unique_ptr<ImageDecoder>(new TiffDecoder));
		//the formats stb reads have no signature in common (a TGA has none), so it takes the rest
		made.add(std::unique_ptr<ImageDecoder>(new StbDecoder));
		return made;
	}();
	return registry;
}

Image loadImage(const std::string &path, std::string &error, const ImageDecoderRegistry &registry) {
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
		if(!registry.decode(data, size_t(file.size()), image, error))
			return Image();
		return image;
	} catch(const std::bad_alloc &) {
		error = "not enough memory to load the image";
	} catch(const Error &e) {
		error = e.what();
	}
	return Image();
}

} //namespace
