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

// ---- Per-eye correction (#35) ----
// Scene: pixel (150, 25) of the 200x100 render, centre (150.5, 25.5), 2 m deep: camera-space point
// (2.02, 0.98, -2). Seen from a camera 1 m to the right it lands at pixel (125.5, 25.5), 25 pixels to the
// left, so a static point's engine vector (previous frame = right eye, 1 m aside) is (-25, 0).

namespace {

RelativeTransform Aside(double x) {
    CameraRecord current;
    current.orientation[3] = 1.0;
    CameraRecord other = current;
    other.position[0] = x;
    return MakeRelativeTransform(ComputeRelativeCamera(current, other));
}

CameraRecord Cam(uint32_t eye, double x) {
    CameraRecord r;
    r.eye = eye;
    r.position[0] = x;
    r.fovY = 1.0f;
    r.nearClip = 0.15f;
    r.farClip = 2000.0f;
    return r;
}

struct Corrected {
    bool applied;
    float x, y;
};

Corrected Correct(float mvX, float mvY, float scale, const RelativeTransform& toPrev, const RelativeTransform& toSame,
                  float depth = 0.45f, uint32_t px = 150, uint32_t py = 25) {
    Corrected c = {};
    c.applied = CorrectMotionVector(px, py, depth, mvX, mvY, scale, scale, 200, 100, TestProjection(),
                                    TestProjection(), TestProjection(), toPrev, toSame, c.x, c.y);
    return c;
}

} // namespace

TEST(MvCorrectionTest, AStaticPointWithTheHeadStillCorrectsToZeroMotionAgainstTheSameEyesPreviousFrame) {
    // Previous frame = the other eye 1 m aside; the same eye's previous camera is where it is now.
    Corrected c = Correct(-25.0f, 0.0f, 1.0f, Aside(1.0), Aside(0.0));
    ASSERT_TRUE(c.applied);
    EXPECT_NEAR(c.x, 0.0f, 1e-2f);
    EXPECT_NEAR(c.y, 0.0f, 1e-2f);
}

TEST(MvCorrectionTest, TheEnginesObjectMotionSurvivesTheCorrection) {
    Corrected c = Correct(-25.0f + 3.0f, -2.0f, 1.0f, Aside(1.0), Aside(0.0));
    ASSERT_TRUE(c.applied);
    EXPECT_NEAR(c.x, 3.0f, 1e-2f);
    EXPECT_NEAR(c.y, -2.0f, 1e-2f);
}

TEST(MvCorrectionTest, AMovedHeadLandsOnTheSameEyesPreviousCameraNotThePreviousFrames) {
    // The same eye's previous camera was 0.5 m to the left: the point (2.02, 0.98, -2) is (2.52, 0.98, -2)
    // from it, pixel x = (2.52 / 4 + 1) / 2 * 200 = 163, a displacement of +12.5 from 150.5.
    Corrected c = Correct(-25.0f, 0.0f, 1.0f, Aside(1.0), Aside(-0.5));
    ASSERT_TRUE(c.applied);
    EXPECT_NEAR(c.x, 12.5f, 1e-2f);
    EXPECT_NEAR(c.y, 0.0f, 1e-2f);
}

TEST(MvCorrectionTest, TheVectorIsInTheUnitsOfTheScaleDlssWillApply) {
    // At scale 2 the stored vector is half the pixel displacement, and so is the stored correction.
    Corrected c = Correct(-12.5f, 0.0f, 2.0f, Aside(1.0), Aside(-0.5));
    ASSERT_TRUE(c.applied);
    EXPECT_NEAR(c.x, 6.25f, 1e-2f);
}

