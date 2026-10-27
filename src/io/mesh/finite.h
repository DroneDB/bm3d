// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The checks that every loader applies to the numbers it reads for a vertex: its coordinates and its
// texture coordinates. A NaN or an infinity (written as such in a text file, or stored as such in a
// binary one, or too large for a float) gives the box of the model no size and the kd-tree no way to
// divide it, and a texture coordinate beyond what a float can wrap gives the texture no pixel to
// take, so it is refused when it is read, with the place in the file, not found later as a failure
// of the build.

#ifndef BM3D_FINITE_H
#define BM3D_FINITE_H

#include <cmath>
#include <string>

#include "../../common/error.h"

namespace bm3d::detail {

//true when the three coordinates are numbers
inline bool isFinite(const float *v) {
	return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

//Throws Error when a coordinate of v is not finite. where() says which vertex it is and in
//which file ("vertex 12 of cube.ply"); it is called only when the error is thrown, so a loader
//can check every vertex at no cost.
template<class Where>
void requireFinite(const float *v, const Where &where) {
	if(isFinite(v))
		return;
	throw Error(where() + " has a coordinate that is not a finite number: " +
	            std::to_string(v[0]) + " " + std::to_string(v[1]) + " " + std::to_string(v[2]));
}

//The texture coordinates are wrapped into [0, 1) by their fractional part, and a float above 2^24 has
//none: it is a whole number, so beyond this the texture position is lost, and the simplification of
//the model, which treats the coordinates as two more dimensions, overflows its quadrics.
constexpr float MAX_TEXTURE_COORDINATE = 16777216.0f;

//true when t is a number whose fractional part is known
inline bool isUsableTexCoord(float t) {
	return std::fabs(t) <= MAX_TEXTURE_COORDINATE; //false for a NaN
}

//Throws Error when one of the count texture coordinates in t is not usable (a NaN, an
//infinity, or beyond MAX_TEXTURE_COORDINATE)
template<class Where>
void requireUsableTexCoords(const float *t, int count, const Where &where) {
	for(int i = 0; i < count; i++)
		if(!isUsableTexCoord(t[i]))
			throw Error(where() + " has a texture coordinate that is not a number from -" +
			            std::to_string(int(MAX_TEXTURE_COORDINATE)) + " to " +
			            std::to_string(int(MAX_TEXTURE_COORDINATE)) + ": " + std::to_string(t[i]));
}

} //namespace

#endif // BM3D_FINITE_H
