// The Virtual Gamepad (ADR-0028, spec #98, ticket #100), seen from outside: the real Companion Mod runs
// against the real VRBridge over FakeTrackedHeadset, the test drives the fake Motion Controllers and the game
// clock, and reads what the game would read by calling the answers the XInput hooks route the game's calls to
// (VirtualGamepad::GetState / GetCapabilities). Installing the hooks themselves is test_xinput_hooks.cpp.

#include "companion_mod_fixture.h"
#include "input/virtual_gamepad.h"

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::FakeTrackedHeadsetOptions;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

// Console commands as the engine registers them, so tests can run the handlers.
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

// The engine's XML settings API, remembering what was saved so a session can be replayed.
constexpr const char* kXmlStubs = R"luau(
    _G.savedXml = _G.savedXml or {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function() return true end
    _G.createXMLFile = function() return 1 end
    _G.setXMLFloat = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLBool = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLString = function(file, key, value) _G.savedXml[key] = value end
    _G.saveXMLFile = function() return true end
    _G.delete = function() end
    _G.loadXMLFile = function() return 2 end
    _G.getXMLFloat = function(file, key) local v = _G.savedXml[key]; return type(v) == "number" and v or nil end
    _G.getXMLBool = function(file, key) local v = _G.savedXml[key]; if type(v) == "boolean" then return v end return nil end
    _G.getXMLString = function(file, key) local v = _G.savedXml[key]; return type(v) == "string" and v or nil end
)luau";

class VirtualGamepadTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    std::unique_ptr<fs25vr::VRBridge> bridgeOwner = std::make_unique<fs25vr::VRBridge>(headset.manager, nullptr);
    fs25vr::VRBridge& Bridge() { return *bridgeOwner; }

    // Starts the Companion Mod over the headset with the clock running. `settingsFile` is Lua that fills
    // savedXml first: what an earlier game session left in the settings file.
    void StartMod(const std::string& settingsFile = "") {
        ASSERT_TRUE(Bridge().Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, settingsFile, "=settingsFile"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        SetGameTime(10000);
        NextFrame(&headset);
    }

    // A new game session over the same headset: a fresh Lua state and a fresh Injector bridge (the pad starts as
    // a new process finds it), the settings file carried over.
    void RestartGameSession() {
        const std::string saved = EvalLua(L, "savedXml['vrSettings.virtualGamepad']");
        const std::string seed = saved == "nil" ? "_G.savedXml = {}" : "_G.savedXml = { ['vrSettings.virtualGamepad'] = " + saved + " }";
        lua_close(L);
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcclosure(L, &MockPrint, "print", 0);
        lua_setglobal(L, "print");
        s_capturedLogs.clear();
        bridgeOwner = std::make_unique<fs25vr::VRBridge>(headset.manager, nullptr);
        StartMod(seed);
    }

    std::string Command(const std::string& line) {
        return EvalLua(L, "runCommand(" + line + ")");
    }

    void TurnOn() {
        Command("'vrVirtualGamepad', 'on'");
        NextFrame(&headset);
    }

    void Hold(uint32_t hand, const FakeOpenXrControllers::Inputs& inputs) {
        headset.SetHandInputs(hand, inputs);
        NextFrame(&headset);
    }

    fs25vr::VirtualGamepad& Pad() { return Bridge().GetVirtualGamepad(); }

    // What the game reads from XInput slot 3 (the highest free slot of a machine without real pads).
    XINPUT_STATE Read(DWORD slot = 3) {
        XINPUT_STATE state{};
        state.Gamepad.wButtons = 0xFFFF;  // proves a call that reports a pad wrote the state
        EXPECT_EQ(Pad().GetState(slot, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state;
    }
};

FakeOpenXrControllers::Inputs Trigger(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = value;
    return inputs;
}

} // namespace

TEST_F(VirtualGamepadTest, TurnedOnItIsAnXboxPadInTheHighestFreeSlotAndTheTriggersAreLTAndRT) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnOn();

    headset.SetHandInputs(kLeft, Trigger(0.5f));
    headset.SetHandInputs(kRight, Trigger(1.0f));
    NextFrame(&headset);

    const XINPUT_STATE state = Read(3);
    EXPECT_EQ(state.Gamepad.bLeftTrigger, 128) << "half way, analog";
    EXPECT_EQ(state.Gamepad.bRightTrigger, 255);

    // Every other slot belongs to the real devices (there are none here).
    XINPUT_STATE other{};
    for (DWORD slot = 0; slot < 3; ++slot) {
        EXPECT_EQ(Pad().GetState(slot, &other), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED)) << "slot " << slot;
    }
}