TEST(MvCorrectionTest, TheSkyIsCorrectedForCameraRotationThroughTheFarPlane) {
    // Depth 0 is the far plane, 11 m. The same eye's previous camera is yawed 45 degrees: a near-centre
    // sky pixel moves about +50 pixels (the 2 m centre case above gives exactly +50). Pixel (99, 49)
    // has centre (99.5, 49.5): the point (-0.11, 0.11, -11) lands at pixel x 149.0.
    CameraRecord current;
    current.orientation[3] = 1.0;
    CameraRecord other = current;
    const double half = 3.14159265358979323846 / 8.0;
    other.orientation[1] = std::sin(half);
    other.orientation[3] = std::cos(half);
    RelativeTransform yawed = MakeRelativeTransform(ComputeRelativeCamera(current, other));
    Corrected c = Correct(0.0f, 0.0f, 1.0f, Identity(), yawed, 0.0f, 99, 49);
    ASSERT_TRUE(c.applied);
    EXPECT_NEAR(c.x, 49.5f, 0.2f);
}

TEST(MvCorrectionTest, APointBehindTheSameEyesPreviousCameraKeepsTheEnginesVector) {
    // A same-eye previous camera 5 m ahead has the 2 m point behind it: no correction is possible.
    RelativeTransform ahead = Identity();
    ahead.c[2] = 5.0f; // p_other = p + (0, 0, 5): z = +3, behind the camera
    Corrected c = Correct(-25.0f, 1.5f, 1.0f, Identity(), ahead);
    EXPECT_FALSE(c.applied);
    EXPECT_FLOAT_EQ(c.x, -25.0f);
    EXPECT_FLOAT_EQ(c.y, 1.5f);
}

TEST(MvCorrectionTest, AZeroScaleCannotBeCorrectedAndKeepsTheEnginesVector) {
    Corrected c = Correct(-25.0f, 0.0f, 0.0f, Aside(1.0), Aside(0.0));
    EXPECT_FALSE(c.applied);
    EXPECT_FLOAT_EQ(c.x, -25.0f);
}

TEST(MvCorrectionTest, CorrectedVectorsPassTheSelfCheckWithSubPixelErrorAgainstTheSameEyesCamera) {
    // The self-check against the same eye's previous camera, fed the corrected vector, is sub-pixel;
    // fed the uncorrected one it is off by the camera offset.
    RelativeTransform toPrev = Aside(1.0);
    RelativeTransform toSame = Aside(-0.5);
    // Engine vector of a static point: previous-frame displacement.
    SampleResult engine = EvaluateSample(150, 25, 0.45f, -25.0f, 0.0f, 1.0f, 1.0f, 200, 100, TestProjection(),
                                         TestProjection(), toSame);
    EXPECT_GT(engine.errToPrevious, 10.0f);
    Corrected c = Correct(-25.0f, 0.0f, 1.0f, toPrev, toSame);
    SampleResult corrected = EvaluateSample(150, 25, 0.45f, c.x, c.y, 1.0f, 1.0f, 200, 100, TestProjection(),
                                            TestProjection(), toSame);
    EXPECT_LT(corrected.errToPrevious, 0.5f);
}

// ---- Reverse-Z with an infinite far plane ----
// View depth is near / d, d = 0 is infinitely far. With near = 1, depth 0.5 is 2 m, the same point as
// depth 0.45 in the finite scene above (near 1, far 11), so the scene numbers carry over.

namespace {

Projection InfiniteProjection() {
    Projection p = TestProjection();
    p.farClip = 0.0f; // unused
    p.infiniteDepth = true;
    return p;
}

Corrected CorrectInfinite(float mvX, float mvY, const RelativeTransform& toPrev, const RelativeTransform& toSame,
                          float depth = 0.5f, uint32_t px = 150, uint32_t py = 25) {
    Corrected c = {};
    c.applied = CorrectMotionVector(px, py, depth, mvX, mvY, 1.0f, 1.0f, 200, 100, InfiniteProjection(),
                                    TestProjection(), TestProjection(), toPrev, toSame, c.x, c.y);
    return c;
}

} // namespace

