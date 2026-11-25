#include "wheel_grab_test_support.h"
#include <string>

namespace {

using namespace wheel_grab_test;

// Values no Wheel Grab call ever has, so the calls of other devices can be told apart in the record.
constexpr const char* kKeyboardValue = "0.77";
constexpr const char* kGamepadValue = "0.66";
constexpr const char* kPhoneTiltValue = "0.55";

void StartMod(lua_State* L, fs25vr::VRBridge& bridge, const std::string& variant = "ok", const std::string& equipOptions = "") {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(RunLuauChunk(L, StubChunk("scene_graph"), "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("steering_vehicle"), "=steeringVehicle"), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("console"), "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, ("StubSteer.install('" + variant + "')").c_str(), "=installGame"), 0);
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    const std::string scene =
        "local cab = StubScene.cab({})\n"
        "StubSteer.equip(cab.vehicle, { parent = cab.parent, " + equipOptions + " })\n"
        "_G.cab = cab\n";
    ASSERT_EQ(RunLuauChunk(L, scene.c_str(), "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
}

int CallsWithValue(lua_State* L, const std::string& value) {
    return static_cast<int>(Number(L, "(function() local n = 0 for _, call in ipairs(StubSteer.calls) do "
                                      "if math.abs(call.value - " + value + ") < 1e-9 then n = n + 1 end end return n end)()"));
}

int CallsThatReachedTheVehicle(lua_State* L) {
    return static_cast<int>(Number(L, "#StubSteer.calls"));
}

void ClearCalls(lua_State* L) {
    EXPECT_EQ(RunLuauChunk(L, "StubSteer.calls = {}", "=clearCalls"), 0);
}

void OtherDevicesSteer(lua_State* L) {
    EXPECT_EQ(RunLuauChunk(L, (std::string("StubSteer.keyboard(cab.vehicle, ") + kKeyboardValue + ")\n" +
                               "StubSteer.gamepad(cab.vehicle, " + kGamepadValue + ")\n" +
                               "StubSteer.wheelPeripheral(cab.vehicle, " + kPhoneTiltValue + ")\n").c_str(), "=otherDevices"), 0);
}

}

#define HOLD_THE_WHEEL() \
    do { \
        GRAB(kRight, RimPoint(L, kRimRadius, 0.0)); \
        TURN_HAND_TO(kRight, 0.2); \
        TURN_HAND_TO(kRight, 0.4); \
        ASSERT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "true") << "the Steering Wheel is held"; \
    } while (0)

#define LET_GO_OF_THE_WHEEL() \
    do { \
        headset.SetHandInputs(kRight, Grip(0.0f)); \
        FRAMES(1); \
        ASSERT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false") << "the Steering Wheel is let go of"; \
    } while (0)


#define HANDS_WIN_WHILE_HELD(equipOptions) \
    do { \
        FakeTrackedHeadset headset; \
        fs25vr::VRBridge bridge(headset.manager, nullptr); \
        ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", (equipOptions))); \
        SetGameTime(10000); \
        BRING_HAND(kRight, RimPoint(L, kRimRadius, 0.0)); \
        OtherDevicesSteer(L); \
        EXPECT_EQ(CallsThatReachedTheVehicle(L), 3) << "with nothing held all three devices steer"; \
        \
        HOLD_THE_WHEEL(); \
        ClearCalls(L); \
        for (int frame = 0; frame < 3; ++frame) { \
            OtherDevicesSteer(L); \
            FRAMES(1); \
        } \
        EXPECT_EQ(CallsWithValue(L, kKeyboardValue), 0) << "a keyboard call was dropped"; \
        EXPECT_EQ(CallsWithValue(L, kGamepadValue), 0) << "a gamepad call was dropped"; \
        EXPECT_EQ(CallsWithValue(L, kPhoneTiltValue), 0) \
            << "a call in the Wheel Peripheral's category was dropped too: only Wheel Grab's own call passes"; \
        EXPECT_EQ(CallsThatReachedTheVehicle(L), 3) << "and Wheel Grab's own call reached the vehicle every frame"; \
        \
        LET_GO_OF_THE_WHEEL(); \
        ClearCalls(L); \
        OtherDevicesSteer(L); \
        EXPECT_EQ(CallsWithValue(L, kKeyboardValue), 1) << "the keyboard steers again after the release"; \
        EXPECT_EQ(CallsWithValue(L, kGamepadValue), 1) << "and the gamepad"; \
        EXPECT_EQ(CallsWithValue(L, kPhoneTiltValue), 1) << "and the game's own tilt steering"; \
    } while (0)

TEST_F(CompanionModTest, WhileTheSteeringWheelIsHeldOtherDevicesCallsAreDroppedAndWheelGrabsPassUntilItIsLetGo) {
    HANDS_WIN_WHILE_HELD("");
}

TEST_F(CompanionModTest, TheVehicleHoldingTheOriginalFunctionItCopiedIsWrappedTooWhileTheSteeringWheelIsHeld) {
    HANDS_WIN_WHILE_HELD("copyOfOriginal = true");
}

TEST_F(CompanionModTest, CallsPassThroughUnchangedWithNothingHeld) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    BRING_HAND(kRight, RimPoint(L, kRimRadius, 0.0));
    ASSERT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false");

    OtherDevicesSteer(L);

    EXPECT_EQ(CallsWithValue(L, kKeyboardValue), 1);
    EXPECT_EQ(CallsWithValue(L, kGamepadValue), 1);
    EXPECT_EQ(CallsWithValue(L, kPhoneTiltValue), 1);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local keyboard = StubSteer.calls[1]
        assert(keyboard.analog == false and keyboard.category == InputDevice.CATEGORY.KEYBOARD_MOUSE,
            "the keyboard's call arrived with its own arguments")
        local gamepad = StubSteer.calls[2]
        assert(gamepad.analog == true and gamepad.category == InputDevice.CATEGORY.GAMEPAD,
            "the gamepad's call arrived with its own arguments")
    )luau", "=arguments"), 0);
}

TEST_F(CompanionModTest, WithVrWheelGrabOffEveryCallPassesThroughEvenFromAHandSqueezingTheRim) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "copyOfOriginal = true"));
    SetGameTime(10000);
    EvalLua(L, "runCommand('vrWheelGrab', 'off')");

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false");
    OtherDevicesSteer(L);

    EXPECT_EQ(CallsThatReachedTheVehicle(L), 3) << "all three calls reached the vehicle";
}

