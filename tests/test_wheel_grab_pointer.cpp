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

void StartMod(lua_State* L, fs25vr::VRBridge& bridge, bool interactiveControlOn = false) {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("scene_graph"), "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("steering_vehicle"), "=steeringVehicle"), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("interactive_control"), "=interactiveControl"), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("console"), "=consoleStubs"), 0);
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

}

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

    AdvanceGameTime(5000);  // the hand rests on the Steering Wheel for longer than a Motion Controller stays in use when it lies still
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true") << "a hand holding the Steering Wheel is still in use";
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
    EXPECT_EQ(PointerHand(L), "1") << "the hand that held the Steering Wheel is the Pointer Hand again";
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

    headset.SetHandInputs(kRight, [] { auto inputs = Grip(1.0f); inputs.trigger = 1.0f; return inputs; }());
    for (int step = 1; step <= 4; ++step) {
        TURN_HAND_TO(kRight, 0.2 * step);
    }
    FRAMES(30);
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
