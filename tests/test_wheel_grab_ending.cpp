// Wheel Grab (spec #89, ticket #94, ADR-0027): a grab never outlives the situation it was made in. The real
// Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with a cab vehicle that steers the way the
// game's does (companion_mod_stubs.h, StubSteer) and a stub menu state (g_gui). Each situation that ends a grab is
// brought about mid-grab, and the test asserts only what the player sees and what the game was told: whether the
// Steering Wheel is still held, the steering calls that reached the vehicle, how the wheel turned, and that a grip
// still squeezed when the situation is over does not take hold again until it is let go and squeezed afresh.
// The cab and the geometry are those of test_wheel_grab.cpp (see the top of that file).

#include "wheel_grab_test_support.h"
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace wheel_grab_test;

// A cab with a steerable vehicle, and a second steerable vehicle the player can switch to.
std::string SteerableCabAndAnotherVehicle() {
    return "StubSteer.install()\n"
           "local cab = StubScene.cab({})\n"
           "StubSteer.equip(cab.vehicle, { parent = cab.parent })\n"
           "_G.cab = cab\n"
           "_G.otherVehicle = StubSteer.equip({ getActiveCamera = function() return cab.cam end }, { parent = cab.parent })\n";
}

void StartMod(lua_State* L, fs25vr::VRBridge& bridge) {
    ASSERT_NO_FATAL_FAILURE(StartWheelGrabMod(L, bridge, SteerableCabAndAnotherVehicle()));
}

int Calls(lua_State* L) { return static_cast<int>(Number(L, "#StubSteer.calls")); }

bool Holds(lua_State* L, uint32_t hand) {
    return EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(" + std::to_string(hand) + ")") == "true";
}

} // namespace


namespace {

// One situation that ends a grab: how it comes about and how it goes away again.
struct Situation {
    const char* name;
    std::function<void(lua_State*, FakeTrackedHeadset&)> begin;
    std::function<void(lua_State*, FakeTrackedHeadset&)> end;
    bool handStaysTracked = true;  // false: the hand is not tracked while the situation lasts
};

void Run(lua_State* L, const char* code) {
    ASSERT_EQ(RunLuauChunk(L, code, "=situation"), 0) << code;
}

std::vector<Situation> Situations() {
    auto lua = [](const char* beginCode, const char* endCode) {
        return std::pair<std::function<void(lua_State*, FakeTrackedHeadset&)>, std::function<void(lua_State*, FakeTrackedHeadset&)>>{
            [beginCode](lua_State* L, FakeTrackedHeadset&) { Run(L, beginCode); },
            [endCode](lua_State* L, FakeTrackedHeadset&) { Run(L, endCode); } };
    };
    std::vector<Situation> all;
    auto add = [&](const char* name, const char* beginCode, const char* endCode) {
        auto pair = lua(beginCode, endCode);
        all.push_back({ name, pair.first, pair.second });
    };
    add("a menu opens", "g_gui.visible = true", "g_gui.visible = false");
    add("2D Floating Window Mode starts", "VRCompanion:set2DViewActive(true)", "VRCompanion:set2DViewActive(false)");
    add("the camera becomes an Outside Camera", "StubSteer.changeCamera(cab.vehicle, { isInside = false })",
        "StubSteer.changeCamera(cab.vehicle, cab.cam)");
    add("the camera becomes a passenger camera", "StubSteer.changeCamera(cab.vehicle, { isInside = true, isPassengerCamera = true })",
        "StubSteer.changeCamera(cab.vehicle, cab.cam)");
    add("the controlled vehicle changes", "g_currentMission.controlledVehicle = otherVehicle",
        "g_currentMission.controlledVehicle = cab.vehicle");
    add("the player gets out", "g_currentMission.controlledVehicle = nil", "g_currentMission.controlledVehicle = cab.vehicle");
    add("Wheel Grab is switched off", "runCommand('vrWheelGrab', 'off')", "runCommand('vrWheelGrab', 'on')");
    add("Motion Controllers are switched off", "runCommand('vrMotionControllers', 'off')", "runCommand('vrMotionControllers', 'on')");
    all.push_back({ "the holding hand loses tracking",
        [](lua_State*, FakeTrackedHeadset& headset) { headset.SetHandTracked(kRight, false); },
        [](lua_State*, FakeTrackedHeadset& headset) { headset.SetHandTracked(kRight, true); }, false });
    return all;
}

} // namespace

