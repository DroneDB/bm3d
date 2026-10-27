// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "input_reader.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

#include "../io/mesh/meshstream.h"

namespace bm3d::detail {

namespace {

//The builder computes in floats the squares of the lengths in a model and the products of its
//edges (the bounding sphere, the cone of the normals, the quadrics of the simplification), and
//those overflow when the model is some billions of units across (a city is 1e7 millimetres). The
//bounding sphere of a model beyond that is not a number and the search for it never ended: that
//is what a file of random bytes read as floats did.
const double kMaxModelSize = 1e9;

} //namespace

void readMesh(StreamSoup &stream, const std::string &input) {
	readingInput([&] {
		std::string mtl;  // not exposed yet
		stream.load(std::vector<std::string>{input}, mtl);
	});
	if(stream.size() == 0)
		throw InputError("The input holds no triangles: " + input);

	// in double, where the square of the largest float difference still fits
	double squared = 0;
	for(int k = 0; k < 3; k++) {
		const double extent = double(stream.box.max[k]) - double(stream.box.min[k]);
		squared += extent * extent;
	}
	if(!(std::sqrt(squared) <= kMaxModelSize)) {
		std::ostringstream message;
		message << "The model is " << std::setprecision(3) << std::sqrt(squared) << " units across (the diagonal of its "
		        << "bounding box); the largest that can be built is " << kMaxModelSize << ": " << input;
		throw InputError(message.str());
	}
}

} //namespace
