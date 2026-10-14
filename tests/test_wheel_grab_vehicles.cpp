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

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include <string>

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

constexpr double kRimRadius = 0.18;  // m from the Steering Wheel's axis: inside the ring, on the rim

// The Steering Wheel turns 540 degrees (three half turns) from straight ahead to full lock in these cabs.
constexpr double kFullLockAngle = 3.0 * 3.14159265358979;

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

// A hand at a world position of the cab at the origin (see the top of the file).
HeadPose HandAtWorld(const Point& p) {
    return HandAt(static_cast<float>(-p.x), static_cast<float>(p.y - 1.5), static_cast<float>(-p.z));
}

FakeOpenXrControllers::Inputs Grip(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.grip = value;
    return inputs;
}

// A cab with a steerable vehicle: the Steering Wheel hangs in the cab's node, the way the game builds one.
// `options` are more StubSteer.equip options, e.g. "turnedSeat = true".
std::string SteerableCab(const std::string& options = "") {
    return "StubSteer.install()\n"
           "local cab = StubScene.cab({})\n"
           "StubSteer.equip(cab.vehicle, { parent = cab.parent, " + options + " })\n"
           "_G.cab = cab\n";
}

void StartMod(lua_State* L, fs25vr::VRBridge& bridge, const std::string& scene = SteerableCab()) {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSteeringVehicle, "=steeringVehicle"), 0);
    ASSERT_EQ(RunLuauChunk(L, scene.c_str(), "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
    // These tests turn the hand faster and further than the wheel follows, which would let go of the wheel by the
    // pull-away rule: that rule is tested in test_wheel_grab_pinned_hand.cpp.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.WHEEL_GRAB_PULL_AWAY_DISTANCE = 1000", "=noPullAway"), 0);
}

// What the game does each frame before the Companion Mod's update: the camera update that places the head in
// the world, and the vehicle's own update that acts on the steering input of the frame before.
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

double WheelAngle(lua_State* L) { return Number(L, "StubSteer.wheelAngle(cab.vehicle)"); }
double RoadWheelAngle(lua_State* L) { return Number(L, "StubSteer.roadWheelAngle(cab.vehicle)"); }
int SteeringCalls(lua_State* L) { return static_cast<int>(Number(L, "#StubSteer.calls")); }

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

// The hand brought to `rim` and the grip squeezed there.
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

// The hand brought to the rim, the grip squeezed and the hand turned a little, then the grip let go again: what a
// player does to steer, whatever comes of it.
#define TRY_TO_STEER(hand) \
    do { \
        GRAB((hand), RimPoint(L, kRimRadius, 0.0)); \
        for (int step_ = 1; step_ <= 4; ++step_) { \
            TURN_HAND_TO((hand), 0.2 * step_); \
        } \
        FRAMES(5); \
        headset.SetHandInputs((hand), Grip(0.0f)); \
        FRAMES(2); \
    } while (0)

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

TEST_F(CompanionModTest, OnATurnedSeatTheSteeringWheelISeeFollowsMyHandAndTheWheelsOnTheGroundSteerTheWayThatWheelWould) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("turnedSeat = true")));
    SetGameTime(10000);

    TURN_CLOCKWISE_BY_SIX_TENTHS(kRight);

    EXPECT_NEAR(WheelAngle(L), 0.6, 0.02) << "the Steering Wheel turned as far as the hand did, the same way";
    // Driving backwards the wheels on the ground steer the other way for the same turn of the Steering Wheel: this is
    // what "steers the way that wheel would" is, and it is the game's doing (it reverses the steering direction).
    EXPECT_NEAR(RoadWheelAngle(L), -0.6 / kFullLockAngle, 0.002)
        << "the wheels on the ground steered by the same fraction of full lock, the other way from an unturned seat";
    // The game reverses the way it turns the Steering Wheel and the vehicle's steering direction alike, so the value
    // that turns the wheel a given way is the same on every seat. Reversing it again would fight the game: the wheel
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
    // A tracked tractor's wheel: 120 degrees from straight ahead to full lock.
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

TEST_F(CompanionModTest, OnATurnedSeatWithASteeringWheelOfItsOwnTheHandTurnsThatWheel) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("turnedSeat = true, turnedSeatWheel = true")));
    SetGameTime(10000);

    TURN_CLOCKWISE_BY_SIX_TENTHS(kRight);  // the rim points are those of the wheel the game turns now

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
