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

struct MvSelfCheckRequest {
    uint32_t eye = 0;
    uint64_t evalIndex = 0;
    float mvScaleX = 1.0f; // the scale DLSS receives with the vectors
    float mvScaleY = 1.0f;
    mvreproj::Projection current;
    mvreproj::Projection previous;           // the previous rendered frame's camera
    mvreproj::RelativeTransform toPrevious;  // current camera space -> previous camera space
    bool nearEstimate = false;
};

struct MvSelfCheckReport {
    uint32_t eye = 0;
    uint64_t evalIndex = 0;
    mvreproj::SelfCheckStats stats;
    uint32_t mvFormat = 0;
    bool nearEstimate = false;
    mvreproj::NearSolution nearSolution; // valid only when nearEstimate
};

bool BuildMvSelfCheckRequest(const ResolvedCameras& resolved, uint32_t eye, uint64_t evalIndex, float mvScaleX,
                             float mvScaleY, const mvreproj::CorrectionPlan* corrected, MvSelfCheckRequest& request,
                             std::string& skipReason);

std::string FormatMvSelfCheckReport(const char* logPrefix, const char* unit, const MvSelfCheckReport& report);
std::string FormatMvRawRangeReport(const char* logPrefix, const char* unit, const MvSelfCheckReport& report);
std::string FormatMvSelfCheckSkipped(const char* logPrefix, const char* unit, uint32_t eye, uint64_t index,
                                     const std::string& reason);

struct MvProjectionSide {
    float nearClip = 0.0f;
    float farClip = 0.0f;
    float fovY = 0.0f;
};

struct MvProjectionDiag {
    const char* source = "camera-record"; // where `used` came from: "camera-record", "dispatch-desc", "override" or "auto"
    MvProjectionSide used;
    float projOffsetX = 0.0f; // the record's; the correction ignores it
    float projOffsetY = 0.0f;
    float aspect = 1.0f;      // render width / render height
    bool infinite = false;
    bool staticGate = false;  // the correction passes non-static-consistent pixels through (NGX only)
    const char* depthSource = ""; // what chose the depth model ("vrDlssDepthModel", "create-flags")
    float autoEstimate = 0.0f; // the automatic near estimate, shown when source is "auto"
    bool hasRecordNear = false; // set to get a hint when source is "override" and the record's near differs
    float recordNear = 0.0f;
    bool hasComparison = false;
    MvProjectionSide dispatchRaw;
    float viewSpaceToMeters = 0.0f;
    MvProjectionSide record;
};

std::string FormatMvProjectionLine(const char* logPrefix, const char* unit, uint32_t eye, uint64_t index,
                                   const MvProjectionDiag& d);

struct MvCorrectionRequest {
    float mvScaleX = 1.0f; // the scale DLSS receives with the vectors
    float mvScaleY = 1.0f;
    mvreproj::Projection current;
    mvreproj::Projection previousFrame;
    mvreproj::Projection sameEye;
    mvreproj::RelativeTransform toPreviousFrame; // current camera space -> previous rendered frame's
    mvreproj::RelativeTransform toSameEye;
    float objectMotionFactor = 1.0f;
    bool gateNonStatic = false;
};

struct MvPassInputs {
    ID3D12GraphicsCommandList* cmdList = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motionVectors = nullptr;
    const MvSelfCheckRequest* selfCheck = nullptr;
    const MvCorrectionRequest* correction = nullptr;
};

struct MvPassResult {
    bool ok = false;
    ID3D12Resource* output = nullptr;
    std::string failureReason;
};

class IMotionVectorPass {
public:
    virtual ~IMotionVectorPass() = default;
    virtual MvPassResult Record(const MvPassInputs& inputs) = 0;
    virtual std::vector<MvSelfCheckReport> PollSelfCheck() { return {}; }
};

using MotionVectorPassFactory = std::function<std::unique_ptr<IMotionVectorPass>(ID3D12GraphicsCommandList*)>;

DXGI_FORMAT MvPassMotionVectorViewFormat(DXGI_FORMAT resourceFormat);
DXGI_FORMAT MvPassDepthViewFormat(DXGI_FORMAT resourceFormat);

const char* MvPassShaderSource();
bool MvPassCompileShader(std::string& bytecode, std::string& error, const char* entryPoint = "main");

std::unique_ptr<IMotionVectorPass> CreateD3D12MotionVectorPass();

}