TEST(MvInfiniteDepthTest, ViewDepthIsNearOverDepth) {
    EXPECT_NEAR(ReverseZToViewDepth(1.0f, 1.0f, 0.0f, true), 1.0f, 1e-6f);
    EXPECT_NEAR(ReverseZToViewDepth(0.5f, 1.0f, 0.0f, true), 2.0f, 1e-6f);
    EXPECT_NEAR(ReverseZToViewDepth(0.25f, 0.2f, 0.0f, true), 0.8f, 1e-6f);
}

TEST(MvInfiniteDepthTest, FiniteDepthIsUnchangedByTheFlagBeingOff) {
    // Worked above: d = 0.45 -> 2 m, d = 0 -> the 11 m far plane.
    EXPECT_NEAR(ReverseZToViewDepth(0.45f, 1.0f, 11.0f, false), 2.0f, 1e-5f);
    EXPECT_NEAR(ReverseZToViewDepth(0.0f, 1.0f, 11.0f, false), 11.0f, 1e-4f);
}

TEST(MvInfiniteDepthTest, AnInfiniteDepthDrivenPointLandsWhereTheFiniteOneDoes) {
    float p[3];
    ASSERT_TRUE(UnprojectPixel(150.0f, 25.0f, 0.5f, kW, kH, InfiniteProjection(), p));
    EXPECT_NEAR(p[0], 2.0f, 1e-4f);
    EXPECT_NEAR(p[1], 1.0f, 1e-4f);
    EXPECT_NEAR(p[2], -2.0f, 1e-4f);
    EXPECT_FALSE(UnprojectPixel(100.0f, 50.0f, 0.0f, kW, kH, InfiniteProjection(), p)) << "d = 0 is the sky";
}

TEST(MvInfiniteDepthTest, ASkyPixelHasAFiniteSkyDistanceInsteadOfADivisionByZero) {
    const float z = ReverseZToViewDepth(0.0f, 1.0f, 0.0f, true);
    EXPECT_TRUE(std::isfinite(z));
    EXPECT_NEAR(z, kInfiniteSkyViewDepth, 1.0f);
    // A tiny depth is clamped to the same distance rather than running off to 1e30.
    EXPECT_NEAR(ReverseZToViewDepth(1e-20f, 1.0f, 0.0f, true), kInfiniteSkyViewDepth, 1.0f);
}

TEST(MvInfiniteDepthTest, AStaticPointCorrectsToZeroMotionLikeTheFiniteScene) {
    Corrected c = CorrectInfinite(-25.0f, 0.0f, Aside(1.0), Aside(0.0));
    ASSERT_TRUE(c.applied);
    EXPECT_NEAR(c.x, 0.0f, 1e-2f);
    EXPECT_NEAR(c.y, 0.0f, 1e-2f);
    Corrected moved = CorrectInfinite(-25.0f, 0.0f, Aside(1.0), Aside(-0.5));
    ASSERT_TRUE(moved.applied);
    EXPECT_NEAR(moved.x, 12.5f, 1e-2f);
}

TEST(MvInfiniteDepthTest, TheSkyCarriesRotationButNoTranslation) {
    // Same yaw as the finite sky test: the sky direction is independent of distance, so +49.5.
    CameraRecord current;
    current.orientation[3] = 1.0;
    CameraRecord other = current;
    const double half = 3.14159265358979323846 / 8.0;
    other.orientation[1] = std::sin(half);
    other.orientation[3] = std::cos(half);
    RelativeTransform yawed = MakeRelativeTransform(ComputeRelativeCamera(current, other));
    Corrected c = CorrectInfinite(0.0f, 0.0f, Identity(), yawed, 0.0f, 99, 49);
    ASSERT_TRUE(c.applied);
    EXPECT_NEAR(c.x, 49.5f, 0.2f);

    // A same-eye camera 0.5 m aside moves the sky by nothing (infinitely far).
    Corrected aside = CorrectInfinite(0.0f, 0.0f, Identity(), Aside(-0.5), 0.0f, 99, 49);
    ASSERT_TRUE(aside.applied);
    EXPECT_NEAR(aside.x, 0.0f, 1e-2f);
    EXPECT_NEAR(aside.y, 0.0f, 1e-2f);
}

