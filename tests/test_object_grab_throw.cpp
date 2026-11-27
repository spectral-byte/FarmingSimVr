#include "object_grab_test_support.h"
#include <cmath>
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kBaleAhead = "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.05, { name = 'bale' })";
constexpr double kFrameSeconds = 16.667 / 1000.0;

constexpr const char* kThrowHelpers = R"luau(
    function _G.handWorld() return { getWorldTranslation(handHolder('right')) } end
    function _G.travel(a, b)   -- the unit vector from a to b
        local x, y, z = b[1] - a[1], b[2] - a[2], b[3] - a[3]
        local length = math.sqrt(x * x + y * y + z * z)
        return { x / length, y / length, z / length }
    end
    function _G.cosine(a, b) return a[1] * b[1] + a[2] * b[2] + a[3] * b[3] end
    function _G.lastThrow() return StubHandTool.throws[#StubHandTool.throws] end
    function _G.throwDirection() local t = lastThrow(); return { t.dirX, t.dirY, t.dirZ } end
)luau";

constexpr const char* kKeepListenerStill = R"luau(
    local baseSetTranslation = setTranslation
    _G.setTranslation = function(node, ...)
        if node == VRCompanion.cyclopeanAudioListenerNode then return end
        return baseSetTranslation(node, ...)
    end
)luau";

class VirtualGamepadObjectGrabThrowTest: public VirtualGamepadObjectGrabTest {
protected:
    float x = kHandX, y = kHandY, z = kHandZ;

    void StartCarrying() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_EQ(RunLuauChunk(L, kThrowHelpers, "=throwHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
        POINT_HAND(kRight, kHandX, kHandY, kHandZ);
        Squeeze(kRight, 1.0f);
        FRAMES(3);
        ASSERT_EQ(PickUps(), 1) << "the grab";
    }

    double Constant(const char* name) { return Number(L, std::string("VRCompanion.") + name); }
    double ThresholdSpeed() { return Constant("OBJECT_GRAB_THROW_MIN_SPEED"); }
    double FullSpeed() { return Constant("OBJECT_GRAB_THROW_FULL_SPEED"); }

    void Move(double speed, float dx, float dy, float dz, int frames, bool letGo = false) {
        const float step = static_cast<float>(speed * kFrameSeconds);
        for (int i = 0; i < frames; ++i) {
            x += dx * step; y += dy * step; z += dz * step;
            headset.SetHandPointing(kRight, HandAt(x, y, z));
            if (letGo && i == frames - 1) Squeeze(kRight, 0.0f);
            FRAMES(1);
        }
    }
    void SwingAndLetGo(double speed, float dx, float dy, float dz, int frames = 6) { Move(speed, dx, dy, dz, frames, true); }
    int Throws() { return static_cast<int>(Number(L, "#StubHandTool.throws")); }
};

}

TEST_F(VirtualGamepadObjectGrabThrowTest, LettingGoWhileSwingingThrowsTheHeldObjectAlongTheHandsTravelWithAForceBetweenZeroAndOne) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "_G.before = handWorld()", "=before"), 0);
    SwingAndLetGo((ThresholdSpeed() + FullSpeed()) / 2, 0.0f, 0.6f, -0.8f);
    ASSERT_EQ(RunLuauChunk(L, "_G.after = handWorld()", "=after"), 0);

    ASSERT_EQ(Throws(), 1) << "the throw";
    EXPECT_EQ(Drops(), 0) << "a throw is not also a drop";
    EXPECT_GT(Number(L, "cosine(throwDirection(), travel(before, after))"), 0.99)
        << "the direction of the hand's travel: " << EvalLua(L, "table.concat(throwDirection(), ' ')");
    EXPECT_GT(Number(L, "lastThrow().force"), 0.0);
    EXPECT_LT(Number(L, "lastThrow().force"), 1.0) << "a throw below the speed of a full throw is not a full throw";
    EXPECT_EQ(EvalLua(L, "tostring(lastThrow().node == bale.node)"), "true") << "the Held Object is what is thrown";
}



