// Wheel Grab with both hands and hand over hand (spec #89 ticket #92, ADR-0027), seen from outside: the real
// Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with a cab vehicle that steers the way the
// game's does (companion_mod_stubs.h, StubSteer). The test moves the two fake Motion Controllers, drives frames the way
// the game would and asserts only what the player would see and what the game was told: the steering calls that
// reached the vehicle and how far the Steering Wheel turned. One hand alone is covered by test_wheel_grab.cpp.
//
// The cab is at the world origin facing +Z with the head at tracking space origin (see test_wheel_grab.cpp). A hand "at
// the rim" is 0.18 m from the Steering Wheel's axis, at an angle about it: 0 is the direction the wheel's rim point
// starts in, a hand turned by +a is where the rim point of a wheel turned by a is, and a hand at angle + pi is on the
// opposite side of the rim.

#include "wheel_grab_test_support.h"
#include <cmath>
#include <string>

namespace {

using namespace wheel_grab_test;

constexpr double kFullLock540 = 3.0 * kPi;  // a vanilla Steering Wheel: 540 degrees each way

void StartMod(lua_State* L, fs25vr::VRBridge& bridge, const std::string& scene = SteerableCab()) {
    StartOptions options;
    options.noPullAway = true;
    ASSERT_NO_FATAL_FAILURE(StartWheelGrabMod(L, bridge, scene, options));
}

Point RimAt(lua_State* L, double angle) { return RimPoint(L, kRimRadius, angle); }

int CallCount(lua_State* L) { return static_cast<int>(Number(L, "#StubSteer.calls")); }

// The last steering value that reached the vehicle.
double LastValue(lua_State* L) { return Number(L, "StubSteer.calls[#StubSteer.calls].value"); }

// The steering value that, once the game has caught up, leaves a 540 degree Steering Wheel at `angle`.
double ValueFor540(double angle) { return -angle / kFullLock540; }

bool Held(lua_State* L) { return EvalLua(L, "VRCompanion:isSteeringWheelHeld()") == "true"; }

} // namespace

// The hand let go of the grip.
#define LET_GO(hand) \
    do { \
        headset.SetHandInputs((hand), Grip(0.0f)); \
        FRAMES(1); \
    } while (0)

// Both hands turned in the same frame: the right hand to `rightAngle`, the left hand to `leftAngle`.
#define TURN_HANDS_TO(rightAngle, leftAngle) \
    do { \
        headset.SetHandPose(kRight, HandAtWorld(RimAt(L, (rightAngle)))); \
        headset.SetHandPose(kLeft, HandAtWorld(RimAt(L, (leftAngle)))); \
        FRAMES(1); \
    } while (0)

// A hand turned from rim angle `from` to rim angle `to` in `steps` frames.
#define SWEEP_HAND(hand, from, to, steps) \
    do { \
        for (int step_ = 1; step_ <= (steps); ++step_) { \
            TURN_HAND_TO((hand), (from) + ((to) - (from)) * step_ / (steps)); \
        } \
    } while (0)


TEST_F(CompanionModTest, EitherHandAloneGrabsTheSteeringWheelAndSteersIt) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    for (const uint32_t hand : { kLeft, kRight }) {
        const char* name = hand == kLeft ? "left" : "right";
        const int callsBefore = CallCount(L);
        GRAB(hand, RimAt(L, 0.5));
        SWEEP_HAND(hand, 0.5, 1.1, 6);
        FRAMES(30);  // the game's analog steering catches up with the hand
        EXPECT_NEAR(WheelAngle(L), 0.6, 0.02) << "the " << name << " hand alone turned the wheel as far as it turned";
        EXPECT_GT(CallCount(L), callsBefore) << "the vehicle was told to steer by the " << name << " hand";

        LET_GO(hand);
        FRAMES(60);  // the game centres the wheel again
        ASSERT_NEAR(WheelAngle(L), 0.0, 0.01) << "the wheel went back to centre after the " << name << " hand let go";
    }
}

