#pragma once

#include "openxr_dispatch.h"
#include "d3d12_eye_copy.h"
#include "menu_quad.h"
#include "openxr_cursor_overlay.h"
#include "openxr_input.h"
#include "openxr_types.h"
#include "core/math_utils.h"
#include "core/raii_utils.h"
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
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
    bool win32TimeConversionEnabled = false;

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

class OpenXrManager {
public:
    explicit OpenXrManager(std::shared_ptr<IOpenXrDispatch> dispatch);
    ~OpenXrManager();

    bool Initialize();
    void Shutdown();

    bool CreateSession(ID3D12Device* device = nullptr, ID3D12CommandQueue* queue = nullptr);
    bool CreateTrackingSpaces();
    HeadPose PollHeadPose(XrTime time = 0);
    void Recenter();
    const HeadPose& GetLastPose() const { return m_lastPose; }
    const HeadPose& GetRawPose() const { return m_rawPose; }

    bool HasMotionControllerActions() const { return m_input.IsReady(); }
    HandState GetHandState(uint32_t hand);
    bool PulseHaptic(uint32_t hand, double durationSeconds, float amplitude);

    EyeProjection PollEyeProjection(uint32_t eyeIndex = 0, XrTime time = 0);
    bool LocateActiveViews(XrTime time = 0);
    XrTime GetCurrentXrTime() const;

    struct PerformanceCounterSample {
        int64_t counter = 0;
        int64_t frequency = 0;
    };
    using PerformanceCounterSource = std::function<PerformanceCounterSample()>;
    void SetPerformanceCounterSource(PerformanceCounterSource source);

    using FrameBeginObserver = std::function<void()>;
    void SetFrameBeginObserver(const void* owner, FrameBeginObserver observer);
    void ClearFrameBeginObserver(const void* owner);
    const EyeProjection& GetEyeProjection(uint32_t eyeIndex = 0) const;
    static EyeProjection ComputeProjectionFromFov(const XrFovf& fov, float swapchainAspect = 0.0f, float fovOverscanPercent = 0.0f);

    void SetFovOverscan(float percent);
    float GetFovOverscan() const { return m_requestedFovOverscan.load(); }
    static constexpr float kMaxFovOverscanPercent = 20.0f;
    static float ClampFovOverscan(float percent);
    void SetFovOverscanSuspended(bool suspended) { m_fovOverscanSuspended.store(suspended); }

    uint32_t GetRecommendedEyeWidth(uint32_t eyeIndex = 0) const;
    uint32_t GetRecommendedEyeHeight(uint32_t eyeIndex = 0) const;
    const std::vector<XrViewConfigurationView>& GetViewConfigurationViews() const { return m_viewConfigViews; }

