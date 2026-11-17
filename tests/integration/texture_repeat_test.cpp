// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Texture coordinates outside the unit square. The sampler of a textured model wraps (OBJ, and
// glTF by default, repeat the texture), so a coordinate of 1.25 shows the texel of 0.25: models
// whose coordinates are shifted by whole repeats (a glTF converted with its v flipped has v from
// -1 to 0), cross an edge of the texture, or tile a texture many times over a surface. The builder
// cuts the pieces of texture each node uses and packs them in one node texture, which the viewer
// samples clamped, so the repeat has to be in the node texture. These tests build a grid whose
// texture gives each texel the colour of its place (red grows with u, green with v), read every
// node back, and check that the node texture shows, at the middle of each triangle, the colour the
// wrapped coordinate of that point has in the model's texture.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "bm3d_file.h"
#include "model_files.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

constexpr int kTextureSide = 256;
// The colours are compared away from the edges of the texture (and two texels of the node texture
// more), where the red and the green jump from 255 to 0 and the filtering of the coarser levels mixes
// the two
constexpr double kEdgeMargin = 0.05;
// The lossy WebP of the node textures, and a texel of the node texture: the step of the gradient
// that its nearest texel is off by, and the averaging of the coarser levels
constexpr double kWebpTolerance = 16;
// A node texture with fewer texels than this for a repeat of the texture (a coarse level of a texture
// repeated many times, which the levels of the model's texture make smaller) holds the mean colour of
// the repeat, not its gradient: its colours are not compared
constexpr double kMinTexelsPerRepeat = 16;
// Points compared in each triangle, along each side
constexpr int kSamplesPerSide = 16;

// Red is u and green is v, from 0 to 255 (rows run from v = 1 at the top); bands count from 1
double placeColour(int band, int x, int y) {
    if (band == 1)
        return 255.0 * (x + 0.5) / kTextureSide;
    if (band == 2)
        return 255.0 * (kTextureSide - 0.5 - y) / kTextureSide;
    return 128.0;
}

struct RepeatCase {
    const char* name;
    int cells;  // of the grid, along each side
    GridTexCoords uv;
};

void PrintTo(const RepeatCase& c, std::ostream* out) {
    *out << c.name;
}

class TextureRepeat : public ::testing::TestWithParam<RepeatCase> {};

double fraction(double value) {
    return value - std::floor(value);
}

// The node texture at (u, v), the nearest texel
std::array<int, 2> sample(const DecodedImage& image, double u, double v) {
    const int x = std::clamp(int(std::floor(u * image.width)), 0, image.width - 1);
    const int y = std::clamp(int(std::floor((1.0 - v) * image.height)), 0, image.height - 1);
    const size_t at = size_t(y) * size_t(image.width) + size_t(x);
    return {image.bands[0][at], image.bands[1][at]};
}

using Point = std::array<double, 2>;

Point mix(const std::array<Point, 3>& corners, double a, double b) {
    const double c = 1 - a - b;
    return {a * corners[0][0] + b * corners[1][0] + c * corners[2][0], a * corners[0][1] + b * corners[1][1] + c * corners[2][1]};
}

