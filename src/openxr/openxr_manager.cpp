#include "openxr_manager.h"
#include "core/logger.h"
#include "core/dll_lifecycle.h"
#include <vector>
#include <cstring>
#include <sstream>
#include <cmath>
#include <algorithm>


namespace fs25vr {

// The performance counter frequency is fixed at boot, so it is queried once.
static int64_t QpcFrequency() {
    static const int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<int64_t>(f.QuadPart);
    }();
    return freq;
}

int64_t OpenXrManager::PerformanceCounterToNanoseconds(int64_t counter, int64_t frequency) {
    if (frequency <= 0) return 0;
    // Split into whole seconds and a sub-second remainder: counter * 1e9 overflows int64 after
    // ~15 minutes of uptime at a 10 MHz counter.
    const int64_t seconds = counter / frequency;
    const int64_t remainder = counter % frequency;
    return seconds * 1000000000LL + (remainder * 1000000000LL) / frequency;
}

void OpenXrManager::SetPerformanceCounterSource(PerformanceCounterSource source) {
    m_counterSource = std::move(source);
}

OpenXrManager::PerformanceCounterSample OpenXrManager::ReadPerformanceCounter() const {
    if (m_counterSource) return m_counterSource();
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    return { qpc.QuadPart, QpcFrequency() };
}

XrTime OpenXrManager::GetCurrentXrTime() const {
    const PerformanceCounterSample now = ReadPerformanceCounter();

    if (m_lastPredictedDisplayTime != 0) {
        int64_t elapsedQpc = now.counter - m_lastPredictedQpc;
        if (elapsedQpc > 0 && now.frequency > 0) {
            return m_lastPredictedDisplayTime + PerformanceCounterToNanoseconds(elapsedQpc, now.frequency);
        }
        return m_lastPredictedDisplayTime;
    }

    // Before the first frame: let the runtime convert the counter into its own time base.
    if (m_runtimeInfo.win32TimeConversionEnabled && m_instance != XR_NULL_HANDLE) {
        LARGE_INTEGER counter;
        counter.QuadPart = now.counter;
        XrTime converted = 0;
        if (XR_SUCCEEDED(m_dispatch->ConvertWin32PerformanceCounterToTime(m_instance, &counter, &converted)) &&
            converted > 0) {
            return converted;
        }
    }

    // No runtime conversion: count up from the counter value at the first query, so the result
    // is monotonic and cannot overflow. It is not in the runtime's time base.
    constexpr XrTime kFallbackBaseTime = 1000000000LL;  // 1 s, keeps the time positive
    if (!m_hasFallbackAnchor) {
        m_hasFallbackAnchor = true;
        m_fallbackAnchorCounter = now.counter;
    }
    XrTime fallback = kFallbackBaseTime + PerformanceCounterToNanoseconds(now.counter - m_fallbackAnchorCounter, now.frequency);
    if (fallback < m_lastFallbackTime) fallback = m_lastFallbackTime;
    m_lastFallbackTime = fallback;
    return fallback;
}

OpenXrManager::OpenXrManager(std::shared_ptr<IOpenXrDispatch> dispatch)
    : m_dispatch(std::move(dispatch)) {
}

OpenXrManager::~OpenXrManager() {
    // At termination the runtime DLL may already be detached and a GPU fence wait could hang:
    // leave the session and instance to the OS (see IsProcessTerminating).
    if (IsProcessTerminating()) return;
    Shutdown();
    m_copyFenceEvent.Close();
}

bool OpenXrManager::CheckD3D12ExtensionSupport() {
    uint32_t extensionCount = 0;
    XrResult res = m_dispatch->EnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    if (XR_FAILED(res) || extensionCount == 0) {
        LogWarn("Failed to enumerate OpenXR extensions or no extensions found.");
        return false;
    }

    std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES, nullptr});
    res = m_dispatch->EnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
    if (XR_FAILED(res)) {
        LogWarn("Failed to retrieve OpenXR extension properties.");
        return false;
    }

    bool d3d12Found = false;
    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0) {
            m_runtimeInfo.d3d12ExtensionSupported = true;
            d3d12Found = true;
        } else if (strcmp(ext.extensionName, XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME) == 0) {
            m_win32TimeConversionAvailable = true;
        }
    }

    if (d3d12Found) return true;

    LogWarn("OpenXR runtime does not support required extension: " + std::string(XR_KHR_D3D12_ENABLE_EXTENSION_NAME));
    return false;
}

