#pragma once

#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

namespace object_grab_test {

using namespace hand_tools_test;

inline std::string HandsStubs(const char* variant = "ok") {
    return StubInstalled("hand_tool_hands", std::string("StubHandTool.installHands('") + variant + "')");
}

inline constexpr const char* kHoldHands = "_G.hands = StubHandTool.hands(foot.player); StubHandTool.hold(hands)";
inline void HoldHands(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldHands, "=holdHands"), 0); }

inline void StartObjectGrabMod(lua_State* L, fs25vr::VRBridge& bridge, const StartOptions& startOptions, const char* handsVariant = "ok",
                               bool hold = true) {
    const std::string stubs = std::string(startOptions.extraStubs) + "\n" + HandsStubs(handsVariant);
    StartOptions options = startOptions;
    options.extraStubs = stubs.c_str();
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
    ASSERT_EQ(RunLuauChunk(L, StubChunk("object_grab_helpers"), "=objectGrabHelpers"), 0);
    if (hold) ASSERT_NO_FATAL_FAILURE(HoldHands(L));
}

class VirtualGamepadObjectGrabTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    void Start(const StartOptions& startOptions = StartOptions()) {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, startOptions));
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    XINPUT_GAMEPAD Pad() {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    }
    bool PadGrip(uint32_t hand) { return (Pad().wButtons & (hand == kLeft ? XINPUT_GAMEPAD_LEFT_SHOULDER : XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0; }

    int PulsesTo(uint32_t hand) const {
        int count = 0;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == static_cast<int>(hand)) ++count;
        }
        return count;
    }

    void Squeeze(uint32_t hand, float grip) { headset.SetHandInputs(hand, hand_tools_test::Squeeze(0.0f, grip)); }
    int PickUps() { return static_cast<int>(Number(L, "#StubHandTool.pickUps")); }
    int Drops() { return static_cast<int>(Number(L, "#StubHandTool.drops")); }
};

}

// A Motion Controller pointing at (x, y, z) in tracking space (the index fingertip is there and the finger runs along -Z of the
// pose given): it moves a little, then holds the pose, so that it is "in use". The game clock is set by the test once.
#define POINT_HAND(hand, x, y, z) \
    do { \
        headset.SetHandPointing((hand), hand_tools_test::HandAt((x) - 0.1f, (y), (z))); \
        FRAMES(1); \
        headset.SetHandPointing((hand), hand_tools_test::HandAt((x), (y), (z))); \
        FRAMES(2); \
    } while (0)
