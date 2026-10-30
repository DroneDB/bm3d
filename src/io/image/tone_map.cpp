// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tone_map.h"

#include <cmath>
#include <cstring>

namespace bm3d::detail {

namespace {

constexpr size_t kBins = 65536;

//The 16 high bits of the float, made to sort as the values do: a positive value has the sign bit
//set and keeps the rest, a negative one has every bit flipped, so that the larger its magnitude the
//lower its bin.
uint16_t binOf(float value) {
	uint32_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	const uint16_t high = uint16_t(bits >> 16);
	return (high & 0x8000u) ? uint16_t(~high) : uint16_t(high | 0x8000u);
}

float valueOf(uint16_t bin) {
	const uint16_t high = (bin & 0x8000u) ? uint16_t(bin & 0x7fffu) : uint16_t(~bin);
	const uint32_t bits = uint32_t(high) << 16;
	float value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

} //namespace

SampleHistogram::SampleHistogram(): bins_(kBins, 0) {}

void SampleHistogram::add(float value) {
	if(value == 0 || !std::isfinite(value))
		return;
	bins_[binOf(value)]++;
	count_++;
}

void SampleHistogram::add(const SampleHistogram &other) {
	for(size_t i = 0; i < kBins; i++)
		bins_[i] += other.bins_[i];
	count_ += other.count_;
	addFullScale(other.fullScale_);
}

void SampleHistogram::addFullScale(float fullScale) {
	if(fullScale > fullScale_)
		fullScale_ = fullScale;
}

float SampleHistogram::quantile(uint64_t numerator, uint64_t denominator) const {
	if(count_ == 0 || denominator == 0)
		return 0;
	//in integers, so that the bin is the same on every platform: the rank is count * numerator /
	//denominator rounded down, worked out without the product, which could overflow
	const uint64_t rank = count_ / denominator * numerator + (count_ % denominator) * numerator / denominator;
	uint64_t counted = 0;
	for(size_t i = 0; i < kBins; i++) {
		counted += bins_[i];
		if(counted && counted >= rank)
			return valueOf(uint16_t(i));
	}
	return valueOf(uint16_t(kBins - 1));
}

ToneMap::ToneMap(): ToneMap(0, 65535) {}

ToneMap::ToneMap(double low, double high): low_(low), high_(high > low ? high : low + 1),
	scale_(255.0 / (high_ - low_)) {}

ToneMap ToneMap::stretch(const SampleHistogram &samples) {
	const double low = samples.quantile(2, 100);
	const double high = samples.quantile(98, 100);
	if(high > low)
		return ToneMap(low, high);
	return ToneMap(0, samples.fullScale() > 0 ? samples.fullScale() : 65535);
}

uint8_t ToneMap::operator()(double value) const {
	if(!(value > low_))
		return 0;
	if(value >= high_)
		return 255;
	return uint8_t(int((value - low_) * scale_ + 0.5));
}

} //namespace
