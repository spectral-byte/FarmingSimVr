#pragma once

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include <cmath>
#include <string>

namespace hand_tools_test {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

inline HeadPose HandAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

inline HeadPose HandAt(float x, float y, float z, float pitch, float yaw) {
    HeadPose pose = HandAt(x, y, z);
    pose.orientation = fs25vr::QuaternionFromEuler(pitch, yaw, 0.0f);
    return pose;
}

inline FakeOpenXrControllers::Inputs Trigger(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = value;
    return inputs;
}

inline FakeOpenXrControllers::Inputs Squeeze(float trigger, float grip) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = trigger;
    inputs.grip = grip;
    return inputs;
}

struct StartOptions {
    const char* variant = "ok";              // StubHandTool.install variant: how HandTool.attachToolToCamera is in this game
    const char* lookRayVariant = "ok";       // StubHandTool.install options.getLookRay
    const char* scene = "_G.foot = StubHandTool.onFoot({})";  // the player on foot; tests that need the lance etc. make them
    const char* extraStubs = "";             // one more Lua chunk, after the stubs and before the scene
    const char* gameStubs = "";              // one more piece of the game (FlashlightGame()), before the Companion Mod's files load
};

inline void StartHandToolsMod(lua_State* L, fs25vr::VRBridge& bridge, const StartOptions& options = StartOptions()) {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(RunLuauChunk(L, StubChunk("scene_graph"), "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("model_loading"), "=modelLoading"), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("hand_tool"), "=handTool"), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("console"), "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("hand_tools_scene_helpers"), "=sceneHelpers"), 0);
    const std::string install = std::string("StubHandTool.install('") + options.variant + "', { getLookRay = '" + options.lookRayVariant + "' })";
    ASSERT_EQ(RunLuauChunk(L, install.c_str(), "=installGame"), 0);
    ASSERT_EQ(RunLuauChunk(L, options.gameStubs, "=gameStubs"), 0);
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, options.extraStubs, "=extraStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, options.scene, "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
}

inline void PreFrame(lua_State* L, const char* extraLua = "") {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        if StubScene.updateCamera then StubScene.updateCamera() end
        StubHandTool.tick(16.667)
    )luau", "=preFrame"), 0);
    if (extraLua[0] != '\0') ASSERT_EQ(RunLuauChunk(L, extraLua, "=preFrameExtra"), 0);
}

inline double Number(lua_State* L, const std::string& expression) {
    return std::stod(EvalLua(L, expression));
}

inline int CountLogLines(const std::string& text) {
    int count = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find(text) != std::string::npos) ++count;
    }
    return count;
}

// The player takes the pressure washer lance in hand: _G.lance. (The Lua chunk, for a macro that cannot use an
// ASSERT_NO_FATAL_FAILURE twice; HoldLance for a test body.)
inline constexpr const char* kHoldLance = "_G.lance = StubHandTool.lance(foot.player); StubHandTool.hold(lance)";
inline void HoldLance(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldLance, "=holdLance"), 0); }

inline std::string FlashlightGame(const char* variant = "ok") {
    return StubInstalled("hand_tool_flashlight", std::string("StubHandTool.installFlashlight('") + variant + "')");
}
inline constexpr const char* kHoldFlashlight = "_G.flashlight = StubHandTool.flashlight(foot.player); StubHandTool.hold(flashlight)";
inline void HoldFlashlight(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldFlashlight, "=holdFlashlight"), 0); }

inline constexpr const char* kHoldOtherModTool = "_G.otherTool = StubHandTool.otherModTool(foot.player); StubHandTool.hold(otherTool)";
inline void HoldOtherModTool(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldOtherModTool, "=holdOtherModTool"), 0); }

inline std::string ChainsawStubs(const char* variant = "ok") {
    return StubInstalled("hand_tool_chainsaw", std::string("StubHandTool.installChainsaw('") + variant + "')");
}

inline constexpr const char* kHoldChainsaw = "_G.saw = StubHandTool.chainsaw(foot.player); StubHandTool.hold(saw)";
inline void HoldChainsaw(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldChainsaw, "=holdChainsaw"), 0); }

inline std::string SprayCanStubs(const char* variant = "ok") {
    return StubInstalled("hand_tool_spray_can", std::string("StubHandTool.installSprayCan('") + variant + "')");
}

inline constexpr const char* kHoldSprayCan = "_G.can = StubHandTool.sprayCan(foot.player); StubHandTool.hold(can)";
inline void HoldSprayCan(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldSprayCan, "=holdSprayCan"), 0); }

inline std::string HorseBrushStubs(const char* handler = "ok", const char* lookup = "ok") {
    return StubInstalled("hand_tool_horse_brush", std::string("StubHandTool.installHorseBrush('") + handler + "', '" + lookup + "')");
}

inline constexpr const char* kHoldHorseBrush = "_G.brush = StubHandTool.horseBrush(foot.player); StubHandTool.hold(brush)";
inline void HoldHorseBrush(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldHorseBrush, "=holdHorseBrush"), 0); }

inline std::string FishingRodStubs(const char* variant = "ok") {
    return StubInstalled("hand_tool_fishing_rod", std::string("StubHandTool.installFishingRod('") + variant + "')");
}

inline constexpr const char* kHoldFishingRod = "_G.rod = StubHandTool.fishingRod(foot.player); StubHandTool.hold(rod)";
inline void HoldFishingRod(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldFishingRod, "=holdFishingRod"), 0); }

}

// A test file whose game frame has more in it defines this to a Lua chunk before including the header.
#ifndef HAND_TOOLS_PRE_FRAME_EXTRA_LUA
#define HAND_TOOLS_PRE_FRAME_EXTRA_LUA ""
#endif

#define FRAMES(count) \
    for (int frame_ = 0; frame_ < (count); ++frame_) { hand_tools_test::PreFrame(L, HAND_TOOLS_PRE_FRAME_EXTRA_LUA); NextFrame(&headset); }

// A Motion Controller picked up: it moves a little, then holds the pose given (aim and grip pose the same, tracked). The
// game clock is set by the test once (the in-use timeout is measured on it).
#define PICK_UP(hand, x, y, z) \
    do { \
        headset.SetHandPose((hand), hand_tools_test::HandAt((x) - 0.1f, (y), (z))); \
        headset.SetHandGripPose((hand), hand_tools_test::HandAt((x) - 0.1f, (y), (z))); \
        FRAMES(1); \
        headset.SetHandPose((hand), hand_tools_test::HandAt((x), (y), (z))); \
        headset.SetHandGripPose((hand), hand_tools_test::HandAt((x), (y), (z))); \
        FRAMES(1); \
    } while (0)

#define MOVE_HAND(hand, x, y, z) \
    do { \
        headset.SetHandPose((hand), hand_tools_test::HandAt((x), (y), (z))); \
        headset.SetHandGripPose((hand), hand_tools_test::HandAt((x), (y), (z))); \
        FRAMES(1); \
    } while (0)
