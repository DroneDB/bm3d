// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A build that fails leaves nothing behind: no output under the name asked for, no half file
// next to it, no scratch file of the build, and a file an earlier build left there is as it was.
// A build that works leaves the output alone in its folder. The kind of the error tells the
// caller whose fault it is: the input, the mesh or a texture it names (InvalidInput), or the
// machine and the build (BuildFailed): a failure after the input was read, or memory or scratch
// space that runs out while it is read.

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "env_var_guard.h"
#include "model_files.h"
#include "container_file.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;
using bm3d::ErrorKind;

// The input and the output are in folders of their own, so that whatever the build leaves in
// the folder of the output is seen
struct Folders {
    TempDir dir;
    fs::path in = dir.makeDir("in");
    fs::path out = dir.makeDir("out");
};

// The kind of the Error a build throws; an assertion failure if it throws nothing
ErrorKind failureKind(const fs::path& input, const fs::path& output, std::string* message = nullptr) {
    try {
        bm3d::build(input, output);
    } catch (const bm3d::Error& e) {
        if (message != nullptr)
            *message = e.what();
        return e.kind();
    }
    ADD_FAILURE() << "the build should have failed";
    return ErrorKind::InvalidArgument;
}

// Three vertices and a hundred thousand faces on them: the triangles cannot be split
// into nodes, which is found after the input has been read
void writeCoincidentTriangles(const fs::path& file) {
    std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    for (int i = 0; i < 100000; i++)
        obj += "f 1 2 3\n";
    writeTextFile(file, obj);
}

void expectEmpty(const TempDir& dir, const std::string& folder) {
    EXPECT_TRUE(dir.list(folder).empty()) << "left in " << folder << ": " << ::testing::PrintToString(dir.list(folder));
}

// A file where the temporary folder should be: the OBJ loader keeps its vertex cache in that
// folder, and cannot create it while it reads the input
fs::path writeFileForTemporaryFolder(const TempDir& dir) {
    const fs::path file = dir.file("not-a-folder");
    writeTextFile(file, "");
    return file;
}

TEST(BuildFailure, aTextFileNamedObjIsInvalidInputAndLeavesNoFile) {
    Folders f;
    writeTextFile(f.in / "x.obj", "this is not a model\n");
    std::string message;
    EXPECT_EQ(failureKind(f.in / "x.obj", f.out / "x.bm3d", &message), ErrorKind::InvalidInput);
    EXPECT_FALSE(message.empty());
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, anEmptyFileIsInvalidInputAndLeavesNoFile) {
    Folders f;
    writeTextFile(f.in / "empty.obj", "");
    EXPECT_EQ(failureKind(f.in / "empty.obj", f.out / "empty.bm3d"), ErrorKind::InvalidInput);
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, aModelWithVerticesAndNoFaceIsInvalidInput) {
    Folders f;
    writeTextFile(f.in / "points.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\n");
    EXPECT_EQ(failureKind(f.in / "points.obj", f.out / "points.bm3d"), ErrorKind::InvalidInput);
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, aTruncatedPlyIsInvalidInputAndLeavesNoFile) {
    Folders f;
    // Four vertices and two faces announced; one vertex and no face present
    writeTextFile(f.in / "cut.ply", plyHeader(2).substr(0, plyHeader(2).find("0 0 0\n")) + "0 0 0\n");
    EXPECT_EQ(failureKind(f.in / "cut.ply", f.out / "cut.bm3d"), ErrorKind::InvalidInput);
    expectEmpty(f.dir, "out");
}

// The formats are told by the extension. Whatever else a file is named, the error says which are
// read, so that a person who gave a .ts, a .tsp or an .off (formats the program once tried) knows
// to convert the model.
TEST(BuildFailure, aFileOfAnUnknownTypeIsInvalidInputThatNamesTheFormatsRead) {
    for (const char* name : {"model.xyz", "model.ts", "model.tsp", "model.off", "model.txt", "model"}) {
        Folders f;
        writeTextFile(f.in / name, kTriangleObj);
        std::string message;
        EXPECT_EQ(failureKind(f.in / name, f.out / "model.bm3d", &message), ErrorKind::InvalidInput) << name;
        for (const char* format : {"obj", "ply", "stl"})
            EXPECT_NE(message.find(format), std::string::npos) << name << ": " << message;
        EXPECT_NE(message.find(name), std::string::npos) << name << ": " << message;
        expectEmpty(f.dir, "out");
    }
}

