#include <gtest/gtest.h>
#include "ngx/near_estimator.h"

using namespace fs25vr;
using namespace fs25vr::mvreproj;

namespace {

NearSolution Solution(float nearClip, double inlierFraction = 0.9, uint32_t used = 500) {
    NearSolution s;
    s.valid = true;
    s.nearClip = nearClip;
    s.scale = 0.15 / nearClip;
    s.used = used;
    s.total = 600;
    s.informative = 560;
    s.retained = 555;
    s.inlierFraction = inlierFraction;
    return s;
}

// Submits n solutions of the same near at consecutive evaluation indices starting after `first`.
uint64_t Feed(NearEstimator& e, uint64_t first, size_t n, float nearClip) {
    uint64_t i = first;
    for (size_t k = 0; k < n; ++k) e.Submit(++i, Solution(nearClip));
    return i;
}

} // namespace

TEST(NearEstimatorTest, HasNoEstimateUntilEnoughSolutionsAgree) {
    NearEstimator e;
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);
    uint64_t i = Feed(e, 0, NearEstimator::kMinSolutions - 1, 0.1f);
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f) << "one fewer than the minimum is not enough";
    Feed(e, i, 1, 0.1f);
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
}

TEST(NearEstimatorTest, TheWindowIsLongEnoughToRideOutSeconds) {
    EXPECT_GE(NearEstimator::kWindow, 15u);
    EXPECT_LE(NearEstimator::kWindow, 30u);
}

TEST(NearEstimatorTest, TheMedianOfTheWindowRejectsAnIsolatedWildSolution) {
    NearEstimator e;
    uint64_t i = Feed(e, 0, 8, 0.1f);
    e.Submit(++i, Solution(0.7f));
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
}

TEST(NearEstimatorTest, TheSpikesSeenWhileDrivingDoNotMoveTheEstimate) {
    // The log of the vehicle driving: 0.107, 0.174, 0.401, 0.334, 0.167, 0.221, 0.108, 0.100, 0.434, 0.269.
    NearEstimator e;
    uint64_t i = Feed(e, 0, NearEstimator::kWindow, 0.1f);
    ASSERT_NEAR(e.Estimate(), 0.1f, 1e-6f);
    for (float v : {0.107f, 0.174f, 0.401f, 0.334f, 0.167f, 0.221f, 0.108f, 0.100f, 0.434f, 0.269f}) {
        e.Submit(++i, Solution(v));
        EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f) << "after " << v;
    }
}

TEST(NearEstimatorTest, TheEstimateFollowsAChangeOfCameraOnceMostOfTheWindowAgrees) {
    NearEstimator e;
    uint64_t i = Feed(e, 0, NearEstimator::kWindow, 0.15f); // on foot
    EXPECT_NEAR(e.Estimate(), 0.15f, 1e-6f);
    i = Feed(e, i, NearEstimator::kWindow / 2, 0.1f); // into the cab: not yet most of the window
    EXPECT_NEAR(e.Estimate(), 0.15f, 1e-6f);
    Feed(e, i, 1, 0.1f);
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
}

TEST(NearEstimatorTest, SmallJitterDoesNotMoveThePublishedValue) {
    NearEstimator e;
    uint64_t i = Feed(e, 0, NearEstimator::kMinSolutions, 0.100f);
    const float first = e.Estimate();
    for (size_t k = 0; k < 9; ++k) e.Submit(++i, Solution(k % 2 ? 0.102f : 0.099f)); // within 3 percent
    EXPECT_FLOAT_EQ(e.Estimate(), first);
    Feed(e, i, NearEstimator::kWindow, 0.11f); // a real change
    EXPECT_NEAR(e.Estimate(), 0.11f, 1e-6f);
}

TEST(NearEstimatorTest, SolutionsOutsideTheSanityRangeAreIgnored) {
    NearEstimator e;
    uint64_t i = Feed(e, 0, 8, 0.01f);
    i = Feed(e, i, 8, 3.0f);
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);
    NearSolution invalid;
    for (int k = 0; k < 8; ++k) e.Submit(++i, invalid);
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);
    Feed(e, i, NearEstimator::kMinSolutions, 0.02f); // the bounds themselves are fine
    EXPECT_NEAR(e.Estimate(), 0.02f, 1e-6f);
}

TEST(NearEstimatorTest, ResetForgetsTheEstimateAndTheReadbacksStillInFlight) {
    NearEstimator e;
    Feed(e, 0, NearEstimator::kMinSolutions, 0.1f);
    ASSERT_GT(e.Estimate(), 0.0f);
    e.Reset(100); // a Discontinuity Flush at evaluation 100
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);

    Feed(e, 90, 8, 0.1f); // recorded before the flush (91..98), read back after
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);
    Feed(e, 99, NearEstimator::kMinSolutions, 0.15f);
    EXPECT_NEAR(e.Estimate(), 0.15f, 1e-6f);
}

TEST(NearEstimatorTest, LowConfidenceSolutionsAreNeverPublished) {
    NearEstimator e;
    // Too few inliers: a fraction under the minimum, or a count under the minimum.
    for (uint64_t i = 1; i <= 12; ++i) e.Submit(i, Solution(0.1f, kMinNearInlierFraction - 0.01));
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);
    for (uint64_t i = 13; i <= 24; ++i) e.Submit(i, Solution(0.1f, 0.9, kMinNearSamples - 1));
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);
    Feed(e, 24, NearEstimator::kMinSolutions, 0.1f);
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
}

TEST(NearEstimatorTest, LowConfidenceSolutionsDoNotDisturbASettledEstimate) {
    NearEstimator e;
    uint64_t i = Feed(e, 0, NearEstimator::kWindow, 0.1f);
    for (int k = 0; k < 30; ++k) e.Submit(++i, Solution(0.3f, 0.2)); // confidently wrong, but not confident
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
}

TEST(NearEstimatorTest, TheEstimateStaysWithinAFactorOfTwoOfThePreviousValue) {
    NearEstimator e;
    uint64_t i = Feed(e, 0, NearEstimator::kWindow, 0.1f);
    // A burst of fewer than kJumpSolutions agreeing solutions beyond 2x (then back) changes nothing, even
    // when it outweighs the rest of the window's median.
    for (size_t k = 0; k < NearEstimator::kJumpSolutions - 1; ++k) e.Submit(++i, Solution(0.35f));
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
    i = Feed(e, i, 3, 0.1f);
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
}

TEST(NearEstimatorTest, AJumpBeyondTheFactorIsAcceptedOnceSeveralConsecutiveSolutionsAgree) {
    NearEstimator e;
    uint64_t i = Feed(e, 0, NearEstimator::kWindow, 0.1f);
    i = Feed(e, i, NearEstimator::kJumpSolutions - 1, 0.3f);
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
    Feed(e, i, 1, 0.3f);
    EXPECT_NEAR(e.Estimate(), 0.3f, 1e-6f);
}

TEST(NearEstimatorTest, ScatteredSolutionsBeyondTheFactorDoNotCountAsAnAgreedJump) {
    NearEstimator e;
    uint64_t i = Feed(e, 0, NearEstimator::kWindow, 0.1f);
    const float scattered[] = {0.3f, 0.5f, 0.25f, 0.6f, 0.35f, 0.45f, 0.28f, 0.55f};
    for (float v : scattered) e.Submit(++i, Solution(v));
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
}
