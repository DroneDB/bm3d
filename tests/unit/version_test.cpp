// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include <regex>
#include <string>

#include <gtest/gtest.h>

#include "bm3d/bm3d.h"
#include "version_file.h"

namespace {

using bm3dtest::versionFileText;

TEST(Version, theLibraryVersionIsTheTextOfTheVersionFile) {
    const std::string expected = versionFileText();
    ASSERT_FALSE(expected.empty());
    EXPECT_STREQ(bm3d::version(), expected.c_str());
}

TEST(Version, theVersionStartsWithMajorMinorAndPatch) {
    EXPECT_TRUE(std::regex_match(bm3d::version(), std::regex(R"(\d+\.\d+\.\d+([-+].+)?)"))) << bm3d::version();
}

TEST(Version, theFormatIsTheNexusContainer) {
    EXPECT_EQ(bm3d::formatVersion(), 0);
}

TEST(Version, theTwoAreStableFromOneCallToTheNext) {
    EXPECT_EQ(bm3d::version(), bm3d::version());
    EXPECT_EQ(bm3d::formatVersion(), bm3d::formatVersion());
}

}  // namespace
