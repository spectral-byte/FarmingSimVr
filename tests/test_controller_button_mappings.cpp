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

    // Starts the Companion Mod over the headset, in a cab, with the clock running. `settingsFile`
    // is Lua that fills savedXml first: what an earlier game session left in the settings file.
    void StartMod(const std::string& settingsFile = "") {
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, settingsFile, "=settingsFile"), 0);
        ASSERT_EQ(RunLuauChunk(L, kCabAndRecenterCounter, "=cab"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        SetGameTime(10000);
        NextFrame(&headset);
    }

    // A new game session over the same headset: fresh Lua state, the settings file carried over.
    void RestartGameSession() {
        std::string seed;
        for (const char* key : { "snapTurn", "recenter", "hudToggle" }) {
            const std::string value = EvalLua(L, std::string("savedXml['vrSettings.controllerMap.") + key + "']");
            if (value != "nil") seed += std::string("savedXml['vrSettings.controllerMap.") + key + "'] = '" + value + "'\n";
        }
        lua_close(L);
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcclosure(L, &MockPrint, "print", 0);
        lua_setglobal(L, "print");
        StartMod("_G.savedXml = {}\n" + seed);
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

TEST_F(ControllerButtonMappingTest, SnapTurnStaysOffWhileAMenuIsShowing) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_gui = { _visible = true, getIsGuiVisible = function(self) return self._visible end }
    )luau", "=menuOpen"), 0);
    NextFrame(&headset);
    Tap(kRight, Thumb(1.0f));
    EXPECT_NEAR(CabYaw(), 0.0, 1e-6) << "no Snap Turn with a menu showing";

    ASSERT_EQ(RunLuauChunk(L, "_G.g_gui._visible = false", "=menuClosed"), 0);
    NextFrame(&headset);
    AdvanceGameTime(300);
    Tap(kRight, Thumb(1.0f));
    EXPECT_NEAR(CabYaw(), -3.14159265 / 2.0, 1e-4) << "Snap Turn is back once the menu is closed";
}

TEST_F(ControllerButtonMappingTest, NothingHappensWhileMotionControllersAreOff) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllers', 'off')").find("off"), std::string::npos);
    const int hudBefore = std::stoi(EvalLua(L, "VRCompanion:getHudState()"));

    Tap(kRight, Thumb(1.0f));
    Tap(kLeft, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::menu));
    EXPECT_NEAR(CabYaw(), 0.0, 1e-6);
    EXPECT_EQ(RecenterCalls(), 0);
    EXPECT_EQ(std::stoi(EvalLua(L, "VRCompanion:getHudState()")), hudBefore);

    // Back on: the next press works, but an input that was already held when they came back does not.
    headset.SetHandInputs(kLeft, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    NextFrame(&headset);
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllers', 'on')").find("on"), std::string::npos);
    NextFrame(&headset);
    EXPECT_EQ(RecenterCalls(), 0) << "a click held while they were off is not a new press";
    Release(kLeft);
    Tap(kLeft, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    EXPECT_EQ(RecenterCalls(), 1);
}

TEST_F(ControllerButtonMappingTest, AControllerThatComesBackTrackedWithAnInputHeldDoesNotFireIt) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    headset.SetHandInputs(kRight, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    headset.SetHandTracked(kRight, false);
    NextFrame(&headset);
    headset.SetHandTracked(kRight, true);
    NextFrame(&headset);
    NextFrame(&headset);
    EXPECT_EQ(RecenterCalls(), 0);
    Release(kRight);
    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    EXPECT_EQ(RecenterCalls(), 1);
}

// ---- Changing, removing and saving the mappings ----

TEST_F(ControllerButtonMappingTest, AConsoleCommandChangesWhichInputTriggersAnAction) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    EXPECT_NE(EvalLua(L, "runCommand('vrControllerMap', 'recenter', 'buttonUpper')").find("buttonUpper"), std::string::npos);

    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    EXPECT_EQ(RecenterCalls(), 0) << "the thumbstick click no longer recenters";
    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::buttonUpper));
    EXPECT_EQ(RecenterCalls(), 1) << "the upper face button does, on the right hand";
    Tap(kLeft, Pressed(&FakeOpenXrControllers::Inputs::buttonUpper));
    EXPECT_EQ(RecenterCalls(), 2) << "and on the left hand";

    // The menu button can move to a face button too.
    const int hudBefore = std::stoi(EvalLua(L, "VRCompanion:getHudState()"));
    EvalLua(L, "runCommand('vrControllerMap', 'hudToggle', 'buttonLower')");
    Tap(kLeft, Pressed(&FakeOpenXrControllers::Inputs::menu));
    EXPECT_EQ(std::stoi(EvalLua(L, "VRCompanion:getHudState()")), hudBefore) << "the menu button no longer toggles the HUD";
    Tap(kLeft, Pressed(&FakeOpenXrControllers::Inputs::buttonLower));
    EXPECT_EQ(std::stoi(EvalLua(L, "VRCompanion:getHudState()")), (hudBefore + 1) % 3);
}

