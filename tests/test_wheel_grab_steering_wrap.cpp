// Wheel Grab, the hands win over other steering (spec #89, ticket #93, ADR-0027), seen from outside: while a hand holds
// the Steering Wheel, every steering call another device makes (the keyboard, a gamepad, a Wheel Peripheral, the game's
// own phone tilt steering) is dropped before it reaches the vehicle, Wheel Grab's own calls reach it, and everything
// passes again as soon as the hand lets go. The Companion Mod runs for real against the real VRBridge over
// FakeTrackedHeadset; the vehicle is the stub of companion_mod_stubs.h (StubSteer) that, like the game, COPIES
// Drivable.setSteeringInput onto the vehicle, so a wrap that only replaced the class function too late would not reach
// it. Every assertion is on the steering calls that reached the vehicle, the function the vehicle ends up with and the
// log, never on the mod's own state. Prior art: test_wheel_grab.cpp (#90) and Interactive Control's wrap of the Click
// Point functions (test_interactive_control_auto_on.cpp).
//
// The cab geometry is the one of test_wheel_grab.cpp: a hand "at the rim" is 0.18 m from the Steering Wheel's axis.

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include <string>

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kRight = 1;

constexpr double kRimRadius = 0.18;  // m from the Steering Wheel's axis: inside the ring, on the rim

// Values no Wheel Grab call ever has, so the calls of other devices can be told apart in the record.
constexpr const char* kKeyboardValue = "0.77";
constexpr const char* kGamepadValue = "0.66";
constexpr const char* kPhoneTiltValue = "0.55";

struct Point {
    double x, y, z;
};

HeadPose HandAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

// A hand at a world position of the cab at the origin (the head is at world (0, 1.5, 0) facing +Z).
HeadPose HandAtWorld(const Point& p) {
    return HandAt(static_cast<float>(-p.x), static_cast<float>(p.y - 1.5), static_cast<float>(-p.z));
}

FakeOpenXrControllers::Inputs Grip(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.grip = value;
    return inputs;
}

// The engine's console globals the Companion Mod registers into.
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
    _G.Input = { MOUSE_BUTTON_NONE = 0, MOUSE_BUTTON_LEFT = 1 }
    _G.g_gui = {
        visible = false,
        getIsGuiVisible = function(self) return self.visible end,
        mouseEvent = function() end,
    }
)luau";

// The game before the Companion Mod loads, as it is in the game: the Drivable class is there when the mod's files load.
// `variant` is a StubSteer.install variant ("ok", "missing", "reshaped"); `equipOptions` are more StubSteer.equip
// options, e.g. "copyOfOriginal = true" for a vehicle whose copy was taken before the class was wrapped.
void StartMod(lua_State* L, fs25vr::VRBridge& bridge, const std::string& variant = "ok", const std::string& equipOptions = "") {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSteeringVehicle, "=steeringVehicle"), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, ("StubSteer.install('" + variant + "')").c_str(), "=installGame"), 0);
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    const std::string scene =
        "local cab = StubScene.cab({})\n"
        "StubSteer.equip(cab.vehicle, { parent = cab.parent, " + equipOptions + " })\n"
        "_G.cab = cab\n";
    ASSERT_EQ(RunLuauChunk(L, scene.c_str(), "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
}

// What the game does each frame before the Companion Mod's update.
void PreFrame(lua_State* L) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        if StubScene.updateCamera then StubScene.updateCamera() end
        StubSteer.tick(16.667)
    )luau", "=preFrame"), 0);
}

double Number(lua_State* L, const std::string& expression) {
    return std::stod(EvalLua(L, expression));
}

// The world position of the point on the Steering Wheel `radius` from its axis when the wheel is turned to `angle`.
Point RimPoint(lua_State* L, double radius, double angle) {
    const std::string args = "(cab.vehicle, " + std::to_string(radius) + ", " + std::to_string(angle) + ")";
    return { Number(L, "select(1, StubSteer.rimPoint" + args + ")"), Number(L, "select(2, StubSteer.rimPoint" + args + ")"),
             Number(L, "select(3, StubSteer.rimPoint" + args + ")") };
}

// How many steering calls with this value reached the vehicle (so far, or since the record was last cleared).
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

// All three other devices steer, once each.
void OtherDevicesSteer(lua_State* L) {
    EXPECT_EQ(RunLuauChunk(L, (std::string("StubSteer.keyboard(cab.vehicle, ") + kKeyboardValue + ")\n" +
                               "StubSteer.gamepad(cab.vehicle, " + kGamepadValue + ")\n" +
                               // the game's own phone tilt steering and the HUD slider use the Wheel Peripheral's category too
                               "StubSteer.wheelPeripheral(cab.vehicle, " + kPhoneTiltValue + ")\n").c_str(), "=otherDevices"), 0);
}

