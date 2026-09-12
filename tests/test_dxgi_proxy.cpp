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
    XrResult LocateViews(XrSession, const XrViewLocateInfo*, XrViewState*, uint32_t, uint32_t*, XrView*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult EnumerateSwapchainFormats(XrSession, uint32_t, uint32_t*, int64_t*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult CreateSwapchain(XrSession, const XrSwapchainCreateInfo*, XrSwapchain*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult DestroySwapchain(XrSwapchain) override { return XR_SUCCESS; }
    XrResult EnumerateSwapchainImages(XrSwapchain, uint32_t, uint32_t*, XrSwapchainImageBaseHeader*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult AcquireSwapchainImage(XrSwapchain, const XrSwapchainImageAcquireInfo*, uint32_t*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult WaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult ReleaseSwapchainImage(XrSwapchain, const XrSwapchainImageReleaseInfo*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult WaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult BeginFrame(XrSession, const XrFrameBeginInfo*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult EndFrame(XrSession, const XrFrameEndInfo*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
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

class MockActiveOpenXrDispatch : public IOpenXrDispatch {
public:
    uint32_t acquireSwapchainImageCalls = 0;
    uint32_t waitSwapchainImageCalls = 0;
    uint32_t releaseSwapchainImageCalls = 0;
    uint32_t waitFrameCalls = 0;
    uint32_t beginFrameCalls = 0;
    uint32_t endFrameCalls = 0;

    XrResult EnumerateInstanceExtensionProperties(const char*, uint32_t propertyCapacityInput, uint32_t* propertyCountOutput, XrExtensionProperties* properties) override {
        if (propertyCountOutput) *propertyCountOutput = 1;
        if (propertyCapacityInput > 0 && properties) {
            properties[0].type = XR_TYPE_EXTENSION_PROPERTIES;
            properties[0].next = nullptr;
            strncpy_s(properties[0].extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME, XR_MAX_EXTENSION_NAME_SIZE - 1);
            properties[0].extensionVersion = 1;
        }
        return XR_SUCCESS;
    }
    XrResult CreateInstance(const XrInstanceCreateInfo*, XrInstance* instance) override {
        if (instance) *instance = reinterpret_cast<XrInstance>(static_cast<uintptr_t>(0xDEADBEEF));
        return XR_SUCCESS;
    }
    XrResult DestroyInstance(XrInstance) override { return XR_SUCCESS; }
    XrResult GetInstanceProperties(XrInstance, XrInstanceProperties* props) override {
        if (props) {
            strncpy_s(props->runtimeName, "MockVR Runtime", XR_MAX_RUNTIME_NAME_SIZE - 1);
            props->runtimeVersion = XR_MAKE_VERSION(1, 0, 0);
        }
        return XR_SUCCESS;
    }
    XrResult GetSystem(XrInstance, const XrSystemGetInfo*, XrSystemId* systemId) override {
        if (systemId) *systemId = 12345;
        return XR_SUCCESS;
    }
    XrResult GetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* props) override {
        if (props) {
            props->systemId = 12345;
            props->trackingProperties.orientationTracking = XR_TRUE;
            props->trackingProperties.positionTracking = XR_TRUE;
        }
        return XR_SUCCESS;
    }
    XrResult GetD3D12GraphicsRequirementsKHR(XrInstance, XrSystemId, XrGraphicsRequirementsD3D12KHR* reqs) override {
        if (reqs) {
            reqs->minFeatureLevel = D3D_FEATURE_LEVEL_12_0;
            reqs->adapterLuid = { 0, 0 };
        }
        return XR_SUCCESS;
    }
    XrResult CreateSession(XrInstance, const XrSessionCreateInfo*, XrSession* session) override {
        if (session) *session = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xCAFE0001));
        return XR_SUCCESS;
    }
    XrResult DestroySession(XrSession) override { return XR_SUCCESS; }
    XrResult CreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo*, XrSpace* space) override {
        static uintptr_t s_space = 0x50ACE001;
        if (space) *space = reinterpret_cast<XrSpace>(s_space++);
        return XR_SUCCESS;
    }
    XrResult DestroySpace(XrSpace) override { return XR_SUCCESS; }
    XrResult LocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation* location) override {
        if (location) {
            location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
                                      XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
            location->pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
            location->pose.position = { 0.0f, 1.5f, 0.0f };
        }
        return XR_SUCCESS;
    }
    XrResult LocateViews(XrSession, const XrViewLocateInfo*, XrViewState* viewState, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views) override {
        if (viewCountOutput) *viewCountOutput = 2;
        if (viewState) {
            viewState->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT |
                                        XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
        }
        if (viewCapacityInput >= 2 && views) {
            for (uint32_t i = 0; i < 2; ++i) {
                views[i].type = XR_TYPE_VIEW;
                views[i].next = nullptr;
                views[i].pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
                views[i].pose.position = { (i == 0 ? -0.032f : 0.032f), 1.5f, 0.0f };
                views[i].fov = { -0.90f, 0.80f, 0.85f, -0.85f };
            }
        }
        return XR_SUCCESS;
    }
    XrResult EnumerateSwapchainFormats(XrSession, uint32_t formatCapacityInput, uint32_t* formatCountOutput, int64_t* formats) override {
        if (formatCountOutput) *formatCountOutput = 1;
        if (formatCapacityInput > 0 && formats) {
            formats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        }
        return XR_SUCCESS;
    }
    XrResult CreateSwapchain(XrSession, const XrSwapchainCreateInfo*, XrSwapchain* swapchain) override {
        static uintptr_t s_chain = 0x54AC0001;
        if (swapchain) *swapchain = reinterpret_cast<XrSwapchain>(s_chain++);
        return XR_SUCCESS;
    }
    XrResult DestroySwapchain(XrSwapchain) override { return XR_SUCCESS; }
    XrResult EnumerateSwapchainImages(XrSwapchain, uint32_t imageCapacityInput, uint32_t* imageCountOutput, XrSwapchainImageBaseHeader* images) override {
        if (imageCountOutput) *imageCountOutput = 2;
        if (imageCapacityInput >= 2 && images) {
            for (uint32_t i = 0; i < 2; ++i) {
                auto* d3d12Image = reinterpret_cast<XrSwapchainImageD3D12KHR*>(&images[i]);
                d3d12Image->type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
                d3d12Image->next = nullptr;
                d3d12Image->texture = nullptr;
            }
        }
        return XR_SUCCESS;
    }
    XrResult AcquireSwapchainImage(XrSwapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index) override {
        acquireSwapchainImageCalls++;
        if (index) *index = 0;
        return XR_SUCCESS;
    }
    XrResult WaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo*) override {
        waitSwapchainImageCalls++;
        return XR_SUCCESS;
    }
    XrResult ReleaseSwapchainImage(XrSwapchain, const XrSwapchainImageReleaseInfo*) override {
        releaseSwapchainImageCalls++;
        return XR_SUCCESS;
    }
    XrResult WaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* frameState) override {
        waitFrameCalls++;
        if (frameState) {
            frameState->type = XR_TYPE_FRAME_STATE;
            frameState->predictedDisplayTime = 1000000;
            frameState->predictedDisplayPeriod = 11111111;
            frameState->shouldRender = XR_TRUE;
        }
        return XR_SUCCESS;
    }
    XrResult BeginFrame(XrSession, const XrFrameBeginInfo*) override {
        beginFrameCalls++;
        return XR_SUCCESS;
    }
    XrResult EndFrame(XrSession, const XrFrameEndInfo*) override {
        endFrameCalls++;
        return XR_SUCCESS;
    }
};

struct MockComObject {
    void** vtable;
};

static int s_mockPresentCalls = 0;
__declspec(noinline) HRESULT STDMETHODCALLTYPE MockPresent(IDXGISwapChain*, UINT, UINT) {
    volatile int a = 1;
    volatile int b = 2;
    volatile int c = a + b;
    s_mockPresentCalls += c;
    return S_OK;
}

static int s_mockCreateSwapChainCalls = 0;
__declspec(noinline) HRESULT STDMETHODCALLTYPE MockCreateSwapChain(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**) {
    volatile int a = 3;
    volatile int b = 4;
    volatile int c = a + b;
    s_mockCreateSwapChainCalls += c;
    return S_OK;
}

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockQueryInterface(IUnknown*, REFIID, void** ppv) {
    if (ppv) *ppv = nullptr;
    return E_NOINTERFACE;
}
__declspec(noinline) ULONG STDMETHODCALLTYPE MockAddRef(IUnknown*) { return 2; }
__declspec(noinline) ULONG STDMETHODCALLTYPE MockRelease(IUnknown*) { return 1; }

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockGetDevice(IDXGISwapChain*, REFIID, void** ppv) {
    if (ppv) *ppv = nullptr;
    return E_NOINTERFACE;
}

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockGetDesc(IDXGISwapChain*, DXGI_SWAP_CHAIN_DESC* pDesc) {
    if (pDesc) {
        pDesc->BufferDesc.Width = 1920;
        pDesc->BufferDesc.Height = 1080;
        pDesc->BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    return S_OK;
}

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockGetBuffer(IDXGISwapChain*, UINT, REFIID, void** ppv) {
    if (ppv) *ppv = nullptr;
    return S_OK;
}

TEST(DxgiProxyTest, OnPresentCopiesLeftEyeOnFrame0AndRightEyeOnFrame1WithPairSubmission) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);

    // Frame 0 (even, left eye, parity = 0)
    proxy.OnPresent(nullptr, 0, 0);

    EXPECT_TRUE(openXrManager->GetEyeSwapchain(0).hasCopiedFrame);
    EXPECT_FALSE(openXrManager->GetEyeSwapchain(1).hasCopiedFrame);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u); // parity advanced to 1

    // Frame 1 (odd, right eye, parity = 1)
    proxy.OnPresent(nullptr, 0, 0);

    EXPECT_EQ(mockDispatch->endFrameCalls, 1u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u); // parity advanced back to 0
}

