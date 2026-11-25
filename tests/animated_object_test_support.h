#pragma once

#include "object_grab_test_support.h"
#include <string>

namespace animated_object_test {

using namespace object_grab_test;

inline std::string AnimatedObjectStubs(const char* variant = "ok") {
    return StubInstalled("animated_objects", std::string("StubAnimatedObject.install('") + variant + "')");
}

constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kDoorAhead = "_G.door = StubAnimatedObject.door({ name = 'door', x = -0.4, y = -0.2, z = 2.0 })";
// A door to the left side of the player's ray: a hand pointing at tracking (-0.4, -0.2, -0.3) has its fingertip at world (0.4, -0.2, 0.3).
constexpr const char* kDoorOnTheLeft = "_G.leftDoor = StubAnimatedObject.door({ name = 'leftDoor', x = 0.4, y = -0.2, z = 2.0 })";

class VirtualGamepadAnimatedObjectsTest : public VirtualGamepadObjectGrabTest {
protected:
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

    int Toggles() { return static_cast<int>(Number(L, "#StubAnimatedObject.toggles")); }
    int Toggles(const char* object) { return static_cast<int>(Number(L, std::string("StubAnimatedObject.toggleCount(") + object + ")")); }

    void PullTrigger(uint32_t hand) { headset.SetHandInputs(hand, Trigger(1.0f)); FRAMES(1); }
    void ReleaseTrigger(uint32_t hand) { headset.SetHandInputs(hand, Trigger(0.0f)); FRAMES(1); }

    // Whether the Pointer Ray is drawn now, and the end of the line that was drawn ("x y z" to two decimals, or "none").
    std::string DrawnEnd() {
        return EvalLua(L, "(function() local x, y, z = drawnRayEnd(); return x and string.format('%.2f %.2f %.2f', x, y, z) or 'none' end)()");
    }

    int Warnings() { return CountLogLines("Animated Objects support disabled"); }

private:
    std::string stubs_;
};

}
