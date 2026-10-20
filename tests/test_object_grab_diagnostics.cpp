// Object Grab, vrObjectGrabDiagnostics (ADR-0033, spec #156, ticket #163), seen from outside: the real Companion Mod runs against the real
// VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (the game's empty hands and loose objects in
// object_grab_test_support.h). The command writes tagged lines to the game log ("[FS25_VR] ...", one field each) that headset testing reads,
// and returns them as one line. The tests run the command in the four situations headset testing meets - idle, aiming, carrying and
// just thrown - and assert the logged text. Expected numbers come from the game's side (the stub's own limits, objects and records of
// what the game was called with), not from the mod. Running the command touches nothing: no pulse, no wrap, the same answer twice.
// Prior art: test_object_grab_throw.cpp (the carry and the throw) and test_wheel_grab.cpp (VrWheelGrabDiagnostics...).

#include "object_grab_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

// A hand pointing at tracking (0.4, -0.2, -0.3) has its fingertip at world (-0.4, -0.2, 0.3) and the Pointer Ray runs along +Z. The bale is
// straight ahead of it, its centre 1.2 m from the fingertip and its surface (radius 0.15 m) 1.05 m.
constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kBaleAhead = "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.05, { name = 'bale' })";
constexpr double kFrameSeconds = 16.667 / 1000.0;

// The game's HandToolHands.calculateItemMass(node) (a static function of the class; the stub does not have it): the object's mass in tonnes.
constexpr const char* kMassOfNode =
    "function HandToolHands.calculateItemMass(node) local o = StubHandTool.looseObjects[node]; return o and o.mass or nil end";

class VirtualGamepadObjectGrabDiagnosticsTest : public VirtualGamepadObjectGrabTest {
protected:
    void StartWithMass() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_EQ(RunLuauChunk(L, kMassOfNode, "=massOfNode"), 0);
    }

    // The right hand pointing at the bale (the Pointer Hand).
    void Aim() {
        ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
        POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    }

    // The right hand has grabbed the bale.
    void Carry() {
        ASSERT_NO_FATAL_FAILURE(Aim());
        Squeeze(kRight, 1.0f);
        FRAMES(3);
        ASSERT_EQ(PickUps(), 1) << "the grab";
    }

    // Runs vrObjectGrabDiagnostics: everything it logged, and what it answered.
    std::string Logged() const {
        std::string all;
        for (const auto& line : s_capturedLogs) all += line + "\n";
        return all;
    }
    std::string Run(std::string* answer = nullptr) {
        s_capturedLogs.clear();
        const std::string reply = EvalLua(L, "runCommand('vrObjectGrabDiagnostics')");
        if (answer != nullptr) *answer = reply;
        return Logged();
    }
    static bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

    // Moves the right hand along (dx, dy, dz) at `speed` m/s (tracking space, a unit vector) for `frames` updates.
    void Move(double speed, float dx, float dy, float dz, int frames, bool letGo = false) {
        const float step = static_cast<float>(speed * kFrameSeconds);
        for (int i = 0; i < frames; ++i) {
            x += dx * step; y += dy * step; z += dz * step;
            headset.SetHandPointing(kRight, HandAt(x, y, z));
            if (letGo && i == frames - 1) Squeeze(kRight, 0.0f);
            FRAMES(1);
        }
    }
    double Constant(const char* name) { return Number(L, std::string("VRCompanion.") + name); }
    float x = kHandX, y = kHandY, z = kHandZ;  // where the right hand is, in tracking space
};

}  // namespace