TEST(MvInfiniteDepthTest, TheSelfCheckScoresAnInfiniteDepthPointLikeTheFiniteOne) {
    // Engine vector of the static point 1 m aside: (-25, 0).
    SampleResult r = EvaluateSample(150, 25, 0.5f, -25.0f, 0.0f, 1.0f, 1.0f, 200, 100, InfiniteProjection(),
                                    TestProjection(), Aside(1.0));
    EXPECT_EQ(r.status, static_cast<uint32_t>(SampleStatus::Valid));
    EXPECT_LT(r.errToPrevious, 0.5f);
    SampleResult sky = EvaluateSample(100, 50, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 200, 100, InfiniteProjection(),
                                      TestProjection(), Aside(1.0));
    EXPECT_EQ(sky.status, static_cast<uint32_t>(SampleStatus::Sky));
}

// ---- A negative motion vector scale ----
// FSR3's dispatch scale in game is (-width/2, +height/2): pixels = stored * scale, so the stored vector is
// the pixel displacement divided by the (signed) scale, and the correction must carry the sign.

TEST(MvCorrectionTest, ANegativeXScaleConvertsTheCorrectionWithItsSign) {
    // Moved-head case at scale 1: the engine's -25 px becomes +12.5 px, a correction of +37.5 px. At scale
    // (-2, 2) the stored engine vector for -25 px is +12.5 and the stored correction is 37.5 / -2 = -18.75,
    // so -6.25 is stored: +12.5 px.
    float x = 0, y = 0;
    ASSERT_TRUE(CorrectMotionVector(150, 25, 0.45f, 12.5f, 0.0f, -2.0f, 2.0f, 200, 100, TestProjection(),
                                    TestProjection(), TestProjection(), Aside(1.0), Aside(-0.5), x, y));
    EXPECT_NEAR(x, -6.25f, 1e-2f);
    EXPECT_NEAR(x * -2.0f, 12.5f, 2e-2f); // in pixels, as with a positive scale
    EXPECT_NEAR(y, 0.0f, 1e-2f);
}

TEST(MvCorrectionTest, AStaticPointStillCorrectsToZeroUnderAMixedSignScale) {
    // Scale (-100, +50) like -width/2, +height/2 of the 200x100 render. The engine's stored vector for the
    // static point (-25 px, 0): x = -25 / -100 = 0.25.
    float x = 9, y = 9;
    ASSERT_TRUE(CorrectMotionVector(150, 25, 0.45f, 0.25f, 0.0f, -100.0f, 50.0f, 200, 100, TestProjection(),
                                    TestProjection(), TestProjection(), Aside(1.0), Aside(0.0), x, y));
    EXPECT_NEAR(x, 0.0f, 1e-4f);
    EXPECT_NEAR(y, 0.0f, 1e-4f);
}

// ---- AFR (#36) ----
// Under AFR the previous frame is the other eye and the same eye's previous frame is two frames back. The
// simulation advances every frame, so the engine's object motion covers one frame where the same-eye
// history is two frames back: object motion takes the eye-stride factor, camera motion does not.

namespace {

Corrected CorrectAfr(float mvX, float mvY, float scale, const RelativeTransform& toPrev, const RelativeTransform& toSame,
                     float objectFactor) {
    Corrected c = {};
    c.applied = CorrectMotionVector(150, 25, 0.45f, mvX, mvY, scale, scale, 200, 100, TestProjection(),
                                    TestProjection(), TestProjection(), toPrev, toSame, c.x, c.y, objectFactor);
    return c;
}

} // namespace

TEST(MvCorrectionAfrTest, TheEyeStrideFactorIsTwoBecauseTheSameEyesHistoryIsTwoFramesBack) {
    EXPECT_FLOAT_EQ(kAfrEyeStride, 2.0f);
}

