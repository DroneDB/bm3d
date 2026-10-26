// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The checker that decides whether a --metadata file is a JSON object. It must accept exactly
// what RFC 8259 calls an object, since what it accepts is stored as it is.

#include <string>

#include <gtest/gtest.h>

#include "json_object.h"

namespace {

using bm3d::jsonObjectError;

// `depth` openers, the core, `depth` closers
std::string nested(char open, char close, int depth, const std::string& core) {
    return std::string(size_t(depth), open) + core + std::string(size_t(depth), close);
}

TEST(JsonObject, anEmptyObjectIsValid) {
    EXPECT_EQ(jsonObjectError("{}"), "");
    EXPECT_EQ(jsonObjectError("  \t\r\n{ \n}\n  "), "");
}

TEST(JsonObject, anObjectWithEveryKindOfValueIsValid) {
    EXPECT_EQ(jsonObjectError(R"({"s":"text","n":-12.5e+3,"t":true,"f":false,"z":null,"a":[1,[2],{}],"o":{"k":"v"}})"), "");
}

TEST(JsonObject, numbersFollowTheGrammar) {
    for (const char* number : {"0", "-0", "7", "-7", "0.5", "10.25", "1e5", "1E5", "1e+5", "1e-5", "-0.0e0", "123456789012345678901234567890"})
        EXPECT_EQ(jsonObjectError(std::string("{\"n\":") + number + "}"), "") << number;
    for (const char* number : {"01", "+1", "1.", ".5", "-", "1e", "1e+", "0x10", "NaN", "Infinity", "- 1", "--1", "1.2.3"})
        EXPECT_NE(jsonObjectError(std::string("{\"n\":") + number + "}"), "") << number;
}

TEST(JsonObject, stringsKeepTheirEscapes) {
    EXPECT_EQ(jsonObjectError(R"({"k":"quote \" backslash \\ slash \/ \b \f \n \r \t A é"})"), "");
    EXPECT_EQ(jsonObjectError(R"({"a":"b"})"), "");
}

TEST(JsonObject, aBadEscapeIsRefused) {
    EXPECT_NE(jsonObjectError(R"({"k":"\x"})"), "");
    EXPECT_NE(jsonObjectError(R"({"k":"\'"})"), "");
    EXPECT_NE(jsonObjectError(R"({"k":"\u12"})"), "");
    EXPECT_NE(jsonObjectError(R"({"k":"\u12G4"})"), "");
    EXPECT_NE(jsonObjectError("{\"k\":\"\\"), "");
}

TEST(JsonObject, aRawControlCharacterInAStringIsRefused) {
    EXPECT_NE(jsonObjectError("{\"k\":\"a\nb\"}"), "");
    EXPECT_NE(jsonObjectError("{\"k\":\"a\tb\"}"), "");
    EXPECT_NE(jsonObjectError(std::string("{\"k\":\"a\0b\"}", 11)), "");
}

TEST(JsonObject, anArrayIsNotAnObject) {
    EXPECT_NE(jsonObjectError("[]"), "");
    EXPECT_NE(jsonObjectError("[{}]"), "");
    EXPECT_NE(jsonObjectError("[1,2,3]"), "");
}

TEST(JsonObject, aValueThatIsNotAnObjectIsRefused) {
    for (const char* text : {"\"text\"", "12", "true", "null", "", "   ", "\n"})
        EXPECT_NE(jsonObjectError(text), "") << text;
}

TEST(JsonObject, aTrailingCommaIsRefused) {
    EXPECT_NE(jsonObjectError(R"({"a":1,})"), "");
    EXPECT_NE(jsonObjectError(R"({"a":[1,2,]})"), "");
    EXPECT_NE(jsonObjectError(R"({,})"), "");
    EXPECT_NE(jsonObjectError(R"({"a":1,,"b":2})"), "");
}

TEST(JsonObject, theMessageGivesTheOffsetOfTheProblem) {
    // The '}' after the comma, where a name was expected
    EXPECT_NE(jsonObjectError(R"({"a":1,})").find("at byte 7"), std::string::npos) << jsonObjectError(R"({"a":1,})");
    EXPECT_NE(jsonObjectError("{}x").find("at byte 2"), std::string::npos) << jsonObjectError("{}x");
}

TEST(JsonObject, theSyntaxOfAnObjectIsStrict) {
    for (const char* text : {R"({a:1})", R"({'a':1})", R"({"a" 1})", R"({"a":})", R"({"a":1 "b":2})", R"({"a":1)", R"({"a")",
                             R"({"a":tru})", R"({"a":nul})", R"({"a":True})", R"({"a":"unclosed})", R"({"a":1}})",
                             R"({"a":1} {"b":2})", R"({"a":1}x)", R"({"a":1}//c)", R"(/*c*/{"a":1})", R"({"a":[1 2]})", R"({"a":{"b":1})",
                             "{\"a\":1}\xEF\xBB\xBF"})
        EXPECT_NE(jsonObjectError(text), "") << text;
}

