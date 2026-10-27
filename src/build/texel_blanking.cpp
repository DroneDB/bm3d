// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#include "texel_blanking.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bm3d::detail {

namespace {

//The texels around the triangles that are kept as they are: bilinear filtering and the first
//mipmap levels read them along the edges of the triangles.
const int TEXEL_MARGIN = 4;

} //namespace

void blankUndrawnTexels(Image &image, const std::vector<UvTriangle> &triangles) {
	const int width = image.width(), height = image.height();
	if(width <= 2*TEXEL_MARGIN || height <= 2*TEXEL_MARGIN)
		return;
	std::vector<uint8_t> drawn(size_t(width) * size_t(height), 0);
	//the texels whose center is inside a triangle, in pixel units of the stored image
	for(const UvTriangle &triangle: triangles) {
		float x[3], y[3];
		for(int k = 0; k < 3; k++) {
			const auto &uv = triangle[k];
			x[k] = uv[0] * width;
			y[k] = (1.0f - uv[1]) * height;
		}
		const float area = (x[1]-x[0])*(y[2]-y[0]) - (x[2]-x[0])*(y[1]-y[0]);
		const int x0 = std::max(0, int(std::floor(std::min({x[0], x[1], x[2]}))));
		const int x1 = std::min(width - 1, int(std::ceil(std::max({x[0], x[1], x[2]}))));
		const int y0 = std::max(0, int(std::floor(std::min({y[0], y[1], y[2]}))));
		const int y1 = std::min(height - 1, int(std::ceil(std::max({y[0], y[1], y[2]}))));
		for(int py = y0; py <= y1; py++) {
			for(int px = x0; px <= x1; px++) {
				const float cx = px + 0.5f, cy = py + 0.5f;
				//on the same side of the three edges as the triangle's own orientation; a triangle
				//with no area marks the texels its corners fall in
				float w[3];
				for(int k = 0; k < 3; k++) {
					const int j = (k + 1) % 3;
					w[k] = (x[j]-x[k])*(cy-y[k]) - (y[j]-y[k])*(cx-x[k]);
				}
				const bool inside = area >= 0 ? (w[0] >= 0 && w[1] >= 0 && w[2] >= 0)
				                              : (w[0] <= 0 && w[1] <= 0 && w[2] <= 0);
				if(inside)
					drawn[size_t(py) * width + px] = 1;
			}
		}
		for(int k = 0; k < 3; k++) {
			const int px = std::min(width - 1, std::max(0, int(x[k]))), py = std::min(height - 1, std::max(0, int(y[k])));
			drawn[size_t(py) * width + px] = 1;
		}
	}
	//grow by TEXEL_MARGIN: a square around each drawn texel, rows then columns
	std::vector<uint8_t> grown(drawn.size(), 0);
	for(int py = 0; py < height; py++) {
		int last = -2*TEXEL_MARGIN - 1; //the last drawn texel to the left, or beyond reach
		for(int px = 0; px < width + TEXEL_MARGIN; px++) {
			if(px < width && drawn[size_t(py) * width + px])
				last = px;
			const int at = px - TEXEL_MARGIN;
			if(at >= 0 && at < width && (last >= at - TEXEL_MARGIN))
				grown[size_t(py) * width + at] = 1;
		}
	}
	std::fill(drawn.begin(), drawn.end(), 0);
	for(int px = 0; px < width; px++) {
		int last = -2*TEXEL_MARGIN - 1;
		for(int py = 0; py < height + TEXEL_MARGIN; py++) {
			if(py < height && grown[size_t(py) * width + px])
				last = py;
			const int at = py - TEXEL_MARGIN;
			if(at >= 0 && at < height && (last >= at - TEXEL_MARGIN))
				drawn[size_t(at) * width + px] = 1;
		}
	}
	//the mean of the kept texels, then the flat color everywhere else
	uint64_t sum[3] = {0, 0, 0}, kept = 0;
	for(int py = 0; py < height; py++) {
		const uint32_t *row = image.scanLine(py);
		for(int px = 0; px < width; px++) {
			if(!drawn[size_t(py) * width + px]) continue;
			sum[0] += (row[px] >> 16) & 0xff; sum[1] += (row[px] >> 8) & 0xff; sum[2] += row[px] & 0xff;
			kept++;
		}
	}
	if(kept == 0)
		return;
	const uint32_t flat = Image::rgb(unsigned(sum[0] / kept), unsigned(sum[1] / kept), unsigned(sum[2] / kept));
	for(int py = 0; py < height; py++) {
		uint32_t *row = image.scanLine(py);
		for(int px = 0; px < width; px++)
			if(!drawn[size_t(py) * width + px])
				row[px] = flat;
	}
}

} //namespace
