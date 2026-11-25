#include "hand_tools_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;

constexpr const char* kPlacementHelpers = R"luau(
    function _G.isWhereTheGamePutsIt(tool)
        if getParent(tool.rootNode) ~= foot.cam.pitchNode then return false end
        local x, y, z = getTranslation(tool.rootNode)
        local p = StubHandTool.CAMERA_PLACEMENT
        return math.abs(x - p.x) < 1e-6 and math.abs(y - p.y) < 1e-6 and math.abs(z - p.z) < 1e-6
    end
    function _G.isInTheHand(tool, which) return isBelow(tool.rootNode, handHolder(which)) end
)luau";

}

#define LANCE_IN_THE_RIGHT_HAND() \
    FakeTrackedHeadset headset; \
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f)); \
    fs25vr::VRBridge bridge(headset.manager, nullptr); \
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge)); \
    ASSERT_EQ(RunLuauChunk(L, kPlacementHelpers, "=placementHelpers"), 0); \
    SetGameTime(10000); \
    ASSERT_EQ(RunLuauChunk(L, hand_tools_test::kHoldLance, "=holdLance"), 0); \
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f); \
    FRAMES(1); \
    ASSERT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()")

TEST_F(CompanionModTest, TheLanceHeldOnFootHangsBelowTheRightVirtualHandAndFollowsItAcrossFrames) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    SetGameTime(10000);
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));

    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);

    EXPECT_EQ(EvalLua(L, "isBelow(lance.rootNode, handHolder('right'))"), "true")
        << "the lance's root is linked below the right Virtual Hand: " << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    ASSERT_EQ(RunLuauChunk(L, "_G.before = { offsetInHolder(lance.rootNode, 'right') }; _G.handBefore = { getWorldTranslation(handHolder('right')) }", "=before"), 0);

    MOVE_HAND(kRight, 0.55f, 0.10f, -0.45f);
    FRAMES(1);

    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local hx, hy, hz = getWorldTranslation(handHolder('right'))
        local moved = math.sqrt((hx - handBefore[1]) ^ 2 + (hy - handBefore[2]) ^ 2 + (hz - handBefore[3]) ^ 2)
        assert(moved > 0.3, "the hand moved " .. moved)
        local x, y, z = offsetInHolder(lance.rootNode, 'right')
        assert(math.abs(x - before[1]) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4,
            "the lance stayed where it was in the hand")
    )luau", "=follows"), 0);
}

TEST_F(CompanionModTest, TheVirtualHandHoldingTheLanceIsClosedAroundTheHandleAndItsIndexFingerStillFollowsTheTrigger) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    SetGameTime(10000);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);

    headset.SetHandInputs(kRight, Squeeze(0.0f, 1.0f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, "_G.fullGrip = fingerCurl('right', 'middle'); _G.fullGripRing = fingerCurl('right', 'ring'); _G.fullGripLittle = fingerCurl('right', 'little')", "=gripReference"), 0);
    headset.SetHandInputs(kRight, Squeeze(1.0f, 0.0f));
    FRAMES(1);
    ASSERT_EQ(RunLuauChunk(L, "_G.fullTrigger = fingerCurl('right', 'index')", "=triggerReference"), 0);

    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    headset.SetHandInputs(kRight, Squeeze(0.0f, 0.0f));
    FRAMES(2);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        assert(isBelow(lance.rootNode, handHolder('right')), "the lance is in the hand")
        for segment = 1, 3 do assert(fullGrip[segment] > 0.3, "the reference grip curls the finger") end
        for _, pair in ipairs({ { 'middle', fullGrip }, { 'ring', fullGripRing }, { 'little', fullGripLittle } }) do
            local curl = fingerCurl('right', pair[1])
            for segment = 1, 3 do
                assert(math.abs(curl[segment] - pair[2][segment]) < 1e-3,
                    pair[1] .. segment .. " is closed around the handle: " .. curl[segment] .. " against " .. pair[2][segment])
            end
        end
        local index = fingerCurl('right', 'index')
        for segment = 1, 3 do assert(math.abs(index[segment]) < 1e-3, "the index finger is open with the trigger released") end
    )luau", "=closedOnTheHandle"), 0);

    headset.SetHandInputs(kRight, Squeeze(0.5f, 0.0f));
    FRAMES(1);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local index = fingerCurl('right', 'index')
        for segment = 1, 3 do
            assert(math.abs(index[segment] - fullTrigger[segment] * 0.5) < 1e-3, "the index finger follows the trigger: half pulled")
        end
    )luau", "=indexOnTheTrigger"), 0);
}