TEST_F(VirtualGamepadTest, EachControlOnEachHandIsItsOwnPadInput) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnOn();

    FakeOpenXrControllers::Inputs left;
    left.grip = 1.0f;
    left.thumbX = 0.5f;
    left.thumbY = -1.0f;
    left.thumbClick = true;
    left.buttonLower = true;
    left.menu = true;
    headset.SetHandInputs(kLeft, left);
    NextFrame(&headset);

    XINPUT_STATE state = Read();
    EXPECT_EQ(state.Gamepad.wButtons,
              XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_START)
        << "left grip, stick click, lower face button and menu button; nothing of the right hand";
    EXPECT_EQ(state.Gamepad.sThumbLX, 16384) << "analog, +1 is right";
    EXPECT_EQ(state.Gamepad.sThumbLY, -32767) << "analog, -1 is down";
    EXPECT_EQ(state.Gamepad.sThumbRX, 0);
    EXPECT_EQ(state.Gamepad.sThumbRY, 0);

    left = {};
    left.buttonUpper = true;
    headset.SetHandInputs(kLeft, left);
    NextFrame(&headset);
    EXPECT_EQ(Read().Gamepad.wButtons, XINPUT_GAMEPAD_Y) << "left upper face button";

    headset.SetHandInputs(kLeft, {});
    FakeOpenXrControllers::Inputs right;
    right.grip = 1.0f;
    right.thumbX = -1.0f;
    right.thumbY = 0.25f;
    right.thumbClick = true;
    right.buttonLower = true;
    right.menu = true;
    headset.SetHandInputs(kRight, right);
    NextFrame(&headset);

    state = Read();
    EXPECT_EQ(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_RIGHT_THUMB | XINPUT_GAMEPAD_A)
        << "right grip, stick click and lower face button; the right menu button feeds nothing";
    EXPECT_EQ(state.Gamepad.sThumbRX, -32767);
    EXPECT_EQ(state.Gamepad.sThumbRY, 8192);
    EXPECT_EQ(state.Gamepad.sThumbLX, 0);

    right = {};
    right.buttonUpper = true;
    headset.SetHandInputs(kRight, right);
    NextFrame(&headset);
    EXPECT_EQ(Read().Gamepad.wButtons, XINPUT_GAMEPAD_B) << "right upper face button";
}

TEST_F(VirtualGamepadTest, BackAndTheDPadAreNeverFed) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnOn();

    FakeOpenXrControllers::Inputs everything;
    everything.trigger = 1.0f;
    everything.grip = 1.0f;
    everything.thumbX = 1.0f;
    everything.thumbY = 1.0f;
    everything.thumbClick = true;
    everything.buttonLower = true;
    everything.buttonUpper = true;
    everything.menu = true;
    headset.SetHandInputs(kLeft, everything);
    headset.SetHandInputs(kRight, everything);
    NextFrame(&headset);

    const WORD fed = Read().Gamepad.wButtons;
    EXPECT_EQ(fed & (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
                     XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT), 0);
    EXPECT_EQ(fed, XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_LEFT_THUMB |
                       XINPUT_GAMEPAD_RIGHT_THUMB | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X |
                       XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_START);
}

