// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The scratch layer (MappedFile, ScratchFile, VirtualMemory) reports its failures with an error type
// of its own, so that a build tells the machine's faults from the input's by the type and not by what
// the message says: a scratch file that cannot be created is a failed build whatever the file is
// called, and a file of the input that cannot be opened is not a scratch failure.

#include <string>

#include <gtest/gtest.h>

#include "common/error.h"
#include "common/mappedfile.h"
#include "common/scratchfile.h"
#include "common/virtualarray.h"
#include "temp_dir.h"

namespace {

using namespace bm3d::detail;

TEST(ScratchErrors, aScratchFileThatCannotBeCreatedIsAScratchError) {
    bm3dtest::TempDir dir;
    // a folder that does not exist: the name of the file says nothing of scratch space
    const std::string prefix = bm3dtest::pathToUtf8(dir.path() / "no-such-folder" / "x");
    MappedFile file;
    EXPECT_THROW(file.createTemporary(prefix), ScratchError);
    ScratchFile scratch;
    EXPECT_THROW(scratch.create(prefix), ScratchError);
    EXPECT_THROW(VirtualChunks chunks(prefix), ScratchError);
}

TEST(ScratchErrors, aScratchFileThatIsNotOpenIsAScratchError) {
    ScratchFile scratch;
    unsigned char byte = 0;
    EXPECT_THROW(scratch.append(&byte, 1), ScratchError);
}

TEST(ScratchErrors, readingPastTheEndOfAScratchFileIsAScratchError) {
    bm3dtest::TempDir dir;
    ScratchFile scratch;
    scratch.create(bm3dtest::pathToUtf8(dir.path() / "scratch"));
    unsigned char byte = 0;
    scratch.append(&byte, 1);
    unsigned char back[2];
    EXPECT_THROW(scratch.read(0, back, 2), ScratchError);
    scratch.read(0, back, 1);
    EXPECT_EQ(back[0], 0);
}

TEST(ScratchErrors, aFileOfTheInputThatCannotBeOpenedIsAnErrorButNotAScratchError) {
    bm3dtest::TempDir dir;
    MappedFile file;
    try {
        file.openReadOnly(bm3dtest::pathToUtf8(dir.path() / "missing"));
        FAIL() << "a file that does not exist was opened";
    } catch (const ScratchError&) {
        FAIL() << "a file of the input is not scratch space";
    } catch (const Error& e) {
        EXPECT_NE(std::string(e.what()).find("missing"), std::string::npos);
    }
}

TEST(ScratchErrors, aScratchErrorIsAnError) {
    try {
        throw ScratchError("no space");
    } catch (const Error& e) {
        EXPECT_STREQ(e.what(), "no space");
    }
}

}  // namespace