TEST_F(CompanionModTest, TheLanceIsWhereTheGamePutsItWhileTheToolHandIsSetDownOrUntrackedAndBackInTheHandWhenItIsUsedAgain) {
    LANCE_IN_THE_RIGHT_HAND();
    auto inTheHand = [&]() { return EvalLua(L, "isInTheHand(lance, 'right')"); };
    auto whereTheGamePutsIt = [&]() { return EvalLua(L, "isWhereTheGamePutsIt(lance)"); };

    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(whereTheGamePutsIt(), "true") << "set down: the game's own attachToolToCamera put the lance in front of the view";
    EXPECT_EQ(inTheHand(), "false");
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_EQ(inTheHand(), "true") << "picked up again: the lance is back in the hand";
    EXPECT_EQ(whereTheGamePutsIt(), "false");

    headset.SetHandTracked(kRight, false);
    FRAMES(1);
    EXPECT_EQ(whereTheGamePutsIt(), "true") << "untracked: the lance is where the game puts it";
    headset.SetHandTracked(kRight, true);
    FRAMES(1);
    EXPECT_EQ(inTheHand(), "true") << "tracked again: back in the hand";

    headset.SetHandGripPose(kRight, HandAt(0.25f, -0.10f, -0.30f), false);
    FRAMES(1);
    EXPECT_EQ(whereTheGamePutsIt(), "true");
    headset.SetHandGripPose(kRight, HandAt(0.25f, -0.10f, -0.30f), true);
    FRAMES(1);
    EXPECT_EQ(inTheHand(), "true");
}

TEST_F(CompanionModTest, AnUnsupportedHandToolIsPlacedExactlyAsTheGamePlacesItAndTheVirtualHandIsEmpty) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kPlacementHelpers, "=placementHelpers"), 0);
    SetGameTime(10000);
    ASSERT_NO_FATAL_FAILURE(HoldOtherModTool(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(otherTool)"), "true") << "the other mod's tool is in front of the view";
    EXPECT_EQ(EvalLua(L, "isInTheHand(otherTool, 'right')"), "false");
    EXPECT_EQ(EvalLua(L, "shown('right')"), "true") << "the Virtual Hand is there as ever";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false");

    ASSERT_EQ(RunLuauChunk(L, "_G.openMiddle = fingerCurl('right', 'middle')", "=openHand"), 0);
    for (int segment = 1; segment <= 3; ++segment) {
        EXPECT_NEAR(Number(L, "openMiddle[" + std::to_string(segment) + "]"), 0.0, 1e-3) << "the middle finger is open";
    }
    headset.SetHandInputs(kRight, Squeeze(0.0f, 1.0f));
    FRAMES(1);
    EXPECT_GT(Number(L, "fingerCurl('right', 'middle')[1]"), 0.3) << "and follows the grip as in an empty hand";
}

TEST_F(CompanionModTest, SwitchingFromTheLanceToAnUnsupportedToolAndBackMovesTheLanceOutOfTheHandAndIntoItAgain) {
    LANCE_IN_THE_RIGHT_HAND();
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(lance); _G.otherTool = StubHandTool.otherModTool(foot.player); StubHandTool.hold(otherTool)", "=switchToOtherModTool"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(otherTool)"), "true");
    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "false") << "the lance put away is not in the hand";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false");

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(otherTool); StubHandTool.hold(lance)", "=switchBack"), 0);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "true") << "the lance taken again goes into the hand";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true");
}

constexpr const char* kRemotePlayerHelpers = R"luau(
    -- another player of the session: not the local one, with a camera of their own, holding a lance in the game's way
    function _G.holdRemoteLance()
        local pitch = StubScene.newNode("remotePitch", 0, 4, 1.7, 4)
        local remote = setmetatable({ isOwner = false, camera = { isFirstPerson = true, pitchNode = pitch } }, { __index = _G.Player })
        _G.remoteLance = StubHandTool.makeTool(remote, "remoteLance", "spec_highPressureWasherLance")
        local spec = remoteLance.spec_highPressureWasherLance
        spec.raycastNode = StubScene.newNode("remoteNozzle", remoteLance.rootNode, 0, 0, 0.5)
        remoteLance.isHeld = true
        remoteLance.carryingPlayer = remote
        remoteLance:attachTool()
    end
    function _G.remoteLanceIsWhereTheGamePutsIt()
        local p = StubHandTool.CAMERA_PLACEMENT
        local x, y, z = getTranslation(remoteLance.rootNode)
        return getParent(remoteLance.rootNode) == remoteLance.carryingPlayer.camera.pitchNode
            and math.abs(x - p.x) < 1e-6 and math.abs(y - p.y) < 1e-6 and math.abs(z - p.z) < 1e-6
    end
)luau";

