#include <gtest/gtest.h>
#include "core/injector_runtime.h"
#include "fake_openxr_runtime.h"
#include "input/virtual_gamepad.h"
#include "luau/vr_bridge.h"
#include <array>
#include <string>

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::InjectorRuntime;
using fs25vr::VRBridge;

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

}

TEST_F(XInputHooksTest, TheVirtualGamepadAnswersInTheHighestFreeSlotAndEveryOtherSlotPassesThrough) {
    const RealSlots real = RealSlots::Read();
    const int free = real.HighestFree();
    if (free < 0) GTEST_SKIP() << "all four XInput slots hold real pads on this machine";

    InjectorRuntime::Dependencies deps;
    deps.vrBridge = bridge;
    InjectorRuntime runtime(std::move(deps));
    ASSERT_TRUE(bridge->GetVirtualGamepad().AreHooksInstalled()) << "the pad is there from the moment the OpenXR session has its action set";
    bridge->GetVirtualGamepad().Configure(true, true);
    ASSERT_TRUE(bridge->GetVirtualGamepad().AreHooksInstalled());

    XINPUT_STATE state{};
    ASSERT_EQ(XInputGetState(static_cast<DWORD>(free), &state), static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(state.Gamepad.bRightTrigger, 255) << "the right trigger of the fake Motion Controller is RT";
    XINPUT_CAPABILITIES caps{};
    ASSERT_EQ(XInputGetCapabilities(static_cast<DWORD>(free), 0, &caps), static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(caps.Type, XINPUT_DEVTYPE_GAMEPAD);
    XINPUT_VIBRATION vibration{};
    EXPECT_EQ(XInputSetState(static_cast<DWORD>(free), &vibration), static_cast<DWORD>(ERROR_SUCCESS));

    const RealSlots during = RealSlots::Read();
    for (DWORD slot = 0; slot < 4; ++slot) {
        if (static_cast<int>(slot) == free) continue;
        EXPECT_EQ(during.state[slot], real.state[slot]) << "slot " << slot;
        EXPECT_EQ(during.capabilities[slot], real.capabilities[slot]) << "slot " << slot;
        EXPECT_EQ(during.vibration[slot], real.vibration[slot]) << "slot " << slot;
    }

    bridge->GetVirtualGamepad().Configure(false, true);
    EXPECT_FALSE(bridge->GetVirtualGamepad().AreHooksInstalled());
    const RealSlots after = RealSlots::Read();
    EXPECT_EQ(after.state, real.state);
    EXPECT_EQ(after.capabilities, real.capabilities);
    EXPECT_EQ(after.vibration, real.vibration);
}

TEST_F(XInputHooksTest, TheHooksComeWithTheOpenXrSessionAndItsActionSetWithoutTheCompanionModAndNotInPassThroughMode) {
    auto passThroughManager = std::make_shared<fs25vr::OpenXrManager>(std::make_shared<fs25vr::FakeOpenXrRuntime>());
    auto passThroughBridge = std::make_shared<VRBridge>(passThroughManager, nullptr);
    {
        InjectorRuntime::Dependencies deps;
        deps.vrBridge = passThroughBridge;
        InjectorRuntime runtime(std::move(deps));
        EXPECT_FALSE(passThroughBridge->GetVirtualGamepad().AreHooksInstalled());
        EXPECT_FALSE(passThroughBridge->GetVirtualGamepad().GetStatus().present);
    }

    auto fakeRuntime = std::make_shared<fs25vr::FakeOpenXrRuntime>();
    auto manager = std::make_shared<fs25vr::OpenXrManager>(fakeRuntime);
    auto lateBridge = std::make_shared<VRBridge>(manager, nullptr);
    InjectorRuntime::Dependencies deps;
    deps.vrBridge = lateBridge;
    InjectorRuntime runtime(std::move(deps));
    EXPECT_FALSE(lateBridge->GetVirtualGamepad().AreHooksInstalled()) << "no session yet";

    ASSERT_TRUE(manager->Initialize() && manager->CreateSession() && manager->CreateTrackingSpaces());
    manager->PollEvents();
    ASSERT_TRUE(manager->BeginFrame());
    EXPECT_TRUE(lateBridge->GetVirtualGamepad().AreHooksInstalled());
    EXPECT_TRUE(lateBridge->GetVirtualGamepad().GetStatus().present);
}

TEST_F(XInputHooksTest, WhenTheHooksCannotBeInstalledThereIsNoPadAndTheReasonSaysSo) {
    InjectorRuntime::Dependencies firstDeps;
    firstDeps.vrBridge = bridge;
    InjectorRuntime first(std::move(firstDeps));
    ASSERT_TRUE(bridge->GetVirtualGamepad().AreHooksInstalled());

    FakeTrackedHeadset secondHeadset;
    auto secondBridge = std::make_shared<VRBridge>(secondHeadset.manager, nullptr);
    InjectorRuntime::Dependencies secondDeps;
    secondDeps.vrBridge = secondBridge;
    InjectorRuntime second(std::move(secondDeps));
    secondHeadset.NextFrame();
    ASSERT_FALSE(secondBridge->GetVirtualGamepad().AreHooksInstalled());

    secondBridge->GetVirtualGamepad().Configure(true, true);
    const auto status = secondBridge->GetVirtualGamepad().GetStatus();
    EXPECT_FALSE(status.present) << "a pad the game cannot see is no pad";
    EXPECT_NE(std::string(status.reason).find("hook"), std::string::npos) << status.reason;
}

TEST_F(XInputHooksTest, ACallThatReachesNoPadStillGoesToTheRealFunctionSoRealPadsDoNotVanish) {
    static DWORD s_stateSlot = 99;
    const auto realGetState = +[](DWORD slot, XINPUT_STATE* state) -> DWORD {
        s_stateSlot = slot;
        state->Gamepad.wButtons = 0x1234;
        return ERROR_SUCCESS;
    };
    const auto realGetCapabilities = +[](DWORD, DWORD, XINPUT_CAPABILITIES*) -> DWORD { return 4242; };
    const auto realSetState = +[](DWORD, XINPUT_VIBRATION*) -> DWORD { return 4343; };
    fs25vr::VirtualGamepad::SetHookOriginalsForTesting(realGetState, realGetCapabilities, realSetState);

    XINPUT_STATE state{};
    XINPUT_CAPABILITIES caps{};
    XINPUT_VIBRATION vibration{};
    EXPECT_EQ(fs25vr::VirtualGamepad::Intercept_XInputGetState(2, &state), static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(s_stateSlot, 2u);
    EXPECT_EQ(state.Gamepad.wButtons, 0x1234);
    EXPECT_EQ(fs25vr::VirtualGamepad::Intercept_XInputGetCapabilities(2, 0, &caps), 4242u);
    EXPECT_EQ(fs25vr::VirtualGamepad::Intercept_XInputSetState(2, &vibration), 4343u);

    fs25vr::VirtualGamepad::SetHookOriginalsForTesting(nullptr, nullptr, nullptr);
    EXPECT_EQ(fs25vr::VirtualGamepad::Intercept_XInputGetState(2, &state), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED))
        << "with nothing to call there is nothing there";
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

    XINPUT_VIBRATION rumble{ 65535, 65535 };
    for (DWORD slot = 0; slot < 4; ++slot) {
        if (static_cast<int>(slot) == free) continue;
        EXPECT_EQ(XInputSetState(slot, &rumble), real.vibration[slot]) << "slot " << slot;
    }
    headset.NextFrame();
    headset.NextFrame();
    EXPECT_TRUE(headset.HapticPulses().empty()) << "not the pad's slot: nothing for the Motion Controllers";

    XINPUT_VIBRATION leftOnly{ 65535, 0 };
    ASSERT_EQ(XInputSetState(static_cast<DWORD>(free), &leftOnly), static_cast<DWORD>(ERROR_SUCCESS));
    headset.NextFrame();
    ASSERT_FALSE(headset.HapticPulses().empty());
    for (const auto& pulse : headset.HapticPulses()) EXPECT_EQ(pulse.hand, 0);

    bridge->GetVirtualGamepad().Configure(false, true);
    EXPECT_EQ(RealSlots::Read().vibration, real.vibration);
}
