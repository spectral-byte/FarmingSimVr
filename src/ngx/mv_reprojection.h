#pragma once

#include "core/camera_records.h"
#include <cstddef>
#include <cstdint>

// CPU mirror of the motion vector self-check shader (see mv_pass.cpp). The shader and these functions
// implement the same steps, so the conventions they share are pinned down by unit tests that need no GPU.
//
// Conventions (all assumptions the self-check exists to prove in game):
//  - Depth is reverse-Z (DLSS creation flag DepthInverted): 1 at the near plane, 0 at the far plane.
//  - Camera space is the engine camera node's: +X right, +Y up, looking down -Z.
//  - Pixel coordinates have the origin top-left, +Y down; pixel centres sit at +0.5.
//  - The motion vector DLSS receives is the engine's vector times the MV scale it is given, in pixels.
//    Two sign conventions are scored: "to previous" (vector = previous position - current position,
//    which is DLSS's documented convention) and "to current" (the opposite).
namespace fs25vr::mvreproj {

struct Projection {
    float tanHalfFovY = 1.0f;
    float aspect = 1.0f; // render width / render height
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    // Reverse-Z with an infinite far plane (FSR3 DepthInfinite): view depth = nearClip / depth, depth 0 is
    // infinitely far and farClip is unused. Only the camera that rendered the depth buffer reads it.
    bool infiniteDepth = false;
};

// View depth given to a sky pixel (depth 0) under an infinite far plane: far enough that translation is
// below a hundredth of a pixel, near enough for float32 to keep the rotation exact.
constexpr float kInfiniteSkyViewDepth = 100000.0f;

// Maps a point from the current camera's space into another camera's space: p_other = m * p + c.
struct RelativeTransform {
    float m[3][3] = {};
    float c[3] = {};
};

// Builds the transform from the double-precision relative camera (translation of the other camera in
// the current camera's axes, rotation = conj(current) * other).
RelativeTransform MakeRelativeTransform(const RelativeCamera& relative);
void ApplyRelativeTransform(const RelativeTransform& t, const float p[3], float out[3]);

// View-space distance in front of the camera for a reverse-Z depth value. With infiniteDepth the far plane
// is at infinity (view depth = near / depth, capped at kInfiniteSkyViewDepth; depth 0 gives the cap).
float ReverseZToViewDepth(float depth, float nearClip, float farClip, bool infiniteDepth = false);

// Camera-space position of the pixel at continuous coordinates (px, py) with the given depth.
// False for the sky (depth at or beyond the far plane).
bool UnprojectPixel(float px, float py, float depth, float width, float height,
                    const Projection& proj, float out[3]);

// Continuous pixel coordinates of a camera-space point. False when the point is not in front of the camera.
bool ProjectToPixel(const float p[3], float width, float height, const Projection& proj,
                    float& px, float& py);

enum class SampleStatus : uint32_t { Valid = 0, Sky = 1, OffScreen = 2 };

// One self-check sample, laid out exactly like the GPU results buffer element.
struct SampleResult {
    float errToPrevious = 0.0f; // pixels, vector taken as previous - current
    float errToCurrent = 0.0f;  // pixels, vector taken as current - previous
    uint32_t status = 0;        // SampleStatus
    uint32_t sequence = 0;      // evaluation stamp the shader writes, to recognise stale readbacks
    // Convention-agnostic accounting (valid samples only). Pixels unless noted.
    float engineX = 0.0f;       // engine vector after the MV scale
    float engineY = 0.0f;
    float predictedX = 0.0f;    // predicted previous - current displacement
    float predictedY = 0.0f;
    float rawX = 0.0f;          // engine vector as stored, before the scale (raw units)
    float rawY = 0.0f;
    float rotationX = 0.0f;     // rotation-only displacement of the point as if at infinity (valid samples)
    float rotationY = 0.0f;
};
static_assert(sizeof(SampleResult) == 48, "SampleResult mirrors the GPU results element (3 x uint4)");

// Predicts the displacement of the static point seen at integer pixel (x, y) between the current camera
// and the previous rendered frame's camera, and scores the engine vector (mvX, mvY) times (scaleX,
// scaleY) against it under both sign conventions. current/previous are the two cameras' projections;
// toPrevious maps the current camera's space into the previous camera's.
SampleResult EvaluateSample(uint32_t x, uint32_t y, float depth, float mvX, float mvY,
                            float scaleX, float scaleY, uint32_t width, uint32_t height,
                            const Projection& current, const Projection& previous,
                            const RelativeTransform& toPrevious);

// Pixel coordinate of sample i of an n-sample row across extent pixels (centre of its cell).
uint32_t GridSampleCoordinate(uint32_t i, uint32_t n, uint32_t extent);

struct SelfCheckStats {
    uint32_t total = 0;
    uint32_t valid = 0;
    uint32_t skipped = 0; // sky and off-screen
    double skippedFraction = 0.0;
    double meanToPrevious = 0.0;
    double medianToPrevious = 0.0;
    double meanToCurrent = 0.0;
    double medianToCurrent = 0.0;
    // Convention-agnostic accounting over valid samples; p is the predicted previous-minus-current
    // displacement, e the engine vector after the scale, r the raw stored vector.
    double engineMean = 0.0;    // mean |e| (px)
    double predictedMean = 0.0; // mean |p| (px)
    double k = 0.0;             // sum(e.p) / sum(p.p): 1 = engine matches, 0 = ignores the offset, <0 = opposite
    double rawMean = 0.0;       // mean |r| (raw units)
    double kRaw = 0.0;          // sum(r.p) / sum(p.p): far from 1 with k near 1 means a units mismatch
    double rawMinX = 0.0, rawMaxX = 0.0, rawMinY = 0.0, rawMaxY = 0.0;
};

SelfCheckStats Summarize(const SampleResult* samples, size_t count);

// ---- Near plane estimate ----
//
// For static geometry the engine vector (pixels, after the scale) is e = R + T / near_true, where R is the
// rotation-only displacement and T the translation part of the prediction to the previous rendered frame's
// camera, computed with an assumed near n0 (T = predicted - R, scaling as 1/near under infinite reverse-Z).
// Least squares gives s = sum((e - R).T) / sum(T.T) and near_true = n0 / s.
constexpr uint32_t kMinNearSamples = 16;       // valid samples needed
constexpr double kMinNearTranslationSq = 0.01; // mean T.T (px^2) below which the ratio is noise

struct NearSolution {
    bool valid = false;
    float nearClip = 0.0f; // metres
    double scale = 0.0;    // s
    uint32_t used = 0;     // samples that took part after outlier rejection
    uint32_t total = 0;    // valid samples offered
};

// Solves for the renderer's near from a self-check recorded against the previous rendered frame's camera
// (not the same-eye one) on the engine's original vectors, with n0 = assumedNear. Rejects s <= 0, too few
// samples, a translation too small to measure, and outliers (moving objects) around a robust first fit.
NearSolution SolveNear(const SampleResult* samples, size_t count, float assumedNear);

enum class Convention { None, ToPrevious, ToCurrent };
// The convention with the smaller median error; None when no sample was valid.
Convention BetterConvention(const SelfCheckStats& stats);
const char* ConventionName(Convention c);


// ---- Per-eye correction (Sequential Dual-Pass) ----
//
// The engine's motion vector points at where the pixel was in the previous *rendered* frame, which under
// Sequential Dual-Pass is the other eye's frame. A per-eye upscaler history is one same-eye frame back,
// so the vector is re-aimed: unproject the pixel through the current camera, then add
//   project(same eye's previous camera, W) - project(previous rendered frame's camera, W)
// (pixels) to it. The camera part is replaced; the engine's object motion stays.

// Camera-space position of the pixel at continuous coordinates with a given view-space depth (metres).
void UnprojectAtViewDepth(float px, float py, float viewDepth, float width, float height, const Projection& proj,
                          float out[3]);

// Corrects one pixel's engine vector (in the units DLSS receives it: the stored value times the scale).
// outX/outY receive the corrected stored vector. Returns false and leaves the engine's vector untouched
// when no correction is possible (a scale of zero, or the point behind either camera). The sky corrects
// like a point on the far plane, which carries the camera rotation.
//
// objectMotionFactor multiplies the engine's object-motion component (its vector minus the static-scene
// prediction toward the previous rendered frame) and nothing else. Sequential Dual-Pass leaves it at 1:
// the second sub-pass replays the same simulation time. Alternate Frame Rendering advances the simulation
// every frame, so the engine's object motion spans one frame while the same eye's history is two frames
// back: it passes kAfrEyeStride. The camera component spans the right gap by construction.
bool CorrectMotionVector(uint32_t x, uint32_t y, float depth, float mvX, float mvY, float scaleX, float scaleY,
                         uint32_t width, uint32_t height, const Projection& current, const Projection& previousFrame,
                         const Projection& sameEye, const RelativeTransform& toPreviousFrame,
                         const RelativeTransform& toSameEye, float& outX, float& outY,
                         float objectMotionFactor = 1.0f);

// Frames between two evaluations of one eye's upscaler context under Alternate Frame Rendering.
constexpr float kAfrEyeStride = 2.0f;

enum class CorrectionDecision {
    Correct,                 // all three cameras known: re-aim the vector
    PreviousFrameIsSameEye,  // eye order swap: the engine's vector already targets the same eye
    MissingRecords,          // a camera record is absent (loading, menus): pass the engine's vector through
    UnusableProjection,      // a record with a zero FOV or clip range: pass through
};

struct CorrectionPlan {
    CorrectionDecision decision = CorrectionDecision::MissingRecords;
    Projection current;
    Projection previousFrame;
    Projection sameEye;
    RelativeTransform toPreviousFrame; // current camera space -> previous rendered frame's camera space
    RelativeTransform toSameEye;       // current camera space -> same eye's previous camera space
};

CorrectionPlan PlanCorrection(const ResolvedCameras& resolved);
const char* CorrectionDecisionName(CorrectionDecision d);
} // namespace fs25vr::mvreproj