bool OpenXrManager::Initialize() {
    LogInfo("Inspecting environment for active OpenXR VR runtime...");

    if (!CheckD3D12ExtensionSupport()) {
        LogInfo("Required OpenXR D3D12 extension unavailable. Entering Pass-Through Mode.");
        m_state = OpenXrState::PassThrough;
        return true;
    }

    std::vector<const char*> enabledExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    if (m_win32TimeConversionAvailable) {
        enabledExtensions.push_back(XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME);
    }

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = nullptr;
    createInfo.createFlags = 0;
    strncpy_s(createInfo.applicationInfo.applicationName, "FarmingSimulator2025 VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
    createInfo.applicationInfo.applicationVersion = 1;
    strncpy_s(createInfo.applicationInfo.engineName, "GIANTS Engine 10", XR_MAX_ENGINE_NAME_SIZE - 1);
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.enabledExtensionNames = enabledExtensions.data();

    XrResult res = m_dispatch->CreateInstance(&createInfo, &m_instance);
    if (XR_FAILED(res)) {
        std::stringstream ss;
        ss << "xrCreateInstance failed (result " << res << "). Silently entering Pass-Through Mode.";
        LogInfo(ss.str());
        m_state = OpenXrState::PassThrough;
        return true;
    }
    m_runtimeInfo.win32TimeConversionEnabled = m_win32TimeConversionAvailable;

    XrInstanceProperties instanceProps{XR_TYPE_INSTANCE_PROPERTIES};
    if ((XR_SUCCEEDED(m_dispatch->GetInstanceProperties(m_instance, &instanceProps)))) {
        m_runtimeInfo.runtimeName = instanceProps.runtimeName;
        m_runtimeInfo.runtimeVersionMajor = XR_VERSION_MAJOR(instanceProps.runtimeVersion);
        m_runtimeInfo.runtimeVersionMinor = XR_VERSION_MINOR(instanceProps.runtimeVersion);
        m_runtimeInfo.runtimeVersionPatch = XR_VERSION_PATCH(instanceProps.runtimeVersion);

        std::stringstream ss;
        ss << "OpenXR Runtime detected: " << m_runtimeInfo.runtimeName
           << " (v" << m_runtimeInfo.runtimeVersionMajor << "."
           << m_runtimeInfo.runtimeVersionMinor << "."
           << m_runtimeInfo.runtimeVersionPatch << ")";
        LogInfo(ss.str());
    }

    XrSystemGetInfo getInfo{XR_TYPE_SYSTEM_GET_INFO};
    getInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    getInfo.next = nullptr;
    getInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    res = m_dispatch->GetSystem(m_instance, &getInfo, &m_runtimeInfo.systemId);
    if (XR_FAILED(res)) {
        std::stringstream ss;
        ss << "OpenXR HMD system unavailable (result " << res << "). Silently entering Pass-Through Mode.";
        LogInfo(ss.str());

        m_dispatch->DestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
        m_state = OpenXrState::PassThrough;
        return true;
    }

    XrSystemProperties systemProps{XR_TYPE_SYSTEM_PROPERTIES};
    if ((XR_SUCCEEDED(m_dispatch->GetSystemProperties(m_instance, m_runtimeInfo.systemId, &systemProps)))) {
        m_runtimeInfo.systemName = systemProps.systemName;
        m_runtimeInfo.maxSwapchainImageWidth = systemProps.graphicsProperties.maxSwapchainImageWidth;
        m_runtimeInfo.maxSwapchainImageHeight = systemProps.graphicsProperties.maxSwapchainImageHeight;
        m_runtimeInfo.maxLayerCount = systemProps.graphicsProperties.maxLayerCount;
        m_runtimeInfo.orientationTracking = (systemProps.trackingProperties.orientationTracking != XR_FALSE);
        m_runtimeInfo.positionTracking = (systemProps.trackingProperties.positionTracking != XR_FALSE);

        std::stringstream ss;
        ss << "OpenXR HMD System acquired: " << m_runtimeInfo.systemName
           << " (System ID: " << m_runtimeInfo.systemId << ")\n"
           << "  - Capabilities: Max Swapchain " << m_runtimeInfo.maxSwapchainImageWidth
           << "x" << m_runtimeInfo.maxSwapchainImageHeight
           << ", Max Layers: " << m_runtimeInfo.maxLayerCount
           << ", Orientation Tracking: " << (m_runtimeInfo.orientationTracking ? "Supported" : "Unsupported")
           << ", Position Tracking: " << (m_runtimeInfo.positionTracking ? "Supported" : "Unsupported");
        LogInfo(ss.str());
    }

    uint32_t viewConfigCount = 0;
    res = m_dispatch->EnumerateViewConfigurationViews(
        m_instance, m_runtimeInfo.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewConfigCount, nullptr);
    if (XR_SUCCEEDED(res) && viewConfigCount > 0) {
        m_viewConfigViews.assign(viewConfigCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        res = m_dispatch->EnumerateViewConfigurationViews(
            m_instance, m_runtimeInfo.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            viewConfigCount, &viewConfigCount, m_viewConfigViews.data());
        if (XR_SUCCEEDED(res) && !m_viewConfigViews.empty()) {
            m_runtimeInfo.recommendedEyeWidth = m_viewConfigViews[0].recommendedImageRectWidth;
            m_runtimeInfo.recommendedEyeHeight = m_viewConfigViews[0].recommendedImageRectHeight;

            std::stringstream ss;
            ss << "OpenXR Recommended View Configuration: "
               << m_runtimeInfo.recommendedEyeWidth << "x" << m_runtimeInfo.recommendedEyeHeight;
            if (m_viewConfigViews.size() > 1) {
                ss << " (Right Eye: " << m_viewConfigViews[1].recommendedImageRectWidth
                   << "x" << m_viewConfigViews[1].recommendedImageRectHeight << ")";
            }
            LogInfo(ss.str());
        }
    }

    XrGraphicsRequirementsD3D12KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    res = m_dispatch->GetD3D12GraphicsRequirementsKHR(m_instance, m_runtimeInfo.systemId, &graphicsRequirements);
    if (XR_FAILED(res)) {
        std::stringstream ss;
        ss << "Failed to query OpenXR D3D12 graphics requirements (result " << res << "). Silently entering Pass-Through Mode.";
        LogInfo(ss.str());

        m_dispatch->DestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
        m_state = OpenXrState::PassThrough;
        return true;
    }

    m_runtimeInfo.minFeatureLevel = graphicsRequirements.minFeatureLevel;
    m_runtimeInfo.adapterLuid = graphicsRequirements.adapterLuid;

    std::stringstream ss;
    ss << "OpenXR D3D12 Graphics Requirements verified (Min Feature Level: 0x"
       << std::hex << m_runtimeInfo.minFeatureLevel << std::dec << ").";
    LogInfo(ss.str());

    m_state = OpenXrState::Active;
    LogInfo("OpenXR lifecycle successfully established in Active VR Mode.");
    return true;
}

void OpenXrManager::Shutdown() {
    DestroyEyeSwapchains();
    if (m_viewSpace != XR_NULL_HANDLE) {
        m_dispatch->DestroySpace(m_viewSpace);
        m_viewSpace = XR_NULL_HANDLE;
    }
    if (m_localSpace != XR_NULL_HANDLE) {
        m_dispatch->DestroySpace(m_localSpace);
        m_localSpace = XR_NULL_HANDLE;
    }
    if (m_baseLocalSpace != XR_NULL_HANDLE) {
        m_dispatch->DestroySpace(m_baseLocalSpace);
        m_baseLocalSpace = XR_NULL_HANDLE;
    }
    if (m_sessionRunning && m_session != XR_NULL_HANDLE) {
        LogInfo("Ending OpenXR session...");
        m_dispatch->EndSession(m_session);
        m_sessionRunning = false;
    }
    if (m_session != XR_NULL_HANDLE) {
        m_dispatch->DestroySession(m_session);
        m_session = XR_NULL_HANDLE;
    }
    if (m_instance != XR_NULL_HANDLE) {
        LogInfo("Shutting down OpenXR instance...");
        m_dispatch->DestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }
    m_state = OpenXrState::Uninitialized;
    m_sessionRunning = false;
    m_sessionState = XR_SESSION_STATE_UNKNOWN;
    m_beginSessionFailures = 0;
    m_frameBegun = false;
    ClearRenderPoses();
    m_eyeProjections[0] = {};
    m_eyeProjections[1] = {};
    m_viewConfigViews.clear();
    m_submittedQuadFrameCount = 0;
    m_hasCustomMenuQuadSize = false;
    m_menuQuadDistance = 1.5f;
    m_lastSubmittedQuadLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    m_lastPolledTime = 0;
    m_lastLocatedViewsTime = 0;
    m_device = nullptr;
    m_queue = nullptr;
}

bool OpenXrManager::CreateSession(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (m_state != OpenXrState::Active || m_instance == XR_NULL_HANDLE) {
        return false;
    }
    if (device) m_device = device;
    if (queue) m_queue = queue;

    if (m_session != XR_NULL_HANDLE) {
        return true;
    }

    XrGraphicsBindingD3D12KHR d3d12Binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
    d3d12Binding.device = m_device;
    d3d12Binding.queue = m_queue;

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = (m_device != nullptr) ? &d3d12Binding : nullptr;
    sessionInfo.systemId = m_runtimeInfo.systemId;

    XrResult res = m_dispatch->CreateSession(m_instance, &sessionInfo, &m_session);
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::CreateSession failed with result " + std::to_string(res));
        return false;
    }
    m_sessionState = XR_SESSION_STATE_IDLE;
    LogInfo("OpenXrManager: Session created successfully; waiting for the runtime to report READY.");
    return true;
}

void OpenXrManager::PollEvents() {
    if (m_instance == XR_NULL_HANDLE || m_state != OpenXrState::Active) {
        return;
    }

    // Bounded drain so a misbehaving runtime can never stall the present path.
    for (int i = 0; i < 64; ++i) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult res = m_dispatch->PollEvent(m_instance, &event);
        if (XR_FAILED(res)) {
            LogWarn("OpenXrManager::PollEvents: xrPollEvent failed (result " + std::to_string(res) + ")");
            if (res == XR_ERROR_INSTANCE_LOST) {
                ReleaseAndEnterPassThrough("instance lost");
            }
            return;
        }
        if (res == XR_EVENT_UNAVAILABLE) {
            break;
        }

        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            HandleSessionStateChanged(changed->state);
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            ReleaseAndEnterPassThrough("instance loss pending");
            break;
        default:
            break;
        }

        if (m_state != OpenXrState::Active) {
            return;
        }
    }

    // READY is delivered only once. If xrBeginSession failed then, keep retrying until it succeeds
    // or the runtime moves the session on.
    if (m_sessionState == XR_SESSION_STATE_READY && !m_sessionRunning) {
        BeginRunningSession();
    }
}

bool OpenXrManager::EnterPassThroughIfLost(XrResult res) {
    // A crashed or restarted runtime may fail frame calls with *_LOST without ever queueing a
    // LOSS_PENDING event; treat it the same way.
    if (res == XR_ERROR_SESSION_LOST) {
        ReleaseAndEnterPassThrough("session lost");
        return true;
    }
    if (res == XR_ERROR_INSTANCE_LOST) {
        ReleaseAndEnterPassThrough("instance lost");
        return true;
    }
    return false;
}

