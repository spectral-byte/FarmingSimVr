#include <gtest/gtest.h>
#include "core/camera_records.h"
#include <cmath>
#include <string>

using fs25vr::CameraRecord;
using fs25vr::CameraRecordStore;

namespace {

CameraRecord MakeRecord(uint32_t eye, double x, double y = 0.0, double z = 0.0) {
    CameraRecord r;
    r.eye = eye;
    r.position[0] = x;
    r.position[1] = y;
    r.position[2] = z;
    r.orientation[3] = 1.0; // identity
    r.fovY = 1.5f;
    r.nearClip = 0.15f;
    r.farClip = 2000.0f;
    return r;
}

// Quaternion for a rotation of angle radians about +Y.
void SetYaw(CameraRecord& r, double angle) {
    r.orientation[0] = 0.0;
    r.orientation[1] = std::sin(angle / 2.0);
    r.orientation[2] = 0.0;
    r.orientation[3] = std::cos(angle / 2.0);
}

// Plays one stereo frame: the given eye reports its camera, then the Present advances the counter.
void PlayFrame(CameraRecordStore& store, uint32_t eye, double x) {
    ASSERT_TRUE(store.Report(MakeRecord(eye, x)));
    store.AdvanceFrame();
}

} // namespace

TEST(CameraRecordStoreTest, ReportedRecordIsStampedWithTheCurrentRenderedFrame) {
    CameraRecordStore store;
    for (int i = 0; i < 5; ++i) store.AdvanceFrame();
    ASSERT_EQ(store.GetFrame(), 5u);

    ASSERT_TRUE(store.Report(MakeRecord(1, 3.0)));

    auto resolved = store.Resolve(1);
    ASSERT_TRUE(resolved.current.has_value());
    EXPECT_EQ(resolved.current->frame, 5u);
    EXPECT_EQ(resolved.current->eye, 1u);
    EXPECT_DOUBLE_EQ(resolved.current->position[0], 3.0);
    EXPECT_FLOAT_EQ(resolved.current->fovY, 1.5f);
    EXPECT_FLOAT_EQ(resolved.current->nearClip, 0.15f);
    EXPECT_FLOAT_EQ(resolved.current->farClip, 2000.0f);
}

TEST(CameraRecordStoreTest, ReportRejectsUnknownEyesAndDegenerateOrientations) {
    CameraRecordStore store;
    EXPECT_FALSE(store.Report(MakeRecord(2, 0.0)));
    CameraRecord zeroQuat = MakeRecord(0, 0.0);
    zeroQuat.orientation[3] = 0.0;
    EXPECT_FALSE(store.Report(zeroQuat));
    EXPECT_FALSE(store.Resolve(0).current.has_value());
}

TEST(CameraRecordStoreTest, AFrameWithNoRecordResolvesAsMissingNotAsAStaleRecord) {
    CameraRecordStore store;
    PlayFrame(store, 0, 0.0);
    PlayFrame(store, 1, 1.0);

    // Frame 2 reports nothing (loading screen, menu).
    auto resolved = store.Resolve(0);
    EXPECT_FALSE(resolved.current.has_value());
    ASSERT_TRUE(resolved.previousFrame.has_value());
    EXPECT_EQ(resolved.previousFrame->frame, 1u);
    EXPECT_FALSE(resolved.previousFrameRelative.has_value());
    EXPECT_FALSE(resolved.sameEyeRelative.has_value());

    // Two frames on, the old records are too old to be the previous frame.
    store.AdvanceFrame();
    resolved = store.Resolve(1);
    EXPECT_FALSE(resolved.current.has_value());
    EXPECT_FALSE(resolved.previousFrame.has_value());
}

TEST(CameraRecordStoreTest, AnotherEyesRecordInTheCurrentFrameIsNotTheCurrentCamera) {
    CameraRecordStore store;
    ASSERT_TRUE(store.Report(MakeRecord(0, 0.0)));
    EXPECT_FALSE(store.Resolve(1).current.has_value());
    EXPECT_TRUE(store.Resolve(0).current.has_value());
}

