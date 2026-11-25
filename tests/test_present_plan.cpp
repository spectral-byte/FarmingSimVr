#include <gtest/gtest.h>
#include "proxy/present_plan.h"

namespace fs25vr {
namespace {

PresentFacts StereoFacts() {
    PresentFacts f;
    f.active = true;
    f.eyeSwapchains = true;
    f.sessionRunning = true;
    f.companionActive = true;
    return f;
}

TEST(PresentPlanTest, PassThroughModePresentsAsTheGameDidAndLatchesTheQueuedMode) {
    PresentFacts f = StereoFacts();
    f.active = false;
    f.wasInQuadMode = true;

    const PresentPlan p = PlanPresent(f);

    EXPECT_FALSE(p.copyToEye.has_value());
    EXPECT_FALSE(p.resetPacing);
    EXPECT_FALSE(p.hideMenuCursor);
    EXPECT_FALSE(p.snapshotEye);
    EXPECT_EQ(p.submit, EyeSubmission::None);
    EXPECT_FALSE(p.alignMenuQuad);
    EXPECT_EQ(p.advance, FrameAdvance::None);
    EXPECT_TRUE(p.latchQueuedMode);
    EXPECT_FALSE(p.clearBackBufferToBlack);
    EXPECT_TRUE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Showing);
    EXPECT_FALSE(p.presentWasStereo);
    EXPECT_FALSE(p.inQuadModeAfter.has_value());
}

TEST(PresentPlanTest, NoEyeSwapchainsIsPlannedLikePassThroughMode) {
    PresentFacts f = StereoFacts();
    f.eyeSwapchains = false;

    const PresentPlan p = PlanPresent(f);

    EXPECT_FALSE(p.copyToEye.has_value());
    EXPECT_EQ(p.submit, EyeSubmission::None);
    EXPECT_TRUE(p.latchQueuedMode);
    EXPECT_TRUE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Showing);
    EXPECT_FALSE(p.presentWasStereo);
    EXPECT_FALSE(p.inQuadModeAfter.has_value());
}

TEST(PresentPlanTest, SessionNotRunningStartsAFreshEyePairAndLeavesTheMirrorToItself) {
    PresentFacts f = StereoFacts();
    f.sessionRunning = false;
    f.wasInQuadMode = true;
    f.desktopMirrorOff = true;

    const PresentPlan p = PlanPresent(f);

    EXPECT_FALSE(p.copyToEye.has_value());
    EXPECT_TRUE(p.resetPacing);
    EXPECT_EQ(p.submit, EyeSubmission::None);
    EXPECT_EQ(p.advance, FrameAdvance::None);
    EXPECT_TRUE(p.latchQueuedMode);
    EXPECT_FALSE(p.clearBackBufferToBlack);
    EXPECT_TRUE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Showing);
    EXPECT_FALSE(p.presentWasStereo);
    ASSERT_TRUE(p.inQuadModeAfter.has_value());
    EXPECT_FALSE(*p.inQuadModeAfter);
}

TEST(PresentPlanTest, MenuShowsTheBackBufferOnTheQuadAndAlignsItOnEntry) {
    PresentFacts f = StereoFacts();
    f.menuVisible = true;
    f.wasInQuadMode = false;

    const PresentPlan p = PlanPresent(f);

    ASSERT_TRUE(p.copyToEye.has_value());
    EXPECT_EQ(*p.copyToEye, 0u);
    EXPECT_FALSE(p.resetPacing);
    EXPECT_FALSE(p.hideMenuCursor);
    EXPECT_FALSE(p.snapshotEye);
    EXPECT_EQ(p.submit, EyeSubmission::MenuQuad);
    EXPECT_TRUE(p.alignMenuQuad);
    EXPECT_EQ(p.advance, FrameAdvance::None);
    EXPECT_TRUE(p.latchQueuedMode);
    EXPECT_FALSE(p.clearBackBufferToBlack);
    EXPECT_TRUE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Showing);
    EXPECT_FALSE(p.presentWasStereo);
    ASSERT_TRUE(p.inQuadModeAfter.has_value());
    EXPECT_TRUE(*p.inQuadModeAfter);
}

