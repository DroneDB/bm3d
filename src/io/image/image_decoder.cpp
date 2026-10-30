// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "image_decoder.h"

#include <new>

#include "jpeg_codec.h"
#include "stb_decoder.h"
#include "tiff_decoder.h"
#include "webp_codec.h"
#include "../../common/error.h"
#include "../../common/mappedfile.h"

namespace bm3d::detail {

bool ImageDecoder::addWideSamples(const unsigned char *, size_t, SampleHistogram &) const {
	return false;
}

bool ImageDecoder::decodeToneMapped(const unsigned char *data, size_t size, const ToneMap &, Image &image,
                                    std::string &error) const {
	return decode(data, size, image, error);
}

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

bool ImageDecoderRegistry::decode(const unsigned char *data, size_t size, const ToneMap &toneMap, Image &image,
                                  std::string &error) const {
	const ImageDecoder *decoder = find(data, size);
	if(!decoder) {
		error = "unknown image format";
		return false;
	}
	return decoder->decodeToneMapped(data, size, toneMap, image, error);
}

bool ImageDecoderRegistry::addWideSamples(const unsigned char *data, size_t size, SampleHistogram &samples) const {
	const ImageDecoder *decoder = find(data, size);
	return decoder && decoder->addWideSamples(data, size, samples);
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

namespace {

//Maps the file and gives its bytes to read(data, size): false, and the reason in error, when the
//file cannot be opened, mapped or is empty, and what read returns otherwise. Running out of memory
//and the errors of the scratch and file layers are reasons too.
template<class Read>
bool readFile(const std::string &path, std::string &error, Read &&read) {
	MappedFile file;
	try {
		file.openReadOnly(path);
		if(file.size() == 0) {
			error = "the file is empty";
			return false;
		}
		if(file.size() > size_t(-1) / 2) {
			error = "the file is too large";
			return false;
		}
		const unsigned char *data = file.map(0, file.size());
		if(!data) {
			error = "could not read the file: " + file.lastError();
			return false;
		}
		return read(data, size_t(file.size()));
	} catch(const std::bad_alloc &) {
		error = "not enough memory to load the image";
	} catch(const Error &e) {
		error = e.what();
	}
	return false;
}

} //namespace

Image loadImage(const std::string &path, std::string &error, const ImageDecoderRegistry &registry) {
	Image image;
	if(!readFile(path, error, [&](const unsigned char *data, size_t size) {
		   return registry.decode(data, size, image, error);
	   }))
		return Image();
	return image;
}

Image loadImage(const std::string &path, const ToneMap &toneMap, std::string &error,
                const ImageDecoderRegistry &registry) {
	Image image;
	if(!readFile(path, error, [&](const unsigned char *data, size_t size) {
		   return registry.decode(data, size, toneMap, image, error);
	   }))
		return Image();
	return image;
}

bool addWideSamples(const std::string &path, SampleHistogram &samples, const ImageDecoderRegistry &registry) {
	std::string ignored;
	return readFile(path, ignored, [&](const unsigned char *data, size_t size) {
		return registry.addWideSamples(data, size, samples);
	});
}

} //namespace
