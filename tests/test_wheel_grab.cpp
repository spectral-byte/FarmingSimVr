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

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include <string>

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

constexpr double kRimRadius = 0.18;  // m from the Steering Wheel's axis: inside the ring, on the rim

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

struct Point {
    double x, y, z;
};

HeadPose HandAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

// A hand at a world position of the cab at the origin (see the top of the file).
HeadPose HandAtWorld(const Point& p) {
    return HandAt(static_cast<float>(-p.x), static_cast<float>(p.y - 1.5), static_cast<float>(-p.z));
}

FakeOpenXrControllers::Inputs Grip(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.grip = value;
    return inputs;
}

// The engine's console globals the Companion Mod registers into.
constexpr const char* kConsoleStubs = R"luau(
    _G.registeredCommands = {}
    _G.addConsoleCommand = function(name, desc, method, target)
        _G.registeredCommands[name] = { desc = desc, method = method, target = target }
    end
    _G.removeConsoleCommand = function(name) _G.registeredCommands[name] = nil end
    _G.runCommand = function(name, ...)
        local command = _G.registeredCommands[name]
        assert(command ~= nil, name .. " is not registered")
        return command.target[command.method](command.target, ...)
    end
    _G.Input = { MOUSE_BUTTON_NONE = 0, MOUSE_BUTTON_LEFT = 1 }
    _G.g_gui = {
        visible = false,
        getIsGuiVisible = function(self) return self.visible end,
        mouseEvent = function() end,
    }
)luau";

// A cab with a steerable vehicle: the Steering Wheel hangs in the cab's node, the way the game builds one.
// `options` are more StubSteer.equip options, e.g. "rotationDeg = 120".
std::string SteerableCab(const std::string& options = "") {
    return "StubSteer.install()\n"
           "local cab = StubScene.cab({})\n"
           "StubSteer.equip(cab.vehicle, { parent = cab.parent, " + options + " })\n"
           "_G.cab = cab\n";
}

// The mod started on a map with a given scene (a steerable cab by default).
void StartMod(lua_State* L, fs25vr::VRBridge& bridge, const std::string& scene = SteerableCab(),
              const char* extraStubs = "") {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSteeringVehicle, "=steeringVehicle"), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, extraStubs, "=extraStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, scene.c_str(), "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
}

// What the game does each frame before the Companion Mod's update: the camera update that places the head in
// the world, and the vehicle's own update that acts on the steering input of the frame before.
void PreFrame(lua_State* L) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        if StubScene.updateCamera then StubScene.updateCamera() end
        StubSteer.tick(16.667)
    )luau", "=preFrame"), 0);
}

double Number(lua_State* L, const std::string& expression) {
    return std::stod(EvalLua(L, expression));
}

// The world position of the point on the Steering Wheel `radius` from its axis and `lift` along it when the wheel is
// turned to `angle`.
Point RimPoint(lua_State* L, double radius, double angle, double lift = 0.0) {
    const std::string args = "(cab.vehicle, " + std::to_string(radius) + ", " + std::to_string(angle) + ", " + std::to_string(lift) + ")";
    return { Number(L, "select(1, StubSteer.rimPoint" + args + ")"), Number(L, "select(2, StubSteer.rimPoint" + args + ")"),
             Number(L, "select(3, StubSteer.rimPoint" + args + ")") };
}

double WheelAngle(lua_State* L) { return Number(L, "StubSteer.wheelAngle(cab.vehicle)"); }

int CountLogLines(const std::string& text) {
    int count = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find(text) != std::string::npos) ++count;
    }
    return count;
}

} // namespace

#define FRAMES(count) \
    for (int frame_ = 0; frame_ < (count); ++frame_) { PreFrame(L); NextFrame(&headset); }

// A Motion Controller put into use at a spot, then moved to `rim` (a hand counts as in use once it has moved).
#define BRING_HAND(hand, rim) \
    do { \
        headset.SetHandPose((hand), HandAtWorld(Point{ 0.6, 1.0, 0.2 })); \
        FRAMES(1); \
        headset.SetHandPose((hand), HandAtWorld(rim)); \
        FRAMES(1); \
    } while (0)

// The hand brought to `rim` and the grip squeezed there.
#define GRAB(hand, rim) \
    do { \
        BRING_HAND((hand), (rim)); \
        headset.SetHandInputs((hand), Grip(1.0f)); \
        FRAMES(1); \
    } while (0)

// The hand turned about the wheel's axis to where the rim point of a wheel turned to `angle` is.
#define TURN_HAND_TO(hand, angle) \
    do { \
        headset.SetHandPose((hand), HandAtWorld(RimPoint(L, kRimRadius, (angle)))); \
        FRAMES(1); \
    } while (0)

// The hand brought to the rim, the grip squeezed and the hand turned a little, then the grip let go again: what a
// player does to steer, whatever comes of it.
#define TRY_TO_STEER(hand) \
    do { \
        GRAB((hand), RimPoint(L, kRimRadius, 0.0)); \
        for (int step_ = 1; step_ <= 4; ++step_) { \
            TURN_HAND_TO((hand), 0.2 * step_); \
        } \
        FRAMES(5); \
        headset.SetHandInputs((hand), Grip(0.0f)); \
        FRAMES(2); \
    } while (0)
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
                              Spot{ 0.18, 0.15, "well in front of the wheel's plane" } }) {
        GRAB(kRight, RimPoint(L, spot.radius, 0.5, spot.lift));
        for (int step = 1; step <= 4; ++step) {
            headset.SetHandPose(kRight, HandAtWorld(RimPoint(L, spot.radius, 0.5 + 0.2 * step, spot.lift)));
            FRAMES(2);
        }
        headset.SetHandInputs(kRight, Grip(0.0f));
        FRAMES(2);
        EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0") << "a grip squeezed " << spot.where << " steered";
        EXPECT_NEAR(WheelAngle(L), 0.0, 1e-6) << "a grip squeezed " << spot.where << " turned the wheel";
    }
}