TEST(JsonObject, aDuplicateNameIsAllowed) {
    // RFC 8259 leaves it to the receiver
    EXPECT_EQ(jsonObjectError(R"({"a":1,"a":2})"), "");
}

TEST(JsonObject, whiteSpaceAroundAndInsideIsAllowed) {
    EXPECT_EQ(jsonObjectError(" \n\r\t{ \"a\" \t: \r\n [ 1 , 2 ] , \"b\" : { } } \n"), "");
    // a form feed and a vertical tab are not JSON white space
    EXPECT_NE(jsonObjectError("\f{}"), "");
    EXPECT_NE(jsonObjectError("{}\v"), "");
}

TEST(JsonObject, aByteOrderMarkBeforeTheObjectIsAccepted) {
    EXPECT_EQ(jsonObjectError("\xEF\xBB\xBF{}"), "");
    EXPECT_EQ(jsonObjectError("\xEF\xBB\xBF  {\"a\":1}\n"), "");
}

TEST(JsonObject, aByteOrderMarkDoesNotMakeAnythingElseAnObject) {
    EXPECT_NE(jsonObjectError("\xEF\xBB\xBF"), "");
    EXPECT_NE(jsonObjectError("\xEF\xBB\xBF[]"), "");
    EXPECT_NE(jsonObjectError("\xEF\xBB\xBF\xEF\xBB\xBF{}"), "");
    // a truncated byte order mark is not one
    EXPECT_NE(jsonObjectError("\xEF\xBB{}"), "");
}

TEST(JsonObject, validUtf8InAStringIsAccepted) {
    // 2, 3 and 4 byte sequences, and the first and last of each length
    EXPECT_EQ(jsonObjectError("{\"k\":\"\xC3\xA9 \xD0\xBC\xD0\xBE\xD0\xB4 \xE6\xA8\xA1 \xF0\x9F\x98\x80\"}"), "");
    EXPECT_EQ(jsonObjectError("{\"k\":\"\xC2\x80 \xDF\xBF \xE0\xA0\x80 \xEF\xBF\xBF \xF0\x90\x80\x80 \xF4\x8F\xBF\xBF\"}"), "");
    EXPECT_EQ(jsonObjectError("{\"\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB5\xD0\xBB\xD1\x8C\":1}"), "");
}

TEST(JsonObject, badUtf8InAStringIsRefused) {
    const char* bad[] = {
        "\x80",              // a continuation byte alone
        "\xBF",              // another
        "\xC0\x80",          // an overlong NUL
        "\xC1\xBF",          // an overlong DEL
        "\xE0\x80\x80",      // an overlong three byte form
        "\xF0\x80\x80\x80",  // an overlong four byte form
        "\xC3",              // cut short at the closing quote
        "\xC3\x28",          // a continuation byte missing
        "\xE6\xA8",          // a three byte form with two bytes
        "\xF0\x9F\x98",      // a four byte form with three
        "\xED\xA0\x80",      // a UTF-16 high surrogate in UTF-8
        "\xED\xBF\xBF",      // a UTF-16 low surrogate in UTF-8
        "\xF4\x90\x80\x80",  // above U+10FFFF
        "\xF5\x80\x80\x80",  // a lead byte past the last
        "\xFE",
        "\xFF",
    };
    for (const char* text : bad)
        EXPECT_NE(jsonObjectError(std::string("{\"k\":\"") + text + "\"}"), "") << "bytes starting with " << int(static_cast<unsigned char>(text[0]));
}

