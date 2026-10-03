#pragma once

#include "ngx_defs.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs25vr {

// Magic identifier to safely identify virtual NGX handles: 'VNGX'
inline constexpr uint32_t VIRTUAL_NGX_HANDLE_MAGIC = 0x564E4758;

enum class NgxParamType {
    Ull,
    Float,
    Double,
    Uint,
    Int,
    D3D11Resource,
    D3D12Resource,
    VoidPtr
};

struct NgxParamValue {
    NgxParamType type = NgxParamType::Int;
    union {
        unsigned long long ullVal;
        float floatVal;
        double doubleVal;
        unsigned int uintVal;
        int intVal;
        ID3D11Resource* d3d11Val;
        ID3D12Resource* d3d12Val;
        void* voidPtrVal;
    };

    NgxParamValue() : type(NgxParamType::Int), intVal(0) {}
};

class NgxParameterImpl : public NVSDK_NGX_Parameter {
public:
    NgxParameterImpl() = default;
    virtual ~NgxParameterImpl() = default;

    void Set(const char* InName, unsigned long long InValue) override {
        if (!InName) return;
        NgxParamValue val;
        val.type = NgxParamType::Ull;
        val.ullVal = InValue;
        m_params[InName] = val;
    }

    void Set(const char* InName, float InValue) override {
        if (!InName) return;
        NgxParamValue val;
        val.type = NgxParamType::Float;
        val.floatVal = InValue;
        m_params[InName] = val;
    }

    void Set(const char* InName, double InValue) override {
        if (!InName) return;
        NgxParamValue val;
        val.type = NgxParamType::Double;
        val.doubleVal = InValue;
        m_params[InName] = val;
    }

    void Set(const char* InName, unsigned int InValue) override {
        if (!InName) return;
        NgxParamValue val;
        val.type = NgxParamType::Uint;
        val.uintVal = InValue;
        m_params[InName] = val;
    }

    void Set(const char* InName, int InValue) override {
        if (!InName) return;
        NgxParamValue val;
        val.type = NgxParamType::Int;
        val.intVal = InValue;
        m_params[InName] = val;
    }

    void Set(const char* InName, ID3D11Resource* InValue) override {
        if (!InName) return;
        NgxParamValue val;
        val.type = NgxParamType::D3D11Resource;
        val.d3d11Val = InValue;
        m_params[InName] = val;
    }

    void Set(const char* InName, ID3D12Resource* InValue) override {
        if (!InName) return;
        NgxParamValue val;
        val.type = NgxParamType::D3D12Resource;
        val.d3d12Val = InValue;
        m_params[InName] = val;
    }

    void Set(const char* InName, void* InValue) override {
        if (!InName) return;
        NgxParamValue val;
        val.type = NgxParamType::VoidPtr;
        val.voidPtrVal = InValue;
        m_params[InName] = val;
    }

    NVSDK_NGX_Result Get(const char* InName, unsigned long long* OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        auto it = m_params.find(InName);
        if (it == m_params.end()) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        const auto& val = it->second;
        switch (val.type) {
            case NgxParamType::Ull: *OutValue = val.ullVal; return NVSDK_NGX_Result_Success;
            case NgxParamType::Uint: *OutValue = static_cast<unsigned long long>(val.uintVal); return NVSDK_NGX_Result_Success;
            case NgxParamType::Int: *OutValue = static_cast<unsigned long long>(val.intVal); return NVSDK_NGX_Result_Success;
            default: return NVSDK_NGX_Result_FAIL_InvalidParameter;
        }
    }

    NVSDK_NGX_Result Get(const char* InName, float* OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        auto it = m_params.find(InName);
        if (it == m_params.end()) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        const auto& val = it->second;
        switch (val.type) {
            case NgxParamType::Float: *OutValue = val.floatVal; return NVSDK_NGX_Result_Success;
            case NgxParamType::Double: *OutValue = static_cast<float>(val.doubleVal); return NVSDK_NGX_Result_Success;
            default: return NVSDK_NGX_Result_FAIL_InvalidParameter;
        }
    }

    NVSDK_NGX_Result Get(const char* InName, double* OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        auto it = m_params.find(InName);
        if (it == m_params.end()) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        const auto& val = it->second;
        switch (val.type) {
            case NgxParamType::Double: *OutValue = val.doubleVal; return NVSDK_NGX_Result_Success;
            case NgxParamType::Float: *OutValue = static_cast<double>(val.floatVal); return NVSDK_NGX_Result_Success;
            default: return NVSDK_NGX_Result_FAIL_InvalidParameter;
        }
    }

