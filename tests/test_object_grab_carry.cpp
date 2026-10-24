// Object Grab, the grip carries (ADR-0033, spec #156, ticket #159), seen from outside: the real Companion Mod runs against the real VRBridge
// over FakeTrackedHeadset, with the game stood in by Lua stubs (the game's empty hands and loose objects in object_grab_test_support.h). While
// the grip holds, the game's kinematic node and rotation node are placed from the hand: a test moves and turns the fake Motion Controller
// and asserts where those nodes are relative to the Virtual Hand (the hand the player sees), that the kinematic node stays level, that the
// game's dirty flag is raised and its joint frame set, and what the game is called with when the grip is let go. Prior art:
// test_object_grab_grip.cpp (the grab) and test_wheel_grab.cpp (a held part that follows the hand).

#include "object_grab_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

// A hand pointing at tracking (0.4, -0.2, -0.3) has its fingertip at world (-0.4, -0.2, 0.3) and the Pointer Ray runs along +Z. The bale is
// straight ahead of it, 1.2 m from the fingertip.
constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kBaleAhead = "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.05, { name = 'bale' })";

// Lua helpers: poses of the nodes in the frame of the right Virtual Hand (the hand the player sees), which is what a carry keeps.
constexpr const char* kCarryHelpers = R"luau(
    local function qmul(ax, ay, az, aw, bx, by, bz, bw)
        return aw * bx + ax * bw + ay * bz - az * by,
               aw * by - ax * bz + ay * bw + az * bx,
               aw * bz + ax * by - ay * bx + az * bw,
               aw * bw - ax * bx - ay * by - az * bz
    end
    function _G.relativePose(node)   -- { x, y, z, qx, qy, qz, qw } of the node in the right Virtual Hand's frame
        local hx, hy, hz, hw = getWorldQuaternion(handHolder('right'))
        local nx, ny, nz, nw = getWorldQuaternion(node)
        local qx, qy, qz, qw = qmul(-hx, -hy, -hz, hw, nx, ny, nz, nw)
        local px, py, pz = offsetInHolder(node, 'right')
        return { px, py, pz, qx, qy, qz, qw }
    end
    function _G.samePose(a, b)
        for i = 1, 3 do
            if math.abs(a[i] - b[i]) > 2e-3 then return false end
        end
        return math.abs(a[4] * b[4] + a[5] * b[5] + a[6] * b[6] + a[7] * b[7]) > 1 - 1e-5
    end
    function _G.poseText(p) return string.format('(%.3f %.3f %.3f) q(%.3f %.3f %.3f %.3f)', table.unpack(p)) end
    function _G.rotationNode() return hands.spec_hands.kinematicRotationNode end
    function _G.kinematicNode() return hands.spec_hands.kinematicNode end
    function _G.up(node) return { StubScene.worldDirection(node, 0, 1, 0) } end
    function _G.worldPosition(node) return { getWorldTranslation(node) } end
    function _G.dist(a, b) return math.sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2) end
)luau";

class VirtualGamepadObjectGrabCarryTest : public VirtualGamepadObjectGrabTest {
protected:
    // The right hand pointing at the bale and squeezing: the bale is its Held Object. restOffset is the bale's pose in the hand's frame
    // just before the grab.
    void StartCarrying() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_EQ(RunLuauChunk(L, kCarryHelpers, "=carryHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
        POINT_HAND(kRight, kHandX, kHandY, kHandZ);
        ASSERT_EQ(RunLuauChunk(L, "_G.restOffset = relativePose(bale.node)", "=restOffset"), 0);
        Squeeze(kRight, 1.0f);
        FRAMES(3);
        ASSERT_EQ(PickUps(), 1) << "the grab";
    }
    std::string Pose(const char* node) { return EvalLua(L, std::string("poseText(relativePose(") + node + "))"); }
    bool Same(const char* node, const char* expected) {
        return EvalLua(L, std::string("tostring(samePose(relativePose(") + node + "), " + expected + "))") == "true";
    }
};

}  // namespace