TEST_F(CompanionModTest, TwoHandsTurningTheSameWayTurnTheSteeringWheelByThatAngle) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimAt(L, 0.5));
    GRAB(kLeft, RimAt(L, 0.5 + kPi));  // the other side of the rim
    for (int step = 1; step <= 6; ++step) {
        TURN_HANDS_TO(0.5 + 0.1 * step, 0.5 + kPi + 0.1 * step);
    }
    FRAMES(30);

    EXPECT_NEAR(WheelAngle(L), 0.6, 0.02) << "two hands turning the same way turned the wheel by that angle, not twice it";
}

TEST_F(CompanionModTest, TwoHandsTurningByDifferentAmountsTurnTheSteeringWheelByTheirAverage) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimAt(L, 0.0));
    GRAB(kLeft, RimAt(L, kPi));
    for (int step = 1; step <= 8; ++step) {
        TURN_HANDS_TO(0.1 * step, kPi + 0.025 * step);  // the right hand turns 0.8, the left hand 0.2
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), 0.5, 0.02) << "the wheel turned by the average of 0.8 and 0.2";

    // Pulling the other way with one hand than with the other, by the same amount, holds the wheel still.
    const double held = WheelAngle(L);
    const int callsBefore = CallCount(L);
    for (int step = 1; step <= 8; ++step) {
        TURN_HANDS_TO(0.8 + 0.05 * step, kPi + 0.2 - 0.05 * step);
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), held, 0.01) << "equal and opposite turns left the wheel still";
    EXPECT_GT(CallCount(L), callsBefore) << "the wheel was still held, and the vehicle kept being told to steer";
    EXPECT_NEAR(LastValue(L), ValueFor540(held), 0.002) << "what it was told is where the wheel is";
}

TEST_F(CompanionModTest, LettingGoWithOneHandLeavesTheOtherHandSteeringWithoutAJump) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimAt(L, 0.0));
    GRAB(kLeft, RimAt(L, kPi));
    for (int step = 1; step <= 4; ++step) {
        TURN_HANDS_TO(0.1 * step, kPi + 0.1 * step);
    }
    FRAMES(30);
    ASSERT_NEAR(WheelAngle(L), 0.4, 0.02);
    const double valueBefore = LastValue(L);
    const int callsBefore = CallCount(L);

    LET_GO(kLeft);
    headset.SetHandPose(kLeft, HandAtWorld(Point{ 0.5, 1.1, 0.3 }));  // and the free hand wanders off
    FRAMES(1);
    EXPECT_GT(CallCount(L), callsBefore) << "the other hand carried on steering";
    EXPECT_NEAR(LastValue(L), valueBefore, 0.002) << "no jump in what the vehicle was told when one hand let go";
    EXPECT_TRUE(Held(L));

    for (int step = 1; step <= 3; ++step) {
        TURN_HAND_TO(kRight, 0.4 + 0.1 * step);
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), 0.7, 0.02) << "the hand that kept holding turned the wheel on by its own turn";
}

TEST_F(CompanionModTest, ARegrabbingHandWhileTheOtherHoldsPicksUpFromTheWheelWithoutAJump) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimAt(L, 0.0));
    GRAB(kLeft, RimAt(L, kPi));
    for (int step = 1; step <= 4; ++step) {
        TURN_HANDS_TO(0.1 * step, kPi + 0.1 * step);
    }
    FRAMES(30);
    ASSERT_NEAR(WheelAngle(L), 0.4, 0.02);

    // The left hand lets go and takes hold again somewhere else on the rim, a good way round from where it was.
    LET_GO(kLeft);
    headset.SetHandPose(kLeft, HandAtWorld(RimAt(L, 2.0)));
    FRAMES(2);
    const double valueBefore = LastValue(L);
    headset.SetHandInputs(kLeft, Grip(1.0f));
    FRAMES(1);
    EXPECT_TRUE(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(0)") == "true") << "the left hand has the wheel again";
    EXPECT_NEAR(LastValue(L), valueBefore, 0.002) << "regrabbing elsewhere did not move the wheel";
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), 0.4, 0.02);

    // Both hands turn from where they hold now.
    for (int step = 1; step <= 5; ++step) {
        TURN_HANDS_TO(0.4 + 0.1 * step, 2.0 + 0.1 * step);
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), 0.9, 0.02) << "turning on from the new grip adds the turn to the wheel's own angle";
}

