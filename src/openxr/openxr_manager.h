#pragma once

#include "openxr_dispatch.h"
#include <memory>
#include <string>

#include <wrl/client.h>
#include <vector>

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

    uint32_t recommendedEyeWidth = 0;
    uint32_t recommendedEyeHeight = 0;
    uint32_t maxSwapchainImageWidth = 0;
    uint32_t maxSwapchainImageHeight = 0;
    uint32_t maxLayerCount = 0;

    bool orientationTracking = false;
    bool positionTracking = false;

    D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    LUID adapterLuid{};
};

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vector3() = default;
    constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    bool operator==(const Vector3& other) const = default;
};

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quaternion() = default;
    constexpr Quaternion(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    bool operator==(const Quaternion& other) const = default;

    constexpr Quaternion operator*(const Quaternion& o) const {
        return Quaternion(
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
            w * o.w - x * o.x - y * o.y - z * o.z
        );
    }
};

struct EulerAngles {
    float pitch = 0.0f; // radians (X-axis)
    float yaw = 0.0f;   // radians (Y-axis)
    float roll = 0.0f;  // radians (Z-axis)

    constexpr EulerAngles() = default;
    constexpr EulerAngles(float pitch_, float yaw_, float roll_) : pitch(pitch_), yaw(yaw_), roll(roll_) {}

    bool operator==(const EulerAngles& other) const = default;
};

struct HeadPose {
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    Quaternion orientation{ 0.0f, 0.0f, 0.0f, 1.0f };
    EulerAngles rotation{ 0.0f, 0.0f, 0.0f };

    bool isValid = false;
    bool positionTracked = false;
    bool orientationTracked = false;

    constexpr HeadPose() = default;
    constexpr HeadPose(const Vector3& pos, const Quaternion& ori, const EulerAngles& rot,
                       bool valid = true, bool posTracked = true, bool oriTracked = true)
        : position(pos), orientation(ori), rotation(rot),
          isValid(valid), positionTracked(posTracked), orientationTracked(oriTracked) {}
};

struct EyeProjection {
    float fovY = 0.0f;
    float fovX = 0.0f;
    float xOffset = 0.0f;
    float yOffset = 0.0f;
    float xOffsetNorm = 0.0f;
    float yOffsetNorm = 0.0f;
    float tanLeft = 0.0f;
    float tanRight = 0.0f;
    float tanUp = 0.0f;
    float tanDown = 0.0f;
    float angleLeft = 0.0f;
    float angleRight = 0.0f;
    float angleUp = 0.0f;
    float angleDown = 0.0f;
    float aspectRatio = 1.0f;
    bool isValid = false;
};

