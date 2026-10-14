// Wheel Grab (spec #89, ticket #91): the Virtual Hand stays on the Steering Wheel while it is held, and the player
// feels the grab and the end stop. The real Companion Mod runs against the real VRBridge over FakeTrackedHeadset,
// with the cab vehicle that steers the way the game's does (companion_mod_stubs.h, StubSteer) and the hand model's
// scene-graph stubs (StubModel). The test moves the fake Motion Controllers, drives frames the way the game would and
// asserts only what the player would see and feel: where the Virtual Hand node is drawn, how the fingers curl, the
// haptic pulses the fake runtime recorded, the steering calls that reached the vehicle and the diagnostics line.
// Prior art: test_wheel_grab.cpp (the same cab), test_virtual_hands.cpp (hand nodes), test_haptics.cpp (pulses).
//
// The cab is at the world origin facing +Z with the head at the tracking space origin, so a point at world
// (x, y, z) is where a hand at tracking (-x, y - 1.5, -z) is. The Steering Wheel hangs 0.5 m in front of the
// head and 0.3 m below it, leaning toward the driver; a hand "at the rim" is 0.18 m from its axis. A small
// full-lock rotation (120 degrees, a tracked tractor's) keeps full lock within a hand's reach and makes the game's
// steering rate lag easy to see: the wheel moves 0.087 rad per frame at most.

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

constexpr double kRimRadius = 0.18;  // m from the Steering Wheel's axis: inside the ring, on the rim
constexpr double kFullLock = 2.0943951;  // rad: 120 degrees, the in-cab rotation of the cab below

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

// The engine's console globals the Companion Mod registers into, and the menu.
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
    -- Where a Virtual Hand ("left" or "right") is drawn in the world, nil when there is none.
    function _G.handPosition(which)
        local node = StubModel.hands()[which]
        if node == nil then return nil end
        return StubScene.worldPosition(node)
    end
    function _G.handIsShown(which)
        local node = StubModel.hands()[which]
        return node ~= nil and StubModel.isShown(node)
    end
)luau";

// A cab with a steerable vehicle that turns 120 degrees at full lock; `options` are more StubSteer.equip options.
std::string SteerableCab(const std::string& options = "") {
    return "StubSteer.install()\n"
           "local cab = StubScene.cab({})\n"
           "StubSteer.equip(cab.vehicle, { parent = cab.parent, rotationDeg = 120, " + options + " })\n"
           "_G.cab = cab\n";
}

void StartMod(lua_State* L, fs25vr::VRBridge& bridge, const std::string& scene = SteerableCab()) {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubModelLoading, "=modelLoading"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSteeringVehicle, "=steeringVehicle"), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, scene.c_str(), "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
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

// The world position of the point on the Steering Wheel `radius` from its axis when the wheel's base is at `angle`.
Point RimPoint(lua_State* L, double radius, double angle) {
    const std::string args = "(cab.vehicle, " + std::to_string(radius) + ", " + std::to_string(angle) + ")";
    return { Number(L, "select(1, StubSteer.rimPoint" + args + ")"), Number(L, "select(2, StubSteer.rimPoint" + args + ")"),
             Number(L, "select(3, StubSteer.rimPoint" + args + ")") };
}

double WheelAngle(lua_State* L) { return Number(L, "StubSteer.wheelAngle(cab.vehicle)"); }

// Where the Virtual Hand node of a hand is drawn.
Point HandDrawnAt(lua_State* L, const char* which) {
    const std::string name = std::string("'") + which + "'";
    return { Number(L, "select(1, handPosition(" + name + "))"), Number(L, "select(2, handPosition(" + name + "))"),
             Number(L, "select(3, handPosition(" + name + "))") };
}

double Distance(const Point& a, const Point& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

int CountLogLines(const std::string& text) {
    int count = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find(text) != std::string::npos) ++count;
    }
    return count;
}