    NVSDK_NGX_Result Get(const char* InName, unsigned int* OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        auto it = m_params.find(InName);
        if (it == m_params.end()) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        const auto& val = it->second;
        switch (val.type) {
            case NgxParamType::Uint: *OutValue = val.uintVal; return NVSDK_NGX_Result_Success;
            case NgxParamType::Int: *OutValue = static_cast<unsigned int>(val.intVal); return NVSDK_NGX_Result_Success;
            case NgxParamType::Ull: *OutValue = static_cast<unsigned int>(val.ullVal); return NVSDK_NGX_Result_Success;
            default: return NVSDK_NGX_Result_FAIL_InvalidParameter;
        }
    }

    NVSDK_NGX_Result Get(const char* InName, int* OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        auto it = m_params.find(InName);
        if (it == m_params.end()) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        const auto& val = it->second;
        switch (val.type) {
            case NgxParamType::Int: *OutValue = val.intVal; return NVSDK_NGX_Result_Success;
            case NgxParamType::Uint: *OutValue = static_cast<int>(val.uintVal); return NVSDK_NGX_Result_Success;
            case NgxParamType::Ull: *OutValue = static_cast<int>(val.ullVal); return NVSDK_NGX_Result_Success;
            default: return NVSDK_NGX_Result_FAIL_InvalidParameter;
        }
    }

    NVSDK_NGX_Result Get(const char* InName, ID3D11Resource** OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        auto it = m_params.find(InName);
        if (it == m_params.end()) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        const auto& val = it->second;
        switch (val.type) {
            case NgxParamType::D3D11Resource: *OutValue = val.d3d11Val; return NVSDK_NGX_Result_Success;
            case NgxParamType::VoidPtr: *OutValue = static_cast<ID3D11Resource*>(val.voidPtrVal); return NVSDK_NGX_Result_Success;
            default: return NVSDK_NGX_Result_FAIL_InvalidParameter;
        }
    }

    NVSDK_NGX_Result Get(const char* InName, ID3D12Resource** OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        auto it = m_params.find(InName);
        if (it == m_params.end()) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        const auto& val = it->second;
        switch (val.type) {
            case NgxParamType::D3D12Resource: *OutValue = val.d3d12Val; return NVSDK_NGX_Result_Success;
            case NgxParamType::VoidPtr: *OutValue = static_cast<ID3D12Resource*>(val.voidPtrVal); return NVSDK_NGX_Result_Success;
            default: return NVSDK_NGX_Result_FAIL_InvalidParameter;
        }
    }

    NVSDK_NGX_Result Get(const char* InName, void** OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        auto it = m_params.find(InName);
        if (it == m_params.end()) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        const auto& val = it->second;
        switch (val.type) {
            case NgxParamType::VoidPtr: *OutValue = val.voidPtrVal; return NVSDK_NGX_Result_Success;
            case NgxParamType::D3D12Resource: *OutValue = static_cast<void*>(val.d3d12Val); return NVSDK_NGX_Result_Success;
            case NgxParamType::D3D11Resource: *OutValue = static_cast<void*>(val.d3d11Val); return NVSDK_NGX_Result_Success;
            default: return NVSDK_NGX_Result_FAIL_InvalidParameter;
        }
    }

    void Reset() override {
        m_params.clear();
    }

    std::shared_ptr<NgxParameterImpl> Clone() const {
        auto clone = std::make_shared<NgxParameterImpl>();
        clone->m_params = m_params;
        return clone;
    }

