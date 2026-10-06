#include <gtest/gtest.h>
#include "core/camera_records.h"
#include "ngx/mv_reprojection.h"
#include <cmath>
#include <vector>

// The reprojection math the self-check shader mirrors. Every expected value below is worked out by hand
// for a 200x100 render (aspect 2), a 90 degree vertical FOV (tan(fov/2) = 1) and near/far of 1 and 11.

using namespace fs25vr;
using namespace fs25vr::mvreproj;

namespace {

constexpr float kW = 200.0f;
constexpr float kH = 100.0f;

Projection TestProjection() {
    Projection p;
    p.tanHalfFovY = 1.0f;
    p.aspect = 2.0f;
    p.nearClip = 1.0f;
    p.farClip = 11.0f;
    return p;
}

RelativeTransform Identity() {
    RelativeTransform t = {};
    t.m[0][0] = t.m[1][1] = t.m[2][2] = 1.0f;
    return t;
}

// Reverse-Z depth of a point z metres in front of the camera: d = n (f - z) / (z (f - n)).
float DepthAtMetres2() { return 0.45f; } // 1 * (11 - 2) / (2 * 10)

} // namespace

TEST(MvReprojectionTest, ReverseZMapsNearToOneAndFarToZero) {
    EXPECT_NEAR(ReverseZToViewDepth(1.0f, 1.0f, 11.0f), 1.0f, 1e-5f);
    EXPECT_NEAR(ReverseZToViewDepth(0.0f, 1.0f, 11.0f), 11.0f, 1e-4f);
    // d = 0.5 -> z = 11 / (1 + 0.5 * 10) = 11 / 6.
    EXPECT_NEAR(ReverseZToViewDepth(0.5f, 1.0f, 11.0f), 11.0f / 6.0f, 1e-5f);
    // d = 0.45 -> z = 11 / (1 + 4.5) = 2.
    EXPECT_NEAR(ReverseZToViewDepth(DepthAtMetres2(), 1.0f, 11.0f), 2.0f, 1e-5f);
}

TEST(MvReprojectionTest, UnprojectPlacesAPixelInFrontOfTheCameraLookingDownMinusZ) {
    // Pixel (150, 25): ndc x = 2*150/200 - 1 = 0.5, ndc y = 1 - 2*25/100 = 0.5, depth 2 m.
    // x = 0.5 * tan * aspect * 2 = 2, y = 0.5 * tan * 2 = 1, z = -2.
    float p[3];
    ASSERT_TRUE(UnprojectPixel(150.0f, 25.0f, DepthAtMetres2(), kW, kH, TestProjection(), p));
    EXPECT_NEAR(p[0], 2.0f, 1e-4f);
    EXPECT_NEAR(p[1], 1.0f, 1e-4f);
    EXPECT_NEAR(p[2], -2.0f, 1e-4f);

    // The image centre sits on the optical axis.
    ASSERT_TRUE(UnprojectPixel(100.0f, 50.0f, DepthAtMetres2(), kW, kH, TestProjection(), p));
    EXPECT_NEAR(p[0], 0.0f, 1e-4f);
    EXPECT_NEAR(p[1], 0.0f, 1e-4f);
    EXPECT_NEAR(p[2], -2.0f, 1e-4f);
}

TEST(MvReprojectionTest, UnprojectRejectsTheSkyAtTheFarPlane) {
    float p[3];
    EXPECT_FALSE(UnprojectPixel(100.0f, 50.0f, 0.0f, kW, kH, TestProjection(), p));
}

TEST(MvReprojectionTest, ProjectIsTheInverseOfUnproject) {
    const float p[3] = {2.0f, 1.0f, -2.0f};
    float px = 0, py = 0;
    ASSERT_TRUE(ProjectToPixel(p, kW, kH, TestProjection(), px, py));
    EXPECT_NEAR(px, 150.0f, 1e-3f);
    EXPECT_NEAR(py, 25.0f, 1e-3f);

    const float behind[3] = {0.0f, 0.0f, 2.0f};
    EXPECT_FALSE(ProjectToPixel(behind, kW, kH, TestProjection(), px, py));
}

