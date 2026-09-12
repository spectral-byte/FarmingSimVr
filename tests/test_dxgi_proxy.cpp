#include <gtest/gtest.h>
#include "proxy/dxgi_proxy.h"
#include "proxy/system_dxgi.h"
#include "openxr/openxr_manager.h"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace fs25vr {

class MockFailingOpenXrDispatch : public IOpenXrDispatch {
public:
    XrResult EnumerateInstanceExtensionProperties(const char*, uint32_t, uint32_t*, XrExtensionProperties*) override {
        return XR_ERROR_RUNTIME_UNAVAILABLE;
    }
    XrResult CreateInstance(const XrInstanceCreateInfo*, XrInstance*) override {
        return XR_ERROR_RUNTIME_UNAVAILABLE;
    }
    XrResult DestroyInstance(XrInstance) override { return XR_SUCCESS; }
    XrResult GetInstanceProperties(XrInstance, XrInstanceProperties*) override { return XR_ERROR_RUNTIME_UNAVAILABLE; }
    XrResult GetSystem(XrInstance, const XrSystemGetInfo*, XrSystemId*) override { return XR_ERROR_RUNTIME_UNAVAILABLE; }
    XrResult GetSystemProperties(XrInstance, XrSystemId, XrSystemProperties*) override { return XR_ERROR_RUNTIME_UNAVAILABLE; }
    XrResult GetD3D12GraphicsRequirementsKHR(XrInstance, XrSystemId, XrGraphicsRequirementsD3D12KHR*) override { return XR_ERROR_RUNTIME_UNAVAILABLE; }
    XrResult CreateSession(XrInstance, const XrSessionCreateInfo*, XrSession*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult DestroySession(XrSession) override { return XR_SUCCESS; }
    XrResult CreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo*, XrSpace*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult DestroySpace(XrSpace) override { return XR_SUCCESS; }
    XrResult LocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
};

TEST(DxgiProxyTest, ResolvesSystemDxgiExports) {
    auto& systemDxgi = SystemDxgi::Instance();
    ASSERT_TRUE(systemDxgi.LoadSystemDxgi());

    const char* exportNames[] = {
        "CreateDXGIFactory",
        "CreateDXGIFactory1",
        "CreateDXGIFactory2",
        "DXGIDeclareAdapterRemovalSupport",
        "DXGIGetDebugInterface1",
        "ApplyCompatResolutionQuirking",
        "CompatString",
        "CompatValue",
        "DXGID3D10CreateDevice",
        "DXGID3D10CreateLayeredDevice",
        "DXGID3D10GetLayeredDeviceSize",
        "DXGID3D10RegisterLayers",
        "DXGIDisableVBlankVirtualization",
        "DXGIDumpJournal",
        "DXGIReportAdapterConfiguration",
        "PIXBeginCapture",
        "PIXEndCapture",
        "PIXGetCaptureState",
        "SetAppCompatStringPointer",
        "UpdateHMDEmulationStatus"
    };

    for (const char* name : exportNames) {
        FARPROC proc = systemDxgi.GetExport(name);
        EXPECT_NE(proc, nullptr) << "Failed to resolve system export: " << name;
    }
}

TEST(DxgiProxyTest, ForwardsCreateDXGIFactory1SilentlyInPassThrough) {
    auto mockDispatch = std::make_shared<MockFailingOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    auto systemDxgi = std::shared_ptr<ISystemDxgi>(&SystemDxgi::Instance(), [](auto*) {});

    DxgiProxy proxy(systemDxgi, openXrManager);

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = proxy.CreateDXGIFactory1(IID_PPV_ARGS(&factory));

    EXPECT_EQ(hr, S_OK);
    ASSERT_NE(factory.Get(), nullptr);

    // Verify OpenXR manager entered Pass-Through Mode
    EXPECT_TRUE(openXrManager->IsPassThrough());
    EXPECT_FALSE(openXrManager->IsActive());
}

TEST(DxgiProxyTest, ForwardsCreateDXGIFactory2SilentlyInPassThrough) {
    auto mockDispatch = std::make_shared<MockFailingOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    auto systemDxgi = std::shared_ptr<ISystemDxgi>(&SystemDxgi::Instance(), [](auto*) {});

    DxgiProxy proxy(systemDxgi, openXrManager);

    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = proxy.CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

    EXPECT_EQ(hr, S_OK);
    ASSERT_NE(factory.Get(), nullptr);
    EXPECT_TRUE(openXrManager->IsPassThrough());
}

TEST(DxgiProxyTest, BinaryLoadsAndInterceptsStartupInPassThrough) {
    HMODULE hProxy = LoadLibraryA("dxgi.dll");
    if (!hProxy) {
        hProxy = LoadLibraryA("build/dxgi.dll");
    }
    if (!hProxy) {
        hProxy = LoadLibraryA("build/bin/dxgi.dll");
    }
    ASSERT_NE(hProxy, nullptr) << "Failed to LoadLibrary on dxgi.dll";

    auto pfnCreateDXGIFactory1 = reinterpret_cast<PFN_CreateDXGIFactory1>(
        GetProcAddress(hProxy, "CreateDXGIFactory1")
    );
    ASSERT_NE(pfnCreateDXGIFactory1, nullptr);

    {
        ComPtr<IDXGIFactory1> factory;
        HRESULT hr = pfnCreateDXGIFactory1(IID_PPV_ARGS(&factory));
        EXPECT_EQ(hr, S_OK);
        EXPECT_NE(factory.Get(), nullptr);
    }

    FreeLibrary(hProxy);
}

} // namespace fs25vr