void OpenXrManager::HandleSessionStateChanged(XrSessionState state) {
    LogInfo("OpenXrManager: Session state " + std::to_string(m_sessionState) + " -> " + std::to_string(state));
    m_sessionState = state;

    switch (state) {
    case XR_SESSION_STATE_READY:
        BeginRunningSession();
        break;
    case XR_SESSION_STATE_STOPPING:
        EndRunningSession();
        break;
    case XR_SESSION_STATE_EXITING:
        ReleaseAndEnterPassThrough("session exiting");
        break;
    case XR_SESSION_STATE_LOSS_PENDING:
        ReleaseAndEnterPassThrough("session loss pending");
        break;
    default:
        break;
    }
}

void OpenXrManager::BeginRunningSession() {
    if (m_session == XR_NULL_HANDLE || m_sessionRunning) {
        return;
    }
    XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
    beginInfo.next = nullptr;
    beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    XrResult res = m_dispatch->BeginSession(m_session, &beginInfo);
    if (XR_FAILED(res)) {
        // Retried once per poll while READY; log the first failure and then periodically.
        if (m_beginSessionFailures++ % 300 == 0) {
            LogError("OpenXrManager: BeginSession failed (result " + std::to_string(res) + ", attempt " +
                     std::to_string(m_beginSessionFailures) + ")");
        }
        EnterPassThroughIfLost(res);
        return;
    }
    m_beginSessionFailures = 0;
    m_sessionRunning = true;
    LogInfo("OpenXrManager: Session begun successfully.");
}

void OpenXrManager::EndRunningSession() {
    if (m_session == XR_NULL_HANDLE || !m_sessionRunning) {
        return;
    }
    // Any frame still in flight is abandoned; the runtime discards it when the session ends.
    m_frameBegun = false;
    m_lastPolledTime = 0;
    m_lastLocatedViewsTime = 0;
    m_eyeSwapchains[0].hasCopiedFrame = false;
    m_eyeSwapchains[1].hasCopiedFrame = false;
    ClearRenderPoses();

    XrResult res = m_dispatch->EndSession(m_session);
    if (XR_FAILED(res)) {
        LogError("OpenXrManager: EndSession failed (result " + std::to_string(res) + ")");
    }
    m_sessionRunning = false;
    LogInfo("OpenXrManager: Session ended; VR frame submission paused until the runtime reports READY.");
}

void OpenXrManager::ReleaseAndEnterPassThrough(const char* reason) {
    LogInfo(std::string("OpenXrManager: Releasing OpenXR resources (") + reason + "). Continuing in Pass-Through Mode.");
    m_frameBegun = false;
    // Keeps Shutdown from calling xrEndSession, which is valid only once the runtime has moved the
    // session to STOPPING; xrDestroySession is valid in any state and ends it.
    m_sessionRunning = false;
    Shutdown();
    m_state = OpenXrState::PassThrough;
}

bool OpenXrManager::CreateTrackingSpaces() {
    if (m_session == XR_NULL_HANDLE) {
        return false;
    }

    if (m_baseLocalSpace == XR_NULL_HANDLE) {
        XrReferenceSpaceCreateInfo baseSpaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        baseSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        baseSpaceInfo.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        baseSpaceInfo.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};

        XrResult res = m_dispatch->CreateReferenceSpace(m_session, &baseSpaceInfo, &m_baseLocalSpace);
        if (XR_FAILED(res)) {
            LogError("OpenXrManager: Failed to create base local reference space (result " + std::to_string(res) + ")");
            return false;
        }
    }

    if (m_localSpace == XR_NULL_HANDLE) {
        XrReferenceSpaceCreateInfo localSpaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        localSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        localSpaceInfo.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        localSpaceInfo.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};

        XrResult res = m_dispatch->CreateReferenceSpace(m_session, &localSpaceInfo, &m_localSpace);
        if (XR_FAILED(res)) {
            LogError("OpenXrManager: Failed to create local reference space (result " + std::to_string(res) + ")");
            return false;
        }
    }

    if (m_viewSpace == XR_NULL_HANDLE) {
        XrReferenceSpaceCreateInfo viewSpaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        viewSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        viewSpaceInfo.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        viewSpaceInfo.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};

        XrResult res = m_dispatch->CreateReferenceSpace(m_session, &viewSpaceInfo, &m_viewSpace);
        if (XR_FAILED(res)) {
            LogError("OpenXrManager: Failed to create view reference space (result " + std::to_string(res) + ")");
            return false;
        }
    }

    LogInfo("OpenXrManager: BaseLocal, Local, and View tracking reference spaces established.");
    return true;
}

EulerAngles OpenXrManager::QuaternionToEuler(const Quaternion& q) {
    return fs25vr::QuaternionToEuler(q);
}

Vector3 OpenXrManager::RotateVectorByQuaternion(const Quaternion& q, const Vector3& v) {
    return fs25vr::RotateVectorByQuaternion(q, v);
}

Vector3 OpenXrManager::ComputeEyePosition(const HeadPose& headPose, float ipd, uint32_t eyeIndex) {
    float eyeSign = (eyeIndex == 1) ? 1.0f : -1.0f;
    float halfIpd = ipd * 0.5f;
    Vector3 rotated = RotateVectorByQuaternion(
        headPose.orientation,
        Vector3(eyeSign * halfIpd, 0.0f, 0.0f)
    );
    return Vector3(
        headPose.position.x + rotated.x,
        headPose.position.y + rotated.y,
        headPose.position.z + rotated.z
    );
}

HeadPose OpenXrManager::ComputeEyePose(const HeadPose& headPose, float ipd, uint32_t eyeIndex) {
    HeadPose eyePose = headPose;
    if (headPose.isValid) {
        eyePose.position = ComputeEyePosition(headPose, ipd, eyeIndex);
    }
    return eyePose;
}