TEST(MvReprojectionTest, RelativeTransformOfAnIdenticalCameraIsTheIdentity) {
    CameraRecord cam;
    cam.orientation[3] = 1.0;
    RelativeTransform t = MakeRelativeTransform(ComputeRelativeCamera(cam, cam));
    const float p[3] = {1.0f, -2.0f, -3.0f};
    float out[3];
    ApplyRelativeTransform(t, p, out);
    EXPECT_NEAR(out[0], 1.0f, 1e-6f);
    EXPECT_NEAR(out[1], -2.0f, 1e-6f);
    EXPECT_NEAR(out[2], -3.0f, 1e-6f);
}

TEST(MvReprojectionTest, ASidewaysMovedCameraShiftsTheScenePixelsOppositely) {
    // The other camera is 1 m to the right of the current one (translation expressed in the current
    // camera's axes). The point (2, 1, -2) is then (1, 1, -2) from it: ndc x = 1 / (2 * 2) = 0.25 ->
    // pixel 125, ndc y = 1 / 2 = 0.5 -> pixel 25. Displacement from pixel (150, 25) is (-25, 0).
    CameraRecord current;
    current.orientation[3] = 1.0;
    CameraRecord other = current;
    other.position[0] = 1.0;
    RelativeTransform t = MakeRelativeTransform(ComputeRelativeCamera(current, other));

    SampleResult r = EvaluateSample(150, 25, DepthAtMetres2(), 0.0f, 0.0f, 1.0f, 1.0f, 200, 100,
                                    TestProjection(), TestProjection(), t);
    // With a zero engine vector the error equals the magnitude of the predicted displacement.
    ASSERT_EQ(r.status, static_cast<uint32_t>(SampleStatus::Valid));
    EXPECT_NEAR(r.errToPrevious, 25.0f, 0.51f); // sample centre is half a pixel off the whole-pixel hand case
    EXPECT_NEAR(r.errToCurrent, 25.0f, 0.51f);
}

TEST(MvReprojectionTest, ARotatedCameraProjectsThroughTheInverseRotation) {
    // The other camera is yawed 45 degrees about +Y (it looks to the left of the current one). The
    // centre point (0, 0, -2) is, in its axes, (sqrt2, 0, -sqrt2): ndc x = sqrt2 / (sqrt2 * 2) = 0.5 ->
    // pixel 150. Displacement from the centre (100, 50) is (+50, 0).
    CameraRecord current;
    current.orientation[3] = 1.0;
    CameraRecord other = current;
    const double half = 3.14159265358979323846 / 8.0; // half of 45 degrees
    other.orientation[1] = std::sin(half);
    other.orientation[3] = std::cos(half);
    RelativeTransform t = MakeRelativeTransform(ComputeRelativeCamera(current, other));

    float p[3] = {0.0f, 0.0f, -2.0f};
    float q[3];
    ApplyRelativeTransform(t, p, q);
    EXPECT_NEAR(q[0], std::sqrt(2.0f), 1e-5f);
    EXPECT_NEAR(q[1], 0.0f, 1e-5f);
    EXPECT_NEAR(q[2], -std::sqrt(2.0f), 1e-5f);

    float px = 0, py = 0;
    ASSERT_TRUE(ProjectToPixel(q, kW, kH, TestProjection(), px, py));
    EXPECT_NEAR(px, 150.0f, 1e-3f);
    EXPECT_NEAR(py, 50.0f, 1e-3f);
}

TEST(MvReprojectionTest, TheRelativeTransformUsesTheTranslationInTheCurrentCamerasAxes) {
    // The current camera is yawed 90 degrees about +Y, so its right axis is world -Z. The other camera
    // sits 1 m along world -Z: that is 1 m to the right of the current one, so the point straight ahead
    // of the current camera at 2 m (0, 0, -2) is (-1, 0, -2) from the other camera.
    CameraRecord current;
    const double half = 3.14159265358979323846 / 4.0;
    current.orientation[1] = std::sin(half);
    current.orientation[3] = std::cos(half);
    CameraRecord other = current;
    other.position[2] = -1.0;
    RelativeTransform t = MakeRelativeTransform(ComputeRelativeCamera(current, other));

    const float p[3] = {0.0f, 0.0f, -2.0f};
    float out[3];
    ApplyRelativeTransform(t, p, out);
    EXPECT_NEAR(out[0], -1.0f, 1e-5f);
    EXPECT_NEAR(out[1], 0.0f, 1e-5f);
    EXPECT_NEAR(out[2], -2.0f, 1e-5f);
}

