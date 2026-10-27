// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The coordinates are quantised to a step the whole file shares, and the step of a model of a
// single node (the node is the root and the leaf) came from its radius, so that a sphere of 5800
// triangles was quantised to the length of its edges and its silhouette was jagged: triangles were
// flipped and flattened. The leaves hold the triangles of the input and keep their precision: the
// step follows the short edges of the leaf.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "bm3d_file.h"
#include "model_files.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

using Point = std::array<double, 3>;

struct Model {
    std::vector<Point> points;
    std::vector<std::array<int, 3>> triangles;
};

// A sphere of radius 1 made from a cube whose faces are cut into n x n squares, the points pushed
// onto the sphere: 12 n n triangles
Model sphereModel(int n) {
    Model model;
    std::map<std::array<int, 3>, int> numbers;
    const auto point = [&](int x, int y, int z) {
        const std::array<int, 3> key{x, y, z};
        const auto found = numbers.find(key);
        if (found != numbers.end())
            return found->second;
        const double length = std::sqrt(double(x * x + y * y + z * z));
        model.points.push_back({x / length, y / length, z / length});
        return numbers[key] = int(model.points.size()) - 1;
    };
    const int k = n / 2;
    for (int axis = 0; axis < 3; axis++)
        for (int sign : {-1, 1}) {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            for (int i = -k; i < k; i++)
                for (int j = -k; j < k; j++) {
                    const auto at = [&](int a, int b) {
                        std::array<int, 3> c{};
                        c[axis] = sign * k;
                        c[u] = a;
                        c[v] = b;
                        return point(c[0], c[1], c[2]);
                    };
                    const int p00 = at(i, j), p10 = at(i + 1, j), p11 = at(i + 1, j + 1), p01 = at(i, j + 1);
                    if (sign > 0) {
                        model.triangles.push_back({p00, p10, p11});
                        model.triangles.push_back({p00, p11, p01});
                    } else {
                        model.triangles.push_back({p00, p11, p10});
                        model.triangles.push_back({p00, p01, p11});
                    }
                }
        }
    return model;
}

// A part like the ones of a CAD program: a block of 1 x 1 x 1 whose four long edges are chamfered
// by 0.05, the cross section an octagon with sides of 0.9 and 0.0707, closed by two caps
Model chamferedBlock() {
    Model model;
    const double c = 0.05;
    const double section[8][2] = {{c, 0}, {1 - c, 0}, {1, c}, {1, 1 - c}, {1 - c, 1}, {c, 1}, {0, 1 - c}, {0, c}};
    for (int layer = 0; layer < 2; layer++)
        for (const auto& s : section)
            model.points.push_back({s[0], s[1], double(layer)});
    for (int i = 0; i < 8; i++) {
        const int a = i, b = (i + 1) % 8;
        model.triangles.push_back({a, b, 8 + b});
        model.triangles.push_back({a, 8 + b, 8 + a});
    }
    for (int i = 1; i < 7; i++) {
        model.triangles.push_back({0, i + 1, i});
        model.triangles.push_back({8, 8 + i, 8 + i + 1});
    }
    return model;
}

std::string objOfModel(const Model& model) {
    std::ostringstream obj;
    obj.precision(17);
    for (const Point& p : model.points)
        obj << "v " << p[0] << " " << p[1] << " " << p[2] << "\n";
    for (const auto& t : model.triangles)
        obj << "f " << t[0] + 1 << " " << t[1] + 1 << " " << t[2] + 1 << "\n";
    return obj.str();
}

NodeMesh buildAndRead(const Model& model, const std::string& name) {
    TempDir dir;
    writeTextFile(dir.file(name + ".obj"), objOfModel(model));
    bm3d::build(dir.file(name + ".obj"), dir.file(name + ".bm3d"));
    const Bm3dFile file = readBm3dFile(dir.file(name + ".bm3d"));
    EXPECT_EQ(file.nNodes, 2u) << "a model of a single node and the sink";
    return readNode(dir.file(name + ".bm3d"), 0);
}

// How far each vertex of the file is from the nearest vertex of the input
double largestDistanceToTheInput(const NodeMesh& node, const Model& model) {
    double worst = 0;
    for (const auto& p : node.positions) {
        double nearest = 1e300;
        for (const Point& q : model.points) {
            const double dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
            nearest = std::min(nearest, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        worst = std::max(worst, nearest);
    }
    return worst;
}

// The sphere of the golden models has 5808 triangles whose edges are about 0.07 long
TEST(LeafPrecision, aSphereOfASingleNodeKeepsItsTrianglesFacingOutwards) {
    const Model model = sphereModel(22);
    ASSERT_EQ(model.triangles.size(), 12u * 22 * 22);
    const NodeMesh node = buildAndRead(model, "sphere");
    ASSERT_FALSE(node.positions.empty());
    ASSERT_EQ(node.indices.size() % 3, 0u);

    int inward = 0;
    double smallestCosine = 1;
    for (size_t t = 0; t + 2 < node.indices.size(); t += 3) {
        const auto& a = node.positions[node.indices[t]];
        const auto& b = node.positions[node.indices[t + 1]];
        const auto& c = node.positions[node.indices[t + 2]];
        const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const double normal[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
        // the outward direction of the sphere at the centre of the triangle
        const double centre[3] = {(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3};
        const double lengthN = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        const double lengthC = std::sqrt(centre[0] * centre[0] + centre[1] * centre[1] + centre[2] * centre[2]);
        const double cosine = lengthN > 0 ? (normal[0] * centre[0] + normal[1] * centre[1] + normal[2] * centre[2]) / (lengthN * lengthC) : 0;
        if (!(cosine > 0))
            inward++;
        smallestCosine = std::min(smallestCosine, cosine);
    }
    EXPECT_EQ(inward, 0) << "triangles flipped or flattened, of " << node.indices.size() / 3;
    EXPECT_GT(smallestCosine, 0.9) << "a triangle is turned by more than 25 degrees from the sphere";

    // the vertices are where the input put them, to a tenth of a mean edge (0.074); quantised to a step
    // of 2^-8 a coordinate is off by less than the step, so a vertex by 0.0068 at most
    EXPECT_LT(largestDistanceToTheInput(node, model), 0.0074);
}

TEST(LeafPrecision, theChamferOfAPartKeepsItsSharpEdges) {
    const Model model = chamferedBlock();
    const NodeMesh node = buildAndRead(model, "block");
    ASSERT_FALSE(node.positions.empty());
    // the chamfer is 0.0707 long: its corners are within a tenth of that of the input's
    EXPECT_LT(largestDistanceToTheInput(node, model), 0.0707 / 10);
}

}  // namespace
