// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the quadric simplification puts the vertex of a collapse. The minimum of the quadric of
// the two ends is the best place only when the planes it is made of meet at one well determined
// point; in a flat or gently curved region, where the planes differ by the noise of the input, the
// system is solvable and still puts the minimum far from the edge, and the surface grows a spike
// outside the model. The place of a collapse must stay near the edge it collapses.

#include <cmath>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <vcg/math/quadric.h>

#include "build/mesh.h"
#include "build/tmesh.h"

namespace {

using namespace bm3d::detail;

// --- the test of the place --------------------------------------------------------------------

TEST(EdgePlacement, aPlaceInTheBoxOfTheEdgeIsNearIt) {
    const double a[3] = {0, 0, 0}, b[3] = {2, 1, -1};
    const double inside[3] = {1, 0.5, -0.5}, atEnd[3] = {2, 1, -1};
    EXPECT_TRUE(vcg::math::IsNearEdge(inside, a, b, 3, 0));
    EXPECT_TRUE(vcg::math::IsNearEdge(atEnd, a, b, 3, 0));
}

// The box is larger on every side by the length of the edge (the slack is that fraction of it)
TEST(EdgePlacement, theBoxIsLargerOnEverySideByTheLengthOfTheEdge) {
    const double a[3] = {0, 0, 0}, b[3] = {2, 1, -1};
    const double beyondX[3] = {2.4, 1, -1}, beyondZ[3] = {1, 0.5, -1.4}, beforeY[3] = {1, -0.4, -0.5};
    const double length = 0.3 / vcg::math::EdgePlacementSlack;
    for (const double* place : {beyondX, beyondZ, beforeY}) {
        EXPECT_FALSE(vcg::math::IsNearEdge(place, a, b, 3, length));
        EXPECT_TRUE(vcg::math::IsNearEdge(place, a, b, 3, length * 5 / 3));
    }
}

// Only the first n coordinates count: a texture coordinate is checked on its own, with the length
// of the edge in texture coordinates
TEST(EdgePlacement, onlyTheCoordinatesAskedForAreChecked) {
    const double a[5] = {0, 0, 0, 0.5, 0.5}, b[5] = {1, 0, 0, 0.5, 0.5};
    const double farInTexture[5] = {0.5, 0, 0, 3, 0.5};
    EXPECT_TRUE(vcg::math::IsNearEdge(farInTexture, a, b, 3, 1));
    EXPECT_FALSE(vcg::math::IsNearEdge(farInTexture + 3, a + 3, b + 3, 2, 0));
}

// An edge of no length (the ends at the same place) accepts what differs by the rounding of a float
// and nothing more
TEST(EdgePlacement, anEdgeOfNoLengthAcceptsOnlyTheRoundingOfAFloat) {
    const double a[2] = {0.7032, 1}, b[2] = {0.7032, 1};
    const double rounded[2] = {0.7032 + 1e-7, 1 - 1e-7}, moved[2] = {0.7032 + 1e-4, 1};
    EXPECT_TRUE(vcg::math::IsNearEdge(rounded, a, b, 2, 0));
    EXPECT_FALSE(vcg::math::IsNearEdge(moved, a, b, 2, 0));
}

TEST(EdgePlacement, aPlaceThatIsNotANumberIsNotNearTheEdge) {
    const double a[3] = {0, 0, 0}, b[3] = {1, 1, 1};
    const double nan = std::nan(""), inf = HUGE_VAL;
    const double notNumber[3] = {0.5, nan, 0.5}, infinite[3] = {0.5, 0.5, inf};
    EXPECT_FALSE(vcg::math::IsNearEdge(notNumber, a, b, 3, 1e30));
    EXPECT_FALSE(vcg::math::IsNearEdge(infinite, a, b, 3, 1e30));
}

// --- one collapse -------------------------------------------------------------------------------

// Two triangles over a square of side `side`, the corners 0 and 1 being the edge to collapse, with
// the quadric of each corner set by the caller
struct Square {
    Mesh mesh;
    explicit Square(double side) {
        auto v = vcg::tri::Allocator<Mesh>::AddVertices(mesh, 4);
        const float s = float(side);
        const vcg::Point3f corner[4] = {{0, 0, 0}, {s, 0, 0}, {s, s, 0}, {0, s, 0}};
        for (int i = 0; i < 4; i++) {
            v[i].P() = corner[i];
            v[i].Qd().SetZero();
        }
        auto f = vcg::tri::Allocator<Mesh>::AddFaces(mesh, 2);
        const int index[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (int i = 0; i < 2; i++)
            for (int k = 0; k < 3; k++)
                f[i].V(k) = &mesh.vert[index[i][k]];
        vcg::tri::UpdateTopology<Mesh>::VertexFace(mesh);
    }
    void addPlane(int corner, const vcg::Point3d& normal, double offset) {
        vcg::Plane3d plane;
        plane.Set(offset, normal);
        vcg::math::Quadric<double> q;
        q.ByPlane(plane);
        mesh.vert[corner].Qd() += q;
    }
    // where the collapse of the corners 0 and 1 puts the vertex
    vcg::Point3f placeOfTheCollapse() {
        vcg::tri::TriEdgeCollapseQuadricParameter parameters;
        TriEdgeCollapse collapse(VertexPair(&mesh.vert[0], &mesh.vert[1]), 0, &parameters);
        return collapse.optimalPos;
    }
};

bool inTheBoxOfTheEdge(const vcg::Point3f& place, double side, double slack) {
    return place[0] >= -slack && place[0] <= side + slack && place[1] >= -slack && place[1] <= slack &&
           std::fabs(place[2]) <= slack;
}

// Three planes that are nearly the same one: z = 0.004, and the two that tilt it by 1e-6 along x and
// along y and move it by 1e-5. They meet at (10, -10, 0.004), where the surface is not.
void addNearlyParallelPlanes(Square& square) {
    square.addPlane(0, vcg::Point3d(0, 0, 1), 0.004);
    square.addPlane(0, vcg::Point3d(1e-6, 0, 1), 0.00401);
    square.addPlane(1, vcg::Point3d(0, 1e-6, 1), 0.00399);
}

TEST(EdgePlacement, nearlyParallelPlanesDoNotPutTheVertexFarFromTheEdge) {
    Square square(0.01);
    addNearlyParallelPlanes(square);

    const vcg::Point3f place = square.placeOfTheCollapse();
    EXPECT_TRUE(inTheBoxOfTheEdge(place, 0.01, 0.01)) << place[0] << " " << place[1] << " " << place[2];
}

// When the minimum is not used the vertex goes where the quadric is the smallest of the two ends and
// the middle: the tilt along x makes the corner at x = 0.01 a little nearer to the planes
TEST(EdgePlacement, aRejectedMinimumGivesTheBestOfTheEndsAndTheMiddle) {
    Square square(0.01);
    addNearlyParallelPlanes(square);

    const vcg::Point3f place = square.placeOfTheCollapse();
    EXPECT_EQ(place[0], 0.01f);
    EXPECT_EQ(place[1], 0.0f);
    EXPECT_EQ(place[2], 0.0f);
}

// A corner of three perpendicular planes has its minimum at the corner, which is near the edge: the
// minimum is still used
TEST(EdgePlacement, theMinimumNearTheEdgeIsUsed) {
    Square square(0.01);
    square.addPlane(0, vcg::Point3d(1, 0, 0), 0.004);   // the planes x = 0.004, y = 0.003, z = 0.002
    square.addPlane(0, vcg::Point3d(0, 1, 0), 0.003);
    square.addPlane(1, vcg::Point3d(0, 0, 1), 0.002);

    const vcg::Point3f place = square.placeOfTheCollapse();
    EXPECT_NEAR(place[0], 0.004f, 1e-6);
    EXPECT_NEAR(place[1], 0.003f, 1e-6);
    EXPECT_NEAR(place[2], 0.002f, 1e-6);
}

// --- one collapse of a model with texture ------------------------------------------------------

// Three triangles of an indoor model (an office plan of a few hundred thousand triangles), at a place where it is flat
// to a hundredth of a millimetre and the quadric of the edge from corner 0 (0.775144, 0.220605) to
// corner 1 (0.774373, 0.220605), 0.0008 long, has its minimum at y = 0.2229, three times the length of
// the edge away, with a texture coordinate that is outside the ones of its ends
struct TexturedCorner {
    float position[3];
    float texture[2];
};

TEST(EdgePlacement, theMinimumOfAnEdgeOfAModelWithTextureStaysNearTheEdge) {
    const TexturedCorner corners[6] = {
        {{0.774372578f, 0.220605403f, 4.92450237f}, {0.250011414f, 0.0f}},       // 0: end 1 of the edge
        {{0.775143623f, 0.220605403f, 4.92450237f}, {0.187508494f, 0.0f}},       // 1: end 0 of the edge
        {{0.777904928f, 0.232341602f, 4.92450237f}, {0.187506393f, 0.0f}},       // 2
        {{0.779375911f, 0.232362106f, 4.92446518f}, {0.126957402f, 0.000244099996f}},  // 3
        {{0.775732994f, 0.220646396f, 4.92450237f}, {0.125005707f, 0.0f}}};      // 4
    const int triangles[3][3] = {{0, 1, 2}, {3, 2, 1}, {1, 4, 3}};

    TMesh mesh;
    auto v = vcg::tri::Allocator<TMesh>::AddVertices(mesh, 5);
    for (int i = 0; i < 5; i++)
        v[i].P() = vcg::Point3f(corners[i].position[0], corners[i].position[1], corners[i].position[2]);
    auto f = vcg::tri::Allocator<TMesh>::AddFaces(mesh, 3);
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) {
            f[i].V(k) = &mesh.vert[triangles[i][k]];
            f[i].WT(k).P() = vcg::Point2f(corners[triangles[i][k]].texture[0], corners[triangles[i][k]].texture[1]);
        }

