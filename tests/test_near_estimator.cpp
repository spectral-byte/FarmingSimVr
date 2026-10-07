#include <gtest/gtest.h>
#include "ngx/near_estimator.h"

using namespace fs25vr;
using namespace fs25vr::mvreproj;

namespace {

NearSolution Solution(float nearClip) {
    NearSolution s;
    s.valid = true;
    s.nearClip = nearClip;
    s.scale = 0.15 / nearClip;
    s.used = 500;
    s.total = 600;
    return s;
}

} // namespace

TEST(NearEstimatorTest, HasNoEstimateUntilEnoughSolutionsAgree) {
    NearEstimator e;
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);
    e.Submit(1, Solution(0.1f));
    e.Submit(2, Solution(0.1f));
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f) << "two solutions are not enough";
    e.Submit(3, Solution(0.1f));
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
}

TEST(NearEstimatorTest, TheMedianOfTheWindowRejectsAnIsolatedWildSolution) {
    NearEstimator e;
    for (uint64_t i = 1; i <= 4; ++i) e.Submit(i, Solution(0.1f));
    e.Submit(5, Solution(0.7f));
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
}

TEST(NearEstimatorTest, TheEstimateFollowsAChangeOfCameraOnceMostOfTheWindowAgrees) {
    NearEstimator e;
    for (uint64_t i = 1; i <= 5; ++i) e.Submit(i, Solution(0.15f)); // on foot
    EXPECT_NEAR(e.Estimate(), 0.15f, 1e-6f);
    for (uint64_t i = 6; i <= 8; ++i) e.Submit(i, Solution(0.1f));  // into the cab
    EXPECT_NEAR(e.Estimate(), 0.1f, 1e-6f);
}

TEST(NearEstimatorTest, SmallJitterDoesNotMoveThePublishedValue) {
    NearEstimator e;
    for (uint64_t i = 1; i <= 3; ++i) e.Submit(i, Solution(0.100f));
    const float first = e.Estimate();
    for (uint64_t i = 4; i <= 12; ++i) e.Submit(i, Solution(i % 2 ? 0.102f : 0.099f)); // within 3 percent
    EXPECT_FLOAT_EQ(e.Estimate(), first);
    for (uint64_t i = 13; i <= 20; ++i) e.Submit(i, Solution(0.11f)); // a real change
    EXPECT_NEAR(e.Estimate(), 0.11f, 1e-6f);
}

TEST(NearEstimatorTest, SolutionsOutsideTheSanityRangeAreIgnored) {
    NearEstimator e;
    for (uint64_t i = 1; i <= 6; ++i) e.Submit(i, Solution(0.01f));
    for (uint64_t i = 7; i <= 12; ++i) e.Submit(i, Solution(3.0f));
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);
    NearSolution invalid;
    for (uint64_t i = 13; i <= 18; ++i) e.Submit(i, invalid);
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);
    for (uint64_t i = 19; i <= 21; ++i) e.Submit(i, Solution(0.02f)); // the bounds themselves are fine
    EXPECT_NEAR(e.Estimate(), 0.02f, 1e-6f);
}

TEST(NearEstimatorTest, ResetForgetsTheEstimateAndTheReadbacksStillInFlight) {
    NearEstimator e;
    for (uint64_t i = 1; i <= 5; ++i) e.Submit(i, Solution(0.1f));
    ASSERT_GT(e.Estimate(), 0.0f);
    e.Reset(10); // a Discontinuity Flush at evaluation 10
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);

    for (uint64_t i = 6; i <= 9; ++i) e.Submit(i, Solution(0.1f)); // recorded before the flush, read back after
    EXPECT_FLOAT_EQ(e.Estimate(), 0.0f);
    for (uint64_t i = 10; i <= 12; ++i) e.Submit(i, Solution(0.15f));
    EXPECT_NEAR(e.Estimate(), 0.15f, 1e-6f);
}