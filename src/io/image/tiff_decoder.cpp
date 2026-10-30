// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tiff_decoder.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>

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

//A TIFF opened from memory, closed when this goes
class OpenTiff {
public:
	OpenTiff(const unsigned char *data, size_t size) {
		reader_.data = data;
		reader_.size = size;
		TIFFOpenOptions *options = TIFFOpenOptionsAlloc();
		if(!options) {
			reader_.error = "no memory for the TIFF decoder";
			return;
		}
		TIFFOpenOptionsSetErrorHandlerExtR(options, tiffMessage, &reader_);
		TIFFOpenOptionsSetWarningHandlerExtR(options, tiffIgnore, &reader_);
		tiff_ = TIFFClientOpenExt("texture", "rm", &reader_, tiffRead, tiffWrite, tiffSeek, tiffClose, tiffSize,
		                          tiffMap, tiffUnmap, options);
		TIFFOpenOptionsFree(options);
		if(!tiff_ && reader_.error.empty())
			reader_.error = "not a TIFF image";
	}
	~OpenTiff() {
		if(tiff_)
			TIFFClose(tiff_);
	}
	OpenTiff(const OpenTiff &) = delete;
	OpenTiff &operator=(const OpenTiff &) = delete;

	TIFF *get() const { return tiff_; }
	//the first error libtiff gave, or otherwise
	std::string error(const char *otherwise) const { return reader_.error.empty() ? otherwise : reader_.error; }

private:
	TiffReader reader_;
	TIFF *tiff_ = nullptr;
};

bool imageSize(TIFF *tiff, uint32_t &width, uint32_t &height, std::string &error) {
	if(!TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width) || !TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height) ||
	   width == 0 || height == 0 || width > 0x7fffffffu || height > 0x7fffffffu) {
		error = "the TIFF image has no size";
		return false;
	}
	return true;
}

//The TIFFs whose samples are read here and not through the RGBA interface of libtiff: samples wider
//than 8 bits (16 bit integers, 32 bit floats), which the interface cuts to their high byte or
//refuses, and an RGB image of fewer than three samples, which it refuses (the one band textures of
//the multispectral models of OpenDroneMap are written so). Every other TIFF goes through the
//interface, which also reads palettes, YCbCr, CMYK and samples of fewer than 8 bits.
struct SampleLayout {
	uint32_t width = 0;
	uint32_t height = 0;
	uint16_t bits = 0;
	uint16_t samplesPerPixel = 0;
	bool floatingPoint = false;
	bool separatePlanes = false;
	bool minIsWhite = false;
	//1 gray (the first sample), 3 RGB (the first three); the samples after them are not read
	uint16_t colors = 1;

	bool wide() const { return bits > 8; }
	//a float holds a fraction (a reflectance), an integer the range of its type
	float fullScale() const { return floatingPoint ? 1.0f : 65535.0f; }
	size_t bytesPerSample() const { return bits / 8; }
	double sample(const unsigned char *at) const {
		if(bits == 8)
			return *at;
		if(bits == 16) {
			uint16_t value;
			std::memcpy(&value, at, sizeof(value));
			return value;
		}
		float value;
		std::memcpy(&value, at, sizeof(value));
		return value;
	}
};

//Whether the samples of the TIFF are read here (see SampleLayout), and how
bool sampleLayout(TIFF *tiff, uint32_t width, uint32_t height, SampleLayout &layout) {
	uint16_t bits = 0, samples = 0, format = 0, planar = 0, photometric = 0;
	if(!TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric))
		return false;
	TIFFGetFieldDefaulted(tiff, TIFFTAG_BITSPERSAMPLE, &bits);
	TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples);
	TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLEFORMAT, &format);
	TIFFGetFieldDefaulted(tiff, TIFFTAG_PLANARCONFIG, &planar);
	const bool rgb = photometric == PHOTOMETRIC_RGB;
	if(!rgb && photometric != PHOTOMETRIC_MINISBLACK && photometric != PHOTOMETRIC_MINISWHITE)
		return false;
	if(samples == 0 || (planar != PLANARCONFIG_CONTIG && planar != PLANARCONFIG_SEPARATE))
		return false;
	const bool integer = format == SAMPLEFORMAT_UINT && (bits == 8 || bits == 16);
	const bool floatingPoint = format == SAMPLEFORMAT_IEEEFP && bits == 32;
	if(!integer && !floatingPoint)
		return false;
	if(bits == 8 && !(rgb && samples < 3))
		return false;
	layout.width = width;
	layout.height = height;
	layout.bits = bits;
	layout.samplesPerPixel = samples;
	layout.floatingPoint = floatingPoint;
	layout.separatePlanes = planar == PLANARCONFIG_SEPARATE && samples > 1;
	layout.minIsWhite = photometric == PHOTOMETRIC_MINISWHITE;
	layout.colors = rgb && samples >= 3 ? 3 : 1;
	return true;
}