TEST(BuildFailure, aFolderAsTheInputIsInvalidInput) {
    Folders f;
    EXPECT_EQ(failureKind(f.in, f.out / "model.bm3d"), ErrorKind::InvalidInput);
    expectEmpty(f.dir, "out");
}

// A vertex whose coordinate is NaN or an infinity is refused by every loader, with the place in the
// file: the model has no box to divide. Each format is first built without it, so that what fails
// is the coordinate.
TEST(BuildFailure, aCoordinateThatIsNotAFiniteNumberIsInvalidInputInEveryFormat) {
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const auto clean = waveTriangles(2);
    const struct {
        const char* name;
        std::function<std::string(const std::vector<Triangle>&)> write;
        const char* message;  // what the error says besides the file
    } formats[] = {
        {"model.obj", objOf, "obj file"},
        {"model.ply", plyOf, "ply file"},
        {"binary.ply", binaryPlyOf, "not a finite number"},
        {"ascii.stl", asciiStl, "finite"},
        {"binary.stl", [](const std::vector<Triangle>& t) { return binaryStl(t); }, "not a finite number"},
    };
    for (const auto& format : formats) {
        Folders f;
        writeBinaryFile(f.in / format.name, format.write(clean));
        ASSERT_NO_THROW(bm3d::build(f.in / format.name, f.out / "clean.bm3d")) << format.name;
        for (const float bad : {nan, infinity, -infinity}) {
            auto triangles = clean;
            triangles[3][1][2] = bad;
            writeBinaryFile(f.in / format.name, format.write(triangles));
            std::string message;
            EXPECT_EQ(failureKind(f.in / format.name, f.out / "bad.bm3d", &message), ErrorKind::InvalidInput)
                << format.name << " " << bad;
            EXPECT_NE(message.find(format.name), std::string::npos) << message;
            EXPECT_NE(message.find(format.message), std::string::npos) << message;
            EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"clean.bm3d"}) << format.name;
        }
    }
}

// A number too large for a float is an infinity once stored: the OBJ and the PLY with double
// coordinates refuse it as they do the others
TEST(BuildFailure, aCoordinateBeyondTheRangeOfAFloatIsInvalidInput) {
    Folders f;
    writeTextFile(f.in / "big.obj", "v 0 0 0\nv 1 0 0\nv 0 1e39 0\nf 1 2 3\n");
    std::string message;
    EXPECT_EQ(failureKind(f.in / "big.obj", f.out / "big.bm3d", &message), ErrorKind::InvalidInput);
    EXPECT_NE(message.find("vertex 2 of obj file"), std::string::npos) << message;
    EXPECT_NE(message.find("not a finite number"), std::string::npos) << message;

    std::string ply = "ply\nformat binary_little_endian 1.0\nelement vertex 3\nproperty double x\nproperty double y\n"
                      "property double z\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n";
    for (const double v : {0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1e39, 0.0}) {
        uint64_t bits;
        std::memcpy(&bits, &v, 8);
        detail::putLittleEndian(ply, bits);
    }
    detail::putLittleEndian(ply, static_cast<uint8_t>(3));
    for (int i = 0; i < 3; i++)
        detail::putLittleEndian(ply, static_cast<int32_t>(i));
    writeBinaryFile(f.in / "big.ply", ply);
    EXPECT_EQ(failureKind(f.in / "big.ply", f.out / "big2.bm3d", &message), ErrorKind::InvalidInput);
    EXPECT_NE(message.find("vertex 2 of ply file"), std::string::npos) << message;
    expectEmpty(f.dir, "out");
}

// --- STL

// A binary STL that ends before the last triangle its header counts is cut: it is refused, not read
// as far as it goes
TEST(BuildFailure, aTruncatedBinaryStlIsInvalidInputThatSaysSo) {
    Folders f;
    const std::string bytes = binaryStl(waveTriangles(3));
    for (const size_t cut : {size_t(1), size_t(49), size_t(50), bytes.size() - 84}) {
        writeBinaryFile(f.in / "cut.stl", bytes.substr(0, bytes.size() - cut));
        std::string message;
        EXPECT_EQ(failureKind(f.in / "cut.stl", f.out / "cut.bm3d", &message), ErrorKind::InvalidInput) << cut;
        EXPECT_NE(message.find("cut.stl"), std::string::npos) << message;
        EXPECT_NE(message.find("truncated"), std::string::npos) << message;
    }
    expectEmpty(f.dir, "out");
}