    // what TMesh::simplify prepares
    vcg::tri::UpdateNormal<TMesh>::PerFaceNormalized(mesh);
    vcg::tri::UpdateTopology<TMesh>::VertexFace(mesh);
    vcg::tri::TriEdgeCollapseQuadricTexParameter parameters;
    parameters.SetDefaultParams();
    parameters.NormalCheck = true;
    vcg::math::Quadric<double> zero;
    zero.SetZero();
    vcg::tri::QuadricTexHelper<TMesh>::QuadricTemp positions(mesh.vert, zero);
    vcg::tri::QuadricTexHelper<TMesh>::TDp3() = &positions;
    std::vector<std::pair<vcg::TexCoord2<float>, vcg::Quadric5<double>>> perTexture;
    vcg::tri::QuadricTexHelper<TMesh>::Quadric5Temp textures(mesh.vert, perTexture);
    vcg::tri::QuadricTexHelper<TMesh>::TDp() = &textures;
    vcg::LocalOptimization<TMesh> session(mesh, &parameters);
    session.Init<MyTriEdgeCollapseQTex>();

    MyTriEdgeCollapseQTex collapse(TVertexPair(&mesh.vert[1], &mesh.vert[0]), 0, &parameters);
    double minimum1[5], minimum2[5];
    vcg::Quadric5<double> quadric1, quadric2;
    vcg::TexCoord2f t01, t11, t02, t12;
    const int coordinates = collapse.GetTexCoords(t01, t11, t02, t12);
    ASSERT_GE(coordinates, 1);
    collapse.ComputeMinimalsAndPriority(minimum1, minimum2, quadric1, quadric2, t01, t11, t02, t12, coordinates, &parameters);

    const double length = 0.00077;
    EXPECT_GE(minimum1[0], 0.774372578 - length);
    EXPECT_LE(minimum1[0], 0.775143623 + length);
    EXPECT_NEAR(minimum1[1], 0.220605403, length);
    EXPECT_NEAR(minimum1[2], 4.92450237, length);
    // the texture coordinates of the two ends are 0.1875 and 0.25, and 0 and 0, 0.0625 apart
    EXPECT_GE(minimum1[3], 0.1875 - 0.0625 - 1e-6);
    EXPECT_LE(minimum1[3], 0.25 + 0.0625 + 1e-6);
    EXPECT_NEAR(minimum1[4], 0.0, 1e-6);
}

}  // namespace
