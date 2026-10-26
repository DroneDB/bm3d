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
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "env_var_guard.h"
#include "model_files.h"
#include "nexus_file.h"
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

std::string contentOf(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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
    EXPECT_EQ(failureKind(f.in / "x.obj", f.out / "x.nxz", &message), ErrorKind::InvalidInput);
    EXPECT_FALSE(message.empty());
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, anEmptyFileIsInvalidInputAndLeavesNoFile) {
    Folders f;
    writeTextFile(f.in / "empty.obj", "");
    EXPECT_EQ(failureKind(f.in / "empty.obj", f.out / "empty.nxz"), ErrorKind::InvalidInput);
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, aModelWithVerticesAndNoFaceIsInvalidInput) {
    Folders f;
    writeTextFile(f.in / "points.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\n");
    EXPECT_EQ(failureKind(f.in / "points.obj", f.out / "points.nxz"), ErrorKind::InvalidInput);
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, aTruncatedPlyIsInvalidInputAndLeavesNoFile) {
    Folders f;
    // Four vertices and two faces announced; one vertex and no face present
    writeTextFile(f.in / "cut.ply", plyHeader(2).substr(0, plyHeader(2).find("0 0 0\n")) + "0 0 0\n");
    EXPECT_EQ(failureKind(f.in / "cut.ply", f.out / "cut.nxz"), ErrorKind::InvalidInput);
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, aFileOfAnUnknownTypeIsInvalidInput) {
    Folders f;
    writeTextFile(f.in / "model.xyz", kTriangleObj);
    EXPECT_EQ(failureKind(f.in / "model.xyz", f.out / "model.nxz"), ErrorKind::InvalidInput);
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, aFolderAsTheInputIsInvalidInput) {
    Folders f;
    EXPECT_EQ(failureKind(f.in, f.out / "model.nxz"), ErrorKind::InvalidInput);
    expectEmpty(f.dir, "out");
}

// The input is read (and has triangles) before this fails: it is a failure of the build
TEST(BuildFailure, aBuildThatFailsAfterTheInputIsReadIsBuildFailed) {
    Folders f;
    writeCoincidentTriangles(f.in / "same.obj");
    std::string message;
    EXPECT_EQ(failureKind(f.in / "same.obj", f.out / "same.nxz", &message), ErrorKind::BuildFailed);
    EXPECT_NE(message.find("too small to divide"), std::string::npos) << message;
    expectEmpty(f.dir, "out");
}

// The build runs to the end and fails when the finished file is given its name: the name is
// that of a folder
TEST(BuildFailure, anOutputNamedLikeAFolderFailsAfterTheWholeBuild) {
    Folders f;
    writeTextFile(f.in / "model.obj", kTriangleObj);
    const fs::path target = f.dir.makeDir("out/model.nxz");
    std::string message;
    EXPECT_EQ(failureKind(f.in / "model.obj", target, &message), ErrorKind::BuildFailed);
    EXPECT_NE(message.find("model.nxz"), std::string::npos) << message;
    // the folder is still there, empty, and so is the folder around it but for it
    EXPECT_TRUE(fs::is_directory(target));
    EXPECT_TRUE(f.dir.list("out/model.nxz").empty());
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.nxz"});
}

TEST(BuildFailure, aFailedBuildLeavesTheEarlierOutputAsItWas) {
    Folders f;
    writeCoincidentTriangles(f.in / "same.obj");
    writeTextFile(f.out / "model.nxz", "an earlier build");
    EXPECT_EQ(failureKind(f.in / "same.obj", f.out / "model.nxz"), ErrorKind::BuildFailed);
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.nxz"});
    EXPECT_EQ(contentOf(f.out / "model.nxz"), "an earlier build");
}

TEST(BuildFailure, aRefusedInputLeavesTheEarlierOutputAsItWas) {
    Folders f;
    writeTextFile(f.in / "x.obj", "this is not a model\n");
    writeTextFile(f.out / "model.nxz", "an earlier build");
    EXPECT_EQ(failureKind(f.in / "x.obj", f.out / "model.nxz"), ErrorKind::InvalidInput);
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.nxz"});
    EXPECT_EQ(contentOf(f.out / "model.nxz"), "an earlier build");
}

