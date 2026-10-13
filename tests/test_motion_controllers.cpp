// Motion Controller state in the Companion Mod (spec #71, ticket #72), seen from outside: the real
// Companion Mod runs against the real VRBridge over FakeTrackedHeadset, the test moves the fake
// controllers and drives frames and the game clock, and asserts what the mod reports and what the
// Injector's OpenXR layer was asked to do. Later Motion Controller tickets add their own files next
// to this one and reuse companion_mod_fixture.h.

#include "companion_mod_fixture.h"

namespace {

using fs25vr::FakeOpenXrControllers;
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

// Records console commands as the engine would register them, so tests can call the handlers.
constexpr const char* kConsoleStubs = R"luau(
    _G.registeredCommands = {}
    _G.removedCommands = {}
    _G.addConsoleCommand = function(name, desc, method, target)
        _G.registeredCommands[name] = { desc = desc, method = method, target = target }
    end
    _G.removeConsoleCommand = function(name) _G.removedCommands[name] = true end
    _G.runCommand = function(name, ...)
        local command = _G.registeredCommands[name]
        assert(command ~= nil, name .. " is not registered")
        return command.target[command.method](command.target, ...)
    end
)luau";

// Starts the Companion Mod over `headset` with console and engine stubs, ready for frames.
void StartMod(lua_State* L, FakeTrackedHeadset& headset, fs25vr::VRBridge& bridge) {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
    (void)headset;
}

} // namespace

TEST_F(CompanionModTest, HandStateOfBothMotionControllersReachesTheModEveryFrame) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));

    FakeOpenXrControllers::Inputs right;
    right.trigger = 0.75f;
    right.thumbX = -0.5f;
    right.menu = true;
    headset.SetHandPose(kRight, HandAt(0.30f, 1.20f, -0.40f));
    headset.SetHandInputs(kRight, right);
    headset.SetHandPose(kLeft, HandAt(-0.30f, 1.10f, -0.35f));
    NextFrame(&headset);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local right = VRCompanion:getHandState(1)
        assert(right.tracked == true, "right tracked")
        assert(math.abs(right.x - 0.30) < 1e-5 and math.abs(right.y - 1.20) < 1e-5 and math.abs(right.z + 0.40) < 1e-5)
        assert(math.abs(right.trigger - 0.75) < 1e-6 and math.abs(right.thumbX + 0.5) < 1e-6 and right.menu == true)
        local left = VRCompanion:getHandState(0)
        assert(left.tracked == true and math.abs(left.x + 0.30) < 1e-5 and left.trigger == 0 and left.menu == false)
    )luau", "=handState"), 0);

    // The next frame brings the next values.
    FakeOpenXrControllers::Inputs released;
    headset.SetHandInputs(kRight, released);
    NextFrame(&headset);
    ASSERT_EQ(RunLuauChunk(L, "assert(VRCompanion:getHandState(1).trigger == 0 and VRCompanion:getHandState(1).menu == false)",
                           "=handStateNext"), 0);
}

// ---- In use and the Pointer Hand ----

TEST_F(CompanionModTest, AHandIsInUseWhileTrackedAndMovedWithinAboutThreeSeconds) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    SetGameTime(10000);
    headset.SetHandPose(kRight, HandAt(0.30f, 1.20f, -0.40f));
    headset.SetHandPose(kLeft, HandAt(-0.30f, 1.10f, -0.35f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false") << "tracked but never moved";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(0)"), "false");

    headset.SetHandPose(kRight, HandAt(0.35f, 1.22f, -0.40f));  // 5 cm
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(0)"), "false") << "the other hand did not move";

    AdvanceGameTime(2800);
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true") << "2.8 s after the move";

    AdvanceGameTime(300);
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false") << "3.1 s after the move";

    headset.SetHandPose(kRight, HandAt(0.45f, 1.22f, -0.40f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true") << "moving again puts it back in use";

    headset.SetHandTracked(kRight, false);
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false") << "an untracked hand is never in use";
    EXPECT_EQ(EvalLua(L, "VRCompanion:getHandState(1).tracked"), "false");
}

TEST_F(CompanionModTest, AControllerLyingStillOrJitteringBelowTheMoveThresholdIsNeverInUse) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    SetGameTime(10000);
    headset.SetHandPose(kRight, HandAt(0.30f, 0.70f, -0.40f));
    for (int i = 0; i < 400; ++i) {  // about 6.7 s of frames, with sub-millimetre sensor noise
        const float noise = (i % 2 == 0) ? 0.0003f : -0.0003f;
        headset.SetHandPose(kRight, HandAt(0.30f + noise, 0.70f, -0.40f));
        NextFrame(&headset);
        ASSERT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false") << "frame " << i;
    }
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil");
}

