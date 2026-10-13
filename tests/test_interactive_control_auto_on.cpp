// Interactive Control switches on when the Pointer Hand is picked up in a cab (spec #71, ticket #77,
// ADR-0026), seen from outside: the real Companion Mod runs against the real VRBridge over
// FakeTrackedHeadset, with a stand-in for the Interactive Control mod whose vehicle records every write
// of its synced state (companion_mod_stubs.h). The test moves the fake Motion Controllers and the game
// clock and asserts what Interactive Control's state became and when it was written.

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"

namespace {

using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

HeadPose HandAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

// A cab at the world origin whose vehicle has Interactive Control's state (`initialState`), an
// Interactive Control release the Companion Mod supports, and the mod started.
void StartMod(lua_State* L, fs25vr::VRBridge& bridge, bool initialState, const char* icVariant = "ok") {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubInteractiveControl, "=interactiveControl"), 0);
    if (icVariant != nullptr) {
        ASSERT_EQ(RunLuauChunk(L, std::string("StubIC.install('") + icVariant + "')", "=installIc"), 0);
    }
    ASSERT_EQ(RunLuauChunk(L, "StubScene.cab({})", "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, std::string("StubIC.equip(g_currentMission.controlledVehicle, ")
                                  + (initialState ? "true" : "false") + ")", "=equip"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
}

// What the game does each frame before the Companion Mod's update: the camera update and Interactive
// Control's own update of its Click Points.
void PreFrame(lua_State* L) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        if StubScene.updateCamera then StubScene.updateCamera() end
        if StubIC.tick then StubIC.tick() end
    )luau", "=preFrame"), 0);
}

} // namespace

#define FRAMES(count) \
    for (int frame_ = 0; frame_ < (count); ++frame_) { PreFrame(L); NextFrame(&headset); }

// The Pointer Hand is in use once its controller has moved: a controller held at (x, y, z) that was
// moved there just now.
#define PICK_UP(hand) \
    do { \
        headset.SetHandPose((hand), HandAt(0.2f, 0.0f, -0.3f)); \
        FRAMES(1); \
        headset.SetHandPose((hand), HandAt(0.3f, 0.0f, -0.3f)); \
        FRAMES(1); \
    } while (0)

// The Pointer Hand stops being in use: the controller lies still for longer than the in-use time.
#define PUT_DOWN() \
    do { \
        AdvanceGameTime(3500); \
        FRAMES(2); \
    } while (0)

TEST_F(CompanionModTest, PickingUpThePointerHandInACabSwitchesInteractiveControlOnForThatVehicle) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false));
    SetGameTime(10000);
    headset.SetHandPose(kRight, HandAt(0.3f, 0.0f, -0.3f));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "false")
        << "a hand that has not moved is not in use";
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "0");

    PICK_UP(kRight);
    EXPECT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "true");
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "1");
    EXPECT_EQ(EvalLua(L, "StubIC.stateWrites[1].state"), "true");
}

TEST_F(CompanionModTest, PuttingThePointerHandDownRestoresOffAndLeavesOnAlone) {
    {
        FakeTrackedHeadset headset;
        fs25vr::VRBridge bridge(headset.manager, nullptr);
        ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false));
        SetGameTime(10000);
        PICK_UP(kRight);
        ASSERT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "true");
        PUT_DOWN();
        EXPECT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "false");
        EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "2") << "on at pick-up, off at put-down, nothing else";
        EXPECT_EQ(EvalLua(L, "StubIC.stateWrites[2].state"), "false");

        // Picking it up again is a new session: on, and off again.
        PICK_UP(kRight);
        EXPECT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "true");
        PUT_DOWN();
        EXPECT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "false");
        EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "4");
    }
}

TEST_F(CompanionModTest, InteractiveControlThatWasAlreadyOnStaysOnAndIsNeverWritten) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, true));
    SetGameTime(10000);
    PICK_UP(kRight);
    FRAMES(5);
    PUT_DOWN();
    EXPECT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "true");
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "0");
}

TEST_F(CompanionModTest, TheStateIsWrittenOnlyAtThePickUpAndPutDownNeverPerFrame) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false));
    SetGameTime(10000);
    PICK_UP(kRight);
    ASSERT_EQ(EvalLua(L, "#StubIC.stateWrites"), "1");
    // The hand keeps moving, pulls the trigger and rests, for a long while, but stays in use.
    for (int i = 0; i < 40; ++i) {
        headset.SetHandPose(kRight, HandAt(0.3f + 0.02f * float(i % 2), 0.0f, -0.3f));
        fs25vr::FakeOpenXrControllers::Inputs inputs;
        inputs.trigger = (i % 3 == 0) ? 1.0f : 0.0f;
        headset.SetHandInputs(kRight, inputs);
        AdvanceGameTime(50);
        FRAMES(1);
    }
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "1") << "no write between the transitions";
    EXPECT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "true");
}

