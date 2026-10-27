// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The reading of the input says whose fault a failure is by the type of the exception: the Error of
// a loader becomes an InputError, and what is the machine's (a scratch file, memory) is let through.

#include <new>
#include <stdexcept>

#include <gtest/gtest.h>

#include "build/input_reader.h"
#include "common/error.h"

namespace {

using namespace bm3d::detail;

TEST(ReadingInput, theErrorOfALoaderBecomesAnInputError) {
    try {
        readingInput([] { throw Error("could not open file a.obj"); });
        FAIL() << "nothing was thrown";
    } catch (const InputError& e) {
        EXPECT_STREQ(e.what(), "could not open file a.obj");
    }
}

TEST(ReadingInput, aScratchErrorIsLetThroughWhateverItNames) {
    EXPECT_THROW(readingInput([] { throw ScratchError("could not open file a.obj"); }), ScratchError);
    try {
        readingInput([] { throw ScratchError("disk full"); });
    } catch (const InputError&) {
        FAIL() << "a scratch file is not the input";
    } catch (const ScratchError&) {
    }
}

TEST(ReadingInput, memoryThatRunsOutIsLetThrough) {
    EXPECT_THROW(readingInput([] { throw std::bad_alloc(); }), std::bad_alloc);
}

TEST(ReadingInput, anyOtherExceptionIsTheInputsWithItsText) {
    try {
        readingInput([] { throw std::runtime_error("bad name"); });
        FAIL() << "nothing was thrown";
    } catch (const InputError& e) {
        EXPECT_STREQ(e.what(), "Exception: bad name");
    }
}

TEST(ReadingInput, anInputErrorKeepsItsType) {
    EXPECT_THROW(readingInput([] { throw InputError("x"); }), InputError);
}

TEST(ReadingInput, aStepThatSucceedsReturns) {
    int done = 0;
    readingInput([&] { done = 1; });
    EXPECT_EQ(done, 1);
}

}  // namespace
