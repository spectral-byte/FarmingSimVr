#include "manual_attach_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace manual_attach_test;

class VirtualGamepadManualAttachPromptsTest : public VirtualGamepadManualAttachTest {
protected:
    std::string Prompt(const char* kind) {
        return EvalLua(L, std::string("(function() local p = StubManualAttach.prompts[") + "'" + kind +
                          "']; return p and p.visible and p.text or 'hidden' end)()");
    }
    std::string ContextBox() { return EvalLua(L, "tostring(StubManualAttach.contextBox or 'none')"); }
};

}

TEST_F(VirtualGamepadManualAttachPromptsTest, WithTwoHitchesInReachThePromptsAndContextBoxDescribeTheHitchTheRayIsOnNotTheNearestOne) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(StubChunk("manual_attach_front_and_rear"));
    Lua("StubManualAttach.attach(tractor, rearPlow, 1, 1)");
    FRAMES(1);
    ASSERT_EQ(Prompt("joint"), "Attach") << "with the ray on neither, Manual Attach's own nearest candidate, the front hitch";
    ASSERT_EQ(ContextBox(), "frontLoader");

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);
    FRAMES(1);
    EXPECT_EQ(Prompt("joint"), "Detach") << "the ray is on the rear hitch: its Implement is on it";
    EXPECT_EQ(ContextBox(), "rearPlow") << "and the context box names it";
}

TEST_F(VirtualGamepadManualAttachPromptsTest, MovingTheRayFromOneHitchToAnotherChangesThePromptsToTheNewHitchsAndBackToo) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(StubChunk("manual_attach_front_and_rear"));
    Lua("StubManualAttach.attach(tractor, rearPlow, 1, 1)");

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(1);
    ASSERT_EQ(Prompt("joint"), "Detach");
    ASSERT_EQ(ContextBox(), "rearPlow");

    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-1.20 -0.20 1.00");
    FRAMES(1);
    EXPECT_EQ(Prompt("joint"), "Attach") << "the front hitch has an Implement lined up";
    EXPECT_EQ(ContextBox(), "frontLoader");

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(1);
    EXPECT_EQ(Prompt("joint"), "Detach");
    EXPECT_EQ(ContextBox(), "rearPlow");
}

TEST_F(VirtualGamepadManualAttachPromptsTest, ManualAttachsOwnKeysActOnTheImplementOfTheHitchTheRayIsOn) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(StubChunk("manual_attach_front_and_rear"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);
    FRAMES(1);

    Lua("StubManualAttach.pressKey('joint')");
    EXPECT_EQ(Attached("rearPlow"), "true") << "the attach key works the pointed hitch, not Manual Attach's nearest one";
    EXPECT_EQ(Attached("frontLoader"), "false");

    FRAMES(2);
    Lua("StubManualAttach.pressKey('pto')");
    Lua("StubManualAttach.pressKey('hoses')");
    EXPECT_EQ(Performs("pto"), 1);
    EXPECT_EQ(Performs("hoses"), 1);
    EXPECT_EQ(EvalLua(L, "tostring(StubManualAttach.performs[#StubManualAttach.performs].attachedImplement == rearPlow"
                         " and StubManualAttach.performs[#StubManualAttach.performs - 1].attachedImplement == rearPlow)"), "true");
}

TEST_F(VirtualGamepadManualAttachPromptsTest, WithTheRayOnNoHitchManualAttachsPromptsAndKeysAreItsOwnNearestCandidates) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(StubChunk("manual_attach_front_and_rear"));
    Lua("StubManualAttach.attach(tractor, rearPlow, 1, 1)");
    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    ASSERT_EQ(DrawnEnd(), "none");
    FRAMES(2);
    EXPECT_EQ(Prompt("joint"), "Attach");
    EXPECT_EQ(ContextBox(), "frontLoader") << "the nearest, front hitch";
    Lua("StubManualAttach.pressKey('joint')");
    EXPECT_EQ(Attached("frontLoader"), "true");
}

TEST_F(VirtualGamepadManualAttachPromptsTest, WhileTheDeskMouseHasControlOrAMenuIsOpenThePromptsAreManualAttachsOwnNearestCandidatesAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(StubChunk("manual_attach_front_and_rear"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(1);
    ASSERT_EQ(ContextBox(), "rearPlow");

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:notePointerMouseMoved()", "=mouseMoved"), 0);
    FRAMES(2);
    EXPECT_EQ(ContextBox(), "frontLoader") << "the mouse has the pointer: Manual Attach's nearest";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(1);
    ASSERT_EQ(ContextBox(), "rearPlow");
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(2);
    EXPECT_EQ(ContextBox(), "frontLoader") << "in a menu";
}

TEST_F(VirtualGamepadManualAttachPromptsTest, WithMotionControllersOffThePromptsAreManualAttachsOwnNearestCandidates) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(StubChunk("manual_attach_front_and_rear"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(1);
    ASSERT_EQ(ContextBox(), "rearPlow");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
    FRAMES(2);
    EXPECT_EQ(ContextBox(), "frontLoader");
}

TEST_F(VirtualGamepadManualAttachPromptsTest, UnloadingTheMapHandsTheCandidateUpdateBackAsFound) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(StubChunk("manual_attach_front_and_rear"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(1);
    ASSERT_FALSE(CandidateUpdateIsAsFound()) << "while the support is in use the candidate update is wrapped";

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_TRUE(CandidateUpdateIsAsFound());
    FRAMES(2);
    EXPECT_EQ(ContextBox(), "frontLoader") << "and Manual Attach picks its nearest again";
}
