// Object Grab, letting go while swinging throws (ADR-0033, spec #156, ticket #160), seen from outside: the real Companion Mod runs against
// the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (the game's empty hands and loose objects in
// object_grab_test_support.h). A test carries a bale with the right hand, swings the fake Motion Controller at a chosen speed, lets go of
// the grip, and asserts which of the game's functions was called: throwHeldItemWithForceVector (StubHandTool.throws: direction and force
// scalar) or dropHeldItem (StubHandTool.drops). Speeds are taken from the mod's named constants (the headset ticket tunes them), so a
// retuned threshold does not break a test. The direction is checked against where the Virtual Hand, the hand the player sees, travelled.

#include "object_grab_test_support.h"
#include <cmath>
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

// A hand pointing at tracking (0.4, -0.2, -0.3) has its fingertip at world (-0.4, -0.2, 0.3) and the Pointer Ray runs along +Z. The bale is
// straight ahead of it, 1.2 m from the fingertip.
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

class VirtualGamepadObjectGrabThrowTest : public VirtualGamepadObjectGrabTest {
protected:
    float x = kHandX, y = kHandY, z = kHandZ;  // where the right hand is, in tracking space

    // The right hand pointing at the bale and squeezing: the bale is its Held Object.
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

    // The hand moves at `speed` m/s along (dx, dy, dz) (tracking space, a unit vector) for `frames` updates. With `letGo` the grip is let
    // go in the last one: the hand is still moving when the release is seen.
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

}  // namespace

// ---- Letting go while swinging throws -------------------------------------------------------------------------------------------

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

// ---- Below the threshold the release is a drop ----------------------------------------------------------------------------------

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
    SwingAndLetGo(0.0, 0.0f, 0.0f, 0.0f, 10);  // the hand has come to rest by the time the grip is let go
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(Throws(), 0);
}

// ---- The velocity is the hand's in the world --------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabThrowTest, ASnapTurnJustBeforeTheLetGoIsNotASwing) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, "_G.before = handWorld(); assert(VRCompanion:snapTurnRight(nil, 20000) == true, 'snap turn happened')", "=snapTurn"), 0);
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, "_G.after = handWorld()", "=after"), 0);
    ASSERT_GT(Number(L, "math.sqrt((after[1]-before[1])^2 + (after[3]-before[3])^2)"), ThresholdSpeed() * 0.1)
        << "the turn moved the hand in the world (else this test proves nothing)";
    SwingAndLetGo(0.0, 0.0f, 0.0f, 0.0f, 2);  // the hand is held still, and let go of next
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

// ---- After a throw ------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabThrowTest, AfterAThrowGrabbingAgainNeedsAFreshSqueeze) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    SwingAndLetGo(FullSpeed(), 0.0f, 1.0f, 0.0f);
    ASSERT_EQ(Throws(), 1);

    // Another bale is straight ahead of where the hand ended up.
    ASSERT_EQ(RunLuauChunk(L, ("StubHandTool.looseObject(-0.4, " + std::to_string(y) + ", 1.5, 0.05, { name = 'second' })").c_str(), "=second"), 0);
    POINT_HAND(kRight, x, y, z);
    FRAMES(5);
    EXPECT_EQ(PickUps(), 1) << "the grip is up: the hand pointing at a bale grabs nothing";
    Squeeze(kRight, 1.0f);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 2) << "a fresh squeeze grabs it";
}

// ---- What the last release did, for the diagnostics ------------------------------------------------------------------------------

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
