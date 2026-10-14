// Installing the XInput hooks (ADR-0028, ticket #100, seam 2): against the real xinput1_4.dll of this machine, the
// Virtual Gamepad answers the game's normal XInputGetState / XInputGetCapabilities / XInputSetState calls in the
// highest free slot, every other slot still reaches the real devices, and removing the pad (or the Injector) puts
// the library back as it was. What the pad reads from the Motion Controllers is test_virtual_gamepad.cpp.

#include <gtest/gtest.h>
#include "core/injector_runtime.h"
#include "fake_openxr_runtime.h"
#include "luau/vr_bridge.h"
#include <array>

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::InjectorRuntime;
using fs25vr::VRBridge;

// What the real library answers for each slot right now, through the normal import.
struct RealSlots {
    std::array<DWORD, 4> state{};
    std::array<DWORD, 4> capabilities{};
    std::array<DWORD, 4> vibration{};

    static RealSlots Read() {
        RealSlots real;
        for (DWORD slot = 0; slot < 4; ++slot) {
            XINPUT_STATE s{};
            XINPUT_CAPABILITIES c{};
            XINPUT_VIBRATION v{};
            real.state[slot] = XInputGetState(slot, &s);
            real.capabilities[slot] = XInputGetCapabilities(slot, 0, &c);
            real.vibration[slot] = XInputSetState(slot, &v);
        }
        return real;
    }

    // The highest slot no real pad occupies, or -1.
    int HighestFree() const {
        for (int slot = 3; slot >= 0; --slot) {
            if (state[slot] != ERROR_SUCCESS) return slot;
        }
        return -1;
    }
};

class XInputHooksTest : public ::testing::Test {
protected:
    FakeTrackedHeadset headset;
    std::shared_ptr<VRBridge> bridge = std::make_shared<VRBridge>(headset.manager, nullptr);

    void SetUp() override {
        FakeOpenXrControllers::Inputs squeezed;
        squeezed.trigger = 1.0f;
        headset.SetHandInputs(1, squeezed);
        headset.NextFrame();
    }
};

} // namespace

