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
#include "webp_codec.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include <webp/decode.h>
#include <webp/encode.h>

#include "../../common/error.h"

namespace bm3d::detail {

namespace {


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

} //namespace

bool WebpDecoder::matches(const unsigned char *data, size_t size) const {
	return size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0;
}

bool WebpDecoder::decode(const unsigned char *data, size_t size, Image &image, std::string &error) const {
	return readWebp(data, size, image, error);
}

std::vector<unsigned char> WebpEncoder::encode(const Image &image) const {
	if(image.isNull())
		throw Error("there is no image to encode");
	WebPConfig config;
	if(!WebPConfigPreset(&config, WEBP_PRESET_PHOTO, float(std::min(100, std::max(0, quality_)))))
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