//Calls visit(x, y, color, value) for each color sample of each pixel, rows from the top as they are
//stored (the orientation the file declares is kept, as the RGBA interface keeps it below). Reads the
//strips or the tiles of the image, one at a time. False, and the reason in error, when one cannot
//be read.
template<class Visit>
bool forEachSample(const OpenTiff &file, const SampleLayout &layout, std::string &error, Visit &&visit) {
	TIFF *tiff = file.get();
	const bool tiled = TIFFIsTiled(tiff) != 0;
	uint32_t chunkWidth = layout.width, chunkHeight = 0;
	if(tiled) {
		if(!TIFFGetField(tiff, TIFFTAG_TILEWIDTH, &chunkWidth) || !TIFFGetField(tiff, TIFFTAG_TILELENGTH, &chunkHeight))
			chunkWidth = chunkHeight = 0;
	} else {
		TIFFGetFieldDefaulted(tiff, TIFFTAG_ROWSPERSTRIP, &chunkHeight);
		if(chunkHeight > layout.height)
			chunkHeight = layout.height;
	}
	const tmsize_t chunkSize = tiled ? TIFFTileSize(tiff) : TIFFStripSize(tiff);
	if(chunkWidth == 0 || chunkHeight == 0 || chunkSize <= 0) {
		error = file.error("the TIFF image has no strips or tiles");
		return false;
	}
	const size_t bytes = layout.bytesPerSample();
	const size_t pixelBytes = layout.separatePlanes ? bytes : bytes * layout.samplesPerPixel;
	const size_t rowBytes = size_t(chunkWidth) * pixelBytes;
	const uint16_t perPlane = layout.separatePlanes ? 1 : layout.colors;
	const uint16_t planes = layout.separatePlanes ? layout.colors : 1;
	std::vector<unsigned char> chunk(static_cast<size_t>(chunkSize));

	for(uint16_t plane = 0; plane < planes; plane++) {
		for(uint32_t y0 = 0; y0 < layout.height; y0 += chunkHeight) {
			for(uint32_t x0 = 0; x0 < layout.width; x0 += chunkWidth) {
				const tmsize_t read =
					tiled ? TIFFReadEncodedTile(tiff, TIFFComputeTile(tiff, x0, y0, 0, plane), chunk.data(), chunkSize)
					      : TIFFReadEncodedStrip(tiff, TIFFComputeStrip(tiff, y0, plane), chunk.data(), chunkSize);
				const uint32_t rows = std::min(chunkHeight, layout.height - y0);
				const uint32_t columns = std::min(chunkWidth, layout.width - x0);
				if(read < 0 || size_t(read) < (rows - 1) * rowBytes + columns * pixelBytes) {
					error = file.error("could not read the TIFF image");
					return false;
				}
				for(uint32_t row = 0; row < rows; row++) {
					const unsigned char *pixel = chunk.data() + row * rowBytes;
					for(uint32_t column = 0; column < columns; column++, pixel += pixelBytes)
						for(uint16_t sample = 0; sample < perPlane; sample++)
							visit(x0 + column, y0 + row, layout.separatePlanes ? plane : sample,
							      layout.sample(pixel + sample * bytes));
				}
			}
		}
	}
	return true;
}

bool measureSamples(const OpenTiff &file, const SampleLayout &layout, SampleHistogram &samples, std::string &error) {
	samples.addFullScale(layout.fullScale());
	return forEachSample(file, layout, error, [&](uint32_t, uint32_t, uint16_t, double value) {
		samples.add(float(value));
	});
}