TEST_F(CompanionModTest, AHandTakingHoldWhileTheOtherTurnsLeavesTheOthersTurnWhole) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimAt(L, 0.0));
    BRING_HAND(kLeft, RimAt(L, kPi));  // on the rim, the grip not squeezed yet
    FRAMES(2);
    ASSERT_NEAR(LastValue(L), 0.0, 0.002) << "the right hand holds the wheel still";

    // In one frame the right hand turns by 0.2 and the left hand takes hold: the new hand has not turned the wheel yet,
    // so the turn that frame is the right hand's, whole and not averaged with the left hand's nothing.
    headset.SetHandPose(kRight, HandAtWorld(RimAt(L, 0.2)));
    headset.SetHandInputs(kLeft, Grip(1.0f));
    FRAMES(1);
    ASSERT_TRUE(Held(L));
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(0)"), "true") << "the left hand took hold";
    EXPECT_NEAR(LastValue(L), ValueFor540(0.2), 0.0005) << "the wheel was asked for the whole of the right hand's turn";

    // From then on the two hands average as usual.
    TURN_HANDS_TO(0.4, kPi + 0.2);
    EXPECT_NEAR(LastValue(L), ValueFor540(0.2 + 0.2), 0.0005) << "both hands turned 0.2 more";
}

TEST_F(CompanionModTest, AHandLettingGoWhileTheOtherTurnsLeavesTheOthersTurnWholeAndIgnoresItsOwn) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimAt(L, 0.0));
    GRAB(kLeft, RimAt(L, kPi));
    FRAMES(2);
    ASSERT_NEAR(LastValue(L), 0.0, 0.002);

    // In one frame the left hand lets go and swings round a long way while the right hand turns by 0.2.
    headset.SetHandInputs(kLeft, Grip(0.0f));
    headset.SetHandPose(kLeft, HandAtWorld(RimAt(L, kPi + 1.5)));
    headset.SetHandPose(kRight, HandAtWorld(RimAt(L, 0.2)));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(0)"), "false") << "the left hand let go";
    EXPECT_NEAR(LastValue(L), ValueFor540(0.2), 0.0005) << "the wheel went by the right hand's turn alone";
}
TEST_F(CompanionModTest, AHandRegrabbingAfterLettingGoPicksUpFromTheWheelsCurrentAngleNotWhereTheLastGrabWasHeading) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    // A very slow steering duration: the wheel stays well behind a hand that turns it, and drifts only a little
    // towards centre while nothing holds it.
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("steeringDuration = 100")));
    SetGameTime(10000);

    GRAB(kRight, RimAt(L, 0.0));
    SWEEP_HAND(kRight, 0.0, 2.0, 10);  // the hand turns far more than the slow wheel has followed
    FRAMES(2);
    ASSERT_GT(WheelAngle(L), 0.0001) << "the wheel has begun to follow";
    ASSERT_LT(WheelAngle(L), 0.3) << "but is far behind the hand";
    LET_GO(kRight);
    TURN_HAND_TO(kRight, 4.0);

    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(1);
    const double wheelNow = WheelAngle(L);
    EXPECT_NEAR(LastValue(L), ValueFor540(wheelNow), 0.002)
        << "regrabbing asked for the wheel where it is, not where the last grab had been heading";

    TURN_HAND_TO(kRight, 4.3);
    EXPECT_NEAR(LastValue(L), ValueFor540(wheelNow + 0.3), 0.002) << "and turns on from there";
}

TEST_F(CompanionModTest, RepeatedHandOverHandTurnsWithOneHandReachFullLockOnA540DegreeSteeringWheel) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));  // 540 degrees at full lock, steering duration 1 s
    SetGameTime(10000);

    // One turn of the hand is 3 radians, a third of the 9.4 the wheel can turn: full lock takes several regrabs.
    int turns = 0;
    double reached = 0.0;
    double previousStart = 0.0;
    while (turns < 12 && reached < kFullLock540 - 0.02) {
        GRAB(kRight, RimAt(L, 0.0));
        SWEEP_HAND(kRight, 0.0, 3.0, 12);
        FRAMES(2);
        reached = WheelAngle(L);
        EXPECT_GT(reached, previousStart + 0.5) << "turn " << turns + 1 << " took the wheel on";
        previousStart = reached;
        ++turns;
        if (reached >= kFullLock540 - 0.02) break;
        LET_GO(kRight);
    }
    EXPECT_NEAR(reached, kFullLock540, 0.02) << "full lock reached";
    EXPECT_GT(turns, 1) << "no single turn of the hand does it";
    EXPECT_LT(turns, 12);
}

