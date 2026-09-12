#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#ifndef XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_D3D12
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <memory>
#include <vector>
#include <string>

namespace fs25vr {

class IOpenXrDispatch {
public:
    virtual ~IOpenXrDispatch() = default;

    virtual XrResult EnumerateInstanceExtensionProperties(
        const char* layerName,
        uint32_t propertyCapacityInput,
        uint32_t* propertyCountOutput,
        XrExtensionProperties* properties) = 0;

    virtual XrResult CreateInstance(
        const XrInstanceCreateInfo* createInfo,
        XrInstance* instance) = 0;

    virtual XrResult DestroyInstance(
        XrInstance instance) = 0;

    virtual XrResult GetInstanceProperties(
        XrInstance instance,
        XrInstanceProperties* instanceProperties) = 0;

    virtual XrResult GetSystem(
        XrInstance instance,
        const XrSystemGetInfo* getInfo,
        XrSystemId* systemId) = 0;

    virtual XrResult GetSystemProperties(
        XrInstance instance,
        XrSystemId systemId,
        XrSystemProperties* properties) = 0;

    virtual XrResult EnumerateViewConfigurationViews(
        XrInstance instance,
        XrSystemId systemId,
        XrViewConfigurationType viewConfigurationType,
        uint32_t viewCapacityInput,
        uint32_t* viewCountOutput,
        XrViewConfigurationView* configurationViews) = 0;

    virtual XrResult GetD3D12GraphicsRequirementsKHR(
        XrInstance instance,
        XrSystemId systemId,
        XrGraphicsRequirementsD3D12KHR* graphicsRequirements) = 0;

    virtual XrResult CreateSession(
        XrInstance instance,
        const XrSessionCreateInfo* createInfo,
        XrSession* session) = 0;

    virtual XrResult DestroySession(
        XrSession session) = 0;

    virtual XrResult CreateReferenceSpace(
        XrSession session,
        const XrReferenceSpaceCreateInfo* createInfo,
        XrSpace* space) = 0;

    virtual XrResult DestroySpace(
        XrSpace space) = 0;

    virtual XrResult LocateSpace(
        XrSpace space,
        XrSpace baseSpace,
        XrTime time,
        XrSpaceLocation* location) = 0;

    virtual XrResult LocateViews(
        XrSession session,
        const XrViewLocateInfo* viewLocateInfo,
        XrViewState* viewState,
        uint32_t viewCapacityInput,
        uint32_t* viewCountOutput,
        XrView* views) = 0;

    virtual XrResult EnumerateSwapchainFormats(
        XrSession session,
        uint32_t formatCapacityInput,
        uint32_t* formatCountOutput,
        int64_t* formats) = 0;

    virtual XrResult CreateSwapchain(
        XrSession session,
        const XrSwapchainCreateInfo* createInfo,
        XrSwapchain* swapchain) = 0;

    virtual XrResult DestroySwapchain(
        XrSwapchain swapchain) = 0;

    virtual XrResult EnumerateSwapchainImages(
        XrSwapchain swapchain,
        uint32_t imageCapacityInput,
        uint32_t* imageCountOutput,
        XrSwapchainImageBaseHeader* images) = 0;

    virtual XrResult AcquireSwapchainImage(
        XrSwapchain swapchain,
        const XrSwapchainImageAcquireInfo* acquireInfo,
        uint32_t* index) = 0;

    virtual XrResult WaitSwapchainImage(
        XrSwapchain swapchain,
        const XrSwapchainImageWaitInfo* waitInfo) = 0;

    virtual XrResult ReleaseSwapchainImage(
        XrSwapchain swapchain,
        const XrSwapchainImageReleaseInfo* releaseInfo) = 0;

    virtual XrResult WaitFrame(
        XrSession session,
        const XrFrameWaitInfo* frameWaitInfo,
        XrFrameState* frameState) = 0;

    virtual XrResult BeginFrame(
        XrSession session,
        const XrFrameBeginInfo* frameBeginInfo) = 0;

    virtual XrResult EndFrame(
        XrSession session,
        const XrFrameEndInfo* frameEndInfo) = 0;
};

class NativeOpenXrDispatch : public IOpenXrDispatch {
public:
    XrResult EnumerateInstanceExtensionProperties(
        const char* layerName,
        uint32_t propertyCapacityInput,
        uint32_t* propertyCountOutput,
        XrExtensionProperties* properties) override {
        return xrEnumerateInstanceExtensionProperties(layerName, propertyCapacityInput, propertyCountOutput, properties);
    }

    XrResult CreateInstance(
        const XrInstanceCreateInfo* createInfo,
        XrInstance* instance) override {
        return xrCreateInstance(createInfo, instance);
    }

    XrResult DestroyInstance(
        XrInstance instance) override {
        return xrDestroyInstance(instance);
    }

    XrResult GetInstanceProperties(
        XrInstance instance,
        XrInstanceProperties* instanceProperties) override {
        return xrGetInstanceProperties(instance, instanceProperties);
    }

