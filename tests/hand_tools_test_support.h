// What the Hand Tools tests (tests/test_hand_tools_*.cpp, ADR-0029) share: how the mod is started on a player
// on foot who can hold Hand Tools (StubHandTool, companion_mod_stubs.h), the frame the game runs before the Companion Mod's
// update, the fake hands, the engine's console stubs and the macros that move the Motion Controllers. Header-only
// (inline), in its own namespace; a test file brings it in with `using namespace hand_tools_test;` inside its anonymous
// namespace. It brings in companion_mod_fixture.h and companion_mod_stubs.h.
//
// The player on foot stands at the world origin, the head at the tracking space origin (the fake headset's head pose is
// the origin), so a hand held at tracking (x, y, z) is in the world about (x, 1.7 + y, z) turned by the head's frame:
// tests never compute that, they compare the tool with the Virtual Hand it is meant to ride in (handHolder) or with where
// the game's own placement put it (the tool placed the game's way is the reference).
//
// What differs between the files stays in the files: which tools a test makes and holds, every assertion helper that
// reads something only that file looks at, and what else happens in a game frame (HAND_TOOLS_PRE_FRAME_EXTRA_LUA, defined
// before including the header, runs after the camera update and the player's update).
//
// Later tickets: add helpers for a tool you introduce (a flashlight, the chainsaw) here, below the "per tool" marker, and
// put your tests in your own tests/test_hand_tools_<topic>.cpp, so the files do not conflict.

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
    pose.rotation = fs25vr::EulerAngles(pitch, yaw, 0.0f);
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

// Which game the mod is started in and what is on the map.
struct StartOptions {
    const char* variant = "ok";              // StubHandTool.install variant: how HandTool.attachToolToCamera is in this game
    const char* lookRayVariant = "ok";       // StubHandTool.install options.getLookRay
    const char* scene = "_G.foot = StubHandTool.onFoot({})";  // the player on foot; tests that need the lance etc. make them
    const char* extraStubs = "";             // one more Lua chunk, after the stubs and before the scene
    const char* gameStubs = "";              // one more piece of the game (FlashlightGame()), before the Companion Mod's files load
};

// The mod loaded and a map started with a player on foot. The game's HandTool and Player are there before the mod's
// files load, as they are in the game, and the scene before loadMap.
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

// What the game does each frame before the Companion Mod's update: the camera update that places the head in the world,
// and the player's own update (the targeter reads the look ray). `extraLua` is whatever else the test's game does.
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

// How many of the game's log lines contain `text`.
inline int CountLogLines(const std::string& text) {
    int count = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find(text) != std::string::npos) ++count;
    }
    return count;
}

// ---- per tool -------------------------------------------------------------------------------------------------------
// The tool in the player's hands, as `lance` (a global of the Lua state), `otherTool`.

// The player takes the pressure washer lance in hand: _G.lance. (The Lua chunk, for a macro that cannot use an
// ASSERT_NO_FATAL_FAILURE twice; HoldLance for a test body.)
inline constexpr const char* kHoldLance = "_G.lance = StubHandTool.lance(foot.player); StubHandTool.hold(lance)";
inline void HoldLance(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldLance, "=holdLance"), 0); }

// The flashlight: the game's flashlight class and the flashlight, as _G.flashlight, with the player taking it
// in hand. Not part of the common start: a test that has a flashlight sets StartOptions::gameStubs to FlashlightGame(variant),
// which installs the game's HandToolFlashlight before the Companion Mod's files load, as the game has it. The flashlight is
// made and held by HoldFlashlight / kHoldFlashlight.
inline std::string FlashlightGame(const char* variant = "ok") {
    return StubChunk("hand_tool_flashlight") + "\nStubHandTool.installFlashlight('" + variant + "')";
}
inline constexpr const char* kHoldFlashlight = "_G.flashlight = StubHandTool.flashlight(foot.player); StubHandTool.hold(flashlight)";
inline void HoldFlashlight(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldFlashlight, "=holdFlashlight"), 0); }

// The player takes another mod's tool (a Hand Tool the Companion Mod does not support) in hand: _G.otherTool.
inline constexpr const char* kHoldOtherModTool = "_G.otherTool = StubHandTool.otherModTool(foot.player); StubHandTool.hold(otherTool)";
inline void HoldOtherModTool(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldOtherModTool, "=holdOtherModTool"), 0); }