// ---- The carry follows the hand -----------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabCarryTest, TheRotationNodeKeepsTheObjectsPlaceInTheHandFromTheGrabThroughHandMovesAndWristTurns) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    EXPECT_TRUE(Same("rotationNode()", "restOffset")) << "no jump at the grab: " << Pose("rotationNode()") << " vs "
        << EvalLua(L, "poseText(restOffset)");

    // The hand moves and turns (pitched up and yawed): the node goes with it.
    const std::string before = EvalLua(L, "string.format('%.3f %.3f %.3f', getWorldTranslation(rotationNode()))");
    headset.SetHandPointing(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.6f, 0.7f));
    FRAMES(3);
    EXPECT_NE(EvalLua(L, "string.format('%.3f %.3f %.3f', getWorldTranslation(rotationNode()))"), before) << "the node moved with the hand";
    EXPECT_TRUE(Same("rotationNode()", "restOffset")) << Pose("rotationNode()") << " vs " << EvalLua(L, "poseText(restOffset)");

    headset.SetHandPointing(kRight, HandAt(-0.2f, -0.3f, -0.1f, -0.9f, -1.2f));
    FRAMES(3);
    EXPECT_TRUE(Same("rotationNode()", "restOffset")) << Pose("rotationNode()") << " vs " << EvalLua(L, "poseText(restOffset)");
}

TEST_F(VirtualGamepadObjectGrabCarryTest, TheKinematicNodeStaysLevelWhileTheWristsTiltGoesIntoTheRotationNode) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    headset.SetHandPointing(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.9f, 0.4f));  // the wrist pitched well up
    FRAMES(3);
    EXPECT_GT(Number(L, "up(kinematicNode())[2]"), 0.9999) << "the kinematic node is level";
    EXPECT_LT(Number(L, "up(rotationNode())[2]"), 0.9) << "the tilt is the rotation node's: " << EvalLua(L, "tostring(up(rotationNode())[2])");
}

TEST_F(VirtualGamepadObjectGrabCarryTest, TheRotationNodeGoesToWhereTheHandHoldsTheObjectWhateverTheKinematicNodesOffset) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    // The rotation node's offset from the kinematic node is set by the game's server at the grab (the node is not at the kinematic
    // node's origin): the kinematic node is placed so that the rotation node still lands in the hand.
    ASSERT_EQ(RunLuauChunk(L, "setTranslation(rotationNode(), 0.3, -0.2, 0.5)", "=offsetTheRotationNode"), 0);
    headset.SetHandPointing(kRight, HandAt(0.2f, 0.1f, -0.4f, 0.5f, 0.9f));
    FRAMES(3);
    EXPECT_TRUE(Same("rotationNode()", "restOffset")) << Pose("rotationNode()") << " vs " << EvalLua(L, "poseText(restOffset)");
    EXPECT_GT(Number(L, "up(kinematicNode())[2]"), 0.9999);
    EXPECT_EQ(EvalLua(L, "string.format('%.3f %.3f %.3f', getTranslation(rotationNode()))"), "0.300 -0.200 0.500") << "the offset itself is the game's";
}

TEST_F(VirtualGamepadObjectGrabCarryTest, TheDirtyFlagIsRaisedEachUpdateAndTheJointFrameIsSetWhenTheGameHasAJointOnTheServer) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.dirtyRaised = {}; StubHandTool.jointFrames = {}", "=forget"), 0);
    FRAMES(5);
    EXPECT_GE(Number(L, "#StubHandTool.dirtyRaised"), 5) << "raised every update, as the game's own is";
    EXPECT_EQ(Number(L, "StubHandTool.dirtyRaised[1].flag"), 1) << "the hands' dirty flag";
    EXPECT_EQ(Number(L, "#StubHandTool.jointFrames"), 0) << "there is no joint yet";

    ASSERT_EQ(RunLuauChunk(L, "hands.spec_hands.heldItemJointId = 7", "=joint"), 0);
    FRAMES(3);
    ASSERT_GE(Number(L, "#StubHandTool.jointFrames"), 3) << "the rotation node is applied to the joint, as the game's rotate action does";
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.jointFrames[#StubHandTool.jointFrames].jointId)"), "7");
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.jointFrames[#StubHandTool.jointFrames].node == rotationNode())"), "true");

    // A client sends the rotation node's pose in the stream and the server applies it: it does not set the joint frame itself.
    ASSERT_EQ(RunLuauChunk(L, "hands.isServer = false; StubHandTool.jointFrames = {}", "=client"), 0);
    FRAMES(3);
    EXPECT_EQ(Number(L, "#StubHandTool.jointFrames"), 0);
}

TEST_F(VirtualGamepadObjectGrabCarryTest, TheNodesFollowTheGamesPlacementUntilTheGamesJointHasMovedTheRotationNodeToTheObject) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, kCarryHelpers, "=carryHelpers"), 0);
    ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(RunLuauChunk(L, "hands.spec_hands.awaitingJoint = true", "=awaiting"), 0);  // the server makes the joint a few physics frames later
    Squeeze(kRight, 1.0f);
    FRAMES(2);
    ASSERT_EQ(PickUps(), 1);
    // Whatever the rotation node is, it is the game's to place until it is at the object: the carry records no offset yet.
    ASSERT_EQ(RunLuauChunk(L, "setTranslation(rotationNode(), 0, 0, 0); setWorldTranslation(kinematicNode(), 5, 5, 5)", "=staleNodes"), 0);
    FRAMES(2);
    EXPECT_NE(EvalLua(L, "string.format('%.1f %.1f %.1f', getWorldTranslation(kinematicNode()))"), "5.0 5.0 5.0") << "the game's own placement ran";

    // The joint exists and the rotation node is at the object: from here the hand holds it.
    ASSERT_EQ(RunLuauChunk(L, "hands.spec_hands.awaitingJoint = false; hands:orientRotationNodeToItem(bale.node)", "=jointMade"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "_G.held = relativePose(rotationNode())", "=held"), 0);
    headset.SetHandPointing(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.6f, 0.7f));
    FRAMES(3);
    EXPECT_TRUE(Same("rotationNode()", "held")) << Pose("rotationNode()") << " vs " << EvalLua(L, "poseText(held)");
}

