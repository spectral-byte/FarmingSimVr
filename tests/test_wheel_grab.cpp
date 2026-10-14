// Wheel Grab (spec #89, ADR-0027), seen from outside: the real Companion Mod runs against the
// real VRBridge over FakeTrackedHeadset, with a cab vehicle that steers the way the game's does
// (companion_mod_stubs.h, StubSteer). The test moves the fake Motion Controllers, drives frames the way the
// game would and asserts only what the player would see and what the game was told: the steering calls that
// reached the vehicle and how far the Steering Wheel turned. Prior art: the Virtual Hands tests
// (test_virtual_hands.cpp) and the Interactive Control auto-on tests (test_interactive_control_auto_on.cpp).
//
// The cab is at the world origin facing +Z with the head at tracking space origin, so a point at world
// (x, y, z) is where a hand at tracking (-x, y - 1.5, -z) is. The Steering Wheel hangs 0.5 m in front of the
// head and 0.3 m below it, leaning toward the driver; a hand "at the rim" is 0.18 m from its axis.

#include "wheel_grab_test_support.h"
#include <string>

namespace {

using namespace wheel_grab_test;

// Stubs of the engine's XML settings API that remember what was saved, so a "session" can be replayed into a fresh
// Lua state (the same as test_motion_controllers.cpp).
constexpr const char* kXmlStubs = R"luau(
    _G.savedXml = _G.savedXml or {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function() return true end
    _G.createXMLFile = function() return 1 end
    _G.setXMLFloat = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLBool = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLString = function(file, key, value) _G.savedXml[key] = value end
    _G.saveXMLFile = function() return true end
    _G.delete = function() end
    _G.loadXMLFile = function() return 2 end
    _G.getXMLFloat = function(file, key) local v = _G.savedXml[key]; return type(v) == "number" and v or nil end
    _G.getXMLBool = function(file, key) local v = _G.savedXml[key]; if type(v) == "boolean" then return v end return nil end
    _G.getXMLString = function(file, key) local v = _G.savedXml[key]; return type(v) == "string" and v or nil end
)luau";

// The mod started on a map with a given scene (a steerable cab by default).
void StartMod(lua_State* L, fs25vr::VRBridge& bridge, const std::string& scene = SteerableCab(),
              const char* extraStubs = "") {
    StartOptions options;
    options.noPullAway = true;
    options.extraStubs = extraStubs;
    ASSERT_NO_FATAL_FAILURE(StartWheelGrabMod(L, bridge, scene, options));
}

} // namespace

TEST_F(CompanionModTest, TurningAHandHeldAtTheRimTurnsTheSteeringWheelByTheSameAngle) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    for (int step = 1; step <= 6; ++step) {
        TURN_HAND_TO(kRight, 0.5 + 0.1 * step);
    }
    FRAMES(30);  // the game's analog steering catches up with the hand

    EXPECT_NEAR(WheelAngle(L), 0.6, 0.02) << "the Steering Wheel turned as far as the hand did";
    TURN_HAND_TO(kRight, 0.5 - 0.2);
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), -0.2, 0.02) << "and back past where it started, in the other direction";
}

TEST_F(CompanionModTest, WheelGrabSteersTheVehicleAsAnAnalogWheelPeripheral) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    TURN_HAND_TO(kRight, 0.5 + 0.5);
    FRAMES(5);

    EXPECT_GT(Number(L, "#StubSteer.calls"), 0.0) << "the vehicle was told to steer";
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        for index, call in ipairs(StubSteer.calls) do
            assert(call.analog == true, "call " .. index .. " was not analog")
            assert(call.category == InputDevice.CATEGORY.WHEEL, "call " .. index .. " was not a Wheel Peripheral's")
            assert(math.abs(call.value) <= 1, "call " .. index .. " was beyond full lock")
        end
    )luau", "=callShape"), 0);
}

TEST_F(CompanionModTest, SqueezingTheGripOutsideTheRimRingDoesNothingToSteering) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    struct Spot { double radius, lift; const char* where; };
    for (const Spot& spot : { Spot{ 0.40, 0.0, "beyond the rim" }, Spot{ 0.04, 0.0, "on the hub" },
                              Spot{ 0.18, 0.15, "well in front of the Steering Wheel's plane" } }) {
        GRAB(kRight, RimPoint(L, spot.radius, 0.5, spot.lift));
        for (int step = 1; step <= 4; ++step) {
            headset.SetHandPose(kRight, HandAtWorld(RimPoint(L, spot.radius, 0.5 + 0.2 * step, spot.lift)));
            FRAMES(2);
        }
        headset.SetHandInputs(kRight, Grip(0.0f));
        FRAMES(2);
        EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0") << "a grip squeezed " << spot.where << " steered";
        EXPECT_NEAR(WheelAngle(L), 0.0, 1e-6) << "a grip squeezed " << spot.where << " turned the Steering Wheel";
    }
}

