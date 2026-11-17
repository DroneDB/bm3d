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
#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace bm3d::detail {

Image::Image(int width, int height) {
	if(width <= 0 || height <= 0)
		throw std::invalid_argument("an image needs a positive size");
	pixels_.reset(new uint32_t[size_t(width) * size_t(height)]);
	width_ = width;
	height_ = height;
}

void Image::fill(uint32_t pixel) {
	if(pixels_)
		std::fill_n(pixels_.get(), size_t(width_) * size_t(height_), pixel);
}

Image Image::copy(int x, int y, int width, int height) const {
	if(x < 0 || y < 0 || width <= 0 || height <= 0 || x + int64_t(width) > width_ || y + int64_t(height) > height_)
		throw std::out_of_range("the part of an image to copy lies outside of it");
	Image result(width, height);
	for(int row = 0; row < height; row++)
		std::memcpy(result.scanLine(row), scanLine(y + row) + x, size_t(width) * sizeof(uint32_t));
	return result;
}

Image Image::mirrored() const {
	if(isNull())
		return Image();
	Image result(width_, height_);
	for(int row = 0; row < height_; row++)
		std::memcpy(result.scanLine(height_ - 1 - row), scanLine(row), size_t(width_) * sizeof(uint32_t));
	return result;
}

void Image::draw(int x, int y, const Image &source, int sx, int sy, int width, int height) {
	if(isNull() || source.isNull())
		return;
	if(sx < 0 || sy < 0 || width < 0 || height < 0 ||
	   sx + int64_t(width) > source.width_ || sy + int64_t(height) > source.height_)
		throw std::out_of_range("the part of an image to draw lies outside of it");
	//clip against this image
	if(x < 0) { sx -= x; width += x; x = 0; }
	if(y < 0) { sy -= y; height += y; y = 0; }
	width = int(std::min<int64_t>(width, width_ - int64_t(x)));
	height = int(std::min<int64_t>(height, height_ - int64_t(y)));
	if(width <= 0 || height <= 0)
		return;
	for(int row = 0; row < height; row++)
		std::memmove(scanLine(y + row) + x, source.scanLine(sy + row) + sx, size_t(width) * sizeof(uint32_t));
}

