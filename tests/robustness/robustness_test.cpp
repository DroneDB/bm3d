// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inputs that are not what the loaders expect, one case at a time, run through the program: each
// ends in time with the exit code its kind of fault has (2 for an invalid input, 0 where the input is
// still a model), writes nothing on failure and says nothing to a sanitizer. The mutation loop is in
// mutation_test.cpp; the last group here runs the pieces of corto that the sanitizers found at fault.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <corto/bitstream.h>
#include <corto/cstream.h>
#include <corto/normal_attribute.h>
#include <gtest/gtest.h>

#include "model_files.h"
#include "container_file.h"
#include "outcome.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

// A folder for the input and one run on it
struct Case {
    TempDir dir;
    fs::path in = dir.makeDir("in");

    Outcome run(const std::string& name, const std::string& bytes) const {
        writeBinaryFile(in / name, bytes);
        return runOn(dir, in / name);
    }
};

// The run is a good one by the rule for any input and exits with `code`
void expectExit(const Outcome& o, int code, const std::string& context = "") {
    EXPECT_EQ(whatIsWrong(o), "") << context;
    EXPECT_EQ(o.process.exitCode, code) << context << "\n" << o.process.err;
}

// ... and its message holds every one of `parts`
void expectInvalidInput(const Outcome& o, const std::vector<std::string>& parts, const std::string& context = "") {
    expectExit(o, 2, context);
    for (const std::string& part : parts)
        EXPECT_NE(o.process.err.find(part), std::string::npos) << context << ": " << part << "\n" << o.process.err;
}

uint64_t facesOf(const Outcome& o) {
    return readContainerFile(o.output).nface;
}

// --- PLY face lists

// A PLY of `n` vertices on a circle and one face of all of them, the count written as `countType`
std::string polygonPly(int n, const char* countType) {
    std::string s = std::string("ply\nformat ascii 1.0\nelement vertex ") + std::to_string(n) +
                    "\nproperty float x\nproperty float y\nproperty float z\nelement face 1\n" + "property list " +
                    countType + " int vertex_indices\nend_header\n";
    std::string indices;
    for (int i = 0; i < n; i++) {
        const double angle = 6.283185307179586 * i / n;
        s += std::to_string(std::cos(angle)) + " " + std::to_string(std::sin(angle)) + " 0\n";
        indices += " " + std::to_string(i);
    }
    return s + std::to_string(n) + indices + "\n";
}

// A binary little-endian PLY of a square with one face of four vertices, whose length is written
// with `bytes` bytes of `length` as the type `countType` says (the value may not fit the type's idea
// of a length: that is the point)
std::string squarePlyWithLength(const char* countType, int bytes, uint64_t length) {
    std::string s = std::string("ply\nformat binary_little_endian 1.0\nelement vertex 4\nproperty float x\n"
                                "property float y\nproperty float z\nelement face 1\nproperty list ") +
                    countType + " int vertex_indices\nend_header\n";
    for (const float c : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 0.f}) {
        uint32_t bits;
        std::memcpy(&bits, &c, 4);
        detail::putLittleEndian(s, bits);
    }
    for (int i = 0; i < bytes; i++)
        s.push_back(char((length >> (8 * i)) & 0xff));
    for (int32_t i = 0; i < 4; i++)
        detail::putLittleEndian(s, i);
    return s;
}

// The face list of a polygon has up to 255 vertices: a face of that many is cut into a fan of
// triangles, one of more is refused with the number it has, and nothing is allocated for the length
// a binary file declares
TEST(PlyFaceLists, aFaceOfTheMostVerticesIsAFanOfTriangles) {
    Case c;
    for (const int n : {3, 4, 254, 255}) {
        const Outcome o = c.run("polygon.ply", polygonPly(n, "ushort"));
        expectExit(o, 0, std::to_string(n));
        EXPECT_EQ(facesOf(o), uint64_t(n - 2)) << n;
    }
}