TEST_F(CompanionModTest, SwitchingWheelGrabOffWhileHoldingLetsOtherDevicesSteerAtOnce) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "copyOfOriginal = true"));
    SetGameTime(10000);
    HOLD_THE_WHEEL();

    EvalLua(L, "runCommand('vrWheelGrab', 'off')");
    ClearCalls(L);
    OtherDevicesSteer(L);

    EXPECT_EQ(CallsThatReachedTheVehicle(L), 3);
}

TEST_F(CompanionModTest, LettingGoGivesTheVehicleBackItsOwnFunctionAndNeverUndoesALaterWrapOfAnotherMod) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "copyOfOriginal = true"));
    SetGameTime(10000);
    const char* isOriginal = "cab.vehicle.setSteeringInput == StubSteer.original";
    ASSERT_EQ(EvalLua(L, isOriginal), "true") << "the vehicle starts with its own copy";
    HOLD_THE_WHEEL();
    EXPECT_EQ(EvalLua(L, isOriginal), "false") << "the Companion Mod stands in front of it while the Steering Wheel is held";
    LET_GO_OF_THE_WHEEL();
    EXPECT_EQ(EvalLua(L, isOriginal), "true") << "the vehicle's own copy is back after letting go";

    HOLD_THE_WHEEL();
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local inner = cab.vehicle.setSteeringInput
        _G.theirCalls = 0
        _G.theirWrap = function(...) _G.theirCalls = _G.theirCalls + 1 return inner(...) end
        cab.vehicle.setSteeringInput = _G.theirWrap
    )luau", "=anotherModWraps"), 0);
    LET_GO_OF_THE_WHEEL();
    EXPECT_EQ(EvalLua(L, "cab.vehicle.setSteeringInput == theirWrap"), "true") << "the other mod's wrap is still the vehicle's function";
    ClearCalls(L);
    OtherDevicesSteer(L);
    EXPECT_EQ(EvalLua(L, "theirCalls"), "3") << "and it still sees every call";
    EXPECT_EQ(CallsThatReachedTheVehicle(L), 3) << "which now reach the vehicle through it";
}

TEST_F(CompanionModTest, LeavingTheVehicleWhileHoldingGivesItsFunctionBackAndTheNextCallReachesIt) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "copyOfOriginal = true"));
    SetGameTime(10000);
    HOLD_THE_WHEEL();

    ASSERT_EQ(RunLuauChunk(L, "g_currentMission.controlledVehicle = nil; VRCompanion:onLeaveVehicle(cab.vehicle)", "=leave"), 0);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "cab.vehicle.setSteeringInput == StubSteer.original"), "true") << "the vehicle has its own copy back";
    ClearCalls(L);
    OtherDevicesSteer(L);
    EXPECT_EQ(CallsThatReachedTheVehicle(L), 3);
}