TEST_F(ControllerButtonMappingTest, AConsoleCommandRemovesAMappingAndDefaultBringsItBack) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    EvalLua(L, "runCommand('vrControllerMap', 'snapTurn', 'none')");
    EvalLua(L, "runCommand('vrControllerMap', 'recenter', 'none')");
    EvalLua(L, "runCommand('vrControllerMap', 'hudToggle', 'none')");
    const int hudBefore = std::stoi(EvalLua(L, "VRCompanion:getHudState()"));

    Tap(kRight, Thumb(1.0f));
    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    Tap(kLeft, Pressed(&FakeOpenXrControllers::Inputs::menu));
    EXPECT_NEAR(CabYaw(), 0.0, 1e-6);
    EXPECT_EQ(RecenterCalls(), 0);
    EXPECT_EQ(std::stoi(EvalLua(L, "VRCompanion:getHudState()")), hudBefore);

    EvalLua(L, "runCommand('vrControllerMap', 'recenter', 'default')");
    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    EXPECT_EQ(RecenterCalls(), 1);

    EvalLua(L, "runCommand('vrControllerMap', 'reset')");
    AdvanceGameTime(300);
    Tap(kRight, Thumb(1.0f));
    EXPECT_NEAR(CabYaw(), -3.14159265 / 2.0, 1e-4) << "reset restores the Snap Turn too";
}

TEST_F(ControllerButtonMappingTest, TheCommandReportsTheMappingsAndRefusesInputsItDoesNotKnow) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    const std::string report = EvalLua(L, "runCommand('vrControllerMap')");
    EXPECT_NE(report.find("snapTurn=thumbX"), std::string::npos) << report;
    EXPECT_NE(report.find("recenter=thumbClick"), std::string::npos) << report;
    EXPECT_NE(report.find("hudToggle=menu"), std::string::npos) << report;

    // The trigger belongs to pointing and clicking, and an action name must be one of the three.
    EXPECT_NE(EvalLua(L, "runCommand('vrControllerMap', 'recenter', 'trigger')").find("Usage"), std::string::npos);
    EXPECT_NE(EvalLua(L, "runCommand('vrControllerMap', 'jump', 'menu')").find("Usage"), std::string::npos);
    EXPECT_NE(EvalLua(L, "runCommand('vrControllerMap', 'snapTurn', 'menu')").find("Usage"), std::string::npos);
    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    EXPECT_EQ(RecenterCalls(), 1) << "a refused command changes nothing";
}

TEST_F(ControllerButtonMappingTest, TheMappingsAreSavedAndComeBackInTheNextGameSession) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    EvalLua(L, "runCommand('vrControllerMap', 'recenter', 'buttonLower')");
    EvalLua(L, "runCommand('vrControllerMap', 'hudToggle', 'none')");
    ASSERT_NO_FATAL_FAILURE(RestartGameSession());

    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::thumbClick));
    EXPECT_EQ(RecenterCalls(), 0);
    Tap(kRight, Pressed(&FakeOpenXrControllers::Inputs::buttonLower));
    EXPECT_EQ(RecenterCalls(), 1) << "the changed mapping came back";
    const int hudBefore = std::stoi(EvalLua(L, "VRCompanion:getHudState()"));
    Tap(kLeft, Pressed(&FakeOpenXrControllers::Inputs::menu));
    EXPECT_EQ(std::stoi(EvalLua(L, "VRCompanion:getHudState()")), hudBefore) << "the removed mapping stayed removed";
    Tap(kRight, Thumb(1.0f));
    EXPECT_NEAR(CabYaw(), -3.14159265 / 2.0, 1e-4) << "the untouched mapping is still the default";
}

TEST_F(ControllerButtonMappingTest, ASavedMappingThatMakesNoSenseIsIgnored) {
    ASSERT_NO_FATAL_FAILURE(StartMod("_G.savedXml = { ['vrSettings.controllerMap.recenter'] = 'trigger', "
                                     "['vrSettings.controllerMap.hudToggle'] = 'nonsense' }"));
    EXPECT_NE(EvalLua(L, "runCommand('vrControllerMap')").find("recenter=thumbClick"), std::string::npos);
    EXPECT_NE(EvalLua(L, "runCommand('vrControllerMap')").find("hudToggle=menu"), std::string::npos);
}
