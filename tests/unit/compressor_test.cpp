// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The node compressor and the cone of the normals: what corto leaves of a node (the counts and
// the ends of the groups are the encoder's, not the ones the node went in with), the step of the
// coordinates, and the four values of the cone.

#include <array>
#include <cmath>
#include <cstring>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "common/cone.h"
#include "compress/compressor.h"
#include "format/bm3d_reader.h"
#include "format/bm3d_writer.h"
#include "temp_dir.h"
#include "model_files.h"

namespace {

using namespace bm3d::detail;

// The memory of a node of the builder: positions, then normals (the layout has no colours or
// texture coordinates), then the triangles
std::vector<unsigned char> nodeMemory(const std::vector<std::array<float, 3>>& positions,
                                      const std::vector<std::array<uint16_t, 3>>& triangles) {
    std::vector<unsigned char> memory;
    const auto append = [&memory](const void* data, size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        memory.insert(memory.end(), bytes, bytes + size);
    };
    for (const auto& p : positions)
        append(p.data(), 12);
    for (size_t i = 0; i < positions.size(); i++) {
        const int16_t normal[3] = {0, 0, 32766};
        append(normal, 6);
    }
    for (const auto& t : triangles)
        append(t.data(), 6);
    return memory;
}

VertexLayout withNormals() {
    VertexLayout layout;
    layout.normals = true;
    return layout;
}

// A unit square in the plane z = 0 and a fifth vertex that only a degenerate triangle uses
const std::vector<std::array<float, 3>> kSquare = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {5, 5, 5}};

TEST(NodeCompressor, aNodeIsKeptAsItIsWhenNoTriangleIsDegenerate) {
    const NodeCompressor compressor(withNormals(), CompressionSettings(), -10);
    const auto memory = nodeMemory({kSquare.begin(), kSquare.begin() + 4}, {{0, 1, 2}, {0, 2, 3}});
    const EncodedNode node = compressor.compress(memory.data(), 4, 2, {1, 2});
    EXPECT_EQ(node.vertexCount, 4);
    EXPECT_EQ(node.faceCount, 2);
    EXPECT_EQ(node.groupEnds, (std::vector<uint32_t>{1, 2}));
    EXPECT_FALSE(node.stream.empty());
    // the stream of corto begins with its magic number 0x787A6300 and the version of the stream
    EXPECT_EQ(std::vector<unsigned char>(node.stream.begin(), node.stream.begin() + 8),
              (std::vector<unsigned char>{0x00, 0x63, 0x7A, 0x78, 1, 0, 0, 0}));
}

// The encoder drops the triangles with two equal vertex indexes and moves the end of each group
// down by the ones it dropped before it; the vertices that no triangle uses are dropped too
TEST(NodeCompressor, theCountsAndTheEndsOfTheGroupsAreTheOnesTheEncoderLeaves) {
    const NodeCompressor compressor(withNormals(), CompressionSettings(), -10);

    // a degenerate triangle in the first group, which keeps one triangle
    auto memory = nodeMemory(kSquare, {{4, 4, 0}, {0, 1, 2}, {0, 2, 3}});
    EncodedNode node = compressor.compress(memory.data(), 5, 3, {2, 3});
    EXPECT_EQ(node.faceCount, 2);
    EXPECT_EQ(node.vertexCount, 4);  // the fifth vertex was used by the degenerate triangle only
    EXPECT_EQ(node.groupEnds, (std::vector<uint32_t>{1, 2}));

    // in the second
    memory = nodeMemory(kSquare, {{0, 1, 2}, {4, 0, 4}, {0, 2, 3}});
    node = compressor.compress(memory.data(), 5, 3, {1, 3});
    EXPECT_EQ(node.groupEnds, (std::vector<uint32_t>{1, 2}));
    EXPECT_EQ(node.faceCount, 2);
    EXPECT_EQ(node.vertexCount, 4);

    // a group that is all degenerate is left empty: its end is the end of the group before it
    memory = nodeMemory(kSquare, {{0, 1, 2}, {0, 2, 3}, {4, 4, 4}, {1, 1, 0}});
    node = compressor.compress(memory.data(), 5, 4, {1, 2, 4});
    EXPECT_EQ(node.groupEnds, (std::vector<uint32_t>{1, 2, 2}));
    EXPECT_EQ(node.faceCount, 2);

    // a vertex nothing uses is dropped although no triangle was degenerate
    memory = nodeMemory(kSquare, {{0, 1, 2}, {0, 2, 3}});
    node = compressor.compress(memory.data(), 5, 2, {2});
    EXPECT_EQ(node.vertexCount, 4);
    EXPECT_EQ(node.faceCount, 2);
}

