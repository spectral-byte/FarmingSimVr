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
};

} // namespace fs25vr