size_t PulsesTo(const FakeTrackedHeadset& headset, int hand) {
    size_t count = 0;
    for (const auto& pulse : headset.HapticPulses()) {
        if (pulse.hand == hand) ++count;
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

// The hand brought to `rim` and the grip squeezed there.
#define GRAB(hand, rim) \
    do { \
        BRING_HAND((hand), (rim)); \
        headset.SetHandInputs((hand), Grip(1.0f)); \
        FRAMES(1); \
    } while (0)

// The hand moved to the rim point at base angle `angle`, then a frame later.
#define MOVE_HAND_TO(hand, angle) \
    do { \
        headset.SetHandPose((hand), HandAtWorld(RimPoint(L, kRimRadius, (angle)))); \
        FRAMES(1); \
    } while (0)

TEST_F(CompanionModTest, AHoldingVirtualHandStaysOnTheRimWhereItGrabbedAndFollowsTheWheelNotTheHand) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    const double grabAngle = 0.5;
    GRAB(kRight, RimPoint(L, kRimRadius, grabAngle));
    EXPECT_LT(Distance(HandDrawnAt(L, "right"), RimPoint(L, kRimRadius, grabAngle)), 1e-3)
        << "the Virtual Hand is on the rim where it grabbed";

    // A sudden yank of the hand runs ahead of the Steering Wheel, which the game turns at a capped rate.
    MOVE_HAND_TO(kRight, grabAngle + 0.8);
    const double lagging = WheelAngle(L);
    ASSERT_LT(lagging, 0.4) << "the wheel has not caught up with the yank yet";
    EXPECT_LT(Distance(HandDrawnAt(L, "right"), RimPoint(L, kRimRadius, grabAngle + lagging)), 1e-3)
        << "the Virtual Hand is drawn with the wheel's actual rotation, not where the hand is";
    EXPECT_GT(Distance(HandDrawnAt(L, "right"), RimPoint(L, kRimRadius, grabAngle + 0.8)), 0.05)
        << "so the lag shows as the hand staying on the wheel";

    FRAMES(30);  // the game's steering catches up with the hand
    EXPECT_NEAR(WheelAngle(L), 0.8, 0.02);
    EXPECT_LT(Distance(HandDrawnAt(L, "right"), RimPoint(L, kRimRadius, grabAngle + WheelAngle(L))), 1e-3)
        << "and the hand turned with the wheel to where the real hand is";
}

// The hand turned about the wheel's axis in steps of `step` rad (a frame each, slow enough for the wheel to keep up)
// from base angle `from` to `to`.
#define TURN_HAND_FROM_TO(hand, from, to, step) \
    do { \
        const double sign_ = ((to) >= (from)) ? 1.0 : -1.0; \
        for (double angle_ = (from); sign_ * ((to) - angle_) > 1e-9; ) { \
            angle_ = (sign_ > 0) ? std::min(angle_ + (step), (double)(to)) : std::max(angle_ - (step), (double)(to)); \
            MOVE_HAND_TO((hand), angle_); \
            FRAMES(1); \
        } \
    } while (0)

TEST_F(CompanionModTest, AfterLettingGoTheVirtualHandIsDrawnAtTheGripPoseAgain) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    MOVE_HAND_TO(kRight, 1.3);  // a yank: the pinned hand lags behind the real one
    ASSERT_GT(Distance(HandDrawnAt(L, "right"), RimPoint(L, kRimRadius, 1.3)), 0.05);

    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(1);
    EXPECT_LT(Distance(HandDrawnAt(L, "right"), RimPoint(L, kRimRadius, 1.3)), 1e-3)
        << "the Virtual Hand is where the real hand is again";
    EXPECT_EQ(EvalLua(L, "handIsShown('right')"), "true");
}

