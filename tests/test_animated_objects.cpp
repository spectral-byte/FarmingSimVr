#include "animated_object_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace animated_object_test;

}

TEST_F(VirtualGamepadAnimatedObjectsTest, InTheZoneThePointerRayOnAMovingPartSelectsItAndTheRayEndsWhereItHitsTheLeaf) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95") << "the door's near face, 1.65 m along the finger";

    ASSERT_EQ(RunLuauChunk(L, "setTranslation(door.animation.parts[1].node, -0.4, -0.2, 2.5)", "=moveLeaf"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 2.45") << "the leaf moves as the door opens, and the ray follows it";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, TouchingThePartFromUpCloseSelectsItToo) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    // The fingertip (z 0.3) is 4 cm inside the leaf, which is 8 cm thick.
    ASSERT_EQ(RunLuauChunk(L, "_G.door = StubAnimatedObject.door({ x = -0.4, y = -0.2, z = 0.3, hz = 0.04 })", "=doorAtTheFinger"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 0.26") << "the ray ends on the leaf's face the finger went through";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ACollisionShapeBelowAPartIsThePartAndAWallInFrontOfTheDoorHidesIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, "_G.door = StubAnimatedObject.door({ x = -0.4, y = -0.2, z = 2.0, collisionChild = true })", "=doorWithAShape"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95") << "the ray hits the shape under the part's node";

    ASSERT_EQ(RunLuauChunk(L, "StubAnimatedObject.wall('wall', -0.4, -0.2, 1.0, 1.0, 1.0, 0.05)", "=wall"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none") << "the ray ends on the wall in front, which is no part of the door";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ABarrierPlacedOnTheMapIsWorkedLikeABuildingsDoor) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, "_G.door = StubAnimatedObject.door({ x = -0.4, y = -0.2, z = 2.0, map = true })", "=barrier"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles("door"), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, OnePullRunsTheToggleOnceASecondPullTurnsItBackAndAPullWhileItMovesTurnsItBack) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);

    PullTrigger(kRight);
    EXPECT_EQ(Toggles("door"), 1) << "one pull, one toggle";
    EXPECT_EQ(EvalLua(L, "door.animation.direction"), "1") << "it opens";
    FRAMES(5);
    EXPECT_EQ(Toggles("door"), 1) << "holding the trigger toggles nothing more";
    ReleaseTrigger(kRight);
    EXPECT_EQ(Toggles("door"), 1) << "nor does letting go";

    ASSERT_EQ(RunLuauChunk(L, "StubAnimatedObject.advance(door, 1)", "=opened"), 0);
    PullTrigger(kRight);
    EXPECT_EQ(Toggles("door"), 2);
    EXPECT_EQ(EvalLua(L, "door.animation.direction"), "-1") << "a second pull closes it again";
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "StubAnimatedObject.advance(door, 0.3)", "=closing"), 0);
    PullTrigger(kRight);
    EXPECT_EQ(Toggles("door"), 3);
    EXPECT_EQ(EvalLua(L, "door.animation.direction"), "1") << "a pull while it moves turns it back";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, OutsideItsZoneNothingIsSelectedAndAPullTogglesNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "the player never entered the zone";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    EXPECT_EQ(Number(L, "#animatedCasts()"), 0) << "nothing in a zone, nothing to look for";
    ReleaseTrigger(kRight);

    EnterZone();
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    LeaveZone();
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none") << "and it is gone again once the player leaves";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, AnotherNodeInTheZoneAndAnObjectDeletedWithNoLeaveEventAreNotTheLocalPlayersZone) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    ASSERT_EQ(RunLuauChunk(L, "StubAnimatedObject.otherEnters(door)", "=someoneElse"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "the callback's own conditions: only the local player's root node counts";

    EnterZone();
    FRAMES(2);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    ASSERT_EQ(RunLuauChunk(L, "StubAnimatedObject.delete(door)", "=delete"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none") << "a deleted object is gone even though no leave event came";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, InTheZonesOfTwoTheOneTheRayIsOnIsTheOneToggledWhicheverIsNearer) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    ASSERT_EQ(RunLuauChunk(L, "_G.nearDoor = StubAnimatedObject.door({ name = 'nearDoor', x = 0.4, y = -0.2, z = 1.0, hx = 0.3 })", "=nearDoor"), 0);
    EnterZone("door");
    EnterZone("nearDoor");

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles("door"), 1) << "the farther one, which the ray is on";
    EXPECT_EQ(Toggles("nearDoor"), 0) << "not the nearer one";
    ReleaseTrigger(kRight);

    POINT_HAND(kRight, -kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "0.40 -0.20 0.95");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles("nearDoor"), 1) << "turned to the nearer one it is that one";
    EXPECT_EQ(Toggles("door"), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ADoorBeyondTheRaysLengthIsNotSelected) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    // The ray is 3 m long from the fingertip (z 0.3): this leaf's near face is 3.2 m along it.
    ASSERT_EQ(RunLuauChunk(L, "_G.door = StubAnimatedObject.door({ x = -0.4, y = -0.2, z = 3.55 })", "=farDoor"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);

    ASSERT_EQ(RunLuauChunk(L, "setTranslation(door.animation.parts[1].node, -0.4, -0.2, 3.25)", "=nearerDoor"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 3.20") << "within the length it is the door the ray is on";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, AnObjectWhoseCanBeTriggeredCheckAnswersFalseIsNotSelectedUntilTheCheckAnswersTrue) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    ASSERT_EQ(RunLuauChunk(L, "door.canBeTriggered = false", "=shut"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "no ray on it";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0) << "and no toggle";
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "door.canBeTriggered = true", "=open"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, TheCheckIsAskedOfTheObjectItselfSoAnOwnCheckLikeAPlaceablesFarmCheckCounts) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, "_G.door = StubAnimatedObject.door({ x = -0.4, y = -0.2, z = 2.0, farmCheck = true }); door.farmAllowed = false", "=otherFarm"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "a door of another farm is not one the player could work";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, AnObjectMovedByHoldingAButtonOrWithNoActionIsNeverSelected) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.control = StubAnimatedObject.door({ name = 'control', x = -1.2, y = -0.2, z = 2.0, hx = 0.3 })
        _G.negative = StubAnimatedObject.door({ name = 'negative', x = -0.4, y = -0.2, z = 2.0, hx = 0.3, negAction = 'ACTIVATE_HANDTOOL_2' })
        _G.same = StubAnimatedObject.door({ name = 'same', x = 0.4, y = -0.2, z = 2.0, hx = 0.3, sameAction = true })
        _G.noAction = StubAnimatedObject.door({ name = 'noAction', x = 1.2, y = -0.2, z = 2.0, hx = 0.3, noAction = true })
        for _, object in ipairs({ control, negative, same, noAction }) do StubAnimatedObject.enterZone(object) end
    )luau", "=doors"), 0);

    POINT_HAND(kRight, 1.2f, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-1.20 -0.20 1.95") << "the control is selected";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles("control"), 1);
    ReleaseTrigger(kRight);

    for (const char* object : { "negative", "same", "noAction" }) {
        SCOPED_TRACE(object);
        const float x = std::string(object) == "negative" ? kHandX : (std::string(object) == "same" ? -kHandX : -1.2f);
        POINT_HAND(kRight, x, kHandY, kHandZ);
        EXPECT_EQ(DrawnEnd(), "none") << "the ray is on it and shows nothing";
        PullTrigger(kRight);
        EXPECT_EQ(Toggles(object), 0);
        ReleaseTrigger(kRight);
    }
}
TEST_F(VirtualGamepadAnimatedObjectsTest, TheRayCastLeavesOutTheZonesTriggerShapesAndThePlayer) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_GT(Number(L, "#animatedCasts()"), 0);
    EXPECT_EQ(EvalLua(L, "tostring(bit32.band(animatedCasts()[1].mask, CollisionFlag.TRIGGER + CollisionFlag.PLAYER) == 0)"), "true")
        << "the zone is made of trigger shapes and the player's own capsule is around the head";
    EXPECT_EQ(EvalLua(L, "string.format('%.3f', animatedCasts()[1].dz)"), "1.000") << "along the finger";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, TheGamesPromptAndItsBoundButtonKeepWorkingAndAPullNeedsNeitherToBeShown) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(RunLuauChunk(L, "assert(StubAnimatedObject.inZone(door), 'the original callback ran: the prompt can show the door')", "=prompt"), 0);

    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1) << "the prompt is not the one shown, and the pull works";
    EXPECT_EQ(Number(L, "#StubAnimatedObject.textCalls"), 0) << "no prompt text is set for a prompt that is not registered";
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "StubAnimatedObject.showPrompt(door); StubAnimatedObject.pressButton(door)", "=button"), 0);
    EXPECT_EQ(Toggles(), 2) << "the bound button runs the same toggle";
    EXPECT_EQ(Number(L, "#StubAnimatedObject.textCalls"), 1) << "and the shown prompt's texts are refreshed";

    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 3) << "a pull with the prompt shown toggles too";
    EXPECT_EQ(Number(L, "#StubAnimatedObject.textCalls"), 2) << "and refreshes the prompt's texts";
}