TEST_F(CompanionModTest, EveryGripSqueezeIsLoggedWithWhereTheHandWasAndWhetherItTookTheSteeringWheel) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, 0.40, 0.5));  // beyond the rim
    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(2);
    EXPECT_EQ(CountLogLines("Wheel Grab: right grip squeezed, Steering Wheel not taken: "), 1) << "the miss is logged once";
    EXPECT_EQ(CountLogLines("m from the axis"), 1) << "with how far from the axis and the plane the hand was";

    GRAB(kRight, RimPoint(L, 0.18, 0.5));  // on the rim
    EXPECT_EQ(CountLogLines("Wheel Grab: right grip squeezed, Steering Wheel taken"), 1) << "the grab is logged once";
    FRAMES(10);
    EXPECT_EQ(CountLogLines("grip squeezed"), 2) << "holding on adds no more lines";
}

TEST_F(CompanionModTest, TurningStartsFromTheSteeringWheelsActualAngleSoGrabbingNeverJumpsIt) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    // Steered 0.5 of the way to full lock: 1.5 turns of the Steering Wheel's 540 degrees at full lock, past the half
    // turn at which a node's Euler angles wrap. A slow steering duration keeps the game from centring the Steering Wheel
    // while the hand is brought up, and from catching up with it quickly afterwards.
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("rotatedTime = 0.5, steeringDuration = 100")));
    SetGameTime(10000);
    const double start = 0.5 * 3.0 * 3.14159265358979;

    GRAB(kRight, RimPoint(L, kRimRadius, 1.0));  // the hand takes hold of the rim wherever it is, not where the Steering Wheel's angle is
    FRAMES(20);
    EXPECT_NEAR(WheelAngle(L), start, 0.02) << "the Steering Wheel stayed where it was when the hand took hold";

    TURN_HAND_TO(kRight, 1.0 + 0.3);
    FRAMES(150);
    EXPECT_NEAR(WheelAngle(L), start + 0.3, 0.02) << "turning the hand turned the Steering Wheel on from its own angle";
}

TEST_F(CompanionModTest, TheSteeringWheelStopsDeadAtFullLockAndTurnsBackAtOnce) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    // A tracked tractor's Steering Wheel turns 120 degrees at full lock.
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("rotationDeg = 120")));
    SetGameTime(10000);
    const double fullLock = 120.0 * 3.14159265358979 / 180.0;

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    for (int step = 1; step <= 15; ++step) {
        TURN_HAND_TO(kRight, 0.2 * step);  // the hand carries on to 3 radians, a good way past full lock
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), fullLock, 0.01) << "the Steering Wheel stopped at full lock";
    EXPECT_NEAR(Number(L, "StubSteer.calls[#StubSteer.calls].value"), -1.0, 1e-6)
        << "at full lock the vehicle was told to steer all the way";

    // Back from the stop by 0.3 radian of hand turn: the Steering Wheel follows at once, no slack to wind back first.
    for (int step = 1; step <= 3; ++step) {
        TURN_HAND_TO(kRight, 3.0 - 0.1 * step);
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), fullLock - 0.3, 0.02) << "turning back from the stop moved the Steering Wheel straight away";

    for (int step = 1; step <= 40; ++step) {
        TURN_HAND_TO(kRight, 2.7 - 0.2 * step);  // all the way round the other way, to -5.3 radians
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), -fullLock, 0.01) << "and the Steering Wheel stops at full lock the other way too";
    EXPECT_NEAR(Number(L, "StubSteer.calls[#StubSteer.calls].value"), 1.0, 1e-6);
}

TEST_F(CompanionModTest, LettingGoOfTheGripStopsSteeringAndTheGameCentresTheSteeringWheel) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    for (int step = 1; step <= 5; ++step) {
        TURN_HAND_TO(kRight, 0.2 * step);
    }
    FRAMES(20);
    ASSERT_NEAR(WheelAngle(L), 1.0, 0.02);

    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(1);
    const std::string callsAtRelease = EvalLua(L, "#StubSteer.calls");
    TURN_HAND_TO(kRight, 2.0);  // the hand carries on turning, no longer holding
    FRAMES(60);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), callsAtRelease) << "nothing was sent after the grip was let go";
    EXPECT_NEAR(WheelAngle(L), 0.0, 0.01) << "the game's own steering back centred the Steering Wheel";
}

