// The menu quad's geometry on its own (spec #247): plain poses and vectors, no OpenXR session. The panel-hit, menu-pointer, Menu
// Distance and OpenXR manager suites cover the same maths through the manager; these look at the module directly.
#include "openxr/menu_quad.h"
#include <gtest/gtest.h>
#include <cmath>

namespace fs25vr {
namespace {

HeadPose HeadAt(float x, float y, float z, float yawRadians = 0.0f) {
    HeadPose head;
    head.position = Vector3(x, y, z);
    head.orientation = Quaternion(0.0f, std::sin(yawRadians * 0.5f), 0.0f, std::cos(yawRadians * 0.5f));
    head.rotation = EulerAngles(0.0f, yawRadians, 0.0f);
    head.isValid = true;
    return head;
}

TEST(MenuQuadTest, WithoutAPoseTheQuadIsAMenuDistanceStraightAheadOfTheOrigin) {
    MenuQuad quad;
    EXPECT_FALSE(quad.HasPose());
    const XrPosef pose = quad.Pose();
    EXPECT_FLOAT_EQ(pose.position.x, 0.0f);
    EXPECT_FLOAT_EQ(pose.position.y, 0.0f);
    EXPECT_FLOAT_EQ(pose.position.z, -MenuQuad::kDefaultDistance);
    EXPECT_FLOAT_EQ(pose.orientation.w, 1.0f);
}

TEST(MenuQuadTest, AlignedQuadSitsAtEyeLevelAheadOfTheHeadsYaw) {
    MenuQuad quad;
    quad.AlignToHeadPose(HeadAt(1.0f, 1.7f, 2.0f, 1.5707964f));  // yawed a quarter turn to the left: forward is -X
    ASSERT_TRUE(quad.HasPose());
    const XrPosef pose = quad.Pose();
    EXPECT_NEAR(pose.position.x, 1.0f - MenuQuad::kDefaultDistance, 1e-5f);
    EXPECT_NEAR(pose.position.y, 1.7f, 1e-6f);
    EXPECT_NEAR(pose.position.z, 2.0f, 1e-5f);
}

TEST(MenuQuadTest, ADistanceBeyondTheRangeIsClampedAndNaNIsTheDefault) {
    EXPECT_FLOAT_EQ(MenuQuad::ClampDistance(0.2f), MenuQuad::kMinDistance);
    EXPECT_FLOAT_EQ(MenuQuad::ClampDistance(50.0f), MenuQuad::kMaxDistance);
    EXPECT_FLOAT_EQ(MenuQuad::ClampDistance(std::nanf("")), MenuQuad::kDefaultDistance);
    EXPECT_FLOAT_EQ(MenuQuad::ClampDistance(3.0f), 3.0f);
}

TEST(MenuQuadTest, AShownQuadMovesAlongTheWayItFacesWhenTheDistanceChanges) {
    MenuQuad quad;
    quad.AlignToHeadPose(HeadAt(0.0f, 1.6f, 0.0f));
    quad.SetDistance(3.0f);
    EXPECT_FLOAT_EQ(quad.Distance(), 3.0f);
    EXPECT_NEAR(quad.Pose().position.z, -3.0f, 1e-5f);
    EXPECT_NEAR(quad.Pose().position.y, 1.6f, 1e-6f);
}

TEST(MenuQuadTest, SizeFollowsTheEyeAspectUnlessSetByHand) {
    MenuQuad quad;
    XrExtent2Df size = quad.Size(0, 0);
    EXPECT_NEAR(size.width, 0.9f * 16.0f / 9.0f, 1e-5f);
    EXPECT_FLOAT_EQ(size.height, 0.9f);

    size = quad.Size(2000, 1000);
    EXPECT_NEAR(size.width, 1.8f, 1e-5f);

    quad.SetCustomSize(2.0f, 1.0f);
    size = quad.Size(2000, 1000);
    EXPECT_FLOAT_EQ(size.width, 2.0f);
    EXPECT_FLOAT_EQ(size.height, 1.0f);

    quad.ClearCustomSize();
    EXPECT_NEAR(quad.Size(2000, 1000).width, 1.8f, 1e-5f);
}

TEST(MenuQuadTest, ARayFromTheFrontHitsAndOneFromBehindOrOffThePanelMisses) {
    XrPosef pose{};
    pose.orientation.w = 1.0f;
    pose.position.z = -1.5f;
    const XrExtent2Df size{ 1.6f, 0.9f };

    PanelHit hit = MenuQuad::RayHitAlong(pose, size, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -2.0f));
    EXPECT_TRUE(hit.hit);
    EXPECT_NEAR(hit.u, 0.5f, 1e-6f);
    EXPECT_NEAR(hit.v, 0.5f, 1e-6f);
    EXPECT_NEAR(hit.distance, 1.5f, 1e-6f);

    EXPECT_FALSE(MenuQuad::RayHitAlong(pose, size, Vector3(0.0f, 0.0f, -3.0f), Vector3(0.0f, 0.0f, 1.0f)).hit);
    EXPECT_FALSE(MenuQuad::RayHitAlong(pose, size, Vector3(5.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -1.0f)).hit);
    EXPECT_FALSE(MenuQuad::RayHitAlong(pose, size, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)).hit);

    hit = MenuQuad::RayHit(pose, size, Vector3(0.4f, 0.45f, 0.0f), Quaternion(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(hit.hit);
    EXPECT_NEAR(hit.u, 0.75f, 1e-5f);
    EXPECT_NEAR(hit.v, 0.0f, 1e-5f);
}

TEST(MenuQuadTest, AQuadPlacedWithoutAValidHeadWaitsForOneAndAFocusWaitsForTheNextFrame) {
    MenuQuad quad;
    HeadPose untracked;
    quad.AlignToHeadPose(untracked);
    EXPECT_FALSE(quad.AwaitsHead(false, 100));
    EXPECT_TRUE(quad.AwaitsHead(true, 100));

    quad.AlignToHeadPose(HeadAt(0.0f, 1.6f, 0.0f));
    EXPECT_FALSE(quad.AwaitsHead(true, 100));

    quad.AwaitHeadAfter(500);
    EXPECT_FALSE(quad.AwaitsHead(true, 499));
    EXPECT_TRUE(quad.AwaitsHead(true, 500));
}

}  // namespace
}  // namespace fs25vr