TEST(NodeCompressor, aNodeWithoutTrianglesCannotBeCompressed) {
    const NodeCompressor compressor(withNormals(), CompressionSettings(), -10);
    const auto memory = nodeMemory({kSquare.begin(), kSquare.begin() + 3}, {});
    EXPECT_THROW(compressor.compress(memory.data(), 3, 0, {0}), Error);
}

// The writer takes the counts from the encoder: the node written for the dropped triangles has
// the lower counts, and the reader decodes exactly what the record says
TEST(NodeCompressor, aNodeWrittenAfterEncodingDecodesToTheCountsOfItsRecord) {
    const NodeCompressor compressor(withNormals(), CompressionSettings(), -10);
    const auto memory = nodeMemory(kSquare, {{4, 4, 0}, {0, 1, 2}, {0, 2, 3}});
    const EncodedNode encoded = compressor.compress(memory.data(), 5, 3, {2, 3});

    bm3dtest::TempDir dir;
    const std::string path = bm3dtest::pathToUtf8(dir.file("o.bm3d"));
    {
        Bm3dWriter writer(path, withNormals(), {1, 2, 0});
        NodeRecord node;
        node.vertexCount = encoded.vertexCount;
        node.faceCount = encoded.faceCount;
        std::vector<PatchRecord> patches(2);
        for (size_t p = 0; p < 2; p++) {
            patches[p].node = 1;
            patches[p].triangleOffset = encoded.groupEnds[p];
        }
        writer.addNode(node, patches.data(), patches.size(), encoded.stream.data(), encoded.stream.size());
        writer.finish(SphereRecord(), "");
    }
    Bm3dReader reader = Bm3dReader::open(path);
    EXPECT_EQ(reader.header().vertexCount, 4u);  // not the 5 the node went in with
    EXPECT_EQ(reader.header().faceCount, 2u);
    EXPECT_EQ(reader.patches()[0].triangleOffset, 1u);
    EXPECT_EQ(reader.patches()[1].triangleOffset, 2u);
    const DecodedNode decoded = reader.decodeNode(0);
    EXPECT_EQ(decoded.positions.size(), 12u);
    EXPECT_EQ(decoded.normals.size(), 12u);
    EXPECT_EQ(decoded.indices.size(), 6u);
    EXPECT_TRUE(decoded.uvs.empty());
    EXPECT_TRUE(decoded.colors.empty());
    for (uint16_t index : decoded.indices)
        EXPECT_LT(index, 4);

    // the two triangles are the two of the square, wherever the encoder put their vertices
    std::set<std::array<float, 3>> corners;
    for (size_t v = 0; v < 4; v++)
        corners.insert({decoded.positions[3 * v], decoded.positions[3 * v + 1], decoded.positions[3 * v + 2]});
    EXPECT_EQ(corners.size(), 4u);
    for (const auto& corner : corners)
        EXPECT_NEAR(corner[2], 0.0f, 1e-3f);
    for (size_t v = 0; v < 4; v++)
        EXPECT_NEAR(decoded.normals[3 * v + 2] / 32767.0f, 1.0f, 0.05f);
}