TEST(MvReprojectionTest, SignConventionsAndUnitsAreComparedAgainstTheScaledEngineVector) {
    // Pixel (150, 25) on the whole-pixel grid has centre (150.5, 25.5); use a camera shift that makes the
    // displacement exact at the centre instead: the right camera 1 m aside, point on the optical axis.
    // Centre pixel (99.5, 49.5) is not the optical axis, so build the expectation from the prediction
    // itself: the engine vector equal to the predicted displacement is zero error under "previous
    // minus current", and twice the displacement error under the other convention.
    CameraRecord current;
    current.orientation[3] = 1.0;
    CameraRecord other = current;
    other.position[0] = 1.0;
    RelativeTransform t = MakeRelativeTransform(ComputeRelativeCamera(current, other));

    // Hand case (continuous pixel 150,25): displacement (-25, 0). Pixel centre 150.5, 25.5 unprojects to
    //   x = (2*150.5/200 - 1) * 4 = 2.02, y = (1 - 2*25.5/100) * 2 = 0.98, z = -2
    // -> from the other camera (1.02, 0.98, -2) -> pixel x = (1.02 / 4 + 1) / 2 * 200 = 125.5,
    //    pixel y = (1 - 0.98 / 2) / 2 * 100 = 25.5, so the displacement is exactly (-25, 0).
    const float depth = DepthAtMetres2();

    // Engine vector (-25, 0), scale 1: matches "MV = previous - current".
    SampleResult a = EvaluateSample(150, 25, depth, -25.0f, 0.0f, 1.0f, 1.0f, 200, 100,
                                    TestProjection(), TestProjection(), t);
    EXPECT_NEAR(a.errToPrevious, 0.0f, 1e-2f);
    EXPECT_NEAR(a.errToCurrent, 50.0f, 1e-2f);

    // Engine vector (25, 0), scale 1: matches the opposite convention, "MV = current - previous".
    SampleResult b = EvaluateSample(150, 25, depth, 25.0f, 0.0f, 1.0f, 1.0f, 200, 100,
                                    TestProjection(), TestProjection(), t);
    EXPECT_NEAR(b.errToPrevious, 50.0f, 1e-2f);
    EXPECT_NEAR(b.errToCurrent, 0.0f, 1e-2f);

    // Units: DLSS multiplies the engine vector by the scale it is given. (-12.5, 0) at scale 2 is the
    // same (-25, 0) in pixels; at scale 1 it is 12.5 pixels short.
    SampleResult c = EvaluateSample(150, 25, depth, -12.5f, 0.0f, 2.0f, 2.0f, 200, 100,
                                    TestProjection(), TestProjection(), t);
    EXPECT_NEAR(c.errToPrevious, 0.0f, 1e-2f);
    SampleResult d = EvaluateSample(150, 25, depth, -12.5f, 0.0f, 1.0f, 1.0f, 200, 100,
                                    TestProjection(), TestProjection(), t);
    EXPECT_NEAR(d.errToPrevious, 12.5f, 1e-2f);

    // The error is the length of the 2D difference: a vertical miss of 4 pixels with a horizontal 3
    // pixel miss is 5 pixels.
    SampleResult e = EvaluateSample(150, 25, depth, -22.0f, 4.0f, 1.0f, 1.0f, 200, 100,
                                    TestProjection(), TestProjection(), t);
    EXPECT_NEAR(e.errToPrevious, 5.0f, 1e-2f);
}