struct EyeSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::vector<XrSwapchainImageD3D12KHR> images;
    uint32_t lastCopiedIndex = 0;
    bool hasCopiedFrame = false;
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

    EyeProjection PollEyeProjection(uint32_t eyeIndex = 0, XrTime time = 0);
    bool LocateActiveViews(XrTime time = 0);
    XrTime GetCurrentXrTime() const;
    const EyeProjection& GetEyeProjection(uint32_t eyeIndex = 0) const;
    void SetSimulatedProjection(uint32_t eyeIndex, const EyeProjection& proj);
    static EyeProjection ComputeProjectionFromFov(const XrFovf& fov);

    uint32_t GetRecommendedEyeWidth(uint32_t eyeIndex = 0) const;
    uint32_t GetRecommendedEyeHeight(uint32_t eyeIndex = 0) const;
    const std::vector<XrViewConfigurationView>& GetViewConfigurationViews() const { return m_viewConfigViews; }

    void SetD3D12DeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue);
    ID3D12Device* GetD3D12Device() const { return m_device; }
    ID3D12CommandQueue* GetD3D12CommandQueue() const { return m_queue; }

    bool CreateEyeSwapchains(uint32_t width = 0, uint32_t height = 0, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    void DestroyEyeSwapchains();
    bool HasEyeSwapchains() const { return m_hasEyeSwapchains; }
    const EyeSwapchain& GetEyeSwapchain(uint32_t eyeIndex = 0) const;

    bool CopyBackBufferToEye(uint32_t eyeIndex, ID3D12Resource* backBuffer);
    bool BeginFrame();
    bool EndFrame();
    bool IsFrameBegun() const { return m_frameBegun; }
    bool SubmitFramePair();

    float GetIpd() const { return m_ipd; }
    void SetIpd(float ipd) { m_ipd = ipd; }

    uint64_t GetSubmittedFramePairCount() const { return m_submittedFramePairCount; }
    void ResetSubmittedFramePairCount() { m_submittedFramePairCount = 0; }

    OpenXrState GetState() const { return m_state; }
    bool IsPassThrough() const { return m_state == OpenXrState::PassThrough; }
    bool IsActive() const { return m_state == OpenXrState::Active; }

    const OpenXrRuntimeInfo& GetRuntimeInfo() const { return m_runtimeInfo; }
    XrInstance GetInstance() const { return m_instance; }
    XrSession GetSession() const { return m_session; }
    XrSpace GetLocalSpace() const { return m_localSpace; }
    XrSpace GetViewSpace() const { return m_viewSpace; }
    bool HasSession() const { return m_session != XR_NULL_HANDLE; }
    bool IsSessionRunning() const { return m_sessionRunning; }
    bool HasTrackingSpaces() const { return m_localSpace != XR_NULL_HANDLE && m_viewSpace != XR_NULL_HANDLE; }

    static EulerAngles QuaternionToEuler(const Quaternion& q);
    static Vector3 RotateVectorByQuaternion(const Quaternion& q, const Vector3& v);
    static Vector3 ComputeEyePosition(const HeadPose& headPose, float ipd, uint32_t eyeIndex);
    static HeadPose ComputeEyePose(const HeadPose& headPose, float ipd, uint32_t eyeIndex);

    const HeadPose& GetCapturedEyePose(uint32_t eyeIndex = 0) const {
        return m_capturedEyePoses[eyeIndex >= 2 ? 0 : eyeIndex];
    }

private:
    struct RecenterOffset {
        Vector3 position{ 0.0f, 0.0f, 0.0f };
        float yaw = 0.0f;
    };

    std::shared_ptr<IOpenXrDispatch> m_dispatch;
    OpenXrState m_state = OpenXrState::Uninitialized;
    XrInstance m_instance = XR_NULL_HANDLE;
    OpenXrRuntimeInfo m_runtimeInfo;

    XrSession m_session = XR_NULL_HANDLE;
    bool m_sessionRunning = false;
    XrSpace m_localSpace = XR_NULL_HANDLE;
    XrSpace m_viewSpace = XR_NULL_HANDLE;

    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_queue = nullptr;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_copyAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_copyCommandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_copyFence;
    uint64_t m_copyFenceValue = 0;
    HANDLE m_copyFenceEvent = nullptr;

    EyeSwapchain m_eyeSwapchains[2];
    bool m_hasEyeSwapchains = false;
    float m_ipd = 0.064f;
    uint64_t m_submittedFramePairCount = 0;
    bool m_frameBegun = false;
    XrFrameState m_frameState{XR_TYPE_FRAME_STATE};
    XrTime m_lastPredictedDisplayTime = 0;
    int64_t m_lastPredictedQpc = 0;

    HeadPose m_rawPose;
    HeadPose m_lastPose;
    HeadPose m_capturedEyePoses[2]{};
    RecenterOffset m_recenterOffset;
    bool m_hasRecenter = false;
    bool m_useSimulatedPose = false;
    HeadPose m_simulatedPose;

    EyeProjection m_eyeProjections[2];
    EyeProjection m_simulatedProjections[2];
    bool m_useSimulatedProjections = false;

    std::vector<XrViewConfigurationView> m_viewConfigViews;

    bool CheckD3D12ExtensionSupport();
};

} // namespace fs25vr
