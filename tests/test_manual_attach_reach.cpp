// Manual Attach, which hitch the Pointer Ray works (ADR-0035, spec #227, ticket #228), seen from outside: the real Companion Mod runs against the
// real VRBridge over FakeTrackedHeadset, with the game and Manual Attach stood in by Lua stubs (manual_attach_test_support.h). Covered here:
// which hitches can be pointed at (within Manual Attach's reach, with an Implement lined up or on them, of a farm the player may use), which one
// is worked when there are several, a trailer's own hitch, and what Manual Attach refuses (the pull still takes the ray and shows its warning).
// Prior art: test_animated_objects_pointer.cpp.

#include "manual_attach_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace manual_attach_test;

// A tractor whose rear hitch is 3 m ahead of the player (Manual Attach's reach is about 2.8 m) with a plow lined up there.
constexpr const char* kTractorOutOfReach = R"luau(
    _G.tractor = StubManualAttach.tractor({ rear = { -0.4, -0.2, 3.0 }, front = { 5, -0.2, 5 } })
    _G.plow = StubManualAttach.implement('plow')
    StubManualAttach.lineUp(plow, tractor, 1)
    StubManualAttach.detectAll()
)luau";

}  // namespace

// ---- Which hitches can be pointed at ----------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, AHitchWithNothingLinedUpAndNothingOnItIsNotSelectedAndThePullIsTheTriggersOwn) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua("_G.tractor = StubManualAttach.tractor({}); _G.plow = StubManualAttach.implement('plow'); StubManualAttach.detectAll()");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "nothing to attach there: no ray";
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0) << "and a pull does nothing";
    EXPECT_EQ(Pad().bRightTrigger, 255) << "it is a binding's trigger like any other";
}

TEST_F(VirtualGamepadManualAttachTest, AnImplementNotLinedUpWithTheHitchIsNotAttachable) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua("StubManualAttach.misalign(plow)");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
}

TEST_F(VirtualGamepadManualAttachTest, AHitchOutsideManualAttachsReachIsNotSelected) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorOutOfReach);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "the player still has to walk to the hitch";
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    EXPECT_EQ(Pad().bRightTrigger, 255);
}

TEST_F(VirtualGamepadManualAttachTest, TheReachIsManualAttachsOwnConstantNotACopyOfIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorOutOfReach);
    Lua("StubManualAttach.Handler.PLAYER_MIN_DISTANCE_SQ = 16");  // a release with a longer reach
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 3.00");
}

TEST_F(VirtualGamepadManualAttachTest, AVehicleManualAttachsDetectionHasNotFoundOffersNoHitch) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua("StubManualAttach.detect()");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
}

TEST_F(VirtualGamepadManualAttachTest, AnImplementOfAnotherFarmIsNeverAttachedByPointing) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua("plow.farmId = 2");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "Manual Attach would show no prompt for it";
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    EXPECT_EQ(Attached("plow"), "false");
}

// ---- Which hitch is worked -----------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, WithTwoHitchesInReachThePullWorksTheOneTheRayIsOnWhicheverIsNearer) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kFrontAndRear);
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "tostring(StubManualAttach.handler.attachable == frontLoader)"), "true") << "Manual Attach's own pick is the nearer, front hitch";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch);
    PullTrigger(kRight);
    ASSERT_EQ(Performs("joint"), 1);
    EXPECT_EQ(EvalLua(L, "tostring(StubManualAttach.performs[1].attachable == rearPlow and StubManualAttach.performs[1].jointIndex == 1)"), "true")
        << "the rear hitch, which is not the nearest";
    EXPECT_EQ(Attached("rearPlow"), "true");
    EXPECT_EQ(Attached("frontLoader"), "false");
    ReleaseTrigger(kRight);

    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-1.20 -0.20 1.00");
    PullTrigger(kRight);
    ASSERT_EQ(Performs("joint"), 2);
    EXPECT_EQ(EvalLua(L, "tostring(StubManualAttach.performs[2].attachable == frontLoader and StubManualAttach.performs[2].jointIndex == 2)"), "true");
    EXPECT_EQ(Attached("frontLoader"), "true");
}

