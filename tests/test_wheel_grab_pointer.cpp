// A hand holding the Steering Wheel never points (spec #89, ticket #95, ADR-0026 and ADR-0027), seen from outside:
// the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with a cab vehicle that steers the
// way the game's does (StubSteer) and the stand-in for the Interactive Control mod (StubIC) on the same vehicle
// (companion_mod_stubs.h). The test grabs the Steering Wheel with fake Motion Controllers and asserts what the
// player would see: which hand is the Pointer Hand, whether a Pointer Ray exists, which Click Points were clicked,
// and what was written to Interactive Control's state. Prior art: test_pointer_hand_switch.cpp,
// test_interactive_control_auto_on.cpp and test_wheel_grab.cpp.
//
// The cab is at the world origin facing +Z with the head at the tracking space origin, so a point at world
// (x, y, z) is where a hand at tracking (-x, y - 1.5, -z) is. The Steering Wheel hangs in front of and below the
// head; a hand "at the rim" is 0.18 m from its axis. The right hand's Click Point is straight ahead of a fingertip
// held 0.3 m to the right of the head, the left hand's likewise on the left.

// What the game does each frame before the Companion Mod's update, besides the camera update and the vehicle's own
// update that acts on the steering input of the frame before: Interactive Control's update of its Click Points.
#define WHEEL_GRAB_PRE_FRAME_EXTRA_LUA "if StubIC.tick then StubIC.tick() end"

#include "wheel_grab_test_support.h"
#include <string>

namespace {

using namespace wheel_grab_test;

FakeOpenXrControllers::Inputs Trigger(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = value;
    return inputs;
}

// A cab whose vehicle both steers (StubSteer) and has Interactive Control's state (StubIC), with a Click Point
// straight ahead of each hand, and the mod started.
void StartMod(lua_State* L, fs25vr::VRBridge& bridge, bool interactiveControlOn = false) {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSteeringVehicle, "=steeringVehicle"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubInteractiveControl, "=interactiveControl"), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.install('ok'); StubSteer.install()", "=install"), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local cab = StubScene.cab({})
        StubSteer.equip(cab.vehicle, { parent = cab.parent })
        _G.cab = cab
    )luau", "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, std::string("StubIC.equip(cab.vehicle, ") + (interactiveControlOn ? "true" : "false") + ")",
                           "=equipIc"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('rightPoint', -0.3, 1.5, 2.3)
        StubIC.addPoint('leftPoint', 0.3, 1.5, 2.3)
    )luau", "=addPoints"), 0);
}

std::string PointerHand(lua_State* L) { return EvalLua(L, "VRCompanion:getPointerHand()"); }
std::string PointerRay(lua_State* L) { return EvalLua(L, "VRCompanion:getPointerRay()"); }
std::string Executed(lua_State* L) { return EvalLua(L, "table.concat(StubIC.executed, ',')"); }
std::string InteractiveControlState(lua_State* L) {
    return EvalLua(L, "cab.vehicle:isInteractiveControlActivated()");
}
std::string InteractiveControlWrites(lua_State* L) { return EvalLua(L, "#StubIC.stateWrites"); }

} // namespace

// The free hand lifted and held so that its index fingertip is 0.3 m to its side of the head and 0.3 m ahead, pointing
// straight ahead at its own Click Point.
#define POINT_AT_OWN_CLICK_POINT(hand) \
    do { \
        const float side_ = (hand) == kRight ? 0.3f : -0.3f; \
        headset.SetHandPointing((hand), HandAt(side_ - 0.1f, 0.0f, -0.3f)); \
        FRAMES(1); \
        headset.SetHandPointing((hand), HandAt(side_, 0.0f, -0.3f)); \
        FRAMES(3); \
    } while (0)


TEST_F(CompanionModTest, AHandThatGrabsTheSteeringWheelStopsBeingThePointerHandAndItsPointerRayDisappears) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    BRING_HAND(kRight, RimPoint(L, kRimRadius, 0.0));
    ASSERT_EQ(PointerHand(L), "1") << "the only hand in use points";
    ASSERT_NE(PointerRay(L), "nil") << "and casts a Pointer Ray";

    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true") << "the grab took";

    EXPECT_EQ(PointerHand(L), "nil") << "a hand holding the Steering Wheel does not point";
    EXPECT_EQ(PointerRay(L), "nil") << "and casts no Pointer Ray";
}

TEST_F(CompanionModTest, AHoldingHandNeverBecomesThePointerHandHoweverItMovesAndStaysInUse) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true");
    for (int step = 1; step <= 6; ++step) {
        TURN_HAND_TO(kRight, 0.15 * step);
        EXPECT_EQ(PointerHand(L), "nil") << "turning the Steering Wheel, step " << step;
        EXPECT_EQ(PointerRay(L), "nil") << "turning the Steering Wheel, step " << step;
    }
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true") << "still holding";

    AdvanceGameTime(5000);  // the hand rests on the wheel for longer than a Motion Controller stays in use when it lies still
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true") << "a hand holding the wheel is still in use";
    EXPECT_EQ(PointerHand(L), "nil");
}