TEST(DxgiProxyTest, AttachSwapChainHooksPresentAndInvokesOnPresent) {
    s_mockPresentCalls = 0;
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    std::vector<void*> vtable(30, nullptr);
    vtable[0] = reinterpret_cast<void*>(&MockQueryInterface);
    vtable[1] = reinterpret_cast<void*>(&MockAddRef);
    vtable[2] = reinterpret_cast<void*>(&MockRelease);
    vtable[7] = reinterpret_cast<void*>(&MockGetDevice);
    vtable[kPresentVTableSlot] = reinterpret_cast<void*>(&MockPresent);
    vtable[9] = reinterpret_cast<void*>(&MockGetBuffer);
    vtable[12] = reinterpret_cast<void*>(&MockGetDesc);
    MockComObject mockSwapChain{ vtable.data() };

    ASSERT_TRUE(proxy.AttachSwapChain(reinterpret_cast<IDXGISwapChain*>(&mockSwapChain)));
    EXPECT_TRUE(proxy.IsSwapChainAttached());

    auto pSwapChain = reinterpret_cast<IDXGISwapChain*>(&mockSwapChain);
    void** hookedVTable = *reinterpret_cast<void***>(pSwapChain);
    auto pfnPresent = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT)>(hookedVTable[kPresentVTableSlot]);

    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
    HRESULT hr = pfnPresent(pSwapChain, 1, 0);
    EXPECT_EQ(hr, S_OK);
    EXPECT_GT(s_mockPresentCalls, 0);
    EXPECT_TRUE(openXrManager->GetEyeSwapchain(0).hasCopiedFrame);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);

    proxy.DetachAll();
    EXPECT_FALSE(proxy.IsSwapChainAttached());
}