TEST(MvCorrectionAfrTest, CameraMotionIsNotScaledByTheEyeStride) {
    // A static point seen from a head that moved: the same eye's previous camera was 0.5 m to the left.
    // The camera displacement already spans the right gap, so a factor of 2 changes nothing.
    Corrected plain = CorrectAfr(-25.0f, 0.0f, 1.0f, Aside(1.0), Aside(-0.5), 1.0f);
    Corrected afr = CorrectAfr(-25.0f, 0.0f, 1.0f, Aside(1.0), Aside(-0.5), kAfrEyeStride);
    ASSERT_TRUE(afr.applied);
    EXPECT_NEAR(afr.x, 12.5f, 1e-2f);
    EXPECT_NEAR(afr.x, plain.x, 1e-3f);
    EXPECT_NEAR(afr.y, plain.y, 1e-3f);
}

TEST(MvCorrectionAfrTest, ObjectMotionIsScaledByTheEyeStride) {
    // Head still; the engine saw the point move by (3, -2) pixels over the one frame it covers. The same
    // eye's previous frame is two frames back, so the motion to it is twice that.
    Corrected c = CorrectAfr(-25.0f + 3.0f, -2.0f, 1.0f, Aside(1.0), Aside(0.0), kAfrEyeStride);
    ASSERT_TRUE(c.applied);
    EXPECT_NEAR(c.x, 6.0f, 1e-2f);
    EXPECT_NEAR(c.y, -4.0f, 1e-2f);
}

TEST(MvCorrectionAfrTest, CameraAndObjectMotionCombineEachWithItsOwnFactor) {
    Corrected c = CorrectAfr(-25.0f + 3.0f, -2.0f, 1.0f, Aside(1.0), Aside(-0.5), kAfrEyeStride);
    ASSERT_TRUE(c.applied);
    EXPECT_NEAR(c.x, 12.5f + 6.0f, 1e-2f);
    EXPECT_NEAR(c.y, -4.0f, 1e-2f);
}

TEST(MvCorrectionAfrTest, TheObjectMotionFactorWorksInTheUnitsOfTheScaleDlssWillApply) {
    // Scale 2: stored vector -12.5 + 1.5 (3 px of object motion). Stored result: 6.25 camera + 3 object.
    Corrected c = CorrectAfr(-12.5f + 1.5f, 0.0f, 2.0f, Aside(1.0), Aside(-0.5), kAfrEyeStride);
    ASSERT_TRUE(c.applied);
    EXPECT_NEAR(c.x, 6.25f + 3.0f, 1e-2f);
}

TEST(MvCorrectionAfrTest, AStaticPointStillScoresSubPixelAgainstTheSameEyesCameraWithTheFactorApplied) {
    RelativeTransform toSame = Aside(-0.5);
    Corrected c = CorrectAfr(-25.0f, 0.0f, 1.0f, Aside(1.0), toSame, kAfrEyeStride);
    SampleResult scored = EvaluateSample(150, 25, 0.45f, c.x, c.y, 1.0f, 1.0f, 200, 100, TestProjection(),
                                         TestProjection(), toSame);
    EXPECT_LT(scored.errToPrevious, 0.5f);
}

TEST(MvCorrectionAfrTest, AfrFramesResolveTheOtherEyeAsPreviousFrameAndTheSameEyeTwoFramesBack) {
    CameraRecordStore store;
    ASSERT_TRUE(store.Report(Cam(0, 9.9)));    // frame 0: left
    store.AdvanceFrame();
    ASSERT_TRUE(store.Report(Cam(1, 10.064))); // frame 1: right
    store.AdvanceFrame();
    ASSERT_TRUE(store.Report(Cam(0, 10.0)));   // frame 2: left, the current frame
    ResolvedCameras left = store.Resolve(0);
    ASSERT_TRUE(left.previousFrame.has_value());
    ASSERT_TRUE(left.sameEyePrevious.has_value());
    EXPECT_EQ(left.previousFrame->eye, 1u);
    EXPECT_EQ(left.previousFrame->frame, 1u);
    EXPECT_EQ(left.sameEyePrevious->eye, 0u);
    EXPECT_EQ(left.sameEyePrevious->frame, 0u);
    EXPECT_FALSE(left.PreviousFrameIsSameEye());
    EXPECT_EQ(PlanCorrection(left).decision, CorrectionDecision::Correct);

    store.AdvanceFrame();
    ASSERT_TRUE(store.Report(Cam(1, 10.07))); // frame 3: right
    ResolvedCameras right = store.Resolve(1);
    ASSERT_TRUE(right.previousFrame.has_value());
    ASSERT_TRUE(right.sameEyePrevious.has_value());
    EXPECT_EQ(right.previousFrame->eye, 0u);
    EXPECT_EQ(right.previousFrame->frame, 2u);
    EXPECT_EQ(right.sameEyePrevious->frame, 1u);
    EXPECT_EQ(PlanCorrection(right).decision, CorrectionDecision::Correct);
}