TEST_F(VirtualGamepadObjectGrabThrowTest, WithoutAGameClockTheHandHasNoSpeedSoTheLetGoIsADropAndNothingRaises) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.getCurrentTimeMs = function() return nil end", "=noClock"), 0);
    SwingAndLetGo(FullSpeed() * 2, 0.0f, 0.6f, -0.8f);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, ALetGoAtOrAboveTheSpeedOfAFullThrowGivesAForceOfExactlyOne) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    SwingAndLetGo(FullSpeed() * 1.05, 0.0f, 0.6f, -0.8f);
    ASSERT_EQ(Throws(), 1);
    EXPECT_EQ(Number(L, "lastThrow().force"), 1.0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, AFarFasterSwingIsNeverHarderThanTheGamesFullThrow) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    SwingAndLetGo(FullSpeed() * 10, 0.0f, 0.6f, -0.8f);
    ASSERT_EQ(Throws(), 1);
    EXPECT_EQ(Number(L, "lastThrow().force"), 1.0);
    EXPECT_LE(Number(L, "math.max(math.abs(lastThrow().dirX), math.abs(lastThrow().dirY), math.abs(lastThrow().dirZ))"), 1.0)
        << "the direction is a unit vector";
}

TEST_F(VirtualGamepadObjectGrabThrowTest, ALetGoBelowTheThresholdSpeedIsADropNotAThrow) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    SwingAndLetGo(ThresholdSpeed() / 2, 0.0f, 0.6f, -0.8f);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, ALetGoOfAHandHeldStillIsADrop) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    SwingAndLetGo(0.0, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, ASwingThatEndedBeforeTheLetGoIsADrop) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    Move(FullSpeed(), 0.0f, 0.6f, -0.8f, 6);
    SwingAndLetGo(0.0, 0.0f, 0.0f, 0.0f, 10);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, ASnapTurnJustBeforeTheLetGoIsNotASwing) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, "_G.before = handWorld(); assert(VRCompanion:snapTurnRight(nil, 20000) == true, 'snap turn happened')", "=snapTurn"), 0);
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, "_G.after = handWorld()", "=after"), 0);
    ASSERT_GT(Number(L, "math.sqrt((after[1]-before[1])^2 + (after[3]-before[3])^2)"), ThresholdSpeed() * 0.1)
        << "the turn moved the hand in the world (else this test proves nothing)";
    SwingAndLetGo(0.0, 0.0f, 0.0f, 0.0f, 2);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, ASnapTurnInTheSameUpdateAsTheLetGoIsNotASwingEither) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, "assert(VRCompanion:snapTurnLeft(nil, 20000) == true, 'snap turn happened')", "=snapTurn"), 0);
    Squeeze(kRight, 0.0f);
    FRAMES(1);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, ARealSwingStillThrowsAfterASnapTurnHasPassedOutOfTheWindow) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "assert(VRCompanion:snapTurnRight(nil, 20000) == true, 'snap turn happened')", "=snapTurn"), 0);
    FRAMES(2);
    SwingAndLetGo(FullSpeed(), 0.0f, 0.6f, -0.8f);
    EXPECT_EQ(Throws(), 1);
    EXPECT_EQ(Drops(), 0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, TheThrowGoesTheWayTheHandMovedInTheWorldWhateverWayThePlayerFaces) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "assert(VRCompanion:snapTurnRight(nil, 20000) == true, 'snap turn happened')", "=snapTurn"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "_G.before = handWorld()", "=before"), 0);
    SwingAndLetGo(FullSpeed(), 0.0f, 0.0f, -1.0f);
    ASSERT_EQ(RunLuauChunk(L, "_G.after = handWorld()", "=after"), 0);
    ASSERT_EQ(Throws(), 1);
    EXPECT_GT(Number(L, "cosine(throwDirection(), travel(before, after))"), 0.99) << EvalLua(L, "table.concat(throwDirection(), ' ')");
}

TEST_F(VirtualGamepadObjectGrabThrowTest, AHandTheRecenterMovedInTrackingSpaceIsNotSwingingEither) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onRecenterAction()", "=recenter"), 0);
    SwingAndLetGo(0.0, 0.0f, 0.0f, 0.0f, 2);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, AJumpOfTheTrackingSpaceInTheWorldJustBeforeTheLetGoIsNotASwing) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, kKeepListenerStill, "=keepListenerStill"), 0);
    ASSERT_EQ(RunLuauChunk(L, "_G.before = handWorld()", "=before"), 0);
    headset.SetHeadPose(HandAt(1.0f, 0.0f, 0.0f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, "_G.after = handWorld()", "=after"), 0);
    ASSERT_GT(Number(L, "math.sqrt((after[1]-before[1])^2 + (after[3]-before[3])^2)"), 0.5)
        << "the hand's place in the world jumped (else this test proves nothing)";
    SwingAndLetGo(0.0, 0.0f, 0.0f, 0.0f, 2);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0) << "a jump of the rig is not a swing";
}

