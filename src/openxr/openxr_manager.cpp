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
    if (m_copyFenceEvent) {
        CloseHandle(m_copyFenceEvent);
        m_copyFenceEvent = nullptr;
    }
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

    uint32_t viewConfigCount = 0;
    res = m_dispatch->EnumerateViewConfigurationViews(
        m_instance, m_runtimeInfo.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewConfigCount, nullptr);
    if (res == XR_SUCCESS && viewConfigCount > 0) {
        m_viewConfigViews.assign(viewConfigCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        res = m_dispatch->EnumerateViewConfigurationViews(
            m_instance, m_runtimeInfo.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            viewConfigCount, &viewConfigCount, m_viewConfigViews.data());
        if (res == XR_SUCCESS && !m_viewConfigViews.empty()) {
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
    DestroyEyeSwapchains();
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
    m_viewConfigViews.clear();
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

EulerAngles OpenXrManager::QuaternionToEuler(const Quaternion& q) {
    EulerAngles angles;
    float sinPitch = 2.0f * (q.w * q.x - q.y * q.z);
    if (std::abs(sinPitch) >= 0.99999f) {
        angles.pitch = std::copysign(3.14159265f / 2.0f, sinPitch);
        angles.yaw = 2.0f * std::atan2(q.y, q.w);
        angles.roll = 0.0f;
    } else {
        angles.pitch = std::asin(std::clamp(sinPitch, -1.0f, 1.0f));
        angles.yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        angles.roll = std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.x * q.x + q.z * q.z));
    }
    return angles;
}

Vector3 OpenXrManager::RotateVectorByQuaternion(const Quaternion& q, const Vector3& v) {
    // t = 2 * cross(q.xyz, v)
    float tx = 2.0f * (q.y * v.z - q.z * v.y);
    float ty = 2.0f * (q.z * v.x - q.x * v.z);
    float tz = 2.0f * (q.x * v.y - q.y * v.x);

    // v' = v + qw * t + cross(q.xyz, t)
    return Vector3(
        v.x + q.w * tx + (q.y * tz - q.z * ty),
        v.y + q.w * ty + (q.z * tx - q.x * tz),
        v.z + q.w * tz + (q.x * ty - q.y * tx)
    );
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

        m_rawPose.position = Vector3(location.pose.position.x, location.pose.position.y, location.pose.position.z);
        m_rawPose.orientation = Quaternion(location.pose.orientation.x, location.pose.orientation.y, location.pose.orientation.z, location.pose.orientation.w);
        m_rawPose.rotation = QuaternionToEuler(m_rawPose.orientation);

        m_rawPose.positionTracked = (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
        m_rawPose.orientationTracked = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
        m_rawPose.isValid = posValid && oriValid;
    }

    HeadPose recentered = m_rawPose;
    if (m_hasRecenter) {
        float dx = m_rawPose.position.x - m_recenterOffset.position.x;
        float dy = m_rawPose.position.y - m_recenterOffset.position.y;
        float dz = m_rawPose.position.z - m_recenterOffset.position.z;

        float cosY = std::cos(-m_recenterOffset.yaw);
        float sinY = std::sin(-m_recenterOffset.yaw);

        recentered.position.x = dx * cosY - dz * sinY;
        recentered.position.y = dy;
        recentered.position.z = dx * sinY + dz * cosY;

        float halfNegYaw = -m_recenterOffset.yaw * 0.5f;
        Quaternion qYawInv(0.0f, std::sin(halfNegYaw), 0.0f, std::cos(halfNegYaw));
        recentered.orientation = qYawInv * m_rawPose.orientation;
        recentered.rotation = QuaternionToEuler(recentered.orientation);
    }

    m_lastPose = recentered;
    return m_lastPose;
}

void OpenXrManager::Recenter() {
    m_recenterOffset.position = m_rawPose.position;
    m_recenterOffset.yaw = m_rawPose.rotation.yaw;
    m_hasRecenter = true;
    LogInfo("OpenXrManager: Head pose recentered.");
}

void OpenXrManager::SetSimulatedPose(const HeadPose& pose) {
    m_simulatedPose = pose;
    m_useSimulatedPose = true;
    m_rawPose = m_simulatedPose;
    m_lastPose = m_simulatedPose;
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
        float measuredIpd = std::abs(views[1].pose.position.x - views[0].pose.position.x);
        if (measuredIpd > 0.01f && measuredIpd < 0.20f) {
            m_ipd = measuredIpd;
        }
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

bool OpenXrManager::CreateEyeSwapchains(uint32_t width, uint32_t height, DXGI_FORMAT format) {
    if (m_session == XR_NULL_HANDLE || m_state != OpenXrState::Active) {
        return false;
    }
    if (m_hasEyeSwapchains) {
        return true;
    }

    for (uint32_t i = 0; i < 2; ++i) {
        uint32_t eyeWidth = (width != 0) ? width : GetRecommendedEyeWidth(i);
        uint32_t eyeHeight = (height != 0) ? height : GetRecommendedEyeHeight(i);
        if (eyeWidth == 0) eyeWidth = 1920;
        if (eyeHeight == 0) eyeHeight = 1080;

        XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        createInfo.format = format;
        createInfo.sampleCount = 1;
        createInfo.width = eyeWidth;
        createInfo.height = eyeHeight;
        createInfo.faceCount = 1;
        createInfo.arraySize = 1;
        createInfo.mipCount = 1;

        XrResult res = m_dispatch->CreateSwapchain(m_session, &createInfo, &m_eyeSwapchains[i].handle);
        if (res != XR_SUCCESS) {
            LogError("OpenXrManager::CreateEyeSwapchains failed for eye " + std::to_string(i) + " (result " + std::to_string(res) + ")");
            DestroyEyeSwapchains();
            return false;
        }

        uint32_t imageCount = 0;
        m_dispatch->EnumerateSwapchainImages(m_eyeSwapchains[i].handle, 0, &imageCount, nullptr);
        if (imageCount > 0) {
            m_eyeSwapchains[i].images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
            m_dispatch->EnumerateSwapchainImages(
                m_eyeSwapchains[i].handle,
                imageCount,
                &imageCount,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(m_eyeSwapchains[i].images.data()));
        }

        m_eyeSwapchains[i].width = eyeWidth;
        m_eyeSwapchains[i].height = eyeHeight;
        m_eyeSwapchains[i].format = format;
        m_eyeSwapchains[i].lastCopiedIndex = 0;
        m_eyeSwapchains[i].hasCopiedFrame = false;
    }

    m_hasEyeSwapchains = true;
    LogInfo("OpenXrManager: Created stereoscopic eye swapchains (" + std::to_string(m_eyeSwapchains[0].width) + "x" + std::to_string(m_eyeSwapchains[0].height) + ").");
    return true;
}

void OpenXrManager::DestroyEyeSwapchains() {
    if (m_copyFence && m_queue && m_copyFenceValue > 0) {
        if (m_copyFence->GetCompletedValue() < m_copyFenceValue) {
            if (m_copyFenceEvent) {
                m_copyFence->SetEventOnCompletion(m_copyFenceValue, m_copyFenceEvent);
                WaitForSingleObject(m_copyFenceEvent, INFINITE);
            }
        }
    }
    m_copyFence.Reset();
    m_copyFenceValue = 0;
    if (m_copyFenceEvent) {
        CloseHandle(m_copyFenceEvent);
        m_copyFenceEvent = nullptr;
    }
    for (uint32_t i = 0; i < 2; ++i) {
        if (m_eyeSwapchains[i].handle != XR_NULL_HANDLE) {
            m_dispatch->DestroySwapchain(m_eyeSwapchains[i].handle);
            m_eyeSwapchains[i].handle = XR_NULL_HANDLE;
        }
        m_eyeSwapchains[i].images.clear();
        m_eyeSwapchains[i].hasCopiedFrame = false;
    }
    if (m_frameBegun) {
        EndFrame();
    }
    m_copyCommandList.Reset();
    m_copyAllocator.Reset();
    m_hasEyeSwapchains = false;
}

const EyeSwapchain& OpenXrManager::GetEyeSwapchain(uint32_t eyeIndex) const {
    if (eyeIndex >= 2) eyeIndex = 0;
    return m_eyeSwapchains[eyeIndex];
}

bool OpenXrManager::BeginFrame() {
    if (m_session == XR_NULL_HANDLE) {
        return false;
    }
    if (m_frameBegun) {
        return true;
    }

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    m_frameState = {XR_TYPE_FRAME_STATE};
    m_frameState.shouldRender = XR_TRUE;
    XrResult res = m_dispatch->WaitFrame(m_session, &waitInfo, &m_frameState);
    if (res != XR_SUCCESS) {
        LogError("OpenXrManager::BeginFrame: WaitFrame failed (result " + std::to_string(res) + ")");
        return false;
    }

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    res = m_dispatch->BeginFrame(m_session, &beginInfo);
    if (res != XR_SUCCESS) {
        LogError("OpenXrManager::BeginFrame: BeginFrame failed (result " + std::to_string(res) + ")");
        return false;
    }

    m_frameBegun = true;
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
    if (res != XR_SUCCESS) {
        LogError("OpenXrManager::EndFrame: EndFrame failed (result " + std::to_string(res) + ")");
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

    // Cache the captured head pose at the moment this eye's frame is copied
    m_capturedEyePoses[eyeIndex] = m_lastPose;

    XrSwapchain swapchain = m_eyeSwapchains[eyeIndex].handle;
    if (swapchain == XR_NULL_HANDLE) {
        return false;
    }

    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t imageIndex = 0;
    XrResult res = m_dispatch->AcquireSwapchainImage(swapchain, &acquireInfo, &imageIndex);
    if (res != XR_SUCCESS) {
        LogError("OpenXrManager::CopyBackBufferToEye: AcquireSwapchainImage failed (result " + std::to_string(res) + ")");
        return false;
    }

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    res = m_dispatch->WaitSwapchainImage(swapchain, &waitInfo);
    if (res != XR_SUCCESS) {
        LogError("OpenXrManager::CopyBackBufferToEye: WaitSwapchainImage failed (result " + std::to_string(res) + ")");
        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        m_dispatch->ReleaseSwapchainImage(swapchain, &releaseInfo);
        return false;
    }

    // If backBuffer, device, and queue are present, perform D3D12 command list recording & execution
    if (backBuffer && m_device && m_queue) {
        if (!m_copyAllocator) {
            m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_copyAllocator));
        }
        if (!m_copyCommandList && m_copyAllocator) {
            m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_copyAllocator.Get(), nullptr, IID_PPV_ARGS(&m_copyCommandList));
        }
        if (!m_copyFence) {
            m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_copyFence));
            m_copyFenceValue = 0;
            if (!m_copyFenceEvent) {
                m_copyFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            }
        }

        if (m_copyCommandList && m_copyAllocator) {
            // Synchronize with previous copy execution to prevent command allocator reuse while GPU is reading
            if (m_copyFence && m_copyFenceValue > 0) {
                if (m_copyFence->GetCompletedValue() < m_copyFenceValue) {
                    if (m_copyFenceEvent) {
                        m_copyFence->SetEventOnCompletion(m_copyFenceValue, m_copyFenceEvent);
                        WaitForSingleObject(m_copyFenceEvent, INFINITE);
                    }
                }
            }

            m_copyAllocator->Reset();
            m_copyCommandList->Reset(m_copyAllocator.Get(), nullptr);

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
                barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

                m_copyCommandList->ResourceBarrier(2, barriers);
                m_copyCommandList->CopyResource(destTexture, backBuffer);

                std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
                std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
                m_copyCommandList->ResourceBarrier(2, barriers);

                m_copyCommandList->Close();
                ID3D12CommandList* lists[] = { m_copyCommandList.Get() };
                m_queue->ExecuteCommandLists(1, lists);

                if (m_copyFence) {
                    m_copyFenceValue++;
                    m_queue->Signal(m_copyFence.Get(), m_copyFenceValue);
                }
            }
        }
    }

    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    res = m_dispatch->ReleaseSwapchainImage(swapchain, &releaseInfo);
    if (res != XR_SUCCESS) {
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

        projViews[i].fov.angleLeft = m_eyeProjections[i].angleLeft;
        projViews[i].fov.angleRight = m_eyeProjections[i].angleRight;
        projViews[i].fov.angleUp = m_eyeProjections[i].angleUp;
        projViews[i].fov.angleDown = m_eyeProjections[i].angleDown;

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

    XrResult res = m_dispatch->EndFrame(m_session, &endInfo);
    m_frameBegun = false;
    if (res != XR_SUCCESS) {
        LogError("OpenXrManager::SubmitFramePair: EndFrame failed (result " + std::to_string(res) + ")");
        return false;
    }

    m_submittedFramePairCount++;
    m_eyeSwapchains[0].hasCopiedFrame = false;
    m_eyeSwapchains[1].hasCopiedFrame = false;
    LogInfo("OpenXrManager: Synchronously submitted paired stereo projection layers (total pairs: " +
            std::to_string(m_submittedFramePairCount) + ").");
    return true;
}

} // namespace fs25vr
