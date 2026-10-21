// Object Grab, the game's own actions while a Held Object is carried (ADR-0033, spec #156, ticket #162), seen from outside: the real
// Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (the game's empty hands and
// loose objects in object_grab_test_support.h; the rotate and level actions, and the joint that makes the object follow the rotation node,
// are stubbed here after the game's source, HandToolHands:rotateHeldItem and :levelHeldItem). The game's rotate and level actions turn the
// rotation node; a test fires them and asserts where the rotation node is, in the world and relative to the Virtual Hand (the hand the
// player sees), after the next game frames and hand moves. The game's own pick-up button keeps the game's carry, and the game's own
// throw and drop end it. Prior art: test_object_grab_carry.cpp.

#include "object_grab_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kBaleAhead = "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.05, { name = 'bale' })";

// Lua helpers: the poses of the nodes in the frame of the right Virtual Hand (as test_object_grab_carry.cpp), and the game's rotate and
// level actions as the game's source has them.
constexpr const char* kActionHelpers = R"luau(
    local function qmul(ax, ay, az, aw, bx, by, bz, bw)
        return aw * bx + ax * bw + ay * bz - az * by,
               aw * by - ax * bz + ay * bw + az * bx,
               aw * bz + ax * by - ay * bx + az * bw,
               aw * bw - ax * bx - ay * by - az * bz
    end
    function _G.relativePose(node)
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
    function _G.heading(node)
        local x, _, z = StubScene.worldDirection(node, 0, 0, 1)
        return math.atan2(x, z)
    end
    function _G.sameQuaternion(a, b) return math.abs(a[1] * b[1] + a[2] * b[2] + a[3] * b[3] + a[4] * b[4]) > 1 - 1e-6 end
    function _G.worldQuaternion(node) return { getWorldQuaternion(node) } end
    -- how far the kinematic node is from where the game puts it: the look ray's origin plus its direction times the hold distance
    function _G.holdError()
        local x, y, z, dx, dy, dz = StubHandTool.lastRay()
        local d = hands.spec_hands.currentHoldDistance
        return dist(worldPosition(kinematicNode()), { x + dx * d, y + dy * d, z + dz * d })
    end

    -- the game's rotate action about an axis given in the kinematic node's space (pitch is its X, yaw its Y): the rotation node turns,
    -- and the server applies it to the joint
    function _G.gameRotates(angle, ax, ay, az)
        local wx, wy, wz = StubScene.worldDirection(kinematicNode(), ax, ay, az)
        local s = math.sin(angle / 2)
        local qx, qy, qz, qw = getWorldQuaternion(rotationNode())
        setWorldQuaternion(rotationNode(), qmul(wx * s, wy * s, wz * s, math.cos(angle / 2), qx, qy, qz, qw))
        local spec = hands.spec_hands
        if hands.isServer and spec.heldItemJointId ~= nil then setJointFrame(spec.heldItemJointId, 0, rotationNode()) end
    end
    -- the game's level action: the rotation node's local rotation becomes the item's yaw relative to the kinematic node, no pitch or roll
    function _G.gameLevels()
        local spec = hands.spec_hands
        local ix, _, iz = StubScene.worldDirection(spec.heldItemNode, 0, 0, 1)
        local kx, _, kz = StubScene.worldDirection(kinematicNode(), 0, 0, 1)
        local yaw = math.atan2(ix, iz) - math.atan2(kx, kz)
        setRotation(rotationNode(), 0, yaw, 0)
        if hands.isServer and spec.heldItemJointId ~= nil then setJointFrame(spec.heldItemJointId, 0, rotationNode()) end
    end
    -- the joint: the held object follows the rotation node (after the game's update of the nodes, each frame)
    table.insert(StubHandTool.updaters, function(dt)
        local node = hands.spec_hands.heldItemNode
        if node ~= nil then
            setWorldTranslation(node, getWorldTranslation(rotationNode()))
            setWorldQuaternion(node, getWorldQuaternion(rotationNode()))
        end
    end)
)luau";

class VirtualGamepadObjectGrabGameActionsTest : public VirtualGamepadObjectGrabTest {
protected:
    void StartWithHelpers() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_EQ(RunLuauChunk(L, kActionHelpers, "=actionHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
        POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    }
    // The right hand pointing at the bale and squeezing: the bale is its Held Object. restOffset is the bale's pose in the hand's frame.
    void StartCarrying() {
        ASSERT_NO_FATAL_FAILURE(StartWithHelpers());
        ASSERT_EQ(RunLuauChunk(L, "_G.restOffset = relativePose(bale.node)", "=restOffset"), 0);
        Squeeze(kRight, 1.0f);
        FRAMES(3);
        ASSERT_EQ(PickUps(), 1) << "the grab";
    }
    std::string Pose(const char* node) { return EvalLua(L, std::string("poseText(relativePose(") + node + "))"); }
    bool Same(const char* node, const char* expected) {
        return EvalLua(L, std::string("tostring(samePose(relativePose(") + node + "), " + expected + "))") == "true";
    }
    bool SameWorldRotation(const char* node, const char* expected) {
        return EvalLua(L, std::string("tostring(sameQuaternion(worldQuaternion(") + node + "), " + expected + "))") == "true";
    }
};

}  // namespace

