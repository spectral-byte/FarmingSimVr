// The Virtual Gamepad is how Motion Controller buttons work (ADR-0028, spec #98, ticket #105), seen from
// outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, the test pushes the
// fake Motion Controllers' thumbsticks and buttons, drives frames and the game clock, and observes the camera's
// yaw, the Recenter calls reaching the bridge, the HUD state and what the game would read from XInput.
//
// These replace the tests of the Companion Mod's own button mapping (spec #71), which is gone: Snap Turn, the
// Recenter Action and the HUD Toggle Action from the Motion Controllers now arrive only as game actions bound on
// the Virtual Gamepad, so with the pad off the buttons fire none of them, and with it on they reach the game as
// pad inputs and nothing fires twice.

#include "companion_mod_fixture.h"
#include "input/virtual_gamepad.h"

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;

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

// The engine's XML settings API, remembering what was saved so a session can be replayed, and which keys the
// mod read and wrote.
constexpr const char* kXmlStubs = R"luau(
    _G.savedXml = _G.savedXml or {}
    _G.readKeys = {}
    _G.writtenKeys = {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function() return true end
    _G.createXMLFile = function() return 1 end
    _G.setXMLFloat = function(file, key, value) _G.writtenKeys[#_G.writtenKeys + 1] = key; _G.savedXml[key] = value end
    _G.setXMLBool = function(file, key, value) _G.writtenKeys[#_G.writtenKeys + 1] = key; _G.savedXml[key] = value end
    _G.setXMLString = function(file, key, value) _G.writtenKeys[#_G.writtenKeys + 1] = key; _G.savedXml[key] = value end
    _G.saveXMLFile = function() return true end
    _G.delete = function() end
    _G.loadXMLFile = function() return 2 end
    _G.getXMLFloat = function(file, key) _G.readKeys[#_G.readKeys + 1] = key; local v = _G.savedXml[key]; return type(v) == "number" and v or nil end
    _G.getXMLBool = function(file, key) _G.readKeys[#_G.readKeys + 1] = key; local v = _G.savedXml[key]; if type(v) == "boolean" then return v end return nil end
    _G.getXMLString = function(file, key) _G.readKeys[#_G.readKeys + 1] = key; local v = _G.savedXml[key]; return type(v) == "string" and v or nil end
)luau";

// The player sits in a vehicle cab: Snap Turn steps cam.rotY. recenterCalls counts the Recenter
// Actions that reach the Injector bridge.
constexpr const char* kCabAndRecenterCounter = R"luau(
    _G.cam = { isInside = true, cameraNode = 101, origRotY = 0.0, rotY = 0.0 }
    _G.g_currentMission = { controlledVehicle = { getActiveCamera = function() return _G.cam end } }
    _G.recenterCalls = 0
    local realRecenter = VRBridge.recenter
    VRBridge.recenter = function(...)
        _G.recenterCalls = _G.recenterCalls + 1
        return realRecenter(...)
    end
)luau";

constexpr double kQuarterTurn = 3.14159265 / 2.0;

class VirtualGamepadButtonsTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    std::unique_ptr<fs25vr::VRBridge> bridgeOwner = std::make_unique<fs25vr::VRBridge>(headset.manager, nullptr);
    fs25vr::VRBridge& Bridge() { return *bridgeOwner; }
    fs25vr::VirtualGamepad& Pad() { return Bridge().GetVirtualGamepad(); }

    // Starts the Companion Mod over the headset, in a cab, with the clock running. `settingsFile` is Lua that
    // fills savedXml first: what an earlier game session left in the settings file.
    void StartMod(const std::string& settingsFile = "") {
        ASSERT_TRUE(Bridge().Inject(L));
        Pad().SetClockForTesting([this] { return GameTime(); });
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, settingsFile, "=settingsFile"), 0);
        ASSERT_EQ(RunLuauChunk(L, kCabAndRecenterCounter, "=cab"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        SetGameTime(10000);
        NextFrame(&headset);
    }

    // The pad is on out of the box; a player turns it off with the console command.
    void TurnPadOff() {
        EXPECT_NE(EvalLua(L, "runCommand('vrVirtualGamepad', 'off')").find("off"), std::string::npos);
        NextFrame(&headset);
    }

    void Hold(uint32_t hand, const FakeOpenXrControllers::Inputs& inputs) {
        headset.SetHandInputs(hand, inputs);
        NextFrame(&headset);
    }
    void Release(uint32_t hand) { Hold(hand, FakeOpenXrControllers::Inputs{}); }

    // Pushes, then lets go; a press-and-release that is one frame down.
    void Tap(uint32_t hand, const FakeOpenXrControllers::Inputs& inputs) {
        Hold(hand, inputs);
        Release(hand);
    }

    double CabYaw() { return std::stod(EvalLua(L, "_G.cam.rotY")); }
    int RecenterCalls() { return std::stoi(EvalLua(L, "_G.recenterCalls")); }
    int HudState() { return std::stoi(EvalLua(L, "VRCompanion:getHudState()")); }

    // What the game reads from XInput slot 3 (the highest free slot of a machine without real pads).
    XINPUT_STATE Read(DWORD slot = 3) {
        XINPUT_STATE state{};
        state.Gamepad.wButtons = 0xFFFF;  // proves a call that reports a pad wrote the state
        EXPECT_EQ(Pad().GetState(slot, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state;
    }

    // Presses everything the old mapping read on each hand, one input at a time, across both hands, and says what
    // the Companion Mod itself did with them.
    void ExpectNoCompanionModActionFromAnyHand() {
        const int hudBefore = HudState();
        for (uint32_t hand : { kLeft, kRight }) {
            Tap(hand, Thumb(1.0f));
            EXPECT_NEAR(CabYaw(), 0.0, 1e-6) << "no Snap Turn to the right, hand " << hand;
            AdvanceGameTime(300);
            Tap(hand, Thumb(-1.0f));
            EXPECT_NEAR(CabYaw(), 0.0, 1e-6) << "no Snap Turn to the left, hand " << hand;
            AdvanceGameTime(300);
            Tap(hand, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
            Tap(hand, Pressed(&FakeOpenXrControllers::Inputs::menu));
            AdvanceGameTime(400);
        }
        EXPECT_EQ(RecenterCalls(), 0) << "no Recenter Action";
        EXPECT_EQ(HudState(), hudBefore) << "no HUD Toggle Action";
    }

    static FakeOpenXrControllers::Inputs Thumb(float x, float y = 0.0f) {
        FakeOpenXrControllers::Inputs inputs;
        inputs.thumbX = x;
        inputs.thumbY = y;
        return inputs;
    }
    static FakeOpenXrControllers::Inputs Pressed(bool FakeOpenXrControllers::Inputs::*button) {
        FakeOpenXrControllers::Inputs inputs;
        inputs.*button = true;
        return inputs;
    }
};

} // namespace

TEST_F(VirtualGamepadButtonsTest, AFreshInstallHasTheVirtualGamepadOn) {
    ASSERT_NO_FATAL_FAILURE(StartMod());

    EXPECT_EQ(EvalLua(L, "VRCompanion.virtualGamepadEnabled"), "true");
    EXPECT_NE(EvalLua(L, "runCommand('vrVirtualGamepad')").find("present in XInput slot 3"), std::string::npos)
        << "the game sees the pad without anyone turning it on";
    headset.SetHandInputs(kRight, Pressed(&FakeOpenXrControllers::Inputs::buttonLower));
    NextFrame(&headset);
    EXPECT_EQ(Read(3).Gamepad.wButtons, XINPUT_GAMEPAD_A) << "and it carries the Motion Controllers' buttons";
}

TEST_F(VirtualGamepadButtonsTest, WithThePadOffAThumbstickPushFiresNoSnapTurnOnEitherHand) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnPadOff();

    for (uint32_t hand : { kLeft, kRight }) {
        Tap(hand, Thumb(1.0f));
        EXPECT_NEAR(CabYaw(), 0.0, 1e-6) << "a push to the right, hand " << hand;
        AdvanceGameTime(300);
        Tap(hand, Thumb(-1.0f));
        EXPECT_NEAR(CabYaw(), 0.0, 1e-6) << "a push to the left, hand " << hand;
        AdvanceGameTime(300);
    }
}

TEST_F(VirtualGamepadButtonsTest, WithThePadOffAStickClickFiresNoRecenterActionOnEitherHand) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnPadOff();

    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    Tap(kLeft, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    EXPECT_EQ(RecenterCalls(), 0);
}

TEST_F(VirtualGamepadButtonsTest, WithThePadOffTheMenuButtonFiresNoHudToggleActionOnEitherHand) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnPadOff();
    const int hudBefore = HudState();

    Tap(kLeft, Pressed(&FakeOpenXrControllers::Inputs::menu));
    AdvanceGameTime(400);
    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::menu));
    EXPECT_EQ(HudState(), hudBefore);
}

TEST_F(VirtualGamepadButtonsTest, WithThePadOffEveryInputTheOldMappingReadStillFiresNothingOnItsOwn) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnPadOff();
    ExpectNoCompanionModActionFromAnyHand();
}

TEST_F(VirtualGamepadButtonsTest, WithThePadOnTheSameInputsReachTheGameAsPadInputsAndTheCompanionModFiresNothingItself) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    ExpectNoCompanionModActionFromAnyHand();  // the game's bindings turn pad inputs into actions, not the Companion Mod

    Hold(kRight, Thumb(1.0f));
    EXPECT_EQ(Read().Gamepad.sThumbRX, 32767) << "the right stick push is the right stick";
    EXPECT_EQ(Read().Gamepad.sThumbLX, 0);
    Release(kRight);

    Hold(kLeft, Thumb(-1.0f));
    EXPECT_EQ(Read().Gamepad.sThumbLX, -32767) << "the left stick push is the left stick";
    Release(kLeft);

    Hold(kLeft, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    EXPECT_EQ(Read().Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB) << "a left stick click is LS";
    Release(kLeft);

    Hold(kLeft, Pressed(&FakeOpenXrControllers::Inputs::menu));
    EXPECT_EQ(Read().Gamepad.wButtons, XINPUT_GAMEPAD_START) << "the left menu button is Start";
    Release(kLeft);

    // A right stick click let go without a push is an RS press on release, as it always is on the pad.
    Hold(kRight, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    Read();  // the game polls the pad every frame, so it sees the click go down
    Release(kRight);
    EXPECT_EQ(Read().Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);

    EXPECT_NEAR(CabYaw(), 0.0, 1e-6);
    EXPECT_EQ(RecenterCalls(), 0);
}

TEST_F(VirtualGamepadButtonsTest, ThePadStaysOffAcrossASessionRestartWhileTheCompanionModStillFiresNothing) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    TurnPadOff();
    const std::string saved = EvalLua(L, "savedXml['vrSettings.virtualGamepad']");
    ASSERT_EQ(saved, "false");

    lua_close(L);
    L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcclosure(L, &MockPrint, "print", 0);
    lua_setglobal(L, "print");
    bridgeOwner = std::make_unique<fs25vr::VRBridge>(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod("_G.savedXml = { ['vrSettings.virtualGamepad'] = false }"));

    XINPUT_STATE state{};
    EXPECT_EQ(Pad().GetState(3, &state), static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED)) << "off stays off";
    ExpectNoCompanionModActionFromAnyHand();
}