TEST_F(CompanionModTest, ASlowDriftThatAddsUpToARealMoveCountsAsMoving) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    SetGameTime(10000);
    headset.SetHandPose(kRight, HandAt(0.30f, 0.70f, -0.40f));
    NextFrame(&headset);
    for (int i = 1; i <= 20; ++i) {  // 2 mm per frame: 4 cm in total
        headset.SetHandPose(kRight, HandAt(0.30f + 0.002f * i, 0.70f, -0.40f));
        NextFrame(&headset);
    }
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true");
}

TEST_F(CompanionModTest, TurningAControllerInPlaceCountsAsMoving) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    SetGameTime(10000);
    HeadPose pose = HandAt(0.30f, 1.00f, -0.40f);
    headset.SetHandPose(kRight, pose);
    NextFrame(&headset);
    pose.rotation = fs25vr::EulerAngles(0.0f, 0.3f, 0.0f);  // 17 degrees of yaw, same position
    headset.SetHandPose(kRight, pose);
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true");
}

TEST_F(CompanionModTest, ASingleHandInUseIsThePointerHand) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    SetGameTime(10000);
    headset.SetHandPose(kLeft, HandAt(-0.30f, 1.10f, -0.35f));
    headset.SetHandPose(kRight, HandAt(0.30f, 1.20f, -0.40f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil") << "nobody picked a controller up yet";

    headset.SetHandPose(kLeft, HandAt(-0.20f, 1.10f, -0.35f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "0");
    EXPECT_EQ(EvalLua(L, "VRCompanion.pointerHand"), "0") << "the field mirrors getPointerHand()";

    // Once that hand rests for 3 s, nobody points; moving the other hand makes it the Pointer Hand.
    AdvanceGameTime(3100);
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil");
    headset.SetHandPose(kRight, HandAt(0.50f, 1.20f, -0.40f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1");
}

TEST_F(CompanionModTest, WithBothHandsInUseThePointerHandStaysUntilItsHandIsPutDown) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    SetGameTime(10000);
    headset.SetHandPose(kLeft, HandAt(-0.30f, 1.10f, -0.35f));
    headset.SetHandPose(kRight, HandAt(0.30f, 1.20f, -0.40f));
    NextFrame(&headset);

    headset.SetHandPose(kRight, HandAt(0.40f, 1.20f, -0.40f));
    NextFrame(&headset);
    ASSERT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1");

    // The left hand is picked up as well: the Pointer Hand does not jump.
    headset.SetHandPose(kLeft, HandAt(-0.20f, 1.10f, -0.35f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(0) and VRCompanion:isHandInUse(1)"), "true");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1");

    // The right hand is set down while the left keeps moving: the left becomes the Pointer Hand.
    for (int i = 0; i < 4; ++i) {
        AdvanceGameTime(900);
        headset.SetHandPose(kLeft, HandAt(-0.20f + 0.1f * (i + 1), 1.10f, -0.35f));
        NextFrame(&headset);
    }
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "0");
}

TEST_F(CompanionModTest, BothHandsPickedUpTogetherLeaveNoPointerHandUntilOneIsChosen) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    SetGameTime(10000);
    headset.SetHandPose(kLeft, HandAt(-0.30f, 1.10f, -0.35f));
    headset.SetHandPose(kRight, HandAt(0.30f, 1.20f, -0.40f));
    NextFrame(&headset);

    headset.SetHandPose(kLeft, HandAt(-0.20f, 1.10f, -0.35f));
    headset.SetHandPose(kRight, HandAt(0.40f, 1.20f, -0.40f));
    NextFrame(&headset);

    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(0) and VRCompanion:isHandInUse(1)"), "true");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil");
}

TEST_F(CompanionModTest, ARecenterDoesNotMakeAControllerLyingStillLookMoved) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    SetGameTime(10000);
    HeadPose head = HandAt(1.0f, 1.6f, 0.5f);
    headset.SetHeadPose(head);
    headset.SetHandPose(kRight, HandAt(1.3f, 0.7f, 0.2f));  // lying on the desk
    NextFrame(&headset);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false");

    // The Recenter Action moves the origin to the head: every pose, the desk controller's too, shifts.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onRecenterAction()", "=recenter"), 0);
    NextFrame(&headset);

    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil");
    EXPECT_LT(std::abs(std::stof(EvalLua(L, "VRCompanion:getHandState(1).x")) - 0.3f), 1e-4f)
        << "the hand pose did follow the recenter";
}