TEST(PresentPlanTest, MenuStaysAlignedWhileItIsAlreadyShowing) {
    PresentFacts f = StereoFacts();
    f.menuVisible = true;
    f.wasInQuadMode = true;

    const PresentPlan p = PlanPresent(f);

    EXPECT_EQ(p.submit, EyeSubmission::MenuQuad);
    EXPECT_FALSE(p.alignMenuQuad);
}

TEST(PresentPlanTest, TheQuadIsAlsoShownBeforeTheHandshakeAndIn2DFloatingWindowMode) {
    PresentFacts beforeHandshake = StereoFacts();
    beforeHandshake.companionActive = false;
    PresentFacts floating = StereoFacts();
    floating.floatingWindow2D = true;

    for (const PresentFacts& f : {beforeHandshake, floating}) {
        const PresentPlan p = PlanPresent(f);
        EXPECT_EQ(p.submit, EyeSubmission::MenuQuad);
        ASSERT_TRUE(p.copyToEye.has_value());
        EXPECT_EQ(*p.copyToEye, 0u);
        EXPECT_FALSE(p.presentWasStereo);
    }
}

TEST(PresentPlanTest, SequentialDualPassFirstSubPassCopiesTheLeftEyeAndHoldsBackThePresent) {
    PresentFacts f = StereoFacts();
    f.sequentialDualPass = true;
    f.eyePhase = 0;

    const PresentPlan p = PlanPresent(f);

    ASSERT_TRUE(p.copyToEye.has_value());
    EXPECT_EQ(*p.copyToEye, 0u);
    EXPECT_FALSE(p.resetPacing);
    EXPECT_TRUE(p.hideMenuCursor);
    EXPECT_TRUE(p.snapshotEye);
    EXPECT_EQ(p.submit, EyeSubmission::None);
    EXPECT_EQ(p.advance, FrameAdvance::ToSecondSubPass);
    EXPECT_FALSE(p.latchQueuedMode);
    EXPECT_FALSE(p.clearBackBufferToBlack);
    EXPECT_FALSE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Unchanged);
    EXPECT_TRUE(p.presentWasStereo);
    ASSERT_TRUE(p.inQuadModeAfter.has_value());
    EXPECT_FALSE(*p.inQuadModeAfter);
}

TEST(PresentPlanTest, SequentialDualPassSecondSubPassCopiesTheRightEyeSubmitsThePairAndPresents) {
    PresentFacts f = StereoFacts();
    f.sequentialDualPass = true;
    f.eyePhase = 1;

    const PresentPlan p = PlanPresent(f);

    ASSERT_TRUE(p.copyToEye.has_value());
    EXPECT_EQ(*p.copyToEye, 1u);
    EXPECT_TRUE(p.snapshotEye);
    EXPECT_EQ(p.submit, EyeSubmission::FramePair);
    EXPECT_EQ(p.advance, FrameAdvance::ToNextFrame);
    EXPECT_TRUE(p.latchQueuedMode);
    EXPECT_FALSE(p.clearBackBufferToBlack);
    EXPECT_TRUE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Showing);
    EXPECT_TRUE(p.presentWasStereo);
}

TEST(PresentPlanTest, AlternateFrameRenderingEvenParityCopiesTheLeftEyeAndPresents) {
    PresentFacts f = StereoFacts();
    f.eyePhase = 0;

    const PresentPlan p = PlanPresent(f);

    ASSERT_TRUE(p.copyToEye.has_value());
    EXPECT_EQ(*p.copyToEye, 0u);
    EXPECT_TRUE(p.hideMenuCursor);
    EXPECT_TRUE(p.snapshotEye);
    EXPECT_EQ(p.submit, EyeSubmission::None);
    EXPECT_EQ(p.advance, FrameAdvance::ToNextFrame);
    EXPECT_FALSE(p.latchQueuedMode);
    EXPECT_TRUE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Showing);
    EXPECT_TRUE(p.presentWasStereo);
}