TEST_F(CompanionModTest, TurningStartsFromTheSteeringWheelsActualAngleSoGrabbingNeverJumpsIt) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    // Steered 0.5 of the way to full lock: 1.5 turns of the wheel's 540 degrees at full lock, past the half
    // turn at which a node's Euler angles wrap. A slow steering duration keeps the game from centring the wheel
    // while the hand is brought up, and from catching up with it quickly afterwards.
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("rotatedTime = 0.5, steeringDuration = 100")));
    SetGameTime(10000);
    const double start = 0.5 * 3.0 * 3.14159265358979;

    GRAB(kRight, RimPoint(L, kRimRadius, 1.0));  // the hand takes hold of the rim wherever it is, not where the wheel's angle is
    FRAMES(20);
    EXPECT_NEAR(WheelAngle(L), start, 0.02) << "the wheel stayed where it was when the hand took hold";

    TURN_HAND_TO(kRight, 1.0 + 0.3);
    FRAMES(150);
    EXPECT_NEAR(WheelAngle(L), start + 0.3, 0.02) << "turning the hand turned the wheel on from its own angle";
}

TEST_F(CompanionModTest, TheSteeringWheelStopsDeadAtFullLockAndTurnsBackAtOnce) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    // A tracked tractor's wheel turns 120 degrees at full lock.
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("rotationDeg = 120")));
    SetGameTime(10000);
    const double fullLock = 120.0 * 3.14159265358979 / 180.0;

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    for (int step = 1; step <= 15; ++step) {
        TURN_HAND_TO(kRight, 0.2 * step);  // the hand carries on to 3 radians, a good way past full lock
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), fullLock, 0.01) << "the wheel stopped at full lock";
    EXPECT_NEAR(Number(L, "StubSteer.calls[#StubSteer.calls].value"), -1.0, 1e-6)
        << "at full lock the vehicle was told to steer all the way";

    // Back from the stop by 0.3 radian of hand turn: the wheel follows at once, no slack to wind back first.
    for (int step = 1; step <= 3; ++step) {
        TURN_HAND_TO(kRight, 3.0 - 0.1 * step);
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), fullLock - 0.3, 0.02) << "turning back from the stop moved the wheel straight away";

    for (int step = 1; step <= 40; ++step) {
        TURN_HAND_TO(kRight, 2.7 - 0.2 * step);  // all the way round the other way, to -5.3 radians
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), -fullLock, 0.01) << "and the wheel stops at full lock the other way too";
    EXPECT_NEAR(Number(L, "StubSteer.calls[#StubSteer.calls].value"), 1.0, 1e-6);
}

TEST_F(CompanionModTest, LettingGoOfTheGripStopsSteeringAndTheGameCentresTheWheel) {
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
    EXPECT_NEAR(WheelAngle(L), 0.0, 0.01) << "the game's own steering back centred the wheel";
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
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0") << "a grip at 0.5 did not take the wheel";

    TURN_HAND_TO(kRight, 0.0);
    headset.SetHandInputs(kRight, Grip(0.7f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Grip(0.4f));  // eased off, still more than the release threshold
    FRAMES(2);
    TURN_HAND_TO(kRight, 0.4);
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), 0.4, 0.02) << "a grip eased to 0.4 still held the wheel";

    headset.SetHandInputs(kRight, Grip(0.2f));
    FRAMES(1);
    const std::string callsAtRelease = EvalLua(L, "#StubSteer.calls");
    TURN_HAND_TO(kRight, 1.0);
    FRAMES(5);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), callsAtRelease) << "below 0.3 the wheel was let go";
}

TEST_F(CompanionModTest, AGripAlreadySqueezedWhenTheHandReachesTheRingDoesNotGrab) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    BRING_HAND(kRight, RimPoint(L, 0.5, 0.0));  // a fist away from the wheel
    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(2);
    for (int step = 1; step <= 5; ++step) {
        headset.SetHandPose(kRight, HandAtWorld(RimPoint(L, 0.5 - 0.06 * step, 0.0)));  // into the ring with the fist closed
        FRAMES(2);
    }
    TURN_HAND_TO(kRight, 0.5);
    FRAMES(10);
    EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), "0") << "a fist swept through the ring did not take the wheel";
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
    EXPECT_GT(Number(L, "#StubSteer.calls"), 0.0) << "back in the cab camera the wheel can be held again";
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

TEST_F(CompanionModTest, SwitchingWheelGrabOrMotionControllersOffLetsGoOfTheWheelAndItIsNotHeldAgainBySwitchingOnAgain) {
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
        EXPECT_EQ(EvalLua(L, "#StubSteer.calls"), callsWhenOff) << setting << " on took hold of the wheel again";
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

TEST_F(CompanionModTest, VrWheelGrabDiagnosticsLogsTheWheelEachHandsDistanceToTheRingTheGrabAndTheSteeringSent) {
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
