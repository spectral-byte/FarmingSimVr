#include "openxr_manager.h"
#include "core/logger.h"
#include <vector>
#include <cstring>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace fs25vr {

namespace {

XrTime GetCurrentXrTime() {
    LARGE_INTEGER qpc, freq;
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&freq);
    return static_cast<XrTime>((qpc.QuadPart * 1000000000LL) / freq.QuadPart);
}

} // anonymous namespace

OpenXrManager& OpenXrManager::Instance() {
    static OpenXrManager instance;
    return instance;
}

OpenXrManager::OpenXrManager(std::shared_ptr<IOpenXrDispatch> dispatch)
    : m_dispatch(dispatch ? std::move(dispatch) : std::make_shared<NativeOpenXrDispatch>()) {
}

OpenXrManager::~OpenXrManager() {
    Shutdown();
}

bool OpenXrManager::CheckD3D12ExtensionSupport() {
    uint32_t extensionCount = 0;
    XrResult res = m_dispatch->EnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    if (res != XR_SUCCESS || extensionCount == 0) {
        LogWarn("Failed to enumerate OpenXR extensions or no extensions found.");
        return false;
    }

    std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES, nullptr});
    res = m_dispatch->EnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
    if (res != XR_SUCCESS) {
        LogWarn("Failed to retrieve OpenXR extension properties.");
        return false;
    }

    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0) {
            m_runtimeInfo.d3d12ExtensionSupported = true;
            return true;
        }
    }

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

    const char* enabledExtensions[] = {
        XR_KHR_D3D12_ENABLE_EXTENSION_NAME
    };

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = nullptr;
    createInfo.createFlags = 0;
    strncpy_s(createInfo.applicationInfo.applicationName, "FarmingSimulator2025 VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
    createInfo.applicationInfo.applicationVersion = 1;
    strncpy_s(createInfo.applicationInfo.engineName, "GIANTS Engine 10", XR_MAX_ENGINE_NAME_SIZE - 1);
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions;

    XrResult res = m_dispatch->CreateInstance(&createInfo, &m_instance);
    if (res != XR_SUCCESS) {
        std::stringstream ss;
        ss << "xrCreateInstance failed (result " << res << "). Silently entering Pass-Through Mode.";
        LogInfo(ss.str());
        m_state = OpenXrState::PassThrough;
        return true;
    }

    XrInstanceProperties instanceProps{XR_TYPE_INSTANCE_PROPERTIES};
    if (m_dispatch->GetInstanceProperties(m_instance, &instanceProps) == XR_SUCCESS) {
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
    if (res != XR_SUCCESS) {
        std::stringstream ss;
        ss << "OpenXR HMD system unavailable (result " << res << "). Silently entering Pass-Through Mode.";
        LogInfo(ss.str());

        m_dispatch->DestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
        m_state = OpenXrState::PassThrough;
        return true;
    }

    XrSystemProperties systemProps{XR_TYPE_SYSTEM_PROPERTIES};
    if (m_dispatch->GetSystemProperties(m_instance, m_runtimeInfo.systemId, &systemProps) == XR_SUCCESS) {
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

    XrGraphicsRequirementsD3D12KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    res = m_dispatch->GetD3D12GraphicsRequirementsKHR(m_instance, m_runtimeInfo.systemId, &graphicsRequirements);
    if (res != XR_SUCCESS) {
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
    if (m_viewSpace != XR_NULL_HANDLE) {
        m_dispatch->DestroySpace(m_viewSpace);
        m_viewSpace = XR_NULL_HANDLE;
    }
    if (m_localSpace != XR_NULL_HANDLE) {
        m_dispatch->DestroySpace(m_localSpace);
        m_localSpace = XR_NULL_HANDLE;
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
    m_hasRecenter = false;
    m_useSimulatedPose = false;
    m_useSimulatedProjections = false;
    m_eyeProjections[0] = {};
    m_eyeProjections[1] = {};
}

bool OpenXrManager::CreateSession(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (m_state != OpenXrState::Active || m_instance == XR_NULL_HANDLE) {
        return false;
    }
    if (m_session != XR_NULL_HANDLE) {
        return true;
    }

    XrGraphicsBindingD3D12KHR d3d12Binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
    d3d12Binding.device = device;
    d3d12Binding.queue = queue;

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = (device != nullptr) ? &d3d12Binding : nullptr;
    sessionInfo.systemId = m_runtimeInfo.systemId;

    XrResult res = m_dispatch->CreateSession(m_instance, &sessionInfo, &m_session);
    if (res != XR_SUCCESS) {
        LogError("OpenXrManager::CreateSession failed with result " + std::to_string(res));
        return false;
    }
    LogInfo("OpenXrManager: Session created successfully.");
    return true;
}

bool OpenXrManager::CreateTrackingSpaces() {
    if (m_session == XR_NULL_HANDLE) {
        return false;
    }

    if (m_localSpace == XR_NULL_HANDLE) {
        XrReferenceSpaceCreateInfo localSpaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        localSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        localSpaceInfo.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        localSpaceInfo.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};

        XrResult res = m_dispatch->CreateReferenceSpace(m_session, &localSpaceInfo, &m_localSpace);
        if (res != XR_SUCCESS) {
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
        if (res != XR_SUCCESS) {
            LogError("OpenXrManager: Failed to create view reference space (result " + std::to_string(res) + ")");
            return false;
        }
    }

    LogInfo("OpenXrManager: Local and View tracking reference spaces established.");
    return true;
}

void OpenXrManager::QuaternionToEuler(float qx, float qy, float qz, float qw,
                                      float& pitch, float& yaw, float& roll) {
    float sinPitch = 2.0f * (qw * qx - qy * qz);
    if (std::abs(sinPitch) >= 0.99999f) {
        pitch = std::copysign(3.14159265f / 2.0f, sinPitch);
        yaw = 2.0f * std::atan2(qy, qw);
        roll = 0.0f;
    } else {
        pitch = std::asin(std::clamp(sinPitch, -1.0f, 1.0f));
        yaw = std::atan2(2.0f * (qw * qy + qx * qz), 1.0f - 2.0f * (qx * qx + qy * qy));
        roll = std::atan2(2.0f * (qw * qz + qx * qy), 1.0f - 2.0f * (qx * qx + qz * qz));
    }
}

HeadPose OpenXrManager::PollHeadPose(XrTime time) {
    if (!m_useSimulatedPose) {
        if (m_session == XR_NULL_HANDLE || m_localSpace == XR_NULL_HANDLE || m_viewSpace == XR_NULL_HANDLE) {
            return m_lastPose;
        }

        if (time == 0) {
            time = GetCurrentXrTime();
        }

        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        XrResult res = m_dispatch->LocateSpace(m_viewSpace, m_localSpace, time, &location);
        if (res != XR_SUCCESS) {
            return m_lastPose;
        }

        bool posValid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        bool oriValid = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

        m_rawPose.x = location.pose.position.x;
        m_rawPose.y = location.pose.position.y;
        m_rawPose.z = location.pose.position.z;

        m_rawPose.qx = location.pose.orientation.x;
        m_rawPose.qy = location.pose.orientation.y;
        m_rawPose.qz = location.pose.orientation.z;
        m_rawPose.qw = location.pose.orientation.w;

        QuaternionToEuler(m_rawPose.qx, m_rawPose.qy, m_rawPose.qz, m_rawPose.qw,
                          m_rawPose.pitch, m_rawPose.yaw, m_rawPose.roll);

        m_rawPose.positionTracked = (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
        m_rawPose.orientationTracked = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
        m_rawPose.isValid = posValid && oriValid;
    }

    HeadPose recentered = m_rawPose;
    if (m_hasRecenter) {
        float dx = m_rawPose.x - m_recenterOffset.x;
        float dy = m_rawPose.y - m_recenterOffset.y;
        float dz = m_rawPose.z - m_recenterOffset.z;

        float cosY = std::cos(-m_recenterOffset.yaw);
        float sinY = std::sin(-m_recenterOffset.yaw);

        recentered.x = dx * cosY - dz * sinY;
        recentered.y = dy;
        recentered.z = dx * sinY + dz * cosY;

        recentered.yaw = m_rawPose.yaw - m_recenterOffset.yaw;
        while (recentered.yaw > 3.14159265f) recentered.yaw -= 6.2831853f;
        while (recentered.yaw < -3.14159265f) recentered.yaw += 6.2831853f;
    }

    m_lastPose = recentered;
    return m_lastPose;
}

void OpenXrManager::Recenter() {
    m_recenterOffset.x = m_rawPose.x;
    m_recenterOffset.y = m_rawPose.y;
    m_recenterOffset.z = m_rawPose.z;
    m_recenterOffset.yaw = m_rawPose.yaw;
    m_hasRecenter = true;
    LogInfo("OpenXrManager: Head pose recentered.");
}

void OpenXrManager::SetSimulatedPose(const HeadPose& pose) {
    m_simulatedPose = pose;
    m_useSimulatedPose = true;
    m_rawPose = pose;
    m_lastPose = pose;
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

EyeProjection OpenXrManager::PollEyeProjection(uint32_t eyeIndex, XrTime time) {
    if (eyeIndex >= 2) {
        eyeIndex = 0;
    }

    if (m_useSimulatedProjections) {
        return m_simulatedProjections[eyeIndex];
    }

    if (m_state != OpenXrState::Active || m_session == XR_NULL_HANDLE) {
        return m_eyeProjections[eyeIndex];
    }

    if (time == 0) {
        time = GetCurrentXrTime();
    }

    XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = time;
    locateInfo.space = (m_localSpace != XR_NULL_HANDLE) ? m_localSpace : m_viewSpace;

    XrViewState viewState{XR_TYPE_VIEW_STATE};
    uint32_t viewCount = 0;
    XrView views[2] = { {XR_TYPE_VIEW, nullptr}, {XR_TYPE_VIEW, nullptr} };

    XrResult res = m_dispatch->LocateViews(m_session, &locateInfo, &viewState, 2, &viewCount, views);
    if (res == XR_SUCCESS && viewCount >= 2) {
        m_eyeProjections[0] = ComputeProjectionFromFov(views[0].fov);
        m_eyeProjections[1] = ComputeProjectionFromFov(views[1].fov);
    }

    return m_eyeProjections[eyeIndex];
}

const EyeProjection& OpenXrManager::GetEyeProjection(uint32_t eyeIndex) const {
    if (eyeIndex >= 2) eyeIndex = 0;
    if (m_useSimulatedProjections) {
        return m_simulatedProjections[eyeIndex];
    }
    return m_eyeProjections[eyeIndex];
}

void OpenXrManager::SetSimulatedProjection(uint32_t eyeIndex, const EyeProjection& proj) {
    if (eyeIndex >= 2) eyeIndex = 0;
    m_simulatedProjections[eyeIndex] = proj;
    m_useSimulatedProjections = true;
    m_eyeProjections[eyeIndex] = proj;
}

} // namespace fs25vr