// ---- Rotate -------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabGameActionsTest, ARotateActionTurnsTheHeldObjectOnTopOfTheWristAndTheTurnSurvivesLaterHandMoves) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    headset.SetHandPointing(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.6f, 0.7f));
    FRAMES(3);
    ASSERT_TRUE(Same("rotationNode()", "restOffset")) << "carried as before";

    // The game's yaw action turns the rotation node 0.8 rad about the vertical, from where the wrist holds it.
    ASSERT_EQ(RunLuauChunk(L, R"(
        local qx, qy, qz, qw = getWorldQuaternion(rotationNode())
        local s, c = math.sin(0.4), math.cos(0.4)  -- 0.8 rad about the vertical, applied on the left: world yaw
        _G.expectedTurn = { c * qx + s * qz, c * qy + s * qw, c * qz - s * qx, c * qw - s * qy }
        gameRotates(0.8, 0, 1, 0)
    )", "=yawAction"), 0);
    FRAMES(1);
    EXPECT_TRUE(SameWorldRotation("rotationNode()", "expectedTurn")) << "the turn is the game's, and is not overwritten by the next placement";
    EXPECT_FALSE(Same("rotationNode()", "restOffset")) << "it is turned relative to the hand";
    ASSERT_EQ(RunLuauChunk(L, "_G.turned = relativePose(rotationNode())", "=turned"), 0);

    // The hand moves and turns: the object keeps the turn it has been given in the hand.
    headset.SetHandPointing(kRight, HandAt(-0.2f, -0.3f, -0.1f, -0.9f, -1.2f));
    FRAMES(3);
    EXPECT_TRUE(Same("rotationNode()", "turned")) << Pose("rotationNode()") << " vs " << EvalLua(L, "poseText(turned)");
    headset.SetHandPointing(kRight, HandAt(0.3f, 0.1f, -0.6f, 0.3f, 0.5f));
    FRAMES(3);
    EXPECT_TRUE(Same("rotationNode()", "turned")) << Pose("rotationNode()") << " vs " << EvalLua(L, "poseText(turned)");

    // The pitch action turns it further, again on top of the first turn.
    ASSERT_EQ(RunLuauChunk(L, "gameRotates(0.5, 1, 0, 0)", "=pitchAction"), 0);
    FRAMES(1);
    EXPECT_FALSE(Same("rotationNode()", "turned"));
    ASSERT_EQ(RunLuauChunk(L, "_G.turnedMore = relativePose(rotationNode())", "=turnedMore"), 0);
    headset.SetHandPointing(kRight, HandAt(0.0f, 0.0f, -0.4f, -0.3f, 0.2f));
    FRAMES(3);
    EXPECT_TRUE(Same("rotationNode()", "turnedMore")) << Pose("rotationNode()") << " vs " << EvalLua(L, "poseText(turnedMore)");
}

TEST_F(VirtualGamepadObjectGrabGameActionsTest, TheRotateActionKeepsTheObjectsPlaceInTheHandAndTheJointFrameFollows) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "hands.spec_hands.heldItemJointId = 7; _G.placeBefore = relativePose(rotationNode())", "=joint"), 0);
    ASSERT_EQ(RunLuauChunk(L, "gameRotates(0.8, 0, 1, 0); StubHandTool.jointFrames = {}", "=yawAction"), 0);
    headset.SetHandPointing(kRight, HandAt(0.2f, 0.1f, -0.4f, 0.5f, 0.9f));
    FRAMES(3);
    EXPECT_LT(Number(L, "dist({ table.unpack(relativePose(rotationNode()), 1, 3) }, placeBefore)"), 2e-3) << "it turns about its own centre";
    ASSERT_GE(Number(L, "#StubHandTool.jointFrames"), 1);
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.jointFrames[#StubHandTool.jointFrames].node == rotationNode())"), "true");
}

TEST_F(VirtualGamepadObjectGrabGameActionsTest, ATurnMadeWhileTheHandIsNotTrackedIsKeptWhenItComesBack) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    headset.SetHandPointing(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.6f, 0.7f));
    FRAMES(3);
    const auto lost = [&](bool tracked) {
        headset.SetHandPose(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.6f, 0.7f), tracked);
        headset.SetHandGripPose(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.6f, 0.7f), tracked);
    };
    lost(false);
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, R"(
        local qx, qy, qz, qw = getWorldQuaternion(rotationNode())
        local s, c = math.sin(0.4), math.cos(0.4)
        _G.expectedTurn = { c * qx + s * qz, c * qy + s * qw, c * qz - s * qx, c * qw - s * qy }
        gameRotates(0.8, 0, 1, 0)
    )", "=yawAction"), 0);
    FRAMES(4);
    EXPECT_TRUE(SameWorldRotation("rotationNode()", "expectedTurn")) << "the nodes stay as they are, turned";
    EXPECT_EQ(Drops(), 0);

    headset.SetHandPointing(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.6f, 0.7f));  // tracked again, where it was
    FRAMES(3);
    EXPECT_TRUE(SameWorldRotation("rotationNode()", "expectedTurn")) << "and the turn is not lost when the hand is back";
}

