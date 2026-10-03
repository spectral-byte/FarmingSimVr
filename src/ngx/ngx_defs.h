#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <d3d12.h>

namespace fs25vr {

// NGX Result Codes
using NVSDK_NGX_Result = int32_t;
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_Success = 1;
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_Fail = static_cast<int32_t>(0x80004005);
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_FeatureNotFound = static_cast<int32_t>(0x80040001);
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_InvalidParameter = static_cast<int32_t>(0x80040002);
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_FAIL_FeatureAlreadyExists = static_cast<int32_t>(0x80040003);

#ifndef NVSDK_NGX_SUCCEED
#define NVSDK_NGX_SUCCEED(res) (((res) & 0x80000000) == 0)
#endif

#ifndef NVSDK_NGX_FAILED
#define NVSDK_NGX_FAILED(res) (!NVSDK_NGX_SUCCEED(res))
#endif

// NGX Feature Enums
enum NVSDK_NGX_Feature {
    NVSDK_NGX_Feature_Reserved = 0,
    NVSDK_NGX_Feature_SuperSampling = 1,
    NVSDK_NGX_Feature_InPainting = 2,
    NVSDK_NGX_Feature_ImageSuperResolution = 3,
    NVSDK_NGX_Feature_SlowMotion = 4,
    NVSDK_NGX_Feature_VideoSuperResolution = 5,
    NVSDK_NGX_Feature_ImageSignalProcessing = 6,
    NVSDK_NGX_Feature_DeepDVC = 7,
    NVSDK_NGX_Feature_Count = 8
};

// NGX Perf Quality Enums
enum NVSDK_NGX_PerfQuality_Value {
    NVSDK_NGX_PerfQuality_Value_MaxPerf = 0,
    NVSDK_NGX_PerfQuality_Value_Balanced = 1,
    NVSDK_NGX_PerfQuality_Value_MaxQuality = 2,
    NVSDK_NGX_PerfQuality_Value_UltraPerformance = 3,
    NVSDK_NGX_PerfQuality_Value_UltraQuality = 4,
    NVSDK_NGX_PerfQuality_Value_DLAA = 5
};

// NGX DLSS Render Preset Hint Enums
enum NVSDK_NGX_DLSS_Hint_Render_Preset {
    NVSDK_NGX_DLSS_Hint_Render_Preset_Default = 0,
    NVSDK_NGX_DLSS_Hint_Render_Preset_A = 1,
    NVSDK_NGX_DLSS_Hint_Render_Preset_B = 2,
    NVSDK_NGX_DLSS_Hint_Render_Preset_C = 3,
    NVSDK_NGX_DLSS_Hint_Render_Preset_D = 4,
    NVSDK_NGX_DLSS_Hint_Render_Preset_E = 5,
    NVSDK_NGX_DLSS_Hint_Render_Preset_F = 6,
    NVSDK_NGX_DLSS_Hint_Render_Preset_G = 7,
    NVSDK_NGX_DLSS_Hint_Render_Preset_J = 10,
    NVSDK_NGX_DLSS_Hint_Render_Preset_K = 11,
    NVSDK_NGX_DLSS_Hint_Render_Preset_L = 12
};

// NGX Handle
struct NVSDK_NGX_Handle {
    unsigned int Id = 0;
};

// Forward declaration of parameters interface
struct NVSDK_NGX_Parameter {
    virtual void Set(const char* InName, unsigned long long InValue) = 0;
    virtual void Set(const char* InName, float InValue) = 0;
    virtual void Set(const char* InName, double InValue) = 0;
    virtual void Set(const char* InName, unsigned int InValue) = 0;
    virtual void Set(const char* InName, int InValue) = 0;
    virtual void Set(const char* InName, struct ID3D11Resource* InValue) = 0;
    virtual void Set(const char* InName, struct ID3D12Resource* InValue) = 0;
    virtual void Set(const char* InName, void* InValue) = 0;

