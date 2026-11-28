#pragma once

#include "core/in_flight_gate.h"
#include "ngx_defs.h"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs25vr {

inline constexpr uint32_t VIRTUAL_NGX_HANDLE_MAGIC = 0x564E4758;

struct TransparentStringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

struct TransparentStringEqual {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a == b;
    }
};

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
    NgxParameterImpl();
    virtual ~NgxParameterImpl();

    NgxParameterImpl(const NgxParameterImpl&) = delete;
    NgxParameterImpl& operator=(const NgxParameterImpl&) = delete;
    NgxParameterImpl(NgxParameterImpl&&) = delete;
    NgxParameterImpl& operator=(NgxParameterImpl&&) = delete;

    void Set(const char* InName, unsigned long long InValue) override {
        NgxParamValue val;
        val.type = NgxParamType::Ull;
        val.ullVal = InValue;
        SetValue(InName, val);
    }

    void Set(const char* InName, float InValue) override {
        NgxParamValue val;
        val.type = NgxParamType::Float;
        val.floatVal = InValue;
        SetValue(InName, val);
    }

    void Set(const char* InName, double InValue) override {
        NgxParamValue val;
        val.type = NgxParamType::Double;
        val.doubleVal = InValue;
        SetValue(InName, val);
    }

    void Set(const char* InName, unsigned int InValue) override {
        NgxParamValue val;
        val.type = NgxParamType::Uint;
        val.uintVal = InValue;
        SetValue(InName, val);
    }

    void Set(const char* InName, int InValue) override {
        NgxParamValue val;
        val.type = NgxParamType::Int;
        val.intVal = InValue;
        SetValue(InName, val);
    }

    void Set(const char* InName, ID3D11Resource* InValue) override {
        NgxParamValue val;
        val.type = NgxParamType::D3D11Resource;
        val.d3d11Val = InValue;
        SetValue(InName, val);
    }

    void Set(const char* InName, ID3D12Resource* InValue) override {
        NgxParamValue val;
        val.type = NgxParamType::D3D12Resource;
        val.d3d12Val = InValue;
        SetValue(InName, val);
    }

    void Set(const char* InName, void* InValue) override {
        NgxParamValue val;
        val.type = NgxParamType::VoidPtr;
        val.voidPtrVal = InValue;
        SetValue(InName, val);
    }

    NVSDK_NGX_Result Get(const char* InName, unsigned long long* OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        auto it = m_params.find(std::string_view(InName));
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
        auto it = m_params.find(std::string_view(InName));
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
        auto it = m_params.find(std::string_view(InName));
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
        auto it = m_params.find(std::string_view(InName));
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
        auto it = m_params.find(std::string_view(InName));
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
        auto it = m_params.find(std::string_view(InName));
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
        auto it = m_params.find(std::string_view(InName));
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
        auto it = m_params.find(std::string_view(InName));
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

    void CopyFrom(const NVSDK_NGX_Parameter* source);

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
        return m_params.find(std::string_view(name)) != m_params.end();
    }

private:
    void SetValue(const char* InName, const NgxParamValue& val) {
        if (!InName) return;
        std::string_view sv(InName);
        auto it = m_params.find(sv);
        if (it != m_params.end()) {
            it->second = val;
        } else {
            m_params.emplace(std::string(sv), val);
        }
    }

    std::unordered_map<std::string, NgxParamValue, TransparentStringHash, TransparentStringEqual> m_params;
};

struct VirtualNgxHandle : public NVSDK_NGX_Handle {
    uint32_t magic = VIRTUAL_NGX_HANDLE_MAGIC;
    NVSDK_NGX_Handle* hLeft = nullptr;
    NVSDK_NGX_Handle* hRight = nullptr;
    NVSDK_NGX_Feature featureId = NVSDK_NGX_Feature_Reserved;
    std::shared_ptr<NgxParameterImpl> clonedParameters;

    uint32_t targetWidth = 0;
    uint32_t targetHeight = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    NVSDK_NGX_PerfQuality_Value perfQuality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
    InFlightCallGate callGate;

    bool IsValid() const {
        return magic == VIRTUAL_NGX_HANDLE_MAGIC && hLeft != nullptr && hRight != nullptr;
    }
};

}
