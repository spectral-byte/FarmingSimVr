#include "object_grab_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

// Right hand: pointing at tracking (kHandX + 0.8, ...), well beside the bale. Left hand: at the bale, a little to the side of where the
// right hand would be (see test_object_grab_grip.cpp for the geometry: the bale is 1.2 m ahead of a fingertip at kHandX).
constexpr float kBaleHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr float kLeftHandX = 0.45f;
constexpr float kAwayHandX = kBaleHandX + 0.8f;
constexpr const char* kBaleAhead = "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.05, { name = 'bale' })";

constexpr const char* kRaycastStub = R"luau(
    function _G.raycastClosest(x, y, z, dx, dy, dz, maxDistance, callbackName, target, mask)
        local length = math.sqrt(dx * dx + dy * dy + dz * dz)
        local bestNode, best = nil, nil
        for node, object in pairs(StubHandTool.looseObjects) do
            if entityExists(node) then
                local cx, cy, cz = getWorldTranslation(node)
                local ox, oy, oz = cx - x, cy - y, cz - z
                local b = (ox * dx + oy * dy + oz * dz) / length
                local disc = b * b - (ox * ox + oy * oy + oz * oz - object.radius ^ 2)
                if disc >= 0 then
                    local t = b - math.sqrt(disc)
                    if t >= 0 and t <= maxDistance and (best == nil or t < best) then bestNode, best = node, t end
                end
            end
        end
        if bestNode == nil then return 0 end
        target[callbackName](target, bestNode, x + dx / length * best, y + dy / length * best, z + dz / length * best, best, 0, 1, 0, 0, 0, true)
        return 1
    end
    -- an object only the physics ray cast knows: the targeter never hits it
    function _G.ghostObject(x, y, z, mass)
        local object = StubHandTool.looseObject(x, y, z, mass, { name = 'ghost' })
        table.remove(StubHandTool.looseObjectList)
        return object
    end
    function _G.slowTargeter()
        local base = PlayerTargeter.update
        function PlayerTargeter:update(dt)
            base(self, dt)
            local key = HandToolHands
            local fresh = self.closestTargetsByKey[key]
            local copy = fresh and { node = fresh.node, x = fresh.x, y = fresh.y, z = fresh.z, distance = fresh.distance } or false
            self.closestTargetsByKey[key] = self.arriving or nil
            self.arriving = copy
        end
    end
)luau";

class VirtualGamepadObjectGrabOffHandTest : public VirtualGamepadObjectGrabTest {
protected:
    void StartWithRaycasts() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_EQ(RunLuauChunk(L, kRaycastStub, "=raycastStub"), 0);
        ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    }

    void BothInUseLeftAtTheBale() {
        POINT_HAND(kRight, kAwayHandX, kHandY, kHandZ);
        POINT_HAND(kLeft, kLeftHandX, kHandY, kHandZ);
    }

    std::string TargeterRayIsTheLeftHands() {
        return EvalLua(L, "tostring(sameRay(targeterRay(), pulledBack(pointerRay(), VRCompanion.OBJECT_GRAB_REACH_BEHIND)) and math.abs(pointerRay()[1] + 0.45) < 0.01)");
    }
};

}

TEST_F(VirtualGamepadObjectGrabOffHandTest, OneSqueezeOfTheOffHandsGripMakesItThePointerHandAndPicksUpWhatTheTargeterFindsWithItsRay) {
    ASSERT_NO_FATAL_FAILURE(StartWithRaycasts());
    BothInUseLeftAtTheBale();
    ASSERT_EQ(PickUps(), 0) << "pointing alone picks nothing up";
    ASSERT_EQ(EvalLua(L, "tostring(pointerRay()[1] < -1.0)"), "true") << "the right hand is the Pointer Hand, beside the bale";

    Squeeze(kLeft, 1.0f);
    FRAMES(1);

    ASSERT_EQ(PickUps(), 1) << "the game's pickUpTarget ran in that squeeze";
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.pickUps[1].node == bale.node)"), "true");
    EXPECT_EQ(EvalLua(L, "string.format('%.2f %.2f %.2f', StubHandTool.pickUps[1].x, StubHandTool.pickUps[1].y, StubHandTool.pickUps[1].z)"),
              "-0.45 -0.20 1.36") << "with the point the targeter hit, from the left hand's ray";
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode == bale.node)"), "true");
    EXPECT_EQ(EvalLua(L, "tostring(math.abs(pointerRay()[1] + 0.45) < 0.01)"), "true") << "the left hand is the Pointer Hand now";
    EXPECT_GE(PulsesTo(kLeft), 1) << "the hand that grabbed feels it";
    EXPECT_EQ(PulsesTo(kRight), 0);

    FRAMES(5);
    EXPECT_EQ(PickUps(), 1) << "one grab, however long it is held";
}