//The samples of the layout as the pixels of image: 8 bit samples as they are, wider ones through
//toneMap, or through a stretch of their own when it is null
bool readSamples(const OpenTiff &file, const SampleLayout &layout, const ToneMap *toneMap, Image &image,
                 std::string &error) {
	ToneMap own;
	if(layout.wide() && !toneMap) {
		SampleHistogram samples;
		if(!measureSamples(file, layout, samples, error))
			return false;
		own = ToneMap::stretch(samples);
		toneMap = &own;
	}
	image = Image(int(layout.width), int(layout.height));
	image.fill(Image::Opaque);
	const bool ok = forEachSample(file, layout, error, [&](uint32_t x, uint32_t y, uint16_t color, double value) {
		uint8_t level = layout.wide() ? (*toneMap)(value) : uint8_t(value);
		if(layout.minIsWhite)
			level = uint8_t(255 - level);
		uint32_t &pixel = image.scanLine(int(y))[x];
		if(layout.colors == 1) {
			pixel = Image::rgb(level, level, level);
		} else {
			const unsigned shift = 16u - 8u * color;
			pixel = (pixel & ~(0xffu << shift)) | (uint32_t(level) << shift);
		}
	});
	if(!ok)
		image = Image();
	return ok;
}

//The rest, through the RGBA interface of libtiff
bool readRgba(const OpenTiff &file, uint32_t width, uint32_t height, Image &image, std::string &error) {
	TIFF *tiff = file.get();
	char message[1024] = {0};
	if(!TIFFRGBAImageOK(tiff, message)) {
		error = message;
		return false;
	}
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
		error = file.error("could not read the TIFF image");
		image = Image();
		return false;
	}
	for(int y = 0; y < image.height(); y++) {
		uint32_t *pixels = image.scanLine(y);
		for(int x = 0; x < image.width(); x++)
			pixels[x] = tiffPixel(pixels[x], premultiplied);
	}
	return true;
}

bool readTiff(const unsigned char *data, size_t size, const ToneMap *toneMap, Image &image, std::string &error) {
	OpenTiff file(data, size);
	if(!file.get()) {
		error = file.error("not a TIFF image");
		return false;
	}
	uint32_t width = 0, height = 0;
	if(!imageSize(file.get(), width, height, error))
		return false;
	SampleLayout layout;
	if(sampleLayout(file.get(), width, height, layout))
		return readSamples(file, layout, toneMap, image, error);
	return readRgba(file, width, height, image, error);
}

bool addTiffSamples(const unsigned char *data, size_t size, SampleHistogram &samples) {
	OpenTiff file(data, size);
	std::string error;
	uint32_t width = 0, height = 0;
	SampleLayout layout;
	if(!file.get() || !imageSize(file.get(), width, height, error) || !sampleLayout(file.get(), width, height, layout) ||
	   !layout.wide())
		return false;
	//counted apart, so that a file that fails half way adds nothing
	SampleHistogram these;
	if(!measureSamples(file, layout, these, error))
		return false;
	samples.add(these);
	return true;
}

#else

//A program built with BM3D_WITH_TIFF=OFF does not link libtiff: a TIFF texture is refused by name
//and not as "unknown image format", so that the way out (another format, another build) is clear.
bool readTiff(const unsigned char *, size_t, const ToneMap *, Image &, std::string &error) {
	error = "this bm3d was built without TIFF support (BM3D_WITH_TIFF=OFF): convert the TIFF texture to PNG or JPEG";
	return false;
}

bool addTiffSamples(const unsigned char *, size_t, SampleHistogram &) {
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
	return readTiff(data, size, nullptr, image, error);
}

bool TiffDecoder::addWideSamples(const unsigned char *data, size_t size, SampleHistogram &samples) const {
	return addTiffSamples(data, size, samples);
}

bool TiffDecoder::decodeToneMapped(const unsigned char *data, size_t size, const ToneMap &toneMap, Image &image,
                                   std::string &error) const {
	return readTiff(data, size, &toneMap, image, error);
}

} //namespace