TEST(MvCorrectionPlanTest, ACameraTripleCorrectsAgainstBothPreviousCameras) {
    CameraRecordStore store;
    ASSERT_TRUE(store.Report(Cam(0, 9.9)));   // frame 0, left
    store.AdvanceFrame();
    ASSERT_TRUE(store.Report(Cam(1, 10.064))); // frame 1, right
    store.AdvanceFrame();
    ASSERT_TRUE(store.Report(Cam(0, 10.0)));  // frame 2, left (current)
    CorrectionPlan plan = PlanCorrection(store.Resolve(0));
    ASSERT_EQ(plan.decision, CorrectionDecision::Correct);
    EXPECT_NEAR(plan.current.tanHalfFovY, std::tan(0.5f), 1e-6f);
    EXPECT_NEAR(plan.toPreviousFrame.c[0], -0.064f, 1e-5f);
    EXPECT_NEAR(plan.toSameEye.c[0], 0.1f, 1e-5f);
}

TEST(MvCorrectionPlanTest, AnEyeOrderSwapNeedsNoCorrectionBecauseThePreviousFrameIsTheSameEye) {
    CameraRecordStore store;
    ASSERT_TRUE(store.Report(Cam(0, 10.0)));
    store.AdvanceFrame();
    ASSERT_TRUE(store.Report(Cam(1, 10.064)));
    store.AdvanceFrame();
    ASSERT_TRUE(store.Report(Cam(1, 10.064))); // the right eye renders twice in a row
    CorrectionPlan plan = PlanCorrection(store.Resolve(1));
    EXPECT_EQ(plan.decision, CorrectionDecision::PreviousFrameIsSameEye);
}

TEST(MvCorrectionPlanTest, MissingRecordsPassTheEnginesVectorsThrough) {
    CameraRecordStore store;
    EXPECT_EQ(PlanCorrection(store.Resolve(0)).decision, CorrectionDecision::MissingRecords);

    ASSERT_TRUE(store.Report(Cam(0, 10.0))); // only the current camera
    EXPECT_EQ(PlanCorrection(store.Resolve(0)).decision, CorrectionDecision::MissingRecords);

    CameraRecordStore start; // first frames: the other eye rendered before, this eye has no previous camera
    ASSERT_TRUE(start.Report(Cam(1, 10.064)));
    start.AdvanceFrame();
    ASSERT_TRUE(start.Report(Cam(0, 10.0)));
    EXPECT_EQ(PlanCorrection(start.Resolve(0)).decision, CorrectionDecision::MissingRecords);
}

TEST(MvCorrectionPlanTest, AnUnusableProjectionPassesThrough) {
    CameraRecordStore store;
    ASSERT_TRUE(store.Report(Cam(0, 9.9)));
    store.AdvanceFrame();
    ASSERT_TRUE(store.Report(Cam(1, 10.064)));
    store.AdvanceFrame();
    CameraRecord bad = Cam(0, 10.0);
    bad.fovY = 0.0f;
    ASSERT_TRUE(store.Report(bad));
    EXPECT_EQ(PlanCorrection(store.Resolve(0)).decision, CorrectionDecision::UnusableProjection);
}
