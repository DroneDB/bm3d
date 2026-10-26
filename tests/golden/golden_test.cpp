// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The golden files: small models built by the program, whose content hashes (docs/FORMAT.md,
// section 9) are in hashes.txt. A change of the code that alters the bytes of a file outside its
// metadata changes a hash and fails here, so it is a decision and not an accident. The files are
// also the fixtures of the other readers of the format: each test copies its file to the folder
// BM3D_GOLDEN_DIR (by default <build folder>/golden), with the hash in <name>.sha256 next to it.
//
// A hash that has to change on purpose is replaced in hashes.txt by the one the failure prints.
// The inputs are written by the tests with coordinates that are exact in a float and no call of the
// mathematical library, so that nothing in them depends on the machine. The files are the same
// bytes with every compiler and standard library: hashes.txt has one hash for each model, and the
// CI checks it on Linux (libstdc++) and on Windows (MSVC). The models with many triangles in a node
// are the ones where an order left to the library would show: equal keys in the sorts of the
// builder and of the encoder, equal costs in the simplification, equal sizes in the texture packing.

#include <array>
#include <cstdlib>
#include <fstream>
#include <cmath>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bm3d_file.h"
#include "cli_support.h"
#include "format/bm3d_reader.h"
#include "format/json_object.h"
#include "model_files.h"
#include "pictures.h"
#include "temp_dir.h"
#include "version_file.h"

namespace {

using namespace bm3dtest;

fs::path fixtureFolder() {
    const char* folder = std::getenv("BM3D_GOLDEN_DIR");
    return pathFromUtf8(folder && *folder ? folder : BM3D_GOLDEN_DEFAULT_DIR);
}

// name -> content hash, from hashes.txt
std::map<std::string, std::string> expectedHashes() {
    std::map<std::string, std::string> hashes;
    std::ifstream in(BM3D_GOLDEN_HASHES);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream words(line);
        std::string name, hash;
        words >> name >> hash;
        hashes[name] = hash;
    }
    return hashes;
}

// Builds `input` with the program, checks the file with the reader and against the hash of
// hashes.txt, and leaves a copy in the folder of the fixtures. Returns the path of the file.
fs::path buildGolden(const TempDir& dir, const std::string& name, const fs::path& input) {
    const fs::path output = dir.file(name + ".bm3d");
    const ProcessResult r = runBm3d({"--threads", "1", pathToUtf8(input), pathToUtf8(output)});
    EXPECT_EQ(r.exitCode, 0) << r.output();

    // every node decodes to the counts of its record, and every texture is a WebP image
    bm3d::detail::Bm3dReader reader = bm3d::detail::Bm3dReader::open(pathToUtf8(output));
    uint64_t vertices = 0, faces = 0;
    for (uint32_t n = 0; n + 1 < reader.nodeCount(); n++) {
        const bm3d::detail::DecodedNode decoded = reader.decodeNode(n);
        EXPECT_EQ(decoded.positions.size(), 3u * reader.nodes()[n].vertexCount) << name << " node " << n;
        EXPECT_EQ(decoded.indices.size(), 3u * reader.nodes()[n].faceCount) << name << " node " << n;
        vertices += reader.nodes()[n].vertexCount;
        faces += reader.nodes()[n].faceCount;
    }
    for (uint32_t t = 0; t < reader.header().textureCount; t++)
        EXPECT_TRUE(isWebp(reader.readTexture(t))) << name << " texture " << t;
    EXPECT_EQ(reader.header().vertexCount, vertices) << name;
    EXPECT_EQ(reader.header().faceCount, faces) << name;

    const std::string hash = reader.contentHash();
    const auto expected = expectedHashes();
    const auto found = expected.find(name);
    if (found == expected.end())
        ADD_FAILURE() << "hashes.txt has no hash for " << name << "; add the line\n" << name << " " << hash;
    else
        EXPECT_EQ(hash, found->second) << name << ": the content of the file changed; if it was meant to, hashes.txt gets\n"
                                       << name << " " << hash;

    const fs::path folder = fixtureFolder();
    std::error_code ec;
    fs::create_directories(folder, ec);
    fs::copy_file(output, folder / (name + ".bm3d"), fs::copy_options::overwrite_existing, ec);
    EXPECT_FALSE(ec) << ec.message();
    writeTextFile(folder / (name + ".sha256"), hash + "  " + name + ".bm3d\n");
    return output;
}

