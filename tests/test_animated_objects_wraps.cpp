// Animated Objects, the wrapped game function, the game in other versions and the diagnostics (ADR-0034, spec #180, ticket #181), seen from
// outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs
// (animated_object_test_support.h) in the "game versions" the stub can be in. A function the module relies on that is missing or has another
// shape turns the support off with ONE warning, and the game's prompts work as they do without the mod; unloading the map hands the game's
// class back exactly as found. Prior art: test_object_grab_wraps.cpp and test_pointer_ray.cpp (Interactive Control missing or mismatched).

#include "animated_object_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace animated_object_test;

class VirtualGamepadAnimatedObjectsWrapsTest : public VirtualGamepadAnimatedObjectsTest {
protected:
    // The game is in a version where something is missing or reshaped: the module warns once, selects nothing, toggles nothing, and the
    // game's own prompt and its bound button work.
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

}  // namespace

// ---- Giving up -------------------------------------------------------------------------------------------------------------------

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
    ASSERT_NO_FATAL_FAILURE(Start());  // no Animated Objects in this game
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

// ---- Unloading the map -----------------------------------------------------------------------------------------------------------

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

    // The player walks into a zone after the unload: the game's callback is all there is.
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

// ---- The diagnostics ---------------------------------------------------------------------------------------------------------------

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

TEST_F(VirtualGamepadAnimatedObjectsWrapsTest, ThePointerDiagnosticsSayTheSupportGaveUp) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("toggleMissing"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("Animated Objects support: gave up (see the warning in the log)"), std::string::npos);
}