TEST_F(VirtualGamepadTest, AGripIsPressedAboveSixtyPercentAndReleasedBelowFortyWithNoFlickerBetween) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnOn();

    const auto leftBumper = [&](float grip) {
        FakeOpenXrControllers::Inputs inputs;
        inputs.grip = grip;
        headset.SetHandInputs(kLeft, inputs);
        NextFrame(&headset);
        return (Read().Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    };
    EXPECT_FALSE(leftBumper(0.5f)) << "half way from rest is not a press";
    EXPECT_TRUE(leftBumper(0.7f));
    EXPECT_TRUE(leftBumper(0.5f)) << "still held between the two thresholds";
    EXPECT_FALSE(leftBumper(0.3f));
    EXPECT_FALSE(leftBumper(0.5f)) << "still released between the two thresholds";
    EXPECT_TRUE(leftBumper(1.0f));
}

TEST_F(VirtualGamepadTest, ThePacketNumberAdvancesWhenTheStateChangesAndOnlyThen) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnOn();

    const DWORD rest = Read().dwPacketNumber;
    NextFrame(&headset);
    EXPECT_EQ(Read().dwPacketNumber, rest) << "nothing moved";

    Hold(kRight, Trigger(0.5f));
    const DWORD pulled = Read().dwPacketNumber;
    EXPECT_NE(pulled, rest) << "the trigger moved";
    NextFrame(&headset);
    EXPECT_EQ(Read().dwPacketNumber, pulled) << "and then nothing moved again";

    // A hand's pose is not a pad input: moving the Motion Controller changes no packet.
    headset.SetHandPose(kRight, fs25vr::HeadPose(fs25vr::Vector3(0.3f, 1.0f, -0.4f), fs25vr::Quaternion(0, 0, 0, 1), fs25vr::EulerAngles(0, 0, 0)));
    NextFrame(&headset);
    EXPECT_EQ(Read().dwPacketNumber, pulled);

    Hold(kRight, Trigger(0.0f));
    EXPECT_NE(Read().dwPacketNumber, pulled) << "letting go is a change too";
}

TEST_F(VirtualGamepadTest, ItReportsAnOrdinaryGamepadWithFullRangeInputsAndOnlyInItsOwnSlot) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnOn();

    XINPUT_CAPABILITIES caps{};
    ASSERT_EQ(Pad().GetCapabilities(3, 0, &caps), static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(caps.Type, XINPUT_DEVTYPE_GAMEPAD);
    EXPECT_EQ(caps.SubType, XINPUT_DEVSUBTYPE_GAMEPAD);
    EXPECT_EQ(caps.Gamepad.bLeftTrigger, 255);
    EXPECT_EQ(caps.Gamepad.bRightTrigger, 255);
    EXPECT_NE(caps.Gamepad.sThumbLX, 0) << "a stick that reports a range";

    for (DWORD slot = 0; slot < 3; ++slot) {
        EXPECT_EQ(Pad().GetCapabilities(slot, 0, &caps), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED)) << "slot " << slot;
    }
}

TEST_F(VirtualGamepadTest, ItIsOffByDefaultAndTheGameSeesNoPadUntilThePlayerTurnsItOn) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    Hold(kRight, Trigger(1.0f));

    XINPUT_STATE state{};
    for (DWORD slot = 0; slot < 4; ++slot) {
        EXPECT_EQ(Pad().GetState(slot, &state), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED)) << "slot " << slot;
    }
    EXPECT_EQ(EvalLua(L, "select(1, VRBridge.getVirtualGamepad())"), "false");
    EXPECT_FALSE(Pad().AreHooksInstalled());
    for (const std::string& line : s_capturedLogs) {
        EXPECT_EQ(line.find("Virtual Gamepad:"), std::string::npos) << "a player who never turned it on reads nothing: " << line;
    }

    TurnOn();
    EXPECT_EQ(Read().Gamepad.bRightTrigger, 255);
}

