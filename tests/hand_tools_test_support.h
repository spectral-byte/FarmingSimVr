// What the Hand Tools tests (tests/test_hand_tools_*.cpp, spec #109, ADR-0029) share: how the mod is started on a player
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

// Stubs of the engine's XML settings API that remember what was saved (in savedXml), so a "session" can be replayed into a
// fresh Lua state (the same as test_motion_controllers.cpp and test_wheel_grab.cpp). Pass them as StartOptions::extraStubs.
inline constexpr const char* kXmlStubs = R"luau(
    _G.savedXml = _G.savedXml or {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function() return true end
    _G.createXMLFile = function() return 1 end
    _G.setXMLFloat = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLBool = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLString = function(file, key, value) _G.savedXml[key] = value end
    _G.saveXMLFile = function() return true end
    _G.loadXMLFile = function() return 2 end
    _G.getXMLFloat = function(file, key) local v = _G.savedXml[key]; return type(v) == "number" and v or nil end
    _G.getXMLBool = function(file, key) local v = _G.savedXml[key]; if type(v) == "boolean" then return v end return nil end
    _G.getXMLString = function(file, key) local v = _G.savedXml[key]; return type(v) == "string" and v or nil end
)luau";

// Lua helpers for reading the scene as the player would see it, run once after the scene exists.
//   isBelow(node, ancestor)           the node hangs somewhere below the ancestor
//   handHolder(which)                 the Virtual Hand's node ("left" or "right"), nil without one
//   distance(a, b)                    metres between two nodes' world positions
//   offsetInHolder(node, which)       the node's position in the Virtual Hand's space, x y z
//   shown(which)                      the Virtual Hand is shown
//   fingerCurl(which, finger)         the curl of the three joints of a finger of a Virtual Hand, radians, toward the palm
inline constexpr const char* kSceneHelpers = R"luau(
    function _G.isBelow(node, ancestor)
        local guard = 0
        while node ~= nil and node ~= 0 and guard < 100 do
            if node == ancestor then return true end
            node = getParent(node)
            guard = guard + 1
        end
        return false
    end
    function _G.handHolder(which) return StubModel.hands()[which] end
    function _G.shown(which)
        local node = StubModel.hands()[which]
        return node ~= nil and StubModel.isShown(node)
    end
    function _G.distance(a, b)
        local ax, ay, az = getWorldTranslation(a)
        local bx, by, bz = getWorldTranslation(b)
        return math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2 + (az - bz) ^ 2)
    end
    function _G.offsetInHolder(node, which)
        local x, y, z = getWorldTranslation(node)
        return worldToLocal(StubModel.hands()[which], x, y, z)
    end
    function _G.fingerCurl(which, finger)
        local hand = StubModel.hands()[which]
        return { StubModel.curl(hand, finger .. '1'), StubModel.curl(hand, finger .. '2'), StubModel.curl(hand, finger .. '3') }
    end
)luau";

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
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubModelLoading, "=modelLoading"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubHandTool, "=handTool"), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, kSceneHelpers, "=sceneHelpers"), 0);
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
// The tool in the player's hands, as `lance` (a global of the Lua state), `sprayCan`.

// The player takes the pressure washer lance in hand: _G.lance. (The Lua chunk, for a macro that cannot use an
// ASSERT_NO_FATAL_FAILURE twice; HoldLance for a test body.)
inline constexpr const char* kHoldLance = "_G.lance = StubHandTool.lance(foot.player); StubHandTool.hold(lance)";
inline void HoldLance(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldLance, "=holdLance"), 0); }

// The flashlight (ticket #113): the game's flashlight class and the flashlight, as _G.flashlight, with the player taking it
// in hand. Not part of the common start: a test that has a flashlight sets StartOptions::gameStubs to FlashlightGame(variant),
// which installs the game's HandToolFlashlight before the Companion Mod's files load, as the game has it. The flashlight is
// made and held by HoldFlashlight / kHoldFlashlight.
inline std::string FlashlightGame(const char* variant = "ok") {
    return std::string(kStubHandToolFlashlight) + "\nStubHandTool.installFlashlight('" + variant + "')";
}
inline constexpr const char* kHoldFlashlight = "_G.flashlight = StubHandTool.flashlight(foot.player); StubHandTool.hold(flashlight)";
inline void HoldFlashlight(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldFlashlight, "=holdFlashlight"), 0); }

// The player takes the spray can (an unsupported Hand Tool) in hand: _G.sprayCan.
inline constexpr const char* kHoldSprayCan = "_G.sprayCan = StubHandTool.sprayCan(foot.player); StubHandTool.hold(sprayCan)";
inline void HoldSprayCan(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldSprayCan, "=holdSprayCan"), 0); }

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