TEST_F(CompanionModTest, UnloadingTheMapTakesTheWrapOffTheClassAndTheVehicle) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "copyOfOriginal = true"));
    SetGameTime(10000);
    HOLD_THE_WHEEL();
    ASSERT_EQ(EvalLua(L, "Drivable.setSteeringInput == StubSteer.original"), "false") << "the class is wrapped while the map is loaded";

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);

    EXPECT_EQ(EvalLua(L, "Drivable.setSteeringInput == StubSteer.original"), "true") << "the class has its own function back";
    EXPECT_EQ(EvalLua(L, "cab.vehicle.setSteeringInput == StubSteer.original"), "true") << "and so has the vehicle";
}

#define WHEEL_GRAB_GIVES_UP_WHEN_STEERING_INPUT_IS(variant) \
    do { \
        FakeTrackedHeadset headset; \
        fs25vr::VRBridge bridge(headset.manager, nullptr); \
        ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, (variant))); \
        SetGameTime(10000); \
        \
        TRY_TO_STEER(kRight); \
        \
        EXPECT_EQ(EvalLua(L, "VRCompanion:isWheelGrabAvailable()"), "false") << "Wheel Grab is unavailable"; \
        EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false") << "and the Steering Wheel could not be taken hold of"; \
        EXPECT_EQ(CallsThatReachedTheVehicle(L), 0) << "Wheel Grab sent the vehicle no steering"; \
        EXPECT_EQ(CountLogLines("[Warning] Wheel Grab"), 1) << "one warning says why"; \
        TRY_TO_STEER(kRight); \
        EXPECT_EQ(CountLogLines("[Warning] Wheel Grab"), 1) << "and it is not repeated"; \
        \
        ASSERT_EQ(RunLuauChunk(L, "StubSteer.keyboard(cab.vehicle, 0.77); StubSteer.gamepad(cab.vehicle, 0.66)", "=otherDevices"), 0); \
        EXPECT_EQ(CallsWithValue(L, kKeyboardValue), 1) << "the keyboard still steers"; \
        EXPECT_EQ(CallsWithValue(L, kGamepadValue), 1) << "and the gamepad"; \
        EXPECT_NE(EvalLua(L, "VRCompanion:getWheelGrabDiagnostics()").find("steering input wrap: gave up"), std::string::npos) \
            << "the diagnostics say the wrap is not installed: " << EvalLua(L, "VRCompanion:getWheelGrabDiagnostics()"); \
    } while (0)

TEST_F(CompanionModTest, WithSetSteeringInputMissingWheelGrabIsUnavailableWarnsOnceAndOtherDevicesStillSteer) {
    WHEEL_GRAB_GIVES_UP_WHEN_STEERING_INPUT_IS("missing");
}

TEST_F(CompanionModTest, WithSetSteeringInputReshapedWheelGrabIsUnavailableWarnsOnceAndOtherDevicesStillSteer) {
    WHEEL_GRAB_GIVES_UP_WHEN_STEERING_INPUT_IS("reshaped");
}

namespace {
int ExactLogLines(const std::string& line) {
    int count = 0;
    for (const auto& logged : s_capturedLogs) {
        if (logged == line) ++count;
    }
    return count;
}

std::string WheelGrabWarning(const std::string& problem) {
    return "[FS25_VR] [Warning] Wheel Grab disabled: " + problem
        + ". Steering from the keyboard, a gamepad or a Wheel Peripheral is unchanged.";
}
}

TEST_F(CompanionModTest, TheWheelGrabWarningSaysTheClassSteeringFunctionIsMissing) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "missing"));
    SetGameTime(10000);
    TRY_TO_STEER(kRight);
    EXPECT_EQ(ExactLogLines(WheelGrabWarning("Drivable.setSteeringInput: it is missing")), 1);
}

TEST_F(CompanionModTest, TheWheelGrabWarningSaysHowTheClassSteeringFunctionIsReshaped) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "reshaped"));
    SetGameTime(10000);
    TRY_TO_STEER(kRight);
    EXPECT_EQ(ExactLogLines(WheelGrabWarning(
                  "Drivable.setSteeringInput: it takes 2 parameters, not (vehicle, value, isAnalog, deviceCategory)")), 1);
}

