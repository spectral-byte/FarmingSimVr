// What the Animated Objects tests (tests/test_animated_objects*.cpp, ADR-0034, spec #180) share, on top of object_grab_test_support.h (the
// player on foot with the game's empty hands, the Lua helpers that read the rays, the macros that point a Motion Controller): the game's
// Animated Objects as a stub (StubAnimatedObject, shaped after AnimatedObject, AnimatedObjectActivatable and PlaceableAnimatedObjects in the
// SDK's gameSource.zip), the engine's raycastClosest over the boxes of its parts, and a fixture. Header-only (inline), in its own
// namespace; a test file brings it in with `using namespace animated_object_test;` inside its anonymous namespace.
//
// The stub is a chunk of its own (animated_objects.luau), like the Hand Tools' tool chunks, passed as StartOptions::extraStubs
// (AnimatedObjectStubs(variant)): it runs after the Companion Mod's files are loaded and before the map is, so the mod finds the game's
// classes when it looks. A door is made with StubAnimatedObject.door(options) after the scene exists (the zone is entered by the local
// player's root node, the node the scene's player stands on).
//
// What the stub offers (StubAnimatedObject.install, door, enterZone, ...) is described at the top of tests/stubs/animated_objects.luau.

#pragma once

#include "object_grab_test_support.h"
#include <string>

namespace animated_object_test {

using namespace object_grab_test;

// The game's Animated Objects (animated_objects.luau), for StartOptions::extraStubs. variant: see the top of that file.
inline std::string AnimatedObjectStubs(const char* variant = "ok") {
    return StubChunk("animated_objects") + "\nStubAnimatedObject.install('" + variant + "')\n";
}

// A hand pointing at tracking (0.4, -0.2, -0.3) has its fingertip at world (-0.4, -0.2, 0.3) and the Pointer Ray runs along +Z (the same
// geometry as the Object Grab tests). The door's leaf is straight ahead of it, 1.65 m from the fingertip.
constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kDoorAhead = "_G.door = StubAnimatedObject.door({ name = 'door', x = -0.4, y = -0.2, z = 2.0 })";
// A door to the left side of the player's ray: a hand pointing at tracking (-0.4, -0.2, -0.3) has its fingertip at world (0.4, -0.2, 0.3).
constexpr const char* kDoorOnTheLeft = "_G.leftDoor = StubAnimatedObject.door({ name = 'leftDoor', x = 0.4, y = -0.2, z = 2.0 })";

// A test of the Pointer Ray working Animated Objects, seen from what the game reads from the Virtual Gamepad: the mod started on a
// player on foot with the game's empty hands, the Virtual Gamepad on, the game clock set, and the game's Animated Objects in the game.
// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) covers.
class VirtualGamepadAnimatedObjectsTest : public VirtualGamepadObjectGrabTest {
protected:
    // The game with its Animated Objects ("ok" or a variant), and the door of kDoorAhead unless `withDoor` is false.
    void StartWithDoor(const char* variant = "ok", bool withDoor = true) {
        stubs_ = AnimatedObjectStubs(variant);
        StartOptions options;
        options.extraStubs = stubs_.c_str();
        ASSERT_NO_FATAL_FAILURE(Start(options));
        ASSERT_EQ(RunLuauChunk(L, StubChunk("animated_object_helpers"), "=animatedObjectHelpers"), 0);
        if (withDoor) ASSERT_EQ(RunLuauChunk(L, kDoorAhead, "=door"), 0);
    }

    void EnterZone(const char* object = "door") { ASSERT_EQ(RunLuauChunk(L, (std::string("StubAnimatedObject.enterZone(") + object + ")").c_str(), "=enterZone"), 0); }
    void LeaveZone(const char* object = "door") { ASSERT_EQ(RunLuauChunk(L, (std::string("StubAnimatedObject.leaveZone(") + object + ")").c_str(), "=leaveZone"), 0); }

    // How many times the toggle handler ran, in all or for one object.
    int Toggles() { return static_cast<int>(Number(L, "#StubAnimatedObject.toggles")); }
    int Toggles(const char* object) { return static_cast<int>(Number(L, std::string("StubAnimatedObject.toggleCount(") + object + ")")); }

    void PullTrigger(uint32_t hand) { headset.SetHandInputs(hand, Trigger(1.0f)); FRAMES(1); }
    void ReleaseTrigger(uint32_t hand) { headset.SetHandInputs(hand, Trigger(0.0f)); FRAMES(1); }

    // Whether the Pointer Ray is drawn now, and the end of the line that was drawn ("x y z" to two decimals, or "none").
    std::string DrawnEnd() {
        return EvalLua(L, "(function() local x, y, z = drawnRayEnd(); return x and string.format('%.2f %.2f %.2f', x, y, z) or 'none' end)()");
    }

    // One count of the module's warnings in the log.
    int Warnings() { return CountLogLines("Animated Objects support disabled"); }

private:
    std::string stubs_;
};

}  // namespace animated_object_test