TEST(MvReprojectionTest, SkyAndOffScreenSamplesAreSkippedAndSaySo) {
    CameraRecord current;
    current.orientation[3] = 1.0;
    CameraRecord other = current;
    other.position[0] = 1.0;
    RelativeTransform t = MakeRelativeTransform(ComputeRelativeCamera(current, other));

    SampleResult sky = EvaluateSample(150, 25, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 200, 100,
                                      TestProjection(), TestProjection(), t);
    EXPECT_EQ(sky.status, static_cast<uint32_t>(SampleStatus::Sky));

    // A point at the left edge 2 m away moves 25 px left under the shift above and leaves the image.
    SampleResult off = EvaluateSample(5, 50, DepthAtMetres2(), 0.0f, 0.0f, 1.0f, 1.0f, 200, 100,
                                      TestProjection(), TestProjection(), t);
    EXPECT_EQ(off.status, static_cast<uint32_t>(SampleStatus::OffScreen));

    // A previous camera 5 m ahead sees the 2 m point behind it and skips it.
    CameraRecord ahead = current;
    ahead.position[2] = -5.0;
    RelativeTransform behind = MakeRelativeTransform(ComputeRelativeCamera(current, ahead));
    SampleResult back = EvaluateSample(100, 50, DepthAtMetres2(), 0.0f, 0.0f, 1.0f, 1.0f, 200, 100,
                                       TestProjection(), TestProjection(), behind);
    EXPECT_EQ(back.status, static_cast<uint32_t>(SampleStatus::OffScreen));
}

TEST(MvReprojectionTest, GridSamplesAreSpreadAcrossTheImage) {
    // 4 samples across 200 pixels: centres of 50 pixel cells, 25, 75, 125, 175.
    EXPECT_EQ(GridSampleCoordinate(0, 4, 200), 25u);
    EXPECT_EQ(GridSampleCoordinate(1, 4, 200), 75u);
    EXPECT_EQ(GridSampleCoordinate(3, 4, 200), 175u);
    // A grid denser than the image clamps to the last pixel.
    EXPECT_EQ(GridSampleCoordinate(7, 8, 4), 3u);
}

TEST(MvReprojectionTest, SummaryReportsMeanMedianAndSkippedFractionPerConvention) {
    std::vector<SampleResult> samples;
    auto valid = [&](float toPrev, float toCur) {
        SampleResult s = {};
        s.status = static_cast<uint32_t>(SampleStatus::Valid);
        s.errToPrevious = toPrev;
        s.errToCurrent = toCur;
        samples.push_back(s);
    };
    valid(1.0f, 10.0f);
    valid(2.0f, 20.0f);
    valid(3.0f, 30.0f);
    valid(10.0f, 40.0f);
    SampleResult sky = {};
    sky.status = static_cast<uint32_t>(SampleStatus::Sky);
    samples.push_back(sky);
    SampleResult off = {};
    off.status = static_cast<uint32_t>(SampleStatus::OffScreen);
    samples.push_back(off);
    samples.push_back(off);
    samples.push_back(off);

    SelfCheckStats stats = Summarize(samples.data(), samples.size());
    EXPECT_EQ(stats.total, 8u);
    EXPECT_EQ(stats.valid, 4u);
    EXPECT_EQ(stats.skipped, 4u);
    EXPECT_DOUBLE_EQ(stats.skippedFraction, 0.5);
    EXPECT_DOUBLE_EQ(stats.meanToPrevious, 4.0);   // (1+2+3+10)/4
    EXPECT_DOUBLE_EQ(stats.medianToPrevious, 2.5); // between 2 and 3
    EXPECT_DOUBLE_EQ(stats.meanToCurrent, 25.0);
    EXPECT_DOUBLE_EQ(stats.medianToCurrent, 25.0);
    EXPECT_EQ(BetterConvention(stats), Convention::ToPrevious);

    SelfCheckStats flipped = stats;
    flipped.medianToCurrent = 0.1;
    EXPECT_EQ(BetterConvention(flipped), Convention::ToCurrent);
}

TEST(MvReprojectionTest, SummaryOfNothingValidHasNoVerdict) {
    SampleResult sky = {};
    sky.status = static_cast<uint32_t>(SampleStatus::Sky);
    SelfCheckStats stats = Summarize(&sky, 1);
    EXPECT_EQ(stats.valid, 0u);
    EXPECT_DOUBLE_EQ(stats.skippedFraction, 1.0);
    EXPECT_EQ(BetterConvention(stats), Convention::None);

    SelfCheckStats empty = Summarize(nullptr, 0);
    EXPECT_EQ(empty.total, 0u);
    EXPECT_EQ(BetterConvention(empty), Convention::None);
}
