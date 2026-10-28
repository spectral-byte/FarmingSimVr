#pragma once

#include "openxr_dispatch.h"
#include "openxr_cursor_overlay.h"
#include "openxr_input.h"
#include "openxr_types.h"
#include "core/math_utils.h"
#include "core/raii_utils.h"
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
    // The runtime offers XR_KHR_win32_convert_performance_counter_time and it was enabled.
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

    // ---- Motion Controllers (ADR-0025) ----
    // True when the Motion Controller action set was declared and attached for this session. False
    // when the runtime refused any step (the session and head tracking are unaffected).
    bool HasMotionControllerActions() const { return m_input.IsReady(); }
    // One hand's state (0 = left, 1 = right), located at the head pose's predicted display time in
    // the same Recenter-aware reference space. Untracked defaults when the action set is unavailable.
    HandState GetHandState(uint32_t hand);
    // A short vibration on one hand (0 = left, 1 = right): `durationSeconds` long at `amplitude`
    // (clamped to 0..1) at the runtime's default frequency. False, and nothing played, for a hand that
    // does not exist, a duration of zero or less, an unavailable action set, or a runtime that fails
    // the call; never an error for the session.
    bool PulseHaptic(uint32_t hand, double durationSeconds, float amplitude);

    EyeProjection PollEyeProjection(uint32_t eyeIndex = 0, XrTime time = 0);
    bool LocateActiveViews(XrTime time = 0);
    // "Now" in the runtime's time base. Extrapolates from the last predicted display time once
    // frames run; before the first frame converts the performance counter through the runtime
    // (XR_KHR_win32_convert_performance_counter_time) or, without it, converts the counter to
    // nanoseconds directly.
    XrTime GetCurrentXrTime() const;

    struct PerformanceCounterSample {
        int64_t counter = 0;
        int64_t frequency = 0;
    };
    using PerformanceCounterSource = std::function<PerformanceCounterSample()>;
    // Replaces the QueryPerformanceCounter/Frequency reads (tests drive a fake clock).
    void SetPerformanceCounterSource(PerformanceCounterSource source);
    // counter / frequency in nanoseconds without overflowing for any int64 counter.
    static int64_t PerformanceCounterToNanoseconds(int64_t counter, int64_t frequency);

    // `observer` runs at the end of every BeginFrame that began a frame, after the hand state is synced. One
    // observer per `owner` (the Virtual Gamepad publishes its snapshot and plays its rumble from here); clearing is by the same
    // owner. Safe from any thread, and from inside an observer: a frame calls the observers it began with.
    using FrameBeginObserver = std::function<void()>;
    void SetFrameBeginObserver(const void* owner, FrameBeginObserver observer);
    void ClearFrameBeginObserver(const void* owner);
    const EyeProjection& GetEyeProjection(uint32_t eyeIndex = 0) const;
    static EyeProjection ComputeProjectionFromFov(const XrFovf& fov);

    uint32_t GetRecommendedEyeWidth(uint32_t eyeIndex = 0) const;
    uint32_t GetRecommendedEyeHeight(uint32_t eyeIndex = 0) const;
    const std::vector<XrViewConfigurationView>& GetViewConfigurationViews() const { return m_viewConfigViews; }

    void SetD3D12DeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue);
    ID3D12Device* GetD3D12Device() const { return m_device; }
    ID3D12CommandQueue* GetD3D12CommandQueue() const { return m_queue; }

    bool CreateEyeSwapchains(uint32_t width = 0, uint32_t height = 0, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    // Picks the supported swapchain format that a plain copy from a back buffer of
    // `backBufferFormat` can target (same format family; UNORM/sRGB variants allowed). For 8-bit
    // families the sRGB variant is preferred over UNORM, whatever the back buffer's variant.
    // Returns DXGI_FORMAT_UNKNOWN when none is compatible.
    static DXGI_FORMAT SelectCompatibleSwapchainFormat(DXGI_FORMAT backBufferFormat, const std::vector<int64_t>& supported);
    // Called when the game creates its swap chain, before any DXGI overrides are applied. Releases
    // OpenXR and enters Pass-Through Mode if no eye-swapchain format is copy-compatible with the
    // back buffer. Returns whether VR is still active.
    bool EnsureBackBufferFormatSupported(DXGI_FORMAT backBufferFormat);
    void DestroyEyeSwapchains();
    bool HasEyeSwapchains() const { return m_hasEyeSwapchains; }
    const EyeSwapchain& GetEyeSwapchain(uint32_t eyeIndex = 0) const;

    bool CopyBackBufferToEye(uint32_t eyeIndex, ID3D12Resource* backBuffer);
    // Clears the game's back buffer to black on the game's queue and leaves it in the PRESENT state
    // the game expects, so the next Present puts a black frame on the Desktop Mirror Window. Call it
    // after the frame's eye copy, which has already taken what the headset needs. Returns false and
    // does nothing when there is no back buffer, device or queue (unit tests, Pass-Through).
    bool ClearBackBufferToBlack(ID3D12Resource* backBuffer);
    bool BeginFrame();
    bool EndFrame();
    bool IsFrameBegun() const { return m_frameBegun; }
    bool SubmitFramePair();
    bool SubmitMenuQuad(const XrPosef* customPose = nullptr, const XrExtent2Df* customSize = nullptr);

    XrPosef ComputeMenuQuadPose() const;
    static XrPosef ComputeMenuQuadPoseFromHeadPose(const HeadPose& headPose, float distance);
    void AlignMenuQuadToHeadPose(const HeadPose* customPose = nullptr);
    bool HasMenuQuadPose() const { return m_hasMenuQuadPose; }
    XrExtent2Df ComputeMenuQuadSize() const;
    void SetMenuQuadDistance(float distance) { m_menuQuadDistance = distance; }
    float GetMenuQuadDistance() const { return m_menuQuadDistance; }
    void SetMenuQuadSize(float width, float height) { m_customMenuQuadSize = { width, height }; m_hasCustomMenuQuadSize = true; }
    XrExtent2Df GetMenuQuadSize() const { return ComputeMenuQuadSize(); }
    void ResetMenuQuadSettings() { m_menuQuadDistance = 1.5f; m_hasCustomMenuQuadSize = false; m_hasMenuQuadPose = false; }

    // Where the ray of `hand` (0 = left, 1 = right) lands on the panel the next SubmitMenuQuad()
    // submits, in normalized panel coordinates (see PanelHit). The hand's aim pose and the layer are
    // both in the Recenter-aware local space, so the test holds across a Recenter. No hit for an
    // untracked hand. Whether a panel is visible is the caller's (VRBridge's) knowledge.
    PanelHit GetPanelHit(uint32_t hand);
    // The same for a ray the caller gives, in the space the hand poses are in (the Pointer Ray starts at the
    // index fingertip and runs along the finger, which only the Companion Mod knows). `direction` need not be
    // a unit vector. No hit for an untracked `hand`, or a direction without a length.
    PanelHit GetPanelHitAlongRay(uint32_t hand, const Vector3& origin, const Vector3& direction);
    // Pure ray (origin, -Z of rayOrientation) against a quad of `quadSize` at `quadPose`; the quad
    // is hit from its front side only, like it is seen.
    static PanelHit ComputeQuadRayHit(const XrPosef& quadPose, const XrExtent2Df& quadSize,
                                      const Vector3& rayOrigin, const Quaternion& rayOrientation);
    // The same for a ray with a direction vector (any length above zero; `distance` is along the unit direction).
    static PanelHit ComputeQuadRayHitAlong(const XrPosef& quadPose, const XrExtent2Df& quadSize,
                                           const Vector3& rayOrigin, const Vector3& rayDirection);

    // While active the drawn cursor sits at (u, v) in PanelHit coordinates (clamped onto the panel)
    // and MirrorWindowController ignores the Win32 cursor: the Pointer Ray has control of the cursor.
    // Inactive means the desk mouse has it.
    void SetPointerCursorOverride(bool active, float u = 0.0f, float v = 0.0f) {
        m_pointerCursor.active = active;
        m_pointerCursor.u = std::fmin(1.0f, std::fmax(0.0f, u));
        m_pointerCursor.v = std::fmin(1.0f, std::fmax(0.0f, v));
    }
    const PointerCursorOverride& GetPointerCursorOverride() const { return m_pointerCursor; }

    uint64_t GetSubmittedQuadFrameCount() const { return m_submittedQuadFrameCount; }
    void ResetSubmittedQuadFrameCount() { m_submittedQuadFrameCount = 0; }
    const XrCompositionLayerQuad& GetLastSubmittedQuadLayer() const { return m_lastSubmittedQuadLayer; }

    bool CreateCursorSwapchain(uint32_t width = 64, uint32_t height = 64, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    void DestroyCursorSwapchain();
    bool HasCursorSwapchain() const { return m_cursorOverlay.HasSwapchain(); }
    const EyeSwapchain& GetCursorSwapchain() const { return m_cursorOverlay.GetSwapchain(); }
    OpenXrCursorOverlay& GetCursorOverlay() { return m_cursorOverlay; }
    const OpenXrCursorOverlay& GetCursorOverlay() const { return m_cursorOverlay; }

    void SetMenuCursorState(const MenuCursorState& cursorState) { m_menuCursorState = cursorState; }
    const MenuCursorState& GetMenuCursorState() const { return m_menuCursorState; }

    static XrPosef ComputeCursorQuadPose(const XrPosef& menuQuadPose, const XrExtent2Df& menuQuadSize,
                                         const MenuCursorState& cursorState, float cursorWidth, float cursorHeight);
    static XrPosef ComputeCursorQuadPose(const XrPosef& menuQuadPose, const XrExtent2Df& menuQuadSize,
                                         float normX, float normY, float cursorWidth, float cursorHeight);

    const XrCompositionLayerQuad& GetLastSubmittedCursorQuadLayer() const { return m_cursorOverlay.GetLastSubmittedCursorQuadLayer(); }
    bool HasLastSubmittedCursorQuadLayer() const { return m_cursorOverlay.HasLastSubmittedCursorQuadLayer(); }

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
    XrSpace GetBaseLocalSpace() const { return m_baseLocalSpace; }
    XrSpace GetViewSpace() const { return m_viewSpace; }
    bool HasSession() const { return m_session != XR_NULL_HANDLE; }
    bool IsSessionRunning() const { return m_sessionRunning; }
    // The headset is on and the game has the focus. A sleeping headset drops to SYNCHRONIZED and the dashboard to
    // VISIBLE; neither ends the running session.
    bool IsSessionFocused() const { return m_sessionState == XR_SESSION_STATE_FOCUSED; }
    XrSessionState GetSessionState() const { return m_sessionState; }

    // Drains the runtime event queue and drives the session lifecycle: begin on READY,
    // end on STOPPING, release resources and enter Pass-Through Mode on EXITING,
    // LOSS_PENDING or instance loss. BeginFrame polls too; a caller that must see the lifecycle
    // settled before it reaches BeginFrame (DxgiProxy::OnPresent) calls this first. A second poll
    // in the same frame finds an empty queue.
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

    // Records the head pose handed to the Companion Mod for this eye's render. The next
    // CopyBackBufferToEye for that eye uses it (instead of the pose polled at frame begin)
    // as the pose the eye is submitted with. Consumed by the copy.
    void RecordRenderPose(uint32_t eyeIndex, const HeadPose& pose);
    bool HasRecordedRenderPose(uint32_t eyeIndex) const {
        return eyeIndex < 2 && m_hasRenderPose[eyeIndex];
    }

    const HeadPose& GetCapturedEyePose(uint32_t eyeIndex = 0) const {
        return m_capturedEyePoses[eyeIndex >= 2 ? 0 : eyeIndex];
    }

private:
    std::shared_ptr<IOpenXrDispatch> m_dispatch;
    OpenXrState m_state = OpenXrState::Uninitialized;
    XrInstance m_instance = XR_NULL_HANDLE;
    OpenXrRuntimeInfo m_runtimeInfo;

    XrSession m_session = XR_NULL_HANDLE;
    bool m_sessionRunning = false;
    XrSessionState m_sessionState = XR_SESSION_STATE_UNKNOWN;
    uint32_t m_beginSessionFailures = 0;
    XrSpace m_baseLocalSpace = XR_NULL_HANDLE;
    XrSpace m_localSpace = XR_NULL_HANDLE;
    XrSpace m_viewSpace = XR_NULL_HANDLE;

    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_queue = nullptr;
    static constexpr uint32_t kCopyRingSize = 2;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_copyAllocators[kCopyRingSize];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_copyCommandLists[kCopyRingSize];
    Microsoft::WRL::ComPtr<ID3D12Fence> m_copyFence;
    uint64_t m_copyFenceValues[kCopyRingSize] = {0, 0};
    uint64_t m_currentFenceValue = 0;
    uint32_t m_copyRingIndex = 0;
    ScopedEventHandle m_copyFenceEvent;
    void EnsureCopyFence();
    // ClearBackBufferToBlack's own recording objects; it signals m_copyFence with m_clearFenceValue.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_clearAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_clearCommandList;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_clearRtvHeap;
    uint64_t m_clearFenceValue = 0;

    EyeSwapchain m_eyeSwapchains[2];
    bool m_hasEyeSwapchains = false;
    OpenXrCursorOverlay m_cursorOverlay;
    OpenXrInput m_input;
    // The frame whose hand snapshot (sync + poses) is held; valid only while that frame is begun.
    uint64_t m_frameSerial = 0;
    uint64_t m_handsFrameSerial = 0;
    bool m_handPosesStale = false;
    void RefreshHandState(XrTime time, bool resync);
    MenuCursorState m_menuCursorState;
    PointerCursorOverride m_pointerCursor;
    float m_ipd = 0.064f;
    uint64_t m_submittedFramePairCount = 0;
    uint64_t m_submittedQuadFrameCount = 0;
    float m_menuQuadDistance = 1.5f;
    XrExtent2Df m_customMenuQuadSize{ 1.6f, 0.9f };
    bool m_hasCustomMenuQuadSize = false;
    XrCompositionLayerQuad m_lastSubmittedQuadLayer{ XR_TYPE_COMPOSITION_LAYER_QUAD };
    XrPosef m_menuQuadPose{};
    bool m_hasMenuQuadPose = false;
    // The menu quad moves to the head on the first rendered frame with a valid head pose predicted at
    // or after m_menuQuadAwaitsHeadAfter: it was placed from no pose, the runtime recentered the
    // local space under it, or the running session was focused for the first time.
    bool m_menuQuadAwaitsHead = false;
    XrTime m_menuQuadAwaitsHeadAfter = 0;
    bool m_focusedSinceBegin = false;
    bool m_frameBegun = false;
    XrFrameState m_frameState{XR_TYPE_FRAME_STATE};
    XrTime m_lastPredictedDisplayTime = 0;
    int64_t m_lastPredictedQpc = 0;
    PerformanceCounterSource m_counterSource;
    // Copy-on-write: an edit builds a new list under the mutex, a frame takes a reference to the current one (no copy, no
    // allocation) and walks it without the lock.
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

    std::vector<XrViewConfigurationView> m_viewConfigViews;

    void HandleSessionStateChanged(XrSessionState state);
    void BeginRunningSession();
    void EndRunningSession();
    void ReleaseAndEnterPassThrough(const char* reason);
    bool EnterPassThroughIfLost(XrResult res);
    bool CheckD3D12ExtensionSupport();
    bool EnumerateSwapchainImagesHelper(EyeSwapchain& sc);
    bool EnsureCursorTextureUploaded();
    // Copy-compatible eye format, the back buffer format if the runtime query failed, or UNKNOWN.
    DXGI_FORMAT NegotiateEyeSwapchainFormat(DXGI_FORMAT backBufferFormat);
    void EnterPassThroughForIncompatibleFormat(DXGI_FORMAT backBufferFormat);
};

} // namespace fs25vr
