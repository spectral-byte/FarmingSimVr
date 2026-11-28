#include "openxr_manager.h"
#include "core/logger.h"
#include "core/dll_lifecycle.h"
#include "core/perf_trace.h"
#include <vector>
#include <cstring>
#include <sstream>
#include <cmath>
#include <algorithm>


namespace fs25vr {

namespace {
constexpr float kMinRenderedFovRadians = 0.02f;
}

void OpenXrManager::SetPerformanceCounterSource(PerformanceCounterSource source) {
    m_counterSource = std::move(source);
}

void OpenXrManager::SetFrameBeginObserver(const void* owner, FrameBeginObserver observer) {
    std::lock_guard<std::mutex> lock(m_frameBeginObserversMutex);
    auto edited = std::make_shared<FrameBeginObserverList>();
    for (const auto& entry : *m_frameBeginObservers) {
        if (entry.first != owner) edited->push_back(entry);
    }
    edited->emplace_back(owner, std::move(observer));
    m_frameBeginObservers = std::move(edited);
}

void OpenXrManager::ClearFrameBeginObserver(const void* owner) {
    std::lock_guard<std::mutex> lock(m_frameBeginObserversMutex);
    auto edited = std::make_shared<FrameBeginObserverList>();
    for (const auto& entry : *m_frameBeginObservers) {
        if (entry.first != owner) edited->push_back(entry);
    }
    m_frameBeginObservers = std::move(edited);
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
            return m_lastPredictedDisplayTime + QpcTicksToNanoseconds(elapsedQpc, now.frequency);
        }
        return m_lastPredictedDisplayTime;
    }

    if (m_runtimeInfo.win32TimeConversionEnabled && m_instance != XR_NULL_HANDLE) {
        LARGE_INTEGER counter;
        counter.QuadPart = now.counter;
        XrTime converted = 0;
        if (XR_SUCCEEDED(m_dispatch->ConvertWin32PerformanceCounterToTime(m_instance, &counter, &converted)) &&
            converted > 0) {
            return converted;
        }
    }

    return QpcTicksToNanoseconds(now.counter, now.frequency);
}

OpenXrManager::OpenXrManager(std::shared_ptr<IOpenXrDispatch> dispatch)
    : m_dispatch(std::move(dispatch)) {
    m_eyeCopy.SetSizeMismatchHandler([this](uint64_t srcWidth, uint32_t srcHeight, uint64_t dstWidth, uint32_t dstHeight) {
        LogEyeCopySizeMismatchOnce(srcWidth, srcHeight, dstWidth, dstHeight);
    });
}

OpenXrManager::~OpenXrManager() {
    if (IsProcessTerminating()) return;
    Shutdown();
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
        m_state.store(OpenXrState::PassThrough);
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
        m_state.store(OpenXrState::PassThrough);
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
        m_state.store(OpenXrState::PassThrough);
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
        m_state.store(OpenXrState::PassThrough);
        return true;
    }

    m_runtimeInfo.minFeatureLevel = graphicsRequirements.minFeatureLevel;
    m_runtimeInfo.adapterLuid = graphicsRequirements.adapterLuid;

    std::stringstream ss;
    ss << "OpenXR D3D12 Graphics Requirements verified (Min Feature Level: 0x"
       << std::hex << m_runtimeInfo.minFeatureLevel << std::dec << ").";
    LogInfo(ss.str());

    m_state.store(OpenXrState::Active);
    LogInfo("OpenXR lifecycle successfully established in Active VR Mode.");
    return true;
}