TEST(DxgiProxyTest, AttachFactoryHooksCreateSwapChain) {
    s_mockCreateSwapChainCalls = 0;
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    std::vector<void*> vtable(30, nullptr);
    vtable[0] = reinterpret_cast<void*>(&MockQueryInterface);
    vtable[1] = reinterpret_cast<void*>(&MockAddRef);
    vtable[2] = reinterpret_cast<void*>(&MockRelease);
    vtable[kCreateSwapChainVTableSlot] = reinterpret_cast<void*>(&MockCreateSwapChain);
    MockComObject mockFactory{ vtable.data() };

    ASSERT_TRUE(proxy.AttachFactory(reinterpret_cast<IDXGIFactory*>(&mockFactory)));
    EXPECT_TRUE(proxy.IsFactoryAttached());

    auto pFactory = reinterpret_cast<IDXGIFactory*>(&mockFactory);
    void** hookedVTable = *reinterpret_cast<void***>(pFactory);
    auto pfnCreate = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**)>(hookedVTable[kCreateSwapChainVTableSlot]);

    HRESULT hr = pfnCreate(pFactory, nullptr, nullptr, nullptr);
    EXPECT_EQ(hr, S_OK);
    EXPECT_GT(s_mockCreateSwapChainCalls, 0);

    proxy.DetachAll();
    EXPECT_FALSE(proxy.IsFactoryAttached());
}

} // namespace fs25vr