TEST_F(CompanionModTest, TheGripGrabsAtTheMenuBackThresholdAndKeepsHoldingUntilItIsAlmostOpen) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    BRING_HAND(kRight, RimPoint(L, kRimRadius, 0.0));
    headset.SetHandInputs(kRight, Grip(0.5f));  // a half squeeze is not a grab
    FRAMES(2);
    TURN_HAND_TO(kRight, 0.4);
    FRAMES(10);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0") << "a grip at 0.5 did not take the Steering Wheel";

    TURN_HAND_TO(kRight, 0.0);
    headset.SetHandInputs(kRight, Grip(0.7f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Grip(0.4f));  // eased off, still more than the release threshold
    FRAMES(2);
    TURN_HAND_TO(kRight, 0.4);
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), 0.4, 0.02) << "a grip eased to 0.4 still held the Steering Wheel";

    headset.SetHandInputs(kRight, Grip(0.2f));
    FRAMES(1);
    const std::string callsAtRelease = EvalLua(L, "#StubSteer.calls");
    TURN_HAND_TO(kRight, 1.0);
    FRAMES(5);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), callsAtRelease) << "below 0.3 the Steering Wheel was let go";
}

TEST_F(CompanionModTest, AGripAlreadySqueezedWhenTheHandReachesTheRingDoesNotGrab) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    BRING_HAND(kRight, RimPoint(L, 0.5, 0.0));  // a fist away from the Steering Wheel
    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(2);
    for (int step = 1; step <= 5; ++step) {
        headset.SetHandPose(kRight, HandAtWorld(RimPoint(L, 0.5 - 0.06 * step, 0.0)));  // into the ring with the fist closed
        FRAMES(2);
    }
    TURN_HAND_TO(kRight, 0.5);
    FRAMES(10);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0") << "a fist swept through the ring did not take the Steering Wheel";
}

// ---- Where Wheel Grab is available ----

TEST_F(CompanionModTest, ThereIsNoWheelGrabInAVehicleWithoutASteeringWheelAndTheLogSaysWhyOnce) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("noWheel = true")));
    SetGameTime(10000);

    TRY_TO_STEER(kRight);
    TRY_TO_STEER(kLeft);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0") << "a vehicle with no Steering Wheel was steered";
    EXPECT_EQ(CountLogLines("Wheel Grab: unavailable (the vehicle has no Steering Wheel)"), 1) << "said once, not every frame";
}

TEST_F(CompanionModTest, ThereIsNoWheelGrabInAnOutsideCameraOrFromAPassengerSeatButItComesBackInTheCab) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    ASSERT_EQ(RunLuauChunk(L, "StubSteer.changeCamera(cab.vehicle, { isInside = false })", "=outsideCamera"), 0);
    TRY_TO_STEER(kRight);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0") << "steered from an Outside Camera";

    ASSERT_EQ(RunLuauChunk(L, "StubSteer.changeCamera(cab.vehicle, { isInside = true, isPassengerCamera = true })", "=passengerCamera"), 0);
    TRY_TO_STEER(kRight);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0") << "steered from a passenger camera";

    ASSERT_EQ(RunLuauChunk(L, "StubSteer.changeCamera(cab.vehicle, cab.cam)", "=cabCamera"), 0);
    TRY_TO_STEER(kRight);
    EXPECT_GT(Number(L, "#StubSteer.calls"), 0.0) << "back in the cab camera the Steering Wheel can be held again";
}

TEST_F(CompanionModTest, ThereIsNoWheelGrabOnFoot) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab() + "StubScene.onFoot({})\n"));
    SetGameTime(10000);

    TRY_TO_STEER(kRight);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0");
}

TEST_F(CompanionModTest, ThereIsNoWheelGrabWhileTheVehicleCouldNotBeSteeredFromTheKeyboard) {
    for (const char* option : { "powered = false", "controlAllowed = false" }) {
        s_capturedLogs.clear();
        lua_close(L);  // each setting gets a fresh session
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcclosure(L, &MockPrint, "print", 0);
        lua_setglobal(L, "print");
        FakeTrackedHeadset headset;
        fs25vr::VRBridge bridge(headset.manager, nullptr);
        ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab(option)));
        SetGameTime(10000);

        TRY_TO_STEER(kRight);
        EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0") << "steered with " << option;
    }
}

// ---- The settings and the console commands ----