HeadPose OpenXrManager::PollHeadPose(XrTime time) {
    if (m_session == XR_NULL_HANDLE || m_localSpace == XR_NULL_HANDLE || m_viewSpace == XR_NULL_HANDLE) {
        return m_lastPose;
    }

    if (time == 0) {
        if (m_frameBegun && m_frameState.predictedDisplayTime != 0) {
            time = m_frameState.predictedDisplayTime;
        } else {
            time = GetCurrentXrTime();
        }
    }

    if (time != 0 && time == m_lastPolledTime && m_lastPose.isValid) {
        return m_lastPose;
    }

    if (m_baseLocalSpace != XR_NULL_HANDLE) {
        XrSpaceLocation baseLocation{XR_TYPE_SPACE_LOCATION};
        XrResult resBase = m_dispatch->LocateSpace(m_viewSpace, m_baseLocalSpace, time, &baseLocation);
        if (XR_SUCCEEDED(resBase)) {
            bool posValid = (baseLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
            bool oriValid = (baseLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

            m_rawPose.position = Vector3(baseLocation.pose.position.x, baseLocation.pose.position.y, baseLocation.pose.position.z);
            m_rawPose.orientation = Quaternion(baseLocation.pose.orientation.x, baseLocation.pose.orientation.y, baseLocation.pose.orientation.z, baseLocation.pose.orientation.w);
            m_rawPose.rotation = QuaternionToEuler(m_rawPose.orientation);

            m_rawPose.positionTracked = (baseLocation.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
            m_rawPose.orientationTracked = (baseLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
            m_rawPose.isValid = posValid && oriValid;
        }
    }

    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    XrResult res = m_dispatch->LocateSpace(m_viewSpace, m_localSpace, time, &location);
    if (XR_FAILED(res)) {
        static uint32_t s_locateFailCount = 0;
        if (++s_locateFailCount % 300 == 1) {
            LogWarn("OpenXrManager::PollHeadPose: LocateSpace failed (result " + std::to_string(res) + ")");
        }
        return m_lastPose;
    }

    bool posValid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
    bool oriValid = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

    m_lastPose.position = Vector3(location.pose.position.x, location.pose.position.y, location.pose.position.z);
    m_lastPose.orientation = Quaternion(location.pose.orientation.x, location.pose.orientation.y, location.pose.orientation.z, location.pose.orientation.w);
    m_lastPose.rotation = QuaternionToEuler(m_lastPose.orientation);

    m_lastPose.positionTracked = (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    m_lastPose.orientationTracked = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
    m_lastPose.isValid = posValid && oriValid;

    if (m_baseLocalSpace == XR_NULL_HANDLE) {
        m_rawPose = m_lastPose;
    }

    m_lastPolledTime = time;
    return m_lastPose;
}

void OpenXrManager::Recenter() {
    ClearRenderPoses();
    Vector3 recenterPos = m_rawPose.position;
    float recenterYaw = m_rawPose.rotation.yaw;  // every poll derives rotation from the quaternion

    if (m_session != XR_NULL_HANDLE && m_baseLocalSpace != XR_NULL_HANDLE && m_viewSpace != XR_NULL_HANDLE) {
        XrTime time = (m_frameState.predictedDisplayTime != 0) ? m_frameState.predictedDisplayTime : GetCurrentXrTime();
        XrSpaceLocation baseLoc{XR_TYPE_SPACE_LOCATION};
        XrResult res = m_dispatch->LocateSpace(m_viewSpace, m_baseLocalSpace, time, &baseLoc);
        if (XR_SUCCEEDED(res) &&
            (baseLoc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
            (baseLoc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            recenterPos = Vector3(baseLoc.pose.position.x, baseLoc.pose.position.y, baseLoc.pose.position.z);
            Quaternion q(baseLoc.pose.orientation.x, baseLoc.pose.orientation.y, baseLoc.pose.orientation.z, baseLoc.pose.orientation.w);
            EulerAngles euler = QuaternionToEuler(q);
            recenterYaw = euler.yaw;

            m_rawPose.position = recenterPos;
            m_rawPose.orientation = q;
            m_rawPose.rotation = euler;
            m_rawPose.positionTracked = (baseLoc.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
            m_rawPose.orientationTracked = (baseLoc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
            m_rawPose.isValid = true;
        }
    }

    if (m_session != XR_NULL_HANDLE) {
        if (m_localSpace != XR_NULL_HANDLE) {
            m_dispatch->DestroySpace(m_localSpace);
            m_localSpace = XR_NULL_HANDLE;
        }

        XrReferenceSpaceCreateInfo localSpaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        localSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        float halfYaw = recenterYaw * 0.5f;
        localSpaceInfo.poseInReferenceSpace.orientation = {0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw)};
        localSpaceInfo.poseInReferenceSpace.position = {recenterPos.x, recenterPos.y, recenterPos.z};

        XrResult res = m_dispatch->CreateReferenceSpace(m_session, &localSpaceInfo, &m_localSpace);
        if (XR_FAILED(res)) {
            LogError("OpenXrManager::Recenter: Failed to recreate local reference space (result " + std::to_string(res) + ")");
        }
    }

    m_lastPolledTime = 0;
    AlignMenuQuadToHeadPose();
    LogInfo("OpenXrManager: Head pose recentered.");
}

EyeProjection OpenXrManager::ComputeProjectionFromFov(const XrFovf& fov) {
    EyeProjection proj;
    proj.angleLeft = fov.angleLeft;
    proj.angleRight = fov.angleRight;
    proj.angleUp = fov.angleUp;
    proj.angleDown = fov.angleDown;

    proj.tanLeft = std::tan(fov.angleLeft);
    proj.tanRight = std::tan(fov.angleRight);
    proj.tanUp = std::tan(fov.angleUp);
    proj.tanDown = std::tan(fov.angleDown);

    proj.xOffset = proj.tanRight + proj.tanLeft;
    proj.yOffset = proj.tanUp + proj.tanDown;

    float tanWidth = proj.tanRight - proj.tanLeft;
    float tanHeight = proj.tanUp - proj.tanDown;

    if (std::abs(tanWidth) > 1e-6f) {
        proj.xOffsetNorm = proj.xOffset / tanWidth;
    }
    if (std::abs(tanHeight) > 1e-6f) {
        proj.yOffsetNorm = proj.yOffset / tanHeight;
    }

    proj.fovY = 2.0f * std::atan(tanHeight * 0.5f);
    proj.fovX = 2.0f * std::atan(tanWidth * 0.5f);

    if (std::abs(tanHeight) > 1e-6f) {
        proj.aspectRatio = tanWidth / tanHeight;
    }

    proj.isValid = true;
    return proj;
}

bool OpenXrManager::LocateActiveViews(XrTime time) {
    if (m_session == XR_NULL_HANDLE) {
        return false;
    }

    if (time == 0) {
        if (m_frameState.predictedDisplayTime != 0) {
            time = m_frameState.predictedDisplayTime;
        } else {
            time = GetCurrentXrTime();
        }
    }

    if (time != 0 && time == m_lastLocatedViewsTime && m_eyeProjections[0].isValid) {
        return true;
    }

    XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = time;
    locateInfo.space = (m_localSpace != XR_NULL_HANDLE) ? m_localSpace : m_viewSpace;

    XrViewState viewState{XR_TYPE_VIEW_STATE};
    uint32_t viewCount = 0;
    XrView views[2] = { {XR_TYPE_VIEW, nullptr}, {XR_TYPE_VIEW, nullptr} };

    XrResult res = m_dispatch->LocateViews(m_session, &locateInfo, &viewState, 2, &viewCount, views);
    if (XR_SUCCEEDED(res) && viewCount >= 2) {
        m_eyeProjections[0] = ComputeProjectionFromFov(views[0].fov);
        m_eyeProjections[1] = ComputeProjectionFromFov(views[1].fov);
        float dx = views[1].pose.position.x - views[0].pose.position.x;
        float dy = views[1].pose.position.y - views[0].pose.position.y;
        float dz = views[1].pose.position.z - views[0].pose.position.z;
        float measuredIpd = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (measuredIpd > 0.04f && measuredIpd < 0.10f) {
            m_ipd = measuredIpd;
        }

        static float s_lastLoggedIpd = 0.0f;
        if (std::abs(m_ipd - s_lastLoggedIpd) > 0.002f) {
            s_lastLoggedIpd = m_ipd;
            std::stringstream ss;
            ss << "OpenXrManager: Calibrated hardware IPD = " << (m_ipd * 1000.0f) << " mm"
               << " (View0 pos: " << views[0].pose.position.x << ", " << views[0].pose.position.y << ", " << views[0].pose.position.z << ")"
               << " (View1 pos: " << views[1].pose.position.x << ", " << views[1].pose.position.y << ", " << views[1].pose.position.z << ")";
            LogInfo(ss.str());
        }
        m_lastLocatedViewsTime = time;
        return true;
    }
    return false;
}

EyeProjection OpenXrManager::PollEyeProjection(uint32_t eyeIndex, XrTime time) {
    if (eyeIndex >= 2) {
        eyeIndex = 0;
    }

    if (m_state != OpenXrState::Active || m_session == XR_NULL_HANDLE) {
        return m_eyeProjections[eyeIndex];
    }

    LocateActiveViews(time);
    return m_eyeProjections[eyeIndex];
}

const EyeProjection& OpenXrManager::GetEyeProjection(uint32_t eyeIndex) const {
    if (eyeIndex >= 2) eyeIndex = 0;
    return m_eyeProjections[eyeIndex];
}

uint32_t OpenXrManager::GetRecommendedEyeWidth(uint32_t eyeIndex) const {
    if (eyeIndex < m_viewConfigViews.size() && m_viewConfigViews[eyeIndex].recommendedImageRectWidth > 0) {
        return m_viewConfigViews[eyeIndex].recommendedImageRectWidth;
    }
    return m_runtimeInfo.recommendedEyeWidth;
}

uint32_t OpenXrManager::GetRecommendedEyeHeight(uint32_t eyeIndex) const {
    if (eyeIndex < m_viewConfigViews.size() && m_viewConfigViews[eyeIndex].recommendedImageRectHeight > 0) {
        return m_viewConfigViews[eyeIndex].recommendedImageRectHeight;
    }
    return m_runtimeInfo.recommendedEyeHeight;
}

void OpenXrManager::SetD3D12DeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (device) m_device = device;
    if (queue) m_queue = queue;
    if (m_session == XR_NULL_HANDLE && m_state == OpenXrState::Active && m_device && m_queue) {
        CreateSession(m_device, m_queue);
        CreateTrackingSpaces();
    }
}

DXGI_FORMAT OpenXrManager::SelectCompatibleSwapchainFormat(DXGI_FORMAT backBufferFormat, const std::vector<int64_t>& supported) {
    // Candidates in preference order: exact match, then same-family variants.
    DXGI_FORMAT candidates[3] = { backBufferFormat, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
    switch (backBufferFormat) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:      candidates[1] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; break;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: candidates[1] = DXGI_FORMAT_R8G8B8A8_UNORM; break;
    case DXGI_FORMAT_B8G8R8A8_UNORM:      candidates[1] = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; break;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: candidates[1] = DXGI_FORMAT_B8G8R8A8_UNORM; break;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        candidates[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        candidates[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        candidates[0] = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        candidates[1] = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    default: break;
    }
    for (DXGI_FORMAT candidate : candidates) {
        if (candidate == DXGI_FORMAT_UNKNOWN) continue;
        for (int64_t sf : supported) {
            if (sf == static_cast<int64_t>(candidate)) return candidate;
        }
    }
    return DXGI_FORMAT_UNKNOWN;
}

DXGI_FORMAT OpenXrManager::NegotiateEyeSwapchainFormat(DXGI_FORMAT backBufferFormat) {
    // The eye image is filled by a straight copy from the back buffer, so the negotiated
    // format must be copy-compatible with it. Never fall back to an incompatible format.
    std::vector<int64_t> supportedFormats;
    uint32_t formatCount = 0;
    XrResult res = m_dispatch->EnumerateSwapchainFormats(m_session, 0, &formatCount, nullptr);
    if (XR_SUCCEEDED(res) && formatCount > 0) {
        supportedFormats.resize(formatCount);
        res = m_dispatch->EnumerateSwapchainFormats(m_session, formatCount, &formatCount, supportedFormats.data());
        supportedFormats.resize(XR_SUCCEEDED(res) ? formatCount : 0);
    }
    if (supportedFormats.empty()) {
        // A failed query says nothing about compatibility; let swapchain creation decide.
        LogWarn("OpenXrManager: Could not enumerate swapchain formats (result " + std::to_string(res) +
                "); requesting back buffer format " + std::to_string(backBufferFormat) + " unchanged.");
        return backBufferFormat;
    }
    return SelectCompatibleSwapchainFormat(backBufferFormat, supportedFormats);
}

void OpenXrManager::EnterPassThroughForIncompatibleFormat(DXGI_FORMAT backBufferFormat) {
    LogError("OpenXrManager: OpenXR runtime offers no swapchain format compatible with back buffer format " +
             std::to_string(backBufferFormat) + ".");
    // Pass-Through Mode means no OpenXR session, so destroy the session and release everything
    // rather than leave a begun session with no frames ever submitted.
    ReleaseAndEnterPassThrough("no copy-compatible swapchain format");
}

bool OpenXrManager::EnsureBackBufferFormatSupported(DXGI_FORMAT backBufferFormat) {
    if (m_session == XR_NULL_HANDLE || m_state != OpenXrState::Active) {
        return m_state == OpenXrState::Active;
    }
    if (NegotiateEyeSwapchainFormat(backBufferFormat) == DXGI_FORMAT_UNKNOWN) {
        EnterPassThroughForIncompatibleFormat(backBufferFormat);
        return false;
    }
    return true;
}

bool OpenXrManager::CreateEyeSwapchains(uint32_t width, uint32_t height, DXGI_FORMAT format) {
    if (m_session == XR_NULL_HANDLE || m_state != OpenXrState::Active) {
        return false;
    }
    if (m_hasEyeSwapchains) {
        return true;
    }

    DXGI_FORMAT selectedFormat = NegotiateEyeSwapchainFormat(format);
    if (selectedFormat == DXGI_FORMAT_UNKNOWN) {
        EnterPassThroughForIncompatibleFormat(format);
        return false;
    }
    if (selectedFormat != format) {
        LogInfo("OpenXrManager: Format " + std::to_string(format) + " not directly supported. Negotiated swapchain format: " + std::to_string(selectedFormat));
    }

    for (uint32_t i = 0; i < 2; ++i) {
        uint32_t eyeWidth = (width != 0) ? width : GetRecommendedEyeWidth(i);
        uint32_t eyeHeight = (height != 0) ? height : GetRecommendedEyeHeight(i);
        if (eyeWidth == 0) eyeWidth = 1920;
        if (eyeHeight == 0) eyeHeight = 1080;

        XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        createInfo.format = selectedFormat;
        createInfo.sampleCount = 1;
        createInfo.width = eyeWidth;
        createInfo.height = eyeHeight;
        createInfo.faceCount = 1;
        createInfo.arraySize = 1;
        createInfo.mipCount = 1;

        XrResult res = m_dispatch->CreateSwapchain(m_session, &createInfo, &m_eyeSwapchains[i].handle);
        if (XR_FAILED(res)) {
            LogError("OpenXrManager::CreateEyeSwapchains failed for eye " + std::to_string(i) + " (result " + std::to_string(res) + ")");
            DestroyEyeSwapchains();
            return false;
        }

        EnumerateSwapchainImagesHelper(m_eyeSwapchains[i]);

        m_eyeSwapchains[i].width = eyeWidth;
        m_eyeSwapchains[i].height = eyeHeight;
        m_eyeSwapchains[i].format = selectedFormat;
        m_eyeSwapchains[i].lastCopiedIndex = 0;
        m_eyeSwapchains[i].hasCopiedFrame = false;
    }

    m_hasEyeSwapchains = true;
    LogInfo("OpenXrManager: Created stereoscopic eye swapchains (" + std::to_string(m_eyeSwapchains[0].width) + "x" + std::to_string(m_eyeSwapchains[0].height) + ").");
    return true;
}

void OpenXrManager::DestroyEyeSwapchains() {
    if (m_copyFence && m_queue && m_currentFenceValue > 0) {
        if (m_copyFence->GetCompletedValue() < m_currentFenceValue) {
            if (m_copyFenceEvent.IsValid()) {
                m_copyFence->SetEventOnCompletion(m_currentFenceValue, m_copyFenceEvent.Get());
                WaitForSingleObject(m_copyFenceEvent.Get(), INFINITE);
            }
        }
    }
    m_copyFence.Reset();
    m_currentFenceValue = 0;
    for (uint32_t r = 0; r < kCopyRingSize; ++r) {
        m_copyFenceValues[r] = 0;
        m_copyCommandLists[r].Reset();
        m_copyAllocators[r].Reset();
    }
    m_copyRingIndex = 0;
    m_copyFenceEvent.Close();
    for (uint32_t i = 0; i < 2; ++i) {
        if (m_eyeSwapchains[i].handle != XR_NULL_HANDLE) {
            m_dispatch->DestroySwapchain(m_eyeSwapchains[i].handle);
            m_eyeSwapchains[i].handle = XR_NULL_HANDLE;
        }
        m_eyeSwapchains[i].images.clear();
        m_eyeSwapchains[i].hasCopiedFrame = false;
    }
    DestroyCursorSwapchain();
    if (m_frameBegun) {
        EndFrame();
    }
    m_hasEyeSwapchains = false;
}

const EyeSwapchain& OpenXrManager::GetEyeSwapchain(uint32_t eyeIndex) const {
    if (eyeIndex >= 2) eyeIndex = 0;
    return m_eyeSwapchains[eyeIndex];
}

bool OpenXrManager::EnumerateSwapchainImagesHelper(EyeSwapchain& sc) {
    if (sc.handle == XR_NULL_HANDLE) {
        return false;
    }
    uint32_t imageCount = 0;
    m_dispatch->EnumerateSwapchainImages(sc.handle, 0, &imageCount, nullptr);
    if (imageCount > 0) {
        sc.images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        m_dispatch->EnumerateSwapchainImages(
            sc.handle,
            imageCount,
            &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data()));
    }
    return true;
}

bool OpenXrManager::EnsureCursorTextureUploaded() {
    return m_cursorOverlay.UploadTexture(m_device, m_queue, m_dispatch);
}

bool OpenXrManager::CreateCursorSwapchain(uint32_t width, uint32_t height, DXGI_FORMAT format) {
    if (m_session == XR_NULL_HANDLE) {
        return false;
    }
    return m_cursorOverlay.CreateSwapchain(m_session, m_dispatch, width, height, format, m_device, m_queue);
}

void OpenXrManager::DestroyCursorSwapchain() {
    m_cursorOverlay.DestroySwapchain(m_dispatch);
}

void OpenXrManager::RecordRenderPose(uint32_t eyeIndex, const HeadPose& pose) {
    if (eyeIndex >= 2 || !pose.isValid) {
        return;
    }
    m_renderPoses[eyeIndex] = pose;
    m_hasRenderPose[eyeIndex] = true;
}

void OpenXrManager::ClearRenderPoses() {
    // A record not consumed by its frame's copy (shouldRender false, a skipped copy, a recenter)
    // belongs to an older frame or tracking space; never let a later frame submit it.
    m_hasRenderPose[0] = false;
    m_hasRenderPose[1] = false;
}

bool OpenXrManager::BeginFrame() {
    if (m_session == XR_NULL_HANDLE) {
        return false;
    }
    if (m_frameBegun) {
        return true;
    }

    PollEvents();
    if (m_session == XR_NULL_HANDLE || !m_sessionRunning) {
        return false;
    }

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    m_frameState = {XR_TYPE_FRAME_STATE};
    m_frameState.shouldRender = XR_TRUE;
    XrResult res = m_dispatch->WaitFrame(m_session, &waitInfo, &m_frameState);
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::BeginFrame: WaitFrame failed (result " + std::to_string(res) + ")");
        EnterPassThroughIfLost(res);
        return false;
    }

    if (m_frameState.predictedDisplayTime != 0) {
        m_lastPredictedDisplayTime = m_frameState.predictedDisplayTime;
        m_lastPredictedQpc = ReadPerformanceCounter().counter;
    }

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    res = m_dispatch->BeginFrame(m_session, &beginInfo);
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::BeginFrame: BeginFrame failed (result " + std::to_string(res) + ")");
        EnterPassThroughIfLost(res);
        return false;
    }

    m_frameBegun = true;

    // OpenXR specification: actively locate views and update projections and head pose
    // for predictedDisplayTime within the active frame lifecycle boundary.
    if (m_frameState.shouldRender) {
        LocateActiveViews(m_frameState.predictedDisplayTime);
        PollHeadPose(m_frameState.predictedDisplayTime);
    }

    return true;
}

bool OpenXrManager::EndFrame() {
    if (m_session == XR_NULL_HANDLE || !m_frameBegun) {
        return false;
    }

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = m_frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 0;
    endInfo.layers = nullptr;

    XrResult res = m_dispatch->EndFrame(m_session, &endInfo);
    m_frameBegun = false;
    ClearRenderPoses();
    m_lastPolledTime = 0;
    m_lastLocatedViewsTime = 0;
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::EndFrame: EndFrame failed (result " + std::to_string(res) + ")");
        EnterPassThroughIfLost(res);
        return false;
    }
    return true;
}

bool OpenXrManager::CopyBackBufferToEye(uint32_t eyeIndex, ID3D12Resource* backBuffer) {
    if (eyeIndex >= 2 || !m_hasEyeSwapchains) {
        return false;
    }

    // Enforce active frame lifecycle: swapchain image acquisition and release
    // must occur strictly within active frame begin and frame end boundaries.
    if (!m_frameBegun) {
        if (!BeginFrame()) {
            return false;
        }
    }

    // OpenXR specification: suppress swapchain image acquisition and rendering passes
    // when runtime indicates shouldRender is false.
    if (!m_frameState.shouldRender) {
        m_eyeSwapchains[eyeIndex].hasCopiedFrame = true;
        return true;
    }

    // Submit the eye with the pose the engine rendered it with (recorded when VRBridge handed
    // it to the Companion Mod). Fall back to the pose polled at frame begin if none was recorded.
    if (m_hasRenderPose[eyeIndex]) {
        m_capturedEyePoses[eyeIndex] = m_renderPoses[eyeIndex];
        m_hasRenderPose[eyeIndex] = false;
    } else {
        m_capturedEyePoses[eyeIndex] = m_lastPose;
    }

    XrSwapchain swapchain = m_eyeSwapchains[eyeIndex].handle;
    if (swapchain == XR_NULL_HANDLE) {
        return false;
    }

    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t imageIndex = 0;
    XrResult res = m_dispatch->AcquireSwapchainImage(swapchain, &acquireInfo, &imageIndex);
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::CopyBackBufferToEye: AcquireSwapchainImage failed (result " + std::to_string(res) + ")");
        return false;
    }

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    res = m_dispatch->WaitSwapchainImage(swapchain, &waitInfo);
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::CopyBackBufferToEye: WaitSwapchainImage failed (result " + std::to_string(res) + ")");
        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        m_dispatch->ReleaseSwapchainImage(swapchain, &releaseInfo);
        return false;
    }

    // If backBuffer, device, and queue are present, perform D3D12 command list recording & execution
    if (backBuffer && m_device && m_queue) {
        if (!m_copyFence) {
            m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_copyFence));
            m_currentFenceValue = 0;
            for (uint32_t r = 0; r < kCopyRingSize; ++r) {
                m_copyFenceValues[r] = 0;
            }
            if (!m_copyFenceEvent.IsValid()) {
                m_copyFenceEvent = ScopedEventHandle::Create(nullptr, FALSE, FALSE, nullptr);
            }
        }

        uint32_t ringSlot = m_copyRingIndex;
        m_copyRingIndex = (m_copyRingIndex + 1) % kCopyRingSize;

        if (!m_copyAllocators[ringSlot]) {
            m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_copyAllocators[ringSlot]));
        }
        if (!m_copyCommandLists[ringSlot] && m_copyAllocators[ringSlot]) {
            m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_copyAllocators[ringSlot].Get(), nullptr, IID_PPV_ARGS(&m_copyCommandLists[ringSlot]));
        }

        auto& curAllocator = m_copyAllocators[ringSlot];
        auto& curCommandList = m_copyCommandLists[ringSlot];

        if (curCommandList && curAllocator) {
            // Synchronize with previous execution on this specific ring slot if GPU has not finished it
            uint64_t waitVal = m_copyFenceValues[ringSlot];
            if (m_copyFence && waitVal > 0) {
                if (m_copyFence->GetCompletedValue() < waitVal) {
                    if (m_copyFenceEvent.IsValid()) {
                        m_copyFence->SetEventOnCompletion(waitVal, m_copyFenceEvent.Get());
                        WaitForSingleObject(m_copyFenceEvent.Get(), INFINITE);
                    }
                }
            }

            curAllocator->Reset();
            curCommandList->Reset(curAllocator.Get(), nullptr);

            ID3D12Resource* destTexture = nullptr;
            if (imageIndex < m_eyeSwapchains[eyeIndex].images.size()) {
                destTexture = m_eyeSwapchains[eyeIndex].images[imageIndex].texture;
            }

            if (destTexture) {
                D3D12_RESOURCE_BARRIER barriers[2]{};
                barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[0].Transition.pResource = backBuffer;
                barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

                barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[1].Transition.pResource = destTexture;
                // OpenXR D3D12: acquired color swapchain images are in RENDER_TARGET and must be
                // returned to RENDER_TARGET before release.
                barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

                curCommandList->ResourceBarrier(2, barriers);

                uint32_t scWidth = m_eyeSwapchains[eyeIndex].width;
                uint32_t scHeight = m_eyeSwapchains[eyeIndex].height;

                D3D12_RESOURCE_DESC srcDesc = backBuffer->GetDesc();
                if (srcDesc.Width == scWidth && srcDesc.Height == scHeight) {
                    curCommandList->CopyResource(destTexture, backBuffer);
                } else {
                    D3D12_RESOURCE_DESC dstDesc = destTexture->GetDesc();
                    static bool loggedMismatch = false;
                    if (!loggedMismatch) {
                        LogWarn("OpenXrManager::CopyBackBufferToEye: Dimension mismatch! BackBuffer: " +
                                std::to_string(srcDesc.Width) + "x" + std::to_string(srcDesc.Height) +
                                ", OpenXR swapchain: " + std::to_string(dstDesc.Width) + "x" + std::to_string(dstDesc.Height));
                        loggedMismatch = true;
                    }
                    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
                    dstLoc.pResource = destTexture;
                    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    dstLoc.SubresourceIndex = 0;

                    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
                    srcLoc.pResource = backBuffer;
                    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    srcLoc.SubresourceIndex = 0;

                    D3D12_BOX box{};
                    box.left = 0;
                    box.top = 0;
                    box.front = 0;
                    box.right = static_cast<UINT>(std::min(srcDesc.Width, static_cast<UINT64>(dstDesc.Width)));
                    box.bottom = static_cast<UINT>(std::min(srcDesc.Height, dstDesc.Height));
                    box.back = 1;

                    curCommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &box);
                }

                std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
                std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
                curCommandList->ResourceBarrier(2, barriers);

                curCommandList->Close();
                ID3D12CommandList* lists[] = { curCommandList.Get() };
                m_queue->ExecuteCommandLists(1, lists);

                if (m_copyFence) {
                    m_currentFenceValue++;
                    m_copyFenceValues[ringSlot] = m_currentFenceValue;
                    m_queue->Signal(m_copyFence.Get(), m_currentFenceValue);
                }
            }
        }
    }

    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    res = m_dispatch->ReleaseSwapchainImage(swapchain, &releaseInfo);
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::CopyBackBufferToEye: ReleaseSwapchainImage failed (result " + std::to_string(res) + ")");
        return false;
    }

    m_eyeSwapchains[eyeIndex].lastCopiedIndex = imageIndex;
    m_eyeSwapchains[eyeIndex].hasCopiedFrame = true;
    return true;
}

