// Object Grab, the wrapped game functions and the saved setting (ADR-0033, spec #156), seen from outside: the real Companion Mod runs
// against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (the game's empty hands in
// object_grab_test_support.h, in the "game versions" the stub can be in). A function the wraps expect that is missing or has another
// shape turns Object Grab off with ONE warning and leaves Hand Tools alone, and Hand Tools giving up leaves Object Grab aiming. Prior
// art: test_hand_tools_look_ray.cpp (give-up and unload of the look ray wrap) and test_wheel_grab_steering_wrap.cpp.

#include "object_grab_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;

}  // namespace

#define OBJECT_GRAB_START_WITH(startOptions, handsVariant) \
    FakeTrackedHeadset headset; \
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f)); \
    fs25vr::VRBridge bridge(headset.manager, nullptr); \
    ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, (startOptions), (handsVariant))); \
    SetGameTime(10000)

// ---- Giving up ----------------------------------------------------------------------------------------------------------------

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
    // A spray can whose draw is reshaped: Hand Tools give up at map load, with their own warning.
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

// ---- Unloading the map -----------------------------------------------------------------------------------------------------

TEST_F(CompanionModTest, UnloadingTheMapGivesTheLookRayAndTheHandsDrawBackToTheGame) {
    OBJECT_GRAB_START_WITH(StartOptions(), "ok");
    ASSERT_EQ(EvalLua(L, "tostring(Player.getLookRay == StubHandTool.originalGetLookRay)"), "false") << "wrapped while the map is loaded";
    ASSERT_EQ(EvalLua(L, "tostring(HandToolHands.onDraw == StubHandTool.originalHandsOnDraw)"), "false");

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "tostring(Player.getLookRay == StubHandTool.originalGetLookRay)"), "true");
    EXPECT_EQ(EvalLua(L, "tostring(HandToolHands.onDraw == StubHandTool.originalHandsOnDraw)"), "true");
}

// ---- The setting and its status phrase -------------------------------------------------------------------------------------

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
        options.extraStubs = kXmlStubs;
        OBJECT_GRAB_START_WITH(options, "ok");
        EXPECT_EQ(EvalLua(L, "VRCompanion.objectGrabEnabled"), "true") << "on by default";
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrObjectGrab', 'off')", "=off"), 0);
        saved = EvalLua(L, "savedXml['vrSettings.objectGrab']");
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
    ASSERT_EQ(RunLuauChunk(L, "savedXml['vrSettings.objectGrab'] = false; VRCompanion:loadMap('x')", "=loadMapAgain"), 0);
    EXPECT_EQ(EvalLua(L, "VRCompanion.objectGrabEnabled"), "false");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrObjectGrab', 'on')", "=on"), 0);
    EXPECT_EQ(EvalLua(L, "savedXml['vrSettings.objectGrab']"), "true");
}

