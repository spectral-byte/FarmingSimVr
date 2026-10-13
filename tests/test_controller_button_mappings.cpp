// Motion Controller buttons trigger Snap Turn, Recenter and HUD toggle (spec #71, ticket #75), seen
// from outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, the
// test pushes the fake controllers' thumbsticks and buttons, drives frames and the game clock, and
// observes the camera's yaw, the Recenter calls reaching the bridge, and the HUD state.

#include "companion_mod_fixture.h"

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

class ControllerButtonMappingTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    // Starts the Companion Mod over the headset, in a cab, with the clock running.
    void StartMod() {
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, kCabAndRecenterCounter, "=cab"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        SetGameTime(10000);
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
};

FakeOpenXrControllers::Inputs Thumb(float x, float y = 0.0f) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.thumbX = x;
    inputs.thumbY = y;
    return inputs;
}

} // namespace

TEST_F(ControllerButtonMappingTest, PushingTheThumbstickRightSnapTurnsRight) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    Hold(kRight, Thumb(1.0f));
    // Snap Turn right steps the in-cab view yaw by -90 degrees (same as the key action).
    EXPECT_NEAR(CabYaw(), -3.14159265 / 2.0, 1e-4);
}

TEST_F(ControllerButtonMappingTest, PushingTheThumbstickLeftSnapTurnsLeftOnEitherHand) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    Hold(kLeft, Thumb(-1.0f));
    EXPECT_NEAR(CabYaw(), 3.14159265 / 2.0, 1e-4) << "left hand, left push";
    Release(kLeft);

    AdvanceGameTime(300);
    Hold(kLeft, Thumb(1.0f));
    EXPECT_NEAR(CabYaw(), 0.0, 1e-4) << "left hand, right push turns back";
    Release(kLeft);

    AdvanceGameTime(300);
    Hold(kRight, Thumb(-1.0f));
    EXPECT_NEAR(CabYaw(), 3.14159265 / 2.0, 1e-4) << "right hand, left push";
}

TEST_F(ControllerButtonMappingTest, ASnapTurnIsOnePerPushAndKeepsTheKeyActionDebounce) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    Hold(kRight, Thumb(1.0f));
    const double afterFirst = CabYaw();
    EXPECT_NEAR(afterFirst, -3.14159265 / 2.0, 1e-4);

    // Holding the stick over for a second does not turn again.
    for (int i = 0; i < 60; ++i) NextFrame(&headset);
    EXPECT_NEAR(CabYaw(), afterFirst, 1e-6) << "a held push is one Snap Turn";

    // A new push inside the 250 ms cooldown of the last Snap Turn is debounced, as a key press is.
    SetGameTime(GameTime() - 1000.0 + 100.0);  // 100 ms after the first turn
    Release(kRight);
    Hold(kRight, Thumb(1.0f));
    EXPECT_NEAR(CabYaw(), afterFirst, 1e-6) << "a second push 100 ms later is debounced";

    // Once the cooldown is over a new push turns again.
    Release(kRight);
    AdvanceGameTime(300);
    Hold(kRight, Thumb(1.0f));
    EXPECT_NEAR(CabYaw(), afterFirst - 3.14159265 / 2.0, 1e-4);
}

TEST_F(ControllerButtonMappingTest, AThumbstickPushedMostlyUpOrOnlySlightlySidewaysDoesNotSnapTurn) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    Hold(kRight, Thumb(0.5f, 0.0f));
    EXPECT_NEAR(CabYaw(), 0.0, 1e-6) << "half way is inside the deadzone";
    Hold(kRight, Thumb(0.8f, 0.95f));
    EXPECT_NEAR(CabYaw(), 0.0, 1e-6) << "pushed more forward than sideways";
    Hold(kRight, Thumb(0.0f, 1.0f));
    EXPECT_NEAR(CabYaw(), 0.0, 1e-6);
}

FakeOpenXrControllers::Inputs Pressed(bool FakeOpenXrControllers::Inputs::*button) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.*button = true;
    return inputs;
}

TEST_F(ControllerButtonMappingTest, ClickingTheThumbstickTriggersTheRecenterActionOnEitherHand) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    EXPECT_EQ(RecenterCalls(), 0);

    Hold(kRight, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    EXPECT_EQ(RecenterCalls(), 1) << "right hand";
    for (int i = 0; i < 30; ++i) NextFrame(&headset);
    EXPECT_EQ(RecenterCalls(), 1) << "holding the click down is one Recenter Action";
    Release(kRight);

    Hold(kLeft, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    EXPECT_EQ(RecenterCalls(), 2) << "left hand";
    Release(kLeft);

    // Both hands clicking in the same frame is one Recenter Action.
    FakeOpenXrControllers::Inputs click = Pressed(&FakeOpenXrControllers::Inputs::thumbClick);
    headset.SetHandInputs(kLeft, click);
    headset.SetHandInputs(kRight, click);
    NextFrame(&headset);
    EXPECT_EQ(RecenterCalls(), 3);
}

TEST_F(ControllerButtonMappingTest, PressingTheMenuButtonTriggersTheHudToggleActionOnEitherHandWithItsDebounce) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    auto hudState = [&]() { return std::stoi(EvalLua(L, "VRCompanion:getHudState()")); };
    const int start = hudState();
    const auto menu = Pressed(&FakeOpenXrControllers::Inputs::menu);

    Hold(kLeft, menu);
    EXPECT_EQ(hudState(), (start + 1) % 3) << "left hand";
    for (int i = 0; i < 30; ++i) NextFrame(&headset);
    EXPECT_EQ(hudState(), (start + 1) % 3) << "holding the button is one toggle";
    Release(kLeft);

    // Pressed again 100 ms after the last toggle: inside the HUD Toggle Action's 300 ms debounce.
    SetGameTime(GameTime() - 500.0 + 100.0);
    Hold(kRight, menu);
    EXPECT_EQ(hudState(), (start + 1) % 3) << "debounced like the key action";
    Release(kRight);

    AdvanceGameTime(400);
    Hold(kRight, menu);
    EXPECT_EQ(hudState(), (start + 2) % 3) << "right hand, after the debounce";
}