TEST_F(CompanionModTest, EverySituationThatEndsAGrabLetsGoOfTheWheelAtOnceAndTheGameCentresIt) {
    for (const Situation& situation : Situations()) {
        s_capturedLogs.clear();
        lua_close(L);  // each situation gets a fresh session
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcclosure(L, &MockPrint, "print", 0);
        lua_setglobal(L, "print");
        FakeTrackedHeadset headset;
        fs25vr::VRBridge bridge(headset.manager, nullptr);
        ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
        SetGameTime(10000);

        GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
        for (int step = 1; step <= 3; ++step) {
            TURN_HAND_TO(kRight, 0.2 * step);
        }
        FRAMES(30);
        ASSERT_TRUE(Holds(L, kRight)) << situation.name << ": the wheel was held to begin with";
        ASSERT_NEAR(WheelAngle(L), 0.6, 0.02) << situation.name << ": and it was steered";

        const int callsBefore = Calls(L);
        situation.begin(L, headset);
        FRAMES(1);
        EXPECT_FALSE(Holds(L, kRight)) << situation.name << ": the hand still held the wheel";
        EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false") << situation.name;
        EXPECT_EQ(Calls(L), callsBefore) << situation.name << ": steering was sent on the next frame";

        // The hand carries on turning, the grip still squeezed.
        headset.SetHandPose(kRight, HandAtWorld(RimPoint(L, kRimRadius, 1.2)), situation.handStaysTracked);
        FRAMES(60);
        EXPECT_EQ(Calls(L), callsBefore) << situation.name << ": steering went on";
        EXPECT_NEAR(WheelAngle(L), 0.0, 0.01) << situation.name << ": the game's own steering back did not centre the wheel";
    }
}

TEST_F(CompanionModTest, AGripStillSqueezedWhenTheSituationIsOverDoesNotGrabAgainUntilItIsLetGoAndSqueezedAfresh) {
    for (const Situation& situation : Situations()) {
        s_capturedLogs.clear();
        lua_close(L);
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcclosure(L, &MockPrint, "print", 0);
        lua_setglobal(L, "print");
        FakeTrackedHeadset headset;
        fs25vr::VRBridge bridge(headset.manager, nullptr);
        ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
        SetGameTime(10000);

        GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
        TURN_HAND_TO(kRight, 0.3);
        FRAMES(10);
        ASSERT_TRUE(Holds(L, kRight)) << situation.name << ": the wheel was held to begin with";
        situation.begin(L, headset);
        FRAMES(3);
        situation.end(L, headset);
        FRAMES(3);

        // Back where it was, the hand on the rim and the grip squeezed all along: that is not a new grab.
        for (int step = 1; step <= 4; ++step) {
            TURN_HAND_TO(kRight, 0.3 + 0.1 * step);
        }
        FRAMES(10);
        const int callsAfterwards = Calls(L);
        EXPECT_FALSE(Holds(L, kRight)) << situation.name << ": the wheel was taken hold of again by a grip squeezed all along";
        TURN_HAND_TO(kRight, 1.2);
        FRAMES(10);
        EXPECT_EQ(Calls(L), callsAfterwards) << situation.name << ": steering began again with no new squeeze";

        // Let go, squeeze afresh at the rim: the wheel can be held again.
        headset.SetHandInputs(kRight, Grip(0.0f));
        FRAMES(3);
        headset.SetHandInputs(kRight, Grip(1.0f));
        FRAMES(1);
        EXPECT_TRUE(Holds(L, kRight)) << situation.name << ": a grip squeezed afresh did not take hold";
        TURN_HAND_TO(kRight, 1.5);
        FRAMES(5);
        EXPECT_GT(Calls(L), callsAfterwards) << situation.name << ": and steering did not begin again";
    }
}

TEST_F(CompanionModTest, LosingTrackingOnOneOfTwoHoldingHandsLetsGoOfOnlyThatHand) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    GRAB(kLeft, RimPoint(L, kRimRadius, kPi));
    ASSERT_TRUE(Holds(L, kRight));
    ASSERT_TRUE(Holds(L, kLeft)) << "both hands hold";

    headset.SetHandTracked(kLeft, false);
    FRAMES(1);
    EXPECT_FALSE(Holds(L, kLeft)) << "the hand that lost tracking let go";
    EXPECT_TRUE(Holds(L, kRight)) << "the other hand still holds";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "true");

    // The wheel follows the remaining hand alone, by the whole of its turn.
    for (int step = 1; step <= 3; ++step) {
        TURN_HAND_TO(kRight, 0.1 * step);
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), 0.3, 0.02) << "the hand still holding steered the wheel on its own";

    // The hand comes back with its grip squeezed all along: it does not take hold again.
    headset.SetHandTracked(kLeft, true);
    FRAMES(1);
    headset.SetHandPose(kLeft, HandAtWorld(RimPoint(L, kRimRadius, kPi + 0.3)));
    FRAMES(3);
    EXPECT_FALSE(Holds(L, kLeft)) << "a tracked-again hand with its grip squeezed all along took hold";

    headset.SetHandInputs(kLeft, Grip(0.0f));
    FRAMES(3);
    headset.SetHandInputs(kLeft, Grip(1.0f));
    FRAMES(1);
    EXPECT_TRUE(Holds(L, kLeft)) << "squeezed afresh it holds again";
}