TEST(PresentPlanTest, AlternateFrameRenderingOddParityCopiesTheRightEyeSubmitsThePairAndLatches) {
    PresentFacts f = StereoFacts();
    f.eyePhase = 1;

    const PresentPlan p = PlanPresent(f);

    ASSERT_TRUE(p.copyToEye.has_value());
    EXPECT_EQ(*p.copyToEye, 1u);
    EXPECT_EQ(p.submit, EyeSubmission::FramePair);
    EXPECT_EQ(p.advance, FrameAdvance::ToNextFrame);
    EXPECT_TRUE(p.latchQueuedMode);
    EXPECT_TRUE(p.callRealPresent);
    EXPECT_TRUE(p.presentWasStereo);
}

TEST(PresentPlanTest, ReturningFromTheQuadStartsTheEyePairAgainFromTheLeftEye) {
    for (bool sdp : {false, true}) {
        PresentFacts f = StereoFacts();
        f.sequentialDualPass = sdp;
        f.wasInQuadMode = true;
        f.eyePhase = 1;

        const PresentPlan p = PlanPresent(f);

        EXPECT_TRUE(p.resetPacing);
        ASSERT_TRUE(p.copyToEye.has_value());
        EXPECT_EQ(*p.copyToEye, 0u);
        EXPECT_EQ(p.submit, EyeSubmission::None);
        EXPECT_FALSE(p.latchQueuedMode);
        ASSERT_TRUE(p.inQuadModeAfter.has_value());
        EXPECT_FALSE(*p.inQuadModeAfter);
    }
}

TEST(PresentPlanTest, DesktopMirrorOffSuppressesTheRealPresentOnceTheBlackFrameIsGone) {
    PresentFacts f = StereoFacts();
    f.eyePhase = 1;
    f.desktopMirrorOff = true;
    f.mirrorWasShowing = false;

    const PresentPlan p = PlanPresent(f);

    EXPECT_EQ(p.submit, EyeSubmission::FramePair);
    EXPECT_TRUE(p.latchQueuedMode);
    EXPECT_FALSE(p.clearBackBufferToBlack);
    EXPECT_FALSE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Unchanged);
}

TEST(PresentPlanTest, DesktopMirrorGoingFromShowingToOffPresentsOnceBlack) {
    PresentFacts f = StereoFacts();
    f.eyePhase = 1;
    f.desktopMirrorOff = true;
    f.mirrorWasShowing = true;

    const PresentPlan p = PlanPresent(f);

    EXPECT_TRUE(p.clearBackBufferToBlack);
    EXPECT_TRUE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Off);
}

TEST(PresentPlanTest, DesktopMirrorOffAlsoGatesTheMenuQuadFrame) {
    PresentFacts f = StereoFacts();
    f.menuVisible = true;
    f.desktopMirrorOff = true;
    f.mirrorWasShowing = false;

    const PresentPlan p = PlanPresent(f);

    EXPECT_EQ(p.submit, EyeSubmission::MenuQuad);
    EXPECT_FALSE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Unchanged);
}

TEST(PresentPlanTest, DesktopMirrorOffDoesNotChangeTheFirstSubPassThatNeverPresents) {
    PresentFacts f = StereoFacts();
    f.sequentialDualPass = true;
    f.eyePhase = 0;
    f.desktopMirrorOff = true;
    f.mirrorWasShowing = true;

    const PresentPlan p = PlanPresent(f);

    EXPECT_FALSE(p.clearBackBufferToBlack);
    EXPECT_FALSE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Unchanged);
}

TEST(PresentPlanTest, DesktopMirrorOnShowsEveryPresentThatReachesDxgi) {
    PresentFacts f = StereoFacts();
    f.eyePhase = 1;
    f.desktopMirrorOff = false;
    f.mirrorWasShowing = false;

    const PresentPlan p = PlanPresent(f);

    EXPECT_FALSE(p.clearBackBufferToBlack);
    EXPECT_TRUE(p.callRealPresent);
    EXPECT_EQ(p.mirror, MirrorBookkeeping::Showing);
}

}
}