bool OpenXrManager::SubmitFramePair() {
    if (!m_hasEyeSwapchains || m_session == XR_NULL_HANDLE) {
        return false;
    }

    // Verified pair completion: enforce both eyes have copied a frame before submitting
    if (!m_eyeSwapchains[0].hasCopiedFrame || !m_eyeSwapchains[1].hasCopiedFrame) {
        return false;
    }

    if (!m_frameBegun) {
        LogError("OpenXrManager::SubmitFramePair: No frame currently begun.");
        return false;
    }

    XrCompositionLayerProjectionView projViews[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        projViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        projViews[i].next = nullptr;

        // Compute aspect-ratio matched FOV for the eye swapchain:
        // GIANTS Engine renders using vertical FOV (fovY) and scales horizontal FOV by backbuffer aspect ratio (W / H).
        // To preserve exact 1:1 square pixel geometry and eliminate horizontal squashing and cross-eyed distortion:
        float halfFovY = m_eyeProjections[i].fovY * 0.5f;
        if (halfFovY <= 0.01f) {
            halfFovY = 0.785398f; // safe ~90 deg fallback
        }
        float tanHalfY = std::tan(halfFovY);
        float aspect = (m_eyeSwapchains[i].height > 0)
            ? static_cast<float>(m_eyeSwapchains[i].width) / static_cast<float>(m_eyeSwapchains[i].height)
            : 1.0f;
        float tanHalfX = aspect * tanHalfY;
        float halfFovX = std::atan(tanHalfX);

        // Aspect-ratio matched symmetric FOV prevents horizontal squashing and aligns optical centers
        projViews[i].fov.angleLeft = -halfFovX;
        projViews[i].fov.angleRight = halfFovX;
        projViews[i].fov.angleUp = halfFovY;
        projViews[i].fov.angleDown = -halfFovY;

        const HeadPose& eyePose = m_capturedEyePoses[i];
        Vector3 eyePos = ComputeEyePosition(eyePose, m_ipd, i);

        projViews[i].pose.position.x = eyePos.x;
        projViews[i].pose.position.y = eyePos.y;
        projViews[i].pose.position.z = eyePos.z;

        // Orientation fallback: ensure quaternion is normalized and never degenerate (length 0)
        float qMagSq = eyePose.orientation.x * eyePose.orientation.x +
                       eyePose.orientation.y * eyePose.orientation.y +
                       eyePose.orientation.z * eyePose.orientation.z +
                       eyePose.orientation.w * eyePose.orientation.w;
        if (qMagSq < 1e-4f) {
            projViews[i].pose.orientation.x = 0.0f;
            projViews[i].pose.orientation.y = 0.0f;
            projViews[i].pose.orientation.z = 0.0f;
            projViews[i].pose.orientation.w = 1.0f;
        } else {
            float invMag = 1.0f / std::sqrt(qMagSq);
            projViews[i].pose.orientation.x = eyePose.orientation.x * invMag;
            projViews[i].pose.orientation.y = eyePose.orientation.y * invMag;
            projViews[i].pose.orientation.z = eyePose.orientation.z * invMag;
            projViews[i].pose.orientation.w = eyePose.orientation.w * invMag;
        }

        projViews[i].subImage.swapchain = m_eyeSwapchains[i].handle;
        projViews[i].subImage.imageRect.offset = { 0, 0 };
        projViews[i].subImage.imageRect.extent = {
            static_cast<int32_t>(m_eyeSwapchains[i].width),
            static_cast<int32_t>(m_eyeSwapchains[i].height)
        };
        projViews[i].subImage.imageArrayIndex = 0;
    }

    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.layerFlags = 0;
    layer.space = (m_localSpace != XR_NULL_HANDLE) ? m_localSpace : m_viewSpace;
    layer.viewCount = 2;
    layer.views = projViews;

    const XrCompositionLayerBaseHeader* layers[] = { reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer) };

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = m_frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = m_frameState.shouldRender ? 1 : 0;
    endInfo.layers = m_frameState.shouldRender ? layers : nullptr;

    XrResult res = m_dispatch->EndFrame(m_session, &endInfo);
    m_frameBegun = false;
    ClearRenderPoses();
    m_lastPolledTime = 0;
    m_lastLocatedViewsTime = 0;
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::SubmitFramePair: EndFrame failed (result " + std::to_string(res) + ")");
        EnterPassThroughIfLost(res);
        return false;
    }

    m_submittedFramePairCount++;
    m_eyeSwapchains[0].hasCopiedFrame = false;
    m_eyeSwapchains[1].hasCopiedFrame = false;

    if (m_submittedFramePairCount == 1 || m_submittedFramePairCount % 300 == 0) {
        std::stringstream ss;
        ss << "OpenXrManager: Submitted stereo pair #" << m_submittedFramePairCount
           << " - Eye0 FOV: [" << projViews[0].fov.angleLeft << ", " << projViews[0].fov.angleRight
           << ", " << projViews[0].fov.angleUp << ", " << projViews[0].fov.angleDown << "]"
           << " Eye0 Pose: (" << projViews[0].pose.position.x << ", " << projViews[0].pose.position.y << ", " << projViews[0].pose.position.z << ")"
           << " Ori: (" << projViews[0].pose.orientation.x << ", " << projViews[0].pose.orientation.y
           << ", " << projViews[0].pose.orientation.z << ", " << projViews[0].pose.orientation.w << ")";
        LogInfo(ss.str());
    }
    return true;
}

