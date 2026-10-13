// Motion Controller state in the Companion Mod (spec #71), seen from outside: the real
// Companion Mod runs against the real VRBridge over FakeTrackedHeadset, the test moves the fake
// Motion Controllers and drives frames and the game clock, and asserts what the mod reports and what the
// Injector's OpenXR layer was asked to do. Other Motion Controller features have their own files next
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

// Stubs of the engine's XML settings API that remember what was saved, so a "session" can be
// replayed into a fresh Lua state.
constexpr const char* kXmlStubs = R"luau(
    _G.savedXml = _G.savedXml or {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function() return true end
    _G.createXMLFile = function() return 1 end
    _G.setXMLFloat = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLBool = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLString = function(file, key, value) _G.savedXml[key] = value end
    _G.saveXMLFile = function() return true end
    _G.delete = function() end
    _G.loadXMLFile = function() return 2 end
    _G.getXMLFloat = function(file, key) local v = _G.savedXml[key]; return type(v) == "number" and v or nil end
    _G.getXMLBool = function(file, key) local v = _G.savedXml[key]; if type(v) == "boolean" then return v end return nil end
    _G.getXMLString = function(file, key) local v = _G.savedXml[key]; return type(v) == "string" and v or nil end
)luau";

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
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil") << "nobody picked a Motion Controller up yet";

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

    // The Recenter Action moves the origin to the head: every pose, the desk Motion Controller's too, shifts.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onRecenterAction()", "=recenter"), 0);
    NextFrame(&headset);

    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil");
    EXPECT_LT(std::abs(std::stof(EvalLua(L, "VRCompanion:getHandState(1).x")) - 0.3f), 1e-4f)
        << "the hand pose did follow the recenter";
}
// ---- vrMotionControllers and the status command ----

TEST_F(CompanionModTest, MotionControllersAreOnByDefaultAndTheConsoleCommandsAreRegisteredAndRemoved) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));

    EXPECT_EQ(EvalLua(L, "VRCompanion.motionControllersEnabled"), "true");
    EXPECT_EQ(EvalLua(L, "registeredCommands['vrMotionControllers'] ~= nil"), "true");
    EXPECT_EQ(EvalLua(L, "registeredCommands['vrMotionControllerStatus'] ~= nil"), "true");
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllers')").find("on"), std::string::npos)
        << "without an argument the command reports the current setting";

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "removedCommands['vrMotionControllers']"), "true");
    EXPECT_EQ(EvalLua(L, "removedCommands['vrMotionControllerStatus']"), "true");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil");
}

TEST_F(CompanionModTest, VrMotionControllersOffIgnoresControllerInputWithoutTouchingTheSession) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    SetGameTime(10000);
    FakeOpenXrControllers::Inputs pulled;
    pulled.trigger = 1.0f;
    headset.SetHandPose(kRight, HandAt(0.30f, 1.20f, -0.40f));
    headset.SetHandInputs(kRight, pulled);
    NextFrame(&headset);
    headset.SetHandPose(kRight, HandAt(0.40f, 1.20f, -0.40f));
    NextFrame(&headset);
    ASSERT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1");
    const auto& controllers = headset.runtime->controllers;
    ASSERT_EQ(controllers.attachCalls, 1u);

    const std::string reply = EvalLua(L, "runCommand('vrMotionControllers', 'off')");
    EXPECT_NE(reply.find("off"), std::string::npos) << reply;

    headset.SetHandPose(kRight, HandAt(0.50f, 1.20f, -0.40f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion.motionControllersEnabled"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getHandState(1).tracked"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getHandState(1).trigger"), "0");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil");
    EXPECT_EQ(EvalLua(L, "VRCompanion.pointerHand"), "nil");

    // The session, its action set and its attach are untouched: the setting lives in the mod only.
    EXPECT_EQ(controllers.attachCalls, 1u);
    EXPECT_EQ(controllers.actionSetNames.size(), 1u);
    EXPECT_EQ(controllers.destroyActionSetCalls, 0u);
    EXPECT_TRUE(headset.manager->HasMotionControllerActions());

    // Switching back on picks the Motion Controllers up again.
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllers', 'on')").find("on"), std::string::npos);
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getHandState(1).tracked"), "true");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil") << "a hand is in use once it moves after being picked up again";
    headset.SetHandPose(kRight, HandAt(0.60f, 1.20f, -0.40f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1");
    EXPECT_EQ(controllers.attachCalls, 1u);
}

TEST_F(CompanionModTest, TheActionSetIsAttachedWhateverVrMotionControllersSays) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "savedXml['vrSettings.motionControllers'] = false; VRCompanion:loadMap('x')", "=loadMap"), 0);

    EXPECT_EQ(EvalLua(L, "VRCompanion.motionControllersEnabled"), "false") << "the saved setting is off";
    EXPECT_EQ(headset.runtime->controllers.attachCalls, 1u) << "the Injector attached it when the session was created";
    EXPECT_EQ(headset.runtime->controllers.actionSetNames.size(), 1u);
}