TEST_F(VirtualGamepadObjectGrabOffHandTest, TheTargeterRunsAgainWithTheOffHandsRayInThatSqueezeWhateverItFoundBeforeIsNotPickedUp) {
    ASSERT_NO_FATAL_FAILURE(StartWithRaycasts());
    ASSERT_EQ(RunLuauChunk(L, "_G.sack = StubHandTool.looseObject(-1.2, -0.2, 1.5, 0.05, { name = 'sack' })", "=sack"), 0);
    BothInUseLeftAtTheBale();
    ASSERT_EQ(PickUps(), 0);

    Squeeze(kLeft, 1.0f);
    FRAMES(1);

    ASSERT_EQ(PickUps(), 1);
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.pickUps[1].node == bale.node)"), "true") << "the bale the left hand points at, not the sack the right hand pointed at";
}

TEST_F(VirtualGamepadObjectGrabOffHandTest, TheGripOfAnOffHandPointingAtAGrabbableObjectReadsReleasedOnThePad) {
    ASSERT_NO_FATAL_FAILURE(StartWithRaycasts());
    BothInUseLeftAtTheBale();

    Squeeze(kLeft, 1.0f);
    FRAMES(1);
    EXPECT_FALSE(PadGrip(kLeft)) << "the squeeze that grabs is not also LB";
    FRAMES(3);
    EXPECT_FALSE(PadGrip(kLeft)) << "nor while the hand carries";
}

TEST_F(VirtualGamepadObjectGrabOffHandTest, AnOffHandPointingAtNothingGrabbableKeepsItsGripBindingAndGrabsNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithRaycasts());
    POINT_HAND(kRight, kAwayHandX, kHandY, kHandZ);
    POINT_HAND(kLeft, kAwayHandX + 0.8f, kHandY, kHandZ);

    Squeeze(kLeft, 1.0f);
    FRAMES(1);
    EXPECT_TRUE(PadGrip(kLeft)) << "nothing under the finger: LB reaches the game";
    EXPECT_EQ(PickUps(), 0);
    EXPECT_EQ(EvalLua(L, "tostring(pointerRay()[1] < -1.0)"), "true") << "and the right hand is still the Pointer Hand";
}

TEST_F(VirtualGamepadObjectGrabOffHandTest, AnOffHandPointingAtAnObjectTheGameWillNotPickUpKeepsItsGripBinding) {
    ASSERT_NO_FATAL_FAILURE(StartWithRaycasts());
    ASSERT_EQ(RunLuauChunk(L, "bale.canBePickedUp = false", "=strappedDown"), 0);
    BothInUseLeftAtTheBale();

    Squeeze(kLeft, 1.0f);
    FRAMES(2);
    EXPECT_EQ(PickUps(), 0);
    EXPECT_TRUE(PadGrip(kLeft)) << "getCanPickUpNode said no: the grip is not taken";
}

TEST_F(VirtualGamepadObjectGrabOffHandTest, IfTheTargeterRunWithTheOffHandsRayFindsNothingTheGameCanPickUpNothingIsPickedUp) {
    ASSERT_NO_FATAL_FAILURE(StartWithRaycasts());
    ASSERT_EQ(RunLuauChunk(L, "bale.node = nil; StubHandTool.looseObjects = {}; StubHandTool.looseObjectList = {}; _G.ghost = ghostObject(-0.45, -0.2, 1.5, 0.05)", "=ghost"), 0);
    BothInUseLeftAtTheBale();

    Squeeze(kLeft, 1.0f);
    FRAMES(4);

    EXPECT_EQ(PickUps(), 0) << "the check alone decides nothing";
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode)"), "nil");
}

