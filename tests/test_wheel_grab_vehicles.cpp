// Wheel Grab on turned seats and with hired helpers (spec #89, ticket #96, ADR-0027), seen from outside: the real
// Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with a cab vehicle that steers the way the
// game's does (companion_mod_stubs.h, StubSteer, with its turnedSeat and helper options). The test moves the fake
// Motion Controllers and asserts only what the player would see and what the game was told: the steering calls that
// reached the vehicle, how far the Steering Wheel turned and which way the wheels on the ground steered.
//
// A turned seat (ReverseDriving) flips BOTH the direction the game turns the Steering Wheel node in and the
// vehicle's steering direction, so the visible Steering Wheel follows the hand exactly as on any seat and the wheels
// on the ground steer the other way, as they must when driving backwards. The two flips cancel in the steering
// value, so for the same turn of the Steering Wheel the value Wheel Grab sends is the same on a turned seat as on
// an unturned one. (Ticket #96 words this as "steering of the opposite sign"; that is what the vehicle's wheels do,
// not the value sent. The tests assert both.)
//
// The cab is the same as in test_wheel_grab.cpp: at the world origin facing +Z with the head at tracking space
// origin, the Steering Wheel 0.5 m in front of the head and 0.3 m below it; a hand "at the rim" is 0.18 m from its axis.

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

} // namespace

// The hand takes hold at 0.5 radians and turns on to 1.1; the game's steering catches up.
#define TURN_CLOCKWISE_BY_SIX_TENTHS(hand) \
    do { \
        GRAB((hand), RimPoint(L, kRimRadius, 0.5)); \
        for (int step_ = 1; step_ <= 6; ++step_) { \
            TURN_HAND_TO((hand), 0.5 + 0.1 * step_); \
        } \
        FRAMES(30); \
    } while (0)


// ---- Turned seats ---------------------------------------------------------------------------------------------------

TEST_F(CompanionModTest, OnAnUnturnedSeatTurningTheSteeringWheelOneWayPointsTheWheelsOnTheGroundTheOtherWay) {
    // The reference the turned seat is compared with.
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
    // Driving backwards the wheels on the ground steer the other way for the same turn of the Steering Wheel: this is
    // what "steers the way that Steering Wheel would" is, and it is the game's doing (it reverses the steering direction).
    EXPECT_NEAR(RoadWheelAngle(L), -0.6 / kFullLockAngle, 0.002)
        << "the wheels on the ground steered by the same fraction of full lock, the other way from an unturned seat";
    // The game reverses the way it turns the Steering Wheel and the vehicle's steering direction alike, so the value
    // that turns the Steering Wheel a given way is the same on every seat. Reversing it again would fight the game: the Steering Wheel
    // would turn away from the hand.
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
        TURN_HAND_TO(kRight, 0.4 * step);  // well past full lock
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

    TURN_CLOCKWISE_BY_SIX_TENTHS(kRight);  // the rim points are those of the Steering Wheel the game turns now

    EXPECT_NEAR(WheelAngle(L), 0.6, 0.02) << "the turned seat's own Steering Wheel followed the hand";
    EXPECT_NEAR(RoadWheelAngle(L), -0.6 / kFullLockAngle, 0.002) << "and steered the vehicle";
}

// ---- Hired helpers --------------------------------------------------------------------------------------------------

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
        TURN_HAND_TO(kRight, 0.7 + 0.1 * step);  // the grip is still squeezed
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

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));  // squeezed at the rim while the helper drives
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

// ---- Steering assists -----------------------------------------------------------------------------------------------

TEST_F(CompanionModTest, ASteeringAssistSeesWheelGrabAsAnAnalogWheelPeripheralLikeAnyOtherSteeringInput) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    // An assist (another mod) that watches the steering input every device sends to the vehicle and lets it through.
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
