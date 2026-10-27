// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The warnings of a build go to the logger of the BuildOptions and nowhere else: the library writes
// nothing to the standard streams.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "model_files.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

// A binary STL with bytes after its last triangle, which are ignored with a warning
void writePaddedStl(const TempDir& dir) {
    writeBinaryFile(dir.file("padded.stl"), binaryStl(waveTriangles(20)) + std::string(7, 'x'));
}

TEST(BuildLogger, aWarningOfALoaderIsGivenToTheLogger) {
    TempDir dir;
    writePaddedStl(dir);
    std::vector<std::string> warnings;
    bm3d::BuildOptions options;
    options.logger = [&](const std::string& message) { warnings.push_back(message); };
    bm3d::build(dir.file("padded.stl"), dir.file("padded.bm3d"), options);
    ASSERT_EQ(warnings.size(), 1u);
    // the text only: the prefix and the line end are the business of whoever prints it
    EXPECT_EQ(warnings[0].find("stl file "), 0u);
    EXPECT_NE(warnings[0].find(" holds 7 bytes after its 800 triangles; they are ignored"), std::string::npos);
}

TEST(BuildLogger, aBuildWithoutALoggerWritesNothingToTheStandardStreams) {
    TempDir dir;
    writePaddedStl(dir);
    testing::internal::CaptureStderr();
    testing::internal::CaptureStdout();
    bm3d::build(dir.file("padded.stl"), dir.file("padded.bm3d"));
    EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "");
}

TEST(BuildLogger, aBuildWithNoWarningNeverCallsTheLogger) {
    TempDir dir;
    writeTextFile(dir.file("wave.obj"), objOf(waveTriangles(20)));
    int calls = 0;
    bm3d::BuildOptions options;
    options.logger = [&](const std::string&) { ++calls; };
    bm3d::build(dir.file("wave.obj"), dir.file("wave.bm3d"), options);
    EXPECT_EQ(calls, 0);
}

}  // namespace