TEST_F(VirtualGamepadObjectGrabThrowTest, AfterAThrowGrabbingAgainNeedsAFreshSqueeze) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    SwingAndLetGo(FullSpeed(), 0.0f, 1.0f, 0.0f);
    ASSERT_EQ(Throws(), 1);

    ASSERT_EQ(RunLuauChunk(L, ("StubHandTool.looseObject(-0.4, " + std::to_string(y) + ", 1.5, 0.05, { name = 'second' })").c_str(), "=second"), 0);
    POINT_HAND(kRight, x, y, z);
    FRAMES(5);
    EXPECT_EQ(PickUps(), 1) << "the grip is up: the hand pointing at a bale grabs nothing";
    Squeeze(kRight, 1.0f);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 2) << "a fresh squeeze grabs it";
}

TEST_F(VirtualGamepadObjectGrabThrowTest, TheLastReleaseIsKeptForTheDiagnostics) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    EXPECT_EQ(EvalLua(L, "tostring(VRCompanion:getObjectGrabLastRelease())"), "nil") << "nothing has been let go yet";
    SwingAndLetGo((ThresholdSpeed() + FullSpeed()) / 2, 0.0f, 0.6f, -0.8f);
    ASSERT_EQ(Throws(), 1);
    EXPECT_EQ(EvalLua(L, "(VRCompanion:getObjectGrabLastRelease())"), "throw");
    EXPECT_NEAR(Number(L, "select(2, VRCompanion:getObjectGrabLastRelease())"), (ThresholdSpeed() + FullSpeed()) / 2, 0.3) << "the hand's speed";
    EXPECT_NEAR(Number(L, "select(6, VRCompanion:getObjectGrabLastRelease())"), Number(L, "lastThrow().force"), 1e-9) << "the force scalar";
    EXPECT_NEAR(Number(L, "select(4, VRCompanion:getObjectGrabLastRelease())"), Number(L, "lastThrow().dirY"), 1e-9) << "the direction";
}

TEST_F(VirtualGamepadObjectGrabThrowTest, SwingingBeforeOpeningAMenuDoesNotThrowWithStaleVelocityOnPostPauseRelease) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    Move(FullSpeed(), 0.0f, 0.6f, -0.8f, 6);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=openMenu"), 0);
    FRAMES(2);
    EXPECT_EQ(Throws(), 0);
    EXPECT_EQ(Drops(), 0);

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    Squeeze(kRight, 0.0f);
    FRAMES(1);
    EXPECT_EQ(Drops(), 1) << "post-pause release drops instead of launching with pre-menu swing velocity";
    EXPECT_EQ(Throws(), 0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, SwingingBefore2DFloatingWindowModeDoesNotThrowWithStaleVelocity) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    Move(FullSpeed(), 0.0f, 0.6f, -0.8f, 6);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:set2DViewActive(true)", "=enter2D"), 0);
    FRAMES(2);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:set2DViewActive(false)", "=exit2D"), 0);
    Squeeze(kRight, 0.0f);
    FRAMES(1);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, SwingingBeforeTrackingLossDoesNotThrowWithStaleVelocityWhenTrackingReturns) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    Move(FullSpeed(), 0.0f, 0.6f, -0.8f, 6);
    headset.SetHandPose(kRight, HandAt(x, y, z), false);
    headset.SetHandGripPose(kRight, HandAt(x, y, z), false);
    FRAMES(2);

    headset.SetHandPointing(kRight, HandAt(x, y, z));
    Squeeze(kRight, 0.0f);
    FRAMES(1);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0);
}

TEST_F(VirtualGamepadObjectGrabThrowTest, ReleasingAfterAThrowWindowGapWithNoFreshSamplesIsADropNotAThrow) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    Move(FullSpeed(), 0.0f, 0.6f, -0.8f, 6);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.menuTakesInput = true", "=pauseInput"), 0);
    FRAMES(1);
    AdvanceGameTime(200);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.menuTakesInput = false", "=resumeInput"), 0);
    Squeeze(kRight, 0.0f);
    FRAMES(1);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0);
}
