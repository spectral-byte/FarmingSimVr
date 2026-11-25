#pragma once

#include "core/camera_records.h"
#include <cstddef>
#include <cstdint>

namespace fs25vr::mvreproj {

struct Projection {
    float tanHalfFovY = 1.0f;
    float aspect = 1.0f; // render width / render height
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    bool infiniteDepth = false;
};

constexpr float kInfiniteSkyViewDepth = 100000.0f;

// Maps a point from the current camera's space into another camera's space: p_other = m * p + c.
struct RelativeTransform {
    float m[3][3] = {};
    float c[3] = {};
};

RelativeTransform MakeRelativeTransform(const RelativeCamera& relative);
void ApplyRelativeTransform(const RelativeTransform& t, const float p[3], float out[3]);

float ReverseZToViewDepth(float depth, float nearClip, float farClip, bool infiniteDepth = false);

// Camera-space position of the pixel at continuous coordinates (px, py) with the given depth.
// False for the sky (depth at or beyond the far plane).
bool UnprojectPixel(float px, float py, float depth, float width, float height,
                    const Projection& proj, float out[3]);

bool ProjectToPixel(const float p[3], float width, float height, const Projection& proj,
                    float& px, float& py);

enum class SampleStatus : uint32_t { Valid = 0, Sky = 1, OffScreen = 2 };

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
    float viewDepth = 0.0f;     // metres in front of the camera at the near plane the sample was made with (0 = unknown)
    float reserved[3] = {};
};
static_assert(sizeof(SampleResult) == 64, "SampleResult mirrors the GPU results element (4 x uint4)");

SampleResult EvaluateSample(uint32_t x, uint32_t y, float depth, float mvX, float mvY,
                            float scaleX, float scaleY, uint32_t width, uint32_t height,
                            const Projection& current, const Projection& previous,
                            const RelativeTransform& toPrevious);

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
    // Fraction of valid samples whose engine vector is not static-consistent with the prediction
    // (IsStaticConsistent): what the correction's gate passes through unchanged. Camera-attached and
    // moving pixels, or everything when the near plane is wrong.
    double gatedFraction = 0.0;
};

SelfCheckStats Summarize(const SampleResult* samples, size_t count);

constexpr float kStaticGateAbsPx = 2.0f;
constexpr float kStaticGateRelative = 0.2f;
bool IsStaticConsistent(float ex, float ey, float dx, float dy);

constexpr uint32_t kMinNearSamples = 16;       // samples needed, both informative and inlier
constexpr double kMinNearTranslationSq = 0.01; // mean T.T (px^2) below which the ratio is noise
constexpr double kMinInformativeTranslationSq = 1.0;
constexpr float kNearFieldViewDepth = 1.5f;
constexpr double kMinNearInlierFraction = 0.5;    // inliers / retained informative samples
constexpr double kMinNearRetainedFraction = 0.25; // retained / informative: the near-field cut may not eat the scene
constexpr size_t kMaxNearCandidates = 64;         // candidate scales tried (quantiles of the per-sample ratios)

struct NearSolution {
    bool valid = false;
    float nearClip = 0.0f; // metres
    double scale = 0.0;    // s
    uint32_t used = 0;     // inliers: static-consistent samples that took part in the fit
    uint32_t total = 0;    // valid samples offered
    uint32_t informative = 0;    // of those, with a translation worth fitting
    uint32_t retained = 0;       // informative samples outside the near field at the fitted scale
    double inlierFraction = 0.0; // used / retained (also filled in for a refused solution, for the log)
};

NearSolution SolveNear(const SampleResult* samples, size_t count, float assumedNear);

enum class Convention { None, ToPrevious, ToCurrent };
Convention BetterConvention(const SelfCheckStats& stats);
const char* ConventionName(Convention c);


void UnprojectAtViewDepth(float px, float py, float viewDepth, float width, float height, const Projection& proj,
                          float out[3]);

bool CorrectMotionVector(uint32_t x, uint32_t y, float depth, float mvX, float mvY, float scaleX, float scaleY,
                         uint32_t width, uint32_t height, const Projection& current, const Projection& previousFrame,
                         const Projection& sameEye, const RelativeTransform& toPreviousFrame,
                         const RelativeTransform& toSameEye, float& outX, float& outY,
                         float objectMotionFactor = 1.0f, bool gateNonStatic = false);

// Frames between two evaluations of one eye's upscaler context under Alternate Frame Rendering.
constexpr float kAfrEyeStride = 2.0f;

enum class CorrectionDecision {
    Correct,                 // all three cameras known: re-aim the vector
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
}
