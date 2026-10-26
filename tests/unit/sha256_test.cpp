// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SHA-256 of the content hash, against the vectors of FIPS 180-4 and, for the lengths where
// the padding changes the number of blocks, against the digests of Python's hashlib.

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "format/sha256.h"

namespace {

using bm3d::detail::Sha256;
using bm3d::detail::sha256Hex;

TEST(Sha256, theVectorsOfTheStandard) {
    EXPECT_EQ(sha256Hex("", 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256Hex("abc", 3), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(sha256Hex("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 112), "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST(Sha256, aMillionAs) {
    const std::string million(1000000, 'a');
    EXPECT_EQ(sha256Hex(million.data(), million.size()), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// 55 bytes is the longest message that leaves room for the length in one block, 56 the shortest
// that needs a second one
TEST(Sha256, theLengthsAtWhichThePaddingChangesTheNumberOfBlocks) {
    EXPECT_EQ(sha256Hex(std::string(55, 'a').data(), 55), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    EXPECT_EQ(sha256Hex(std::string(56, 'a').data(), 56), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    EXPECT_EQ(sha256Hex(std::string(57, 'a').data(), 57), "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6");
    EXPECT_EQ(sha256Hex(std::string(63, 'a').data(), 63), "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
    EXPECT_EQ(sha256Hex(std::string(64, 'a').data(), 64), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    EXPECT_EQ(sha256Hex(std::string(65, 'a').data(), 65), "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
    EXPECT_EQ(sha256Hex(std::string(119, 'a').data(), 119), "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb");
    EXPECT_EQ(sha256Hex(std::string(120, 'a').data(), 120), "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c");
    EXPECT_EQ(sha256Hex(std::string(128, 'a').data(), 128), "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e");
}

// The same digest whatever way the bytes arrive
TEST(Sha256, theBytesMayComeInPiecesOfAnySize) {
    std::string text;
    for (int i = 0; i < 1000; i++)
        text += char('a' + i % 26);
    const std::string whole = sha256Hex(text.data(), text.size());
    for (size_t piece : {1, 3, 7, 55, 56, 63, 64, 65, 100, 999}) {
        Sha256 hash;
        for (size_t at = 0; at < text.size(); at += piece)
            hash.update(text.data() + at, std::min(piece, text.size() - at));
        EXPECT_EQ(hash.finishHex(), whole) << piece;
    }
}

TEST(Sha256, anEmptyUpdateChangesNothing) {
    Sha256 hash;
    hash.update("abc", 3);
    hash.update("", 0);
    hash.update(nullptr, 0);
    EXPECT_EQ(hash.finishHex(), sha256Hex("abc", 3));
}

}  // namespace
