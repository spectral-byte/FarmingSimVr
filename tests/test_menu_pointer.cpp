// Menu pointing in the Companion Mod (spec #71, ticket #74): the real Companion Mod over the real
// VRBridge over FakeTrackedHeadset, with a stub GUI that records the cursor moves and clicks the
// mod delivers through the game's own GUI mouse handling. The first block covers the two bridge
// calls the mod relies on (panel hit, drawn cursor); the rest covers the mod's behaviour.

#include "companion_mod_fixture.h"

namespace {

using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

HeadPose PoseAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

// Head at (0, 1.6, 0) looking ahead; the menu panel will sit 1.5 m in front of it at eye height.
void PlaceHead(FakeTrackedHeadset& headset) {
    headset.SetHeadPose(PoseAt(0.0f, 1.6f, 0.0f));
    headset.manager->PollHeadPose();
}

} // namespace

// ---- VRBridge.getPanelHit / VRBridge.setMenuCursor ----

TEST_F(CompanionModTest, BridgeReportsWhereAHandsRayHitsTheVisibleMenuPanel) {
    FakeTrackedHeadset headset;
    PlaceHead(headset);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    bridge.SetMenuVisible(true);
    headset.SetHandPose(kRight, PoseAt(0.0f, 1.6f, 0.0f));
    headset.SetHandPose(kLeft, PoseAt(0.4f, 1.6f + 0.225f, 0.0f));
    headset.NextFrame();

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local right = VRBridge.getPanelHit(1)
        assert(right.hit == true, "right hand hits")
        assert(math.abs(right.u - 0.5) < 1e-4 and math.abs(right.v - 0.5) < 1e-4, "centre")
        assert(math.abs(right.distance - 1.5) < 1e-4, "distance along the ray")
        local left = VRBridge.getPanelHit("left")
        assert(left.hit == true and math.abs(left.u - 0.75) < 1e-4 and math.abs(left.v - 0.25) < 1e-4, "left hand, by name")
    )luau", "=panelHit"), 0);
}

TEST_F(CompanionModTest, BridgePanelHitFillsACallerSuppliedTableInPlaceLikeTheOtherPoseCalls) {
    FakeTrackedHeadset headset;
    PlaceHead(headset);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    bridge.SetMenuVisible(true);
    headset.SetHandPose(kRight, PoseAt(0.0f, 1.6f, 0.0f));
    headset.NextFrame();

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local incomplete = {}
        local fresh = VRBridge.getPanelHit(1, incomplete)
        assert(fresh ~= incomplete and next(incomplete) == nil, "an incomplete table is left untouched")
        assert(fresh.hit == true)
        local count = 0
        for _ in pairs(fresh) do count = count + 1 end
        assert(count == 4, "hit, u, v, distance")
        assert(VRBridge.getPanelHit(1, fresh) == fresh, "a complete table is filled in place")
        assert(VRBridge.getPanelHit(fresh, 1) == fresh, "argument order does not matter")
    )luau", "=panelHitInPlace"), 0);
}

TEST_F(CompanionModTest, BridgePanelHitIsAMissWithoutAVisiblePanelATrackedHandOrAnInjectorRuntime) {
    FakeTrackedHeadset headset;
    PlaceHead(headset);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    headset.SetHandPose(kRight, PoseAt(0.0f, 1.6f, 0.0f));
    headset.NextFrame();

    // No menu and no 2D Floating Window: there is no panel to hit.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local miss = VRBridge.getPanelHit(1)
        assert(miss.hit == false and miss.u == 0 and miss.v == 0 and miss.distance == 0)
    )luau", "=noPanel"), 0);

    // The 2D Floating Window Mode panel counts the same as a menu.
    bridge.Set2DViewActive(true);
    ASSERT_EQ(RunLuauChunk(L, "assert(VRBridge.getPanelHit(1).hit == true, '2D panel')", "=panel2d"), 0);
    bridge.Set2DViewActive(false);

    bridge.SetMenuVisible(true);
    headset.SetHandTracked(kRight, false);
    headset.NextFrame();
    ASSERT_EQ(RunLuauChunk(L, "assert(VRBridge.getPanelHit(1).hit == false, 'untracked')", "=untracked"), 0);
    ASSERT_EQ(RunLuauChunk(L, "assert(VRBridge.getPanelHit(5).hit == false and VRBridge.getPanelHit().hit == false, 'bad hand')", "=badHand"), 0);

    lua_State* L2 = luaL_newstate();
    luaL_openlibs(L2);
    fs25vr::VRBridge bare(nullptr, nullptr);
    ASSERT_TRUE(bare.Inject(L2));
    bare.SetMenuVisible(true);
    ASSERT_EQ(RunLuauChunk(L2, "assert(VRBridge.getPanelHit(1).hit == false)", "=noRuntime"), 0);
    lua_close(L2);
}

TEST_F(CompanionModTest, BridgeSetsTheDrawnCursorWhileThePointerRayHasControlAndClearsItOnTheMenuChanging) {
    FakeTrackedHeadset headset;
    PlaceHead(headset);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    const auto& pointer = [&]() { return headset.manager->GetPointerCursorOverride(); };

    EXPECT_FALSE(pointer().active);
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(true, 0.25, 0.75)", "=setCursor"), 0);
    EXPECT_TRUE(pointer().active);
    EXPECT_FLOAT_EQ(pointer().u, 0.25f);
    EXPECT_FLOAT_EQ(pointer().v, 0.75f);

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(true, 2.0, -1.0)", "=clampCursor"), 0);
    EXPECT_FLOAT_EQ(pointer().u, 1.0f) << "kept on the panel";
    EXPECT_FLOAT_EQ(pointer().v, 0.0f);

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(false)", "=releaseCursor"), 0);
    EXPECT_FALSE(pointer().active) << "the desk mouse has control again";

    // A stale ray cursor must not survive a menu opening or closing.
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(true, 0.5, 0.5)", "=setAgain"), 0);
    bridge.SetMenuVisible(true);
    EXPECT_FALSE(pointer().active);
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(true, 0.5, 0.5)", "=setAgain2"), 0);
    bridge.SetMenuVisible(false);
    EXPECT_FALSE(pointer().active);
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(true, 0.5, 0.5)", "=setAgain3"), 0);
    bridge.Set2DViewActive(true);
    EXPECT_FALSE(pointer().active);
}