// ---- Level --------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabGameActionsTest, TheLevelActionSquaresTheObjectKeepsItsHeadingAndTheSquaringSurvivesHandMoves) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    headset.SetHandPointing(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.9f, 0.4f));  // the wrist pitched well up: the object tilts with it
    FRAMES(3);
    ASSERT_LT(Number(L, "up(rotationNode())[2]"), 0.9) << "tilted";
    ASSERT_EQ(RunLuauChunk(L, "_G.headingBefore = heading(rotationNode()); gameLevels()", "=levelAction"), 0);
    FRAMES(1);
    EXPECT_GT(Number(L, "up(rotationNode())[2]"), 0.9999) << "squared";
    EXPECT_LT(Number(L, "math.abs(math.sin((heading(rotationNode()) - headingBefore) / 2))"), 1e-3)
        << "its heading is what it was: " << EvalLua(L, "tostring(heading(rotationNode()))") << " vs " << EvalLua(L, "tostring(headingBefore)");
    EXPECT_GT(Number(L, "up(kinematicNode())[2]"), 0.9999);

    // The squaring is kept in the hand, as any turn is: the heading follows the wrist.
    ASSERT_EQ(RunLuauChunk(L, "_G.leveled = relativePose(rotationNode())", "=leveled"), 0);
    headset.SetHandPointing(kRight, HandAt(-0.2f, -0.3f, -0.1f, -0.9f, -1.2f));
    FRAMES(3);
    EXPECT_TRUE(Same("rotationNode()", "leveled")) << Pose("rotationNode()") << " vs " << EvalLua(L, "poseText(leveled)");
}

// ---- The game's own pick-up button ----------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabGameActionsTest, APickUpWithTheGamesButtonStaysLevelAtItsDistanceAlongTheHandsRayAndIsNotMovedByTheGripPlacement) {
    ASSERT_NO_FATAL_FAILURE(StartWithHelpers());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressPickUp(hands)", "=gamesButton"), 0);
    ASSERT_EQ(PickUps(), 1);
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, "_G.kinematicBefore = worldPosition(kinematicNode())", "=remember"), 0);

    headset.SetHandPointing(kRight, HandAt(0.1f, 0.2f, -0.5f, 0.6f, 0.7f));  // pitched up and yawed
    FRAMES(3);
    EXPECT_GT(Number(L, "dist(worldPosition(kinematicNode()), kinematicBefore)"), 0.1) << "it follows the hand";
    EXPECT_LT(Number(L, "holdError()"), 1e-3) << "at the distance it was picked up from, along the look ray, which is the hand's";
    EXPECT_GT(Number(L, "up(kinematicNode())[2]"), 0.9999) << "level";
    EXPECT_GT(Number(L, "up(rotationNode())[2]"), 0.9999) << "and the wrist's tilt is not in it";
    EXPECT_EQ(Drops(), 0);
}

TEST_F(VirtualGamepadObjectGrabGameActionsTest, TheGamesOwnThrowAndDropButtonsEndTheGamesCarryWhileTheGripLeavesItAlone) {
    ASSERT_NO_FATAL_FAILURE(StartWithHelpers());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressPickUp(hands)", "=gamesButton"), 0);
    ASSERT_EQ(PickUps(), 1);
    Squeeze(kRight, 1.0f);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 1);
    EXPECT_EQ(Drops(), 0) << "the grip neither grabs nor drops what the game carries";

    ASSERT_EQ(RunLuauChunk(L, "hands:throwHeldItemWithForceVector(0, 0.2, 1, 0.5)", "=gameThrows"), 0);
    FRAMES(3);
    EXPECT_EQ(Number(L, "#StubHandTool.throws"), 1) << "the game's throw";
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode)"), "nil");
    EXPECT_EQ(Drops(), 0);

    // Picked up again with the button, and put down with the game's drop.
    Squeeze(kRight, 0.0f);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressPickUp(hands)", "=gamesButtonAgain"), 0);
    ASSERT_EQ(PickUps(), 2);
    ASSERT_EQ(RunLuauChunk(L, "hands:dropHeldItem()", "=gameDrops"), 0);
    FRAMES(3);
    EXPECT_EQ(Drops(), 1) << "the game's drop, and not another";
}
