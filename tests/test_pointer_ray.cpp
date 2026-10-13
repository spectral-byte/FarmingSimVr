// The Pointer Ray and Interactive Control Click Points (spec #71, ticket #73), seen from outside: the
// real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with a stand-in for the
// Interactive Control mod (companion_mod_stubs.h). The test moves the fake Motion Controllers and the
// game clock, drives frames the way the game would (camera update, Interactive Control's own update,
// the Companion Mod's update) and asserts which Click Point Interactive Control executed, what the
// ray drew, and what the log says.

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

HeadPose HandAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

HeadPose HandAt(float x, float y, float z, float pitch, float yaw) {
    HeadPose pose = HandAt(x, y, z);
    pose.rotation = fs25vr::EulerAngles(pitch, yaw, 0.0f);
    return pose;
}

FakeOpenXrControllers::Inputs Trigger(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = value;
    return inputs;
}

// The engine globals the Companion Mod looks at when it draws.
constexpr const char* kDrawStubs = R"luau(
    _G.debugLines, _G.debugPoints = {}, {}
    _G.drawDebugLine = function(x0, y0, z0, r0, g0, b0, x1, y1, z1, r1, g1, b1, solid)
        table.insert(_G.debugLines, { x0 = x0, y0 = y0, z0 = z0, x1 = x1, y1 = y1, z1 = z1 })
    end
    _G.drawDebugPoint = function(x, y, z, r, g, b, a, solid)
        table.insert(_G.debugPoints, { x = x, y = y, z = z })
    end
    _G.drawPass = function()
        _G.debugLines, _G.debugPoints = {}, {}
        VRCompanion:draw()
    end
)luau";

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
)luau";

// A cab at the world origin, an Interactive Control release the Companion Mod supports, and the mod
// started: the player sits at (0, 1.5, 0) looking along world +Z, so a hand held at (x, y, -z)
// from the head points along +Z from world (-x, 1.5 + y, +z).
void StartMod(lua_State* L, fs25vr::VRBridge& bridge, const char* icVariant = "ok") {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubInteractiveControl, "=interactiveControl"), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, kDrawStubs, "=drawStubs"), 0);
    if (icVariant != nullptr) {
        ASSERT_EQ(RunLuauChunk(L, std::string("StubIC.install('") + icVariant + "')", "=installIc"), 0);
    }
    ASSERT_EQ(RunLuauChunk(L, "StubScene.cab()", "=cab"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
}

// What the game does each frame before the Companion Mod's update: the camera update that places the
// head in the world, and Interactive Control's own update of its Click Points.
void PreFrame(lua_State* L) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        if StubScene.updateCamera then StubScene.updateCamera() end
        if StubIC.tick then StubIC.tick() end
    )luau", "=preFrame"), 0);
}

} // namespace

#define FRAMES(count) \
    for (int frame_ = 0; frame_ < (count); ++frame_) { PreFrame(L); NextFrame(&headset); }

// The Pointer Hand: picks the controller up (it moves) and aims it along the given pose.
#define PICK_UP_AND_AIM(hand, x, y, z) \
    do { \
        SetGameTime(10000); \
        headset.SetHandPose((hand), HandAt((x) - 0.1f, (y), (z))); \
        FRAMES(1); \
        headset.SetHandPose((hand), HandAt((x), (y), (z))); \
        FRAMES(1); \
    } while (0)

TEST_F(CompanionModTest, PullingTheTriggerOnAClickPointInTheRayExecutesItOnce) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    // The hand is held 0.3 m right of and 0.3 m ahead of the head, pointing straight ahead: in the world
    // it starts at (-0.3, 1.5, 0.3) and points along +Z.
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('lights', -0.3, 1.5, 2.3)", "=addPoint"), 0);

    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "lights") << "Interactive Control treats the aimed Click Point as clickable";
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0") << "aiming alone never clicks";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "1");
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lights");
}