// The count of a binary STL is checked against the size of the file before a triangle is read
TEST(BuildFailure, aBinaryStlThatCountsMoreTrianglesThanItHoldsIsInvalidInput) {
    Folders f;
    std::string bytes = binaryStl(waveTriangles(2));
    bytes[80] = bytes[81] = bytes[82] = bytes[83] = char(0xff);  // 4294967295 triangles
    writeBinaryFile(f.in / "many.stl", bytes);
    std::string message;
    EXPECT_EQ(failureKind(f.in / "many.stl", f.out / "many.bm3d", &message), ErrorKind::InvalidInput);
    EXPECT_NE(message.find("many.stl"), std::string::npos) << message;
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, anStlWithNoTriangleIsInvalidInput) {
    Folders f;
    writeBinaryFile(f.in / "none.stl", binaryStl({}));
    writeTextFile(f.in / "ascii.stl", "solid nothing\nendsolid nothing\n");
    writeTextFile(f.in / "empty.stl", "");
    writeTextFile(f.in / "short.stl", "solid");
    for (const char* name : {"none.stl", "ascii.stl", "empty.stl", "short.stl"})
        EXPECT_EQ(failureKind(f.in / name, f.out / "none.bm3d"), ErrorKind::InvalidInput) << name;
    expectEmpty(f.dir, "out");
}

// An ASCII STL that is not what it says names the line that breaks it
TEST(BuildFailure, aBrokenAsciiStlIsInvalidInputThatNamesTheLine) {
    Folders f;
    const std::string facet = "facet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\n";
    const struct {
        const char* name;
        std::string text;
        const char* line;  // where the error is
    } cases[] = {
        {"cut.stl", "solid x\n" + facet + "facet normal 0 0 1\nouter loop\nvertex 0 0 0\n", "line 11"},
        {"word.stl", "solid x\n" + facet + "triangle 0 0 0\n", "line 9"},
        {"number.stl", "solid x\nfacet normal 0 0 1\nouter loop\nvertex 0 zero 0\n", "line 4"},
        {"missing.stl", "solid x\nfacet normal 0 0 1\nouter loop\nvertex 0 0\nvertex 1 0 0\n", "line 5"},  // the numbers go on to the next line
        {"endloop.stl", "solid x\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nvertex 1 1 1\n", "line 7"},
    };
    for (const auto& c : cases) {
        writeTextFile(f.in / c.name, c.text);
        std::string message;
        EXPECT_EQ(failureKind(f.in / c.name, f.out / "x.bm3d", &message), ErrorKind::InvalidInput) << c.name;
        EXPECT_NE(message.find(c.name), std::string::npos) << message;
        EXPECT_NE(message.find(c.line), std::string::npos) << c.name << ": " << message;
    }
    expectEmpty(f.dir, "out");
}

// The builder measures with floats: the squares of the lengths in a model, and the products of its
// edges, have to fit in one. A model larger than a billion units is refused, as the build of one used to
// run for ever (the bounding sphere never ended) or crash.
TEST(BuildFailure, aModelTooLargeToBeMeasuredIsInvalidInput) {
    Folders f;
    for (const float size : {2e9f, 1e12f, 1e30f, 3e38f}) {
        const Triangle triangle{{{0, 0, 0}, {size, 0, 0}, {0, 1, 0}}};
        writeBinaryFile(f.in / "large.stl", binaryStl({triangle}));
        std::string message;
        EXPECT_EQ(failureKind(f.in / "large.stl", f.out / "large.bm3d", &message), ErrorKind::InvalidInput) << size;
        EXPECT_NE(message.find("large.stl"), std::string::npos) << message;
        EXPECT_NE(message.find("units across"), std::string::npos) << message;
    }
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, aLargeModelThatCanBeMeasuredIsBuilt) {
    Folders f;
    const Triangle triangle{{{0, 0, 0}, {6e8f, 0, 0}, {0, 6e8f, 0}}};
    writeBinaryFile(f.in / "large.stl", binaryStl({triangle}));
    ASSERT_NO_THROW(bm3d::build(f.in / "large.stl", f.out / "large.bm3d"));
    EXPECT_EQ(readContainerFile(f.out / "large.bm3d").nface, 1u);
}