// --- the inputs -----------------------------------------------------------------------------------

// A cube of side 2 with square faces (the OBJ loader cuts each into two triangles)
const char* kCubeObj =
    "v 0 0 0\nv 2 0 0\nv 2 2 0\nv 0 2 0\nv 0 0 2\nv 2 0 2\nv 2 2 2\nv 0 2 2\n"
    "f 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 2 3 7 6\nf 3 4 8 7\nf 4 1 5 8\n";

// A pyramid with a colour for each vertex
const char* kPyramidPly =
    "ply\nformat ascii 1.0\nelement vertex 5\n"
    "property float x\nproperty float y\nproperty float z\n"
    "property uchar red\nproperty uchar green\nproperty uchar blue\nproperty uchar alpha\n"
    "element face 5\nproperty list uchar int vertex_indices\nend_header\n"
    "0 0 0 255 0 0 255\n4 0 0 0 255 0 255\n4 4 0 0 0 255 255\n0 4 0 255 255 0 255\n2 2 3 255 255 255 255\n"
    "4 0 3 2 1\n3 0 1 4\n3 1 2 4\n3 2 3 4\n3 3 0 4\n";

// An n x n grid of squares, two triangles each, over a surface with a height at every corner. The
// coordinates are multiples of 1/8 and the heights come from integer arithmetic: nothing in the
// input depends on the mathematical library of the machine (waveTriangles of the other tests
// does: it uses sin and cos, which differ in the last bit between platforms).
std::vector<Triangle> heightField(int n) {
    const auto corner = [](int x, int y) {
        const int height = (7 * x + 3 * y + (x * y) % 5 + (x / 3) * (y / 2)) % 9;
        return std::array<float, 3>{float(x) * 0.25f, float(y) * 0.25f, float(height) * 0.125f};
    };
    std::vector<Triangle> triangles;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            triangles.push_back({corner(x, y), corner(x + 1, y), corner(x + 1, y + 1)});
            triangles.push_back({corner(x, y), corner(x + 1, y + 1), corner(x, y + 1)});
        }
    return triangles;
}

// A square, and a triangle whose first two corners are the same point: the encoder drops it, and
// the corner that only it has
std::vector<Triangle> degenerateTriangles() {
    return {
        {{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}}},
        {{{0, 0, 0}, {4, 4, 0}, {0, 4, 0}}},
        {{{4, 4, 0}, {4, 4, 0}, {9, 9, 9}}},
    };
}

// An n x n grid over the height field with a texture, each square its own piece of texture: a
// tile of 6 x 6 texels of the 64 x 64 picture (tiles of 8 x 8 texels, 64 of them, used in turn). A
// node holds many pieces of the same size, which the texture of the node packs.
std::string tiledObj(int n) {
    std::ostringstream obj;
    obj << "mtllib tiles.mtl\n";
    for (int y = 0; y <= n; y++)
        for (int x = 0; x <= n; x++) {
            const int height = (7 * x + 3 * y + (x * y) % 5 + (x / 3) * (y / 2)) % 9;
            obj << "v " << float(x) * 0.25f << " " << float(y) * 0.25f << " " << float(height) * 0.125f << "\n";
        }
    // multiples of 1/64, exact in a float and in their decimal form
    const auto texel = [](int t) { return double(t) / 64.0; };
    obj << "usemtl m\n";
    int vt = 1;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            const int tile = (x + y * n) % 64;
            const int u = (tile % 8) * 8 + 1, v = (tile / 8) * 8 + 1;
            obj << "vt " << texel(u) << " " << texel(v) << "\nvt " << texel(u + 6) << " " << texel(v) << "\n"
                << "vt " << texel(u + 6) << " " << texel(v + 6) << "\nvt " << texel(u) << " " << texel(v + 6) << "\n";
            const int a = y * (n + 1) + x + 1, b = a + 1, c = a + n + 2, d = a + n + 1;
            obj << "f " << a << "/" << vt << " " << b << "/" << vt + 1 << " " << c << "/" << vt + 2 << "\n"
                << "f " << a << "/" << vt << " " << c << "/" << vt + 2 << " " << d << "/" << vt + 3 << "\n";
            vt += 4;
        }
    return obj.str();
}