TEST_F(VirtualGamepadObjectGrabOffHandTest, AnOffHandGripHeldBeforeItsFingerLandedOnAnObjectKeepsItsBindingAndGrabsNothingUntilLetGoAndSqueezedAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithRaycasts());
    POINT_HAND(kRight, kAwayHandX, kHandY, kHandZ);
    POINT_HAND(kLeft, kAwayHandX + 0.8f, kHandY, kHandZ);
    Squeeze(kLeft, 1.0f);
    FRAMES(1);
    ASSERT_TRUE(PadGrip(kLeft));

    POINT_HAND(kLeft, kLeftHandX, kHandY, kHandZ);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 0);
    EXPECT_TRUE(PadGrip(kLeft)) << "it keeps its binding";

    Squeeze(kLeft, 0.0f);
    FRAMES(2);
    EXPECT_FALSE(PadGrip(kLeft));
    Squeeze(kLeft, 1.0f);
    FRAMES(1);
    EXPECT_EQ(PickUps(), 1) << "let go and squeezed afresh, it grabs in that squeeze";
}

TEST_F(VirtualGamepadObjectGrabOffHandTest, AnOffHandsTriggerPullStillMovesThePointerHandAndPicksNothingUp) {
    ASSERT_NO_FATAL_FAILURE(StartWithRaycasts());
    BothInUseLeftAtTheBale();

    headset.SetHandInputs(kLeft, hand_tools_test::Squeeze(1.0f, 0.0f));
    FRAMES(2);

    EXPECT_EQ(TargeterRayIsTheLeftHands(), "true") << "the pull made the left hand the Pointer Hand";
    EXPECT_EQ(PickUps(), 0) << "a pull is no grab";
}

TEST_F(VirtualGamepadObjectGrabOffHandTest, AnOffHandThatGrabbedAndLetsGoWhileSwingingThrowsAlongItsOwnTravel) {
    ASSERT_NO_FATAL_FAILURE(StartWithRaycasts());
    BothInUseLeftAtTheBale();
    Squeeze(kLeft, 1.0f);
    FRAMES(3);
    ASSERT_EQ(PickUps(), 1) << "the left hand grabbed in one squeeze";
    ASSERT_EQ(RunLuauChunk(L, "_G.before = { getWorldTranslation(handHolder('left')) }", "=before"), 0);

    const double fullSpeed = Number(L, "VRCompanion.OBJECT_GRAB_THROW_FULL_SPEED");
    const float step = static_cast<float>(fullSpeed * 1.05 * 16.667 / 1000.0);
    float y = kHandY, z = kHandZ;
    for (int i = 0; i < 6; ++i) {
        y += 0.6f * step;
        z -= 0.8f * step;
        headset.SetHandPointing(kLeft, HandAt(kLeftHandX, y, z));
        if (i == 5) Squeeze(kLeft, 0.0f);
        FRAMES(1);
    }
    ASSERT_EQ(RunLuauChunk(L, "_G.after = { getWorldTranslation(handHolder('left')) }", "=after"), 0);

    ASSERT_EQ(Number(L, "#StubHandTool.throws"), 1) << "thrown, not dropped";
    EXPECT_EQ(Drops(), 0);
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.throws[1].node == bale.node)"), "true");
    EXPECT_EQ(Number(L, "StubHandTool.throws[1].force"), 1.0);
    const char* cosine = "(function() local t = StubHandTool.throws[1]; local x, y, z = after[1]-before[1], after[2]-before[2], after[3]-before[3]; "
                         "local n = math.sqrt(x*x + y*y + z*z); return (t.dirX*x + t.dirY*y + t.dirZ*z) / n end)()";
    EXPECT_GT(Number(L, cosine), 0.99) << "along the way the left hand travelled";
    EXPECT_FALSE(PadGrip(kLeft));
}

TEST_F(VirtualGamepadObjectGrabOffHandTest, WhenTheTargetersAnswerToTheNewRayComesAFrameLaterTheSqueezeStillPicksItUp) {
    ASSERT_NO_FATAL_FAILURE(StartWithRaycasts());
    ASSERT_EQ(RunLuauChunk(L, "slowTargeter()", "=slowTargeter"), 0);
    BothInUseLeftAtTheBale();
    ASSERT_EQ(PickUps(), 0);

    Squeeze(kLeft, 1.0f);
    FRAMES(3);

    ASSERT_EQ(PickUps(), 1);
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.pickUps[1].node == bale.node)"), "true");
}