TEST_F(CompanionModTest, AHandHeldStillOnTheWheelStaysShownLongerThanTheInUseTimeout) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    ASSERT_EQ(EvalLua(L, "handIsShown('right')"), "true");
    AdvanceGameTime(5000);  // well past the 3 s a Motion Controller counts as in use after it last moved
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "handIsShown('right')"), "true") << "a hand that holds the Steering Wheel is not put away";
    EXPECT_LT(Distance(HandDrawnAt(L, "right"), RimPoint(L, kRimRadius, 0.5)), 1e-3) << "and stays on the rim";

    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(3);
    AdvanceGameTime(5000);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "handIsShown('right')"), "false") << "once it has let go it is a hand at rest like any other";
}

TEST_F(CompanionModTest, PullingTheRealHandAwayFromThePinnedPointEndsTheGrabWithTheGripStillSqueezed) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);
    const double pullAway = Number(L, "VRCompanion.WHEEL_GRAB_PULL_AWAY_DISTANCE");
    EXPECT_NEAR(pullAway, 0.20, 0.05) << "about 20 cm";

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    const Point pinned = RimPoint(L, kRimRadius, 0.5);

    // Just inside the pull-away distance (and outside the grab ring, which only matters for grabbing): still holding.
    headset.SetHandPose(kRight, HandAtWorld(RimPoint(L, kRimRadius + 0.9 * pullAway, 0.5)));
    const size_t callsBefore = static_cast<size_t>(Number(L, "#StubSteer.calls"));
    FRAMES(3);
    EXPECT_GT(Number(L, "#StubSteer.calls"), callsBefore) << "the Steering Wheel is still steered";
    EXPECT_LT(Distance(HandDrawnAt(L, "right"), pinned), 1e-3) << "and the Virtual Hand is on the wheel, not on the hand";

    // Past it: the grab ends as though the grip were let go.
    const Point away = RimPoint(L, kRimRadius + 1.1 * pullAway, 0.5);
    headset.SetHandPose(kRight, HandAtWorld(away));
    FRAMES(1);
    const double callsAfterPull = Number(L, "#StubSteer.calls");
    FRAMES(5);
    EXPECT_EQ(Number(L, "#StubSteer.calls"), callsAfterPull) << "nothing steers the vehicle any more";
    EXPECT_LT(Distance(HandDrawnAt(L, "right"), away), 1e-3) << "the Virtual Hand is at the grip pose again";

    // The grip is still squeezed: coming back to the rim does not take hold again, squeezing afresh does.
    headset.SetHandPose(kRight, HandAtWorld(pinned));
    FRAMES(3);
    EXPECT_LT(Distance(HandDrawnAt(L, "right"), pinned), 1e-3);
    const double callsBack = Number(L, "#StubSteer.calls");
    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(2);
    EXPECT_GT(Number(L, "#StubSteer.calls"), callsBack) << "squeezing the grip again at the rim takes hold again";
}

TEST_F(CompanionModTest, AHoldingVirtualHandKeepsCurlingItsFingersWithTheGrip) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local hand = StubModel.hands().right
        for _, name in ipairs({ 'middle1', 'ring1', 'little1' }) do
            assert(StubModel.curl(hand, name) > math.rad(30), name .. " is curled round the rim")
        end
    )luau", "=fingersCurled"), 0);

    headset.SetHandInputs(kRight, Grip(0.5f));  // eased, still above the release threshold: holding
    FRAMES(1);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local hand = StubModel.hands().right
        local half = StubModel.curl(hand, 'middle1')
        assert(half > math.rad(5) and half < math.rad(40), "the fingers follow the grip: " .. math.deg(half))
    )luau", "=fingersFollowGrip"), 0);
}

