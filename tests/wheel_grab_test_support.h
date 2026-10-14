// What the Wheel Grab tests (tests/test_wheel_grab*.cpp, spec #89, ADR-0027) share: the cab geometry, the fake hands,
// the engine's console and menu stubs, the steerable cab, how the mod is started, the frame the game runs before the
// Companion Mod's update, and the macros that move the Motion Controllers. Header-only (inline), in its own namespace;
// a test file brings it in with `using namespace wheel_grab_test;` inside its anonymous namespace.
//
// The cab is at the world origin facing +Z with the head at the tracking space origin, so a point at world (x, y, z) is
// where a hand at tracking (-x, y - 1.5, -z) is. The Steering Wheel hangs 0.5 m in front of the head and 0.3 m below it,
// leaning toward the driver (companion_mod_stubs.h, StubSteer); a hand "at the rim" is 0.18 m from its axis.
//
// What differs between the files stays in the files: which stubs a mod is started with (StartOptions), the cab's
// options, what else happens in a game frame (WHEEL_GRAB_PRE_FRAME_EXTRA_LUA) and every assertion helper that reads
// something only that file looks at.

#pragma once

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include <string>

namespace wheel_grab_test {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

constexpr double kPi = 3.14159265358979;
constexpr double kRimRadius = 0.18;  // m from the Steering Wheel's axis: inside the ring, on the rim

struct Point {
    double x, y, z;
};

inline HeadPose HandAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

// A hand at a world position of the cab at the origin (see the top of the file).
inline HeadPose HandAtWorld(const Point& p) {
    return HandAt(static_cast<float>(-p.x), static_cast<float>(p.y - 1.5), static_cast<float>(-p.z));
}

inline FakeOpenXrControllers::Inputs Grip(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.grip = value;
    return inputs;
}

// The engine's console and menu globals the Companion Mod registers into and polls: the commands can be run with
// runCommand(name, ...), and g_gui.visible opens and closes a menu.
inline constexpr const char* kConsoleStubs = R"luau(
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

// Wheel Grab lets go of a hand that is further than WHEEL_GRAB_PULL_AWAY_DISTANCE from the point its Virtual Hand is
// pinned to. Most of these tests turn the hand faster and further than an arm could while the Steering Wheel follows
// (the stops, hand over hand), which the pull-away rule would take for a hand pulled off the wheel, so they set the
// distance so large it never happens. The pull-away rule itself is tested in test_wheel_grab_pinned_hand.cpp, which
// leaves the distance alone.
inline constexpr const char* kNoPullAway = "VRCompanion.WHEEL_GRAB_PULL_AWAY_DISTANCE = 1000";

// A cab with a steerable vehicle: the Steering Wheel hangs in the cab's node, the way the game builds one.
// `options` are more StubSteer.equip options, e.g. "rotationDeg = 120".
inline std::string SteerableCab(const std::string& options = "") {
    return "StubSteer.install()\n"
           "local cab = StubScene.cab({})\n"
           "StubSteer.equip(cab.vehicle, { parent = cab.parent, " + options + " })\n"
           "_G.cab = cab\n";
}

// Which stubs the mod is started with, besides the scene graph and the steering vehicle.
struct StartOptions {
    bool consoleStubs = true;   // kConsoleStubs
    bool modelLoading = false;  // kStubModelLoading: the Virtual Hand's model (StubModel)
    bool noPullAway = false;    // kNoPullAway
    const char* extraStubs = "";  // one more Lua chunk, run after the stubs above and before the scene
};

// The mod loaded and a map started on `scene` (a steerable cab by default). The stubs and the scene are there before
// loadMap, as they are in the game.
inline void StartWheelGrabMod(lua_State* L, fs25vr::VRBridge& bridge, const std::string& scene = SteerableCab(),
                              const StartOptions& options = StartOptions()) {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    if (options.modelLoading) ASSERT_EQ(RunLuauChunk(L, kStubModelLoading, "=modelLoading"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSteeringVehicle, "=steeringVehicle"), 0);
    if (options.consoleStubs) ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, options.extraStubs, "=extraStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, scene.c_str(), "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
    if (options.noPullAway) ASSERT_EQ(RunLuauChunk(L, kNoPullAway, "=noPullAway"), 0);
}

// What the game does each frame before the Companion Mod's update: the camera update that places the head in the world,
// and the vehicle's own update that acts on the steering input of the frame before. `extraLua` is whatever else the
// test's game does in a frame (e.g. Interactive Control's update of its Click Points), after those.
inline void PreFrame(lua_State* L, const char* extraLua = "") {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        if StubScene.updateCamera then StubScene.updateCamera() end
        StubSteer.tick(16.667)
    )luau", "=preFrame"), 0);
    if (extraLua[0] != '\0') ASSERT_EQ(RunLuauChunk(L, extraLua, "=preFrameExtra"), 0);
}