TEST_F(CompanionModTest, TheWheelGrabWarningSaysHowTheVehiclesOwnCopyIsReshaped) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    ASSERT_EQ(RunLuauChunk(L, "cab.vehicle.setSteeringInput = function(vehicle) end", "=reshapedCopy"), 0);
    TRY_TO_STEER(kRight);
    EXPECT_EQ(ExactLogLines(WheelGrabWarning(
                  "the vehicle's setSteeringInput: it takes 1 parameters, not (vehicle, value, isAnalog, deviceCategory)")), 1);
}

TEST_F(CompanionModTest, TheWheelGrabWarningOfAGameWithoutDrivableSaysSo) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    ASSERT_EQ(RunLuauChunk(L, "_G.Drivable = nil", "=noDrivable"), 0);
    TRY_TO_STEER(kRight);
    EXPECT_EQ(ExactLogLines(WheelGrabWarning("Drivable is missing")), 1);
}

TEST_F(CompanionModTest, WhenWheelGrabsOwnSteeringCallKeepsRaisingWheelGrabGivesUpWithOneWarningAndOtherDevicesStillSteer) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "raises"));
    SetGameTime(10000);

    TRY_TO_STEER(kRight);

    EXPECT_EQ(EvalLua(L, "VRCompanion:isWheelGrabAvailable()"), "false") << "Wheel Grab is unavailable";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false") << "and the Steering Wheel is not held";
    EXPECT_EQ(CountLogLines("[Warning] Wheel Grab disabled"), 1) << "one warning says so";
    EXPECT_EQ(CountLogLines("[Warning] Wheel Grab disabled: setSteeringInput raised"), 1) << "and says what went wrong";
    TRY_TO_STEER(kRight);
    EXPECT_EQ(CountLogLines("[Warning] Wheel Grab"), 1) << "and it is not repeated";

    ASSERT_EQ(RunLuauChunk(L, "StubSteer.keyboard(cab.vehicle, 0.77); StubSteer.gamepad(cab.vehicle, 0.66)", "=otherDevices"), 0);
    EXPECT_EQ(CallsWithValue(L, kKeyboardValue), 1) << "the keyboard still steers";
    EXPECT_EQ(CallsWithValue(L, kGamepadValue), 1) << "and the gamepad";
    EXPECT_EQ(EvalLua(L, "Drivable.setSteeringInput == StubSteer.original"), "true") << "the wrap is lifted off the class";
    EXPECT_NE(EvalLua(L, "VRCompanion:getWheelGrabDiagnostics()").find("steering input wrap: gave up"), std::string::npos)
        << EvalLua(L, "VRCompanion:getWheelGrabDiagnostics()");
}

TEST_F(CompanionModTest, ASteeringCallThatRaisesNowAndThenDoesNotSwitchWheelGrabOff) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    HOLD_THE_WHEEL();

    ASSERT_EQ(RunLuauChunk(L, "StubSteer.failNextWheelCalls(2)", "=failTwice"), 0);
    FRAMES(5);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "true") << "two failures in a row did not end the hold";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isWheelGrabAvailable()"), "true");
    EXPECT_EQ(CountLogLines("Wheel Grab disabled"), 0) << "and gave nothing up";
    ClearCalls(L);
    FRAMES(3);
    EXPECT_EQ(CallsThatReachedTheVehicle(L), 3) << "Wheel Grab's own call reached the vehicle every frame again";

    // A failure on two updates, a success, then two more: never three in a row.
    for (int round = 0; round < 3; ++round) {
        ASSERT_EQ(RunLuauChunk(L, "StubSteer.failNextWheelCalls(2)", "=failTwiceMore"), 0);
        FRAMES(3);
    }
    EXPECT_EQ(CountLogLines("Wheel Grab disabled"), 0) << "failures that never come three updates in a row gave nothing up";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "true");
}
TEST_F(CompanionModTest, VrWheelGrabDiagnosticsReportsThatTheWrapIsInstalled) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    FRAMES(1);

    const std::string reply = EvalLua(L, "runCommand('vrWheelGrabDiagnostics')");

    EXPECT_NE(reply.find("steering input wrap: installed"), std::string::npos) << reply;
    EXPECT_EQ(CountLogLines("[FS25_VR] steering input wrap: installed"), 1) << "and the log lists it too";
}

TEST_F(CompanionModTest, TheWrapIsInstalledAgainWhenTheNextMapLoads) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, "ok", "copyOfOriginal = true"));
    SetGameTime(10000);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap(); VRCompanion:loadMap('y')", "=reloadMap"), 0);

    HOLD_THE_WHEEL();
    ClearCalls(L);
    OtherDevicesSteer(L);

    EXPECT_EQ(CallsWithValue(L, kKeyboardValue), 0) << "other devices are dropped while held, on the second map as on the first";
}
