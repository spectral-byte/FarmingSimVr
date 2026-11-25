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

inline HeadPose HandAtWorld(const Point& p) {
    return HandAt(static_cast<float>(-p.x), static_cast<float>(p.y - 1.5), static_cast<float>(-p.z));
}

inline FakeOpenXrControllers::Inputs Grip(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.grip = value;
    return inputs;
}

inline constexpr const char* kNoPullAway = "VRCompanion.WHEEL_GRAB_PULL_AWAY_DISTANCE = 1000";

inline std::string SteerableCab(const std::string& options = "") {
    return "StubSteer.install()\n"
           "local cab = StubScene.cab({})\n"
           "StubSteer.equip(cab.vehicle, { parent = cab.parent, " + options + " })\n"
           "_G.cab = cab\n";
}

struct StartOptions {
    bool consoleStubs = true;   // console.luau
    bool modelLoading = false;  // model_loading.luau: the Virtual Hand's model (StubModel)
    bool noPullAway = false;    // kNoPullAway
    const char* extraStubs = "";  // one more Lua chunk, run after the stubs above and before the scene
};

inline void StartWheelGrabMod(lua_State* L, fs25vr::VRBridge& bridge, const std::string& scene = SteerableCab(),
                              const StartOptions& options = StartOptions()) {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("scene_graph"), "=sceneGraph"), 0);
    if (options.modelLoading) ASSERT_EQ(RunLuauChunk(L, StubChunk("model_loading"), "=modelLoading"), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("steering_vehicle"), "=steeringVehicle"), 0);
    if (options.consoleStubs) ASSERT_EQ(RunLuauChunk(L, StubChunk("console"), "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, options.extraStubs, "=extraStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, scene.c_str(), "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
    if (options.noPullAway) ASSERT_EQ(RunLuauChunk(L, kNoPullAway, "=noPullAway"), 0);
}

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

inline int CountLogLines(const std::string& text) {
    int count = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find(text) != std::string::npos) ++count;
    }
    return count;
}

}

#ifndef WHEEL_GRAB_PRE_FRAME_EXTRA_LUA
#define WHEEL_GRAB_PRE_FRAME_EXTRA_LUA ""
#endif

#define FRAMES(count) \
    for (int frame_ = 0; frame_ < (count); ++frame_) { wheel_grab_test::PreFrame(L, WHEEL_GRAB_PRE_FRAME_EXTRA_LUA); NextFrame(&headset); }

#define BRING_HAND(hand, rim) \
    do { \
        headset.SetHandPose((hand), wheel_grab_test::HandAtWorld(wheel_grab_test::Point{ 0.6, 1.0, 0.2 })); \
        FRAMES(1); \
        headset.SetHandPose((hand), wheel_grab_test::HandAtWorld(rim)); \
        FRAMES(1); \
    } while (0)

#define GRAB(hand, rim) \
    do { \
        BRING_HAND((hand), (rim)); \
        headset.SetHandInputs((hand), wheel_grab_test::Grip(1.0f)); \
        FRAMES(1); \
    } while (0)

#define TURN_HAND_TO(hand, angle) \
    do { \
        headset.SetHandPose((hand), wheel_grab_test::HandAtWorld(wheel_grab_test::RimPoint(L, wheel_grab_test::kRimRadius, (angle)))); \
        FRAMES(1); \
    } while (0)

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