// ---- Letting go ------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabCarryTest, LettingGoOfTheGripWithAStillHandDropsTheObjectThroughTheGame) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(Drops(), 0);

    Squeeze(kRight, 0.0f);
    FRAMES(1);
    ASSERT_EQ(Drops(), 1) << "dropHeldItem";
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.drops[1].held == bale.node)"), "true");
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode)"), "nil");
    FRAMES(5);
    EXPECT_EQ(Drops(), 1);
    EXPECT_FALSE(PadGrip(kRight));

    // The grip squeezed again grabs again (the bale lies where it was, under the ray).
    Squeeze(kRight, 1.0f);
    FRAMES(1);
    EXPECT_EQ(PickUps(), 2);
}

TEST_F(VirtualGamepadObjectGrabCarryTest, TheGamesOwnDropEndsTheCarryCleanlyAndTheGripGoesBackToThePadOnceItIsLetGo) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());

    ASSERT_EQ(RunLuauChunk(L, "hands:dropHeldItem()", "=gameDrops"), 0);  // getting into a vehicle, taking a Hand Tool
    FRAMES(5);
    EXPECT_EQ(Drops(), 1) << "the game's drop, and not another";
    EXPECT_EQ(PickUps(), 1) << "a grip still held does not grab again, though the ray is on the bale";
    EXPECT_FALSE(PadGrip(kRight)) << "and what it took stays off the pad until it is let go";

    Squeeze(kRight, 0.0f);
    FRAMES(2);
    EXPECT_FALSE(PadGrip(kRight));
    EXPECT_EQ(Drops(), 1) << "letting go drops nothing";
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);  // away from the bale
    Squeeze(kRight, 1.0f);
    FRAMES(1);
    EXPECT_TRUE(PadGrip(kRight)) << "squeezed afresh at nothing, the grip reaches the pad again";
    EXPECT_EQ(PickUps(), 1);
}

// ---- A carry that Object Grab itself ends ----------------------------------------------------------------------------------------
// The game keeps carrying along its own look ray if nobody lets go: the object would jump to the head's ray with the grip still on the
// pad. So when Object Grab ends the carry (it is switched off, the Motion Controllers are, or it gave up) the hands drop what they hold.

TEST_F(VirtualGamepadObjectGrabCarryTest, SwitchingObjectGrabOffWhileCarryingDropsTheObjectThroughTheGameOnce) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrObjectGrab', 'off')", "=off"), 0);
    FRAMES(1);
    EXPECT_EQ(Drops(), 1) << "dropHeldItem";
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode)"), "nil") << "not left to the game's own carry along the head's ray";
    FRAMES(5);
    EXPECT_EQ(Drops(), 1);
}