// The input is read (and has triangles) before this fails: it is a failure of the build
TEST(BuildFailure, aBuildThatFailsAfterTheInputIsReadIsBuildFailed) {
    Folders f;
    writeCoincidentTriangles(f.in / "same.obj");
    std::string message;
    EXPECT_EQ(failureKind(f.in / "same.obj", f.out / "same.bm3d", &message), ErrorKind::BuildFailed);
    EXPECT_NE(message.find("too small to divide"), std::string::npos) << message;
    expectEmpty(f.dir, "out");
}

// The build runs to the end and fails when the finished file is given its name: the name is
// that of a folder
TEST(BuildFailure, anOutputNamedLikeAFolderFailsAfterTheWholeBuild) {
    Folders f;
    writeTextFile(f.in / "model.obj", kTriangleObj);
    const fs::path target = f.dir.makeDir("out/model.bm3d");
    std::string message;
    EXPECT_EQ(failureKind(f.in / "model.obj", target, &message), ErrorKind::BuildFailed);
    EXPECT_NE(message.find("model.bm3d"), std::string::npos) << message;
    // the folder is still there, empty, and so is the folder around it but for it
    EXPECT_TRUE(fs::is_directory(target));
    EXPECT_TRUE(f.dir.list("out/model.bm3d").empty());
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.bm3d"});
}

TEST(BuildFailure, aFailedBuildLeavesTheEarlierOutputAsItWas) {
    Folders f;
    writeCoincidentTriangles(f.in / "same.obj");
    writeTextFile(f.out / "model.bm3d", "an earlier build");
    EXPECT_EQ(failureKind(f.in / "same.obj", f.out / "model.bm3d"), ErrorKind::BuildFailed);
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.bm3d"});
    EXPECT_EQ(readBinaryFile(f.out / "model.bm3d"), "an earlier build");
}

TEST(BuildFailure, aRefusedInputLeavesTheEarlierOutputAsItWas) {
    Folders f;
    writeTextFile(f.in / "x.obj", "this is not a model\n");
    writeTextFile(f.out / "model.bm3d", "an earlier build");
    EXPECT_EQ(failureKind(f.in / "x.obj", f.out / "model.bm3d"), ErrorKind::InvalidInput);
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.bm3d"});
    EXPECT_EQ(readBinaryFile(f.out / "model.bm3d"), "an earlier build");
}

// The whole model is built and the file the save writes, under a temporary name next to the
// output, cannot be created: a folder has that name. The output of an earlier build is left as
// it was, and no file of this build stays behind.
void expectAFailedSaveLeavesTheEarlierOutput(const std::string& nameInTheWay) {
    Folders f;
    writeTextFile(f.in / "model.obj", kTriangleObj);
    writeTextFile(f.out / "model.bm3d", "an earlier build");
    f.dir.makeDir("out/" + nameInTheWay);
    std::string message;
    EXPECT_EQ(failureKind(f.in / "model.obj", f.out / "model.bm3d", &message), ErrorKind::BuildFailed);
    EXPECT_NE(message.find(nameInTheWay), std::string::npos) << message;
    std::vector<std::string> expected{"model.bm3d", nameInTheWay};
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(f.dir.list("out"), expected);
    EXPECT_TRUE(f.dir.list("out/" + nameInTheWay).empty());
    EXPECT_EQ(readBinaryFile(f.out / "model.bm3d"), "an earlier build");
}

// The builder writes the uncompressed container first
TEST(BuildFailure, aSaveOfTheUncompressedFileThatFailsLeavesTheEarlierOutputAsItWas) {
    expectAFailedSaveLeavesTheEarlierOutput("model.bm3d.tmp.bm3d");
}

// The compression reads that container back and writes the file that becomes the output
TEST(BuildFailure, aSaveOfTheCompressedFileThatFailsLeavesTheEarlierOutputAsItWas) {
    expectAFailedSaveLeavesTheEarlierOutput("model.bm3d.part");
}