TEST_F(CompanionModTest, LeavingTheVehicleWhileThePointerHandIsInUseRestoresThatVehiclesState) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false));
    SetGameTime(10000);
    ASSERT_EQ(RunLuauChunk(L, "cab = g_currentMission.controlledVehicle", "=remember"), 0);
    PICK_UP(kRight);
    ASSERT_EQ(EvalLua(L, "cab:isInteractiveControlActivated()"), "true");

    ASSERT_EQ(RunLuauChunk(L, "g_currentMission.controlledVehicle = nil; VRCompanion:onLeaveVehicle(cab)", "=leave"), 0);
    // The hand is still being moved: it is in use, but there is no cab to switch on.
    headset.SetHandPose(kRight, HandAt(0.2f, 0.0f, -0.3f));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "cab:isInteractiveControlActivated()"), "false") << "that vehicle's previous state is back";
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "2");

    // Back in the same cab with the hand still in use: switched on again, restored at the next put-down.
    ASSERT_EQ(RunLuauChunk(L, "g_currentMission.controlledVehicle = cab; VRCompanion:onEnterVehicle(cab, true, nil)", "=enter"), 0);
    headset.SetHandPose(kRight, HandAt(0.3f, 0.0f, -0.3f));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "cab:isInteractiveControlActivated()"), "true");
    PUT_DOWN();
    EXPECT_EQ(EvalLua(L, "cab:isInteractiveControlActivated()"), "false");
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "4");
}

TEST_F(CompanionModTest, InteractiveControlsOwnAlwaysOnSettingWinsAndNothingIsWritten) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.stateSetting = 2", "=alwaysOn"), 0);
    SetGameTime(10000);
    PICK_UP(kRight);
    PUT_DOWN();
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "0");
}

TEST_F(CompanionModTest, InteractiveControlsOwnAlwaysOffSettingWinsAndNothingIsWritten) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.stateSetting = 3", "=alwaysOff"), 0);
    SetGameTime(10000);
    PICK_UP(kRight);
    PUT_DOWN();
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "0");
}

TEST_F(CompanionModTest, TheSettingChangingToAlwaysOnDuringUseMeansNothingIsRestored) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false));
    SetGameTime(10000);
    PICK_UP(kRight);
    ASSERT_EQ(EvalLua(L, "#StubIC.stateWrites"), "1");
    ASSERT_EQ(RunLuauChunk(L, "StubIC.stateSetting = 2", "=alwaysOn"), 0);
    PUT_DOWN();
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "1") << "Interactive Control's setting wins at the put-down too";
}

TEST_F(CompanionModTest, WithMotionControllersOffInteractiveControlIsNeverSwitchedAndTurningThemOffRestoresIt) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false));
    SetGameTime(10000);
    PICK_UP(kRight);
    ASSERT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "true");

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:setMotionControllersEnabled(false)", "=off"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "false")
        << "turning the feature off puts the state back";
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "2");

    PICK_UP(kRight);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "2") << "with the feature off a picked-up hand changes nothing";
}

TEST_F(CompanionModTest, WithoutInteractiveControlOrWithAMismatchedReleaseNothingIsSwitched) {
    {
        FakeTrackedHeadset headset;
        fs25vr::VRBridge bridge(headset.manager, nullptr);
        ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false, nullptr));
        SetGameTime(10000);
        PICK_UP(kRight);
        PUT_DOWN();
        EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "0");
    }
}

TEST_F(CompanionModTest, AMismatchedInteractiveControlReleaseSwitchesNothing) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false, "renamedUpdateClickable"));
    SetGameTime(10000);
    PICK_UP(kRight);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "0") << "Interactive Control support is off for that release";
}

TEST_F(CompanionModTest, ACameraSwitchThatMakesInteractiveControlSwitchItselfOffIsAnsweredOnceNotPerFrame) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false));
    SetGameTime(10000);
    PICK_UP(kRight);
    ASSERT_EQ(EvalLua(L, "#StubIC.stateWrites"), "1");

    // Interactive Control switches itself off when an inside camera becomes active.
    ASSERT_EQ(RunLuauChunk(L, "StubIC.changeCamera(g_currentMission.controlledVehicle, { isInside = true })", "=camera"), 0);
    ASSERT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "false");
    headset.SetHandPose(kRight, HandAt(0.2f, 0.0f, -0.3f));
    FRAMES(10);
    EXPECT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "true");
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "3") << "switched on once, after IC's own write";

    PUT_DOWN();
    EXPECT_EQ(EvalLua(L, "g_currentMission.controlledVehicle:isInteractiveControlActivated()"), "false");
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "4");
}

TEST_F(CompanionModTest, ControlledVehicleWithoutInteractiveControlClickPointsOrOnFootSwitchesNothing) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false));
    ASSERT_EQ(RunLuauChunk(L, "g_currentMission.controlledVehicle.spec_interactiveControl.interactiveControllers = {}", "=noPoints"), 0);
    SetGameTime(10000);
    PICK_UP(kRight);
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "0") << "a vehicle without Click Points";

    ASSERT_EQ(RunLuauChunk(L, "StubScene.onFoot({})", "=onFoot"), 0);
    PUT_DOWN();
    PICK_UP(kRight);
    EXPECT_EQ(EvalLua(L, "#StubIC.stateWrites"), "0") << "on foot there is no cab";
}