    virtual NVSDK_NGX_Result Get(const char* InName, unsigned long long* OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char* InName, float* OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char* InName, double* OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char* InName, unsigned int* OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char* InName, int* OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char* InName, struct ID3D11Resource** OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char* InName, struct ID3D12Resource** OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char* InName, void** OutValue) const = 0;

    virtual void Reset() = 0;
};

// Common NGX Parameter Names
inline constexpr const char* NVSDK_NGX_Parameter_Width = "Width";
inline constexpr const char* NVSDK_NGX_Parameter_Height = "Height";
inline constexpr const char* NVSDK_NGX_Parameter_OutWidth = "OutWidth";
inline constexpr const char* NVSDK_NGX_Parameter_OutHeight = "OutHeight";
inline constexpr const char* NVSDK_NGX_Parameter_PerfQualityValue = "PerfQualityValue";
inline constexpr const char* NVSDK_NGX_Parameter_RTXValue = "RTXValue";
inline constexpr const char* NVSDK_NGX_Parameter_Reset = "Reset";
inline constexpr const char* NVSDK_NGX_Parameter_CreationNodeMask = "CreationNodeMask";
inline constexpr const char* NVSDK_NGX_Parameter_VisibilityNodeMask = "VisibilityNodeMask";
inline constexpr const char* NVSDK_NGX_Parameter_Color = "Color";
inline constexpr const char* NVSDK_NGX_Parameter_Output = "Output";
inline constexpr const char* NVSDK_NGX_Parameter_Depth = "Depth";
inline constexpr const char* NVSDK_NGX_Parameter_MotionVectors = "MotionVectors";
inline constexpr const char* NVSDK_NGX_Parameter_Jitter_Offset_X = "Jitter.Offset.X";
inline constexpr const char* NVSDK_NGX_Parameter_Jitter_Offset_Y = "Jitter.Offset.Y";
inline constexpr const char* NVSDK_NGX_Parameter_MV_Scale_X = "MV.Scale.X";
inline constexpr const char* NVSDK_NGX_Parameter_MV_Scale_Y = "MV.Scale.Y";
inline constexpr const char* NVSDK_NGX_Parameter_MV_Offset_X = "MV.Offset.X";
inline constexpr const char* NVSDK_NGX_Parameter_MV_Offset_Y = "MV.Offset.Y";
inline constexpr const char* NVSDK_NGX_Parameter_Sharpness = "Sharpness";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags = "DLSS.Feature.Create.Flags";
inline constexpr const char* NVSDK_NGX_Parameter_InWidth = "InWidth";
inline constexpr const char* NVSDK_NGX_Parameter_InHeight = "InHeight";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Optimal_Render_Width = "DLSS.Optimal.Render.Width";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Optimal_Render_Height = "DLSS.Optimal.Render.Height";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width = "DLSS.Render.Subrect.Dimensions.Width";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height = "DLSS.Render.Subrect.Dimensions.Height";
inline constexpr const char* NVSDK_NGX_Parameter_ExposureTexture = "ExposureTexture";
inline constexpr const char* NVSDK_NGX_Parameter_PreExposure = "PreExposure";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Exposure_Texture = "DLSS.Exposure.Texture";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Pre_Exposure = "DLSS.Pre.Exposure";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Exposure = "Exposure";
inline constexpr const char* NVSDK_NGX_Parameter_TonemapperType = "TonemapperType";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask = "DLSS.Input.Bias.Current.Color.Mask";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset = "DLSS.Hint.Render.Preset";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA = "DLSS.Hint.Render.Preset.DLAA";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality = "DLSS.Hint.Render.Preset.Quality";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced = "DLSS.Hint.Render.Preset.Balanced";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance = "DLSS.Hint.Render.Preset.Performance";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance = "DLSS.Hint.Render.Preset.UltraPerformance";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality = "DLSS.Hint.Render.Preset.UltraQuality";
inline constexpr const char* NVSDK_NGX_Parameter_Resource_Width = "ResourceWidth";
inline constexpr const char* NVSDK_NGX_Parameter_Resource_Height = "ResourceHeight";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects = "DLSS.Enable.Output.Subrects";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X = "DLSS.Input.Depth.Subrect.Base.X";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y = "DLSS.Input.Depth.Subrect.Base.Y";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X = "DLSS.Output.Subrect.Base.X";
inline constexpr const char* NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y = "DLSS.Output.Subrect.Base.Y";

struct DlssRenderResolution {
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t targetWidth = 0;
    uint32_t targetHeight = 0;
    float scaleFactor = 1.0f;
    NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
};

inline DlssRenderResolution DeriveDlssRenderResolution(
    uint32_t targetWidth,
    uint32_t targetHeight,
    NVSDK_NGX_PerfQuality_Value quality
) {
    DlssRenderResolution res;
    res.targetWidth = targetWidth;
    res.targetHeight = targetHeight;
    res.quality = quality;

    switch (quality) {
        case NVSDK_NGX_PerfQuality_Value_MaxPerf:
            res.scaleFactor = 0.50f;
            break;
        case NVSDK_NGX_PerfQuality_Value_Balanced:
            res.scaleFactor = 1.0f / 1.7f;
            break;
        case NVSDK_NGX_PerfQuality_Value_MaxQuality:
            res.scaleFactor = 2.0f / 3.0f;
            break;
        case NVSDK_NGX_PerfQuality_Value_UltraPerformance:
            res.scaleFactor = 1.0f / 3.0f;
            break;
        case NVSDK_NGX_PerfQuality_Value_UltraQuality:
            res.scaleFactor = 1.0f / 1.3f;
            break;
        case NVSDK_NGX_PerfQuality_Value_DLAA:
        default:
            res.scaleFactor = 1.0f;
            break;
    }

    if (res.scaleFactor >= 0.999f && res.scaleFactor <= 1.001f) {
        res.renderWidth = targetWidth;
        res.renderHeight = targetHeight;
    } else {
        res.renderWidth = static_cast<uint32_t>(std::round(static_cast<double>(targetWidth) * res.scaleFactor));
        res.renderHeight = static_cast<uint32_t>(std::round(static_cast<double>(targetHeight) * res.scaleFactor));
    }

    return res;
}

// Progress Callback
using PFN_NVSDK_NGX_ProgressCallback = void (*)(float InProgress, bool* OutShouldCancel);

// NGX Function Signatures
using PFN_NVSDK_NGX_D3D12_CreateFeature = NVSDK_NGX_Result (*)(
    ID3D12GraphicsCommandList* InCmdList,
    NVSDK_NGX_Feature InFeatureID,
    NVSDK_NGX_Parameter* InParameters,
    NVSDK_NGX_Handle** OutHandle
);

using PFN_NVSDK_NGX_D3D12_ReleaseFeature = NVSDK_NGX_Result (*)(
    NVSDK_NGX_Handle* InHandle
);

using PFN_NVSDK_NGX_D3D12_EvaluateFeature = NVSDK_NGX_Result (*)(
    ID3D12GraphicsCommandList* InCmdList,
    const NVSDK_NGX_Handle* InFeatureHandle,
    const NVSDK_NGX_Parameter* InParameters,
    PFN_NVSDK_NGX_ProgressCallback InCallback
);

using PFN_NVSDK_NGX_D3D12_GetCapabilityParameters = NVSDK_NGX_Result (*)(
    NVSDK_NGX_Parameter** OutParameters
);

using PFN_NVSDK_NGX_D3D12_AllocateParameters = NVSDK_NGX_Result (*)(
    NVSDK_NGX_Parameter** OutParameters
);

using PFN_NVSDK_NGX_D3D12_DestroyParameters = NVSDK_NGX_Result (*)(
    NVSDK_NGX_Parameter* InParameters
);

} // namespace fs25vr