TEST(PlyFaceLists, aFaceOfMoreVerticesThanTheBoundIsInvalidInputThatSaysSo) {
    Case c;
    for (const int n : {256, 257, 1000, 5000}) {
        const Outcome o = c.run("polygon.ply", polygonPly(n, "ushort"));
        expectInvalidInput(o, {"polygon.ply", "has " + std::to_string(n) + " vertex indices", "3 to 255"}, std::to_string(n));
    }
}

// A binary file declares the length of a list in any integer type, and the length can be anything
TEST(PlyFaceLists, aBinaryLengthBeyondTheBoundOrNegativeIsInvalidInputWithNoAllocation) {
    const struct {
        const char* type;
        int bytes;
        uint64_t length;
    } cases[] = {{"uchar", 1, 255},          {"ushort", 2, 65535},      {"uint", 4, 4294967295u},
                 {"uint", 4, 2147483648u},   {"int", 4, 2147483647u},   {"int", 4, 4294967295u}};  // -1 as an int
    Case c;
    for (const auto& k : cases) {
        const Outcome o = c.run("length.ply", squarePlyWithLength(k.type, k.bytes, k.length));
        expectInvalidInput(o, {"length.ply"}, std::string(k.type) + " " + std::to_string(k.length));
    }
    // the same square with a length of 4 is the control
    expectExit(c.run("length.ply", squarePlyWithLength("int", 4, 4)), 0);
}

// The length and the index of a face list are of any integer type; each pair gives the same model
template <typename Count, typename Index>
void expectTheSquare(const Case& c, const char* countType, const char* indexType) {
    const std::string name = std::string(countType) + "_" + indexType + ".ply";
    writeBinaryPly<Count, Index>(c.in / name, countType, indexType);
    const Outcome o = runOn(c.dir, c.in / name);
    expectExit(o, 0, name);
    const ContainerFile h = readContainerFile(o.output);
    EXPECT_EQ(h.nface, 2u) << name;
    EXPECT_EQ(h.nvert, 4u) << name;
}

TEST(PlyFaceLists, everyCountTypeAndIndexTypeBuildsTheSameSquare) {
    Case c;
    expectTheSquare<uint8_t, int32_t>(c, "uchar", "int");
    expectTheSquare<uint8_t, uint32_t>(c, "uchar", "uint");
    expectTheSquare<uint8_t, int16_t>(c, "uchar", "short");
    expectTheSquare<uint16_t, int32_t>(c, "ushort", "int");
    expectTheSquare<uint16_t, uint32_t>(c, "ushort", "uint");
    expectTheSquare<uint16_t, int16_t>(c, "ushort", "short");
    expectTheSquare<uint32_t, int32_t>(c, "uint", "int");
    expectTheSquare<uint32_t, uint32_t>(c, "uint", "uint");
    expectTheSquare<uint32_t, int16_t>(c, "uint", "short");
    expectTheSquare<int32_t, int32_t>(c, "int", "int");
    expectTheSquare<int32_t, uint32_t>(c, "int", "uint");
    expectTheSquare<int32_t, int16_t>(c, "int", "short");
}

// --- PLY header

std::string plyWithComment(const std::string& comment) {
    return "ply\nformat ascii 1.0\n" + comment +
           "\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\n"
           "element face 2\nproperty list uchar int vertex_indices\nend_header\n"
           "0 0 0\n1 0 0\n1 1 0\n0 1 0\n3 0 1 2\n3 0 2 3\n";
}

// plylib reads a header line into 512 bytes. A longer one used to be cut in two, and a texture name in
// the rest of it overran the buffer that copies it; now the file is refused with the reason.
TEST(PlyHeader, aCommentOfTheMostCharactersABuildsAndALongerOneIsInvalidInputThatSaysSo) {
    Case c;
    for (const int length : {10, 200, 480}) {
        expectExit(c.run("c.ply", plyWithComment("comment " + std::string(size_t(length), 'x'))), 0, std::to_string(length));
        expectExit(c.run("c.ply", plyWithComment("comment TextureFile " + std::string(size_t(length), 'x') + ".png")), 0,
                   "texture " + std::to_string(length));
    }
    for (const int length : {1100, 1500, 5000, 100000}) {
        expectInvalidInput(c.run("c.ply", plyWithComment("comment " + std::string(size_t(length), 'x'))),
                           {"c.ply", "header line", "511"}, std::to_string(length));
        expectInvalidInput(c.run("c.ply", plyWithComment("comment TextureFile " + std::string(size_t(length), 'x') + ".png")),
                           {"c.ply", "header line", "511"}, "texture " + std::to_string(length));
    }
}

