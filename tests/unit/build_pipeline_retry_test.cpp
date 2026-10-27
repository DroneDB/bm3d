// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The retries of a build through the pipeline: what a try that fails in a way the input can be built
// round leads to, the warnings it gives the logger and the error of the API that a failure ends in.
// No small input fails in these ways (a node needs a texture over 16K a side, or more vertices than
// a node holds), so the tries are made by functions that throw.

#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "api/error_mapping.h"
#include "bm3d/bm3d.h"
#include "build/build_pipeline.h"
#include "common/error.h"

namespace {

using namespace bm3d::detail;

// What a run of the pipeline with the given tries left behind
struct Outcome {
    std::vector<Attempt> attempts;
    std::vector<std::string> warnings;
    bool succeeded = false;
    bool apiErrorRaised = false;
    bm3d::ErrorKind kind = bm3d::ErrorKind::BuildFailed;
    std::string message;
};

// Runs the pipeline with tries that call `tryOf` with the number of the try (from 0) and the attempt
Outcome outcomeOf(const std::function<void(size_t, const Attempt&)>& tryOf, int nodeFaces = 16384) {
    Outcome result;
    BuildSettings settings;
    settings.nodeFaces = nodeFaces;
    const BuildPipeline pipeline(
        settings, [&](const std::string& message) { result.warnings.push_back(message); },
        MeshLoaderRegistry::standard(), [&](const Attempt& attempt) {
            result.attempts.push_back(attempt);
            tryOf(result.attempts.size() - 1, attempt);
        });
    try {
        try {
            pipeline.build("in.obj", "out.bm3d", 3, {});
            result.succeeded = true;
        } catch (...) {
            rethrowAsApiError();
        }
    } catch (const bm3d::Error& e) {
        result.apiErrorRaised = true;
        result.kind = e.kind();
        result.message = e.what();
    }
    return result;
}

TEST(BuildPipelineRetries, aBuildThatNeedsNoRetryGivesNoWarning) {
    const Outcome r = outcomeOf([](size_t, const Attempt&) {});
    EXPECT_TRUE(r.succeeded);
    ASSERT_EQ(r.attempts.size(), 1u);
    EXPECT_TRUE(r.attempts[0].withTextures);
    EXPECT_EQ(r.attempts[0].nodeFaces, 16384);
    EXPECT_EQ(r.attempts[0].threads, 3);
    EXPECT_TRUE(r.warnings.empty());
}

TEST(BuildPipelineRetries, aTextureThatCannotBePackedIsBuiltWithoutTexturesAndTheWarningsSayIt) {
    const Outcome r = outcomeOf([](size_t n, const Attempt&) {
        if (n == 0)
            throw TexturePackingError("Failed packing");
    });
    EXPECT_TRUE(r.succeeded);
    ASSERT_EQ(r.attempts.size(), 2u);
    EXPECT_TRUE(r.attempts[0].withTextures);
    EXPECT_FALSE(r.attempts[1].withTextures);
    EXPECT_EQ(r.attempts[1].nodeFaces, 16384);
    EXPECT_EQ(r.attempts[1].threads, 3);
    // the reason of the retry when it is made, then what the file came out as
    ASSERT_EQ(r.warnings.size(), 2u);
    EXPECT_EQ(r.warnings[0], "Failed packing");
    EXPECT_EQ(r.warnings[1], "textures could not be packed; built without textures");
}

TEST(BuildPipelineRetries, aPackingFailureWithoutTexturesIsTheFailureOfTheBuild) {
    const Outcome r = outcomeOf([](size_t, const Attempt&) { throw TexturePackingError("Failed packing"); });
    EXPECT_FALSE(r.succeeded);
    EXPECT_EQ(r.attempts.size(), 2u);
    ASSERT_TRUE(r.apiErrorRaised);
    EXPECT_EQ(r.kind, bm3d::ErrorKind::BuildFailed);
    EXPECT_EQ(r.message, "Failed packing");
    // only the retry that was made is told; no build came out, so no summary
    ASSERT_EQ(r.warnings.size(), 1u);
    EXPECT_EQ(r.warnings[0], "Failed packing");
}

TEST(BuildPipelineRetries, aNodeThatIsTooLargeIsBuiltAgainWithHalfAsManyTrianglesEachTime) {
    const Outcome r = outcomeOf([](size_t n, const Attempt&) {
        if (n < 2)
            throw NodeTooLargeError("a node has too many vertices");
    });
    EXPECT_TRUE(r.succeeded);
    ASSERT_EQ(r.attempts.size(), 3u);
    EXPECT_EQ(r.attempts[0].nodeFaces, 16384);
    EXPECT_EQ(r.attempts[1].nodeFaces, 8192);
    EXPECT_EQ(r.attempts[2].nodeFaces, 4096);
    for (const Attempt& attempt : r.attempts)
        EXPECT_TRUE(attempt.withTextures);
    ASSERT_EQ(r.warnings.size(), 3u);
    EXPECT_EQ(r.warnings[0], "a node has too many vertices");
    EXPECT_EQ(r.warnings[1], "a node has too many vertices");
    EXPECT_EQ(r.warnings[2],
              "built with nodes of 4096 triangles instead of 16384: the texture parametrization is too "
              "fragmented for larger nodes");
}

TEST(BuildPipelineRetries, nodesThatAreStillTooLargeAfterTheLastRetryEndTheBuildWithAnErrorThatSaysSo) {
    const Outcome r = outcomeOf([](size_t, const Attempt&) { throw NodeTooLargeError("a node has too many vertices"); });
    EXPECT_FALSE(r.succeeded);
    // the first try and three with smaller nodes
    ASSERT_EQ(r.attempts.size(), 4u);
    EXPECT_EQ(r.attempts[3].nodeFaces, 2048);
    ASSERT_TRUE(r.apiErrorRaised);
    EXPECT_EQ(r.kind, bm3d::ErrorKind::BuildFailed);
    EXPECT_NE(r.message.find("nodes of at most 2048 triangles"), std::string::npos);
    EXPECT_NE(r.message.find("the build started with 16384"), std::string::npos);
    EXPECT_EQ(r.warnings.size(), 3u);
}

TEST(BuildPipelineRetries, bothFallbacksCanFollowEachOtherAndBothAreInTheSummary) {
    const Outcome r = outcomeOf([](size_t n, const Attempt& attempt) {
        if (n == 0)
            throw NodeTooLargeError("too many vertices");
        if (attempt.withTextures)
            throw TexturePackingError("Failed packing");
    });
    EXPECT_TRUE(r.succeeded);
    ASSERT_EQ(r.attempts.size(), 3u);
    EXPECT_EQ(r.attempts[2].nodeFaces, 8192);
    EXPECT_FALSE(r.attempts[2].withTextures);
    ASSERT_EQ(r.warnings.size(), 4u);
    EXPECT_EQ(r.warnings[0], "too many vertices");
    EXPECT_EQ(r.warnings[1], "Failed packing");
    EXPECT_NE(r.warnings[2].find("nodes of 8192 triangles instead of 16384"), std::string::npos);
    EXPECT_EQ(r.warnings[3], "textures could not be packed; built without textures");
}

TEST(BuildPipelineRetries, aFailureThatIsNotTheInputsToBuildRoundIsNotRetried) {
    const Outcome input = outcomeOf([](size_t, const Attempt&) { throw InputError("could not open file x"); });
    EXPECT_EQ(input.attempts.size(), 1u);
    ASSERT_TRUE(input.apiErrorRaised);
    EXPECT_EQ(input.kind, bm3d::ErrorKind::InvalidInput);
    EXPECT_TRUE(input.warnings.empty());

    const Outcome scratch = outcomeOf([](size_t, const Attempt&) { throw ScratchError("disk full"); });
    EXPECT_EQ(scratch.attempts.size(), 1u);
    ASSERT_TRUE(scratch.apiErrorRaised);
    EXPECT_EQ(scratch.kind, bm3d::ErrorKind::BuildFailed);
    EXPECT_TRUE(scratch.warnings.empty());
}

}  // namespace
