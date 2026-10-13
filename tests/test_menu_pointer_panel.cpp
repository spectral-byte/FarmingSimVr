// Menu pointing in the Injector (spec #71): where a Motion Controller's ray lands on the
// floating menu / 2D Floating Window Mode panel, in normalized panel coordinates, and the drawn
// cursor override the Pointer Ray uses. The panel geometry lives here (ADR-0006, ADR-0008); Lua only
// consumes the hit.
//
// Panel coordinates: u runs left to right and v top to bottom, both 0..1, the same convention as the
// drawn cursor (MenuCursorState::normX/normY). The game's GUI uses a bottom-left origin, so the
// Companion Mod flips v when it talks to the GUI.

#include <gtest/gtest.h>
#include "fake_openxr_runtime.h"
#include "proxy/mirror_window_controller.h"
#include <cmath>

namespace fs25vr {
namespace {

constexpr float kPi = 3.14159265358979f;

XrPosef QuadAt(float x, float y, float z, float yawRadians = 0.0f) {
    XrPosef pose{};
    pose.position = { x, y, z };
    pose.orientation = { 0.0f, std::sin(yawRadians * 0.5f), 0.0f, std::cos(yawRadians * 0.5f) };
    return pose;
}

Quaternion Yaw(float radians) { return Quaternion(0.0f, std::sin(radians * 0.5f), 0.0f, std::cos(radians * 0.5f)); }

constexpr XrExtent2Df kSize{ 1.6f, 0.9f };

} // namespace

TEST(OpenXrManagerTest, RayThroughThePanelCentreHitsAtHalfHalfAndTheRayLength) {
    const PanelHit hit = OpenXrManager::ComputeQuadRayHit(QuadAt(0, 0, -1.5f), kSize, Vector3(0, 0, 0), Quaternion(0, 0, 0, 1));
    EXPECT_TRUE(hit.hit);
    EXPECT_NEAR(hit.u, 0.5f, 1e-5f);
    EXPECT_NEAR(hit.v, 0.5f, 1e-5f);
    EXPECT_NEAR(hit.distance, 1.5f, 1e-5f);
}

TEST(OpenXrManagerTest, PanelHitIsReportedWithTheOriginAtTheTopLeft) {
    // 0.4 m right of centre on a 1.6 m wide panel is u = 0.75; 0.225 m above centre on a 0.9 m high
    // panel is v = 0.25 (v grows downwards).
    const PanelHit hit = OpenXrManager::ComputeQuadRayHit(QuadAt(0, 0, -1.5f), kSize, Vector3(0.4f, 0.225f, 0), Quaternion(0, 0, 0, 1));
    EXPECT_TRUE(hit.hit);
    EXPECT_NEAR(hit.u, 0.75f, 1e-5f);
    EXPECT_NEAR(hit.v, 0.25f, 1e-5f);
}

TEST(OpenXrManagerTest, RayThatLandsBesideAboveOrBelowThePanelMisses) {
    const XrPosef quad = QuadAt(0, 0, -1.5f);
    const Quaternion forward(0, 0, 0, 1);
    EXPECT_FALSE(OpenXrManager::ComputeQuadRayHit(quad, kSize, Vector3(0.81f, 0, 0), forward).hit) << "right of the edge";
    EXPECT_FALSE(OpenXrManager::ComputeQuadRayHit(quad, kSize, Vector3(-0.81f, 0, 0), forward).hit) << "left of the edge";
    EXPECT_FALSE(OpenXrManager::ComputeQuadRayHit(quad, kSize, Vector3(0, 0.46f, 0), forward).hit) << "above the edge";
    EXPECT_FALSE(OpenXrManager::ComputeQuadRayHit(quad, kSize, Vector3(0, -0.46f, 0), forward).hit) << "below the edge";
    EXPECT_TRUE(OpenXrManager::ComputeQuadRayHit(quad, kSize, Vector3(0.79f, 0.44f, 0), forward).hit) << "just inside the corner";
}

TEST(OpenXrManagerTest, RayPointingAwayFromThePanelOrAtItsBackMisses) {
    const XrPosef quad = QuadAt(0, 0, -1.5f);
    EXPECT_FALSE(OpenXrManager::ComputeQuadRayHit(quad, kSize, Vector3(0, 0, 0), Yaw(kPi)).hit) << "pointing away from it";
    EXPECT_FALSE(OpenXrManager::ComputeQuadRayHit(quad, kSize, Vector3(0, 0, -3.0f), Quaternion(0, 0, 0, 1)).hit)
        << "behind the panel, pointing the way it faces";
    EXPECT_FALSE(OpenXrManager::ComputeQuadRayHit(quad, kSize, Vector3(0, 0, -3.0f), Yaw(kPi)).hit)
        << "the panel is only visible from its front, so its back is no target";
    EXPECT_FALSE(OpenXrManager::ComputeQuadRayHit(quad, kSize, Vector3(0, 0, 0), Yaw(kPi * 0.5f)).hit) << "parallel to the panel";
}

TEST(OpenXrManagerTest, PanelTurnedToFaceThePlayerIsHitThroughItsOwnOrientation) {
    // Panel 1.5 m to the left, turned a quarter turn so it faces the origin; the hand turned the same
    // way aims straight at its centre.
    const PanelHit hit = OpenXrManager::ComputeQuadRayHit(QuadAt(-1.5f, 0, 0, kPi * 0.5f), kSize, Vector3(0, 0, 0), Yaw(kPi * 0.5f));
    EXPECT_TRUE(hit.hit);
    EXPECT_NEAR(hit.u, 0.5f, 1e-4f);
    EXPECT_NEAR(hit.v, 0.5f, 1e-4f);
    EXPECT_NEAR(hit.distance, 1.5f, 1e-4f);
}

// ---- A hand's ray against the panel that is submitted ----

namespace {

HeadPose HandPoseAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

// A headset whose head sits at (0, 1.6, 0) looking straight ahead, with the menu quad aligned in front of it.
void PlaceHeadAndAlignPanel(FakeTrackedHeadset& headset) {
    headset.SetHeadPose(HandPoseAt(0.0f, 1.6f, 0.0f));
    headset.manager->PollHeadPose();
    headset.manager->AlignMenuQuadToHeadPose();
}

} // namespace

TEST(OpenXrManagerTest, AHandAimedAtThePanelHitsItWhereTheRayLands) {
    FakeTrackedHeadset headset;
    PlaceHeadAndAlignPanel(headset);  // panel 1.5 m ahead at eye height, 1.6 m x 0.9 m

    headset.SetHandPose(1, HandPoseAt(0.0f, 1.6f, 0.0f));
    headset.SetHandPose(0, HandPoseAt(0.4f, 1.6f + 0.225f, 0.0f));
    headset.NextFrame();

    const PanelHit right = headset.manager->GetPanelHit(1);
    EXPECT_TRUE(right.hit);
    EXPECT_NEAR(right.u, 0.5f, 1e-4f);
    EXPECT_NEAR(right.v, 0.5f, 1e-4f);
    const PanelHit left = headset.manager->GetPanelHit(0);
    EXPECT_TRUE(left.hit);
    EXPECT_NEAR(left.u, 0.75f, 1e-4f);
    EXPECT_NEAR(left.v, 0.25f, 1e-4f);
}

TEST(OpenXrManagerTest, AnUntrackedHandOrAnUnknownHandHasNoPanelHit) {
    FakeTrackedHeadset headset;
    PlaceHeadAndAlignPanel(headset);
    headset.SetHandPose(1, HandPoseAt(0.0f, 1.6f, 0.0f));
    headset.SetHandTracked(1, false);
    headset.NextFrame();

    EXPECT_FALSE(headset.manager->GetPanelHit(1).hit) << "untracked";
    EXPECT_FALSE(headset.manager->GetPanelHit(0).hit) << "never placed, so untracked";
    EXPECT_FALSE(headset.manager->GetPanelHit(7).hit) << "no such hand";
}

TEST(OpenXrManagerTest, PanelHitFollowsTheLocalSpaceThePanelIsSubmittedIn) {
    FakeTrackedHeadset headset;
    PlaceHeadAndAlignPanel(headset);
    headset.manager->Recenter();  // the local space now has its origin at the head, 1.6 m up
    headset.NextFrame();

    // Hands are located in the re-based local space, and so is the submitted panel, which stays where
    // it was aligned (1.6 m up in that space). Ray and panel are tested in that one space, so the
    // hit lands where the panel is actually drawn, not where it was before the recentre.
    const XrPosef quad = headset.manager->ComputeMenuQuadPose();
    headset.SetHandPose(1, HandPoseAt(quad.position.x, quad.position.y + 1.6f, 0.0f));
    headset.NextFrame();
    const PanelHit atDrawnPanel = headset.manager->GetPanelHit(1);
    EXPECT_TRUE(atDrawnPanel.hit);
    EXPECT_NEAR(atDrawnPanel.u, 0.5f, 1e-4f);
    EXPECT_NEAR(atDrawnPanel.v, 0.5f, 1e-4f);

    headset.SetHandPose(1, HandPoseAt(0.0f, 1.6f, 0.0f));  // where the panel was drawn before the recentre
    headset.NextFrame();
    EXPECT_FALSE(headset.manager->GetPanelHit(1).hit);
}

// ---- The drawn cursor follows the Pointer Ray ----

TEST(MirrorWindowControllerTest, CursorIsDrawnWhereThePointerRayLandsWhileItHasControl) {
    FakeTrackedHeadset headset;
    MirrorWindowController controller;

    headset.manager->SetPointerCursorOverride(true, 0.3f, 0.7f);
    controller.UpdateMenuCursorState(nullptr, headset.manager.get());

    const MenuCursorState& cursor = headset.manager->GetMenuCursorState();
    EXPECT_TRUE(cursor.visible);
    EXPECT_FLOAT_EQ(cursor.normX, 0.3f);
    EXPECT_FLOAT_EQ(cursor.normY, 0.7f);

    headset.manager->SetPointerCursorOverride(true, 0.6f, 0.2f);
    controller.UpdateMenuCursorState(nullptr, headset.manager.get());
    EXPECT_FLOAT_EQ(cursor.normX, 0.6f);
    EXPECT_FLOAT_EQ(cursor.normY, 0.2f);
}

} // namespace fs25vr