int CountLogLines(const std::string& text) {
    int count = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find(text) != std::string::npos) ++count;
    }
    return count;
}

} // namespace

#define FRAMES(count) \
    for (int frame_ = 0; frame_ < (count); ++frame_) { PreFrame(L); NextFrame(&headset); }

// A Motion Controller put into use at a spot, then moved to `rim` (a hand counts as in use once it has moved).
#define BRING_HAND(hand, rim) \
    do { \
        headset.SetHandPose((hand), HandAtWorld(Point{ 0.6, 1.0, 0.2 })); \
        FRAMES(1); \
        headset.SetHandPose((hand), HandAtWorld(rim)); \
        FRAMES(1); \
    } while (0)

// The hand brought to the rim and the grip squeezed there.
#define GRAB(hand, rim) \
    do { \
        BRING_HAND((hand), (rim)); \
        headset.SetHandInputs((hand), Grip(1.0f)); \
        FRAMES(1); \
    } while (0)

// The hand turned about the wheel's axis to where the rim point of a wheel turned to `angle` is.
#define TURN_HAND_TO(hand, angle) \
    do { \
        headset.SetHandPose((hand), HandAtWorld(RimPoint(L, kRimRadius, (angle)))); \
        FRAMES(1); \
    } while (0)

// The Steering Wheel held and turned a little, so Wheel Grab is steering.
#define HOLD_THE_WHEEL() \
    do { \
        GRAB(kRight, RimPoint(L, kRimRadius, 0.0)); \
        TURN_HAND_TO(kRight, 0.2); \
        TURN_HAND_TO(kRight, 0.4); \
        ASSERT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "true") << "the Steering Wheel is held"; \
    } while (0)

// The Steering Wheel held and turned a little, so Wheel Grab is steering.
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

// The hand brought to the rim, the grip squeezed and the hand turned a little, then the grip let go again: what a
// player does to steer, whatever comes of it.
#define TRY_TO_STEER() \
    do { \
        GRAB(kRight, RimPoint(L, kRimRadius, 0.0)); \
        for (int step_ = 1; step_ <= 4; ++step_) { \
            TURN_HAND_TO(kRight, 0.2 * step_); \
        } \
        FRAMES(5); \
        headset.SetHandInputs(kRight, Grip(0.0f)); \
        FRAMES(2); \
    } while (0)

// The whole story of the acceptance criteria for a vehicle set up with `equipOptions`: other devices steer before the
// grab, are dropped while the Steering Wheel is held (Wheel Grab's own call reaches the vehicle all the same) and steer
// again after it is let go.
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
    // The Companion Mod wrapped the class at file load, before the game copied the function onto the vehicle.
    HANDS_WIN_WHILE_HELD("");
}

TEST_F(CompanionModTest, TheVehicleHoldingTheOriginalFunctionItCopiedIsWrappedTooWhileTheSteeringWheelIsHeld) {
    // The game's copy on the vehicle was taken before anything wrapped the class, so only a wrap of the vehicle's own
    // function reaches the calls of the other devices.
    HANDS_WIN_WHILE_HELD("copyOfOriginal = true");
}

TEST_F(CompanionModTest, CallsPassThroughUnchangedWithNothingHeld) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    // A hand at the rim with the grip not squeezed holds nothing.
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
    OtherDevicesSteer(L);  // before the next update: the setting alone lifts the wrap

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

    // Another mod wraps the vehicle's function while the Steering Wheel is held; letting go leaves that wrap alone.
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

// A game whose steering function is not what the wrap expects: Wheel Grab is unavailable, the log says so once and
// the other devices go on steering.
#define WHEEL_GRAB_GIVES_UP_WHEN_STEERING_INPUT_IS(variant) \
    do { \
        FakeTrackedHeadset headset; \
        fs25vr::VRBridge bridge(headset.manager, nullptr); \
        ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, (variant))); \
        SetGameTime(10000); \
        \
        TRY_TO_STEER(); \
        \
        EXPECT_EQ(EvalLua(L, "VRCompanion:isWheelGrabAvailable()"), "false") << "Wheel Grab is unavailable"; \
        EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false") << "and the Steering Wheel could not be taken hold of"; \
        EXPECT_EQ(CallsThatReachedTheVehicle(L), 0) << "Wheel Grab sent the vehicle no steering"; \
        EXPECT_EQ(CountLogLines("[Warning] Wheel Grab"), 1) << "one warning says why"; \
        TRY_TO_STEER(); \
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