TEST_F(CompanionModTest, AnotherPlayersLanceIsNeverTakenIntoTheToolHandAndStaysWhereTheGamePutsIt) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kRemotePlayerHelpers, "=remotePlayerHelpers"), 0);
    SetGameTime(10000);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    ASSERT_EQ(RunLuauChunk(L, "holdRemoteLance()", "=holdRemoteLance"), 0);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "remoteLanceIsWhereTheGamePutsIt()"), "true");
    EXPECT_EQ(EvalLua(L, "isBelow(remoteLance.rootNode, handHolder('right'))"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false");

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "#StubHandTool.useCalls"), "0") << "the Tool Hand's trigger does not use another player's lance";
}

TEST_F(CompanionModTest, AnotherPlayersToolAttachedWhileTheLocalPlayerHoldsTheLanceLeavesTheLocalLanceInTheHand) {
    LANCE_IN_THE_RIGHT_HAND();
    ASSERT_EQ(RunLuauChunk(L, kRemotePlayerHelpers, "=remotePlayerHelpers"), 0);
    ASSERT_EQ(RunLuauChunk(L, "holdRemoteLance()", "=holdRemoteLance"), 0);
    FRAMES(3);

    EXPECT_EQ(EvalLua(L, "remoteLanceIsWhereTheGamePutsIt()"), "true") << "the other player's lance is the game's";
    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "true") << "and ours stays in the Tool Hand";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true");

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "lance.spec_highPressureWasherLance.isActivated"), "true") << "the trigger washes with our lance";
    EXPECT_EQ(EvalLua(L, "remoteLance.spec_highPressureWasherLance.isActivated"), "nil") << "and not with theirs";
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        assert(#StubHandTool.useCalls > 0)
        for _, call in ipairs(StubHandTool.useCalls) do assert(call.self == lance, "a use of " .. tostring(call.tool)) end
    )luau", "=onlyOurLance"), 0);
}

TEST_F(CompanionModTest, InThirdPersonTheLanceIsInTheGamesPlayerHandAndBackInTheToolHandInFirstPerson) {
    LANCE_IN_THE_RIGHT_HAND();

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.thirdPerson(foot.player, true)", "=thirdPerson"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "getParent(lance.rootNode) == foot.player.handNode"), "true") << "in the game's third person hand";
    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "false");

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.thirdPerson(foot.player, false)", "=firstPerson"), 0);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "true");
}

TEST_F(CompanionModTest, WhileAMenuIsOpenTheLanceIsWhereTheGamePutsItAndBackInTheHandWhenItCloses) {
    LANCE_IN_THE_RIGHT_HAND();

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(lance)"), "true");

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "true");
}

TEST_F(CompanionModTest, SwitchingVrHandToolsOffMovesTheLanceOutOfTheHandAtOnceAndOnAgainPutsItBack) {
    LANCE_IN_THE_RIGHT_HAND();

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(lance)"), "true") << "off: the lance is where the game puts it";
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(lance)"), "true") << "and stays there however the hand moves";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(lance); StubHandTool.hold(lance)", "=takeAgain"), 0);
    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(lance)"), "true");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "true") << "on again: back in the hand";
}

TEST_F(CompanionModTest, WithMotionControllersOffTheLanceIsWhereTheGamePutsItAndComesBackWhenTheyAreOnAgain) {
    LANCE_IN_THE_RIGHT_HAND();

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(lance)"), "true");
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(lance); StubHandTool.hold(lance)", "=takeAgain"), 0);
    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(lance)"), "true") << "taken with Motion Controllers off: the game's placement";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'on')", "=on"), 0);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "true");
}

TEST_F(CompanionModTest, WhenTheInjectorOffersNoHandStatesTheLanceGoesBackToWhereTheGamePutsIt) {
    LANCE_IN_THE_RIGHT_HAND();

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.getHandState = nil", "=noHandStates"), 0);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(lance)"), "true");
}