// The count of an element is a whole number from 0 to INT_MAX; a header that ends before it, or has
// a number that is negative, too large or not a number, is refused (it crashed, or asserted)
TEST(PlyHeader, anElementCountThatIsMissingNegativeOrNotANumberIsInvalidInput) {
    Case c;
    const std::string rest =
        "\nproperty float x\nproperty float y\nproperty float z\nelement face 1\n"
        "property list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";
    for (const char* count : {"-1", "-2147483648", "2147483648", "99999999999", "abc", "4x", "0x3", "1e3", "+-3"}) {
        expectInvalidInput(c.run("count.ply", std::string("ply\nformat ascii 1.0\nelement vertex ") + count + rest), {"count.ply"},
                           count);
        expectInvalidInput(c.run("count.ply", "ply\nformat ascii 1.0\nelement vertex 3" + rest.substr(0, rest.find("1\nprop")) +
                                                  count + rest.substr(rest.find("1\nprop") + 1)),
                           {"count.ply"}, std::string("face ") + count);
    }
    // the header ends inside the line, or the line has only the name
    for (const std::string& header : {"ply\nformat ascii 1.0\nelement vertex ", "ply\nformat ascii 1.0\nelement vertex",
                                      "ply\nformat ascii 1.0\nelement ", "ply\nformat ascii 1.0\nelement"})
        expectInvalidInput(c.run("count.ply", header), {"count.ply"}, header);
}

TEST(PlyHeader, aTextureNamedInALongCommentIsFoundAndUsed) {
    Case c;
    const std::string name = std::string(60, 'a') + ".png";
    writePng(c.in / name, gradientRaster(3));
    writeTextFile(c.in / "t.ply",
                  "ply\nformat ascii 1.0\ncomment " + std::string(300, 'x') + "\ncomment TextureFile " + name +
                      "\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\n"
                      "property float s\nproperty float t\nelement face 2\nproperty list uchar int vertex_indices\n"
                      "end_header\n0 0 0 0 0\n1 0 0 1 0\n1 1 0 1 1\n0 1 0 0 1\n3 0 1 2\n3 0 2 3\n");
    const Outcome o = runOn(c.dir, c.in / "t.ply");
    expectExit(o, 0);
    EXPECT_GE(readContainerFile(o.output).nTextures, 1u);
}

// --- OBJ

// A face line that is not a face: refused with its line, whatever is wrong with it
TEST(ObjFaces, aGarbageFaceIsInvalidInputThatQuotesTheLine) {
    Case c;
    for (const char* face :
         {"f 1 two 99999", "f -77 0 3", "f /1/ /2/ /3/", "f 1/2/3/4/5 2 3", "f", "f 1 2", "f 99999999999999999999 1 2",
          "f 1 2 3 4 5 6 7 8 9 10 11 12", "f 0 1 2", "f 1/1/1 2/2/2 3/3/3", "f 1 2 3x", "f 1 2 -4", "f -2147483649 1 2",
          "f 4294967297 1 2", "f 1 2 3 \x01\x02", "f 1 2 3 1e9999"}) {
        const Outcome o = c.run("bad.obj", std::string("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n") + face + "\n");
        expectExit(o, 2, face);
        EXPECT_NE(o.process.err.find("bad.obj"), std::string::npos) << face << "\n" << o.process.err;
    }
}

// What looks odd but is a face: an empty texture index, and negative indices that count from the end
TEST(ObjFaces, aFaceWithAnEmptyNormalIndexOrNegativeIndicesBuilds) {
    Case c;
    for (const char* face : {"f 1//1 2//1 3//1", "f 1// 2// 3//", "f -1 -2 -3"})
        expectExit(c.run("odd.obj", std::string("v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\n") + face + "\n"), 0, face);
}

// --- texture coordinates