TEST_F(VirtualGamepadManualAttachTest, WithAnImplementOnOneHitchAndAnotherLinedUpAtTheOtherThePullWorksThePointedOne) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kFrontAndRear);
    Lua("StubManualAttach.attach(tractor, rearPlow, 1, 1)");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "tostring(StubManualAttach.performs[1].attachedImplement == rearPlow)"), "true") << "the pointed hitch's Implement is detached";
    EXPECT_EQ(Attached("rearPlow"), "false");
    EXPECT_EQ(Attached("frontLoader"), "false") << "and the other hitch is left alone";
    EXPECT_EQ(Performs(), 1);
}

TEST_F(VirtualGamepadManualAttachTest, TheHitchesOfTwoVehiclesAreWorkedByPointingAtThem) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(R"luau(
        _G.tractor = StubManualAttach.tractor({ name = 'tractor', rear = { -0.4, -0.2, 2.0 }, front = { 5, -0.2, 9 } })
        _G.loader = StubManualAttach.tractor({ name = 'loader', rear = { -1.2, -0.2, 1.0 }, front = { 5, -0.2, 9 } })
        _G.rearPlow = StubManualAttach.implement('rearPlow')
        _G.other = StubManualAttach.implement('other')
        StubManualAttach.lineUp(rearPlow, tractor, 1)
        StubManualAttach.lineUp(other, loader, 1)
        StubManualAttach.detectAll()
    )luau");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    PullTrigger(kRight);
    EXPECT_EQ(Attached("rearPlow"), "true");
    EXPECT_EQ(Attached("other"), "false") << "the nearer vehicle's hitch is not the one pointed at";
}

TEST_F(VirtualGamepadManualAttachTest, ATrailersOwnHitchWorksTheSameWay) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(R"luau(
        _G.tractor = StubManualAttach.tractor({ rear = { -0.4, -0.2, 6.0 }, front = { 5, -0.2, 9 } })
        _G.trailer = StubManualAttach.trailer('trailer', { -0.4, -0.2, 2.0 })
        _G.second = StubManualAttach.implement('second')
        StubManualAttach.attach(tractor, trailer, 1, 1)
        StubManualAttach.lineUp(second, trailer, 1)
        StubManualAttach.detectAll()
    )luau");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch);
    PullTrigger(kRight);
    ASSERT_EQ(Performs("joint"), 1);
    EXPECT_EQ(EvalLua(L, "tostring(StubManualAttach.performs[1].attacherVehicle == trailer)"), "true");
    EXPECT_EQ(Attached("second"), "true") << "the trailer tows the next trailer";
}

// ---- What Manual Attach refuses ------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, ARefusedDetachStillTakesTheRayAndThePullShowsManualAttachsWarning) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua("StubManualAttach.attach(tractor, plow, 1, 1); StubManualAttach.raise(plow)");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "the hitch is pointed at although Manual Attach will refuse";
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1);
    EXPECT_EQ(EvalLua(L, "StubManualAttach.warnings[1]"), "info_lower_warning") << "Manual Attach says why";
    EXPECT_EQ(Attached("plow"), "true") << "and detaches nothing";
}

TEST_F(VirtualGamepadManualAttachTest, ADetachRefusedForAConnectedPtoOrHosesShowsTheMatchingWarning) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua("StubManualAttach.attach(tractor, plow, 1, 1); plow.ptoConnected = true");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "StubManualAttach.warnings[1]"), "info_detach_pto_warning");
    ReleaseTrigger(kRight);

    Lua("plow.ptoConnected = false; plow.hosesConnected = true");
    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "StubManualAttach.warnings[2]"), "info_detach_hoses_warning");
    EXPECT_EQ(Attached("plow"), "true");
    ReleaseTrigger(kRight);

    Lua("plow.hosesConnected = false");
    PullTrigger(kRight);
    EXPECT_EQ(Attached("plow"), "false") << "with both off it detaches";
    EXPECT_EQ(BlinkingWarnings(), 2);
}