TEST(CameraRecordStoreTest, ResolvesOtherEyePreviousFrameAndSameEyeRecordsInAnAlternatingSequence) {
    CameraRecordStore store;
    PlayFrame(store, 0, 0.0);  // frame 0: L
    PlayFrame(store, 1, 0.064); // frame 1: R
    ASSERT_TRUE(store.Report(MakeRecord(0, 0.5))); // frame 2: L, head moved

    auto resolved = store.Resolve(0);
    ASSERT_TRUE(resolved.current.has_value());
    ASSERT_TRUE(resolved.previousFrame.has_value());
    ASSERT_TRUE(resolved.sameEyePrevious.has_value());
    EXPECT_EQ(resolved.previousFrame->eye, 1u);
    EXPECT_EQ(resolved.previousFrame->frame, 1u);
    EXPECT_EQ(resolved.sameEyePrevious->eye, 0u);
    EXPECT_EQ(resolved.sameEyePrevious->frame, 0u);
}

TEST(CameraRecordStoreTest, SameEyeRecordTooOldToBeTheLastRenderedOfThatEyeIsMissing) {
    CameraRecordStore store;
    PlayFrame(store, 0, 0.0);
    for (int i = 0; i < 6; ++i) PlayFrame(store, 1, 0.0);
    ASSERT_TRUE(store.Report(MakeRecord(0, 0.0)));
    EXPECT_FALSE(store.Resolve(0).sameEyePrevious.has_value());
}

TEST(CameraRecordStoreTest, SameEyeRecordIsMissingWhenTheRealPreviousSameEyeFrameHadNoRecord) {
    CameraRecordStore store;
    PlayFrame(store, 0, 0.0); // frame 0: L
    PlayFrame(store, 1, 0.0); // frame 1: R
    store.AdvanceFrame();     // frame 2: the left eye rendered but reported nothing
    ASSERT_TRUE(store.Report(MakeRecord(0, 0.5))); // frame 3: L; the L record at frame 0 is not its previous render

    auto resolved = store.Resolve(0);
    ASSERT_TRUE(resolved.current.has_value());
    EXPECT_FALSE(resolved.sameEyePrevious.has_value());
    EXPECT_FALSE(resolved.sameEyeRelative.has_value());
}

TEST(CameraRecordStoreTest, SameEyeRecordTwoFramesBackIsMissingWhenTheFrameBetweenHasNoRecord) {
    CameraRecordStore store;
    PlayFrame(store, 0, 0.0); // frame 0: L
    store.AdvanceFrame();     // frame 1: no record (could have been L)
    ASSERT_TRUE(store.Report(MakeRecord(0, 0.5))); // frame 2: L

    EXPECT_FALSE(store.Resolve(0).sameEyePrevious.has_value());
}

TEST(CameraRecordStoreTest, SameEyeRecordTwoFramesBackStillResolvesUnderAlternateFrameRendering) {
    CameraRecordStore store;
    PlayFrame(store, 0, 0.0);
    PlayFrame(store, 1, 0.064);
    ASSERT_TRUE(store.Report(MakeRecord(0, 0.5)));
    auto resolved = store.Resolve(0);
    ASSERT_TRUE(resolved.sameEyePrevious.has_value());
    EXPECT_EQ(resolved.sameEyePrevious->frame, 0u);
}

TEST(CameraRecordStoreTest, ClearDropsEveryRecord) {
    CameraRecordStore store;
    PlayFrame(store, 0, 0.0);
    ASSERT_TRUE(store.Report(MakeRecord(1, 1.0)));
    store.Clear();
    EXPECT_FALSE(store.Resolve(1).current.has_value());
    EXPECT_FALSE(store.Resolve(1).previousFrame.has_value());
}

TEST(CameraRecordStoreTest, ReportingTheSameEyeTwiceInAFrameKeepsTheLatest) {
    CameraRecordStore store;
    ASSERT_TRUE(store.Report(MakeRecord(0, 1.0)));
    ASSERT_TRUE(store.Report(MakeRecord(0, 2.0)));
    EXPECT_DOUBLE_EQ(store.Resolve(0).current->position[0], 2.0);
}