TEST(TextureCoordinates, onlyUIsEnoughForAnObjAndLeavesTheOtherAt0) {
    Case c;
    writePng(c.in / "t.png", gradientRaster(3));
    writeTexturedModel(c.in, "t.png", "vt 0\nvt 1\nvt 1\nvt 0\n");
    const Outcome o = runOn(c.dir, c.in / "model.obj");
    expectExit(o, 0);
    EXPECT_EQ(facesOf(o), 2u);
}

TEST(TextureCoordinates, aPlyWithOnlyOneOfTheTwoCoordinatesBuildsWithoutThem) {
    Case c;
    writePng(c.in / "t.png", gradientRaster(3));
    const Outcome o = c.run("s.ply",
                            "ply\nformat ascii 1.0\ncomment TextureFile t.png\nelement vertex 4\nproperty float x\n"
                            "property float y\nproperty float z\nproperty float s\nelement face 2\n"
                            "property list uchar int vertex_indices\nend_header\n"
                            "0 0 0 0\n1 0 0 1\n1 1 0 1\n0 1 0 0\n3 0 1 2\n3 0 2 3\n");
    expectExit(o, 0);
    EXPECT_EQ(facesOf(o), 2u);
}

// A NaN or an infinity has no pixel to take, and nor has a number beyond what a float can wrap (above
// 2^24 a float is a whole number)
TEST(TextureCoordinates, aCoordinateThatIsNotUsableIsInvalidInputInEveryFormat) {
    Case c;
    writePng(c.in / "t.png", gradientRaster(3));
    writeTextFile(c.in / "m.mtl", "newmtl m\nmap_Kd t.png\n");
    for (const char* vt : {"vt nan 0", "vt 0 inf", "vt -inf", "vt 1e999 0", "vt 0 -1e39", "vt 1e30 0", "vt 16777218 0", "vt 0 -3e38"}) {
        const Outcome o = c.run("uv.obj", std::string("mtllib m.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\n") + vt +
                                              "\nusemtl m\nf 1/1 2/2 3/3\n");
        expectInvalidInput(o, {"uv.obj", "texture coordinate", "16777216"}, vt);
    }

    const std::string vertexHeader =
        "ply\nformat ascii 1.0\ncomment TextureFile t.png\nelement vertex 4\nproperty float x\nproperty float y\n"
        "property float z\nproperty float s\nproperty float t\nelement face 2\n"
        "property list uchar int vertex_indices\nend_header\n";
    for (const char* first : {"nan 0", "0 inf", "-inf 0", "1e39 0", "0 nan", "1e30 0", "0 -16777218"}) {
        const Outcome o = c.run("uv.ply", vertexHeader + "0 0 0 " + first + "\n1 0 0 1 0\n1 1 0 1 1\n0 1 0 0 1\n3 0 1 2\n3 0 2 3\n");
        expectInvalidInput(o, {"uv.ply", "vertex 0", "texture coordinate", "16777216"}, first);
    }

    const std::string faceHeader =
        "ply\nformat ascii 1.0\ncomment TextureFile t.png\nelement vertex 4\nproperty float x\nproperty float y\n"
        "property float z\nelement face 2\nproperty list uchar int vertex_indices\n"
        "property list uchar float texcoord\nend_header\n0 0 0\n1 0 0\n1 1 0\n0 1 0\n";
    for (const char* bad : {"nan", "inf", "-inf", "1e30", "-16777218"}) {
        const Outcome o = c.run("uvf.ply", faceHeader + "3 0 1 2 6 0 0 " + bad + " 0 1 1\n3 0 2 3 6 0 0 1 1 0 1\n");
        expectInvalidInput(o, {"uvf.ply", "face 0", "texture coordinate", "16777216"}, bad);
    }
}

// ... and a coordinate within that range, however far from 0..1, is wrapped into the texture
TEST(TextureCoordinates, aCoordinateFarOutsideTheTextureIsWrappedIntoIt) {
    Case c;
    writePng(c.in / "t.png", gradientRaster(3));
    writeTextFile(c.in / "m.mtl", "newmtl m\nmap_Kd t.png\n");
    expectExit(c.run("uv.obj", "mtllib m.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 16777216 0\nvt -16777216 1000000\nvt 12345.5 -7.25\n"
                               "usemtl m\nf 1/1 2/2 3/3\n"),
               0);
}