TEST_F(VirtualGamepadTest, TurningItOffRemovesThePadFromTheGameAgain) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnOn();
    ASSERT_EQ(Read().dwPacketNumber, 0u);

    EXPECT_NE(Command("'vrVirtualGamepad', 'off'").find("off"), std::string::npos);
    NextFrame(&headset);
    XINPUT_STATE state{};
    EXPECT_EQ(Pad().GetState(3, &state), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED));
    XINPUT_CAPABILITIES caps{};
    EXPECT_EQ(Pad().GetCapabilities(3, 0, &caps), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED));
}

TEST_F(VirtualGamepadTest, TheSettingIsSavedAndTheNextGameSessionStartsWithThePadOrWithout) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnOn();
    ASSERT_NO_FATAL_FAILURE(RestartGameSession());
    EXPECT_EQ(Read().Gamepad.bLeftTrigger, 0) << "the pad is there again without anyone turning it on";

    Command("'vrVirtualGamepad', 'off'");
    ASSERT_NO_FATAL_FAILURE(RestartGameSession());
    XINPUT_STATE state{};
    EXPECT_EQ(Pad().GetState(3, &state), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED)) << "off stays off";
}

TEST_F(VirtualGamepadTest, AnUntrackedHandReadsNeutralWhileTheOtherHandStillPlays) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnOn();

    FakeOpenXrControllers::Inputs squeezed;
    squeezed.trigger = 1.0f;
    squeezed.grip = 1.0f;
    squeezed.thumbX = 1.0f;
    squeezed.buttonLower = true;
    headset.SetHandInputs(kLeft, squeezed);
    headset.SetHandInputs(kRight, squeezed);
    NextFrame(&headset);
    ASSERT_EQ(Read().Gamepad.bLeftTrigger, 255);

    headset.SetHandTracked(kLeft, false);
    NextFrame(&headset);
    const XINPUT_GAMEPAD pad = Read().Gamepad;
    EXPECT_EQ(pad.bLeftTrigger, 0);
    EXPECT_EQ(pad.sThumbLX, 0);
    EXPECT_EQ(pad.wButtons & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_X), 0) << "nothing of the left hand";
    EXPECT_EQ(pad.bRightTrigger, 255);
    EXPECT_EQ(pad.sThumbRX, 32767);
    EXPECT_EQ(pad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_A);
}

TEST_F(VirtualGamepadTest, WithMotionControllersOffThePadStaysInTheGameButReadsNeutral) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnOn();
    FakeOpenXrControllers::Inputs squeezed;
    squeezed.trigger = 1.0f;
    squeezed.buttonLower = true;
    headset.SetHandInputs(kRight, squeezed);
    NextFrame(&headset);
    ASSERT_EQ(Read().Gamepad.bRightTrigger, 255);

    EXPECT_NE(Command("'vrMotionControllers', 'off'").find("off"), std::string::npos);
    NextFrame(&headset);
    const XINPUT_STATE off = Read();
    EXPECT_EQ(off.Gamepad.bRightTrigger, 0);
    EXPECT_EQ(off.Gamepad.wButtons, 0);
    EXPECT_NE(EvalLua(L, "VRCompanion:getMotionControllerStatus()").find("present in XInput slot 3"), std::string::npos)
        << "so the game's device list does not flicker";

    EXPECT_NE(Command("'vrMotionControllers', 'on'").find("on"), std::string::npos);
    NextFrame(&headset);
    EXPECT_EQ(Read().Gamepad.bRightTrigger, 255);
}