// ---- Idle -----------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, IdleItSaysObjectGrabIsOnNothingAimsAndReadsTheGamesLimits) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    FRAMES(3);
    std::string answer;
    const std::string log = Run(&answer);

    EXPECT_TRUE(Has(log, "[FS25_VR] Object Grab: off (there is no Pointer Hand")) << "the setting is on; nothing points yet: " << log;
    EXPECT_TRUE(Has(log, "[FS25_VR] aiming: no")) << log;
    EXPECT_TRUE(Has(log, "[FS25_VR] Pointer Ray on: nothing")) << log;
    EXPECT_TRUE(Has(log, "maximum mass 0.100 t")) << "the game's currentMaximumMass, read at run time: " << log;
    EXPECT_TRUE(Has(log, "pickup distance 3.00 m")) << "the game's pickupDistance: " << log;
    EXPECT_TRUE(Has(log, "super strength off")) << log;
    EXPECT_TRUE(Has(log, "[FS25_VR] carrying: no")) << log;
    EXPECT_TRUE(Has(log, "[FS25_VR] last release: none")) << log;
    EXPECT_TRUE(Has(log, "[FS25_VR] last snap: none")) << log;
    EXPECT_TRUE(Has(log, "getLookRay wrap: installed")) << log;
    EXPECT_TRUE(Has(log, "handsJointBroken wrap: installed")) << log;
    EXPECT_TRUE(Has(answer, "Object Grab: off (there is no Pointer Hand") && Has(answer, " | last snap: none")) << "the reply is the fields in one line: " << answer;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, EveryFunctionObjectGrabWrapsHasAWrapStatusInNameOrderAndOneItOnlyChecksHasNone) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    std::string answer;
    Run(&answer);
    size_t at = 0;
    for (const char* name : { "getLookRay", "handsJointBroken", "handsOnDraw", "handsUpdateKinematicNode" }) {
        const size_t found = answer.find(std::string(name) + " wrap: installed", at);
        ASSERT_NE(found, std::string::npos) << name << " is reported, after the one before it, as installed: " << answer;
        at = found;
    }
    EXPECT_FALSE(Has(answer, "getCanPickUpNode wrap")) << "it is only checked, never wrapped: " << answer;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, TheLimitsAreReadFromTheGameEachTimeSoSuperStrengthShowsAtOnce) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.superStrength(hands, true)", "=superStrength"), 0);
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "maximum mass 1000.000 t")) << log;
    EXPECT_TRUE(Has(log, "pickup distance 10.00 m")) << log;
    EXPECT_TRUE(Has(log, "super strength on")) << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, RunningItChangesNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_NO_FATAL_FAILURE(Aim());
    FRAMES(3);
    const std::string before = EvalLua(L, "VRCompanion:getObjectGrabDiagnostics()");
    const size_t pulses = headset.HapticPulses().size();
    const std::string rayBefore = EvalLua(L, "rayText(targeterRay())");
    const std::string first = Run();
    const std::string second = Run();
    EXPECT_TRUE(Has(first, "Object Grab: on (aiming)")) << "it did log: " << first;
    EXPECT_EQ(first, second) << "the same answer twice";
    EXPECT_EQ(EvalLua(L, "VRCompanion:getObjectGrabDiagnostics()"), before);
    EXPECT_EQ(headset.HapticPulses().size(), pulses) << "nothing is felt";
    EXPECT_EQ(PickUps(), 0) << "and nothing is picked up";
    EXPECT_EQ(Drops(), 0);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "rayText(targeterRay())"), rayBefore) << "the look ray is as it was";
}

// ---- Why Object Grab is off ---------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, WhenTheSettingIsOffItSaysSoAndHowToTurnItOn) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrObjectGrab', 'off')", "=off"), 0);
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "[FS25_VR] Object Grab: off (the setting is off")) << log;
    EXPECT_TRUE(Has(log, "vrObjectGrab on")) << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, WithNoPointerHandItSaysObjectGrabHasNothingToAimWith) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "[FS25_VR] Object Grab: off (there is no Pointer Hand")) << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, InAVehicleItSaysObjectGrabIsOff) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_NO_FATAL_FAILURE(Aim());
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.getControlledVehicle = function() return {} end", "=vehicle"), 0);
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "[FS25_VR] Object Grab: off (in a vehicle")) << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, WithAHandToolInHandItSaysSo) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_NO_FATAL_FAILURE(Aim());
    ASSERT_EQ(RunLuauChunk(L, "foot.player.currentHandTool = { spec_sprayCan = {} }", "=handTool"), 0);
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "[FS25_VR] Object Grab: off (a Hand Tool is in hand")) << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, WhenAWrapFailedItSaysWhichGameFunctionWasWrong) {
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, StartOptions(), "canPickUpReshaped"));
    SetGameTime(10000);
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "[FS25_VR] Object Grab: off (Object Grab gave up (HandToolHands.getCanPickUpNode")) << log;
}

// ---- Aiming -------------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, AimingItNamesTheHandAndTheObjectTheRayIsOnWithItsMassAgainstTheLimit) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_NO_FATAL_FAILURE(Aim());
    FRAMES(3);
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "[FS25_VR] Object Grab: on (aiming)")) << log;
    EXPECT_TRUE(Has(log, "[FS25_VR] aiming: the right hand")) << log;
    EXPECT_TRUE(Has(log, "Pointer Ray on: an object 1.05 m from the fingertip")) << log;
    EXPECT_TRUE(Has(log, "mass 0.050 t of 0.100 t")) << "its mass against currentMaximumMass: " << log;
    EXPECT_TRUE(Has(log, "the hands can pick it up")) << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, AnObjectTooHeavyForTheHandsIsSaidToBeTooHeavy) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_EQ(RunLuauChunk(L, "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.5, 5.0, { name = 'heavy' })", "=heavy"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(3);
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "mass 5.000 t of 0.100 t")) << log;
    EXPECT_TRUE(Has(log, "the hands cannot pick it up (too heavy")) << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, AnObjectBeyondThePickupDistanceIsNotTheRaysTarget) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_EQ(RunLuauChunk(L, "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 6.0, 0.05, { name = 'far' })", "=far"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(3);
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "Pointer Ray on: nothing")) << "the targeter finds nothing beyond the pickup distance of " << "3.00 m: " << log;
}