inline double Number(lua_State* L, const std::string& expression) {
    return std::stod(EvalLua(L, expression));
}

// The world position of the point on the Steering Wheel `radius` from its axis and `lift` along it when the Steering Wheel
// is turned to `angle`.
inline Point RimPoint(lua_State* L, double radius, double angle, double lift = 0.0) {
    const std::string args = "(cab.vehicle, " + std::to_string(radius) + ", " + std::to_string(angle) + ", " + std::to_string(lift) + ")";
    return { Number(L, "select(1, StubSteer.rimPoint" + args + ")"), Number(L, "select(2, StubSteer.rimPoint" + args + ")"),
             Number(L, "select(3, StubSteer.rimPoint" + args + ")") };
}

// How far the Steering Wheel is turned (radians, unwrapped).
inline double WheelAngle(lua_State* L) { return Number(L, "StubSteer.wheelAngle(cab.vehicle)"); }

// How many of the game's log lines contain `text`.
inline int CountLogLines(const std::string& text) {
    int count = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find(text) != std::string::npos) ++count;
    }
    return count;
}

}  // namespace wheel_grab_test

// A test file whose game frame has more in it defines this to a Lua chunk before including the header.
#ifndef WHEEL_GRAB_PRE_FRAME_EXTRA_LUA
#define WHEEL_GRAB_PRE_FRAME_EXTRA_LUA ""
#endif

// The macros read `L` and `headset` of the test body, as the tests do.
#define FRAMES(count) \
    for (int frame_ = 0; frame_ < (count); ++frame_) { wheel_grab_test::PreFrame(L, WHEEL_GRAB_PRE_FRAME_EXTRA_LUA); NextFrame(&headset); }

// A Motion Controller put into use at a spot, then moved to `rim` (a hand counts as in use once it has moved).
#define BRING_HAND(hand, rim) \
    do { \
        headset.SetHandPose((hand), wheel_grab_test::HandAtWorld(wheel_grab_test::Point{ 0.6, 1.0, 0.2 })); \
        FRAMES(1); \
        headset.SetHandPose((hand), wheel_grab_test::HandAtWorld(rim)); \
        FRAMES(1); \
    } while (0)

// The hand brought to `rim` and the grip squeezed there.
#define GRAB(hand, rim) \
    do { \
        BRING_HAND((hand), (rim)); \
        headset.SetHandInputs((hand), wheel_grab_test::Grip(1.0f)); \
        FRAMES(1); \
    } while (0)

// The hand turned about the Steering Wheel's axis to where the rim point of a Steering Wheel turned to `angle` is.
#define TURN_HAND_TO(hand, angle) \
    do { \
        headset.SetHandPose((hand), wheel_grab_test::HandAtWorld(wheel_grab_test::RimPoint(L, wheel_grab_test::kRimRadius, (angle)))); \
        FRAMES(1); \
    } while (0)

// The hand brought to the rim, the grip squeezed and the hand turned a little, then the grip let go again: what a
// player does to steer, whatever comes of it.
#define TRY_TO_STEER(hand) \
    do { \
        GRAB((hand), wheel_grab_test::RimPoint(L, wheel_grab_test::kRimRadius, 0.0)); \
        for (int step_ = 1; step_ <= 4; ++step_) { \
            TURN_HAND_TO((hand), 0.2 * step_); \
        } \
        FRAMES(5); \
        headset.SetHandInputs((hand), wheel_grab_test::Grip(0.0f)); \
        FRAMES(2); \
    } while (0)