// --- triangles that are not a surface

TEST(Triangles, severalCopiesOfATriangleBuildWithTheRestOfTheModel) {
    Case c;
    std::string obj = objOf(waveTriangles(6));
    obj += "f 1 2 3\nf 1 2 3\nf 3 2 1\nf 1 3 2\n";
    const Outcome o = c.run("same.obj", obj);
    expectExit(o, 0);
    EXPECT_GE(facesOf(o), 72u);
}

// A hundred thousand copies of one triangle cannot be divided into nodes. That is found after the
// input is read, so it is a failed build (exit 1) and not a bad input, in time and with no file.
TEST(Triangles, aHundredThousandCoincidentTrianglesFailTheBuildAndLeaveNoFile) {
    Case c;
    std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    for (int i = 0; i < 100000; i++)
        obj += "f 1 2 3\n";
    const Outcome o = c.run("same.obj", obj);
    EXPECT_FALSE(o.process.timedOut);
    EXPECT_EQ(o.process.exitCode, 1) << o.process.err;
    EXPECT_NE(o.process.err.find("too small to divide"), std::string::npos) << o.process.err;
    EXPECT_TRUE(fs::is_empty(o.outputFolder));
}

// Triangles with no area (three points on a line, or one point three times) have no normal; the
// vertices that only they use get the sum of nothing, and the encoder of the normals divided it by
// itself. The model builds and every normal that is read back is a number. A flat grid has the same
// vertices at every level, and its coarse levels collapse triangles to a point or a line as well.
TEST(Triangles, trianglesWithNoAreaBuildAndEveryNormalIsFinite) {
    Case c;
    const int n = 16;
    const auto corner = [n](int x, int y) { return std::array<float, 3>{float(x) / n, float(y) / n, 0.0f}; };
    std::vector<Triangle> triangles;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            triangles.push_back({corner(x, y), corner(x + 1, y), corner(x + 1, y + 1)});
            triangles.push_back({corner(x, y), corner(x + 1, y + 1), corner(x, y + 1)});
        }
    const size_t good = triangles.size();
    for (int x = 0; x + 2 <= n; x++) {
        triangles.push_back({corner(x, 0), corner(x + 1, 0), corner(x + 2, 0)});  // on a line
        triangles.push_back({corner(x, 3), corner(x, 3), corner(x, 3)});         // a point
    }
    for (const std::string& format : {std::string("obj"), std::string("stl")}) {
        const Outcome o = c.run("flat." + format, format == "obj" ? objOf(triangles) : binaryStl(triangles));
        expectExit(o, 0, format);
        const ContainerFile h = readContainerFile(o.output);
        EXPECT_GE(h.nface, good) << format;
        for (uint32_t node = 0; node + 1 < h.nNodes; node++)
            for (const auto& normal : readNode(o.output, node).normals)
                for (const float v : normal)
                    ASSERT_TRUE(std::isfinite(v)) << format << " node " << node;
    }
}

// --- STL

TEST(StlFiles, aBinaryStlCutAnywhereIsInvalidInputAndLeavesNoFile) {
    Case c;
    const std::string bytes = binaryStl(waveTriangles(3));
    for (const size_t keep : {size_t(0), size_t(1), size_t(79), size_t(80), size_t(83), size_t(84), size_t(100), bytes.size() - 50,
                              bytes.size() - 1}) {
        const Outcome o = c.run("cut.stl", bytes.substr(0, keep));
        expectInvalidInput(o, {"cut.stl"}, std::to_string(keep));
    }
}

TEST(StlFiles, anAsciiStlCutAnywhereIsInvalidInputOrTheTrianglesBeforeTheCut) {
    Case c;
    const std::string text = asciiStl(waveTriangles(2));
    for (size_t keep = 0; keep < text.size(); keep += 7) {
        const Outcome o = c.run("cut.stl", text.substr(0, keep));
        EXPECT_EQ(whatIsWrong(o), "") << keep;
        EXPECT_TRUE(o.process.exitCode == 0 || o.process.exitCode == 2) << keep << "\n" << o.process.err;
    }
}