TEST_F(VirtualGamepadObjectGrabCarryTest, TurningTheMotionControllersOffWhileCarryingDropsTheObjectThroughTheGame) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.motionControllersEnabled = false", "=motionControllersOff"), 0);
    FRAMES(1);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode)"), "nil");
}

TEST_F(VirtualGamepadObjectGrabCarryTest, ObjectGrabGivingUpWhileCarryingDropsTheObjectThroughTheGame) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    // A game function the wraps expect turns out reshaped at the next map load check: Object Grab gives up.
    ASSERT_EQ(RunLuauChunk(L, "HandToolHands.getCanPickUpNode = function() return false end; VRCompanion:registerObjectGrabConsoleCommands()", "=giveUp"), 0);
    ASSERT_EQ(CountLogLines("[Warning] Object Grab disabled"), 1);
    FRAMES(1);
    EXPECT_EQ(Drops(), 1);
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode)"), "nil");
}

TEST_F(VirtualGamepadObjectGrabCarryTest, ACarryTheGameEndedItselfIsNotDroppedAgainWhenObjectGrabIsSwitchedOffAfterwards) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(hands); hands.spec_hands.heldItemNode = nil", "=gameEndsItWithTheHandsPutAway"), 0);
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrObjectGrab', 'off')", "=off"), 0);
    FRAMES(3);
    EXPECT_EQ(Drops(), 0) << "the hands are not held and hold nothing: there is nothing to drop";
}

// ---- The game's guards stay -------------------------------------------------------------------------------------------------------
// The game's updateKinematicNode does nothing for hands that are not held, or whose player is not the owner. Taking over the placement
// must not place the nodes then either (a frame can run between the hands being put away and the carry being seen to end).

TEST_F(VirtualGamepadObjectGrabCarryTest, TheCarrysPlacementLeavesTheNodesAloneForHandsThatAreNotHeldAsTheGamesOwnDoes) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "setWorldTranslation(kinematicNode(), 5, 5, 5); hands.isHeld = false; hands:updateKinematicNode()", "=notHeld"), 0);
    EXPECT_EQ(EvalLua(L, "string.format('%.1f %.1f %.1f', getWorldTranslation(kinematicNode()))"), "5.0 5.0 5.0");
}

TEST_F(VirtualGamepadObjectGrabCarryTest, TheCarrysPlacementLeavesTheNodesAloneWhenThePlayerIsNotTheOwnerAsTheGamesOwnDoes) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "setWorldTranslation(kinematicNode(), 5, 5, 5); foot.player.isOwner = false; hands:updateKinematicNode()", "=notOwner"), 0);
    EXPECT_EQ(EvalLua(L, "string.format('%.1f %.1f %.1f', getWorldTranslation(kinematicNode()))"), "5.0 5.0 5.0");
}

TEST_F(VirtualGamepadObjectGrabCarryTest, NoPulsesWhileCarryingOrLettingGo) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    const int pulses = PulsesTo(kRight);
    headset.SetHandPointing(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.6f, 0.7f));
    FRAMES(30);
    EXPECT_EQ(PulsesTo(kRight), pulses) << "nothing is felt while it carries";
    Squeeze(kRight, 0.0f);
    FRAMES(3);
    EXPECT_EQ(PulsesTo(kRight), pulses) << "nor as it lets go";
    EXPECT_EQ(PulsesTo(kLeft), 0);
}

// ---- The carrying hand and the other hand ---------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabCarryTest, TheCarryingHandIsNeverThePointerHandAndStaysInUseHoweverStillItIsHeld) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    EXPECT_EQ(EvalLua(L, "tostring(VRCompanion:getPointerHand())"), "nil") << "no ray sticks out of the object";
    EXPECT_EQ(EvalLua(L, "tostring(VRCompanion:getPointerRay())"), "nil");

    AdvanceGameTime(5000);  // far past the time a still hand counts as set down
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "tostring(VRCompanion:isHandInUse(1))"), "true");
    EXPECT_EQ(EvalLua(L, "tostring(shown('right'))"), "true") << "its Virtual Hand stays";

    // The other hand can point, and its ray is the look ray.
    POINT_HAND(kLeft, -kHandX, kHandY, kHandZ);
    EXPECT_EQ(EvalLua(L, "tostring(VRCompanion:getPointerHand())"), "0");
    EXPECT_EQ(Drops(), 0);

    // Once the grip is let go the hand may point again.
    Squeeze(kRight, 0.0f);
    FRAMES(1);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(Drops(), 1);
    EXPECT_EQ(EvalLua(L, "tostring(VRCompanion:isHandInUse(1))"), "true");
}


