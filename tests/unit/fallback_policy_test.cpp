// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the build does when a try fails in a way the input can be built round: the decisions of the
// policy, which holds no state of the build.

#include <string>

#include <gtest/gtest.h>

#include "build/fallback_policy.h"

namespace {

using namespace bm3d::detail;

Attempt firstAttempt() {
    Attempt attempt;
    attempt.threads = 6;
    attempt.nodeFaces = 16384;
    return attempt;
}

TEST(FallbackPolicy, aTextureThatCannotBePackedMeansABuildWithoutTextures) {
    const FallbackPolicy policy(firstAttempt());
    const auto retry = policy.after(firstAttempt(), TexturePackingError("too large to pack"));
    ASSERT_TRUE(retry.has_value());
    EXPECT_FALSE(retry->attempt.withTextures);
    EXPECT_EQ(retry->warning, "too large to pack");
    // nothing else changes
    EXPECT_EQ(retry->attempt.threads, 6);
    EXPECT_EQ(retry->attempt.nodeFaces, 16384);
    EXPECT_EQ(retry->attempt.smallerNodeAttempts, 0);
}

TEST(FallbackPolicy, aBuildWithoutTexturesThatFailsToPackHasNoWayRound) {
    const FallbackPolicy policy(firstAttempt());
    Attempt without = firstAttempt();
    without.withTextures = false;
    EXPECT_FALSE(policy.after(without, TexturePackingError("again")).has_value());
}

TEST(FallbackPolicy, aNodeWithTooManyVerticesMeansNodesOfHalfTheTriangles) {
    const FallbackPolicy policy(firstAttempt());
    FallbackPolicy::Retry retry = policy.after(firstAttempt(), NodeTooLargeError("fragmented"));
    EXPECT_EQ(retry.attempt.nodeFaces, 8192);
    EXPECT_EQ(retry.attempt.smallerNodeAttempts, 1);
    EXPECT_TRUE(retry.attempt.withTextures);
    EXPECT_EQ(retry.warning, "fragmented");
}

TEST(FallbackPolicy, theNodesAreMadeSmallerThreeTimesAndThenTheBuildFailsSayingSo) {
    const FallbackPolicy policy(firstAttempt());
    Attempt attempt = firstAttempt();
    for (int expected : {8192, 4096, 2048}) {
        attempt = policy.after(attempt, NodeTooLargeError("fragmented")).attempt;
        EXPECT_EQ(attempt.nodeFaces, expected);
    }
    try {
        policy.after(attempt, NodeTooLargeError("fragmented"));
        FAIL() << "a fourth try was allowed";
    } catch (const Error& e) {
        EXPECT_STREQ(e.what(), "fragmented; nodes of at most 2048 triangles, the smallest tried (the build started with "
                               "16384), are still too large for this model");
    }
}

TEST(FallbackPolicy, theLimitIsTheOneTheConstructorGets) {
    const FallbackPolicy policy(firstAttempt(), 1);
    const Attempt once = policy.after(firstAttempt(), NodeTooLargeError("x")).attempt;
    EXPECT_THROW(policy.after(once, NodeTooLargeError("x")), Error);
}

TEST(FallbackPolicy, theTwoWaysRoundCombine) {
    const FallbackPolicy policy(firstAttempt());
    Attempt attempt = policy.after(firstAttempt(), NodeTooLargeError("a")).attempt;
    attempt = policy.after(attempt, TexturePackingError("b"))->attempt;
    EXPECT_EQ(attempt.nodeFaces, 8192);
    EXPECT_FALSE(attempt.withTextures);
}

TEST(FallbackPolicy, theSummaryOfABuildThatNeededNoFallbackIsEmpty) {
    EXPECT_TRUE(FallbackPolicy(firstAttempt()).summary(firstAttempt()).empty());
}

TEST(FallbackPolicy, theSummaryTellsWhatTheBuildThatSucceededDiffersIn) {
    const FallbackPolicy policy(firstAttempt());
    Attempt attempt = policy.after(firstAttempt(), NodeTooLargeError("a")).attempt;
    EXPECT_EQ(policy.summary(attempt),
              (std::vector<std::string>{"built with nodes of 8192 triangles instead of 16384: the texture "
                                        "parametrization is too fragmented for larger nodes"}));
    attempt = policy.after(attempt, TexturePackingError("b"))->attempt;
    const auto warnings = policy.summary(attempt);
    ASSERT_EQ(warnings.size(), 2u);
    EXPECT_EQ(warnings[1], "textures could not be packed; built without textures");
}

}  // namespace