TEST(JsonObject, badUtf8InAnameIsRefusedToo) {
    EXPECT_NE(jsonObjectError("{\"\xFF\":1}"), "");
}

TEST(JsonObject, badUtf8OutsideAStringIsRefused) {
    EXPECT_NE(jsonObjectError("{\"a\":1}\xC3\xA9"), "");
    EXPECT_NE(jsonObjectError("{\"a\":\xC3\xA9}"), "");
}

TEST(JsonObject, aLoneSurrogateEscapeIsRefused) {
    EXPECT_NE(jsonObjectError(R"({"k":"\ud800"})"), "");
    EXPECT_NE(jsonObjectError(R"({"k":"\udbff"})"), "");
    EXPECT_NE(jsonObjectError(R"({"k":"\udc00"})"), "");
    EXPECT_NE(jsonObjectError(R"({"k":"\udfff"})"), "");
    EXPECT_NE(jsonObjectError(R"({"k":"\uD800x"})"), "");
    EXPECT_NE(jsonObjectError(R"({"k":"\uD800A"})"), "");
    EXPECT_NE(jsonObjectError(R"({"k":"\uD800\uD800"})"), "");
    EXPECT_NE(jsonObjectError(R"({"k":"\uDC00\uD800"})"), "");
    EXPECT_NE(jsonObjectError(R"({"\ud800":1})"), "");
}

TEST(JsonObject, aSurrogatePairEscapeIsAccepted) {
    EXPECT_EQ(jsonObjectError(R"({"k":"😀"})"), "");
    EXPECT_EQ(jsonObjectError(R"({"k":"𐀀 􏿿"})"), "");
    // the code points either side of the surrogates are not surrogates
    EXPECT_EQ(jsonObjectError(R"({"k":"퟿ "})"), "");
}

TEST(JsonObject, nestingPastTheLimitIsRefused) {
    EXPECT_EQ(bm3d::kJsonMaxDepth, 256);
    const int limit = bm3d::kJsonMaxDepth;
    // the object is the first level: the array in it is the second, and so on
    const std::string atLimit = "{\"a\":" + nested('[', ']', limit - 1, "1") + "}";
    const std::string pastLimit = "{\"a\":" + nested('[', ']', limit, "1") + "}";
    EXPECT_EQ(jsonObjectError(atLimit), "");
    const std::string problem = jsonObjectError(pastLimit);
    EXPECT_NE(problem, "");
    EXPECT_NE(problem.find("nested too deeply"), std::string::npos) << problem;
}

TEST(JsonObject, nestingOfObjectsIsLimitedAsWell) {
    const int limit = bm3d::kJsonMaxDepth;
    auto objects = [](int levels) {
        std::string text;
        for (int level = 0; level < levels; ++level)
            text += "{\"a\":";
        return text + "1" + std::string(size_t(levels), '}');
    };
    EXPECT_EQ(jsonObjectError(objects(limit)), "");
    EXPECT_NE(jsonObjectError(objects(limit + 1)), "");
}

TEST(JsonObject, aHugeNestingIsRefusedWithoutExhaustingTheStack) {
    const std::string deep = "{\"a\":" + std::string(200000, '[');
    const std::string problem = jsonObjectError(deep);
    EXPECT_NE(problem.find("nested too deeply"), std::string::npos) << problem;
}

TEST(JsonObject, aLongStringAndALongArrayAreAccepted) {
    EXPECT_EQ(jsonObjectError("{\"k\":\"" + std::string(1 << 20, 'a') + "\"}"), "");
    std::string array = "{\"a\":[";
    for (int i = 0; i < 100000; ++i)
        array += i == 0 ? "1" : ",1";
    EXPECT_EQ(jsonObjectError(array + "]}"), "");
}

}  // namespace
