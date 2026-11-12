// What the Object Grab tests (tests/test_object_grab_*.cpp, ADR-0033) share, on top of hand_tools_test_support.h (the player on
// foot, the frame the game runs before the Companion Mod's update, the fake hands): the game's empty hands as a stub
// (StubHandTool.hands, shaped after HandToolHands in the SDK's gameSource.zip), loose objects with a mass that its targeter can hit
// (StubHandTool.looseObject), the Lua helpers that read the rays, and the macros that point a Motion Controller. Header-only
// (inline), in its own namespace; a test file brings it in with `using namespace object_grab_test;` inside its anonymous namespace.
//
// The stubs are a chunk of their own (hand_tool_hands.luau), like the Hand Tools' tool chunks, so that the later Object Grab tickets
// extend it without touching the Hand Tools' stubs. A test passes HandsStubs(variant) as StartOptions::extraStubs (concatenated
// after xml_settings.luau, if it uses both), and the player's hands are taken by HoldHands (the game's empty hands are Player.hands, held while
// the player holds no Hand Tool: Player:getHeldHandTool() is nil then, and answers the Hand Tool once one is held).
//
// What the stub offers (StubHandTool.installHands, hands, looseObject, ...) is described at the top of tests/stubs/hand_tool_hands.luau.

#pragma once

#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

namespace object_grab_test {

using namespace hand_tools_test;

// The game's hands in the game (hand_tool_hands.luau), for StartOptions::extraStubs. variant: see the top of that file.
inline std::string HandsStubs(const char* variant = "ok") {
    return StubInstalled("hand_tool_hands", std::string("StubHandTool.installHands('") + variant + "')");
}

// The player's hands are empty: the game's hands tool (Player.hands) is held, as _G.hands.
inline constexpr const char* kHoldHands = "_G.hands = StubHandTool.hands(foot.player); StubHandTool.hold(hands)";
inline void HoldHands(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldHands, "=holdHands"), 0); }

// The mod started on a player on foot with the game's hands (handsVariant: how HandToolHands is) and the Lua helpers above. The
// player holds the hands tool unless `hold` is false. startOptions.extraStubs is a Lua chunk that runs before the hands (xml_settings.luau,
// SprayCanStubs()...).
inline void StartObjectGrabMod(lua_State* L, fs25vr::VRBridge& bridge, const StartOptions& startOptions, const char* handsVariant = "ok",
                               bool hold = true) {
    const std::string stubs = std::string(startOptions.extraStubs) + "\n" + HandsStubs(handsVariant);
    StartOptions options = startOptions;
    options.extraStubs = stubs.c_str();
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
    ASSERT_EQ(RunLuauChunk(L, StubChunk("object_grab_helpers"), "=objectGrabHelpers"), 0);
    if (hold) ASSERT_NO_FATAL_FAILURE(HoldHands(L));
}

// A test of Object Grab that reads what the game reads from the Virtual Gamepad: the mod started on a player on foot with the game's
// hands, the Virtual Gamepad on and the game clock set (the in-use timeout is measured on it). The suite name is one the filter for the
// Companion Mod tests (VirtualGamepad*) covers.
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

    // What the game reads from XInput slot 3, the highest free slot of a machine without real pads.
    XINPUT_GAMEPAD Pad() {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    }
    bool PadGrip(uint32_t hand) { return (Pad().wButtons & (hand == kLeft ? XINPUT_GAMEPAD_LEFT_SHOULDER : XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0; }

    // How many haptic pulses reached a hand (the fake controllers keep them all).
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

}  // namespace object_grab_test

// A Motion Controller pointing at (x, y, z) in tracking space (the index fingertip is there and the finger runs along -Z of the
// pose given): it moves a little, then holds the pose, so that it is "in use". The game clock is set by the test once.
#define POINT_HAND(hand, x, y, z) \
    do { \
        headset.SetHandPointing((hand), hand_tools_test::HandAt((x) - 0.1f, (y), (z))); \
        FRAMES(1); \
        headset.SetHandPointing((hand), hand_tools_test::HandAt((x), (y), (z))); \
        FRAMES(2); \
    } while (0)