#define HAND_TOOLS_GIVE_UP_WHEN_ATTACH_TO_CAMERA_IS(gameVariant) \
    do { \
        FakeTrackedHeadset headset; \
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f)); \
        fs25vr::VRBridge bridge(headset.manager, nullptr); \
        StartOptions options; \
        options.variant = (gameVariant); \
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options)); \
        ASSERT_EQ(RunLuauChunk(L, kPlacementHelpers, "=placementHelpers"), 0); \
        SetGameTime(10000); \
        ASSERT_EQ(RunLuauChunk(L, hand_tools_test::kHoldLance, "=holdLance"), 0); \
        \
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f); \
        FRAMES(3); \
        \
        EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(lance)"), "true") << "the game's Hand Tool works unchanged"; \
        EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "false"); \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "one warning says why"; \
        ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(lance); StubHandTool.hold(lance)", "=takeAgain"), 0); \
        PICK_UP(kRight, 0.30f, -0.10f, -0.30f); \
        FRAMES(3); \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "and it is not repeated"; \
        EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(lance)"), "true"); \
        EXPECT_NE(EvalLua(L, "VRCompanion:getHandToolsDiagnostics()").find("attachToolToCamera wrap: gave up"), std::string::npos) \
            << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()"); \
    } while (0)

TEST_F(CompanionModTest, WithAttachToolToCameraMissingTheGamesHandToolsWorkUnchangedAndTheLogHasOneWarning) {
    HAND_TOOLS_GIVE_UP_WHEN_ATTACH_TO_CAMERA_IS("missing");
    EXPECT_EQ(EvalLua(L, "HandTool.attachToolToCamera"), "nil") << "the class is as the game made it";
}

TEST_F(CompanionModTest, WithAttachToolToCameraReshapedTheGamesHandToolsWorkUnchangedAndTheLogHasOneWarning) {
    HAND_TOOLS_GIVE_UP_WHEN_ATTACH_TO_CAMERA_IS("reshaped");
    EXPECT_EQ(EvalLua(L, "HandTool.attachToolToCamera == StubHandTool.originalAttachToolToCamera"), "true")
        << "the class function was never wrapped";
}

TEST_F(CompanionModTest, UnloadingTheMapPutsTheLanceBackAndTakesTheWrapOffTheClass) {
    LANCE_IN_THE_RIGHT_HAND();
    ASSERT_EQ(EvalLua(L, "HandTool.attachToolToCamera == StubHandTool.originalAttachToolToCamera"), "false") << "wrapped while the map is loaded";

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);

    EXPECT_EQ(EvalLua(L, "HandTool.attachToolToCamera == StubHandTool.originalAttachToolToCamera"), "true") << "the class has its own function back";
    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(lance)"), "true") << "and the lance is not left hanging below a Virtual Hand that is deleted";
    EXPECT_EQ(EvalLua(L, "registeredCommands['vrHandTools']"), "nil");
    EXPECT_EQ(EvalLua(L, "registeredCommands['vrHandToolsDiagnostics']"), "nil");
}

TEST_F(CompanionModTest, AnotherModsLaterWrapOfAttachToolToCameraIsNeverUndone) {
    LANCE_IN_THE_RIGHT_HAND();
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local inner = HandTool.attachToolToCamera
        _G.theirCalls = 0
        _G.theirWrap = function(...) _G.theirCalls = _G.theirCalls + 1 return inner(...) end
        HandTool.attachToolToCamera = _G.theirWrap
    )luau", "=anotherModWraps"), 0);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);

    EXPECT_EQ(EvalLua(L, "HandTool.attachToolToCamera == theirWrap"), "true") << "the other mod's wrap is still the class function";
}

TEST_F(CompanionModTest, TheHandToolWrapIsInstalledAgainWhenTheNextMapLoads) {
    LANCE_IN_THE_RIGHT_HAND();
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap(); VRCompanion:loadMap('y')", "=reloadMap"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(lance); StubHandTool.hold(lance)", "=takeAgain"), 0);
    PICK_UP(kRight, 0.30f, -0.10f, -0.30f);
    FRAMES(1);

    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
}