// 40 triangles that share one edge, the pages of a book, the next page facing the other way: an
// edge of more than two faces, which the encoder has to pair
std::string bookObj() {
    std::vector<std::array<int, 3>> ring;  // points on the sides of a square around the spine
    for (int x = -5; x < 5; x++) ring.push_back({x, -5, 2});
    for (int y = -5; y < 5; y++) ring.push_back({5, y, 2});
    for (int x = 5; x > -5; x--) ring.push_back({x, 5, 2});
    for (int y = 5; y > -5; y--) ring.push_back({-5, y, 2});
    std::ostringstream obj;
    obj << "v 0 0 0\nv 0 0 4\n";
    for (const auto& p : ring)
        obj << "v " << p[0] << " " << p[1] << " " << p[2] << "\n";
    for (size_t k = 0; k < ring.size(); k++)
        obj << (k % 2 == 0 ? "f 1 2 " : "f 2 1 ") << k + 3 << "\n";
    return obj.str();
}

// A 129 x 129 grid of squares over a hill, with a texture over it all: the textured model of the
// viewer's tests. The coordinates are multiples of 1/128, the heights integers over 2^16.
std::string terrainObj() {
    const int n = 129;
    std::ostringstream obj;
    obj << std::setprecision(9) << "mtllib terrain.mtl\n";
    for (int y = 0; y <= n; y++)
        for (int x = 0; x <= n; x++) {
            const int dx = x - 64, dy = y - 48;
            const int height = 9000 - (dx * dx + dy * dy) / 2 + ((x * 7 + y * 3) % 13) * 20;
            obj << "v " << double(x) / 128 << " " << double(y) / 128 << " " << double(height) / 65536 << "\n";
        }
    for (int y = 0; y <= n; y++)
        for (int x = 0; x <= n; x++)
            obj << "vt " << double(x) / n << " " << double(y) / n << "\n";
    obj << "usemtl m\n";
    const auto index = [n](int x, int y) { return y * (n + 1) + x + 1; };
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            const int a = index(x, y), b = index(x + 1, y), c = index(x + 1, y + 1), d = index(x, y + 1);
            obj << "f " << a << "/" << a << " " << b << "/" << b << " " << c << "/" << c << "\n"
                << "f " << a << "/" << a << " " << c << "/" << c << " " << d << "/" << d << "\n";
        }
    return obj.str();
}