// ---- Tracking lost ----------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabCarryTest, WithTrackingLostAndTheGripHeldTheNodesKeepTheirPlacementAndFollowTheHandAgainWhenItComesBack) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    headset.SetHandPointing(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.6f, 0.7f));
    FRAMES(3);
    ASSERT_TRUE(Same("rotationNode()", "restOffset"));
    ASSERT_EQ(RunLuauChunk(L, "_G.lastKinematic = worldPosition(kinematicNode()); _G.lastRotation = worldPosition(rotationNode())", "=remember"), 0);

    headset.SetHandPose(kRight, HandAt(-0.6f, 0.5f, -0.2f, 0.1f, 0.2f), false);  // lost, and the controller is somewhere else
    headset.SetHandGripPose(kRight, HandAt(-0.6f, 0.5f, -0.2f, 0.1f, 0.2f), false);
    FRAMES(10);
    EXPECT_LT(Number(L, "dist(worldPosition(kinematicNode()), lastKinematic)"), 1e-3) << "the kinematic node stays where it was";
    EXPECT_LT(Number(L, "dist(worldPosition(rotationNode()), lastRotation)"), 1e-3) << "and so does the rotation node";
    EXPECT_EQ(Drops(), 0) << "a hand that is not tracked has not let go";
    EXPECT_EQ(PickUps(), 1);

    headset.SetHandPointing(kRight, HandAt(0.3f, -0.1f, -0.2f, -0.4f, 1.1f));  // tracked again
    FRAMES(3);
    EXPECT_TRUE(Same("rotationNode()", "restOffset")) << Pose("rotationNode()") << " vs " << EvalLua(L, "poseText(restOffset)");
    EXPECT_EQ(Drops(), 0);
    EXPECT_FALSE(PadGrip(kRight)) << "and the grip is still the carry's";
}

TEST_F(VirtualGamepadObjectGrabCarryTest, AGripLetGoWhileTrackingWasLostDropsTheObjectWhenTheHandIsTrackedAgain) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    headset.SetHandPose(kRight, HandAt(-0.6f, 0.5f, -0.2f), false);
    headset.SetHandGripPose(kRight, HandAt(-0.6f, 0.5f, -0.2f), false);
    Squeeze(kRight, 0.0f);
    FRAMES(5);
    EXPECT_EQ(Drops(), 0) << "nothing is known of the hand until it is tracked";

    headset.SetHandPointing(kRight, HandAt(0.3f, -0.1f, -0.2f));
    FRAMES(2);
    EXPECT_EQ(Drops(), 1);
}


// ---- What is the game's ------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabCarryTest, ThePickUpWithTheGamesOwnButtonIsCarriedTheGamesWayAndTheGripLeavesItAlone) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, kCarryHelpers, "=carryHelpers"), 0);
    ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressPickUp(hands)", "=gamesButton"), 0);
    ASSERT_EQ(PickUps(), 1);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "string.format('%.2f %.2f %.2f', getWorldTranslation(kinematicNode()))"), "-0.40 -0.20 1.35")
        << "the kinematic node is along the look ray, which is the hand's";
    EXPECT_FALSE(PadGrip(kRight)) << "(no grip is held)";

    Squeeze(kRight, 1.0f);
    FRAMES(2);
    Squeeze(kRight, 0.0f);
    FRAMES(2);
    EXPECT_EQ(Drops(), 0) << "the grip did not grab it, so letting go does not drop it";
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode == bale.node)"), "true");
}

TEST_F(VirtualGamepadObjectGrabCarryTest, UnloadingTheMapGivesTheHandsToolItsOwnPlacementBack) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(EvalLua(L, "tostring(hands.updateKinematicNode == StubHandTool.handsRegistered.updateKinematicNode)"), "false") << "wrapped";
    ASSERT_EQ(EvalLua(L, "tostring(HandToolHands.updateKinematicNode == StubHandTool.handsRegistered.updateKinematicNode)"), "false");

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "tostring(hands.updateKinematicNode == StubHandTool.handsRegistered.updateKinematicNode)"), "true");
    EXPECT_EQ(EvalLua(L, "tostring(HandToolHands.updateKinematicNode == StubHandTool.handsRegistered.updateKinematicNode)"), "true");
}