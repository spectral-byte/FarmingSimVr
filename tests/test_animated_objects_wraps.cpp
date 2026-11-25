#include "animated_object_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace animated_object_test;

class VirtualGamepadAnimatedObjectsWrapsTest : public VirtualGamepadAnimatedObjectsTest {
protected:
    void ExpectTheSupportGivesUpWith(const char* variant) {
        SCOPED_TRACE(variant);
        ASSERT_NO_FATAL_FAILURE(StartWithDoor(variant));
        EnterZone();
        POINT_HAND(kRight, kHandX, kHandY, kHandZ);
        FRAMES(3);
        EXPECT_EQ(Warnings(), 1) << "one warning, however many frames";
        EXPECT_EQ(DrawnEnd(), "none") << "nothing is selected";
        PullTrigger(kRight);
        EXPECT_EQ(Toggles(), 0) << "and a pull toggles nothing";
        EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger is a binding's";
        EXPECT_EQ(EvalLua(L, "tostring(StubAnimatedObject.inZone(door))"), "true") << "the game's callback works: the prompt can show the door";
        ASSERT_EQ(RunLuauChunk(L, "StubAnimatedObject.showPrompt(door); StubAnimatedObject.pressButton(door)", "=button"), 0);
        EXPECT_EQ(Toggles(), 1) << "and the bound button toggles";
        EXPECT_EQ(EvalLua(L, "tostring(AnimatedObject.triggerCallback == StubAnimatedObject.originalTriggerCallback)"), "true")
            << "the game's functions are as the game has them";
        EXPECT_NE(EvalLua(L, "VRCompanion:getAnimatedObjectsDiagnostics()").find("gave up"), std::string::npos);
        EXPECT_EQ(Warnings(), 1);
    }
};

}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, ATriggerCallbackThatIsMissingTurnsTheSupportOffWithOneWarning) {
    ExpectTheSupportGivesUpWith("triggerCallbackMissing");
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, ATriggerCallbackThatIsReshapedTurnsTheSupportOffWithOneWarning) {
    ExpectTheSupportGivesUpWith("triggerCallbackReshaped");
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, AToggleHandlerThatIsMissingTurnsTheSupportOffWithOneWarning) {
    ExpectTheSupportGivesUpWith("toggleMissing");
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, AToggleHandlerThatIsReshapedTurnsTheSupportOffWithOneWarning) {
    ExpectTheSupportGivesUpWith("toggleReshaped");
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, ACanBeTriggeredCheckThatIsMissingTurnsTheSupportOffWithOneWarning) {
    ExpectTheSupportGivesUpWith("canBeTriggeredMissing");
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, ACanBeTriggeredCheckThatIsReshapedTurnsTheSupportOffWithOneWarning) {
    ExpectTheSupportGivesUpWith("canBeTriggeredReshaped");
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, AGameWithoutAnimatedObjectsAtAllWarnsOnceAndLeavesTheRestAlone) {
    ASSERT_NO_FATAL_FAILURE(Start());
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(3);
    EXPECT_EQ(Warnings(), 1);
    EXPECT_NE(EvalLua(L, "VRCompanion:getAnimatedObjectsDiagnostics()").find("gave up"), std::string::npos);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1") << "the Pointer Hand itself is unaffected";
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, NoWarningWhileNoMotionControllerIsTrackedOrWhileMotionControllersAreOff) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("triggerCallbackMissing"));
    headset.SetHandTracked(kLeft, false);
    headset.SetHandTracked(kRight, false);
    FRAMES(5);
    EXPECT_EQ(Warnings(), 0) << "a player without Motion Controllers is never told about Animated Objects";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
    headset.SetHandTracked(kRight, true);
    FRAMES(5);
    EXPECT_EQ(Warnings(), 0) << "with Motion Controllers off the support is never looked at";
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, UnloadingTheMapHandsTheGamesClassBackExactlyAsFound) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    ASSERT_EQ(EvalLua(L, "tostring(AnimatedObject.triggerCallback == StubAnimatedObject.originalTriggerCallback)"), "false")
        << "wrapped while the map is loaded";
    ASSERT_EQ(EvalLua(L, "tostring(rawget(AnimatedMapObject, 'triggerCallback'))"), "nil") << "the subclass is reached through the class";
    ASSERT_EQ(RunLuauChunk(L, "_G.canBeTriggered = AnimatedObject.getCanBeTriggered; _G.toggle = AnimatedObjectActivatable.onAnimationInputToggle", "=others"), 0);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "tostring(AnimatedObject.triggerCallback == StubAnimatedObject.originalTriggerCallback)"), "true");
    EXPECT_EQ(EvalLua(L, "tostring(rawget(AnimatedMapObject, 'triggerCallback'))"), "nil");
    EXPECT_EQ(EvalLua(L, "tostring(AnimatedObject.getCanBeTriggered == canBeTriggered and AnimatedObjectActivatable.onAnimationInputToggle == toggle)"),
              "true") << "the other functions were never touched";
    EXPECT_EQ(Warnings(), 0);

    EnterZone();
    EXPECT_EQ(EvalLua(L, "tostring(StubAnimatedObject.inZone(door))"), "true");
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, AWrapSomethingElseAddedOnTopIsLeftInPlaceWhenTheMapUnloads) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local wrapped = AnimatedObject.triggerCallback
        _G.theirs = function(...) return wrapped(...) end
        AnimatedObject.triggerCallback = theirs
    )luau", "=anotherMod"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "tostring(AnimatedObject.triggerCallback == theirs)"), "true") << "another mod's wrap is not undone under it";
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, ThePointerDiagnosticsCarryOneLineForAnimatedObjects) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("Animated Objects support: installed | in zone: 0 | selected: none"), std::string::npos)
        << EvalLua(L, "runCommand('vrPointerDiagnostics')");

    EnterZone();
    FRAMES(2);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("Animated Objects support: installed | in zone: 1 | selected: door"), std::string::npos)
        << EvalLua(L, "runCommand('vrPointerDiagnostics')");

    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("in zone: 1 | selected: none"), std::string::npos);

    LeaveZone();
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("in zone: 0 | selected: none"), std::string::npos);
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, TheDiagnosticsNameTheSelectedObjectByItsPartWhenItHasNoSaveId) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, "_G.door = StubAnimatedObject.door({ name = 'barnDoor', x = -0.4, y = -0.2, z = 2.0, noSaveId = true })", "=buildingDoor"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    const std::string text = EvalLua(L, "runCommand('vrPointerDiagnostics')");
    EXPECT_NE(text.find("in zone: 1 | selected: barnDoorPart"), std::string::npos) << text;

    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("in zone: 1 | selected: none"), std::string::npos);
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, ThePointerDiagnosticsSayTheSupportGaveUp) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("toggleMissing"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("Animated Objects support: gave up (see the warning in the log)"), std::string::npos);
}