TEST_F(CompanionModTest, AfterReleaseTheHandPointsAgainAsUsual) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    AdvanceGameTime(5000);
    FRAMES(3);
    ASSERT_EQ(PointerHand(L), "nil");

    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false") << "the grip was let go";
    EXPECT_EQ(PointerHand(L), "1") << "the hand that held the wheel is the Pointer Hand again";
    EXPECT_NE(PointerRay(L), "nil") << "and casts its Pointer Ray again";
}

TEST_F(CompanionModTest, TheFreeHandBecomesThePointerHandAndClicksAClickPointWhileTheOtherHandSteers) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    ASSERT_EQ(PointerHand(L), "nil");
    POINT_AT_OWN_CLICK_POINT(kLeft);

    EXPECT_EQ(PointerHand(L), "0") << "the free hand is the Pointer Hand";
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "leftPoint") << "its Pointer Ray selects its Click Point";

    headset.SetHandInputs(kLeft, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(Executed(L), "leftPoint") << "its trigger clicks it";

    // The other hand carries on steering all the while, and its trigger clicks nothing.
    headset.SetHandInputs(kRight, [] { auto inputs = Grip(1.0f); inputs.trigger = 1.0f; return inputs; }());
    for (int step = 1; step <= 4; ++step) {
        TURN_HAND_TO(kRight, 0.2 * step);
    }
    FRAMES(30);  // the game's analog steering catches up with the hand
    EXPECT_NEAR(Number(L, "StubSteer.wheelAngle(cab.vehicle)"), 0.8, 0.02) << "the Steering Wheel turned with the holding hand";
    EXPECT_EQ(PointerHand(L), "0");
    EXPECT_EQ(Executed(L), "leftPoint") << "the holding hand's trigger clicked nothing";
}

TEST_F(CompanionModTest, ThePointerHandThatGrabsTheSteeringWheelPassesThePointerHandToTheOtherHandInUse) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    BRING_HAND(kRight, RimPoint(L, kRimRadius, 0.0));
    POINT_AT_OWN_CLICK_POINT(kLeft);
    ASSERT_EQ(PointerHand(L), "1") << "the right hand has been the Pointer Hand since it was picked up";
    ASSERT_NE(PointerRay(L), "nil") << "it casts a Pointer Ray";
    ASSERT_NEAR(Number(L, "VRCompanion:getPointerRay().ox"), 0.175, 0.01) << "from the rim of the Steering Wheel";

    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(3);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true");
    EXPECT_EQ(PointerHand(L), "0") << "the left hand takes over without a trigger pull";
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "leftPoint");
    EXPECT_EQ(EvalLua(L, "string.format('%.2f', VRCompanion:getPointerRay().ox)"), "0.30") << "the ray is the left hand's";

    // After the release the right hand has to pull its trigger to take the Pointer Hand back, as with any two hands.
    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(3);
    EXPECT_EQ(PointerHand(L), "0") << "letting go does not take the Pointer Hand back";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(PointerHand(L), "1") << "a trigger pull on the released hand does";
}

TEST_F(CompanionModTest, GrabbingAndReleasingTheSteeringWheelWritesNoInteractiveControlState) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, false));
    SetGameTime(10000);

    BRING_HAND(kRight, RimPoint(L, kRimRadius, 0.0));
    ASSERT_EQ(InteractiveControlWrites(L), "1") << "picking up the Pointer Hand switched Interactive Control on";
    ASSERT_EQ(InteractiveControlState(L), "true");

    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(3);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "true");
    EXPECT_EQ(InteractiveControlWrites(L), "1") << "grabbing wrote nothing";
    EXPECT_EQ(InteractiveControlState(L), "true") << "Interactive Control stays as it was while the only hand holds on";

    AdvanceGameTime(5000);
    FRAMES(5);
    EXPECT_EQ(InteractiveControlWrites(L), "1") << "holding on for a long time wrote nothing";

    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(3);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false");
    EXPECT_EQ(InteractiveControlWrites(L), "1") << "releasing wrote nothing";
    EXPECT_EQ(InteractiveControlState(L), "true");
}

TEST_F(CompanionModTest, AnInteractiveControlThatWasOnAlreadyIsNeverWrittenByGrabbingAndReleasing) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, true));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    FRAMES(3);
    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(3);
    EXPECT_EQ(InteractiveControlWrites(L), "0") << "an Interactive Control that was on is left alone";
    EXPECT_EQ(InteractiveControlState(L), "true");
}