TEST_F(VirtualGamepadButtonsTest, TheOldButtonMappingCommandIsNotRegistered) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    EXPECT_EQ(EvalLua(L, "registeredCommands['vrMotionControllerMap']"), "nil");
    EXPECT_NE(EvalLua(L, "registeredCommands['vrMotionControllerStatus']"), "nil") << "the status command is still there";
    EXPECT_NE(EvalLua(L, "registeredCommands['vrVirtualGamepad']"), "nil");
}

TEST_F(VirtualGamepadButtonsTest, ASettingsFileHoldingTheOldMappingChangesNothingAndLogsNoWarning) {
    // A session with no old settings: what the log says.
    ASSERT_NO_FATAL_FAILURE(StartMod());
    NextFrame(&headset);
    const std::vector<std::string> cleanLog = s_capturedLogs;

    lua_close(L);
    L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcclosure(L, &MockPrint, "print", 0);
    lua_setglobal(L, "print");
    s_capturedLogs.clear();
    bridgeOwner = std::make_unique<fs25vr::VRBridge>(headset.manager, nullptr);

    // An earlier version left its mapping in the settings file: every action on a different input, and a value
    // that never made sense.
    ASSERT_NO_FATAL_FAILURE(StartMod(
        "_G.savedXml = { ['vrSettings.motionControllerMap.snapTurn'] = 'none', "
        "['vrSettings.motionControllerMap.recenter'] = 'buttonUpper', "
        "['vrSettings.motionControllerMap.hudToggle'] = 'nonsense' }"));
    NextFrame(&headset);

    EXPECT_EQ(s_capturedLogs, cleanLog) << "the game log reads exactly as it does without the old settings";
    for (const std::string& line : s_capturedLogs) {
        EXPECT_EQ(line.find("motionControllerMap"), std::string::npos) << line;
        EXPECT_EQ(line.find("mapping"), std::string::npos) << line;
    }

    // The old mapping's inputs do nothing, with the pad on and off.
    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::buttonUpper));
    Tap(kLeft, Pressed(&FakeOpenXrControllers::Inputs::buttonUpper));
    ExpectNoCompanionModActionFromAnyHand();
    TurnPadOff();
    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::buttonUpper));
    ExpectNoCompanionModActionFromAnyHand();
    EXPECT_EQ(RecenterCalls(), 0);

    // Not read and not written: saving settings (the pad switch is saved) leaves the old keys alone.
    for (const char* list : { "readKeys", "writtenKeys" }) {
        EXPECT_EQ(EvalLua(L, std::string("(function() for _, key in ipairs(_G.") + list +
                                 ") do if string.find(key, 'otionControllerMap') then return key end end return 'none' end)()"),
                  "none")
            << list;
    }
    EXPECT_EQ(EvalLua(L, "savedXml['vrSettings.motionControllerMap.recenter']"), "buttonUpper") << "left as it was";
    EXPECT_NE(EvalLua(L, "#writtenKeys"), "0") << "settings were saved at all";
}
