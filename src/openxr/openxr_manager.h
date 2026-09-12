#pragma once

#include "openxr_dispatch.h"
#include <memory>
#include <string>

namespace fs25vr {

enum class OpenXrState {
    Uninitialized,
    PassThrough,
    Active
};

struct OpenXrRuntimeInfo {
    std::string runtimeName;
    uint32_t runtimeVersionMajor = 0;
    uint32_t runtimeVersionMinor = 0;
    uint32_t runtimeVersionPatch = 0;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    std::string systemName;
    bool d3d12ExtensionSupported = false;

    uint32_t maxSwapchainImageWidth = 0;
    uint32_t maxSwapchainImageHeight = 0;
    uint32_t maxLayerCount = 0;

    bool orientationTracking = false;
    bool positionTracking = false;

    D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    LUID adapterLuid{};
};

class OpenXrManager {
public:
    explicit OpenXrManager(std::shared_ptr<IOpenXrDispatch> dispatch = nullptr);
    ~OpenXrManager();

    bool Initialize();
    void Shutdown();

    OpenXrState GetState() const { return m_state; }
    bool IsPassThrough() const { return m_state == OpenXrState::PassThrough; }
    bool IsActive() const { return m_state == OpenXrState::Active; }

    const OpenXrRuntimeInfo& GetRuntimeInfo() const { return m_runtimeInfo; }
    XrInstance GetInstance() const { return m_instance; }

private:
    std::shared_ptr<IOpenXrDispatch> m_dispatch;
    OpenXrState m_state = OpenXrState::Uninitialized;
    XrInstance m_instance = XR_NULL_HANDLE;
    OpenXrRuntimeInfo m_runtimeInfo;

    bool CheckD3D12ExtensionSupport();
};

} // namespace fs25vr
