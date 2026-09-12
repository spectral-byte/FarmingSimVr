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

struct HeadPose {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;

    float pitch = 0.0f; // radians (X-axis)
    float yaw = 0.0f;   // radians (Y-axis)
    float roll = 0.0f;  // radians (Z-axis)

    bool isValid = false;
    bool positionTracked = false;
    bool orientationTracked = false;
};

class OpenXrManager {
public:
    static OpenXrManager& Instance();

    explicit OpenXrManager(std::shared_ptr<IOpenXrDispatch> dispatch = nullptr);
    ~OpenXrManager();

    bool Initialize();
    void Shutdown();

    bool CreateSession(ID3D12Device* device = nullptr, ID3D12CommandQueue* queue = nullptr);
    bool CreateTrackingSpaces();
    HeadPose PollHeadPose(XrTime time = 0);
    void Recenter();
    void SetSimulatedPose(const HeadPose& pose);
    const HeadPose& GetLastPose() const { return m_lastPose; }
    const HeadPose& GetRawPose() const { return m_rawPose; }

    OpenXrState GetState() const { return m_state; }
    bool IsPassThrough() const { return m_state == OpenXrState::PassThrough; }
    bool IsActive() const { return m_state == OpenXrState::Active; }

    const OpenXrRuntimeInfo& GetRuntimeInfo() const { return m_runtimeInfo; }
    XrInstance GetInstance() const { return m_instance; }
    XrSession GetSession() const { return m_session; }
    XrSpace GetLocalSpace() const { return m_localSpace; }
    XrSpace GetViewSpace() const { return m_viewSpace; }
    bool HasSession() const { return m_session != XR_NULL_HANDLE; }
    bool HasTrackingSpaces() const { return m_localSpace != XR_NULL_HANDLE && m_viewSpace != XR_NULL_HANDLE; }

    static void QuaternionToEuler(float qx, float qy, float qz, float qw,
                                  float& pitch, float& yaw, float& roll);

private:
    struct RecenterOffset {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
    };

    std::shared_ptr<IOpenXrDispatch> m_dispatch;
    OpenXrState m_state = OpenXrState::Uninitialized;
    XrInstance m_instance = XR_NULL_HANDLE;
    OpenXrRuntimeInfo m_runtimeInfo;

    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_localSpace = XR_NULL_HANDLE;
    XrSpace m_viewSpace = XR_NULL_HANDLE;

    HeadPose m_rawPose;
    HeadPose m_lastPose;
    RecenterOffset m_recenterOffset;
    bool m_hasRecenter = false;
    bool m_useSimulatedPose = false;
    HeadPose m_simulatedPose;

    bool CheckD3D12ExtensionSupport();
};

} // namespace fs25vr
