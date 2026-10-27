// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The kind of an error of the API comes from the type of what the build threw and never from its
// message: a failure of a scratch file is a failed build whatever the message says, and a failure
// of the input is an invalid input whatever it names.

#include <new>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "api/error_mapping.h"
#include "bm3d/bm3d.h"
#include "common/error.h"

namespace {

using namespace bm3d::detail;

// What rethrowAsApiError makes of an exception
template <typename Thrown>
bm3d::Error mapped(const Thrown& thrown) {
    try {
        try {
            throw thrown;
        } catch (...) {
            rethrowAsApiError();
        }
    } catch (const bm3d::Error& e) {
        return e;
    }
    ADD_FAILURE() << "nothing was thrown";
    return bm3d::Error(bm3d::ErrorKind::BuildFailed, "");
}

TEST(ErrorMapping, anInputErrorIsAnInvalidInput) {
    const bm3d::Error e = mapped(InputError("could not open file x"));
    EXPECT_EQ(e.kind(), bm3d::ErrorKind::InvalidInput);
    EXPECT_STREQ(e.what(), "could not open file x");
}

TEST(ErrorMapping, anArgumentErrorIsAnInvalidArgument) {
    EXPECT_EQ(mapped(ArgumentError("too long")).kind(), bm3d::ErrorKind::InvalidArgument);
}

TEST(ErrorMapping, aScratchErrorIsABuildFailedWhateverItsMessageSays) {
    // a message that names no scratch file, and one that looks like a failure of the input
    EXPECT_EQ(mapped(ScratchError("disk full")).kind(), bm3d::ErrorKind::BuildFailed);
    EXPECT_EQ(mapped(ScratchError("could not open file model.obj")).kind(), bm3d::ErrorKind::BuildFailed);
}

TEST(ErrorMapping, anErrorOfTheInputWithTheMessageOfAScratchFileIsStillTheInputs) {
    EXPECT_EQ(mapped(InputError("unable to create temporary file /tmp/x")).kind(), bm3d::ErrorKind::InvalidInput);
}

TEST(ErrorMapping, aPlainErrorOfTheBuildIsABuildFailed) {
    EXPECT_EQ(mapped(Error("no valid geometry")).kind(), bm3d::ErrorKind::BuildFailed);
    EXPECT_EQ(mapped(NodeTooLargeError("x")).kind(), bm3d::ErrorKind::BuildFailed);
    EXPECT_EQ(mapped(TexturePackingError("x")).kind(), bm3d::ErrorKind::BuildFailed);
}

TEST(ErrorMapping, otherExceptionsAreABuildFailedWithTheirText) {
    // the text of bad_alloc is the standard library's own ("std::bad_alloc", "bad allocation")
    const bm3d::Error memory = mapped(std::bad_alloc());
    EXPECT_EQ(memory.kind(), bm3d::ErrorKind::BuildFailed);
    EXPECT_EQ(std::string(memory.what()), std::string("Exception: ") + std::bad_alloc().what());
    const bm3d::Error other = mapped(std::runtime_error("boom"));
    EXPECT_EQ(other.kind(), bm3d::ErrorKind::BuildFailed);
    EXPECT_STREQ(other.what(), "Exception: boom");
    EXPECT_STREQ(mapped(42).what(), "Unknown exception");
}

TEST(ErrorMapping, anErrorOfTheApiPassesAsItIs) {
    const bm3d::Error e = mapped(bm3d::Error(bm3d::ErrorKind::InvalidArgument, "threads"));
    EXPECT_EQ(e.kind(), bm3d::ErrorKind::InvalidArgument);
    EXPECT_STREQ(e.what(), "threads");
}

}  // namespace
