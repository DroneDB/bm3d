// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Paths are UTF-8 on the command line on every platform, whatever the code page of the system:
// a model under a Cyrillic or CJK folder is built, with the texture, the metadata and the output
// there too.

#include <string>

#include <gtest/gtest.h>

#include "cli_support.h"
#include "model_files.h"
#include "container_file.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

TEST(CliPaths, aModelAndItsOutputUnderFoldersInOtherAlphabetsAreBuilt) {
    TempDir dir;
    const fs::path in = dir.makeDir("модель/模型");
    dir.makeDir("выход/出力");
    writeTextFile(in / pathFromUtf8("модель.obj"), kTriangleObj);

    const ProcessResult r = runBm3d({pathToUtf8(in / pathFromUtf8("модель.obj")), pathToUtf8(dir.file("выход/出力/результат.bm3d"))});
    EXPECT_EQ(r.exitCode, 0) << r.output();
    EXPECT_EQ(dir.list("выход/出力"), std::vector<std::string>{"результат.bm3d"});
    EXPECT_EQ(readContainerFile(dir.file("выход/出力/результат.bm3d")).nface, 1u);
}

TEST(CliPaths, theHeartbeatNamesTheInputInUtf8) {
    TempDir dir;
    const fs::path in = dir.makeDir("модель/模型");
    writeTextFile(in / "model.obj", kTriangleObj);
    const std::string input = pathToUtf8(in / "model.obj");
    const ProcessResult r = runBm3d({input, pathToUtf8(dir.file("out.bm3d"))});
    ASSERT_EQ(r.exitCode, 0) << r.output();
    EXPECT_EQ(r.out, "bm3d: building " + input + " (0 s)\n");
}

TEST(CliPaths, aTextureAndMetadataWithNamesInOtherAlphabetsAreRead) {
    TempDir dir;
    const fs::path folder = dir.makeDir("模型/модель");
    writePng(folder / pathFromUtf8("текстура.png"), gradientRaster(3));
    writeTexturedModel(folder, "текстура.png");
    writeTextFile(folder / pathFromUtf8("метаданные.json"), "{\"название\":\"模型\"}");

    const std::string output = pathToUtf8(dir.file("результат.bm3d"));
    const ProcessResult r = runBm3d({"--metadata", pathToUtf8(folder / pathFromUtf8("метаданные.json")),
                                     pathToUtf8(folder / "model.obj"), output});
    EXPECT_EQ(r.exitCode, 0) << r.output();
    const ContainerFile h = readContainerFile(dir.file("результат.bm3d"));
    EXPECT_EQ(h.nface, 2u);
    EXPECT_GE(h.nTextures, 1u);
}

TEST(CliPaths, aMissingInputUnderOtherAlphabetsIsNamedInTheMessage) {
    TempDir dir;
    const std::string input = pathToUtf8(dir.file("модель/模型/нет.obj"));
    const ProcessResult r = runBm3d({input, pathToUtf8(dir.file("out.bm3d"))});
    EXPECT_EQ(r.exitCode, 2) << r.output();
    EXPECT_NE(r.err.find(input), std::string::npos) << r.err;
}

TEST(CliPaths, aPathWithSpacesAndQuotesReachesTheProgramAsOneArgument) {
    TempDir dir;
    const fs::path in = dir.makeDir("my models/it's a model");
    writeTextFile(in / "a model.obj", kTriangleObj);
    const ProcessResult r = runBm3d({pathToUtf8(in / "a model.obj"), pathToUtf8(dir.file("my output.bm3d"))});
    EXPECT_EQ(r.exitCode, 0) << r.output();
    EXPECT_TRUE(fs::exists(dir.file("my output.bm3d")));
}

}  // namespace