TEST(CameraRecordStoreTest, RelativeTranslationIsExpressedInTheCurrentCamerasAxes) {
    CameraRecordStore store;
    // Both cameras are yawed 90 degrees about Y, so camera-local +X is world -Z.
    CameraRecord prev = MakeRecord(1, 0.0, 0.0, 0.0);
    SetYaw(prev, 1.5707963267948966);
    ASSERT_TRUE(store.Report(prev));
    store.AdvanceFrame();

    CameraRecord cur = MakeRecord(0, 0.0, 0.0, -0.064);
    SetYaw(cur, 1.5707963267948966);
    ASSERT_TRUE(store.Report(cur));

    auto resolved = store.Resolve(0);
    ASSERT_TRUE(resolved.previousFrameRelative.has_value());
    const auto& rel = *resolved.previousFrameRelative;
    // prev - cur = (0, 0, +0.064) in world. Camera-local X is world (cos90, 0, -sin90) = (0,0,-1),
    // so the world offset +0.064 on Z is -0.064 along camera X.
    EXPECT_NEAR(rel.translation[0], -0.064, 1e-12);
    EXPECT_NEAR(rel.translation[1], 0.0, 1e-12);
    EXPECT_NEAR(rel.translation[2], 0.0, 1e-12);
    EXPECT_NEAR(rel.translationLength, 0.064, 1e-12);
    EXPECT_NEAR(rel.rotationAngle, 0.0, 1e-9);
}

TEST(CameraRecordStoreTest, RelativeRotationAngleIsTheYawBetweenTheCameras) {
    CameraRecordStore store;
    CameraRecord prev = MakeRecord(1, 0.0);
    SetYaw(prev, 0.25);
    ASSERT_TRUE(store.Report(prev));
    store.AdvanceFrame();
    CameraRecord cur = MakeRecord(0, 0.0);
    SetYaw(cur, 0.75);
    ASSERT_TRUE(store.Report(cur));

    const auto& rel = *store.Resolve(0).previousFrameRelative;
    EXPECT_NEAR(rel.rotationAngle, 0.5, 1e-12);
}

TEST(CameraRecordStoreTest, RelativeTransformsKeepMillimetrePrecisionAtLargeMapCoordinates) {
    CameraRecordStore store;
    // A float cannot hold 4,000,000.064 to better than about 0.25 m; double keeps it to nanometres.
    const double base = 4000000.0;
    ASSERT_TRUE(store.Report(MakeRecord(1, base, 123456.0, -987654.0)));
    store.AdvanceFrame();
    ASSERT_TRUE(store.Report(MakeRecord(0, base + 0.064, 123456.0, -987654.0)));

    const auto& rel = *store.Resolve(0).previousFrameRelative;
    EXPECT_NEAR(rel.translation[0], -0.064, 1e-9);
    EXPECT_NEAR(rel.translationLength, 0.064, 1e-9);
}

TEST(CameraRecordStoreTest, DescriptionNamesMissingRecordsAndReportsRelativeTransforms) {
    CameraRecordStore store;
    auto none = store.Resolve(0);
    std::string text = fs25vr::DescribeResolvedCameras(none);
    EXPECT_NE(text.find("Cur=missing"), std::string::npos) << text;
    EXPECT_NE(text.find("Prev=missing"), std::string::npos) << text;
    EXPECT_NE(text.find("SameEye=missing"), std::string::npos) << text;

    ASSERT_TRUE(store.Report(MakeRecord(1, 0.0)));
    store.AdvanceFrame();
    ASSERT_TRUE(store.Report(MakeRecord(0, 0.064)));
    text = fs25vr::DescribeResolvedCameras(store.Resolve(0));
    EXPECT_NE(text.find("Cur=eye0@f1"), std::string::npos) << text;
    EXPECT_NE(text.find("Prev=eye1@f0"), std::string::npos) << text;
    EXPECT_NE(text.find("SameEye=missing"), std::string::npos) << text;
    EXPECT_NE(text.find("PrevRel=(dx=-0.0640"), std::string::npos) << text;
    EXPECT_NE(text.find("rot=0.00"), std::string::npos) << text;
}