// A sphere of radius 1 with a colour at each vertex, made from a cube whose faces are cut into
// 22 x 22 squares and whose points are pushed onto the sphere (a division by a square root, which
// is exact to the last bit everywhere, unlike sin and cos). The colour follows the position (red
// = 127.5 + 127 x, and so on). At each corner of the cube a triangle has two equal corners and a
// third point inside the sphere: the encoder drops the triangle and that point, so the file holds
// fewer vertices and triangles than the input (docs/FORMAT.md, section 12).
std::string spherePly() {
    const int k = 11;  // half the squares of a side
    std::vector<std::array<int, 3>> points;
    std::map<std::array<int, 3>, int> numbers;
    const auto point = [&](int x, int y, int z) {
        const std::array<int, 3> p{x, y, z};
        const auto found = numbers.find(p);
        if (found != numbers.end())
            return found->second;
        numbers[p] = int(points.size());
        points.push_back(p);
        return int(points.size()) - 1;
    };
    std::vector<std::array<int, 3>> faces;
    // the six faces: the axis that is +-k, and the two that run over the face, in an order that
    // makes the triangles face outwards
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
                        faces.push_back({p00, p10, p11});
                        faces.push_back({p00, p11, p01});
                    } else {
                        faces.push_back({p00, p11, p10});
                        faces.push_back({p00, p01, p11});
                    }
                }
        }
    const size_t surface = points.size();
    std::vector<std::array<double, 3>> position(surface);
    for (size_t i = 0; i < surface; i++) {
        const auto& p = points[i];
        const double length = std::sqrt(double(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]));
        position[i] = {p[0] / length, p[1] / length, p[2] / length};
    }
    for (int x : {-k, k})
        for (int y : {-k, k})
            for (int z : {-k, k}) {
                const int corner = numbers[{x, y, z}];
                position.push_back({position[corner][0] / 2, position[corner][1] / 2, position[corner][2] / 2});
                faces.push_back({corner, corner, int(position.size()) - 1});
            }

    std::ostringstream ply;
    ply << "ply\nformat ascii 1.0\nelement vertex " << position.size()
        << "\nproperty float x\nproperty float y\nproperty float z\n"
           "property uchar red\nproperty uchar green\nproperty uchar blue\nproperty uchar alpha\n"
           "element face "
        << faces.size() << "\nproperty list uchar int vertex_indices\nend_header\n";
    ply << std::setprecision(9);
    for (const auto& p : position) {
        const float f[3] = {float(p[0]), float(p[1]), float(p[2])};
        ply << f[0] << " " << f[1] << " " << f[2];
        for (float c : f)
            ply << " " << int(std::floor(127.5 + 127.0 * c + 0.5));
        ply << " 255\n";
    }
    for (const auto& f : faces)
        ply << "3 " << f[0] << " " << f[1] << " " << f[2] << "\n";
    return ply.str();
}