TEST_F(CompanionModTest, AHoldingVirtualHandTurnsWithTheWheelAsIfGluedToIt) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    TURN_HAND_FROM_TO(kRight, 0.5, 1.3, 0.1);
    FRAMES(20);
    ASSERT_NEAR(WheelAngle(L), 0.8, 0.02);

    // The hand's forward direction, carried into the frame above the wheel node (which does not turn with the wheel),
    // has turned about the wheel's axis (that frame's Y) by the angle the wheel turned, like the hand's position.
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local base = StubSteer.wheelBase(cab.vehicle)
        local hand = StubModel.hands().right
        local px, py, pz = StubScene.worldPosition(hand)
        local fx, fy, fz = StubScene.worldDirection(hand, 0, 0, -1)
        local ax, ay, az = worldToLocal(base, px, py, pz)
        local bx, by, bz = worldToLocal(base, px + fx, py + fy, pz + fz)
        local dx, dy, dz = bx - ax, by - ay, bz - az
        -- the same direction when the wheel was at 0 (the hand just grabbed): turn it back by the wheel's angle
        local a = StubSteer.wheelAngle(cab.vehicle)
        local ox = dx * math.cos(a) - dz * math.sin(a)
        local oy = dy
        local oz = dx * math.sin(a) + dz * math.cos(a)
        -- at the grab the hand was at tracking identity rotation: forward is world +Z from the cab at the origin
        local ex, ey, ez = worldToLocal(base, 0, 0, 0)
        local tx, ty, tz = worldToLocal(base, 0, 0, 1)
        local ux, uy, uz = tx - ex, ty - ey, tz - ez
        assert(math.abs(ox - ux) < 1e-3 and math.abs(oy - uy) < 1e-3 and math.abs(oz - uz) < 1e-3,
            string.format("forward turned back (%.3f %.3f %.3f) should be the grab-time forward (%.3f %.3f %.3f)", ox, oy, oz, ux, uy, uz))
    )luau", "=pinnedRotation"), 0);
}

// ---- Haptics -------------------------------------------------------------------------------------------------

TEST_F(CompanionModTest, GrabbingPulsesTheGrabbingHandOnceAndLettingGoIsSilent) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    BRING_HAND(kRight, RimPoint(L, kRimRadius, 0.5));
    EXPECT_EQ(headset.HapticPulses().size(), 0u) << "reaching the rim is silent";
    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(1);
    ASSERT_EQ(headset.HapticPulses().size(), 1u) << "one pulse when the hand takes hold";
    EXPECT_EQ(headset.HapticPulses()[0].hand, 1) << "in the hand that grabbed";
    EXPECT_GT(headset.HapticPulses()[0].amplitude, 0.0f);
    EXPECT_LE(headset.HapticPulses()[0].duration, 60 * 1000 * 1000) << "a short one";

    FRAMES(10);
    EXPECT_EQ(headset.HapticPulses().size(), 1u) << "no continuous rumble";
    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(5);
    EXPECT_EQ(headset.HapticPulses().size(), 1u) << "nothing on release";
}

TEST_F(CompanionModTest, GrabbingAwayFromTheRimIsSilent) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, 0.6, 0.5));
    EXPECT_EQ(headset.HapticPulses().size(), 0u) << "a grip squeezed far from the Steering Wheel takes nothing";
}

TEST_F(CompanionModTest, ReachingFullLockPulsesFirmlyOnceAndAgainOnlyAfterLeavingTheStop) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    ASSERT_EQ(headset.HapticPulses().size(), 1u);
    const auto grabPulse = headset.HapticPulses()[0];

    TURN_HAND_FROM_TO(kRight, 0.5, 0.5 + kFullLock - 0.2, 0.1);
    EXPECT_EQ(headset.HapticPulses().size(), 1u) << "nothing before the stop";
    TURN_HAND_FROM_TO(kRight, 0.5 + kFullLock - 0.2, 0.5 + kFullLock + 0.3, 0.1);
    FRAMES(30);
    ASSERT_EQ(headset.HapticPulses().size(), 2u) << "one pulse when the target reaches full lock";
    const auto lockPulse = headset.HapticPulses()[1];
    EXPECT_EQ(lockPulse.hand, 1);
    EXPECT_GT(lockPulse.amplitude, grabPulse.amplitude) << "firmer than the grab";
    EXPECT_GT(lockPulse.duration, grabPulse.duration);
    EXPECT_NEAR(WheelAngle(L), kFullLock, 0.02) << "the wheel is at the stop";

    // The hand keeps pulling: the stop is dead, the pulse does not repeat.
    TURN_HAND_FROM_TO(kRight, 0.5 + kFullLock + 0.3, 0.5 + kFullLock + 0.45, 0.05);
    FRAMES(10);
    EXPECT_EQ(headset.HapticPulses().size(), 2u) << "no repeat while at the stop";

    // Turning back moves the wheel at once, leaving the stop; reaching it again pulses again.
    TURN_HAND_FROM_TO(kRight, 0.5 + kFullLock + 0.45, 0.5 + kFullLock - 0.4, 0.05);
    FRAMES(10);
    EXPECT_EQ(headset.HapticPulses().size(), 2u) << "leaving the stop is silent";
    TURN_HAND_FROM_TO(kRight, 0.5 + kFullLock - 0.4, 0.5 + kFullLock + 0.6, 0.05);  // the 0.85 rad it left by, and more
    FRAMES(10);
    EXPECT_EQ(headset.HapticPulses().size(), 3u) << "reaching it again";

    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(5);
    EXPECT_EQ(headset.HapticPulses().size(), 3u) << "nothing on release";
    EXPECT_EQ(PulsesTo(headset, 0), 0u) << "the other hand felt nothing";
}