TEST(NodeCompressor, aNodeWithColoursAndTextureCoordinatesIsEncodedWithThem) {
    VertexLayout layout;
    layout.normals = layout.colors = layout.texCoords = true;
    const NodeCompressor compressor(layout, CompressionSettings(), -10);
    std::vector<unsigned char> memory;
    const auto append = [&memory](const void* data, size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        memory.insert(memory.end(), bytes, bytes + size);
    };
    for (int i = 0; i < 4; i++)
        append(kSquare[size_t(i)].data(), 12);
    for (int i = 0; i < 4; i++) {
        const float uv[2] = {kSquare[size_t(i)][0], kSquare[size_t(i)][1]};
        append(uv, 8);
    }
    for (int i = 0; i < 4; i++) {
        const int16_t normal[3] = {0, 0, 32766};
        append(normal, 6);
    }
    for (int i = 0; i < 4; i++) {
        const unsigned char color[4] = {200, 100, 50, 255};
        append(color, 4);
    }
    const uint16_t triangles[6] = {0, 1, 2, 0, 2, 3};
    append(triangles, 12);
    const EncodedNode encoded = compressor.compress(memory.data(), 4, 2, {2});
    EXPECT_EQ(encoded.vertexCount, 4);

    bm3dtest::TempDir dir;
    const std::string path = bm3dtest::pathToUtf8(dir.file("o.bm3d"));
    Bm3dWriter writer(path, layout, {1, 1, 1});
    NodeRecord node;
    node.vertexCount = encoded.vertexCount;
    node.faceCount = encoded.faceCount;
    PatchRecord patch;
    patch.node = 1;
    patch.triangleOffset = encoded.groupEnds[0];
    patch.texture = 0;
    writer.addNode(node, &patch, 1, encoded.stream.data(), encoded.stream.size());
    const unsigned char image[16] = {'R', 'I', 'F', 'F', 8, 0, 0, 0, 'W', 'E', 'B', 'P', 0, 0, 0, 0};
    writer.addTexture(image, sizeof(image));
    writer.finish(SphereRecord(), "");

    Bm3dReader reader = Bm3dReader::open(path);
    const DecodedNode decoded = reader.decodeNode(0);
    EXPECT_EQ(decoded.uvs.size(), 8u);
    EXPECT_EQ(decoded.colors.size(), 16u);
    for (size_t v = 0; v < 4; v++) {
        EXPECT_NEAR(decoded.colors[4 * v], 200, 20);
        EXPECT_NEAR(decoded.colors[4 * v + 3], 255, 10);
        // the texture coordinate of a vertex is its x and y
        EXPECT_NEAR(decoded.uvs[2 * v], decoded.positions[3 * v], 0.01f);
        EXPECT_NEAR(decoded.uvs[2 * v + 1], decoded.positions[3 * v + 1], 0.01f);
    }
}

// --- the step of the coordinates -------------------------------------------------------------

TEST(CoordinateStep, itIsAFractionOfTheSmallestErrorOfTheRootAndTheLeaves) {
    EXPECT_FLOAT_EQ(coordinateStep(0.1f, 8.0f, {}), 0.4f);
    EXPECT_FLOAT_EQ(coordinateStep(0.1f, 8.0f, {4.0f, 2.0f, 6.0f}), 0.1f);
    EXPECT_FLOAT_EQ(coordinateStep(0.1f, 8.0f, {100.0f}), 0.4f);
}

TEST(CoordinateStep, aLeafWithoutErrorIsLeftOutAndAStepThatIsNotPositiveBecomesOne) {
    EXPECT_FLOAT_EQ(coordinateStep(0.1f, 8.0f, {0.0f, 4.0f}), 0.2f);
    EXPECT_EQ(coordinateStep(0.1f, 0.0f, {}), 1.0f);
    EXPECT_EQ(coordinateStep(0.1f, -1.0f, {}), 1.0f);
}

// The exponent is log2 of the step rounded toward zero, read from the bits of the float
TEST(CoordinateStep, theExponentIsTheLogarithmRoundedTowardZero) {
    for (int e = -40; e <= 40; e++) {
        const float power = std::ldexp(1.0f, e);
        EXPECT_EQ(coordinateExponent(power), e) << power;
        // just above and below a power of two
        const float above = std::nextafter(power, 2 * power), below = std::nextafter(power, 0.0f);
        EXPECT_EQ(coordinateExponent(above), e < 0 ? e + 1 : e) << above;
        EXPECT_EQ(coordinateExponent(below), e <= 0 ? e : e - 1) << below;
    }
    EXPECT_EQ(coordinateExponent(0.3f), -1);
    EXPECT_EQ(coordinateExponent(3.0f), 1);
    EXPECT_EQ(coordinateExponent(0.75f), 0);
    EXPECT_EQ(coordinateExponent(1e-42f), -139);  // a subnormal step
}