namespace {
int ExactLogLines(const std::string& line) {
    int count = 0;
    for (const auto& logged : s_capturedLogs) {
        if (logged == line) ++count;
    }
    return count;
}

std::string AnimatedObjectsWarning(const std::string& problem) {
    return "[FS25_VR] [Warning] Animated Objects support disabled: " + problem
        + ". Motion Controllers cannot work doors and gates; the game's prompts are unchanged.";
}
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, TheWarningSaysWhichFunctionIsMissing) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("triggerCallbackMissing"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(3);
    EXPECT_EQ(ExactLogLines(AnimatedObjectsWarning("AnimatedObject.triggerCallback is missing")), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, TheWarningSaysHowTheTriggerCallbackIsReshaped) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("triggerCallbackReshaped"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(3);
    EXPECT_EQ(ExactLogLines(AnimatedObjectsWarning(
                  "AnimatedObject.triggerCallback takes 1 parameters, not (object, triggerId, otherId, onEnter, onLeave, onStay)")), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, TheWarningSaysHowTheCanBeTriggeredCheckIsReshaped) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("canBeTriggeredReshaped"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(3);
    EXPECT_EQ(ExactLogLines(AnimatedObjectsWarning("AnimatedObject.getCanBeTriggered takes 0 parameters, not (object)")), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, TheWarningSaysTheToggleHandlerIsMissing) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("toggleMissing"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(3);
    EXPECT_EQ(ExactLogLines(AnimatedObjectsWarning("AnimatedObjectActivatable.onAnimationInputToggle is missing")), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, TheWarningOfAGameWithoutAnimatedObjectsSaysTheClassWasNotFound) {
    ASSERT_NO_FATAL_FAILURE(Start());
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(3);
    EXPECT_EQ(ExactLogLines(AnimatedObjectsWarning("the game's AnimatedObject class was not found")), 1);
}
