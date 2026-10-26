// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The metadata block (src/format/metadata.h, docs/FORMAT.md section 7): what the builder writes,
// the members a caller adds, and the keys it may not use.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/error.h"
#include "format/json_object.h"
#include "format/metadata.h"

namespace {

using namespace bm3d::detail;
using bm3d::JsonMember;

std::vector<BuildOption> exampleOptions() {
    return {
        BuildOption::whole("threads", 1),
        BuildOption::whole("nodeFaces", 16384),
        BuildOption::whole("topNodeFaces", 4096),
        BuildOption::whole("textureQuality", 90),
        BuildOption::whole("maxNodeTextureSide", 1024),
        BuildOption::fractional("scaling", 0.5f),
        BuildOption::fractional("adaptive", 0.333f),
        BuildOption::whole("skipLevels", -1),
        BuildOption::fractional("errorQ", 0.1f),
        BuildOption::whole("normalBits", 10),
        BuildOption::whole("lumaBits", 6),
        BuildOption::whole("chromaBits", 6),
        BuildOption::whole("alphaBits", 5),
        BuildOption::fractional("texStep", 0.25f),
    };
}

SourceInfo exampleSource() {
    SourceInfo source;
    source.name = "quad.obj";
    source.vertices = 4;
    source.faces = 2;
    source.boundsMax[0] = source.boundsMax[1] = 1;
    return source;
}

std::vector<JsonMember> membersOf(const std::string& text) {
    std::vector<JsonMember> members;
    EXPECT_EQ(parseExtraMetadata(text, members), "") << text;
    return members;
}

// The block of the worked example of the specification
TEST(Metadata, theBlockOfTheWorkedExampleIsReproducedByteForByte) {
    const std::string block = composeMetadata("bm3d 1.0.0", exampleOptions(), exampleSource(), {});
    EXPECT_EQ(block,
              "{\"generator\":\"bm3d 1.0.0\",\"buildOptions\":{\"threads\":1,\"nodeFaces\":16384,\"topNodeFaces\":4096,"
              "\"textureQuality\":90,\"maxNodeTextureSide\":1024,\"scaling\":0.5,\"adaptive\":0.333,\"skipLevels\":-1,"
              "\"errorQ\":0.1,\"normalBits\":10,\"lumaBits\":6,\"chromaBits\":6,\"alphaBits\":5,\"texStep\":0.25},"
              "\"source\":{\"name\":\"quad.obj\",\"vertices\":4,\"faces\":2,\"bounds\":{\"min\":[0,0,0],\"max\":[1,1,0]}}}");
    EXPECT_EQ(block.size(), 363u);
    EXPECT_EQ(bm3d::jsonObjectError(block), "");
}

TEST(Metadata, theExtraMembersFollowTheReservedOnesInTheirOrderWithTheirValuesUnchanged) {
    const std::string extras = " { \"crs\" : \"EPSG:32633\" ,\"origin\":[ 1.50 , 2e3, -0 ],\n \"nested\":{\"generator\":1,\"source\":{}} , \"z\" : null } ";
    const std::string block = composeMetadata("bm3d 1.0.0", exampleOptions(), exampleSource(), membersOf(extras));
    EXPECT_EQ(bm3d::jsonObjectError(block), "");
    const std::string tail =
        ",\"crs\":\"EPSG:32633\",\"origin\":[ 1.50 , 2e3, -0 ],\"nested\":{\"generator\":1,\"source\":{}},\"z\":null}";
    ASSERT_GT(block.size(), tail.size());
    EXPECT_EQ(block.substr(block.size() - tail.size()), tail);

    std::vector<JsonMember> members;
    ASSERT_EQ(bm3d::jsonObjectMembers(block, members), "");
    std::vector<std::string> names;
    for (const JsonMember& m : members)
        names.push_back(m.name);
    EXPECT_EQ(names, (std::vector<std::string>{"generator", "buildOptions", "source", "crs", "origin", "nested", "z"}));
}

TEST(Metadata, noExtraMembersIsAnEmptyObjectOrNothing) {
    std::vector<JsonMember> members;
    EXPECT_EQ(parseExtraMetadata("{}", members), "");
    EXPECT_TRUE(members.empty());
    EXPECT_EQ(composeMetadata("g", {}, SourceInfo(), members).substr(0, 31), "{\"generator\":\"g\",\"buildOptions\"");
}

TEST(Metadata, theKeysOfTheBuilderAndTheNamesThatStartWithBm3dAreRefusedNamingTheKey) {
    for (const char* key : {"generator", "buildOptions", "source", "bm3d", "bm3dVersion", "bm3d_x", "bm3d-crs"}) {
        std::vector<JsonMember> members;
        const std::string problem = parseExtraMetadata(std::string("{\"ok\":1,\"") + key + "\":2}", members);
        EXPECT_NE(problem.find(std::string("\"") + key + "\""), std::string::npos) << key << ": " << problem;
        EXPECT_NE(problem.find("reserved"), std::string::npos) << key << ": " << problem;
        EXPECT_TRUE(members.empty()) << key;
    }
}

TEST(Metadata, aKeyWrittenWithEscapesIsTheKeyItSpells) {
    for (const char* spelled : {"{\"gen\\u0065rator\":1}", "{\"\\u0073ource\":1}", "{\"build\\u004Fptions\":1}", "{\"bm\\u0033d\":1}", "{\"b\\u006D3dx\":1}"}) {
        std::vector<JsonMember> members;
        EXPECT_NE(parseExtraMetadata(spelled, members), "") << spelled;
    }
    std::vector<JsonMember> members;
    EXPECT_NE(parseExtraMetadata("{\"a\":1,\"\\u0061\":2}", members).find("twice"), std::string::npos);
}

TEST(Metadata, aKeyGivenTwiceIsRefusedBecauseTheReadersOfJsonDisagreeOnWhichWins) {
    std::vector<JsonMember> members;
    const std::string problem = parseExtraMetadata("{\"crs\":1,\"other\":2,\"crs\":3}", members);
    EXPECT_NE(problem.find("\"crs\""), std::string::npos) << problem;
    EXPECT_NE(problem.find("twice"), std::string::npos) << problem;
    EXPECT_THROW(composeMetadata("g", {}, SourceInfo(), {JsonMember{"a", "\"a\"", "1"}, JsonMember{"a", "\"a\"", "2"}}), Error);
    EXPECT_THROW(composeMetadata("g", {}, SourceInfo(), {JsonMember{"source", "\"source\"", "1"}}), Error);
}

TEST(Metadata, keysNestedInAValueAreNotExamined) {
    std::vector<JsonMember> members;
    EXPECT_EQ(parseExtraMetadata("{\"a\":{\"generator\":1,\"x\":1,\"x\":2},\"b\":[{\"source\":1}],\"c\":{\"bm3d\":1}}", members), "");
    EXPECT_EQ(members.size(), 3u);
}

TEST(Metadata, whatIsNotAJsonObjectIsRefused) {
    for (const char* bad : {"", "[]", "1", "\"a\"", "{", "{\"a\":1,}", "{\"a\":1} {}"}) {
        std::vector<JsonMember> members;
        EXPECT_NE(parseExtraMetadata(bad, members).find("not a JSON object"), std::string::npos) << bad;
    }
}

TEST(Metadata, theNameOfTheInputIsEscapedAndStaysValidUtf8) {
    SourceInfo source = exampleSource();
    source.name = "a \"quoted\" \\ name\n\t\x01 \xC3\xA9\xE6\xA8\xA1\xF0\x9F\x98\x80";
    std::string block = composeMetadata("g", {}, source, {});
    EXPECT_EQ(bm3d::jsonObjectError(block), "");
    EXPECT_NE(block.find("\"name\":\"a \\\"quoted\\\" \\\\ name\\u000a\\u0009\\u0001 \xC3\xA9\xE6\xA8\xA1\xF0\x9F\x98\x80\""), std::string::npos) << block;

    // a name that is not UTF-8 (a file of a machine with another code page): U+FFFD for each bad byte
    for (const std::string& name : std::vector<std::string>{std::string("a\xFF" "b"), std::string("\xC3"), std::string("\xE6\xA8"), std::string("\xC0\x80"),
                                   std::string("\xED\xA0\x80"), std::string("\xF4\x90\x80\x80"), std::string("x\x80y")}) {
        source.name = name;
        block = composeMetadata("g", {}, source, {});
        EXPECT_EQ(bm3d::jsonObjectError(block), "") << name;
        EXPECT_NE(block.find("\xEF\xBF\xBD"), std::string::npos) << name;
    }
}

TEST(Metadata, theNumbersAreTheShortestThatReadBackAsTheSameFloat) {
    SourceInfo source;
    source.boundsMin[0] = 0.1f;
    source.boundsMin[1] = -1234.5f;
    source.boundsMin[2] = 1e-7f;
    source.boundsMax[0] = 16777216.0f;
    source.boundsMax[1] = 3.4028235e38f;
    source.boundsMax[2] = -0.0f;
    source.vertices = 18446744073709551615ull;
    const std::string block = composeMetadata("g", {BuildOption::fractional("x", 0.1f)}, source, {});
    EXPECT_EQ(bm3d::jsonObjectError(block), "");
    EXPECT_NE(block.find("\"x\":0.1}"), std::string::npos) << block;
    EXPECT_NE(block.find("\"vertices\":18446744073709551615"), std::string::npos) << block;
    // the bounds read back as the floats they were
    const size_t min = block.find("\"min\":[") + 7;
    EXPECT_EQ(std::stof(block.substr(min)), 0.1f);
    EXPECT_NE(block.find("-1234.5"), std::string::npos) << block;
}

}  // namespace
