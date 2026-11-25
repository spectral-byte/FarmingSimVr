#include "object_grab_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;

}

#define OBJECT_GRAB_START_WITH(startOptions, handsVariant) \
    FakeTrackedHeadset headset; \
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f)); \
    fs25vr::VRBridge bridge(headset.manager, nullptr); \
    ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, (startOptions), (handsVariant))); \
    SetGameTime(10000)

TEST_F(CompanionModTest, AHandsToolFunctionThatIsMissingOrReshapedTurnsObjectGrabOffWithOneWarningAndTheGamesDrawComesBack) {
    for (const char* variant : { "drawMissing", "drawReshaped", "canPickUpReshaped" }) {
        SCOPED_TRACE(variant);
        s_capturedLogs.clear();
        lua_close(L);
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcclosure(L, &MockPrint, "print", 0);
        lua_setglobal(L, "print");
        OBJECT_GRAB_START_WITH(StartOptions(), variant);

        POINT_HAND(kRight, kHandX, kHandY, kHandZ);
        FRAMES(3);
        EXPECT_EQ(CountLogLines("[Warning] Object Grab disabled"), 1) << "once, however many frames";
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 0) << "Hand Tools are not affected";
        EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "the head's ray: the hand does not aim";
        EXPECT_EQ(EvalLua(L, "tostring(HandToolHands.onDraw == StubHandTool.originalHandsOnDraw)"), "true") << "the game's draw is the class function";
        EXPECT_NE(EvalLua(L, "VRCompanion:getObjectGrabDiagnostics()").find("gave up"), std::string::npos);
    }
}

TEST_F(CompanionModTest, AGetLookRayThatIsMissingOrReshapedTurnsObjectGrabOffWithOneWarningAndHandToolsToo) {
    for (const char* lookRayVariant : { "missing", "reshaped" }) {
        SCOPED_TRACE(lookRayVariant);
        s_capturedLogs.clear();
        lua_close(L);
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcclosure(L, &MockPrint, "print", 0);
        lua_setglobal(L, "print");
        StartOptions options;
        options.lookRayVariant = lookRayVariant;
        OBJECT_GRAB_START_WITH(options, "ok");

        POINT_HAND(kRight, kHandX, kHandY, kHandZ);
        EXPECT_EQ(CountLogLines("[Warning] Object Grab disabled"), 1);
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools disabled"), 1) << "as before: the wrap is theirs too";
   }
}

TEST_F(CompanionModTest, ObjectGrabGivingUpLeavesHandToolsWrappedAndInTheToolHand) {
    OBJECT_GRAB_START_WITH(StartOptions(), "drawMissing");
    ASSERT_EQ(CountLogLines("[Warning] Object Grab disabled"), 1);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(hands)", "=putAwayTheHands"), 0);
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), { StubHandTool.nozzleRay(lance) })"), "true") << "the tool's ray: Hand Tools still aim";
    EXPECT_EQ(EvalLua(L, "tostring(HandTool.attachToolToCamera == StubHandTool.originalAttachToolToCamera)"), "false") << "and are still wrapped";
    EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 0);
}

TEST_F(CompanionModTest, HandToolsGivingUpLeavesTheHandAimingTheEmptyHands) {
    const std::string sprayCan = SprayCanStubs("reshaped");
    StartOptions options;
    options.extraStubs = sprayCan.c_str();
    OBJECT_GRAB_START_WITH(options, "ok");
    ASSERT_EQ(CountLogLines("[Warning] Hand Tools disabled"), 1);

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), pulledBack(pointerRay(), 0.6))"), "true")
        << "the hand still aims: " << EvalLua(L, "rayText(targeterRay())");
    EXPECT_EQ(CountLogLines("[Warning] Object Grab"), 0);
    EXPECT_NE(EvalLua(L, "VRCompanion:getHandToolsDiagnostics()").find("getLookRay wrap: gave up"), std::string::npos)
        << "Hand Tools say they gave the wrap up";
}

TEST_F(CompanionModTest, UnloadingTheMapGivesTheLookRayAndTheHandsDrawBackToTheGame) {
    OBJECT_GRAB_START_WITH(StartOptions(), "ok");
    ASSERT_EQ(EvalLua(L, "tostring(Player.getLookRay == StubHandTool.originalGetLookRay)"), "false") << "wrapped while the map is loaded";
    ASSERT_EQ(EvalLua(L, "tostring(HandToolHands.onDraw == StubHandTool.originalHandsOnDraw)"), "false");

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "tostring(Player.getLookRay == StubHandTool.originalGetLookRay)"), "true");
    EXPECT_EQ(EvalLua(L, "tostring(HandToolHands.onDraw == StubHandTool.originalHandsOnDraw)"), "true");
}

TEST_F(CompanionModTest, VrObjectGrabReportsItsStatusAndTakesOnOrOffAndNothingElse) {
    OBJECT_GRAB_START_WITH(StartOptions(), "ok");
    EXPECT_EQ(EvalLua(L, "runCommand('vrObjectGrab')"), "Object Grab is on. Usage: vrObjectGrab <on|off> (saved)");
    EXPECT_EQ(EvalLua(L, "runCommand('vrObjectGrab', 'off')"), "Object Grab set to: off (Saved)");
    EXPECT_EQ(EvalLua(L, "runCommand('vrObjectGrab', 'status')"), "Object Grab is off. Usage: vrObjectGrab <on|off> (saved)");
    EXPECT_EQ(EvalLua(L, "runCommand('vrObjectGrab', 'maybe')"), "Usage: vrObjectGrab <on|off>");
    EXPECT_EQ(EvalLua(L, "runCommand('vrObjectGrab', 'on')"), "Object Grab set to: on (Saved)");
    EXPECT_EQ(EvalLua(L, "VRCompanion.motionControllersEnabled"), "true") << "the Motion Controllers stay on";
}

TEST_F(CompanionModTest, VrObjectGrabIsSavedAcrossSessions) {
    std::string saved;
    {
        StartOptions options;
        const std::string xmlStubs = StubChunk("xml_settings");
        options.extraStubs = xmlStubs.c_str();
        OBJECT_GRAB_START_WITH(options, "ok");
        EXPECT_EQ(EvalLua(L, "VRCompanion.objectGrabEnabled"), "true") << "on by default";
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrObjectGrab', 'off')", "=off"), 0);
        saved = EvalLua(L, "savedXml['vrSettings.objectGrab']");
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
    ASSERT_EQ(RunLuauChunk(L, "savedXml['vrSettings.objectGrab'] = false; VRCompanion:loadMap('x')", "=loadMapAgain"), 0);
    EXPECT_EQ(EvalLua(L, "VRCompanion.objectGrabEnabled"), "false");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrObjectGrab', 'on')", "=on"), 0);
    EXPECT_EQ(EvalLua(L, "savedXml['vrSettings.objectGrab']"), "true");
}

