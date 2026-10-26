// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The check that every loader applies to the coordinates of the vertices it reads. A NaN or an
// infinity (written as such in a text file, or stored as such in a binary one, or too large for a
// float) gives the box of the model no size and the kd-tree no way to divide it, so it is refused
// when it is read, with the place in the file, not found later as a failure of the build.

#ifndef NX_FINITE_H
#define NX_FINITE_H

#include <cmath>
#include <string>

#include "../common/error.h"

namespace nx {

//true when the three coordinates are numbers
inline bool isFinite(const float *v) {
	return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

//Throws nx::Error when a coordinate of v is not finite. where() says which vertex it is and in
//which file ("vertex 12 of cube.ply"); it is called only when the error is thrown, so a loader
//can check every vertex at no cost.
template<class Where>
void requireFinite(const float *v, const Where &where) {
	if(isFinite(v))
		return;
	throw Error(where() + " has a coordinate that is not a finite number: " +
	            std::to_string(v[0]) + " " + std::to_string(v[1]) + " " + std::to_string(v[2]));
}

} //namespace

#endif // NX_FINITE_H