// ---- chainsaw -----------------------------------------------------------------------------------------------
// The chainsaw's game-side stub (hand_tool_chainsaw.luau) is not part of every test's game: a test that wants it passes
// ChainsawStubs(variant) as StartOptions::extraStubs. variant is how HandToolChainsaw is: "ok", "missing", "reshaped" (onUpdate), "drawMissing", "drawReshaped" (onDraw), "minimumMissing" (MINIMUM_CUT_DISTANCE), ...
inline std::string ChainsawStubs(const char* variant = "ok") {
    return StubChunk("hand_tool_chainsaw") + "\nStubHandTool.installChainsaw('" + variant + "')\n";
}

// The player takes the chainsaw in hand: _G.saw.
inline constexpr const char* kHoldChainsaw = "_G.saw = StubHandTool.chainsaw(foot.player); StubHandTool.hold(saw)";
inline void HoldChainsaw(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldChainsaw, "=holdChainsaw"), 0); }

// ---- spray can (ADR-0030) -----------------------------------------------------------------------------------------
// The spray can's game-side stub (hand_tool_spray_can.luau) is not part of every test's game: a test that wants it passes
// SprayCanStubs() as StartOptions::extraStubs (after xml_settings.luau, if it uses both: concatenate).
// variant is how HandToolSprayCan.onDraw is: "ok", "missing", "reshaped".
inline std::string SprayCanStubs(const char* variant = "ok") {
    return StubChunk("hand_tool_spray_can") + "\nStubHandTool.installSprayCan('" + variant + "')\n";
}

// The player takes the spray can in hand: _G.can.
inline constexpr const char* kHoldSprayCan = "_G.can = StubHandTool.sprayCan(foot.player); StubHandTool.hold(can)";
inline void HoldSprayCan(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldSprayCan, "=holdSprayCan"), 0); }

// ---- horse brush (ADR-0030) ---------------------------------------------------------------------------------------
// The horse brush's game-side stub (hand_tool_horse_brush.luau) is not part of every test's game: a test that wants it passes
// HorseBrushStubs() as StartOptions::extraStubs (concatenated after the spray can's, if both). handler is how
// HandToolHorseBrush.onBrushAction is ("ok", "missing", "reshaped"), lookup how
// HandToolHorseBrush.getHusbandryAndClusterFromNode is (the same three).
inline std::string HorseBrushStubs(const char* handler = "ok", const char* lookup = "ok") {
    return StubChunk("hand_tool_horse_brush") + "\nStubHandTool.installHorseBrush('" + handler + "', '" + lookup + "')\n";
}

// The player takes the horse brush in hand: _G.brush.
inline constexpr const char* kHoldHorseBrush = "_G.brush = StubHandTool.horseBrush(foot.player); StubHandTool.hold(brush)";
inline void HoldHorseBrush(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldHorseBrush, "=holdHorseBrush"), 0); }

// ---- fishing rod (ADR-0031) ---------------------------------------------------------------------------------------
// The Highlands Fishing DLC's rod (hand_tool_fishing_rod.luau, shaped after the #125 probe dump) is not part of every test's game: a
// test that wants the DLC passes FishingRodStubs() as StartOptions::extraStubs (concatenated after the other tools' chunks, if
// any). A game without the DLC simply leaves it out. variant is how the class's ACTIVATE_HANDTOOL handler (onActivateAction) is:
// "ok", "missing", "reshaped".
inline std::string FishingRodStubs(const char* variant = "ok") {
    return StubChunk("hand_tool_fishing_rod") + "\nStubHandTool.installFishingRod('" + variant + "')\n";
}

// The player takes the fishing rod in hand: _G.rod.
inline constexpr const char* kHoldFishingRod = "_G.rod = StubHandTool.fishingRod(foot.player); StubHandTool.hold(rod)";
inline void HoldFishingRod(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldFishingRod, "=holdFishingRod"), 0); }

}  // namespace hand_tools_test

// A test file whose game frame has more in it defines this to a Lua chunk before including the header.
#ifndef HAND_TOOLS_PRE_FRAME_EXTRA_LUA
#define HAND_TOOLS_PRE_FRAME_EXTRA_LUA ""
#endif

// The macros read `L` and `headset` of the test body, as the tests do.
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

// A held Hand Tool in a hand that is moved to (x, y, z) and left there for a frame (aim and grip pose the same).
#define MOVE_HAND(hand, x, y, z) \
    do { \
        headset.SetHandPose((hand), hand_tools_test::HandAt((x), (y), (z))); \
        headset.SetHandGripPose((hand), hand_tools_test::HandAt((x), (y), (z))); \
        FRAMES(1); \
    } while (0)
