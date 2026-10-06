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
};

// Maps a point from the current camera's space into another camera's space: p_other = m * p + c.
struct RelativeTransform {
    float m[3][3] = {};
    float c[3] = {};
};

// Builds the transform from the double-precision relative camera (translation of the other camera in
// the current camera's axes, rotation = conj(current) * other).
RelativeTransform MakeRelativeTransform(const RelativeCamera& relative);
void ApplyRelativeTransform(const RelativeTransform& t, const float p[3], float out[3]);

// View-space distance in front of the camera for a reverse-Z depth value.
float ReverseZToViewDepth(float depth, float nearClip, float farClip);

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
};

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
};

SelfCheckStats Summarize(const SampleResult* samples, size_t count);

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