TEST(GoldenFixtures, aCubeOfSquareFacesFromAnObj) {
    TempDir dir;
    writeTextFile(dir.file("cube.obj"), kCubeObj);
    const fs::path file = buildGolden(dir, "cube", dir.file("cube.obj"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);  // one node and the sink
    EXPECT_EQ(f.nface, 12u);
    EXPECT_EQ(f.nTextures, 0u);
    EXPECT_FALSE(f.hasColors);

    // the metadata names the program and the input
    const std::string metadata = readMetadata(file);
    EXPECT_NE(metadata.find("\"generator\":\"bm3d " + versionFileText() + "\""), std::string::npos) << metadata;
    EXPECT_NE(metadata.find("\"name\":\"cube.obj\""), std::string::npos) << metadata;
}

TEST(GoldenFixtures, aPyramidWithColoursFromAPly) {
    TempDir dir;
    writeTextFile(dir.file("pyramid.ply"), kPyramidPly);
    const fs::path file = buildGolden(dir, "pyramid-colors", dir.file("pyramid.ply"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);
    EXPECT_EQ(f.nface, 6u);
    EXPECT_TRUE(f.hasColors);
}

TEST(GoldenFixtures, aWavySurfaceFromABinaryStl) {
    TempDir dir;
    writeBinaryFile(dir.file("wave.stl"), binaryStl(heightField(2)));
    const fs::path file = buildGolden(dir, "wave", dir.file("wave.stl"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);
    EXPECT_EQ(f.nface, 8u);
}

// The encoder drops a triangle with two equal corners, and the vertex only it uses: the counts the
// file holds are the encoder's, lower than the ones of the input
TEST(GoldenFixtures, aDegenerateTriangleIsLeftOutOfTheCountsOfTheFile) {
    TempDir dir;
    writeTextFile(dir.file("degenerate.stl"), asciiStl(degenerateTriangles()));
    const fs::path file = buildGolden(dir, "degenerate", dir.file("degenerate.stl"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);
    EXPECT_EQ(f.nface, 2u);  // the input has 3 triangles
    EXPECT_EQ(f.nvert, 4u);  // and 5 different points

    std::vector<bm3d::JsonMember> members;
    ASSERT_EQ(bm3d::jsonObjectMembers(readMetadata(file), members), "");
    std::string source;
    for (const auto& m : members)
        if (m.name == "source")
            source = m.rawValue;
    EXPECT_NE(source.find("\"faces\":3"), std::string::npos) << source;  // what was read, before the encoder
}

TEST(GoldenFixtures, aTexturedSquareFromAnObj) {
    TempDir dir;
    writePng(dir.file("texture.png"), gradientRaster(3));
    writeTexturedModel(dir.path(), "texture.png");
    const fs::path file = buildGolden(dir, "textured", dir.file("model.obj"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);
    EXPECT_EQ(f.nTextures, 1u);
    EXPECT_EQ(f.nface, 2u);
}

// More triangles than a node holds: a hierarchy of several nodes, whose patches name children
TEST(GoldenFixtures, aSurfaceOfSeveralNodesFromABinaryStl) {
    TempDir dir;
    writeBinaryFile(dir.file("grid.stl"), binaryStl(heightField(100)));
    const fs::path file = buildGolden(dir, "grid", dir.file("grid.stl"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_GT(f.nNodes, 3u);
    bm3d::detail::Bm3dReader reader = bm3d::detail::Bm3dReader::open(pathToUtf8(file));
    EXPECT_EQ(reader.rootCount(), 1u);
    EXPECT_FALSE(reader.isLeaf(0));
}

// A textured surface whose node holds thousands of pieces of texture of the same size
TEST(GoldenFixtures, aSurfaceOfManyPiecesOfTextureFromAnObj) {
    TempDir dir;
    writePng(dir.file("tiles.png"), makeRaster(64, 64, 3, [](int band, int x, int y) {
                 return double((x * (11 * band) + y * (5 + 13 * band) + (x / 8) * (y / 8) * 7) % 256);
             }));
    writeTextFile(dir.file("tiles.mtl"), "newmtl m\nKd 1 1 1\nmap_Kd tiles.png\n");
    writeTextFile(dir.file("tiles.obj"), tiledObj(60));
    const fs::path file = buildGolden(dir, "tiles", dir.file("tiles.obj"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_GT(f.nNodes, 2u);
    EXPECT_GT(f.nTextures, 0u);
}

// An edge shared by 40 triangles
TEST(GoldenFixtures, theTrianglesOfOneEdgeFromAnObj) {
    TempDir dir;
    writeTextFile(dir.file("book.obj"), bookObj());
    const fs::path file = buildGolden(dir, "book", dir.file("book.obj"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);
    EXPECT_EQ(f.nface, 40u);
}

// The textured model of the viewer's tests: several nodes and textures, normals
TEST(GoldenFixtures, aTexturedTerrainFromAnObj) {
    TempDir dir;
    writePng(dir.file("terrain.png"), makeRaster(512, 512, 3, [](int band, int x, int y) {
                 const int check = ((x / 32) + (y / 32)) % 2;
                 if (band == 1) return double(80 + check * 100 + (x % 32));
                 if (band == 2) return double(60 + (y / 2) % 120 + check * 40);
                 return double(40 + ((x + y) / 4) % 100);
             }));
    writeTextFile(dir.file("terrain.mtl"), "newmtl m\nKd 1 1 1\nmap_Kd terrain.png\n");
    writeTextFile(dir.file("terrain.obj"), terrainObj());
    const fs::path file = buildGolden(dir, "terrain", dir.file("terrain.obj"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_GT(f.nNodes, 5u);
    EXPECT_GT(f.nTextures, 3u);
}

// The coloured model of the viewer's tests: one node, vertex colours, no texture
TEST(GoldenFixtures, aSphereWithColoursFromAPly) {
    TempDir dir;
    writeTextFile(dir.file("sphere.ply"), spherePly());
    const fs::path file = buildGolden(dir, "sphere", dir.file("sphere.ply"));
    const Bm3dFile f = readBm3dFile(file);
    EXPECT_EQ(f.nNodes, 2u);
    EXPECT_TRUE(f.hasColors);
    EXPECT_EQ(f.nface, 6u * 2 * 22 * 22);  // the 8 triangles with two equal corners are dropped
}

}  // namespace