TEST_F(XInputHooksTest, TheVirtualGamepadAnswersInTheHighestFreeSlotAndEveryOtherSlotPassesThrough) {
    const RealSlots real = RealSlots::Read();
    const int free = real.HighestFree();
    if (free < 0) GTEST_SKIP() << "all four XInput slots hold real pads on this machine";

    InjectorRuntime::Dependencies deps;
    deps.vrBridge = bridge;
    InjectorRuntime runtime(std::move(deps));
    EXPECT_FALSE(bridge->GetVirtualGamepad().AreHooksInstalled()) << "a pad nobody turned on hooks nothing";
    EXPECT_EQ(RealSlots::Read().state, real.state);

    bridge->GetVirtualGamepad().Configure(/*enabled=*/true, /*live=*/true);
    ASSERT_TRUE(bridge->GetVirtualGamepad().AreHooksInstalled());

    XINPUT_STATE state{};
    ASSERT_EQ(XInputGetState(static_cast<DWORD>(free), &state), static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(state.Gamepad.bRightTrigger, 255) << "the right trigger of the fake Motion Controller is RT";
    XINPUT_CAPABILITIES caps{};
    ASSERT_EQ(XInputGetCapabilities(static_cast<DWORD>(free), 0, &caps), static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(caps.Type, XINPUT_DEVTYPE_GAMEPAD);
    XINPUT_VIBRATION vibration{};
    EXPECT_EQ(XInputSetState(static_cast<DWORD>(free), &vibration), static_cast<DWORD>(ERROR_SUCCESS));

    // Every other slot is still the real devices'.
    const RealSlots during = RealSlots::Read();
    for (DWORD slot = 0; slot < 4; ++slot) {
        if (static_cast<int>(slot) == free) continue;
        EXPECT_EQ(during.state[slot], real.state[slot]) << "slot " << slot;
        EXPECT_EQ(during.capabilities[slot], real.capabilities[slot]) << "slot " << slot;
        EXPECT_EQ(during.vibration[slot], real.vibration[slot]) << "slot " << slot;
    }

    // Turning the pad off removes it and puts the library back.
    bridge->GetVirtualGamepad().Configure(false, true);
    EXPECT_FALSE(bridge->GetVirtualGamepad().AreHooksInstalled());
    const RealSlots after = RealSlots::Read();
    EXPECT_EQ(after.state, real.state);
    EXPECT_EQ(after.capabilities, real.capabilities);
    EXPECT_EQ(after.vibration, real.vibration);
}

TEST_F(XInputHooksTest, TheLibraryIsRestoredWhenTheInjectorGoesAwayWithThePadStillOn) {
    const RealSlots real = RealSlots::Read();
    const int free = real.HighestFree();
    if (free < 0) GTEST_SKIP() << "all four XInput slots hold real pads on this machine";

    {
        InjectorRuntime::Dependencies deps;
        deps.vrBridge = bridge;
        InjectorRuntime runtime(std::move(deps));
        bridge->GetVirtualGamepad().Configure(true, true);
        XINPUT_STATE state{};
        ASSERT_EQ(XInputGetState(static_cast<DWORD>(free), &state), static_cast<DWORD>(ERROR_SUCCESS));
    }
    EXPECT_FALSE(bridge->GetVirtualGamepad().AreHooksInstalled());
    EXPECT_EQ(RealSlots::Read().state, real.state);

    // The same pad hooks again under a new runtime.
    InjectorRuntime::Dependencies deps;
    deps.vrBridge = bridge;
    InjectorRuntime runtime(std::move(deps));
    EXPECT_TRUE(bridge->GetVirtualGamepad().AreHooksInstalled()) << "it is still wanted";
    XINPUT_STATE state{};
    EXPECT_EQ(XInputGetState(static_cast<DWORD>(free), &state), static_cast<DWORD>(ERROR_SUCCESS));
}

TEST_F(XInputHooksTest, AStandalonePadWithoutARuntimeNeverHooksTheLibrary) {
    bridge->GetVirtualGamepad().Configure(true, true);
    EXPECT_FALSE(bridge->GetVirtualGamepad().AreHooksInstalled());
}

TEST_F(XInputHooksTest, VibrationForThePadPlaysOnTheHandsAndVibrationForEverySlotElseStaysWithTheRealDevices) {
    const RealSlots real = RealSlots::Read();
    const int free = real.HighestFree();
    if (free < 0) GTEST_SKIP() << "all four XInput slots hold real pads on this machine";

    InjectorRuntime::Dependencies deps;
    deps.vrBridge = bridge;
    InjectorRuntime runtime(std::move(deps));
    bridge->GetVirtualGamepad().Configure(true, true);
    ASSERT_TRUE(bridge->GetVirtualGamepad().AreHooksInstalled());

    // Through the normal import, as the game calls it: other slots answer as the real library does and move no hand.
    XINPUT_VIBRATION rumble{ 65535, 65535 };
    for (DWORD slot = 0; slot < 4; ++slot) {
        if (static_cast<int>(slot) == free) continue;
        EXPECT_EQ(XInputSetState(slot, &rumble), real.vibration[slot]) << "slot " << slot;
    }
    headset.NextFrame();
    headset.NextFrame();
    EXPECT_TRUE(headset.HapticPulses().empty()) << "not the pad's slot: nothing for the Motion Controllers";

    // The pad's own slot reaches the hands: the left motor the left hand, the right motor the right.
    XINPUT_VIBRATION leftOnly{ 65535, 0 };
    ASSERT_EQ(XInputSetState(static_cast<DWORD>(free), &leftOnly), static_cast<DWORD>(ERROR_SUCCESS));
    headset.NextFrame();
    ASSERT_FALSE(headset.HapticPulses().empty());
    for (const auto& pulse : headset.HapticPulses()) EXPECT_EQ(pulse.hand, 0);

    bridge->GetVirtualGamepad().Configure(false, true);
    EXPECT_EQ(RealSlots::Read().vibration, real.vibration);
}