// ---- Carrying -----------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, CarryingItNamesTheHandTheGrabOffsetAndThatTheObjectIsWithTheHand) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_NO_FATAL_FAILURE(Carry());
    FRAMES(5);
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "[FS25_VR] Object Grab: on (carrying)")) << log;
    EXPECT_TRUE(Has(log, "[FS25_VR] carrying: the right hand")) << log;
    EXPECT_TRUE(Has(log, "grab offset (")) << log;
    EXPECT_TRUE(Has(log, "trails the hand by 0.00 m")) << "a hand held still: the object is where the hand puts it: " << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, TheGrabOffsetIsHowFarTheObjectWasFromTheHandWhenGrabbed) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_NO_FATAL_FAILURE(Carry());
    FRAMES(3);
    const std::string log = Run();
    // The bale's centre against the Virtual Hand the player sees, measured in the world by the test.
    const double gap = Number(L, "apart({ getWorldTranslation(handHolder('right')) }, { getWorldTranslation(bale.node) })");
    ASSERT_GT(gap, 0.0);
    char expected[64];
    snprintf(expected, sizeof expected, "%.2f m from the hand", gap);
    EXPECT_TRUE(Has(log, expected)) << "expected the offset to be " << expected << " in: " << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, AHandThatJumpedLeavesTheObjectTrailingByTheJump) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_NO_FATAL_FAILURE(Carry());
    FRAMES(3);
    // One update, 0.3 m up: the game places the object (before the mod's update) where the hand was.
    y += 0.3f;
    headset.SetHandPointing(kRight, HandAt(x, y, z));
    FRAMES(1);
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "trails the hand by 0.30 m")) << log;
}

// ---- Just let go ----------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, AfterAThrowItGivesTheHandsSpeedTheDirectionAndTheForceScalar) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_NO_FATAL_FAILURE(Carry());
    const double speed = (Constant("OBJECT_GRAB_THROW_MIN_SPEED") + Constant("OBJECT_GRAB_THROW_FULL_SPEED")) / 2;
    Move(speed, 0.0f, 0.6f, -0.8f, 6, true);
    ASSERT_EQ(Number(L, "#StubHandTool.throws"), 1.0) << "the throw";
    ASSERT_EQ(RunLuauChunk(L, "_G.thrown = StubHandTool.throws[#StubHandTool.throws]", "=thrown"), 0);

    const std::string log = Run();
    char expected[160];
    snprintf(expected, sizeof expected, "[FS25_VR] last release: throw, hand speed %.1f m/s", speed);
    EXPECT_TRUE(Has(log, expected)) << "expected " << expected << " in: " << log;
    snprintf(expected, sizeof expected, "force %.2f", Number(L, "thrown.force"));
    EXPECT_TRUE(Has(log, expected)) << "the force scalar the game was called with: " << log;
    snprintf(expected, sizeof expected, "direction (%.2f, %.2f, %.2f)", Number(L, "thrown.dirX"), Number(L, "thrown.dirY"),
             Number(L, "thrown.dirZ"));
    EXPECT_TRUE(Has(log, expected)) << "the direction the game was called with: " << log;
    EXPECT_TRUE(Has(log, "[FS25_VR] carrying: no")) << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, AfterADropItSaysDropAndTheHandsSpeed) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_NO_FATAL_FAILURE(Carry());
    Move(0.0, 0.0f, 0.0f, 0.0f, 3, true);
    ASSERT_EQ(Drops(), 1);
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "[FS25_VR] last release: drop, hand speed 0.0 m/s")) << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, AfterASnapItSaysWhenTheLastSnapWas) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_NO_FATAL_FAILURE(Carry());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.breakJoint(hands)", "=snap"), 0);
    FRAMES(60);  // about a second
    const std::string log = Run();
    EXPECT_TRUE(Has(log, "[FS25_VR] last snap: 1 s ago")) << log;
    EXPECT_TRUE(Has(log, "[FS25_VR] last release: none")) << "a snap is not a release: " << log;
    EXPECT_TRUE(Has(log, "[FS25_VR] carrying: no")) << log;
}

TEST_F(VirtualGamepadObjectGrabDiagnosticsTest, ThePointerDiagnosticsCarryTheSameFields) {
    ASSERT_NO_FATAL_FAILURE(StartWithMass());
    ASSERT_NO_FATAL_FAILURE(Aim());
    FRAMES(3);
    const std::string pointer = EvalLua(L, "VRCompanion:getPointerDiagnostics()");
    EXPECT_TRUE(Has(pointer, "Object Grab: on (aiming)")) << pointer;
    EXPECT_TRUE(Has(pointer, "last release: none")) << pointer;
}