TEST_F(CompanionModTest, VrHandToolsDiagnosticsLogsTheToolHandTheHeldHandToolWhetherItIsInTheHandAndWhereTheLookRayStarts) {
    LANCE_IN_THE_RIGHT_HAND();
    s_capturedLogs.clear();

    const std::string reply = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");

    for (const char* expected : { "Hand Tools: on", "Tool Hand: right", "held Hand Tool: lance (supported)", "in the Tool Hand: yes",
                                  "look ray starts at (", "attachToolToCamera wrap: installed" }) {
        EXPECT_NE(reply.find(expected), std::string::npos) << "missing from the reply: " << expected << "\n" << reply;
        EXPECT_EQ(CountLogLines(std::string("[FS25_VR] ") + expected), 1) << "and from the log: " << expected;
    }

    AdvanceGameTime(3100);
    FRAMES(1);
    const std::string setDown = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_NE(setDown.find("in the Tool Hand: no (the Tool Hand is not in use)"), std::string::npos) << setDown;

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(lance); _G.otherTool = StubHandTool.otherModTool(foot.player); StubHandTool.hold(otherTool)", "=otherTool"), 0);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    const std::string unsupported = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_NE(unsupported.find("held Hand Tool: unsupported"), std::string::npos) << unsupported;
    EXPECT_NE(unsupported.find("in the Tool Hand: no (the Hand Tool is not supported)"), std::string::npos) << unsupported;

    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("Tool Hand: right"), std::string::npos);
}

TEST_F(CompanionModTest, VrHandToolsWithoutAnArgumentSaysWhetherItIsOnAndAWrongArgumentSaysHowToUseIt) {
    LANCE_IN_THE_RIGHT_HAND();
    EXPECT_NE(EvalLua(L, "runCommand('vrHandTools')").find("Hand Tools are on"), std::string::npos);
    EXPECT_NE(EvalLua(L, "runCommand('vrHandTools', 'sideways')").find("Usage: vrHandTools"), std::string::npos);
    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'right')"), "true") << "a wrong argument changes nothing";
}

TEST_F(CompanionModTest, VrHandToolsIsSavedAcrossSessions) {
    StartOptions saving;
    const std::string xmlStubs = StubChunk("xml_settings");
    saving.extraStubs = xmlStubs.c_str();
    std::string saved;
    {
        FakeTrackedHeadset headset;
        fs25vr::VRBridge bridge(headset.manager, nullptr);
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, saving));
        EXPECT_EQ(EvalLua(L, "VRCompanion.handToolsEnabled"), "true") << "on by default";
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
        saved = EvalLua(L, "savedXml['vrSettings.handTools']");
    }
    ASSERT_EQ(saved, "false") << "the setting is written to the settings file";

    lua_close(L);
    L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcclosure(L, &MockPrint, "print", 0);
    lua_setglobal(L, "print");
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("console"), "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, StubChunk("xml_settings"), "=xmlStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "savedXml['vrSettings.handTools'] = false; VRCompanion:loadMap('x')", "=loadMapAgain"), 0);
    EXPECT_EQ(EvalLua(L, "VRCompanion.handToolsEnabled"), "false");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    EXPECT_EQ(EvalLua(L, "savedXml['vrSettings.handTools']"), "true");
}

TEST_F(CompanionModTest, ALeftMotionControllerPickedUpDoesNotTakeTheLance) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kPlacementHelpers, "=placementHelpers"), 0);
    SetGameTime(10000);
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));

    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    FRAMES(1);

    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(lance)"), "true");
    EXPECT_EQ(EvalLua(L, "isInTheHand(lance, 'left')"), "false");
}

TEST_F(CompanionModTest, TheLanceInTheHandIsCarriedAlongByAFrameRenderedWithoutAnUpdateAfterThePlayerMoved) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    SetGameTime(10000);
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "isBelow(lance.rootNode, handHolder('right'))"), "true");
    ASSERT_EQ(RunLuauChunk(L, "_G.before = { getWorldTranslation(lance.rootNode) }", "=before"), 0);

    ASSERT_EQ(RunLuauChunk(L, "setTranslation(foot.parent, 0.12, 0, 0.05); StubScene.updateCamera()", "=moveWithoutUpdate"), 0);

    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = getWorldTranslation(lance.rootNode)
        assert(math.abs(x - before[1] - 0.12) < 1e-3 and math.abs(y - before[2]) < 1e-3 and math.abs(z - before[3] - 0.05) < 1e-3,
            string.format("the lance moved by %.3f %.3f %.3f", x - before[1], y - before[2], z - before[3]))
    )luau", "=carriedAlong"), 0);
}
