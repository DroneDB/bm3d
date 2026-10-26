// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What build() refuses before it reads a file: the arguments are checked first, so these tests
// run with an input that does not exist and tell the refusal of an argument (InvalidArgument)
// from the next thing that fails (the missing input, InvalidInput) without building a model.

#include <climits>
#include <string>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "temp_dir.h"

namespace {

using bm3d::ErrorKind;
using bm3dtest::TempDir;

// The kind of the Error build() throws, or an assertion failure when it throws nothing
ErrorKind kindOfFailure(const bm3dtest::fs::path& input, const bm3dtest::fs::path& output, int threads) {
    bm3d::BuildOptions options;
    options.threads = threads;
    try {
        bm3d::build(input, output, options);
    } catch (const bm3d::Error& e) {
        EXPECT_GT(std::string(e.what()).size(), 0u);
        return e.kind();
    }
    ADD_FAILURE() << "build() did not throw";
    return ErrorKind::BuildFailed;
}

TEST(BuildOptions, theDefaultIsOneThreadPerCore) {
    EXPECT_EQ(bm3d::BuildOptions{}.threads, 0);
}

TEST(BuildOptions, aThreadCountFromZeroTo4096PassesTheArgumentCheck) {
    TempDir dir;
    // The input is missing, which is the failure that comes after the arguments are accepted
    for (const int threads : {0, 1, 2, 64, 4096})
        EXPECT_EQ(kindOfFailure(dir.file("missing.obj"), dir.file("out.nxz"), threads), ErrorKind::InvalidInput) << threads;
}

TEST(BuildOptions, aThreadCountOutOfRangeIsAnInvalidArgument) {
    TempDir dir;
    for (const int threads : {-1, -2, 4097, 99999, INT_MAX, INT_MIN})
        EXPECT_EQ(kindOfFailure(dir.file("missing.obj"), dir.file("out.nxz"), threads), ErrorKind::InvalidArgument) << threads;
}

TEST(BuildOptions, theMessageOfARefusedThreadCountNamesTheRange) {
    TempDir dir;
    try {
        bm3d::BuildOptions options;
        options.threads = 4097;
        bm3d::build(dir.file("missing.obj"), dir.file("out.nxz"), options);
        FAIL() << "build() did not throw";
    } catch (const bm3d::Error& e) {
        EXPECT_NE(std::string(e.what()).find("threads"), std::string::npos) << e.what();
        EXPECT_NE(std::string(e.what()).find("4096"), std::string::npos) << e.what();
    }
}

TEST(BuildOptions, aRefusedArgumentLeavesNoOutput) {
    TempDir dir;
    kindOfFailure(dir.file("missing.obj"), dir.file("out.nxz"), -1);
    EXPECT_TRUE(dir.list().empty());
}

TEST(BuildErrors, anEmptyOutputPathIsAnInvalidArgument) {
    TempDir dir;
    EXPECT_EQ(kindOfFailure(dir.file("missing.obj"), {}, 1), ErrorKind::InvalidArgument);
}

TEST(BuildErrors, anEmptyInputPathIsInvalidInput) {
    TempDir dir;
    EXPECT_EQ(kindOfFailure({}, dir.file("out.nxz"), 1), ErrorKind::InvalidInput);
}

TEST(BuildErrors, aMissingInputIsInvalidInputAndNamesTheFile) {
    TempDir dir;
    try {
        bm3d::build(dir.file("absent.obj"), dir.file("out.nxz"));
        FAIL() << "build() did not throw";
    } catch (const bm3d::Error& e) {
        EXPECT_EQ(e.kind(), ErrorKind::InvalidInput);
        EXPECT_NE(std::string(e.what()).find("absent.obj"), std::string::npos) << e.what();
    }
    EXPECT_TRUE(dir.list().empty());
}

TEST(BuildErrors, theErrorIsARuntimeErrorThatCarriesItsKind) {
    const bm3d::Error error(ErrorKind::BuildFailed, "no space left");
    const std::runtime_error& base = error;
    EXPECT_STREQ(base.what(), "no space left");
    EXPECT_EQ(error.kind(), ErrorKind::BuildFailed);
    EXPECT_EQ(bm3d::Error(ErrorKind::InvalidInput, "x").kind(), ErrorKind::InvalidInput);
    EXPECT_EQ(bm3d::Error(ErrorKind::InvalidArgument, "x").kind(), ErrorKind::InvalidArgument);
}

}  // namespace