// The whole model is built and the file the save writes, under a temporary name next to the
// output, cannot be created: a folder has that name. The output of an earlier build is left as
// it was, and no file of this build stays behind.
void expectAFailedSaveLeavesTheEarlierOutput(const std::string& nameInTheWay) {
    Folders f;
    writeTextFile(f.in / "model.obj", kTriangleObj);
    writeTextFile(f.out / "model.nxz", "an earlier build");
    f.dir.makeDir("out/" + nameInTheWay);
    std::string message;
    EXPECT_EQ(failureKind(f.in / "model.obj", f.out / "model.nxz", &message), ErrorKind::BuildFailed);
    EXPECT_NE(message.find(nameInTheWay), std::string::npos) << message;
    std::vector<std::string> expected{"model.nxz", nameInTheWay};
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(f.dir.list("out"), expected);
    EXPECT_TRUE(f.dir.list("out/" + nameInTheWay).empty());
    EXPECT_EQ(contentOf(f.out / "model.nxz"), "an earlier build");
}

// The builder writes the uncompressed container first
TEST(BuildFailure, aSaveOfTheUncompressedFileThatFailsLeavesTheEarlierOutputAsItWas) {
    expectAFailedSaveLeavesTheEarlierOutput("model.nxz.tmp.nxs");
}

// The compression reads that container back and writes the file that becomes the output
TEST(BuildFailure, aSaveOfTheCompressedFileThatFailsLeavesTheEarlierOutputAsItWas) {
    expectAFailedSaveLeavesTheEarlierOutput("model.nxz.part");
}

// The input is fine; the scratch file that reading it needs cannot be created
TEST(BuildFailure, aTemporaryFolderThatIsAFileWhileTheInputIsReadIsBuildFailed) {
    Folders f;
    writeTextFile(f.in / "model.obj", kTriangleObj);
    const TemporaryFolderGuard temporary(pathToUtf8(writeFileForTemporaryFolder(f.dir)));
    std::string message;
    EXPECT_EQ(failureKind(f.in / "model.obj", f.out / "model.nxz", &message), ErrorKind::BuildFailed);
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
        EXPECT_EQ(failureKind(f.in / "model.obj", f.out / "model.nxz", &message), ErrorKind::BuildFailed);
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
    EXPECT_EQ(failureKind(f.in / "cut.ply", f.out / "cut.nxz"), ErrorKind::InvalidInput);
    expectEmpty(f.dir, "out");
}

// The textures the materials name are part of the input
TEST(BuildFailure, aTextureTheMaterialNamesThatIsMissingIsInvalidInput) {
    Folders f;
    writeTexturedModel(f.in, "texture.png");
    std::string message;
    EXPECT_EQ(failureKind(f.in / "model.obj", f.out / "model.nxz", &message), ErrorKind::InvalidInput);
    EXPECT_NE(message.find("texture.png"), std::string::npos) << message;
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, aTextureThatCannotBeDecodedIsInvalidInput) {
    Folders f;
    writeTexturedModel(f.in, "texture.png");
    writeTextFile(f.in / "texture.png", "this is not a picture");
    std::string message;
    EXPECT_EQ(failureKind(f.in / "model.obj", f.out / "model.nxz", &message), ErrorKind::InvalidInput);
    EXPECT_NE(message.find("texture.png"), std::string::npos) << message;
    expectEmpty(f.dir, "out");
}

TEST(BuildFailure, aBuildThatWorksLeavesTheOutputAloneInItsFolder) {
    Folders f;
    writePng(f.in / "texture.png", gradientRaster(3));
    writeTexturedModel(f.in, "texture.png");
    ASSERT_NO_THROW(bm3d::build(f.in / "model.obj", f.out / "model.nxz"));
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.nxz"});
    EXPECT_EQ(readNexusFile(f.out / "model.nxz").magic, kNexusMagic);
}

TEST(BuildFailure, aBuildThatWorksReplacesTheEarlierOutput) {
    Folders f;
    writeTextFile(f.in / "model.obj", kTriangleObj);
    writeTextFile(f.out / "model.nxz", "an earlier build");
    ASSERT_NO_THROW(bm3d::build(f.in / "model.obj", f.out / "model.nxz"));
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.nxz"});
    EXPECT_EQ(readNexusFile(f.out / "model.nxz").nface, 1u);
}

}  // namespace