TEST(StlFiles, aBinaryStlWhoseHeaderStartsWithSolidIsBinary) {
    Case c;
    const auto triangles = waveTriangles(5);
    for (const char* header : {"solid", "solid wave exported by a program that writes binary", "solid facet normal 0 0 1"}) {
        const Outcome o = c.run("solid.stl", binaryStl(triangles, header));
        expectExit(o, 0, header);
        EXPECT_EQ(facesOf(o), triangles.size()) << header;
    }
}

// --- corto
//
// The sanitizers found these in the encoder; the patches in third_party/corto-patches fixed them. The
// builds above reach them already, and these call them directly, so a regression names its cause.

TEST(Corto, theOctahedronEncodingOfAVectorOfLengthZeroIsTheCentre) {
    const crt::Point2i zero = crt::NormalAttr::toOcta(crt::Point3f(0.0f, 0.0f, 0.0f), 1 << 10);
    EXPECT_EQ(zero[0], 0);
    EXPECT_EQ(zero[1], 0);
    const crt::Point2i same = crt::NormalAttr::toOcta(crt::Point3i(0, 0, 0), 1 << 10);
    EXPECT_EQ(same[0], 0);
    EXPECT_EQ(same[1], 0);
    // a vector that is not zero is encoded as before: +z is the centre, +x the edge
    const crt::Point2i z = crt::NormalAttr::toOcta(crt::Point3f(0.0f, 0.0f, 3.0f), 1 << 10);
    EXPECT_EQ(z[0], 0);
    EXPECT_EQ(z[1], 0);
    const crt::Point2i x = crt::NormalAttr::toOcta(crt::Point3f(2.0f, 0.0f, 0.0f), 1 << 10);
    EXPECT_EQ(x[0], 1 << 10);
    EXPECT_EQ(x[1], 0);
}

TEST(Corto, aBitStreamReservedForNoWordHoldsWhatIsWrittenAfterwards) {
    crt::BitStream stream(0);
    stream.write(5, 3);
    stream.write(0x1ffff, 17);
    stream.flush();
    ASSERT_EQ(stream.size, 1);
    crt::BitStream reader(stream.size, stream.buffer);
    EXPECT_EQ(reader.read(3), 5u);
    EXPECT_EQ(reader.read(17), 0x1ffffu);

    // a stream reserved again frees its words (LeakSanitizer says so if it does not)
    crt::BitStream again(8);
    again.reserve(16);
    again.reserve(0);
    again.write(1, 1);
}

TEST(Corto, aBitStreamThatFillsAWordReadsBack) {
    crt::BitStream stream(1);
    for (int i = 0; i < 100; i++)
        stream.write(uint32_t(i) & 0x7fffffffu, 31);  // 31 bits per value: the mask of a read is 1 << 31 - 1
    stream.flush();
    crt::BitStream reader(stream.size, stream.buffer);
    for (int i = 0; i < 100; i++)
        ASSERT_EQ(reader.read(31), uint32_t(i)) << i;
}

TEST(Corto, anOutStreamStoresValuesAtAnyOffsetAndAnEmptyArrayAddsNothing) {
    crt::OutStream out;
    out.write<uint8_t>(7);
    out.write<float>(1.5f);        // at offset 1
    out.write<uint16_t>(0x1234);   // at offset 5
    out.write<int32_t>(-2);        // at offset 7
    const size_t before = out.size();
    out.push(nullptr, 0);
    crt::BitStream empty(0);
    out.write(empty);              // nothing was written to it: no words, a null buffer
    EXPECT_EQ(before, 11u);
    EXPECT_GE(out.size(), before);

    const uint8_t* b = out.data();
    float f;
    int32_t i;
    uint16_t s;
    std::memcpy(&f, b + 1, 4);
    std::memcpy(&s, b + 5, 2);
    std::memcpy(&i, b + 7, 4);
    EXPECT_EQ(b[0], 7);
    EXPECT_EQ(f, 1.5f);
    EXPECT_EQ(s, 0x1234);
    EXPECT_EQ(i, -2);
}

}  // namespace
