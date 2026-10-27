// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Windows program declares itself long path aware in the manifest embedded in it (the
// manifest is cli/bm3d.manifest, added to the program by CMakeLists.txt). Nothing else makes the
// file functions of a program that was not written for long paths take one, so the test reads the
// manifest from the file. It has no counterpart on other platforms.

#ifdef _WIN32

#include <cctype>
#include <string>

#include <gtest/gtest.h>

#include "cli_support.h"
#include "model_files.h"

namespace {

using namespace bm3dtest;

// True when the text holds <longPathAware ...>true</longPathAware>, with or without a namespace
// prefix on the name and with any white space around the value
bool declaresLongPathAware(const std::string& manifest) {
    const std::string name = "longPathAware";
    for (size_t at = manifest.find(name); at != std::string::npos; at = manifest.find(name, at + 1)) {
        const size_t close = manifest.find('>', at);
        if (close == std::string::npos)
            return false;
        const auto skipSpace = [&](size_t from) {
            while (from < manifest.size() && std::isspace(static_cast<unsigned char>(manifest[from])))
                from++;
            return from;
        };
        const size_t value = skipSpace(close + 1);
        if (manifest.compare(value, 4, "true") == 0 && manifest.compare(skipSpace(value + 4), 2, "</") == 0)
            return true;
    }
    return false;
}

TEST(CliManifest, theDetectorTellsTrueFromFalseAndFromAnythingElse) {
    EXPECT_TRUE(declaresLongPathAware(
        "<longPathAware xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">true</longPathAware>"));
    EXPECT_TRUE(declaresLongPathAware("<ws2:longPathAware>\n  true\n</ws2:longPathAware>"));
    EXPECT_FALSE(declaresLongPathAware("<longPathAware>false</longPathAware>"));
    EXPECT_FALSE(declaresLongPathAware("<dpiAware>true</dpiAware>"));
    EXPECT_FALSE(declaresLongPathAware("longPathAware"));
}

TEST(CliManifest, theProgramDeclaresItselfLongPathAware) {
    const std::string program = readBinaryFile(bm3dExecutable());
    ASSERT_FALSE(program.empty()) << bm3dExecutable();
    EXPECT_TRUE(declaresLongPathAware(program))
        << "no <longPathAware>true</longPathAware> in the manifest of " << bm3dExecutable();
}

}  // namespace

#endif  // _WIN32