XrPosef OpenXrManager::ComputeMenuQuadPoseFromHeadPose(const HeadPose& headPose, float distance) {
    float yaw = headPose.rotation.yaw;
    // Fallback: if yaw Euler is 0 but orientation quaternion is non-identity, extract yaw from quaternion
    if (std::abs(yaw) < 1e-6f && (std::abs(headPose.orientation.x) > 1e-6f ||
                                 std::abs(headPose.orientation.y) > 1e-6f ||
                                 std::abs(headPose.orientation.z) > 1e-6f ||
                                 std::abs(headPose.orientation.w - 1.0f) > 1e-6f)) {
        yaw = QuaternionToEuler(headPose.orientation).yaw;
    }

    float halfYaw = yaw * 0.5f;
    Quaternion qYaw(0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw));

    Vector3 forwardOffset(0.0f, 0.0f, -distance);
    Vector3 rotatedOffset = RotateVectorByQuaternion(qYaw, forwardOffset);

    XrPosef pose{};
    pose.position.x = headPose.position.x + rotatedOffset.x;
    pose.position.y = headPose.position.y; // Decouple pitch/roll: virtual screen remains at eye level
    pose.position.z = headPose.position.z + rotatedOffset.z;

    pose.orientation.x = qYaw.x;
    pose.orientation.y = qYaw.y;
    pose.orientation.z = qYaw.z;
    pose.orientation.w = qYaw.w;

    return pose;
}