TEST_F(CompanionModTest, EveryHoldingHandFeelsTheStopWhenTwoHandsTurnTheWheelToFullLock) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    const double pi = 3.14159265358979;
    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    GRAB(kLeft, RimPoint(L, kRimRadius, 0.5 + pi));
    ASSERT_EQ(PulsesTo(headset, 1), 1u);
    ASSERT_EQ(PulsesTo(headset, 0), 1u) << "each hand felt its own grab";

    for (int step = 1; step <= 25; ++step) {
        const double turn = 0.1 * step;
        headset.SetHandPose(kRight, HandAtWorld(RimPoint(L, kRimRadius, 0.5 + turn)));
        headset.SetHandPose(kLeft, HandAtWorld(RimPoint(L, kRimRadius, 0.5 + pi + turn)));
        FRAMES(2);
    }
    FRAMES(20);
    EXPECT_NEAR(WheelAngle(L), kFullLock, 0.02);
    EXPECT_EQ(PulsesTo(headset, 1), 2u) << "the right hand felt the stop";
    EXPECT_EQ(PulsesTo(headset, 0), 2u) << "and so did the left hand, once";
}

TEST_F(CompanionModTest, ASecondHandTakingHoldAtTheStopFeelsOnlyItsOwnGrab) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    TURN_HAND_FROM_TO(kRight, 0.5, 0.5 + kFullLock + 0.1, 0.1);
    FRAMES(30);
    ASSERT_NEAR(WheelAngle(L), kFullLock, 0.02);
    ASSERT_EQ(PulsesTo(headset, 1), 2u) << "the grab and the stop";

    GRAB(kLeft, RimPoint(L, kRimRadius, 0.5 + kFullLock + 0.1 + 3.14159265358979));
    FRAMES(10);
    EXPECT_EQ(PulsesTo(headset, 0), 1u) << "the left hand felt its grab and nothing more: the wheel was at the stop already";
    EXPECT_EQ(PulsesTo(headset, 1), 2u) << "and the right hand felt nothing new";
}
// ---- Diagnostics ---------------------------------------------------------------------------------------------

TEST_F(CompanionModTest, TheDiagnosticsReportHowFarTheRealHandIsFromItsPinnedPoint) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    SetGameTime(10000);

    GRAB(kRight, RimPoint(L, kRimRadius, 0.5));
    headset.SetHandPose(kRight, HandAtWorld(RimPoint(L, kRimRadius + 0.12, 0.5)));
    FRAMES(2);

    const std::string report = EvalLua(L, "runCommand('vrWheelGrabDiagnostics')");
    EXPECT_NE(report.find("0.12 m from its pinned point"), std::string::npos) << report;
    EXPECT_GE(CountLogLines("0.12 m from its pinned point"), 1) << "also in the game log";
}