    void CopyFrom(const NVSDK_NGX_Parameter* source) {
        if (!source) return;

        const auto* impl = dynamic_cast<const NgxParameterImpl*>(source);
        if (impl) {
            m_params = impl->m_params;
            return;
        }

        static const char* const s_knownNames[] = {
            NVSDK_NGX_Parameter_Width,
            NVSDK_NGX_Parameter_Height,
            NVSDK_NGX_Parameter_OutWidth,
            NVSDK_NGX_Parameter_OutHeight,
            NVSDK_NGX_Parameter_PerfQualityValue,
            NVSDK_NGX_Parameter_RTXValue,
            NVSDK_NGX_Parameter_Reset,
            NVSDK_NGX_Parameter_CreationNodeMask,
            NVSDK_NGX_Parameter_VisibilityNodeMask,
            NVSDK_NGX_Parameter_Color,
            NVSDK_NGX_Parameter_Output,
            NVSDK_NGX_Parameter_Depth,
            NVSDK_NGX_Parameter_MotionVectors,
            NVSDK_NGX_Parameter_Jitter_Offset_X,
            NVSDK_NGX_Parameter_Jitter_Offset_Y,
            NVSDK_NGX_Parameter_MV_Scale_X,
            NVSDK_NGX_Parameter_MV_Scale_Y,
            NVSDK_NGX_Parameter_MV_Offset_X,
            NVSDK_NGX_Parameter_MV_Offset_Y,
            NVSDK_NGX_Parameter_Sharpness,
            NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
            NVSDK_NGX_Parameter_InWidth,
            NVSDK_NGX_Parameter_InHeight,
            NVSDK_NGX_Parameter_DLSS_Optimal_Render_Width,
            NVSDK_NGX_Parameter_DLSS_Optimal_Render_Height,
            NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,
            NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,
            NVSDK_NGX_Parameter_ExposureTexture,
            NVSDK_NGX_Parameter_PreExposure,
            NVSDK_NGX_Parameter_DLSS_Exposure_Texture,
            NVSDK_NGX_Parameter_DLSS_Pre_Exposure,
            NVSDK_NGX_Parameter_DLSS_Exposure,
            NVSDK_NGX_Parameter_TonemapperType,
            NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality,
            NVSDK_NGX_Parameter_Resource_Width,
            NVSDK_NGX_Parameter_Resource_Height,
            NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects,
            NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X,
            NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y,
            NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X,
            NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y
        };

        for (const char* name : s_knownNames) {
            unsigned int uVal = 0;
            if (NVSDK_NGX_SUCCEED(source->Get(name, &uVal))) {
                Set(name, uVal);
                continue;
            }
            int iVal = 0;
            if (NVSDK_NGX_SUCCEED(source->Get(name, &iVal))) {
                Set(name, iVal);
                continue;
            }
            float fVal = 0.0f;
            if (NVSDK_NGX_SUCCEED(source->Get(name, &fVal))) {
                Set(name, fVal);
                continue;
            }
            double dVal = 0.0;
            if (NVSDK_NGX_SUCCEED(source->Get(name, &dVal))) {
                Set(name, dVal);
                continue;
            }
            unsigned long long ullVal = 0;
            if (NVSDK_NGX_SUCCEED(source->Get(name, &ullVal))) {
                Set(name, ullVal);
                continue;
            }
            ID3D12Resource* res12 = nullptr;
            if (NVSDK_NGX_SUCCEED(source->Get(name, &res12))) {
                Set(name, res12);
                continue;
            }
            ID3D11Resource* res11 = nullptr;
            if (NVSDK_NGX_SUCCEED(source->Get(name, &res11))) {
                Set(name, res11);
                continue;
            }
            void* pVal = nullptr;
            if (NVSDK_NGX_SUCCEED(source->Get(name, &pVal))) {
                Set(name, pVal);
                continue;
            }
        }
    }

    void MirrorTo(NVSDK_NGX_Parameter* destination) const {
        if (!destination) return;
        for (const auto& [name, val] : m_params) {
            switch (val.type) {
                case NgxParamType::Ull:
                    destination->Set(name.c_str(), val.ullVal);
                    break;
                case NgxParamType::Float:
                    destination->Set(name.c_str(), val.floatVal);
                    break;
                case NgxParamType::Double:
                    destination->Set(name.c_str(), val.doubleVal);
                    break;
                case NgxParamType::Uint:
                    destination->Set(name.c_str(), val.uintVal);
                    break;
                case NgxParamType::Int:
                    destination->Set(name.c_str(), val.intVal);
                    break;
                case NgxParamType::D3D11Resource:
                    destination->Set(name.c_str(), val.d3d11Val);
                    break;
                case NgxParamType::D3D12Resource:
                    destination->Set(name.c_str(), val.d3d12Val);
                    break;
                case NgxParamType::VoidPtr:
                    destination->Set(name.c_str(), val.voidPtrVal);
                    break;
            }
        }
    }

    size_t GetParamCount() const { return m_params.size(); }
    bool HasParam(const char* name) const {
        if (!name) return false;
        return m_params.find(name) != m_params.end();
    }

private:
    std::unordered_map<std::string, NgxParamValue> m_params;
};

// Opaque Virtual NGX Handle wrapping left and right runtime feature handles
struct VirtualNgxHandle : public NVSDK_NGX_Handle {
    uint32_t magic = VIRTUAL_NGX_HANDLE_MAGIC;
    NVSDK_NGX_Handle* hLeft = nullptr;
    NVSDK_NGX_Handle* hRight = nullptr;
    NVSDK_NGX_Feature featureId = NVSDK_NGX_Feature_Reserved;
    std::shared_ptr<NgxParameterImpl> clonedParameters;

    // Headset-native target and internal render dimensions
    uint32_t targetWidth = 0;
    uint32_t targetHeight = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    NVSDK_NGX_PerfQuality_Value perfQuality = NVSDK_NGX_PerfQuality_Value_MaxQuality;

    bool IsValid() const {
        return magic == VIRTUAL_NGX_HANDLE_MAGIC && hLeft != nullptr && hRight != nullptr;
    }
};

} // namespace fs25vr
