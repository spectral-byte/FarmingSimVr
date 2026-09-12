#include "openxr_manager.h"
#include "core/logger.h"
#include <vector>
#include <cstring>
#include <sstream>

namespace fs25vr {

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
        std::stringstream ss;
        ss << "OpenXR HMD System acquired: " << m_runtimeInfo.systemName
           << " (System ID: " << m_runtimeInfo.systemId << ")";
        LogInfo(ss.str());
    }

    m_state = OpenXrState::Active;
    LogInfo("OpenXR lifecycle successfully established in Active VR Mode.");
    return true;
}

void OpenXrManager::Shutdown() {
    if (m_instance != XR_NULL_HANDLE) {
        LogInfo("Shutting down OpenXR instance...");
        m_dispatch->DestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }
    m_state = OpenXrState::Uninitialized;
}

} // namespace fs25vr
