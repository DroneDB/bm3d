// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The coarser nodes of a model are made by simplifying the finer ones, and a simplified node must
// stay on the model: no vertex of any node lies outside the bounding box of the input (a little
// larger, as the vertices are quantised and the surface may round a corner). The meshes are the
// ones where the quadric of a collapse is nearly singular: a flat surface with noise, a strip so
// thin that its planes are all nearly the same, and a cube whose faces are flat and meet at the
// edges.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "bm3d_file.h"
#include "model_files.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

using Point = std::array<double, 3>;

// A mesh as an indexed set of triangles, written as an OBJ with or without texture coordinates
struct Mesh {
    std::vector<Point> points;
    std::vector<std::array<int, 3>> triangles;
    bool textured = false;  // a "vt" for each point: its x and y over the extent of the mesh

    int add(double x, double y, double z) {
        points.push_back({x, y, z});
        return int(points.size()) - 1;
    }
};

// A noise in -0.5..0.5 from integers only, the same on every machine
double noise(int a, int b) {
    uint32_t h = uint32_t(a) * 2654435761u ^ uint32_t(b) * 40503u ^ 0x9e3779b9u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return double(h & 0xffff) / 65535.0 - 0.5;
}

// A grid of nx x ny squares over [0, sx] x [0, sy], each point moved in z by up to `amplitude` / 2
Mesh gridMesh(int nx, int ny, double sx, double sy, double amplitude) {
    Mesh mesh;
    for (int y = 0; y <= ny; y++)
        for (int x = 0; x <= nx; x++)
            mesh.add(sx * x / nx, sy * y / ny, amplitude * noise(x, y));
    const auto at = [nx](int x, int y) { return y * (nx + 1) + x; };
    for (int y = 0; y < ny; y++)
        for (int x = 0; x < nx; x++) {
            mesh.triangles.push_back({at(x, y), at(x + 1, y), at(x + 1, y + 1)});
            mesh.triangles.push_back({at(x, y), at(x + 1, y + 1), at(x, y + 1)});
        }
    return mesh;
}

// A cube of side 1 whose six faces are cut into n x n squares
Mesh cubeMesh(int n) {
    Mesh mesh;
    std::vector<int> ids;  // the point of every (i, j, k) of the surface, -1 for none
    const int side = n + 1;
    ids.assign(side * side * side, -1);
    const auto point = [&](int i, int j, int k) {
        int& id = ids[(i * side + j) * side + k];
        if (id < 0)
            id = mesh.add(double(i) / n, double(j) / n, double(k) / n);
        return id;
    };
    for (int axis = 0; axis < 3; axis++)
        for (int fixed : {0, n})
            for (int a = 0; a < n; a++)
                for (int b = 0; b < n; b++) {
                    const auto at = [&](int da, int db) {
                        int c[3];
                        c[axis] = fixed;
                        c[(axis + 1) % 3] = a + da;
                        c[(axis + 2) % 3] = b + db;
                        return point(c[0], c[1], c[2]);
                    };
                    mesh.triangles.push_back({at(0, 0), at(1, 0), at(1, 1)});
                    mesh.triangles.push_back({at(0, 0), at(1, 1), at(0, 1)});
                }
    return mesh;
}

void writeObj(const TempDir& dir, const Mesh& mesh) {
    double low[2] = {1e300, 1e300}, high[2] = {-1e300, -1e300};
    for (const Point& p : mesh.points)
        for (int k = 0; k < 2; k++) {
            low[k] = std::min(low[k], p[k]);
            high[k] = std::max(high[k], p[k]);
        }
    std::ostringstream obj;
    obj.precision(9);
    if (mesh.textured)
        obj << "mtllib model.mtl\n";
    for (const Point& p : mesh.points)
        obj << "v " << p[0] << " " << p[1] << " " << p[2] << "\n";
    if (mesh.textured)
        for (const Point& p : mesh.points)
            obj << "vt " << (p[0] - low[0]) / (high[0] - low[0]) << " " << (p[1] - low[1]) / (high[1] - low[1]) << "\n";
    if (mesh.textured)
        obj << "usemtl m\n";
    for (const auto& t : mesh.triangles) {
        obj << "f";
        for (int corner : t)
            obj << " " << corner + 1 << (mesh.textured ? "/" + std::to_string(corner + 1) : "");
        obj << "\n";
    }
    writeTextFile(dir.file("model.obj"), obj.str());
    if (mesh.textured) {
        writePng(dir.file("model.png"), makeRaster(256, 256, 3, [](int band, int x, int y) { return band == 1 ? x : band == 2 ? y : 128; }));
        writeTextFile(dir.file("model.mtl"), "newmtl m\nKd 1 1 1\nmap_Kd model.png\n");
    }
}

// Builds the mesh and checks the vertices of every node against the box of the input, made larger
// by 1% of its diagonal; returns the number of nodes the file holds
uint32_t expectEveryNodeInsideTheBox(const Mesh& mesh) {
    TempDir dir;
    writeObj(dir, mesh);
    Point low = mesh.points[0], high = mesh.points[0];
    for (const Point& p : mesh.points)
        for (int k = 0; k < 3; k++) {
            low[k] = std::min(low[k], p[k]);
            high[k] = std::max(high[k], p[k]);
        }
    double diagonal = 0;
    for (int k = 0; k < 3; k++)
        diagonal += (high[k] - low[k]) * (high[k] - low[k]);
    const double slack = 0.01 * std::sqrt(diagonal);

    bm3d::build(dir.file("model.obj"), dir.file("model.bm3d"));
    const Bm3dFile file = readBm3dFile(dir.file("model.bm3d"));
    const uint32_t nodes = file.nNodes - 1;  // the sink is the last
    for (uint32_t node = 0; node < nodes; node++) {
        const NodeMesh decoded = readNode(dir.file("model.bm3d"), node);
        EXPECT_FALSE(decoded.positions.empty()) << "node " << node;
        double worst = 0;  // how far the farthest vertex of the node is outside the box
        for (const auto& p : decoded.positions)
            for (int k = 0; k < 3; k++)
                worst = std::max({worst, low[k] - p[k], p[k] - high[k]});
        EXPECT_LE(worst, slack) << "node " << node << " of " << nodes << " has a vertex " << worst << " outside the box of the input";
    }
    return nodes;
}

TEST(SimplifiedLevels, aNoisyFlatSurfaceStaysInsideItsBox) {
    EXPECT_GT(expectEveryNodeInsideTheBox(gridMesh(160, 160, 10, 10, 1e-3)), 2u);
}

TEST(SimplifiedLevels, aThinStripStaysInsideItsBox) {
    EXPECT_GT(expectEveryNodeInsideTheBox(gridMesh(4, 6000, 0.02, 20, 1e-5)), 2u);
}

TEST(SimplifiedLevels, aCubeStaysInsideItsBox) {
    EXPECT_GT(expectEveryNodeInsideTheBox(cubeMesh(60)), 2u);
}

TEST(SimplifiedLevels, aNoisyFlatSurfaceWithTextureStaysInsideItsBox) {
    Mesh mesh = gridMesh(160, 160, 10, 10, 1e-3);
    mesh.textured = true;
    EXPECT_GT(expectEveryNodeInsideTheBox(mesh), 2u);
}

}  // namespace