namespace {

//The source pixels one target pixel of a box filtered resize covers along an axis, with the
//share of each one: target pixel i spans [i * ratio, (i + 1) * ratio) of the source.
struct BoxSpan {
	int first = 0;
	std::vector<float> weights;   //of source pixels first, first + 1, ... summing to 1
};

std::vector<BoxSpan> boxSpans(int source, int target) {
	std::vector<BoxSpan> spans(static_cast<size_t>(target));
	const double ratio = double(source) / double(target);
	for(int i = 0; i < target; i++) {
		const double start = i * ratio;
		const double end = std::min(double(source), (i + 1) * ratio);
		BoxSpan &span = spans[size_t(i)];
		span.first = int(std::floor(start));
		for(int s = span.first; s < source && s < end; s++) {
			const double covered = std::min(end, s + 1.0) - std::max(start, double(s));
			span.weights.push_back(float(covered / (end - start)));
		}
	}
	return spans;
}

//An average of a channel, rounded to a byte
unsigned channel(float v) {
	return unsigned(std::min(255.0f, std::max(0.0f, v + 0.5f)));
}

uint32_t packPixel(const float *rgb) {
	return Image::rgb(channel(rgb[0]), channel(rgb[1]), channel(rgb[2]));
}

//The box filter of an image to width x height. load(pixel, values) gives the Channels values a
//pixel adds to the average, store(values) the pixel of the averages.
template <int Channels, class Load, class Store>
Image boxFiltered(const Image &source, int width, int height, Load load, Store store) {
	const std::vector<BoxSpan> columns = boxSpans(source.width(), width);
	const std::vector<BoxSpan> rows = boxSpans(source.height(), height);
	Image result(width, height);
	std::vector<float> row(size_t(width) * Channels), sum(size_t(width) * Channels);
	for(int y = 0; y < height; y++) {
		std::fill(sum.begin(), sum.end(), 0.0f);
		const BoxSpan &rowSpan = rows[size_t(y)];
		for(size_t r = 0; r < rowSpan.weights.size(); r++) {
			//the source row, filtered across to the target width
			const uint32_t *from = source.scanLine(rowSpan.first + int(r));
			for(int x = 0; x < width; x++) {
				const BoxSpan &column = columns[size_t(x)];
				float filtered[Channels] = {};
				for(size_t c = 0; c < column.weights.size(); c++) {
					float values[Channels];
					load(from[column.first + int(c)], values);
					const float w = column.weights[c];
					for(int k = 0; k < Channels; k++)
						filtered[k] += w * values[k];
				}
				for(int k = 0; k < Channels; k++)
					row[size_t(x) * Channels + k] = filtered[k];
			}
			const float w = rowSpan.weights[r];
			for(size_t k = 0; k < sum.size(); k++)
				sum[k] += w * row[k];
		}
		uint32_t *to = result.scanLine(y);
		for(int x = 0; x < width; x++)
			to[x] = store(&sum[size_t(x) * Channels]);
	}
	return result;
}

//The red, green and blue of a pixel, as numbers
void loadColor(uint32_t p, float *rgb) {
	rgb[0] = float((p >> 16) & 0xff);
	rgb[1] = float((p >> 8) & 0xff);
	rgb[2] = float(p & 0xff);
}

//The averages of an image that is not opaque: the colors multiplied by the alpha, the alpha, and the
//colors as they are, for a pixel whose whole area is cut out (it has no drawn color to take)
const int WeightedChannels = 7;

void loadWeighted(uint32_t p, float *values) {
	loadColor(p, values + 4);
	const float a = float(Image::alpha(p));
	for(int k = 0; k < 3; k++)
		values[k] = values[4 + k] * a;
	values[3] = a;
}

uint32_t storeWeighted(const float *values) {
	const float a = values[3];
	if(a <= 0.0f)
		return Image::rgba(channel(values[4]), channel(values[5]), channel(values[6]), 0);
	return Image::rgba(channel(values[0] / a), channel(values[1] / a), channel(values[2] / a), channel(a));
}

} //namespace

Image Image::downscaled(int width, int height) const {
	if(isNull() || width <= 0 || height <= 0)
		return Image();
	if(width > width_ || height > height_)
		throw std::invalid_argument("an image is downscaled to a size larger than its own");
	if(width == width_ && height == height_)
		return *this;
	if(isOpaque())
		return boxFiltered<3>(*this, width, height, loadColor, packPixel);
	return boxFiltered<WeightedChannels>(*this, width, height, loadWeighted, storeWeighted);
}

bool Image::isOpaque() const {
	const size_t count = size_t(width_) * size_t(height_);
	for(size_t i = 0; i < count; i++)
		if(alpha(pixels_[i]) != 255)
			return false;
	return true;
}

void Image::setOpaque() {
	const size_t count = size_t(width_) * size_t(height_);
	for(size_t i = 0; i < count; i++)
		pixels_[i] |= Opaque;
}

void Image::setAlphaFrom(const Image &mask) {
	if(isNull() || mask.isNull())
		return;
	const bool fromAlpha = !mask.isOpaque();
	for(int y = 0; y < height_; y++) {
		const uint32_t *from = mask.scanLine(int(int64_t(y) * mask.height_ / height_));
		uint32_t *to = scanLine(y);
		for(int x = 0; x < width_; x++) {
			const uint32_t m = from[int64_t(x) * mask.width_ / width_];
			//the luminance of Rec. 601, in integers: the weights add up to 1000
			const unsigned value = fromAlpha ? alpha(m)
			                                 : (299 * ((m >> 16) & 0xff) + 587 * ((m >> 8) & 0xff) + 114 * (m & 0xff) + 500) / 1000;
			to[x] = (to[x] & 0x00ffffffu) | (uint32_t(value) << 24);
		}
	}
}

void Image::binarizeAlpha() {
	const size_t count = size_t(width_) * size_t(height_);
	for(size_t i = 0; i < count; i++)
		pixels_[i] = alpha(pixels_[i]) >= CutoutThreshold ? (pixels_[i] | Opaque) : (pixels_[i] & ~Opaque);
}

} //namespace
