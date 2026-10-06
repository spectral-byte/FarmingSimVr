#pragma once

#include <d3d12.h>
#include <dxgiformat.h>
#include <functional>
#include "core/camera_records.h"
#include <memory>
#include <string>
#include <vector>
#include "mv_reprojection.h"

namespace fs25vr {

// The motion vector pass recorded on the game's command list just ahead of an upscaler evaluation.
// It reads the engine's depth and motion vectors and writes motion vectors into a texture the
// Injector owns. The GPU work sits behind this interface so the interceptor is testable with a fake.
// Asks the pass to predict the engine's motion vectors for static geometry from depth and the camera
// records and to score the engine's vectors against the prediction. The prediction is only measured:
// the pass output stays a copy of the engine's vectors. The pass fills in the render aspect itself.
struct MvSelfCheckRequest {
    uint32_t eye = 0;
    uint64_t evalIndex = 0;
    float mvScaleX = 1.0f; // the scale DLSS receives with the vectors
    float mvScaleY = 1.0f;
    mvreproj::Projection current;
    mvreproj::Projection previous;           // the previous rendered frame's camera
    mvreproj::RelativeTransform toPrevious;  // current camera space -> previous camera space
};

// A self-check read back some frames after it was recorded.
struct MvSelfCheckReport {
    uint32_t eye = 0;
    uint64_t evalIndex = 0;
    mvreproj::SelfCheckStats stats;
};

// Builds the self-check request for one evaluation from the raw camera records, or says why it cannot
// (missing record, unusable projection). The prediction targets the previous rendered frame's camera;
// when corrected is a Correct plan the evaluation's vectors are re-aimed at the same eye's previous
// camera, so it targets that camera instead. Built from the records, not the plan, so it also works on
// frames the plan declines to correct (an eye order swap).
bool BuildMvSelfCheckRequest(const ResolvedCameras& resolved, uint32_t eye, uint64_t evalIndex, float mvScaleX,
                             float mvScaleY, const mvreproj::CorrectionPlan* corrected, MvSelfCheckRequest& request,
                             std::string& skipReason);

// The [Diag] log lines shared by the upscaler interceptors. logPrefix names the interceptor (for example
// "NgxInterceptor"); unit names what it counts ("Eval" or "Dispatch").
std::string FormatMvSelfCheckReport(const char* logPrefix, const char* unit, const MvSelfCheckReport& report);
std::string FormatMvSelfCheckSkipped(const char* logPrefix, const char* unit, uint32_t eye, uint64_t index,
                                     const std::string& reason);

// Asks the pass to re-aim the engine's motion vectors from the previous rendered frame's camera at the
// same eye's previous camera (see mv_reprojection.h). Absent (null) means the pass output is a plain
// copy of the engine's vectors. The pass fills in the render aspect itself.
struct MvCorrectionRequest {
    float mvScaleX = 1.0f; // the scale DLSS receives with the vectors
    float mvScaleY = 1.0f;
    mvreproj::Projection current;
    mvreproj::Projection previousFrame;
    mvreproj::Projection sameEye;
    mvreproj::RelativeTransform toPreviousFrame; // current camera space -> previous rendered frame's
    mvreproj::RelativeTransform toSameEye;       // current camera space -> same eye's previous camera
    // Multiplies the engine's object-motion component only (1 under Sequential Dual-Pass, the eye stride
    // under Alternate Frame Rendering). See mvreproj::CorrectMotionVector.
    float objectMotionFactor = 1.0f;
};

struct MvPassInputs {
    ID3D12GraphicsCommandList* cmdList = nullptr;
    // The engine's resources, read in the state the upscaler already requires (never transitioned).
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motionVectors = nullptr;
    // Optional: also record a self-check for this evaluation.
    const MvSelfCheckRequest* selfCheck = nullptr;
    // Optional: correct the vectors. A self-check recorded alongside then scores the corrected output.
    const MvCorrectionRequest* correction = nullptr;
};

struct MvPassResult {
    bool ok = false;
    // The Injector's motion vector texture, in the state the upscaler requires. Valid until the
    // upscaler evaluation that follows has been recorded.
    ID3D12Resource* output = nullptr;
    // Why the pass cannot run. Set when ok is false; the interceptor logs it and disables the pass.
    std::string failureReason;
};

class IMotionVectorPass {
public:
    virtual ~IMotionVectorPass() = default;
    virtual MvPassResult Record(const MvPassInputs& inputs) = 0;
    // Self-checks whose results have been read back since the last call. The readback lags the recording
    // by a few evaluations (no fence on the game's queue).
    virtual std::vector<MvSelfCheckReport> PollSelfCheck() { return {}; }
};

// Creates the pass the first time an evaluation needs it. May return null, which disables the pass.
using MotionVectorPassFactory = std::function<std::unique_ptr<IMotionVectorPass>(ID3D12GraphicsCommandList*)>;

// Motion vector and depth formats the compute pass supports (it is a copy: output format = input format).
// Return the DXGI format a shader view reads them through, or DXGI_FORMAT_UNKNOWN when unsupported.
DXGI_FORMAT MvPassMotionVectorViewFormat(DXGI_FORMAT resourceFormat);
DXGI_FORMAT MvPassDepthViewFormat(DXGI_FORMAT resourceFormat);

// HLSL source of the pass-through compute shader.
const char* MvPassShaderSource();
// Compile the shader with the system's d3dcompiler_47.dll. Returns true and fills bytecode on success;
// otherwise fills error with the reason.
bool MvPassCompileShader(std::string& bytecode, std::string& error, const char* entryPoint = "main");

// The real D3D12 implementation. Resources are created lazily from the command list's device.
std::unique_ptr<IMotionVectorPass> CreateD3D12MotionVectorPass();

} // namespace fs25vr