    void SetD3D12DeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue);
    ID3D12Device* GetD3D12Device() const { return m_device; }
    ID3D12CommandQueue* GetD3D12CommandQueue() const { return m_queue; }

    bool CreateEyeSwapchains(uint32_t width = 0, uint32_t height = 0, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    static DXGI_FORMAT SelectCompatibleSwapchainFormat(DXGI_FORMAT backBufferFormat, const std::vector<int64_t>& supported);
    bool EnsureBackBufferFormatSupported(DXGI_FORMAT backBufferFormat);
    void DestroyEyeSwapchains();
    bool HasEyeSwapchains() const { return m_hasEyeSwapchains; }
    const EyeSwapchain& GetEyeSwapchain(uint32_t eyeIndex = 0) const;

    bool CopyBackBufferToEye(uint32_t eyeIndex, ID3D12Resource* backBuffer);
    bool ClearBackBufferToBlack(ID3D12Resource* backBuffer);
    bool BeginFrame();
    bool EndFrame();
    bool IsFrameBegun() const { return m_frameBegun; }
    bool SubmitFramePair();
    bool SubmitMenuQuad(const XrPosef* customPose = nullptr, const XrExtent2Df* customSize = nullptr);

    XrPosef ComputeMenuQuadPose() const { return m_menuQuad.Pose(); }
    static XrPosef ComputeMenuQuadPoseFromHeadPose(const HeadPose& headPose, float distance) {
        return MenuQuad::PoseFromHeadPose(headPose, distance);
    }
    void AlignMenuQuadToHeadPose(const HeadPose* customPose = nullptr);
    bool HasMenuQuadPose() const { return m_menuQuad.HasPose(); }
    XrExtent2Df ComputeMenuQuadSize() const;
    static constexpr float kDefaultMenuQuadDistance = MenuQuad::kDefaultDistance;
    static constexpr float kMinMenuQuadDistance = MenuQuad::kMinDistance;
    static constexpr float kMaxMenuQuadDistance = MenuQuad::kMaxDistance;
    static float ClampMenuQuadDistance(float distance) { return MenuQuad::ClampDistance(distance); }
    void SetMenuQuadDistance(float distance) { m_menuQuad.SetDistance(distance); }
    float GetMenuQuadDistance() const { return m_menuQuad.Distance(); }
    void SetMenuQuadSize(float width, float height) { m_menuQuad.SetCustomSize(width, height); }
    XrExtent2Df GetMenuQuadSize() const { return ComputeMenuQuadSize(); }
    void ResetMenuQuadSettings() { m_menuQuad.ResetSettings(); }

    PanelHit GetPanelHit(uint32_t hand);
    PanelHit GetPanelHitAlongRay(uint32_t hand, const Vector3& origin, const Vector3& direction);
    static PanelHit ComputeQuadRayHit(const XrPosef& quadPose, const XrExtent2Df& quadSize,
                                      const Vector3& rayOrigin, const Quaternion& rayOrientation) {
        return MenuQuad::RayHit(quadPose, quadSize, rayOrigin, rayOrientation);
    }
    static PanelHit ComputeQuadRayHitAlong(const XrPosef& quadPose, const XrExtent2Df& quadSize,
                                           const Vector3& rayOrigin, const Vector3& rayDirection) {
        return MenuQuad::RayHitAlong(quadPose, quadSize, rayOrigin, rayDirection);
    }

    void SetPointerCursorOverride(bool active, float u = 0.0f, float v = 0.0f) {
        m_pointerCursor.active = active;
        m_pointerCursor.u = std::fmin(1.0f, std::fmax(0.0f, u));
        m_pointerCursor.v = std::fmin(1.0f, std::fmax(0.0f, v));
    }
    const PointerCursorOverride& GetPointerCursorOverride() const { return m_pointerCursor; }

    uint64_t GetSubmittedQuadFrameCount() const { return m_submittedQuadFrameCount; }
    const XrCompositionLayerQuad& GetLastSubmittedQuadLayer() const { return m_lastSubmittedQuadLayer; }

    bool CreateCursorSwapchain(uint32_t width = 64, uint32_t height = 64, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    void DestroyCursorSwapchain();
    bool HasCursorSwapchain() const { return m_cursorOverlay.HasSwapchain(); }
    const EyeSwapchain& GetCursorSwapchain() const { return m_cursorOverlay.GetSwapchain(); }

    void SetMenuCursorState(const MenuCursorState& cursorState) { m_menuCursorState = cursorState; }
    const MenuCursorState& GetMenuCursorState() const { return m_menuCursorState; }

    static XrPosef ComputeCursorQuadPose(const XrPosef& menuQuadPose, const XrExtent2Df& menuQuadSize,
                                         const MenuCursorState& cursorState, float cursorWidth, float cursorHeight);
    static XrPosef ComputeCursorQuadPose(const XrPosef& menuQuadPose, const XrExtent2Df& menuQuadSize,
                                         float normX, float normY, float cursorWidth, float cursorHeight);

    const XrCompositionLayerQuad& GetLastSubmittedCursorQuadLayer() const { return m_cursorOverlay.GetLastSubmittedCursorQuadLayer(); }
    bool HasLastSubmittedCursorQuadLayer() const { return m_cursorOverlay.HasLastSubmittedCursorQuadLayer(); }

    void LogEyeCopySizeMismatchOnce(uint64_t srcWidth, uint32_t srcHeight, uint64_t dstWidth, uint32_t dstHeight);

    float GetIpd() const { return m_ipd; }
    void SetIpd(float ipd) { m_ipd = ipd; }

    uint64_t GetSubmittedFramePairCount() const { return m_submittedFramePairCount; }

    OpenXrState GetState() const { return m_state.load(); }
    bool IsPassThrough() const { return m_state.load() == OpenXrState::PassThrough; }
    bool IsActive() const { return m_state.load() == OpenXrState::Active; }

    const OpenXrRuntimeInfo& GetRuntimeInfo() const { return m_runtimeInfo; }
    XrSession GetSession() const { return m_session.load(); }
    XrSpace GetLocalSpace() const { return m_localSpace; }
    XrSpace GetBaseLocalSpace() const { return m_baseLocalSpace; }
    XrSpace GetViewSpace() const { return m_viewSpace; }
    bool HasSession() const { return m_session.load() != XR_NULL_HANDLE; }
    bool IsSessionRunning() const { return m_sessionRunning; }
    bool IsSessionFocused() const { return m_sessionState == XR_SESSION_STATE_FOCUSED; }
    XrSessionState GetSessionState() const { return m_sessionState; }

    void PollEvents();
    bool HasTrackingSpaces() const {
        return m_localSpace != XR_NULL_HANDLE &&
               m_baseLocalSpace != XR_NULL_HANDLE &&
               m_viewSpace != XR_NULL_HANDLE;
    }

    static EulerAngles QuaternionToEuler(const Quaternion& q);
    static Vector3 RotateVectorByQuaternion(const Quaternion& q, const Vector3& v);
    static Vector3 ComputeEyePosition(const HeadPose& headPose, float ipd, uint32_t eyeIndex);
    static HeadPose ComputeEyePose(const HeadPose& headPose, float ipd, uint32_t eyeIndex);

    void RecordRenderPose(uint32_t eyeIndex, const HeadPose& pose);
    bool HasRecordedRenderPose(uint32_t eyeIndex) const {
        return eyeIndex < 2 && m_hasRenderPose[eyeIndex];
    }

    const HeadPose& GetCapturedEyePose(uint32_t eyeIndex = 0) const {
        return m_capturedEyePoses[eyeIndex >= 2 ? 0 : eyeIndex];
    }

private:
    std::shared_ptr<IOpenXrDispatch> m_dispatch;
    std::atomic<OpenXrState> m_state{OpenXrState::Uninitialized};
    XrInstance m_instance = XR_NULL_HANDLE;
    OpenXrRuntimeInfo m_runtimeInfo;

    std::atomic<XrSession> m_session{XR_NULL_HANDLE};
    bool m_sessionRunning = false;
    XrSessionState m_sessionState = XR_SESSION_STATE_UNKNOWN;
    uint32_t m_beginSessionFailures = 0;
    XrSpace m_baseLocalSpace = XR_NULL_HANDLE;
    XrSpace m_localSpace = XR_NULL_HANDLE;
    XrSpace m_viewSpace = XR_NULL_HANDLE;

    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_queue = nullptr;
    D3D12EyeCopy m_eyeCopy;
    XrResult DispatchEndFrame(const XrFrameEndInfo& endInfo);

    EyeSwapchain m_eyeSwapchains[2];
    bool m_hasEyeSwapchains = false;
    OpenXrCursorOverlay m_cursorOverlay;
    OpenXrInput m_input;
    uint64_t m_frameSerial = 0;
    uint64_t m_handsFrameSerial = 0;
    bool m_handPosesStale = false;
    void RefreshHandState(XrTime time, bool resync);
    MenuCursorState m_menuCursorState;
    PointerCursorOverride m_pointerCursor;
    float m_ipd = 0.064f;
    float m_lastLoggedIpd = 0.0f;
    bool m_loggedEyeCopySizeMismatch = false;
    bool m_loggedMissingEyeProjection = false;
    uint32_t m_headPoseLocateFailures = 0;
    uint64_t m_submittedFramePairCount = 0;
    uint64_t m_submittedQuadFrameCount = 0;
    MenuQuad m_menuQuad;
    XrCompositionLayerQuad m_lastSubmittedQuadLayer{ XR_TYPE_COMPOSITION_LAYER_QUAD };
    bool m_focusedSinceBegin = false;
    bool m_frameBegun = false;
    XrFrameState m_frameState{XR_TYPE_FRAME_STATE};
    XrTime m_lastPredictedDisplayTime = 0;
    int64_t m_lastPredictedQpc = 0;
    PerformanceCounterSource m_counterSource;
    using FrameBeginObserverList = std::vector<std::pair<const void*, FrameBeginObserver>>;
    std::mutex m_frameBeginObserversMutex;
    std::shared_ptr<const FrameBeginObserverList> m_frameBeginObservers = std::make_shared<const FrameBeginObserverList>();
    bool m_win32TimeConversionAvailable = false;
    PerformanceCounterSample ReadPerformanceCounter() const;
    XrTime m_lastPolledTime = 0;
    XrTime m_lastLocatedViewsTime = 0;

    HeadPose m_rawPose;
    HeadPose m_lastPose;
    HeadPose m_capturedEyePoses[2]{};
    HeadPose m_renderPoses[2]{};
    bool m_hasRenderPose[2]{false, false};
    void ClearRenderPoses();

    EyeProjection m_eyeProjections[2];
    float SwapchainAspect(uint32_t eyeIndex) const;
    bool HasRenderedFov() const;
    void LatchFovOverscan();
    void RefreshRenderedFov();
    void LogFovIfChanged();
    static bool NearlyEqualAngle(float a, float b) { return !(std::abs(a - b) > 1e-4f); }
    struct HeadsetFov {
        float left = 0.0f, right = 0.0f, up = 0.0f, down = 0.0f;  // radians
        bool operator==(const HeadsetFov& other) const {
            return NearlyEqualAngle(left, other.left) && NearlyEqualAngle(right, other.right) &&
                   NearlyEqualAngle(up, other.up) && NearlyEqualAngle(down, other.down);
        }
    };
    struct RenderedFov {
        float vertical = 0.0f, horizontal = 0.0f;  // radians
        bool operator==(const RenderedFov& other) const {
            return NearlyEqualAngle(vertical, other.vertical) && NearlyEqualAngle(horizontal, other.horizontal);
        }
    };
    HeadsetFov m_loggedHeadsetFov[2]{};
    RenderedFov m_loggedRenderedFov[2]{};
    bool m_loggedHeadsetFovValid = false;
    bool m_loggedRenderedFovValid = false;
    float m_loggedFovOverscan = 0.0f;
    std::atomic<float> m_requestedFovOverscan{0.0f};
    std::atomic<bool> m_fovOverscanSuspended{false};
    float m_fovOverscan = 0.0f;

    std::vector<XrViewConfigurationView> m_viewConfigViews;

    void HandleSessionStateChanged(XrSessionState state);
    void BeginRunningSession();
    void EndRunningSession();
    void ReleaseAndEnterPassThrough(const char* reason);
    bool EnterPassThroughIfLost(XrResult res);
    bool CheckD3D12ExtensionSupport();
    bool EnumerateSwapchainImagesHelper(EyeSwapchain& sc);
    bool EnsureCursorTextureUploaded();
    DXGI_FORMAT NegotiateEyeSwapchainFormat(DXGI_FORMAT backBufferFormat);
    void EnterPassThroughForIncompatibleFormat(DXGI_FORMAT backBufferFormat);
};

}