// --- the cone -----------------------------------------------------------------------------------

// The ring of a direction is its angle with the axis in units of pi/49, rounded down: what
// int(49 * acos(t) / pi) gives wherever the angle is not within a hair of a boundary
TEST(Cone, theRingIsTheAngleInFortyNinthsOfPiRoundedDown) {
    const double pi = 3.14159265358979323846;
    const vcg::Point3f axis(0, 0, 1);
    int checked = 0;
    for (int i = 0; i <= 20000; i++) {
        const double angle = pi * i / 20000;
        const vcg::Point3f n(float(std::sin(angle)), 0.0f, float(std::cos(angle)));
        // the angle the float direction makes: the cosine is a float as in coneRing
        const float t = (axis * n) / (axis.Norm() * n.Norm());
        const double rings = 49 * std::acos(std::max(-1.0, std::min(1.0, double(t)))) / pi;
        if (std::fabs(rings - std::round(rings)) < 1e-6 && rings > 0 && rings < 49)
            continue;  // at a boundary, where the last bit of acos decides
        ASSERT_EQ(coneRing(axis, n), int(rings)) << angle;
        checked++;
    }
    EXPECT_GT(checked, 19000);
    EXPECT_EQ(coneRing(axis, vcg::Point3f(0, 0, 0)), -1);   // a direction of no length
    EXPECT_EQ(coneRing(axis, vcg::Point3f(0, 0, -5)), 49);  // the opposite direction
    EXPECT_EQ(coneRing(axis, vcg::Point3f(0, 0, 2)), 0);
}


std::array<short, 4> coneOf(const std::vector<vcg::Point3f>& normals) {
    std::vector<vcg::Point3f> copy = normals;
    AnchoredCone3f anchored;
    anchored.AddNormals(copy, 0.95f);
    Cone3s cone;
    cone.Import(anchored);
    return {cone.n[0], cone.n[1], cone.n[2], cone.n[3]};
}

// The cone of the worked example: two triangles that face +z. The mean normal is (0, 0, 1), the
// angle is pi/50 and the axis 1/sin(pi/50), about 15.9, which is written as 10 at most
TEST(Cone, theNormalsOfTheWorkedExampleGiveItsFourValues) {
    EXPECT_EQ(coneOf({{0, 0, 1}, {0, 0, 1}}), (std::array<short, 4>{0, 0, 32766, 32760}));
    // the direction is the mean of the normals, cone[i] = trunc(32766 * c)
    const float s = std::sqrt(0.5f);
    const auto cone = coneOf({{s, 0, s}, {s, 0, s}});
    EXPECT_EQ(cone[0], short(32766 * s));
    EXPECT_EQ(cone[1], 0);
    EXPECT_EQ(cone[2], short(32766 * s));
    EXPECT_EQ(cone[3], 32760);
}

TEST(Cone, aSpreadOfNormalsThatCullsNothingGivesFourZeros) {
    // normals all around: the mean is zero
    EXPECT_EQ(coneOf({{1, 0, 0}, {-1, 0, 0}}), (std::array<short, 4>{0, 0, 0, 0}));
    // normals in a half sphere: the cone is at least pi/2 - 0.1 wide
    std::vector<vcg::Point3f> wide;
    for (int i = 0; i < 100; i++) {
        const float angle = 3.14159265f * float(i) / 100;
        wide.push_back({std::cos(angle), 0.0f, std::sin(angle)});
    }
    EXPECT_EQ(coneOf(wide), (std::array<short, 4>{0, 0, 0, 0}));
}

TEST(Cone, anEmptyConeOfTheBuilderIsFourZeros) {
    EXPECT_EQ(Cone3s().n[0], 0);
    EXPECT_EQ(Cone3s().n[3], 0);
    Cone3s imported;
    imported.n[0] = imported.n[1] = imported.n[2] = imported.n[3] = 5;
    imported.Import(AnchoredCone3f());  // a cone with no axis
    EXPECT_EQ(imported.n[0], 0);
    EXPECT_EQ(imported.n[1], 0);
    EXPECT_EQ(imported.n[2], 0);
    EXPECT_EQ(imported.n[3], 0);
}

}  // namespace