void OpenXrManager::Shutdown() {
    DestroyEyeSwapchains();
    m_input.Destroy(*m_dispatch);
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
    const XrSession session = m_session.load();
    if (m_sessionRunning && session != XR_NULL_HANDLE) {
        LogInfo("Ending OpenXR session...");
        m_dispatch->EndSession(session);
        m_sessionRunning = false;
    }
    if (session != XR_NULL_HANDLE) {
        m_dispatch->DestroySession(session);
        m_session.store(XR_NULL_HANDLE);
    }
    if (m_instance != XR_NULL_HANDLE) {
        LogInfo("Shutting down OpenXR instance...");
        m_dispatch->DestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }
    m_state.store(OpenXrState::Uninitialized);
    m_runtimeInfo = {};
    m_sessionRunning = false;
    m_sessionState = XR_SESSION_STATE_UNKNOWN;
    m_beginSessionFailures = 0;
    m_frameBegun = false;
    m_frameState = {XR_TYPE_FRAME_STATE};
    m_lastPredictedDisplayTime = 0;
    m_lastPredictedQpc = 0;
    m_win32TimeConversionAvailable = false;
    m_focusedSinceBegin = false;
    m_submittedFramePairCount = 0;
    m_frameSerial = 0;
    m_handsFrameSerial = 0;
    m_handPosesStale = false;
    ClearRenderPoses();
    m_eyeProjections[0] = {};
    m_eyeProjections[1] = {};
    m_viewConfigViews.clear();
    m_submittedQuadFrameCount = 0;
    m_lastLoggedIpd = 0.0f;
    m_loggedHeadsetFovValid = false;
    m_loggedRenderedFovValid = false;
    m_fovOverscan = m_fovOverscanSuspended.load() ? 0.0f : m_requestedFovOverscan.load();
    m_loggedEyeCopySizeMismatch = false;
    m_loggedMissingEyeProjection = false;
    m_headPoseLocateFailures = 0;
    m_menuQuad.ClearCustomSize();
    m_lastSubmittedQuadLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    m_lastPolledTime = 0;
    m_lastLocatedViewsTime = 0;
    m_device = nullptr;
    m_queue = nullptr;
    m_eyeCopy.SetDeviceAndQueue(m_device, m_queue);
}

bool OpenXrManager::CreateSession(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (m_state.load() != OpenXrState::Active || m_instance == XR_NULL_HANDLE) {
        return false;
    }
    if (device) m_device = device;
    if (queue) m_queue = queue;
    m_eyeCopy.SetDeviceAndQueue(m_device, m_queue);

    if (m_session.load() != XR_NULL_HANDLE) {
        return true;
    }

    XrGraphicsBindingD3D12KHR d3d12Binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
    d3d12Binding.device = m_device;
    d3d12Binding.queue = m_queue;

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = (m_device != nullptr) ? &d3d12Binding : nullptr;
    sessionInfo.systemId = m_runtimeInfo.systemId;

    XrSession createdSession = XR_NULL_HANDLE;
    XrResult res = m_dispatch->CreateSession(m_instance, &sessionInfo, &createdSession);
    if (XR_FAILED(res)) {
        m_session.store(XR_NULL_HANDLE);
        LogError("OpenXrManager: The OpenXR runtime refused to create the session (result " + std::to_string(res) +
                 "). Continuing in Pass-Through Mode.");
        ReleaseAndEnterPassThrough("session refused");
        return false;
    }
    m_session.store(createdSession);
    m_sessionState = XR_SESSION_STATE_IDLE;
    LogInfo("OpenXrManager: Session created successfully; waiting for the runtime to report READY.");

    m_input.Create(*m_dispatch, m_instance, createdSession);
    return true;
}

void OpenXrManager::PollEvents() {
    if (m_instance == XR_NULL_HANDLE || m_state.load() != OpenXrState::Active) {
        return;
    }

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
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
            const auto* change = reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&event);
            if (change->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
                m_menuQuad.AwaitHeadAfter(change->changeTime);
                LogInfo("OpenXrManager: Runtime recentered the local space; the menu quad follows the head.");
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            ReleaseAndEnterPassThrough("instance loss pending");
            break;
        default:
            break;
        }

        if (m_state.load() != OpenXrState::Active) {
            return;
        }
    }

    if (m_sessionState == XR_SESSION_STATE_READY && !m_sessionRunning) {
        BeginRunningSession();
    }
}

bool OpenXrManager::EnterPassThroughIfLost(XrResult res) {
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
    case XR_SESSION_STATE_FOCUSED:
        if (!m_focusedSinceBegin) {
            m_focusedSinceBegin = true;
            m_menuQuad.AwaitHeadAfter(0);
        }
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
        if (m_beginSessionFailures++ % 300 == 0) {
            LogError("OpenXrManager: BeginSession failed (result " + std::to_string(res) + ", attempt " +
                     std::to_string(m_beginSessionFailures) + ")");
        }
        EnterPassThroughIfLost(res);
        return;
    }
    m_beginSessionFailures = 0;
    m_sessionRunning = true;
    m_focusedSinceBegin = false;
    LogInfo("OpenXrManager: Session begun successfully.");
}