void OpenXrManager::AlignMenuQuadToHeadPose(const HeadPose* customPose) {
    const HeadPose& pose = customPose ? *customPose : m_rawPose;
    m_menuQuadPose = ComputeMenuQuadPoseFromHeadPose(pose, m_menuQuadDistance);
    m_hasMenuQuadPose = true;
}

XrPosef OpenXrManager::ComputeMenuQuadPose() const {
    if (m_hasMenuQuadPose) {
        return m_menuQuadPose;
    }
    XrPosef pose{};
    pose.position.x = 0.0f;
    pose.position.y = 0.0f;
    pose.position.z = -m_menuQuadDistance;
    pose.orientation.w = 1.0f;
    return pose;
}

XrExtent2Df OpenXrManager::ComputeMenuQuadSize() const {
    if (m_hasCustomMenuQuadSize) {
        return m_customMenuQuadSize;
    }
    float height = 0.9f;
    float aspect = 16.0f / 9.0f;
    if (m_hasEyeSwapchains && m_eyeSwapchains[0].height > 0) {
        aspect = static_cast<float>(m_eyeSwapchains[0].width) / static_cast<float>(m_eyeSwapchains[0].height);
    }
    return XrExtent2Df{ height * aspect, height };
}

bool OpenXrManager::SubmitMenuQuad(const XrPosef* customPose, const XrExtent2Df* customSize) {
    if (!m_hasEyeSwapchains || m_session == XR_NULL_HANDLE) {
        return false;
    }

    if (!m_eyeSwapchains[0].hasCopiedFrame) {
        return false;
    }

    if (!m_frameBegun) {
        LogError("OpenXrManager::SubmitMenuQuad: No frame currently begun.");
        return false;
    }

    if (m_localSpace == XR_NULL_HANDLE) {
        LogError("OpenXrManager::SubmitMenuQuad: Local reference space is not initialized.");
        return false;
    }

    XrCompositionLayerQuad quadLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    quadLayer.next = nullptr;
    quadLayer.layerFlags = 0;
    quadLayer.space = m_localSpace;
    quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;

    quadLayer.subImage.swapchain = m_eyeSwapchains[0].handle;
    quadLayer.subImage.imageRect.offset = { 0, 0 };
    quadLayer.subImage.imageRect.extent = {
        static_cast<int32_t>(m_eyeSwapchains[0].width),
        static_cast<int32_t>(m_eyeSwapchains[0].height)
    };
    quadLayer.subImage.imageArrayIndex = 0;

    quadLayer.pose = customPose ? *customPose : ComputeMenuQuadPose();
    quadLayer.size = customSize ? *customSize : ComputeMenuQuadSize();

    m_lastSubmittedQuadLayer = quadLayer;

    XrCompositionLayerQuad cursorQuadLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    bool showCursor = m_cursorOverlay.PrepareCompositionLayer(
        quadLayer, m_menuCursorState, m_localSpace, cursorQuadLayer,
        m_device, m_queue, m_dispatch);

    const XrCompositionLayerBaseHeader* layers[2] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quadLayer),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cursorQuadLayer)
    };

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = m_frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = m_frameState.shouldRender ? (showCursor ? 2 : 1) : 0;
    endInfo.layers = m_frameState.shouldRender ? layers : nullptr;

    XrResult res = m_dispatch->EndFrame(m_session, &endInfo);
    m_frameBegun = false;
    ClearRenderPoses();
    m_lastPolledTime = 0;
    m_lastLocatedViewsTime = 0;
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::SubmitMenuQuad: EndFrame failed (result " + std::to_string(res) + ")");
        EnterPassThroughIfLost(res);
        return false;
    }

    m_submittedQuadFrameCount++;
    m_eyeSwapchains[0].hasCopiedFrame = false;
    m_eyeSwapchains[1].hasCopiedFrame = false;

    if (m_submittedQuadFrameCount == 1 || m_submittedQuadFrameCount % 300 == 0) {
        std::stringstream ss;
        ss << "OpenXrManager: Submitted floating menu quad #" << m_submittedQuadFrameCount
           << " - Size: [" << quadLayer.size.width << "x" << quadLayer.size.height << "]"
           << " Pose: (" << quadLayer.pose.position.x << ", " << quadLayer.pose.position.y << ", " << quadLayer.pose.position.z << ")"
           << " Ori: (" << quadLayer.pose.orientation.x << ", " << quadLayer.pose.orientation.y
           << ", " << quadLayer.pose.orientation.z << ", " << quadLayer.pose.orientation.w << ")";
        if (showCursor) {
            ss << " [Cursor: visible at (" << m_menuCursorState.normX << ", " << m_menuCursorState.normY << ")]";
        }
        LogInfo(ss.str());
    }
    return true;
}

XrPosef OpenXrManager::ComputeCursorQuadPose(const XrPosef& menuQuadPose, const XrExtent2Df& menuQuadSize,
                                            const MenuCursorState& cursorState, float cursorWidth, float cursorHeight) {
    return OpenXrCursorOverlay::ComputeCursorQuadPose(menuQuadPose, menuQuadSize, cursorState, cursorWidth, cursorHeight);
}

XrPosef OpenXrManager::ComputeCursorQuadPose(const XrPosef& menuQuadPose, const XrExtent2Df& menuQuadSize,
                                            float normX, float normY, float cursorWidth, float cursorHeight) {
    return OpenXrCursorOverlay::ComputeCursorQuadPose(menuQuadPose, menuQuadSize, normX, normY, cursorWidth, cursorHeight);
}

} // namespace fs25vr
