// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#ifndef BM3D_TEXEL_BLANKING_H
#define BM3D_TEXEL_BLANKING_H

#include <array>
#include <vector>

#include <vcg/space/point2.h>

#include "../io/image/image.h"

namespace bm3d::detail {

//The texture coordinates of the three corners of a triangle, in the texture they index
using UvTriangle = std::array<vcg::Point2f, 3>;

//A node texture is packed from the bounding boxes of its pieces of texture, so most of it lies
//outside the node's triangles: texels of the model's texture that the node never draws, which
//cost as many bytes as the ones it draws. Those further than a few texels from every triangle
//get one flat color, the mean of the others. image is the node texture as stored (row 0 at the
//top, v = 1), triangles the ones of the node with their texture coordinates in that texture.
//An image too small to have texels away from the triangles is left as it is.
void blankUndrawnTexels(Image &image, const std::vector<UvTriangle> &triangles);

} //namespace

#endif // BM3D_TEXEL_BLANKING_H