// The input is fine; the scratch file that reading it needs cannot be created
TEST(BuildFailure, aTemporaryFolderThatIsAFileWhileTheInputIsReadIsBuildFailed) {
    Folders f;
    writeTextFile(f.in / "model.obj", kTriangleObj);
    const TemporaryFolderGuard temporary(pathToUtf8(writeFileForTemporaryFolder(f.dir)));
    std::string message;
    EXPECT_EQ(failureKind(f.in / "model.obj", f.out / "model.bm3d", &message), ErrorKind::BuildFailed);
    EXPECT_FALSE(message.empty());
    expectEmpty(f.dir, "out");
}

#ifndef _WIN32
// The same, with a temporary folder that is there but cannot be written to (which does not hold
// for root, who can write anywhere)
TEST(BuildFailure, aTemporaryFolderThatCannotBeWrittenWhileTheInputIsReadIsBuildFailed) {
    if (geteuid() == 0)
        GTEST_SKIP() << "root can write to any folder";
    Folders f;
    writeTextFile(f.in / "model.obj", kTriangleObj);
    const fs::path readOnly = f.dir.makeDir("read-only");
    fs::permissions(readOnly, fs::perms::owner_read | fs::perms::owner_exec);
    std::string message;
    {
        const TemporaryFolderGuard temporary(pathToUtf8(readOnly));
        EXPECT_EQ(failureKind(f.in / "model.obj", f.out / "model.bm3d", &message), ErrorKind::BuildFailed);
    }
    fs::permissions(readOnly, fs::perms::owner_all);
    EXPECT_NE(message.find(pathToUtf8(readOnly)), std::string::npos) << message;
    expectEmpty(f.dir, "out");
    expectEmpty(f.dir, "read-only");
}
#endif

// A PLY keeps its scratch files next to the output and does not need the temporary folder: what
// is wrong with it is still the input's fault
TEST(BuildFailure, aTruncatedPlyIsInvalidInputWhateverTheTemporaryFolder) {
    Folders f;
    writeTextFile(f.in / "cut.ply", plyHeader(2).substr(0, plyHeader(2).find("0 0 0\n")) + "0 0 0\n");
    const TemporaryFolderGuard temporary(pathToUtf8(writeFileForTemporaryFolder(f.dir)));
    EXPECT_EQ(failureKind(f.in / "cut.ply", f.out / "cut.bm3d"), ErrorKind::InvalidInput);
    expectEmpty(f.dir, "out");
}

// The textures the materials name are part of the input
TEST(BuildFailure, aTextureTheMaterialNamesThatIsMissingIsInvalidInput) {
    Folders f;
    writeTexturedModel(f.in, "texture.png");
    std::string message;
    EXPECT_EQ(failureKind(f.in / "model.obj", f.out / "model.bm3d", &message), ErrorKind::InvalidInput);
    EXPECT_NE(message.find("texture.png"), std::string::npos) << message;
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, aTextureThatCannotBeDecodedIsInvalidInput) {
    Folders f;
    writeTexturedModel(f.in, "texture.png");
    writeTextFile(f.in / "texture.png", "this is not a picture");
    std::string message;
    EXPECT_EQ(failureKind(f.in / "model.obj", f.out / "model.bm3d", &message), ErrorKind::InvalidInput);
    EXPECT_NE(message.find("texture.png"), std::string::npos) << message;
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, aBuildThatWorksLeavesTheOutputAloneInItsFolder) {
    Folders f;
    writePng(f.in / "texture.png", gradientRaster(3));
    writeTexturedModel(f.in, "texture.png");
    ASSERT_NO_THROW(bm3d::build(f.in / "model.obj", f.out / "model.bm3d"));
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.bm3d"});
    EXPECT_EQ(readContainerFile(f.out / "model.bm3d").magic, kContainerMagic);
}

TEST(BuildFailure, aBuildThatWorksReplacesTheEarlierOutput) {
    Folders f;
    writeTextFile(f.in / "model.obj", kTriangleObj);
    writeTextFile(f.out / "model.bm3d", "an earlier build");
    ASSERT_NO_THROW(bm3d::build(f.in / "model.obj", f.out / "model.bm3d"));
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.bm3d"});
    EXPECT_EQ(readContainerFile(f.out / "model.bm3d").nface, 1u);
}

}  // namespace