void OpenXrManager::EndRunningSession() {
    if (m_session == XR_NULL_HANDLE || !m_sessionRunning) {
        return;
    }
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
    m_sessionRunning = false;
    Shutdown();
    m_state.store(OpenXrState::PassThrough);
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
            m_rawPose.orientation = OrientationFromRuntime(baseLocation.pose.orientation);

            m_rawPose.positionTracked = (baseLocation.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
            m_rawPose.orientationTracked = (baseLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
            m_rawPose.isValid = posValid && oriValid;
        }
    }

    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    XrResult res = m_dispatch->LocateSpace(m_viewSpace, m_localSpace, time, &location);
    if (XR_FAILED(res)) {
        if (++m_headPoseLocateFailures % 300 == 1) {
            LogWarn("OpenXrManager::PollHeadPose: LocateSpace failed (result " + std::to_string(res) + ")");
        }
        return m_lastPose;
    }

    bool posValid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
    bool oriValid = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

    m_lastPose.position = Vector3(location.pose.position.x, location.pose.position.y, location.pose.position.z);
    m_lastPose.orientation = OrientationFromRuntime(location.pose.orientation);

    m_lastPose.positionTracked = (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    m_lastPose.orientationTracked = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
    m_lastPose.isValid = posValid && oriValid;

    if (m_baseLocalSpace == XR_NULL_HANDLE) {
        m_rawPose = m_lastPose;
    }

    m_lastPolledTime = time;
    return m_lastPose;
}

void OpenXrManager::RefreshHandState(XrTime time, bool resync) {
    if (resync) {
        m_input.Sync(*m_dispatch, m_session);
    }
    m_input.LocatePoses(*m_dispatch, m_localSpace, time);
    m_handPosesStale = false;
    m_handsFrameSerial = m_frameSerial;
}

HandState OpenXrManager::GetHandState(uint32_t hand) {
    if (hand >= OpenXrInput::kHandCount || m_state.load() != OpenXrState::Active || m_session.load() == XR_NULL_HANDLE ||
        m_localSpace == XR_NULL_HANDLE || !m_input.IsReady()) {
        return HandState{};
    }

    if (m_frameBegun && m_handsFrameSerial == m_frameSerial) {
        if (m_handPosesStale) {
            RefreshHandState(m_frameState.predictedDisplayTime, false);
        }
    } else {
        RefreshHandState(GetCurrentXrTime(), true);
    }
    return m_input.Get(hand);
}

bool OpenXrManager::PulseHaptic(uint32_t hand, double durationSeconds, float amplitude) {
    const XrSession session = m_session.load();
    if (m_state.load() != OpenXrState::Active || session == XR_NULL_HANDLE || !(durationSeconds > 0.0)) {
        return false;
    }
    const float strength = std::min(1.0f, std::max(0.0f, amplitude));
    const double nanoseconds = std::min(durationSeconds * 1.0e9, 1.0e10);  // at most 10 s
    return m_input.PulseHaptic(*m_dispatch, session, hand, static_cast<int64_t>(std::llround(nanoseconds)), strength);
}

void OpenXrManager::Recenter() {
    ClearRenderPoses();
    Vector3 recenterPos = m_rawPose.position;
    float recenterYaw = m_rawPose.Rotation().yaw;
    const XrTime time = (m_frameState.predictedDisplayTime != 0) ? m_frameState.predictedDisplayTime : GetCurrentXrTime();

    if (m_session != XR_NULL_HANDLE && m_baseLocalSpace != XR_NULL_HANDLE && m_viewSpace != XR_NULL_HANDLE) {
        XrSpaceLocation baseLoc{XR_TYPE_SPACE_LOCATION};
        XrResult res = m_dispatch->LocateSpace(m_viewSpace, m_baseLocalSpace, time, &baseLoc);
        if (XR_SUCCEEDED(res) &&
            (baseLoc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
            (baseLoc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            recenterPos = Vector3(baseLoc.pose.position.x, baseLoc.pose.position.y, baseLoc.pose.position.z);
            const Quaternion q = OrientationFromRuntime(baseLoc.pose.orientation);
            recenterYaw = QuaternionToEuler(q).yaw;

            m_rawPose.position = recenterPos;
            m_rawPose.orientation = q;
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
    m_handPosesStale = true;
    PollHeadPose(time);
    AlignMenuQuadToHeadPose();
    LogInfo("OpenXrManager: Head pose recentered.");
}

float OpenXrManager::ClampFovOverscan(float percent) {
    if (!(percent >= 0.0f)) return 0.0f;
    return std::min(percent, kMaxFovOverscanPercent);
}

void OpenXrManager::SetFovOverscan(float percent) {
    m_requestedFovOverscan.store(ClampFovOverscan(percent));
}

EyeProjection OpenXrManager::ComputeProjectionFromFov(const XrFovf& fov, float swapchainAspect, float fovOverscanPercent) {
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

    if (std::abs(tanHeight) > 1e-6f) {
        proj.aspectRatio = tanWidth / tanHeight;
    }

    const float aspect = (swapchainAspect > 1e-6f) ? swapchainAspect : proj.aspectRatio;
    const float widerTanX = std::max(std::abs(proj.tanLeft), std::abs(proj.tanRight));
    const float coveringTanHalfY = std::max({ std::abs(proj.tanUp), std::abs(proj.tanDown), widerTanX / aspect });
    const float tanHalfY = coveringTanHalfY * (1.0f + fovOverscanPercent / 100.0f);
    proj.fovY = 2.0f * std::atan(tanHalfY);
    proj.fovX = 2.0f * std::atan(aspect * tanHalfY);

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

    if (!m_frameBegun) {
        LatchFovOverscan();
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
        m_eyeProjections[0] = ComputeProjectionFromFov(views[0].fov, SwapchainAspect(0), m_fovOverscan);
        m_eyeProjections[1] = ComputeProjectionFromFov(views[1].fov, SwapchainAspect(1), m_fovOverscan);
        LogFovIfChanged();
        float dx = views[1].pose.position.x - views[0].pose.position.x;
        float dy = views[1].pose.position.y - views[0].pose.position.y;
        float dz = views[1].pose.position.z - views[0].pose.position.z;
        float measuredIpd = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (measuredIpd > 0.04f && measuredIpd < 0.10f) {
            m_ipd = measuredIpd;
        }

        if (std::abs(m_ipd - m_lastLoggedIpd) > 0.002f) {
            m_lastLoggedIpd = m_ipd;
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

void OpenXrManager::LatchFovOverscan() {
    const float wanted = m_fovOverscanSuspended.load() ? 0.0f : m_requestedFovOverscan.load();
    if (wanted != m_fovOverscan) {
        m_fovOverscan = wanted;
        m_lastLocatedViewsTime = 0;
    }
}

bool OpenXrManager::HasRenderedFov() const {
    for (const EyeProjection& proj : m_eyeProjections) {
        if (!proj.isValid || proj.fovY <= kMinRenderedFovRadians || proj.fovX <= kMinRenderedFovRadians) {
            return false;
        }
    }
    return true;
}

float OpenXrManager::SwapchainAspect(uint32_t eyeIndex) const {
    if (!m_hasEyeSwapchains || eyeIndex >= 2 || m_eyeSwapchains[eyeIndex].height == 0) {
        return 0.0f;
    }
    return static_cast<float>(m_eyeSwapchains[eyeIndex].width) / static_cast<float>(m_eyeSwapchains[eyeIndex].height);
}

void OpenXrManager::RefreshRenderedFov() {
    for (uint32_t i = 0; i < 2; ++i) {
        const EyeProjection& current = m_eyeProjections[i];
        if (!current.isValid) {
            continue;
        }
        const XrFovf headsetFov{ current.angleLeft, current.angleRight, current.angleUp, current.angleDown };
        m_eyeProjections[i] = ComputeProjectionFromFov(headsetFov, SwapchainAspect(i), m_fovOverscan);
    }
    LogFovIfChanged();
}

void OpenXrManager::LogFovIfChanged() {
    if (!m_eyeProjections[0].isValid || !m_eyeProjections[1].isValid) {
        return;
    }
    const float radToDeg = 180.0f / 3.14159265358979f;

    HeadsetFov headset[2];
    RenderedFov rendered[2];
    for (int eye = 0; eye < 2; ++eye) {
        const EyeProjection& proj = m_eyeProjections[eye];
        headset[eye] = { proj.angleLeft, proj.angleRight, proj.angleUp, proj.angleDown };
        rendered[eye] = { proj.fovY, proj.fovX };
    }

    const bool headsetChanged = !m_loggedHeadsetFovValid || !(headset[0] == m_loggedHeadsetFov[0]) ||
                                !(headset[1] == m_loggedHeadsetFov[1]);
    if (headsetChanged) {
        std::copy(headset, headset + 2, m_loggedHeadsetFov);
        m_loggedHeadsetFovValid = true;
        std::stringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(1);
        ss << "OpenXrManager: Headset FOV (degrees, left/right/up/down)";
        for (int eye = 0; eye < 2; ++eye) {
            const HeadsetFov& fov = headset[eye];
            ss << " - Eye" << eye << ": [" << fov.left * radToDeg << ", " << fov.right * radToDeg << ", "
               << fov.up * radToDeg << ", " << fov.down * radToDeg << "]";
        }
        LogInfo(ss.str());
    }

    const bool renderedChanged = !m_loggedRenderedFovValid || !(rendered[0] == m_loggedRenderedFov[0]) ||
                                 !(rendered[1] == m_loggedRenderedFov[1]) ||
                                 std::abs(m_loggedFovOverscan - m_fovOverscan) > 1e-4f;
    if (renderedChanged) {
        std::copy(rendered, rendered + 2, m_loggedRenderedFov);
        m_loggedRenderedFovValid = true;
        m_loggedFovOverscan = m_fovOverscan;
        std::stringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(1);
        ss << "OpenXrManager: Rendered FOV (degrees, vertical x horizontal)";
        for (int eye = 0; eye < 2; ++eye) {
            ss << " - Eye" << eye << ": " << rendered[eye].vertical * radToDeg << " x "
               << rendered[eye].horizontal * radToDeg;
        }
        ss << " - Overscan " << m_fovOverscan << "%";
        LogInfo(ss.str());
    }
}

EyeProjection OpenXrManager::PollEyeProjection(uint32_t eyeIndex, XrTime time) {
    if (eyeIndex >= 2) {
        eyeIndex = 0;
    }

    if (m_state.load() != OpenXrState::Active || m_session.load() == XR_NULL_HANDLE) {
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
    m_eyeCopy.SetDeviceAndQueue(m_device, m_queue);
    if (m_session.load() == XR_NULL_HANDLE && m_state.load() == OpenXrState::Active && m_device && m_queue) {
        CreateSession(m_device, m_queue);
        CreateTrackingSpaces();
    }
}

DXGI_FORMAT OpenXrManager::SelectCompatibleSwapchainFormat(DXGI_FORMAT backBufferFormat, const std::vector<int64_t>& supported) {
    DXGI_FORMAT candidates[2] = { backBufferFormat, DXGI_FORMAT_UNKNOWN };
    switch (backBufferFormat) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        candidates[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        candidates[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
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
    std::vector<int64_t> supportedFormats;
    uint32_t formatCount = 0;
    XrResult res = m_dispatch->EnumerateSwapchainFormats(m_session, 0, &formatCount, nullptr);
    if (XR_SUCCEEDED(res) && formatCount > 0) {
        supportedFormats.resize(formatCount);
        res = m_dispatch->EnumerateSwapchainFormats(m_session, formatCount, &formatCount, supportedFormats.data());
        supportedFormats.resize(XR_SUCCEEDED(res) ? formatCount : 0);
    }
    if (supportedFormats.empty()) {
        LogWarn("OpenXrManager: Could not enumerate swapchain formats (result " + std::to_string(res) +
                "); requesting back buffer format " + std::to_string(backBufferFormat) + " unchanged.");
        return backBufferFormat;
    }
    return SelectCompatibleSwapchainFormat(backBufferFormat, supportedFormats);
}

void OpenXrManager::EnterPassThroughForIncompatibleFormat(DXGI_FORMAT backBufferFormat) {
    LogError("OpenXrManager: OpenXR runtime offers no swapchain format compatible with back buffer format " +
             std::to_string(backBufferFormat) + ".");
    ReleaseAndEnterPassThrough("no copy-compatible swapchain format");
}

bool OpenXrManager::EnsureBackBufferFormatSupported(DXGI_FORMAT backBufferFormat) {
    if (m_session.load() == XR_NULL_HANDLE || m_state.load() != OpenXrState::Active) {
        return m_state.load() == OpenXrState::Active;
    }
    if (NegotiateEyeSwapchainFormat(backBufferFormat) == DXGI_FORMAT_UNKNOWN) {
        EnterPassThroughForIncompatibleFormat(backBufferFormat);
        return false;
    }
    return true;
}

bool OpenXrManager::CreateEyeSwapchains(uint32_t width, uint32_t height, DXGI_FORMAT format) {
    if (m_session.load() == XR_NULL_HANDLE || m_state.load() != OpenXrState::Active) {
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
    LogInfo("OpenXrManager: Eye swapchain format " + std::to_string(selectedFormat) +
            " for back buffer format " + std::to_string(format) + ".");
    if (selectedFormat == DXGI_FORMAT_R8G8B8A8_UNORM || selectedFormat == DXGI_FORMAT_B8G8R8A8_UNORM) {
        LogWarn("OpenXrManager: The OpenXR runtime offers no sRGB eye swapchain format; the headset image may look washed out and too bright.");
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
            EnterPassThroughIfLost(res);
            return false;
        }

        if (!EnumerateSwapchainImagesHelper(m_eyeSwapchains[i])) {
            DestroyEyeSwapchains();
            return false;
        }

        m_eyeSwapchains[i].width = eyeWidth;
        m_eyeSwapchains[i].height = eyeHeight;
        m_eyeSwapchains[i].format = selectedFormat;
        m_eyeSwapchains[i].lastCopiedIndex = 0;
        m_eyeSwapchains[i].hasCopiedFrame = false;
    }

    m_hasEyeSwapchains = true;
    RefreshRenderedFov();
    LogInfo("OpenXrManager: Created stereoscopic eye swapchains (" + std::to_string(m_eyeSwapchains[0].width) + "x" + std::to_string(m_eyeSwapchains[0].height) + ").");
    return true;
}

void OpenXrManager::DestroyEyeSwapchains() {
    m_eyeCopy.Release();
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
    RefreshRenderedFov();
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
    XrResult res = m_dispatch->EnumerateSwapchainImages(sc.handle, 0, &imageCount, nullptr);
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::EnumerateSwapchainImages failed (result " + std::to_string(res) + ")");
        EnterPassThroughIfLost(res);
        return false;
    }
    if (imageCount > 0) {
        sc.images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        res = m_dispatch->EnumerateSwapchainImages(
            sc.handle,
            imageCount,
            &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data()));
        if (XR_FAILED(res)) {
            LogError("OpenXrManager::EnumerateSwapchainImages failed (result " + std::to_string(res) + ")");
            sc.images.clear();
            EnterPassThroughIfLost(res);
            return false;
        }
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
    bool created = m_cursorOverlay.CreateSwapchain(m_session, m_dispatch, width, height, format, m_device, m_queue);
    if (EnterPassThroughIfLost(m_cursorOverlay.GetLastXrResult())) {
        return false;
    }
    return created;
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
    m_hasRenderPose[0] = false;
    m_hasRenderPose[1] = false;
}

void OpenXrManager::ResetCopiedSwapchainsAndCloseFrame() {
    m_eyeSwapchains[0].hasCopiedFrame = false;
    m_eyeSwapchains[1].hasCopiedFrame = false;
    if (m_frameBegun) {
        EndFrame();
    }
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
    XrResult res;
    {
        PerfScope waitTiming(PerfSection::XrWaitFrame);
        res = m_dispatch->WaitFrame(m_session, &waitInfo, &m_frameState);
    }
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::BeginFrame: WaitFrame failed (result " + std::to_string(res) + ")");
        EnterPassThroughIfLost(res);
        return false;
    }
    PerfScope beginTiming(PerfSection::FrameBegin);

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

    LatchFovOverscan();
    m_frameBegun = true;
    ++m_frameSerial;
    if (m_input.IsReady()) {
        RefreshHandState(m_frameState.predictedDisplayTime != 0 ? m_frameState.predictedDisplayTime : GetCurrentXrTime(), true);
    }

    if (m_frameState.shouldRender) {
        LocateActiveViews(m_frameState.predictedDisplayTime);
        PollHeadPose(m_frameState.predictedDisplayTime);
        if (m_menuQuad.AwaitsHead(m_lastPose.isValid, m_frameState.predictedDisplayTime)) {
            AlignMenuQuadToHeadPose();
            LogInfo("OpenXrManager: Menu quad placed in front of the tracked head.");
        }
    }

    std::shared_ptr<const FrameBeginObserverList> observers;
    {
        std::lock_guard<std::mutex> lock(m_frameBeginObserversMutex);
        observers = m_frameBeginObservers;
    }
    for (const auto& entry : *observers) entry.second();
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

    XrResult res = DispatchEndFrame(endInfo);
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

XrResult OpenXrManager::DispatchEndFrame(const XrFrameEndInfo& endInfo) {
    PerfScope timing(PerfSection::XrEndFrame);
    return m_dispatch->EndFrame(m_session, &endInfo);
}

void OpenXrManager::LogEyeCopySizeMismatchOnce(uint64_t srcWidth, uint32_t srcHeight, uint64_t dstWidth, uint32_t dstHeight) {
    if (m_loggedEyeCopySizeMismatch) {
        return;
    }
    m_loggedEyeCopySizeMismatch = true;
    LogWarn("OpenXrManager::CopyBackBufferToEye: Dimension mismatch! BackBuffer: " +
            std::to_string(srcWidth) + "x" + std::to_string(srcHeight) +
            ", OpenXR swapchain: " + std::to_string(dstWidth) + "x" + std::to_string(dstHeight));
}

bool OpenXrManager::CopyBackBufferToEye(uint32_t eyeIndex, ID3D12Resource* backBuffer) {
    if (eyeIndex >= 2 || !m_hasEyeSwapchains) {
        return false;
    }

    if (!m_frameBegun) {
        if (!BeginFrame()) {
            return false;
        }
    }

    if (!m_frameState.shouldRender) {
        m_eyeSwapchains[eyeIndex].hasCopiedFrame = true;
        return true;
    }

    if (m_hasRenderPose[eyeIndex]) {
        m_capturedEyePoses[eyeIndex] = m_renderPoses[eyeIndex];
        m_hasRenderPose[eyeIndex] = false;
    } else {
        m_capturedEyePoses[eyeIndex] = m_lastPose;
    }

    XrSwapchain swapchain = m_eyeSwapchains[eyeIndex].handle;
    if (swapchain == XR_NULL_HANDLE) {
        ResetCopiedSwapchainsAndCloseFrame();
        return false;
    }

    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t imageIndex = 0;
    XrResult res = XR_SUCCESS;
    bool acquired = false;
    {
        PerfScope swapchainTiming(PerfSection::SwapchainWait);
        res = m_dispatch->AcquireSwapchainImage(swapchain, &acquireInfo, &imageIndex);
        if (XR_SUCCEEDED(res)) {
            acquired = true;
            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitInfo.timeout = XR_INFINITE_DURATION;
            res = m_dispatch->WaitSwapchainImage(swapchain, &waitInfo);
        }
    }
    if (XR_FAILED(res)) {
        LogError(std::string(acquired ? "OpenXrManager::CopyBackBufferToEye: WaitSwapchainImage failed (result "
                                      : "OpenXrManager::CopyBackBufferToEye: AcquireSwapchainImage failed (result ") +
                 std::to_string(res) + ")");
        XrResult releaseRes = XR_SUCCESS;
        if (acquired) {
            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            releaseRes = m_dispatch->ReleaseSwapchainImage(swapchain, &releaseInfo);
        }
        if (!EnterPassThroughIfLost(res) && !EnterPassThroughIfLost(releaseRes)) {
            ResetCopiedSwapchainsAndCloseFrame();
        }
        return false;
    }

    if (backBuffer && m_device && m_queue) {
        ID3D12Resource* destTexture = nullptr;
        if (imageIndex < m_eyeSwapchains[eyeIndex].images.size()) {
            destTexture = m_eyeSwapchains[eyeIndex].images[imageIndex].texture;
        }
        m_eyeCopy.Copy(backBuffer, destTexture, m_eyeSwapchains[eyeIndex].width, m_eyeSwapchains[eyeIndex].height);
    }

    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    res = m_dispatch->ReleaseSwapchainImage(swapchain, &releaseInfo);
    if (XR_FAILED(res)) {
        LogError("OpenXrManager::CopyBackBufferToEye: ReleaseSwapchainImage failed (result " + std::to_string(res) + ")");
        if (!EnterPassThroughIfLost(res)) {
            ResetCopiedSwapchainsAndCloseFrame();
        }
        return false;
    }

    m_eyeSwapchains[eyeIndex].lastCopiedIndex = imageIndex;
    m_eyeSwapchains[eyeIndex].hasCopiedFrame = true;
    return true;
}

bool OpenXrManager::ClearBackBufferToBlack(ID3D12Resource* backBuffer) {
    if (!backBuffer || !m_device || !m_queue) {
        return false;
    }
    return m_eyeCopy.ClearToBlack(backBuffer);
}

bool OpenXrManager::SubmitFramePair() {
    if (!m_hasEyeSwapchains || m_session == XR_NULL_HANDLE) {
        return false;
    }

    if (!m_eyeSwapchains[0].hasCopiedFrame || !m_eyeSwapchains[1].hasCopiedFrame) {
        if (!m_eyeSwapchains[0].hasCopiedFrame) {
            ResetCopiedSwapchainsAndCloseFrame();
        }
        return false;
    }

    if (!m_frameBegun) {
        LogError("OpenXrManager::SubmitFramePair: No frame currently begun.");
        return false;
    }

    if (m_frameState.shouldRender && !HasRenderedFov()) {
        if (!m_loggedMissingEyeProjection) {
            m_loggedMissingEyeProjection = true;
            LogWarn("OpenXrManager::SubmitFramePair: No eye projection with a usable field of view from the runtime yet (the views are not located, or a field of view is near zero); submitting empty frames until it reports one.");
        }
        ResetCopiedSwapchainsAndCloseFrame();
        return false;
    }

    XrCompositionLayerProjectionView projViews[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        projViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        projViews[i].next = nullptr;

        const float halfFovY = m_eyeProjections[i].fovY * 0.5f;
        const float halfFovX = m_eyeProjections[i].fovX * 0.5f;

        projViews[i].fov.angleLeft = -halfFovX;
        projViews[i].fov.angleRight = halfFovX;
        projViews[i].fov.angleUp = halfFovY;
        projViews[i].fov.angleDown = -halfFovY;

        const HeadPose& eyePose = m_capturedEyePoses[i];
        Vector3 eyePos = ComputeEyePosition(eyePose, m_ipd, i);

        projViews[i].pose.position.x = eyePos.x;
        projViews[i].pose.position.y = eyePos.y;
        projViews[i].pose.position.z = eyePos.z;

        projViews[i].pose.orientation.x = eyePose.orientation.x;
        projViews[i].pose.orientation.y = eyePose.orientation.y;
        projViews[i].pose.orientation.z = eyePose.orientation.z;
        projViews[i].pose.orientation.w = eyePose.orientation.w;

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

    XrResult res = DispatchEndFrame(endInfo);
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

void OpenXrManager::AlignMenuQuadToHeadPose(const HeadPose* customPose) {
    m_menuQuad.AlignToHeadPose(customPose ? *customPose : m_lastPose);
}

XrExtent2Df OpenXrManager::ComputeMenuQuadSize() const {
    if (m_hasEyeSwapchains) {
        return m_menuQuad.Size(m_eyeSwapchains[0].width, m_eyeSwapchains[0].height);
    }
    return m_menuQuad.Size(0, 0);
}

PanelHit OpenXrManager::GetPanelHit(uint32_t hand) {
    const HandState state = GetHandState(hand);
    if (!state.tracked || !state.aim.isValid) {
        return {};
    }
    return MenuQuad::RayHit(m_menuQuad.Pose(), ComputeMenuQuadSize(), state.aim.position, state.aim.orientation);
}

PanelHit OpenXrManager::GetPanelHitAlongRay(uint32_t hand, const Vector3& origin, const Vector3& direction) {
    const HandState state = GetHandState(hand);
    if (!state.tracked || !state.aim.isValid) {
        return {};
    }
    return MenuQuad::RayHitAlong(m_menuQuad.Pose(), ComputeMenuQuadSize(), origin, direction);
}

bool OpenXrManager::SubmitMenuQuad(const XrPosef* customPose, const XrExtent2Df* customSize) {
    if (!m_hasEyeSwapchains || m_session == XR_NULL_HANDLE) {
        return false;
    }

    if (!m_eyeSwapchains[0].hasCopiedFrame) {
        ResetCopiedSwapchainsAndCloseFrame();
        return false;
    }

    if (!m_frameBegun) {
        LogError("OpenXrManager::SubmitMenuQuad: No frame currently begun.");
        return false;
    }

    if (m_localSpace == XR_NULL_HANDLE) {
        LogError("OpenXrManager::SubmitMenuQuad: Local reference space is not initialized.");
        ResetCopiedSwapchainsAndCloseFrame();
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
    if (EnterPassThroughIfLost(m_cursorOverlay.GetLastXrResult())) {
        return false;
    }

    const XrCompositionLayerBaseHeader* layers[2] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quadLayer),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cursorQuadLayer)
    };

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = m_frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = m_frameState.shouldRender ? (showCursor ? 2 : 1) : 0;
    endInfo.layers = m_frameState.shouldRender ? layers : nullptr;

    XrResult res = DispatchEndFrame(endInfo);
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

}