TEST_F(CompanionModTest, PassingTheSteeringWheelFromHandToHandReachesFullLockWithoutItEverCentring) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    // The holder turns 1.6 radians, the other hand takes hold behind it, the holder lets go and goes back round. A
    // hand is on the wheel the whole time, so the game never centres it.
    uint32_t holder = kRight;
    uint32_t other = kLeft;
    double holderAngle = 0.0;
    GRAB(holder, RimAt(L, holderAngle));
    double reached = 0.0;
    int handOvers = 0;
    while (handOvers < 10 && reached < kFullLock540 - 0.02) {
        SWEEP_HAND(holder, holderAngle, holderAngle + 1.6, 8);
        holderAngle += 1.6;
        FRAMES(5);
        const double sofar = WheelAngle(L);
        EXPECT_GT(sofar, reached + 1.4) << "hand-over " << handOvers + 1 << " took the wheel on";
        reached = sofar;

        // The other hand takes hold behind the holder (where it started its turn) while the holder still holds.
        const double behind = holderAngle - 1.6;
        GRAB(other, RimAt(L, behind));
        FRAMES(5);
        EXPECT_NEAR(WheelAngle(L), reached, 0.05) << "the second hand taking hold did not move the wheel";
        LET_GO(holder);
        EXPECT_TRUE(Held(L)) << "the second hand holds the wheel";
        EXPECT_NEAR(WheelAngle(L), reached, 0.2) << "the first hand letting go did not move the wheel";

        std::swap(holder, other);
        holderAngle = behind;
        ++handOvers;
    }
    FRAMES(5);
    EXPECT_NEAR(WheelAngle(L), kFullLock540, 0.02) << "full lock reached with the wheel never let go";
    EXPECT_LT(handOvers, 10);
}

TEST_F(CompanionModTest, TheGrabEndsAndSteeringStopsBeingSentOnlyWhenTheLastHandLetsGo) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimAt(L, 0.0));
    GRAB(kLeft, RimAt(L, kPi));
    for (int step = 1; step <= 4; ++step) {
        TURN_HANDS_TO(0.1 * step, kPi + 0.1 * step);
    }
    FRAMES(10);
    ASSERT_TRUE(Held(L));

    LET_GO(kLeft);
    int calls = CallCount(L);
    FRAMES(5);
    EXPECT_GT(CallCount(L), calls) << "one hand still holding: steering is still being sent";
    EXPECT_TRUE(Held(L)) << "and the wheel is still held";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(0)"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true");

    LET_GO(kRight);
    EXPECT_FALSE(Held(L)) << "the last hand let go: the grab ended";
    calls = CallCount(L);
    TURN_HAND_TO(kRight, 2.0);  // hands moving on do not steer
    FRAMES(60);
    EXPECT_EQ(CallCount(L), calls) << "nothing was sent after the last hand let go";
    EXPECT_NEAR(WheelAngle(L), 0.0, 0.01) << "the game's own steering back centred the wheel";
}

TEST_F(CompanionModTest, TheFirstHandLettingGoWhileTheSecondHoldsAlsoLeavesTheSecondSteeringAfterwards) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    // The left hand takes hold first and lets go first: the order of the hands does not matter.
    GRAB(kLeft, RimAt(L, 0.0));
    GRAB(kRight, RimAt(L, kPi));
    LET_GO(kLeft);
    EXPECT_TRUE(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)") == "true");
    for (int step = 1; step <= 5; ++step) {
        TURN_HAND_TO(kRight, kPi + 0.2 * step);
    }
    FRAMES(30);
    EXPECT_NEAR(WheelAngle(L), 1.0, 0.02) << "the right hand steered alone after the left let go";
}