TEST_P(TextureRepeat, eachTriangleShowsTheTexelsOfItsWrappedCoordinates) {
    const RepeatCase& c = GetParam();
    TempDir dir;
    writeBmp(dir.file("texture.bmp"), makeRaster(kTextureSide, kTextureSide, 3, placeColour));
    writeGridModel(dir.path(), c.cells, "texture.bmp", c.uv);
    const fs::path out = dir.file("grid.bm3d");
    ASSERT_NO_THROW(bm3d::build(dir.file("grid.obj"), out));

    const Bm3dFile file = readBm3dFile(out);
    ASSERT_FALSE(file.textures.empty());
    int checked = 0, wrong = 0;
    for (size_t node = 0; node < file.nodeTexture.size(); node++) {
        ASSERT_GE(file.nodeTexture[node], 0) << "node " << node;
        const DecodedImage image = decodeWebp(file.textures[size_t(file.nodeTexture[node])]);
        const NodeMesh mesh = readNode(out, uint32_t(node));
        ASSERT_EQ(mesh.uvs.size(), mesh.positions.size()) << "node " << node;
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            // the coordinates of the model's texture at the corners (not wrapped), and in the node texture
            std::array<Point, 3> model, stored;
            for (int k = 0; k < 3; k++) {
                const uint32_t vertex = mesh.indices[i + size_t(k)];
                for (int axis = 0; axis < 2; axis++) {
                    model[k][axis] = c.uv.repeats * mesh.positions[vertex][axis] / c.cells + c.uv.offset[axis];
                    stored[k][axis] = mesh.uvs[vertex][axis];
                }
            }
            const double modelSpan = std::hypot(model[1][0] - model[0][0], model[1][1] - model[0][1]);
            const double storedSpan = std::hypot((stored[1][0] - stored[0][0]) * image.width,
                                                 (stored[1][1] - stored[0][1]) * image.height);
            const double texelsPerRepeat = modelSpan > 0 ? storedSpan / modelSpan : 0;
            if (texelsPerRepeat < kMinTexelsPerRepeat)
                continue;
            const double margin = kEdgeMargin + 2 / texelsPerRepeat;
            const double tolerance = kWebpTolerance + 255 / texelsPerRepeat;
            for (int a = 1; a < kSamplesPerSide; a++)
                for (int b = 1; a + b < kSamplesPerSide; b++) {
                    const Point at = mix(model, double(a) / kSamplesPerSide, double(b) / kSamplesPerSide);
                    const double u = fraction(at[0]), v = fraction(at[1]);
                    if (std::min({u, v, 1 - u, 1 - v}) < margin)
                        continue;
                    const Point where = mix(stored, double(a) / kSamplesPerSide, double(b) / kSamplesPerSide);
                    const std::array<int, 2> shown = sample(image, where[0], where[1]);
                    checked++;
                    if (std::abs(shown[0] - 255.0 * u) > tolerance || std::abs(shown[1] - 255.0 * v) > tolerance) {
                        if (wrong++ < 5)
                            ADD_FAILURE() << "node " << node << ": the point (" << at[0] << ", " << at[1]
                                          << ") of the texture shows red " << shown[0] << " and green " << shown[1]
                                          << ", not " << 255.0 * u << " and " << 255.0 * v;
                    }
                }
        }
    }
    EXPECT_GT(checked, 2 * c.cells * c.cells) << "too few points away from the edges of the texture";
    EXPECT_EQ(wrong, 0) << "of " << checked << " points";
}

INSTANTIATE_TEST_SUITE_P(
    Coordinates, TextureRepeat,
    ::testing::Values(RepeatCase{"inTheUnitSquare", 64, {}},
                      // a glTF whose v is flipped by its conversion to OBJ
                      RepeatCase{"shiftedBelowZero", 64, {1, {0, -1}}},
                      RepeatCase{"shiftedByWholeRepeats", 64, {1, {3, -6}}},
                      // squares of a quarter of the texture, half of them across an edge of it
                      RepeatCase{"acrossTheEdges", 4, {1, {0.375, 0.125}}},
                      RepeatCase{"repeatedThreeTimes", 8, {3, {0, 0}}},
                      // the nodes are cut small to keep the texels of the texture repeated 50 times
                      RepeatCase{"repeatedFiftyTimes", 64, {50, {-0.3, 0.1}}},
                      // two triangles over 40 repeats of 256 texels, 10240 texels a side: the repeat
                      // is made smaller for the node texture to pack
                      RepeatCase{"repeatedFortyTimesByTwoTriangles", 1, {40, {0.2, -0.7}}}),
    [](const ::testing::TestParamInfo<RepeatCase>& info) { return std::string(info.param.name); });

}  // namespace