TEST_F(CompanionModTest, VrWheelGrabSwitchesWheelGrabOffOnItsOwnAndBackOn) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    EXPECT_NE(EvalLua(L, "runCommand('vrWheelGrab')").find("Wheel Grab is on"), std::string::npos) << "Wheel Grab starts on";

    EXPECT_NE(EvalLua(L, "runCommand('vrWheelGrab', 'off')").find("set to: off"), std::string::npos);
    TRY_TO_STEER(kRight);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0") << "steered with vrWheelGrab off";
    EXPECT_EQ(EvalLua(L, "VRCompanion.motionControllersEnabled"), "true") << "the Motion Controllers stay on";

    EXPECT_NE(EvalLua(L, "runCommand('vrWheelGrab', 'on')").find("set to: on"), std::string::npos);
    TRY_TO_STEER(kRight);
    EXPECT_GT(Number(L, "#StubSteer.calls"), 0.0) << "and back on";

    EXPECT_NE(EvalLua(L, "runCommand('vrWheelGrab', 'maybe')").find("Usage"), std::string::npos);
}

TEST_F(CompanionModTest, ThereIsNoWheelGrabWithMotionControllersOff) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);

    TRY_TO_STEER(kRight);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0");
}

TEST_F(CompanionModTest, SwitchingWheelGrabOrMotionControllersOffLetsGoOfTheSteeringWheelAndItIsNotHeldAgainBySwitchingOnAgain) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    for (const char* setting : { "vrWheelGrab", "vrMotionControllers" }) {
        GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
        TURN_HAND_TO(kRight, 0.2);
        FRAMES(3);
        ASSERT_EQ(RunLuauChunk(L, (std::string("runCommand('") + setting + "', 'off')").c_str(), "=off"), 0);
        FRAMES(2);
        const std::string callsWhenOff = EvalLua(L, "#StubSteer.calls");
        TURN_HAND_TO(kRight, 0.6);
        FRAMES(5);
        EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), callsWhenOff) << "steering went on after " << setting << " off";

        ASSERT_EQ(RunLuauChunk(L, (std::string("runCommand('") + setting + "', 'on')").c_str(), "=on"), 0);
        FRAMES(3);
        TURN_HAND_TO(kRight, 1.0);  // the grip is still squeezed: that is not a new grab
        FRAMES(5);
        EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), callsWhenOff) << setting << " on took hold of the Steering Wheel again";
        headset.SetHandInputs(kRight, Grip(0.0f));
        FRAMES(30);
    }
}

TEST_F(CompanionModTest, VrWheelGrabIsSavedAcrossSessions) {
    std::string saved;
    {
        FakeTrackedHeadset headset;
        fs25vr::VRBridge bridge(headset.manager, nullptr);
        ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab(), kXmlStubs));
        EXPECT_EQ(EvalLua(L, "VRCompanion.wheelGrabEnabled"), "true");
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrWheelGrab', 'off')", "=off"), 0);
        saved = EvalLua(L, "savedXml['vrSettings.wheelGrab']");
    }
    ASSERT_EQ(saved, "false") << "the setting is written to the settings file";

    // A fresh game session reads it back.
    lua_close(L);
    L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcclosure(L, &MockPrint, "print", 0);
    lua_setglobal(L, "print");
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "savedXml['vrSettings.wheelGrab'] = false; VRCompanion:loadMap('x')", "=loadMapAgain"), 0);
    EXPECT_EQ(EvalLua(L, "VRCompanion.wheelGrabEnabled"), "false");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrWheelGrab', 'on')", "=on"), 0);
    EXPECT_EQ(EvalLua(L, "savedXml['vrSettings.wheelGrab']"), "true");
}

// ---- Diagnostics ----

TEST_F(CompanionModTest, VrWheelGrabDiagnosticsLogsTheSteeringWheelEachHandsDistanceToTheRingTheGrabAndTheSteeringSent) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("rotationDeg = 120")));
    SetGameTime(10000);

    // The right hand holds the rim; the left hand is brought up a hand's width beyond the ring.
    BRING_HAND(kLeft, RimPoint(L, 0.5, 2.0));
    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    for (int step = 1; step <= 15; ++step) {
        TURN_HAND_TO(kRight, 0.2 * step);  // all the way to full lock
    }
    FRAMES(5);
    s_capturedLogs.clear();
    const std::string reply = EvalLua(L, "runCommand('vrWheelGrabDiagnostics')");

    for (const char* expected : { "Steering Wheel: found", "Left hand: 0.25 m from the ring", "Right hand: inside the ring",
                                  "inside the ring, grip 1.00, holding", "steering sent: -1.00", "at full lock: yes" }) {
        EXPECT_NE(reply.find(expected), std::string::npos) << "missing from the reply: " << expected << "\n" << reply;
    }
    EXPECT_GT(CountLogLines("[FS25_VR] Wheel Grab"), 0) << "the game log got tagged lines";
    EXPECT_NE(EvalLua(L, "VRCompanion:getPointerDiagnostics()").find("Wheel Grab"), std::string::npos)
        << "the Pointer diagnostics carry it too";
}
