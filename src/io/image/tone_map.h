// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BM3D_TONE_MAP_H
#define BM3D_TONE_MAP_H

#include <cstdint>
#include <vector>

namespace bm3d::detail {

//The distribution of the samples of one or more images whose samples are wider than the 8 bits of
//a texture (16 bit integers, 32 bit floats): the textures of a multispectral or thermal model hold
//reflectance or radiance, which fills a small part of the range of the type, so that the samples
//cannot be scaled to 8 bits by the range of the type alone (a reflectance of 0.05 in a float, or
//of 15000 in 16 bits, is black or near black).
//
//A sample is counted in the bin of its value with the 16 high bits of its 32 bit float (the sign,
//the exponent and 7 bits of the mantissa), so the bins have the same relative width whatever the
//range and the scale of the values: a quantile is within 1 % of the sample it stands for. Zero is
//not counted: it is the texel that no photo covers in the texture atlases of photogrammetry, and
//counting it would put the low quantile there. Values that are not finite are not counted either.
class SampleHistogram {
public:
	SampleHistogram();

	void add(float value);
	//the samples of other as well
	void add(const SampleHistogram &other);
	//The value the samples of the type of an image span (65535 for 16 bit integers, 1 for floats, which
	//hold a fraction): the range the samples are scaled from when they are all the same value or
	//when none is counted. The largest of those given is kept.
	void addFullScale(float fullScale);

	uint64_t count() const { return count_; }
	float fullScale() const { return fullScale_; }
	//The value of the first bin (its 16 high bits) where the samples counted up to it reach
	//numerator / denominator of the count; 0 when none is counted.
	float quantile(uint64_t numerator, uint64_t denominator) const;

private:
	std::vector<uint64_t> bins_;
	uint64_t count_ = 0;
	float fullScale_ = 0;
};

//Maps a sample wider than 8 bits to 8 bits with a linear stretch between two values: the low one
//and below are 0, the high one and above 255. Not a number is 0.
class ToneMap {
public:
	//0 to 65535, the range of 16 bit samples
	ToneMap();
	ToneMap(double low, double high);

	//The stretch of the map view of the rasters of DroneDB and of most GIS programs: from the 2nd
	//to the 98th percentile of the samples counted, so that a few saturated or dark texels do not
	//take the contrast of the rest. When the two percentiles are the same value (a texture of one flat
	//value, or of zeros only) the stretch is from 0 to the full scale of the samples.
	static ToneMap stretch(const SampleHistogram &samples);

	uint8_t operator()(double value) const;

	double low() const { return low_; }
	double high() const { return high_; }

private:
	double low_;
	double high_;
	double scale_;
};

} //namespace

#endif // BM3D_TONE_MAP_H