    XrResult GetSystem(
        XrInstance instance,
        const XrSystemGetInfo* getInfo,
        XrSystemId* systemId) override {
        return xrGetSystem(instance, getInfo, systemId);
    }

    XrResult GetSystemProperties(
        XrInstance instance,
        XrSystemId systemId,
        XrSystemProperties* properties) override {
        return xrGetSystemProperties(instance, systemId, properties);
    }

    XrResult EnumerateViewConfigurationViews(
        XrInstance instance,
        XrSystemId systemId,
        XrViewConfigurationType viewConfigurationType,
        uint32_t viewCapacityInput,
        uint32_t* viewCountOutput,
        XrViewConfigurationView* configurationViews) override {
        return xrEnumerateViewConfigurationViews(
            instance, systemId, viewConfigurationType, viewCapacityInput, viewCountOutput, configurationViews);
    }

    XrResult GetD3D12GraphicsRequirementsKHR(
        XrInstance instance,
        XrSystemId systemId,
        XrGraphicsRequirementsD3D12KHR* graphicsRequirements) override {
        PFN_xrGetD3D12GraphicsRequirementsKHR pfnGetD3D12GraphicsRequirements = nullptr;
        XrResult res = xrGetInstanceProcAddr(
            instance,
            "xrGetD3D12GraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetD3D12GraphicsRequirements));
        if (res != XR_SUCCESS || !pfnGetD3D12GraphicsRequirements) {
            return res != XR_SUCCESS ? res : XR_ERROR_FUNCTION_UNSUPPORTED;
        }
        return pfnGetD3D12GraphicsRequirements(instance, systemId, graphicsRequirements);
    }

    XrResult CreateSession(
        XrInstance instance,
        const XrSessionCreateInfo* createInfo,
        XrSession* session) override {
        return xrCreateSession(instance, createInfo, session);
    }

    XrResult DestroySession(
        XrSession session) override {
        return xrDestroySession(session);
    }

    XrResult CreateReferenceSpace(
        XrSession session,
        const XrReferenceSpaceCreateInfo* createInfo,
        XrSpace* space) override {
        return xrCreateReferenceSpace(session, createInfo, space);
    }

    XrResult DestroySpace(
        XrSpace space) override {
        return xrDestroySpace(space);
    }

    XrResult LocateSpace(
        XrSpace space,
        XrSpace baseSpace,
        XrTime time,
        XrSpaceLocation* location) override {
        return xrLocateSpace(space, baseSpace, time, location);
    }

    XrResult LocateViews(
        XrSession session,
        const XrViewLocateInfo* viewLocateInfo,
        XrViewState* viewState,
        uint32_t viewCapacityInput,
        uint32_t* viewCountOutput,
        XrView* views) override {
        return xrLocateViews(session, viewLocateInfo, viewState, viewCapacityInput, viewCountOutput, views);
    }

    XrResult EnumerateSwapchainFormats(
        XrSession session,
        uint32_t formatCapacityInput,
        uint32_t* formatCountOutput,
        int64_t* formats) override {
        return xrEnumerateSwapchainFormats(session, formatCapacityInput, formatCountOutput, formats);
    }

    XrResult CreateSwapchain(
        XrSession session,
        const XrSwapchainCreateInfo* createInfo,
        XrSwapchain* swapchain) override {
        return xrCreateSwapchain(session, createInfo, swapchain);
    }

    XrResult DestroySwapchain(
        XrSwapchain swapchain) override {
        return xrDestroySwapchain(swapchain);
    }

    XrResult EnumerateSwapchainImages(
        XrSwapchain swapchain,
        uint32_t imageCapacityInput,
        uint32_t* imageCountOutput,
        XrSwapchainImageBaseHeader* images) override {
        return xrEnumerateSwapchainImages(swapchain, imageCapacityInput, imageCountOutput, images);
    }

    XrResult AcquireSwapchainImage(
        XrSwapchain swapchain,
        const XrSwapchainImageAcquireInfo* acquireInfo,
        uint32_t* index) override {
        return xrAcquireSwapchainImage(swapchain, acquireInfo, index);
    }

    XrResult WaitSwapchainImage(
        XrSwapchain swapchain,
        const XrSwapchainImageWaitInfo* waitInfo) override {
        return xrWaitSwapchainImage(swapchain, waitInfo);
    }

    XrResult ReleaseSwapchainImage(
        XrSwapchain swapchain,
        const XrSwapchainImageReleaseInfo* releaseInfo) override {
        return xrReleaseSwapchainImage(swapchain, releaseInfo);
    }

    XrResult WaitFrame(
        XrSession session,
        const XrFrameWaitInfo* frameWaitInfo,
        XrFrameState* frameState) override {
        return xrWaitFrame(session, frameWaitInfo, frameState);
    }

    XrResult BeginFrame(
        XrSession session,
        const XrFrameBeginInfo* frameBeginInfo) override {
        return xrBeginFrame(session, frameBeginInfo);
    }

    XrResult EndFrame(
        XrSession session,
        const XrFrameEndInfo* frameEndInfo) override {
        return xrEndFrame(session, frameEndInfo);
    }
};

} // namespace fs25vr