TEST_F(VirtualGamepadTest, ThereIsNoPadWithoutAnOpenXrSessionAndNoPadWhenTheRuntimeRefusedTheActionSet) {
    // No OpenXR manager at all, and a manager that never started a session (Pass-Through Mode).
    fs25vr::VRBridge withoutRuntime(nullptr, nullptr);
    fs25vr::VRBridge passThrough(std::make_shared<fs25vr::OpenXrManager>(std::make_shared<fs25vr::FakeOpenXrRuntime>()), nullptr);
    for (fs25vr::VRBridge* bridgeUnderTest : { &withoutRuntime, &passThrough }) {
        bridgeUnderTest->GetVirtualGamepad().Configure(true, true);
        const auto status = bridgeUnderTest->GetVirtualGamepad().GetStatus();
        EXPECT_FALSE(status.present);
        EXPECT_STREQ(status.reason, "no OpenXR session");
        XINPUT_STATE state{};
        for (DWORD slot = 0; slot < 4; ++slot) {
            EXPECT_EQ(bridgeUnderTest->GetVirtualGamepad().GetState(slot, &state), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED));
        }
    }

    FakeTrackedHeadset refused(FakeTrackedHeadsetOptions{ .refuseActionSet = true });
    fs25vr::VRBridge refusedBridge(refused.manager, nullptr);
    refusedBridge.GetVirtualGamepad().Configure(true, true);
    const auto status = refusedBridge.GetVirtualGamepad().GetStatus();
    EXPECT_FALSE(status.present);
    EXPECT_NE(std::string(status.reason).find("refused"), std::string::npos) << status.reason;
}

TEST_F(VirtualGamepadTest, ThePadTakesTheHighestSlotNoRealPadOccupiesSoARealPadKeepsTheFirstOne) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    Pad().SetRealPadProbeForTesting([](DWORD slot) { return slot == 0 || slot == 3; });
    TurnOn();

    EXPECT_EQ(EvalLua(L, "select(2, VRBridge.getVirtualGamepad())"), "2");
    EXPECT_EQ(Read(2).dwPacketNumber, 0u);
    XINPUT_STATE state{};
    EXPECT_EQ(Pad().GetState(3, &state), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED)) << "a real pad's slot passes through";
    EXPECT_EQ(Pad().GetState(0, &state), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED));
}

TEST_F(VirtualGamepadTest, WithAllFourSlotsTakenByRealPadsThereIsNoPadAndTheLogSaysSoOnce) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    Pad().SetRealPadProbeForTesting([](DWORD) { return true; });
    TurnOn();
    for (int i = 0; i < 5; ++i) NextFrame(&headset);

    XINPUT_STATE state{};
    for (DWORD slot = 0; slot < 4; ++slot) {
        EXPECT_EQ(Pad().GetState(slot, &state), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED));
    }
    EXPECT_NE(EvalLua(L, "VRCompanion:getMotionControllerStatus()").find("not present (all four XInput slots hold real pads)"),
              std::string::npos);
    int lines = 0;
    for (const std::string& line : s_capturedLogs) {
        if (line.find("all four XInput slots hold real pads") != std::string::npos) ++lines;
    }
    EXPECT_EQ(lines, 1);

    // A pad that is unplugged frees a slot and the Virtual Gamepad appears.
    Pad().SetRealPadProbeForTesting([](DWORD slot) { return slot != 1; });
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "select(2, VRBridge.getVirtualGamepad())"), "1");
}

TEST_F(VirtualGamepadTest, TheGameLogAndTheStatusCommandSayWhetherThePadIsThereAndInWhichSlot) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    EXPECT_NE(Command("'vrMotionControllerStatus'").find("Virtual Gamepad: not present (off)"), std::string::npos);

    TurnOn();
    bool appeared = false;
    for (const std::string& line : s_capturedLogs) {
        if (line.find("Virtual Gamepad: present in XInput slot 3") != std::string::npos) appeared = true;
    }
    EXPECT_TRUE(appeared) << "the game log names the slot the pad took";
    EXPECT_NE(Command("'vrMotionControllerStatus'").find("Virtual Gamepad: present in XInput slot 3"), std::string::npos);
    EXPECT_NE(Command("'vrVirtualGamepad'").find("present in XInput slot 3"), std::string::npos);

    Command("'vrVirtualGamepad', 'off'");
    NextFrame(&headset);
    EXPECT_NE(Command("'vrMotionControllerStatus'").find("Virtual Gamepad: not present (off)"), std::string::npos);
}