TEST_F(CompanionModTest, VrMotionControllersIsSavedAcrossSessions) {
    std::string saved;
    {
        FakeTrackedHeadset headset;
        fs25vr::VRBridge bridge(headset.manager, nullptr);
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        EXPECT_EQ(EvalLua(L, "VRCompanion.motionControllersEnabled"), "true");

        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
        saved = EvalLua(L, "savedXml['vrSettings.motionControllers']");
    }
    ASSERT_EQ(saved, "false") << "the setting is written to the settings file";

    // A fresh game session reads it back.
    lua_close(L);
    L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcclosure(L, &MockPrint, "print", 0);
    lua_setglobal(L, "print");
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "savedXml['vrSettings.motionControllers'] = false; VRCompanion:loadMap('x')", "=loadMapAgain"), 0);
    EXPECT_EQ(EvalLua(L, "VRCompanion.motionControllersEnabled"), "false");

    // And "on" is saved too.
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'on')", "=on"), 0);
    EXPECT_EQ(EvalLua(L, "savedXml['vrSettings.motionControllers']"), "true");
}

TEST_F(CompanionModTest, VrMotionControllersRejectsAnUnknownArgumentAndKeepsTheSetting) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));

    const std::string reply = EvalLua(L, "runCommand('vrMotionControllers', 'maybe')");
    EXPECT_NE(reply.find("Usage"), std::string::npos) << reply;
    EXPECT_EQ(EvalLua(L, "VRCompanion.motionControllersEnabled"), "true");
}

TEST_F(CompanionModTest, TheStatusCommandReportsEachHandsTrackedAndInUseStateAndThePointerHand) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    SetGameTime(10000);
    headset.SetHandPose(kLeft, HandAt(-0.30f, 1.10f, -0.35f), /*tracked=*/false);
    headset.SetHandPose(kRight, HandAt(0.30f, 1.20f, -0.40f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "runCommand('vrMotionControllerStatus')"),
              "Motion Controllers: on | Left: not tracked, not in use | Right: tracked, not in use | Pointer Hand: none");

    headset.SetHandPose(kRight, HandAt(0.40f, 1.20f, -0.40f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "runCommand('vrMotionControllerStatus')"),
              "Motion Controllers: on | Left: not tracked, not in use | Right: tracked, in use | Pointer Hand: right");

    headset.SetHandPose(kLeft, HandAt(-0.30f, 1.10f, -0.35f));
    NextFrame(&headset);
    headset.SetHandPose(kLeft, HandAt(-0.20f, 1.10f, -0.35f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "runCommand('vrMotionControllerStatus')"),
              "Motion Controllers: on | Left: tracked, in use | Right: tracked, in use | Pointer Hand: right");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "runCommand('vrMotionControllerStatus')"),
              "Motion Controllers: off (Motion Controller input is ignored) | Left: ignored | Right: ignored | Pointer Hand: none");
}

// ---- Nothing to track ----

TEST_F(CompanionModTest, ARuntimeThatRefusesTheActionSetLeavesHeadTrackingAndTheModUnchanged) {
    FakeTrackedHeadset headset(fs25vr::FakeTrackedHeadsetOptions{ /*refuseActionSet=*/true });
    fs25vr::HeadPose head = HandAt(0.1f, 1.6f, -0.2f);
    headset.SetHeadPose(head);
    headset.SetHandPose(kRight, HandAt(0.30f, 1.20f, -0.40f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    SetGameTime(10000);

    for (int i = 0; i < 3; ++i) {
        headset.SetHandPose(kRight, HandAt(0.30f + 0.1f * i, 1.20f, -0.40f));
        NextFrame(&headset);
    }

    EXPECT_FALSE(headset.manager->HasMotionControllerActions());
    EXPECT_EQ(EvalLua(L, "VRCompanion:getHandState(0).tracked"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getHandState(1).tracked"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil");
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("Right: not tracked, not in use"), std::string::npos);

    // Head tracking and presentation are as before.
    EXPECT_EQ(EvalLua(L, "VRBridge.getHeadPose().isValid"), "true");
    EXPECT_NEAR(std::stof(EvalLua(L, "VRBridge.getHeadPose().y")), 1.6f, 1e-5f);
    EXPECT_TRUE(headset.manager->IsSessionRunning());
    EXPECT_EQ(headset.runtime->controllers.actionSetsAttached, 0u);
}

TEST_F(CompanionModTest, AnInjectorWithoutHandStateLeavesTheModQuietAndAllHandsUntracked) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, headset, bridge));
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.getHandState = nil", "=olderInjector"), 0);
    SetGameTime(10000);
    headset.SetHandPose(kRight, HandAt(0.30f, 1.20f, -0.40f));

    NextFrame(&headset);
    headset.SetHandPose(kRight, HandAt(0.60f, 1.20f, -0.40f));
    NextFrame(&headset);

    EXPECT_EQ(EvalLua(L, "VRCompanion:getHandState(1).tracked"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false");
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("not available"), std::string::npos);
}

TEST_F(CompanionModTest, WithoutAnInjectorTheModKeepsRunningAndNoHandIsTracked) {
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
    SetGameTime(10000);

    NextFrame();

    EXPECT_EQ(EvalLua(L, "VRCompanion:getHandState(0).tracked"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "nil");
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("not available"), std::string::npos);
}