#include "wheel_grab_test_support.h"
#include <string>

namespace {

using namespace wheel_grab_test;

// The Steering Wheel turns 540 degrees (three half turns) from straight ahead to full lock in these cabs.
constexpr double kFullLockAngle = 3.0 * kPi;

void StartMod(lua_State* L, fs25vr::VRBridge& bridge, const std::string& scene = SteerableCab()) {
    StartOptions options;
    options.consoleStubs = false;
    options.noPullAway = true;
    ASSERT_NO_FATAL_FAILURE(StartWheelGrabMod(L, bridge, scene, options));
}

double RoadWheelAngle(lua_State* L) { return Number(L, "StubSteer.roadWheelAngle(cab.vehicle)"); }
int SteeringCalls(lua_State* L) { return static_cast<int>(Number(L, "#StubSteer.calls")); }

}

#define TURN_CLOCKWISE_BY_SIX_TENTHS(hand) \
    do { \
        GRAB((hand), RimPoint(L, kRimRadius, 0.5)); \
        for (int step_ = 1; step_ <= 6; ++step_) { \
            TURN_HAND_TO((hand), 0.5 + 0.1 * step_); \
        } \
        FRAMES(30); \
    } while (0)


TEST_F(CompanionModTest, OnAnUnturnedSeatTurningTheSteeringWheelOneWayPointsTheWheelsOnTheGroundTheOtherWay) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    TURN_CLOCKWISE_BY_SIX_TENTHS(kRight);

    EXPECT_NEAR(WheelAngle(L), 0.6, 0.02) << "the Steering Wheel followed the hand";
    EXPECT_NEAR(RoadWheelAngle(L), 0.6 / kFullLockAngle, 0.002) << "the wheels on the ground steered by the same fraction of full lock";
    EXPECT_LT(Number(L, "StubSteer.calls[#StubSteer.calls].value"), 0.0) << "by a steering value of this sign";
}

TEST_F(CompanionModTest, OnATurnedSeatTheSteeringWheelISeeFollowsMyHandAndTheWheelsOnTheGroundSteerTheWayThatSteeringWheelWould) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("turnedSeat = true")));
    SetGameTime(10000);

    TURN_CLOCKWISE_BY_SIX_TENTHS(kRight);

    EXPECT_NEAR(WheelAngle(L), 0.6, 0.02) << "the Steering Wheel turned as far as the hand did, the same way";
    EXPECT_NEAR(RoadWheelAngle(L), -0.6 / kFullLockAngle, 0.002)
        << "the wheels on the ground steered by the same fraction of full lock, the other way from an unturned seat";
    EXPECT_LT(Number(L, "StubSteer.calls[#StubSteer.calls].value"), 0.0) << "the steering value has the sign an unturned seat's has";

    TURN_HAND_TO(kRight, 0.5 - 0.2);
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), -0.2, 0.02) << "and back past where it started, in the other direction";
    EXPECT_NEAR(RoadWheelAngle(L), 0.2 / kFullLockAngle, 0.002) << "with the wheels on the ground following";
}

TEST_F(CompanionModTest, OnATurnedSeatTheFullTurnOfTheSteeringWheelIsFullLock) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    // A tracked tractor's Steering Wheel: 120 degrees from straight ahead to full lock.
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("turnedSeat = true, rotationDeg = 120")));
    SetGameTime(10000);
    const double fullLock = 120.0 * 3.14159265358979 / 180.0;

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    for (int step = 1; step <= 12; ++step) {
        TURN_HAND_TO(kRight, 0.4 * step);
    }
    FRAMES(30);

    EXPECT_NEAR(WheelAngle(L), fullLock, 0.02) << "the Steering Wheel stopped at full lock in the direction of the hand";
    EXPECT_NEAR(RoadWheelAngle(L), -1.0, 0.002) << "with the wheels on the ground at full lock";
}

TEST_F(CompanionModTest, OnATurnedSeatWithASteeringWheelOfItsOwnTheHandTurnsThatSteeringWheel) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("turnedSeat = true, turnedSeatWheel = true")));
    SetGameTime(10000);

    TURN_CLOCKWISE_BY_SIX_TENTHS(kRight);

    EXPECT_NEAR(WheelAngle(L), 0.6, 0.02) << "the turned seat's own Steering Wheel followed the hand";
    EXPECT_NEAR(RoadWheelAngle(L), -0.6 / kFullLockAngle, 0.002) << "and steered the vehicle";
}

TEST_F(CompanionModTest, WhileAHiredHelperDrivesSqueezingTheGripAtTheRimSteersNothingAndLeavesTheHelperDriving) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("helper = true")));
    SetGameTime(10000);

    TRY_TO_STEER(kRight);

    EXPECT_EQ(SteeringCalls(L), 0) << "no steering was sent";
    EXPECT_NEAR(WheelAngle(L), 0.0, 1e-6) << "the Steering Wheel stayed as the helper had it";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false") << "nothing was grabbed";
    EXPECT_EQ(EvalLua(L, "cab.vehicle:getIsAIActive()"), "true") << "the helper is still driving";
    EXPECT_EQ(EvalLua(L, "select(1, VRCompanion:isWheelGrabAvailable())"), "false") << "Wheel Grab says it is unavailable";
}

TEST_F(CompanionModTest, WhenAHelperTakesOverWhileIHoldTheSteeringWheelIMustLetGoAndSteeringStops) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    TURN_HAND_TO(kRight, 0.7);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "true") << "the Steering Wheel is held before the helper starts";
    const int callsBefore = SteeringCalls(L);
    ASSERT_GT(callsBefore, 0);

    ASSERT_EQ(RunLuauChunk(L, "StubSteer.setHelper(cab.vehicle, true)", "=helperTakesOver"), 0);
    FRAMES(1);
    for (int step = 1; step <= 4; ++step) {
        TURN_HAND_TO(kRight, 0.7 + 0.1 * step);
    }

    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "false") << "the hand no longer holds the Steering Wheel";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false");
    EXPECT_EQ(SteeringCalls(L), callsBefore) << "nothing more was sent";
    EXPECT_EQ(EvalLua(L, "cab.vehicle:getIsAIActive()"), "true") << "the helper is still driving";
}

TEST_F(CompanionModTest, AGripStillSqueezedWhenTheHelperStopsDoesNotGrabAgainButANewSqueezeDoes) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("helper = true")));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    ASSERT_EQ(RunLuauChunk(L, "StubSteer.setHelper(cab.vehicle, false)", "=helperStops"), 0);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false") << "a grip squeezed before the helper stopped took nothing";
    EXPECT_EQ(SteeringCalls(L), 0);

    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "true") << "squeezing again takes hold";
}

TEST_F(CompanionModTest, ASteeringAssistSeesWheelGrabAsAnAnalogWheelPeripheralLikeAnyOtherSteeringInput) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.assistSaw = {}
        local vehicle = cab.vehicle
        local passThrough = vehicle.setSteeringInput
        vehicle.setSteeringInput = function(self, value, isAnalog, category)
            table.insert(assistSaw, { value = value, analog = isAnalog, category = category })
            return passThrough(self, value, isAnalog, category)
        end
    )luau", "=assist"), 0);

    TURN_CLOCKWISE_BY_SIX_TENTHS(kRight);

    EXPECT_GT(Number(L, "#assistSaw"), 0.0) << "the assist was told about the steering";
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        for index, call in ipairs(assistSaw) do
            assert(call.analog == true, "call " .. index .. " was not analog")
            assert(call.category == InputDevice.CATEGORY.WHEEL, "call " .. index .. " was not a Wheel Peripheral's")
        end
    )luau", "=assistSaw"), 0);
    EXPECT_NEAR(WheelAngle(L), 0.6, 0.02) << "and the Steering Wheel followed the hand";
}

TEST_F(CompanionModTest, MidGrabSteeringWheelNodeChangeReleasesHeldHandsUntilSqueezedAfresh) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("turnedSeatWheel = true")));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    TURN_HAND_TO(kRight, 0.3);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true");

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local altBase = StubScene.newNode("altSteeringWheelBase", cab.parent, 0, 1.2, 0.5, -0.6, 0, 0)
        local altNode = StubScene.newNode("altSteeringWheel", altBase, 0, 0, 0)
        cab.vehicle.spec_crabSteering = {
            hasSteeringModes = true,
            state = 1,
            steeringModes = {
                [1] = { steeringWheel = { node = altNode, indoorRotation = math.rad(540) } },
            },
        }
    )luau", "=crabWheelChange"), 0);
    FRAMES(1);
    const int callsAtChange = SteeringCalls(L);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false");

    TURN_HAND_TO(kRight, 0.7);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "false");
    EXPECT_EQ(SteeringCalls(L), callsAtChange);

    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true");
}

TEST_F(CompanionModTest, MidGrabSteeringDirectionChangeReleasesHeldHandsUntilSqueezedAfresh) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    TURN_HAND_TO(kRight, 0.3);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true");

    ASSERT_EQ(RunLuauChunk(L, "cab.vehicle.spec_reverseDriving = { isReverseDriving = true }", "=flipDirection"), 0);
    FRAMES(1);
    const int callsAtFlip = SteeringCalls(L);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isSteeringWheelHeld()"), "false");

    TURN_HAND_TO(kRight, 0.7);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "false");
    EXPECT_EQ(SteeringCalls(L), callsAtFlip);

    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true");
}
