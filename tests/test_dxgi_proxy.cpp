#include <gtest/gtest.h>
#include "proxy/dxgi_proxy.h"
#include "proxy/system_dxgi.h"
#include "openxr/openxr_manager.h"
#include "ngx/ngx_interceptor.h"
#include <wrl/client.h>
#include <unordered_set>

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
    XrResult EnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t, uint32_t*, XrViewConfigurationView*) override { return XR_ERROR_RUNTIME_UNAVAILABLE; }
    XrResult GetD3D12GraphicsRequirementsKHR(XrInstance, XrSystemId, XrGraphicsRequirementsD3D12KHR*) override { return XR_ERROR_RUNTIME_UNAVAILABLE; }
    XrResult CreateSession(XrInstance, const XrSessionCreateInfo*, XrSession*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult BeginSession(XrSession, const XrSessionBeginInfo*) override { return XR_ERROR_FUNCTION_UNSUPPORTED; }
    XrResult EndSession(XrSession) override { return XR_SUCCESS; }
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
    // The DLL carries its own NgxInterceptor; drop this binary's LoadLibrary hooks first so the two
    // copies do not hook the same kernel32 entry points on top of each other.
    NgxInterceptor::Instance().DetachAll();
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
    uint32_t mockRecommendedViewWidth[2] = { 2160, 2160 };
    uint32_t mockRecommendedViewHeight[2] = { 2160, 2160 };
    XrResult EnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrViewConfigurationView* configurationViews) override {
        if (viewCountOutput) *viewCountOutput = 2;
        if (viewCapacityInput >= 2 && configurationViews) {
            for (uint32_t i = 0; i < 2; ++i) {
                configurationViews[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
                configurationViews[i].next = nullptr;
                configurationViews[i].recommendedImageRectWidth = mockRecommendedViewWidth[i];
                configurationViews[i].recommendedImageRectHeight = mockRecommendedViewHeight[i];
                configurationViews[i].maxImageRectWidth = 4096;
                configurationViews[i].maxImageRectHeight = 4096;
                configurationViews[i].recommendedSwapchainSampleCount = 1;
                configurationViews[i].maxSwapchainSampleCount = 4;
            }
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
    XrResult BeginSession(XrSession, const XrSessionBeginInfo*) override { return XR_SUCCESS; }
    XrResult EndSession(XrSession) override { return XR_SUCCESS; }
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
    bool enforceCallOrder = false;
    bool isFrameWaiting = false;
    bool isFrameBegun = false;
    std::unordered_set<XrSwapchain> acquiredSwapchains;
    uint32_t callOrderErrors = 0;

    XrResult AcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index) override {
        acquireSwapchainImageCalls++;
        if (enforceCallOrder && (!isFrameBegun || acquiredSwapchains.count(swapchain) > 0)) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        if (enforceCallOrder) acquiredSwapchains.insert(swapchain);
        if (index) *index = 0;
        return XR_SUCCESS;
    }
    XrResult WaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo*) override {
        waitSwapchainImageCalls++;
        if (enforceCallOrder && (!isFrameBegun || acquiredSwapchains.count(swapchain) == 0)) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        return XR_SUCCESS;
    }
    XrResult ReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*) override {
        releaseSwapchainImageCalls++;
        if (enforceCallOrder && (!isFrameBegun || acquiredSwapchains.count(swapchain) == 0)) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        if (enforceCallOrder) acquiredSwapchains.erase(swapchain);
        return XR_SUCCESS;
    }
    XrResult WaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* frameState) override {
        waitFrameCalls++;
        if (enforceCallOrder && isFrameBegun) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        if (enforceCallOrder) isFrameWaiting = true;
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
        if (enforceCallOrder && (!isFrameWaiting || isFrameBegun)) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        if (enforceCallOrder) {
            isFrameWaiting = false;
            isFrameBegun = true;
        }
        return XR_SUCCESS;
    }
    XrFrameEndInfo lastFrameEndInfo{};
    std::vector<XrCompositionLayerQuad> lastSubmittedQuadLayers;

    XrResult EndFrame(XrSession, const XrFrameEndInfo* frameEndInfo) override {
        endFrameCalls++;
        if (enforceCallOrder && (!isFrameBegun || !acquiredSwapchains.empty())) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        if (enforceCallOrder) isFrameBegun = false;
        if (frameEndInfo) {
            lastFrameEndInfo = *frameEndInfo;
            lastSubmittedQuadLayers.clear();
            if (frameEndInfo->layerCount > 0 && frameEndInfo->layers) {
                for (uint32_t l = 0; l < frameEndInfo->layerCount; ++l) {
                    const auto* baseHeader = frameEndInfo->layers[l];
                    if (baseHeader && baseHeader->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
                        const auto* quadLayer = reinterpret_cast<const XrCompositionLayerQuad*>(baseHeader);
                        if (quadLayer) {
                            lastSubmittedQuadLayers.push_back(*quadLayer);
                        }
                    }
                }
            }
        }
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

static void* s_activeMockDevice = nullptr;
static void* s_activeMockQueue = nullptr;

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockDeviceQI(IUnknown* self, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(ID3D12Device) || riid == __uuidof(IUnknown)) {
        *ppv = self;
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockQueueQI(IUnknown* self, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(ID3D12CommandQueue) || riid == __uuidof(IUnknown)) {
        *ppv = self;
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockQueueGetDevice(ID3D12CommandQueue*, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if ((riid == __uuidof(ID3D12Device) || riid == __uuidof(IUnknown)) && s_activeMockDevice) {
        *ppv = s_activeMockDevice;
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockSwapChainQI(IUnknown* self, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1) || riid == __uuidof(IUnknown)) {
        *ppv = self;
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockSwapChainGetDeviceWithQueue(IDXGISwapChain*, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if ((riid == __uuidof(ID3D12CommandQueue) || riid == __uuidof(IUnknown)) && s_activeMockQueue) {
        *ppv = s_activeMockQueue;
        return S_OK;
    }
    if (riid == __uuidof(ID3D12Device) && s_activeMockDevice) {
        *ppv = s_activeMockDevice;
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

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
    vrBridge->PerformHandshake();
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

TEST(DxgiProxyTest, SequentialDualPassSubPass0SuppressesPresentAndAdvancesSubPass) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::SequentialDualPass);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::SequentialDualPass);
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
    EXPECT_EQ(vrBridge->GetFrameIndex(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);

    // Sub-Pass 0 (Left Eye): copies eye 0, suppresses physical DXGI present, advances subPassIndex to 1 within the same logical frame
    bool presented = proxy.OnPresent(nullptr, 0, 0);

    EXPECT_FALSE(presented);
    EXPECT_TRUE(proxy.IsPresentSuppressed());
    EXPECT_TRUE(openXrManager->GetEyeSwapchain(0).hasCopiedFrame);
    EXPECT_FALSE(openXrManager->GetEyeSwapchain(1).hasCopiedFrame);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(mockDispatch->endFrameCalls, 0u);
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 1u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);
    EXPECT_EQ(vrBridge->GetFrameIndex(), 0u); // Frame index remains on the same logical frame
}

TEST(DxgiProxyTest, SequentialDualPassSubPass1SubmitsPairAndExecutesPresent) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::SequentialDualPass);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    // Sub-Pass 0 (Left Eye)
    EXPECT_FALSE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 1u);
    EXPECT_EQ(vrBridge->GetFrameIndex(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);

    // Sub-Pass 1 (Right Eye): copies eye 1, submits pair, executes physical present, resets subPassIndex to 0 and advances logical frame
    bool presented = proxy.OnPresent(nullptr, 0, 0);
    EXPECT_TRUE(presented);
    EXPECT_FALSE(proxy.IsPresentSuppressed());
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_EQ(mockDispatch->endFrameCalls, 1u);
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
    EXPECT_EQ(vrBridge->GetFrameIndex(), 1u); // Exactly 1 logical frame boundary completed
}

TEST(DxgiProxyTest, AttachSwapChainDetourSuppressesPhysicalPresentOnSubPass0) {
    s_mockPresentCalls = 0;
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::SequentialDualPass);
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
    auto pSwapChain = reinterpret_cast<IDXGISwapChain*>(&mockSwapChain);
    void** hookedVTable = *reinterpret_cast<void***>(pSwapChain);
    auto pfnPresent = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT)>(hookedVTable[kPresentVTableSlot]);

    // Sub-Pass 0: returns S_OK, physical present call is suppressed (s_mockPresentCalls remains 0)
    HRESULT hr0 = pfnPresent(pSwapChain, 1, 0);
    EXPECT_EQ(hr0, S_OK);
    EXPECT_EQ(s_mockPresentCalls, 0);
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 1u);
    EXPECT_TRUE(openXrManager->GetEyeSwapchain(0).hasCopiedFrame);

    // Sub-Pass 1: returns S_OK, physical present call executes (s_mockPresentCalls increments), pair submitted
    HRESULT hr1 = pfnPresent(pSwapChain, 1, 0);
    EXPECT_EQ(hr1, S_OK);
    EXPECT_GT(s_mockPresentCalls, 0);
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    proxy.DetachAll();
}

TEST(DxgiProxyTest, PendingRenderingModeLatchedStrictlyOnCompletedFrameBoundaryFromSequentialDualPassToAfr) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::SequentialDualPass);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    // Sub-Pass 0
    EXPECT_FALSE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 1u);

    // Mode change requested during Sub-Pass 0
    proxy.SetPendingRenderingMode(VRRenderingMode::AFR);
    EXPECT_EQ(proxy.GetPendingRenderingMode(), VRRenderingMode::AFR);
    // Mode must NOT have changed yet because compositor frame is mid-pair
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::SequentialDualPass);

    // Sub-Pass 1 completes the pair
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    // Completed frame boundary: pending mode is latched atomically
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);
    EXPECT_FALSE(proxy.GetPendingRenderingMode().has_value());
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
}

TEST(DxgiProxyTest, PendingRenderingModeLatchedStrictlyOnCompletedFrameBoundaryFromAfrToSequentialDualPass) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::AFR);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    // Queue pending mode change to SequentialDualPass
    proxy.SetPendingRenderingMode(VRRenderingMode::SequentialDualPass);
    EXPECT_EQ(proxy.GetPendingRenderingMode(), VRRenderingMode::SequentialDualPass);
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);

    // AFR Frame 0 (parity = 0, Left Eye): does NOT complete pair
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);
    // Mode must NOT be latched yet!
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);
    EXPECT_EQ(proxy.GetPendingRenderingMode(), VRRenderingMode::SequentialDualPass);

    // AFR Frame 1 (parity = 1, Right Eye): completes pair and calls SubmitFramePair()
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    // Completed frame boundary: pending mode is latched atomically
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::SequentialDualPass);
    EXPECT_FALSE(proxy.GetPendingRenderingMode().has_value());
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);

    // Next pass is now Dual-Pass Sub-Pass 0: suppresses present!
    EXPECT_FALSE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_TRUE(proxy.IsPresentSuppressed());
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 1u);
}

TEST(DxgiProxyTest, LuauSetRenderingModeQueuesPendingModeWhenProxyActiveAndLatchesOnBoundary) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::SequentialDualPass);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    ASSERT_TRUE(vrBridge->Inject(L));

    // Sub-Pass 0 (Left Eye)
    EXPECT_FALSE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 1u);

    // Companion Mod / Luau calls VRBridge.setRenderingMode("AFR") mid-pair
    lua_getglobal(L, "VRBridge");
    lua_getfield(L, -1, "setRenderingMode");
    lua_pushstring(L, "AFR");
    ASSERT_EQ(lua_pcall(L, 1, 0, 0), 0);
    lua_pop(L, 1); // pop VRBridge

    // Mode is queued into DxgiProxy::m_pendingRenderingMode and NOT latched mid-pair
    EXPECT_EQ(proxy.GetPendingRenderingMode(), VRRenderingMode::AFR);
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::SequentialDualPass);

    // Sub-Pass 1 (Right Eye) completes the pair
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    // Latched strictly on completed compositor frame boundary
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);
    EXPECT_FALSE(proxy.GetPendingRenderingMode().has_value());

    lua_close(L);
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
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::AFR);
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

TEST(DxgiProxyTest, OnPresentGatesAfrPacingBehindTelemetryProbe) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto probe = std::make_shared<GraphicsTelemetryProbe>();
    // Case 1: Native stereo detected -> probe->RequiresAfrFallback() is false, HasNativeStereoDetected() is true
    D3D12_VIEWPORT vps[2]{
        { 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f },
        { 1920.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f }
    };
    D3D12_RECT scissors[2]{
        { 0, 0, 1920, 1080 },
        { 1920, 0, 3840, 1080 }
    };
    probe->RecordViewports(2, vps);
    probe->RecordScissorRects(2, scissors);
    probe->OnFrameBoundary();

    ASSERT_TRUE(probe->HasNativeStereoDetected());
    ASSERT_FALSE(probe->RequiresAfrFallback());

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, probe);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::AFR);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, probe, nullptr, vrBridge);

    // In native stereo mode, OnPresent submits rendered stereo frames directly on every frame without suppression
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_FALSE(openXrManager->GetEyeSwapchain(0).hasCopiedFrame);
    EXPECT_FALSE(openXrManager->GetEyeSwapchain(1).hasCopiedFrame);
    EXPECT_EQ(vrBridge->GetFrameIndex(), 1u);

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 2u);
    EXPECT_EQ(vrBridge->GetFrameIndex(), 2u);

    // Case 2: Monoscopic pipeline -> probe->RequiresAfrFallback() is true
    probe->Reset();
    D3D12_VIEWPORT monoVp{ 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f };
    D3D12_RECT monoScissor{ 0, 0, 1920, 1080 };
    for (int i = 0; i < 5; ++i) {
        probe->RecordViewports(1, &monoVp);
        probe->RecordScissorRects(1, &monoScissor);
        probe->OnFrameBoundary();
    }
    ASSERT_TRUE(probe->RequiresAfrFallback());

    openXrManager->ResetSubmittedFramePairCount();
    vrBridge->SetFrameIndex(0);
    vrBridge->SetFrameParity(0);

    // First frame (parity 0): copies eye 0 and advances parity to 1
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_TRUE(openXrManager->GetEyeSwapchain(0).hasCopiedFrame);
    EXPECT_FALSE(openXrManager->GetEyeSwapchain(1).hasCopiedFrame);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);
    EXPECT_EQ(vrBridge->GetFrameIndex(), 1u);

    // Second frame (parity 1): copies eye 1, submits pair, advances parity to 0
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_FALSE(openXrManager->GetEyeSwapchain(0).hasCopiedFrame);
    EXPECT_FALSE(openXrManager->GetEyeSwapchain(1).hasCopiedFrame);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
    EXPECT_EQ(vrBridge->GetFrameIndex(), 2u);

    // Case 3: Inconclusive / probing frames -> presentation remains active and uninterrupted via AFR pacing
    probe->Reset();
    openXrManager->ResetSubmittedFramePairCount();
    vrBridge->SetFrameIndex(0);
    vrBridge->SetFrameParity(0);
    EXPECT_EQ(probe->GetStereoVerdict(), StereoVerdict::Inconclusive);

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_TRUE(openXrManager->GetEyeSwapchain(0).hasCopiedFrame);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
}

TEST(DxgiProxyTest, EnforcesStrictOpenXrLifecycleAndContinuousPresentationAcrossTelemetryStates) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    mockDispatch->enforceCallOrder = true;

    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto probe = std::make_shared<GraphicsTelemetryProbe>();
    auto vrBridge = std::make_shared<VRBridge>(openXrManager, probe);
    vrBridge->PerformHandshake();
    DxgiProxy proxy(nullptr, openXrManager, nullptr, probe, nullptr, vrBridge);

    // 1. Inconclusive probing frames: verify AFR pairs submit without call order error
    proxy.OnPresent(nullptr, 0, 0);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_FALSE(openXrManager->IsFrameBegun());

    // 2. Monoscopic probe verdict (AFR fallback)
    for (int i = 0; i < 5; ++i) {
        D3D12_VIEWPORT monoVp{ 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f };
        D3D12_RECT monoScissor{ 0, 0, 1920, 1080 };
        probe->RecordViewports(1, &monoVp);
        probe->RecordScissorRects(1, &monoScissor);
        probe->OnFrameBoundary();
    }
    ASSERT_TRUE(probe->RequiresAfrFallback());

    proxy.OnPresent(nullptr, 0, 0);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 2u);
    EXPECT_FALSE(openXrManager->IsFrameBegun());

    // 3. Native stereo verdict
    probe->Reset();
    D3D12_VIEWPORT vps[2]{
        { 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f },
        { 1920.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f }
    };
    D3D12_RECT scissors[2]{
        { 0, 0, 1920, 1080 },
        { 1920, 0, 3840, 1080 }
    };
    probe->RecordViewports(2, vps);
    probe->RecordScissorRects(2, scissors);
    probe->OnFrameBoundary();
    ASSERT_TRUE(probe->HasNativeStereoDetected());

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 3u);
    EXPECT_FALSE(openXrManager->IsFrameBegun());

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 4u);
    EXPECT_FALSE(openXrManager->IsFrameBegun());
}

struct MockD3D12Environment {
    std::vector<void*> devVTable;
    std::vector<void*> queueVTable;
    MockComObject mockDevice{ nullptr };
    MockComObject mockQueue{ nullptr };

    MockD3D12Environment() : devVTable(20, nullptr), queueVTable(20, nullptr) {
        devVTable[0] = reinterpret_cast<void*>(&MockDeviceQI);
        devVTable[1] = reinterpret_cast<void*>(&MockAddRef);
        devVTable[2] = reinterpret_cast<void*>(&MockRelease);
        mockDevice.vtable = devVTable.data();

        queueVTable[0] = reinterpret_cast<void*>(&MockQueueQI);
        queueVTable[1] = reinterpret_cast<void*>(&MockAddRef);
        queueVTable[2] = reinterpret_cast<void*>(&MockRelease);
        queueVTable[7] = reinterpret_cast<void*>(&MockQueueGetDevice);
        mockQueue.vtable = queueVTable.data();

        s_activeMockDevice = &mockDevice;
        s_activeMockQueue = &mockQueue;
    }

    ~MockD3D12Environment() {
        s_activeMockDevice = nullptr;
        s_activeMockQueue = nullptr;
    }

    IUnknown* GetDeviceUnknown() { return reinterpret_cast<IUnknown*>(&mockDevice); }
    IUnknown* GetQueueUnknown() { return reinterpret_cast<IUnknown*>(&mockQueue); }
    ID3D12Device* GetDevice() { return reinterpret_cast<ID3D12Device*>(&mockDevice); }
    ID3D12CommandQueue* GetQueue() { return reinterpret_cast<ID3D12CommandQueue*>(&mockQueue); }
};

TEST(DxgiProxyTest, ConsolidatesDeviceAndQueueExtractionFromCommandQueue) {
    MockD3D12Environment env;

    ComPtr<ID3D12Device> extractedDevice;
    ComPtr<ID3D12CommandQueue> extractedQueue;

    EXPECT_TRUE(DxgiProxy::ExtractD3D12DeviceAndQueue(
        env.GetQueueUnknown(), extractedDevice, extractedQueue));
    EXPECT_EQ(extractedQueue.Get(), env.GetQueue());
    EXPECT_EQ(extractedDevice.Get(), env.GetDevice());

    DxgiProxy proxy;
    EXPECT_TRUE(proxy.ResolveD3D12DeviceAndQueue(env.GetQueueUnknown()));
    EXPECT_EQ(proxy.GetD3D12CommandQueue(), env.GetQueue());
    EXPECT_EQ(proxy.GetD3D12Device(), env.GetDevice());
}

TEST(DxgiProxyTest, ConsolidatesDeviceAndQueueExtractionFromDevice) {
    MockD3D12Environment env;

    ComPtr<ID3D12Device> extractedDevice;
    ComPtr<ID3D12CommandQueue> extractedQueue;

    EXPECT_TRUE(DxgiProxy::ExtractD3D12DeviceAndQueue(
        env.GetDeviceUnknown(), extractedDevice, extractedQueue));
    EXPECT_EQ(extractedDevice.Get(), env.GetDevice());
    EXPECT_EQ(extractedQueue.Get(), nullptr);

    DxgiProxy proxy;
    EXPECT_TRUE(proxy.ResolveD3D12DeviceAndQueue(env.GetDeviceUnknown()));
    EXPECT_EQ(proxy.GetD3D12Device(), env.GetDevice());
    EXPECT_EQ(proxy.GetD3D12CommandQueue(), nullptr);
}

TEST(DxgiProxyTest, ConsolidatesDeviceAndQueueExtractionFromSwapChain) {
    MockD3D12Environment env;

    std::vector<void*> scVTable(30, nullptr);
    scVTable[0] = reinterpret_cast<void*>(&MockSwapChainQI);
    scVTable[1] = reinterpret_cast<void*>(&MockAddRef);
    scVTable[2] = reinterpret_cast<void*>(&MockRelease);
    scVTable[7] = reinterpret_cast<void*>(&MockSwapChainGetDeviceWithQueue);
    MockComObject mockSwapChain{ scVTable.data() };

    ComPtr<ID3D12Device> extractedDevice;
    ComPtr<ID3D12CommandQueue> extractedQueue;

    EXPECT_TRUE(DxgiProxy::ExtractD3D12DeviceAndQueue(
        reinterpret_cast<IUnknown*>(&mockSwapChain), extractedDevice, extractedQueue));
    EXPECT_EQ(extractedQueue.Get(), env.GetQueue());
    EXPECT_EQ(extractedDevice.Get(), env.GetDevice());
}

TEST(DxgiProxyTest, CreateSwapChainInterceptorsConsolidateDeviceAndQueueExtraction) {
    MockD3D12Environment env;

    DxgiProxy proxy;
    DxgiProxy::Intercept_CreateSwapChain(nullptr, env.GetQueueUnknown(), nullptr, nullptr);
    EXPECT_EQ(proxy.GetD3D12CommandQueue(), env.GetQueue());
    EXPECT_EQ(proxy.GetD3D12Device(), env.GetDevice());

    proxy.SetD3D12DeviceAndQueue(nullptr, nullptr);
    EXPECT_EQ(proxy.GetD3D12Device(), nullptr);
    EXPECT_EQ(proxy.GetD3D12CommandQueue(), nullptr);

    DxgiProxy::Intercept_CreateSwapChainForHwnd(nullptr, env.GetQueueUnknown(), nullptr, nullptr, nullptr, nullptr, nullptr);
    EXPECT_EQ(proxy.GetD3D12CommandQueue(), env.GetQueue());
    EXPECT_EQ(proxy.GetD3D12Device(), env.GetDevice());
}

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockGetDescZeroDimensions(IDXGISwapChain*, DXGI_SWAP_CHAIN_DESC* pDesc) {
    if (pDesc) {
        pDesc->BufferDesc.Width = 0;
        pDesc->BufferDesc.Height = 0;
        pDesc->BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    return S_OK;
}

TEST(DxgiProxyTest, ConfiguresBackbufferMatchedOpenXrEyeSwapchainDimensionsOnPresent) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    mockDispatch->mockRecommendedViewWidth[0] = 2064;
    mockDispatch->mockRecommendedViewHeight[0] = 2208;
    mockDispatch->mockRecommendedViewWidth[1] = 2160;
    mockDispatch->mockRecommendedViewHeight[1] = 2160;

    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());

    EXPECT_EQ(openXrManager->GetRecommendedEyeWidth(0), 2064u);
    EXPECT_EQ(openXrManager->GetRecommendedEyeHeight(0), 2208u);
    EXPECT_EQ(openXrManager->GetRecommendedEyeWidth(1), 2160u);
    EXPECT_EQ(openXrManager->GetRecommendedEyeHeight(1), 2160u);
    EXPECT_FALSE(openXrManager->HasEyeSwapchains());

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    std::vector<void*> scVTable(30, nullptr);
    scVTable[0] = reinterpret_cast<void*>(&MockSwapChainQI);
    scVTable[1] = reinterpret_cast<void*>(&MockAddRef);
    scVTable[2] = reinterpret_cast<void*>(&MockRelease);
    scVTable[7] = reinterpret_cast<void*>(&MockGetDevice);
    scVTable[kPresentVTableSlot] = reinterpret_cast<void*>(&MockPresent);
    scVTable[9] = reinterpret_cast<void*>(&MockGetBuffer);
    scVTable[12] = reinterpret_cast<void*>(&MockGetDesc); // Returns 1920x1080
    MockComObject mockSwapChain{ scVTable.data() };

    auto pSwapChain = reinterpret_cast<IDXGISwapChain*>(&mockSwapChain);

    proxy.OnPresent(pSwapChain, 0, 0);

    // Eye swapchains should match backbuffer dimensions (1920x1080) for 1:1 GPU copy
    EXPECT_TRUE(openXrManager->HasEyeSwapchains());
    EXPECT_EQ(openXrManager->GetEyeSwapchain(0).width, 1920u);
    EXPECT_EQ(openXrManager->GetEyeSwapchain(0).height, 1080u);
    EXPECT_EQ(openXrManager->GetEyeSwapchain(1).width, 1920u);
    EXPECT_EQ(openXrManager->GetEyeSwapchain(1).height, 1080u);
}

TEST(DxgiProxyTest, FallsBackToRecommendedDimensionsWhenBackbufferDimensionsUnavailable) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    mockDispatch->mockRecommendedViewWidth[0] = 2064;
    mockDispatch->mockRecommendedViewHeight[0] = 2208;
    mockDispatch->mockRecommendedViewWidth[1] = 2160;
    mockDispatch->mockRecommendedViewHeight[1] = 2160;

    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());

    EXPECT_FALSE(openXrManager->HasEyeSwapchains());

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    std::vector<void*> scVTable(30, nullptr);
    scVTable[0] = reinterpret_cast<void*>(&MockSwapChainQI);
    scVTable[1] = reinterpret_cast<void*>(&MockAddRef);
    scVTable[2] = reinterpret_cast<void*>(&MockRelease);
    scVTable[7] = reinterpret_cast<void*>(&MockGetDevice);
    scVTable[kPresentVTableSlot] = reinterpret_cast<void*>(&MockPresent);
    scVTable[9] = reinterpret_cast<void*>(&MockGetBuffer);
    scVTable[12] = reinterpret_cast<void*>(&MockGetDescZeroDimensions); // Returns 0x0
    MockComObject mockSwapChain{ scVTable.data() };

    auto pSwapChain = reinterpret_cast<IDXGISwapChain*>(&mockSwapChain);

    proxy.OnPresent(pSwapChain, 0, 0);

    // When backbuffer dimensions are 0, fall back to OpenXR recommended dimensions
    EXPECT_TRUE(openXrManager->HasEyeSwapchains());
    EXPECT_EQ(openXrManager->GetEyeSwapchain(0).width, 2064u);
    EXPECT_EQ(openXrManager->GetEyeSwapchain(0).height, 2208u);
    EXPECT_EQ(openXrManager->GetEyeSwapchain(1).width, 2160u);
    EXPECT_EQ(openXrManager->GetEyeSwapchain(1).height, 2160u);
}

TEST(DxgiProxyTest, FallsBackToDefaultDimensionsWhenBothBackbufferAndRecommendedUnavailable) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    mockDispatch->mockRecommendedViewWidth[0] = 0;
    mockDispatch->mockRecommendedViewHeight[0] = 0;
    mockDispatch->mockRecommendedViewWidth[1] = 0;
    mockDispatch->mockRecommendedViewHeight[1] = 0;

    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());

    EXPECT_FALSE(openXrManager->HasEyeSwapchains());

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    std::vector<void*> scVTable(30, nullptr);
    scVTable[0] = reinterpret_cast<void*>(&MockSwapChainQI);
    scVTable[1] = reinterpret_cast<void*>(&MockAddRef);
    scVTable[2] = reinterpret_cast<void*>(&MockRelease);
    scVTable[7] = reinterpret_cast<void*>(&MockGetDevice);
    scVTable[kPresentVTableSlot] = reinterpret_cast<void*>(&MockPresent);
    scVTable[9] = reinterpret_cast<void*>(&MockGetBuffer);
    scVTable[12] = reinterpret_cast<void*>(&MockGetDescZeroDimensions); // Returns 0x0
    MockComObject mockSwapChain{ scVTable.data() };

    auto pSwapChain = reinterpret_cast<IDXGISwapChain*>(&mockSwapChain);

    proxy.OnPresent(pSwapChain, 0, 0);

    // Default fallback is 1920x1080
    EXPECT_TRUE(openXrManager->HasEyeSwapchains());
    EXPECT_EQ(openXrManager->GetEyeSwapchain(0).width, 1920u);
    EXPECT_EQ(openXrManager->GetEyeSwapchain(0).height, 1080u);
    EXPECT_EQ(openXrManager->GetEyeSwapchain(1).width, 1920u);
    EXPECT_EQ(openXrManager->GetEyeSwapchain(1).height, 1080u);
}

TEST(DxgiProxyTest, OnPresentSubmitsFloatingQuadLayerWhenNoCompanionHandshake) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    EXPECT_FALSE(vrBridge->IsCompanionActive());
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);

    proxy.OnPresent(nullptr, 0, 0);

    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(mockDispatch->endFrameCalls, 1u);
    ASSERT_EQ(mockDispatch->lastSubmittedQuadLayers.size(), 1u);

    const auto& quad = mockDispatch->lastSubmittedQuadLayers[0];
    EXPECT_EQ(quad.type, XR_TYPE_COMPOSITION_LAYER_QUAD);
    EXPECT_EQ(quad.space, openXrManager->GetLocalSpace());
    EXPECT_EQ(quad.eyeVisibility, XR_EYE_VISIBILITY_BOTH);
    EXPECT_EQ(quad.subImage.swapchain, openXrManager->GetEyeSwapchain(0).handle);
    EXPECT_FLOAT_EQ(quad.pose.position.x, 0.0f);
    // Dynamic quad auto-centering aligns the quad to the head eye level (1.5f in mock dispatch)
    EXPECT_FLOAT_EQ(quad.pose.position.y, 1.5f);
    EXPECT_FLOAT_EQ(quad.pose.position.z, -1.5f);
    EXPECT_FLOAT_EQ(quad.pose.orientation.w, 1.0f);
    EXPECT_NEAR(quad.size.width, 1.6f, 0.001f);
    EXPECT_NEAR(quad.size.height, 0.9f, 0.001f);
}

TEST(DxgiProxyTest, OnPresentSubmitsQuadContinuouslyWithoutAfrPacing) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    // 4 frames in menu mode -> 4 quad submissions, frame parity never advances to 1
    for (int i = 0; i < 4; ++i) {
        proxy.OnPresent(nullptr, 0, 0);
        EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), static_cast<uint64_t>(i + 1));
        EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
    }
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
}

TEST(DxgiProxyTest, SeamlesslyTransitionsBetweenQuadAndStereoOnCompanionHandshakeAndUnload) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    // 1. Menu mode: 2 frames submit 2 quad layers
    proxy.OnPresent(nullptr, 0, 0);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 2u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);

    // 2. Player enters map: Companion mod handshakes
    vrBridge->PerformHandshake();
    EXPECT_TRUE(vrBridge->IsCompanionActive());

    // In-game AFR: frame 0 copies left, frame 1 copies right and submits stereo pair
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 2u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);

    // 3. Player exits back to menu: Map unloads
    vrBridge->SetCompanionActive(false);
    EXPECT_FALSE(vrBridge->IsCompanionActive());

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 3u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
}

TEST(DxgiProxyTest, DynamicallySwitchesBetweenProjectionAndQuadWhenInGameMenuToggled) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    // Active companion handshake
    vrBridge->PerformHandshake();
    ASSERT_TRUE(vrBridge->IsCompanionActive());
    EXPECT_FALSE(vrBridge->IsMenuVisible());

    // 1. Active gameplay: 2 frames -> 1 stereo pair
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);

    // 2. Player opens in-game pause menu / shop
    vrBridge->SetMenuVisible(true);
    EXPECT_TRUE(vrBridge->IsMenuVisible());
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);

    // Next 3 frames in menu should each submit a floating quad layer immediately
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 2u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 3u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    // 3. Player closes in-game menu: return to active gameplay
    vrBridge->SetMenuVisible(false);
    EXPECT_FALSE(vrBridge->IsMenuVisible());
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);

    // Frame 0 of resumed gameplay: left eye
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 3u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);

    // Frame 1 of resumed gameplay: right eye and pair submit
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 3u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 2u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
}

TEST(DxgiProxyTest, MidPairMenuTransitionDoesNotDeadlockSwapchainsOrLeakFrames) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    vrBridge->PerformHandshake();
    ASSERT_TRUE(vrBridge->IsCompanionActive());

    // Frame 0: render left eye in gameplay
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);

    // Player opens menu before frame 1 renders right eye
    vrBridge->SetMenuVisible(true);
    EXPECT_TRUE(vrBridge->IsMenuVisible());

    // Presentation must handle switching immediately to quad without deadlocking swapchain
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);

    // And dismiss menu
    vrBridge->SetMenuVisible(false);
    EXPECT_FALSE(vrBridge->IsMenuVisible());
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);

    // Resumed gameplay completes a clean pair
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);
}

TEST(DxgiProxyTest, TwoDViewModeSwitchesPresentationBetweenFramePairsAndQuadLayers) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    // Active companion handshake
    vrBridge->PerformHandshake();
    ASSERT_TRUE(vrBridge->IsCompanionActive());
    EXPECT_FALSE(vrBridge->IsMenuVisible());
    EXPECT_FALSE(vrBridge->Is2DViewActive());

    // 1. Active 3D gameplay: 2 frames -> 1 stereo pair
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);

    // 2. Player toggles 2D Floating Window View mode on
    vrBridge->Set2DViewActive(true);
    EXPECT_TRUE(vrBridge->Is2DViewActive());
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);

    // Next 3 frames in 2D Floating Window Mode should each submit a floating quad layer immediately
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 2u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 3u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    // 3. Player toggles 2D Floating Window View mode off: return to active 3D gameplay
    vrBridge->Set2DViewActive(false);
    EXPECT_FALSE(vrBridge->Is2DViewActive());
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);

    // Frame 0 of resumed 3D gameplay: left eye
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 3u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);

    // Frame 1 of resumed 3D gameplay: right eye and pair submit
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 3u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 2u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
}

TEST(DxgiProxyTest, MidPair2DViewTransitionDoesNotDeadlockSwapchainsOrLeakFrames) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    vrBridge->PerformHandshake();
    ASSERT_TRUE(vrBridge->IsCompanionActive());

    // Frame 0: render left eye in gameplay
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);

    // Player switches to 2D Floating Window Mode before frame 1 renders right eye
    vrBridge->Set2DViewActive(true);
    EXPECT_TRUE(vrBridge->Is2DViewActive());

    // Presentation must handle switching immediately to quad without deadlocking swapchain
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);

    // And toggle back to 3D VR mode
    vrBridge->Set2DViewActive(false);
    EXPECT_FALSE(vrBridge->Is2DViewActive());
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);

    // Resumed gameplay completes a clean pair
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);
}

TEST(DxgiProxyTest, TransitionInto2DFloatingWindowModeTriggersQuadAlignment) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    vrBridge->PerformHandshake();
    ASSERT_TRUE(vrBridge->IsCompanionActive());

    // Complete 1 frame pair in 3D VR
    proxy.OnPresent(nullptr, 0, 0);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    // Set head pose with offset
    HeadPose simPose;
    simPose.position = Vector3(2.0f, 1.7f, 3.0f);
    simPose.rotation = EulerAngles(0.0f, 0.0f, 0.0f);
    simPose.isValid = true;
    openXrManager->SetSimulatedPose(simPose);

    // Trigger VR View Toggle Action to activate 2D Floating Window Mode
    vrBridge->Set2DViewActive(true);
    proxy.OnPresent(nullptr, 0, 0);

    EXPECT_TRUE(openXrManager->HasMenuQuadPose());
    XrPosef quadPose = openXrManager->GetLastSubmittedQuadLayer().pose;
    EXPECT_NEAR(quadPose.position.x, 2.0f, 0.001f);
    EXPECT_NEAR(quadPose.position.y, 1.7f, 0.001f);
    EXPECT_NEAR(quadPose.position.z, 1.5f, 0.001f); // 3.0 - 1.5 = 1.5
}

TEST(DxgiProxyTest, TwoDViewModePersistentAcrossInGameMenuTransitions) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    vrBridge->PerformHandshake();
    ASSERT_TRUE(vrBridge->IsCompanionActive());

    // Activate 2D Floating Window Mode
    vrBridge->Set2DViewActive(true);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);

    // Open in-game menu while in 2D Floating Window Mode
    vrBridge->SetMenuVisible(true);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 2u);

    // Close in-game menu: 2D Floating Window Mode remains sticky/active
    vrBridge->SetMenuVisible(false);
    EXPECT_TRUE(vrBridge->Is2DViewActive());
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 3u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);

    // Only when 2D Floating Window Mode is explicitly disabled does it return to 3D VR
    vrBridge->Set2DViewActive(false);
    proxy.OnPresent(nullptr, 0, 0); // Eye 0
    proxy.OnPresent(nullptr, 0, 0); // Eye 1
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 3u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
}

TEST(DxgiProxyTest, TransitionIntoMenuModeTriggersQuadAlignment) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    // Set head pose with offset and yaw
    HeadPose simPose;
    simPose.position = Vector3(1.0f, 1.6f, -2.0f);
    simPose.rotation = EulerAngles(0.0f, 0.0f, 0.0f);
    simPose.isValid = true;
    openXrManager->SetSimulatedPose(simPose);

    // Initial frame in pre-game menu: should align menu quad to simPose
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_TRUE(openXrManager->HasMenuQuadPose());
    XrPosef quadPose = openXrManager->GetLastSubmittedQuadLayer().pose;
    EXPECT_NEAR(quadPose.position.x, 1.0f, 0.001f);
    EXPECT_NEAR(quadPose.position.y, 1.6f, 0.001f);
    EXPECT_NEAR(quadPose.position.z, -3.5f, 0.001f); // -2.0 - 1.5 = -3.5
}

TEST(DxgiProxyTest, OnPresentInitializesCursorSwapchainInMenuMode) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    EXPECT_FALSE(openXrManager->HasCursorSwapchain());

    // Present in menu mode (pre-game menu)
    proxy.OnPresent(nullptr, 0, 0);

    EXPECT_TRUE(openXrManager->HasCursorSwapchain());
    EXPECT_EQ(openXrManager->GetCursorSwapchain().width, 64u);
    EXPECT_EQ(openXrManager->GetCursorSwapchain().height, 64u);
}

TEST(DxgiProxyTest, UpdateMenuCursorStateHandlesNullHwndSafely) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    // Should not crash with null swapchain / null HWND
    EXPECT_NO_THROW(proxy.UpdateMenuCursorState(nullptr));
    EXPECT_FALSE(openXrManager->GetMenuCursorState().visible);
}

TEST(DxgiProxyTest, ScreenToNormalizedClientHandlesNullOrInvalidHwndSafely) {
    float normX = 0.0f, normY = 0.0f;
    POINT pt{ 100, 100 };
    EXPECT_FALSE(DxgiProxy::ScreenToNormalizedClient(nullptr, pt, normX, normY));
    EXPECT_FALSE(DxgiProxy::ScreenToNormalizedClient(reinterpret_cast<HWND>(static_cast<uintptr_t>(0xDEADBEEF)), pt, normX, normY));
}

TEST(DxgiProxyTest, ScreenToNormalizedClientNormalizesAndClipsBounds) {
    HWND testWnd = CreateWindowA("STATIC", "TestWindow", WS_POPUP, 100, 100, 200, 200, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    float normX = -1.0f, normY = -1.0f;
    // Point inside window (client center: screen x=200, y=200)
    POINT centerPt{ 200, 200 };
    EXPECT_TRUE(DxgiProxy::ScreenToNormalizedClient(testWnd, centerPt, normX, normY));
    EXPECT_NEAR(normX, 0.5f, 0.05f);
    EXPECT_NEAR(normY, 0.5f, 0.05f);

    // Point outside client bounds (screen x=50, y=50)
    POINT outsidePt{ 50, 50 };
    EXPECT_FALSE(DxgiProxy::ScreenToNormalizedClient(testWnd, outsidePt, normX, normY));

    DestroyWindow(testWnd);
}

TEST(DxgiProxyTest, OnPresentSubmitsCursorQuadWhenCursorShowing) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, nullptr, vrBridge);

    // Initial frame creates cursor swapchain
    proxy.OnPresent(nullptr, 0, 0);
    ASSERT_TRUE(openXrManager->HasCursorSwapchain());

    // Explicitly set simulated cursor active state on OpenXrManager
    openXrManager->SetMenuCursorState({ true, 0.5f, 0.5f, MenuCursorState::kDefaultCursorSize });

    // Next menu present should submit 2 quad layers
    proxy.OnPresent(nullptr, 0, 0);
    ASSERT_EQ(mockDispatch->lastSubmittedQuadLayers.size(), 2u);

    const auto& cursorQuad = mockDispatch->lastSubmittedQuadLayers[1];
    EXPECT_EQ(cursorQuad.layerFlags, XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
    EXPECT_EQ(cursorQuad.subImage.swapchain, openXrManager->GetCursorSwapchain().handle);
    EXPECT_FLOAT_EQ(cursorQuad.size.width, MenuCursorState::kDefaultCursorSize);
}

TEST(DxgiProxyTest, ComputeClampedMirrorWindowRect_Standard16x9Displays) {
    // 1080p borderless
    RECT work1080{ 0, 0, 1920, 1080 };
    RECT res1080 = DxgiProxy::ComputeClampedMirrorWindowRect(work1080, WS_POPUP, 0, false);
    EXPECT_EQ(res1080.left, 0);
    EXPECT_EQ(res1080.top, 0);
    EXPECT_EQ(res1080.right, 1920);
    EXPECT_EQ(res1080.bottom, 1080);

    // 1440p borderless
    RECT work1440{ 0, 0, 2560, 1440 };
    RECT res1440 = DxgiProxy::ComputeClampedMirrorWindowRect(work1440, WS_POPUP, 0, false);
    EXPECT_EQ(res1440.left, 0);
    EXPECT_EQ(res1440.top, 0);
    EXPECT_EQ(res1440.right, 2560);
    EXPECT_EQ(res1440.bottom, 1440);

    // 4K borderless
    RECT work4k{ 0, 0, 3840, 2160 };
    RECT res4k = DxgiProxy::ComputeClampedMirrorWindowRect(work4k, WS_POPUP, 0, false);
    EXPECT_EQ(res4k.left, 0);
    EXPECT_EQ(res4k.top, 0);
    EXPECT_EQ(res4k.right, 3840);
    EXPECT_EQ(res4k.bottom, 2160);

    // 720p borderless
    RECT work720{ 0, 0, 1280, 720 };
    RECT res720 = DxgiProxy::ComputeClampedMirrorWindowRect(work720, WS_POPUP, 0, false);
    EXPECT_EQ(res720.left, 0);
    EXPECT_EQ(res720.top, 0);
    EXPECT_EQ(res720.right, 1280);
    EXPECT_EQ(res720.bottom, 720);
}

TEST(DxgiProxyTest, ComputeClampedMirrorWindowRect_WindowedStylesPreserveBordersAndFitWorkArea) {
    RECT frameRect{ 0, 0, 0, 0 };
    AdjustWindowRectEx(&frameRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int frameW = frameRect.right - frameRect.left;
    int frameH = frameRect.bottom - frameRect.top;

    std::vector<RECT> workAreas = {
        { 0, 0, 1920, 1040 }, // 1080p display with 40px taskbar
        { 0, 0, 2560, 1400 }, // 1440p display with 40px taskbar
        { 0, 0, 3840, 2100 }  // 4K display with 60px taskbar
    };

    for (const auto& workArea : workAreas) {
        RECT res = DxgiProxy::ComputeClampedMirrorWindowRect(workArea, WS_OVERLAPPEDWINDOW, 0, false);
        int workW = workArea.right - workArea.left;
        int workH = workArea.bottom - workArea.top;
        int outerW = res.right - res.left;
        int outerH = res.bottom - res.top;

        // Outer window must fit strictly within monitor work area
        EXPECT_GE(res.left, workArea.left);
        EXPECT_GE(res.top, workArea.top);
        EXPECT_LE(res.right, workArea.right);
        EXPECT_LE(res.bottom, workArea.bottom);
        EXPECT_LE(outerW, workW);
        EXPECT_LE(outerH, workH);

        // Window must be centered within workArea
        EXPECT_EQ(res.left, workArea.left + (workW - outerW) / 2);
        EXPECT_EQ(res.top, workArea.top + (workH - outerH) / 2);

        // Client area must strictly maintain 16:9 aspect ratio
        int clientW = outerW - frameW;
        int clientH = outerH - frameH;
        EXPECT_GT(clientW, 0);
        EXPECT_GT(clientH, 0);
        EXPECT_LE(std::abs(clientW - (clientH * 16) / 9), 1);
    }
}

TEST(DxgiProxyTest, ComputeClampedMirrorWindowRect_UltrawideAndSuperUltrawideDisplays) {
    // 21:9 2560x1080 borderless
    RECT work2560x1080{ 0, 0, 2560, 1080 };
    RECT res2560 = DxgiProxy::ComputeClampedMirrorWindowRect(work2560x1080, WS_POPUP, 0, false);
    EXPECT_EQ(res2560.left, 320);
    EXPECT_EQ(res2560.top, 0);
    EXPECT_EQ(res2560.right, 2240);
    EXPECT_EQ(res2560.bottom, 1080);
    EXPECT_EQ(res2560.right - res2560.left, 1920);
    EXPECT_EQ(res2560.bottom - res2560.top, 1080);

    // 21:9 3440x1440 borderless
    RECT work3440x1440{ 0, 0, 3440, 1440 };
    RECT res3440 = DxgiProxy::ComputeClampedMirrorWindowRect(work3440x1440, WS_POPUP, 0, false);
    EXPECT_EQ(res3440.left, 440);
    EXPECT_EQ(res3440.top, 0);
    EXPECT_EQ(res3440.right, 3000);
    EXPECT_EQ(res3440.bottom, 1440);
    EXPECT_EQ(res3440.right - res3440.left, 2560);
    EXPECT_EQ(res3440.bottom - res3440.top, 1440);

    // 32:9 5120x1440 super-ultrawide borderless
    RECT work5120x1440{ 0, 0, 5120, 1440 };
    RECT res5120 = DxgiProxy::ComputeClampedMirrorWindowRect(work5120x1440, WS_POPUP, 0, false);
    EXPECT_EQ(res5120.left, 1280);
    EXPECT_EQ(res5120.top, 0);
    EXPECT_EQ(res5120.right, 3840);
    EXPECT_EQ(res5120.bottom, 1440);
    EXPECT_EQ(res5120.right - res5120.left, 2560);
    EXPECT_EQ(res5120.bottom - res5120.top, 1440);
}

TEST(DxgiProxyTest, ComputeClampedMirrorWindowRect_PortraitAndTallDisplays) {
    // 9:16 1080x1920 borderless
    RECT work1080x1920{ 0, 0, 1080, 1920 };
    RECT res1080 = DxgiProxy::ComputeClampedMirrorWindowRect(work1080x1920, WS_POPUP, 0, false);
    EXPECT_EQ(res1080.left, 0);
    EXPECT_EQ(res1080.right, 1080);
    int expectedH1080 = (1080 * 9) / 16; // 607
    EXPECT_EQ(res1080.bottom - res1080.top, expectedH1080);
    EXPECT_EQ(res1080.top, (1920 - expectedH1080) / 2);
    EXPECT_EQ(res1080.bottom, res1080.top + expectedH1080);

    // 9:16 1440x2560 borderless
    RECT work1440x2560{ 0, 0, 1440, 2560 };
    RECT res1440 = DxgiProxy::ComputeClampedMirrorWindowRect(work1440x2560, WS_POPUP, 0, false);
    EXPECT_EQ(res1440.left, 0);
    EXPECT_EQ(res1440.right, 1440);
    int expectedH1440 = (1440 * 9) / 16; // 810
    EXPECT_EQ(res1440.bottom - res1440.top, expectedH1440);
    EXPECT_EQ(res1440.top, (2560 - expectedH1440) / 2);
    EXPECT_EQ(res1440.bottom, res1440.top + expectedH1440);

    // Windowed portrait display with borders
    RECT frameRect{ 0, 0, 0, 0 };
    AdjustWindowRectEx(&frameRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int frameW = frameRect.right - frameRect.left;
    int frameH = frameRect.bottom - frameRect.top;

    RECT workPortraitWindowed{ 0, 0, 1080, 1880 };
    RECT resWindowed = DxgiProxy::ComputeClampedMirrorWindowRect(workPortraitWindowed, WS_OVERLAPPEDWINDOW, 0, false);
    int outerW = resWindowed.right - resWindowed.left;
    int outerH = resWindowed.bottom - resWindowed.top;

    EXPECT_GE(resWindowed.left, 0);
    EXPECT_GE(resWindowed.top, 0);
    EXPECT_LE(resWindowed.right, 1080);
    EXPECT_LE(resWindowed.bottom, 1880);

    EXPECT_EQ(resWindowed.left, (1080 - outerW) / 2);
    EXPECT_EQ(resWindowed.top, (1880 - outerH) / 2);

    int clientW = outerW - frameW;
    int clientH = outerH - frameH;
    EXPECT_EQ(clientW, 1080 - frameW);
    EXPECT_LE(std::abs(clientH - (clientW * 9) / 16), 1);
}

TEST(DxgiProxyTest, ComputeClampedMirrorWindowRect_MultiMonitorOffsetOrigins) {
    // Secondary 1080p monitor positioned to the right of primary
    RECT workRight{ 1920, 0, 3840, 1080 };
    RECT resRight = DxgiProxy::ComputeClampedMirrorWindowRect(workRight, WS_POPUP, 0, false);
    EXPECT_EQ(resRight.left, 1920);
    EXPECT_EQ(resRight.top, 0);
    EXPECT_EQ(resRight.right, 3840);
    EXPECT_EQ(resRight.bottom, 1080);

    // Secondary 1440p monitor positioned to the left and shifted down
    RECT workLeft{ -2560, 100, 0, 1540 };
    RECT resLeft = DxgiProxy::ComputeClampedMirrorWindowRect(workLeft, WS_POPUP, 0, false);
    EXPECT_EQ(resLeft.left, -2560);
    EXPECT_EQ(resLeft.top, 100);
    EXPECT_EQ(resLeft.right, 0);
    EXPECT_EQ(resLeft.bottom, 1540);

    // Secondary ultrawide 21:9 monitor on negative coordinates
    RECT workLeftUltrawide{ -3440, 0, 0, 1440 };
    RECT resLeftUW = DxgiProxy::ComputeClampedMirrorWindowRect(workLeftUltrawide, WS_POPUP, 0, false);
    EXPECT_EQ(resLeftUW.left, -3000);
    EXPECT_EQ(resLeftUW.right, -440);
    EXPECT_EQ(resLeftUW.top, 0);
    EXPECT_EQ(resLeftUW.bottom, 1440);
    EXPECT_EQ(resLeftUW.right - resLeftUW.left, 2560);
    EXPECT_EQ(resLeftUW.bottom - resLeftUW.top, 1440);
}

TEST(DxgiProxyTest, ComputeClampedMirrorWindowRect_DegenerateAndHwndOverload) {
    // Zero or inverted work area
    RECT zeroWork{ 100, 100, 100, 100 };
    RECT resZero = DxgiProxy::ComputeClampedMirrorWindowRect(zeroWork, WS_POPUP, 0, false);
    EXPECT_EQ(resZero.left, 100);
    EXPECT_EQ(resZero.right, 100);

    RECT invertedWork{ 200, 200, 100, 100 };
    RECT resInverted = DxgiProxy::ComputeClampedMirrorWindowRect(invertedWork, WS_POPUP, 0, false);
    EXPECT_EQ(resInverted.left, 200);
    EXPECT_EQ(resInverted.right, 100);

    // Work area smaller than non-client frame borders
    RECT tinyWork{ 0, 0, 10, 10 };
    RECT resTiny = DxgiProxy::ComputeClampedMirrorWindowRect(tinyWork, WS_OVERLAPPEDWINDOW, 0, false);
    EXPECT_EQ(resTiny.left, 0);
    EXPECT_EQ(resTiny.top, 0);
    EXPECT_EQ(resTiny.right, 10);
    EXPECT_EQ(resTiny.bottom, 10);

    // HWND overload: null and invalid handles return zero RECT
    RECT nullRes = DxgiProxy::ComputeClampedMirrorWindowRect(static_cast<HWND>(nullptr));
    EXPECT_EQ(nullRes.left, 0);
    EXPECT_EQ(nullRes.top, 0);
    EXPECT_EQ(nullRes.right, 0);
    EXPECT_EQ(nullRes.bottom, 0);

    HWND invalidHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0xDEADBEEF));
    RECT invalidRes = DxgiProxy::ComputeClampedMirrorWindowRect(invalidHwnd);
    EXPECT_EQ(invalidRes.left, 0);
    EXPECT_EQ(invalidRes.top, 0);
    EXPECT_EQ(invalidRes.right, 0);
    EXPECT_EQ(invalidRes.bottom, 0);

    // HWND overload: real window
    HWND testWnd = CreateWindowA("STATIC", "ClampedMirrorTest", WS_OVERLAPPEDWINDOW, 50, 50, 4000, 3000, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    HMONITOR hMon = MonitorFromWindow(testWnd, MONITOR_DEFAULTTONEAREST);
    ASSERT_TRUE(hMon != nullptr);
    MONITORINFO mi{ sizeof(MONITORINFO) };
    ASSERT_TRUE(GetMonitorInfo(hMon, &mi));

    RECT hwndRes = DxgiProxy::ComputeClampedMirrorWindowRect(testWnd);
    int outerW = hwndRes.right - hwndRes.left;
    int outerH = hwndRes.bottom - hwndRes.top;
    int workW = mi.rcWork.right - mi.rcWork.left;
    int workH = mi.rcWork.bottom - mi.rcWork.top;

    EXPECT_LE(outerW, workW);
    EXPECT_LE(outerH, workH);
    EXPECT_GE(hwndRes.left, mi.rcWork.left);
    EXPECT_GE(hwndRes.top, mi.rcWork.top);
    EXPECT_LE(hwndRes.right, mi.rcWork.right);
    EXPECT_LE(hwndRes.bottom, mi.rcWork.bottom);

    DestroyWindow(testWnd);
}

TEST(DxgiProxyTest, WindowSubclassAttachesInActiveVrModeAndRemainsInertInPassThrough) {
    HWND testWnd = CreateWindowA("STATIC", "SubclassTestWnd", WS_OVERLAPPEDWINDOW, 50, 50, 4000, 3000, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    // 1. Pass-Through Mode: must remain completely inert
    {
        auto mockFailingDispatch = std::make_shared<MockFailingOpenXrDispatch>();
        auto ptOpenXrManager = std::make_shared<OpenXrManager>(mockFailingDispatch);
        ASSERT_TRUE(ptOpenXrManager->Initialize());
        EXPECT_TRUE(ptOpenXrManager->IsPassThrough());
        EXPECT_FALSE(ptOpenXrManager->IsActive());

        DxgiProxy ptProxy(nullptr, ptOpenXrManager);
        ptProxy.SetGameHwnd(testWnd);

        EXPECT_FALSE(ptProxy.IsWindowSubclassed());
        EXPECT_FALSE(ptProxy.AttachWindowSubclass(testWnd));
    }

    // 2. Active VR Mode: attaches subclass cleanly
    {
        auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
        auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
        ASSERT_TRUE(activeOpenXrManager->Initialize());
        ASSERT_TRUE(activeOpenXrManager->CreateSession());
        EXPECT_TRUE(activeOpenXrManager->IsActive());

        DxgiProxy activeProxy(nullptr, activeOpenXrManager);
        activeProxy.SetGameHwnd(testWnd);

        EXPECT_TRUE(activeProxy.IsWindowSubclassed());
        EXPECT_EQ(activeProxy.GetGameHwnd(), testWnd);

        activeProxy.DetachAll();
        EXPECT_FALSE(activeProxy.IsWindowSubclassed());
    }

    DestroyWindow(testWnd);
}

TEST(DxgiProxyTest, SubclassInterceptsWindowPosChangingAndClampsDimensions) {
    HWND testWnd = CreateWindowA("STATIC", "PosChangingTestWnd", WS_OVERLAPPEDWINDOW, 50, 50, 4000, 3000, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(activeOpenXrManager->Initialize());
    ASSERT_TRUE(activeOpenXrManager->CreateSession());

    DxgiProxy activeProxy(nullptr, activeOpenXrManager);
    activeProxy.SetGameHwnd(testWnd);
    ASSERT_TRUE(activeProxy.IsWindowSubclassed());

    RECT expectedClamped = DxgiProxy::ComputeClampedMirrorWindowRect(testWnd);
    int maxW = expectedClamped.right - expectedClamped.left;
    int maxH = expectedClamped.bottom - expectedClamped.top;
    ASSERT_GT(maxW, 0);
    ASSERT_GT(maxH, 0);

    // 1. Requested dimensions exceed calculated bounds (e.g. 5000x4000 or 4K on 1440p)
    WINDOWPOS wpOversized{};
    wpOversized.hwnd = testWnd;
    wpOversized.x = 250;
    wpOversized.y = 350;
    wpOversized.cx = 5000;
    wpOversized.cy = 4000;
    wpOversized.flags = 0;
    SendMessage(testWnd, WM_WINDOWPOSCHANGING, 0, reinterpret_cast<LPARAM>(&wpOversized));

    EXPECT_EQ(wpOversized.cx, maxW);
    EXPECT_EQ(wpOversized.cy, maxH);
    EXPECT_EQ(wpOversized.x, 250); // Preserves user movement coordinates
    EXPECT_EQ(wpOversized.y, 350);

    // 2. Requested dimensions smaller than bounds are preserved
    WINDOWPOS wpSmall{};
    wpSmall.hwnd = testWnd;
    wpSmall.cx = maxW / 2;
    wpSmall.cy = maxH / 2;
    wpSmall.flags = 0;
    SendMessage(testWnd, WM_WINDOWPOSCHANGING, 0, reinterpret_cast<LPARAM>(&wpSmall));

    EXPECT_EQ(wpSmall.cx, maxW / 2);
    EXPECT_EQ(wpSmall.cy, maxH / 2);

    // 3. Messages with SWP_NOSIZE do not alter cx or cy
    WINDOWPOS wpNoSize{};
    wpNoSize.hwnd = testWnd;
    wpNoSize.cx = 9999;
    wpNoSize.cy = 9999;
    wpNoSize.flags = SWP_NOSIZE;
    SendMessage(testWnd, WM_WINDOWPOSCHANGING, 0, reinterpret_cast<LPARAM>(&wpNoSize));

    EXPECT_EQ(wpNoSize.cx, 9999);
    EXPECT_EQ(wpNoSize.cy, 9999);

    DestroyWindow(testWnd);
}

TEST(DxgiProxyTest, InterceptCreateSwapChainForHwndAppliesInitialPrePresentationClamp) {
    HWND testWnd = CreateWindowA("STATIC", "InitialClampTestWnd", WS_OVERLAPPEDWINDOW, 50, 50, 4000, 3000, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    // 1. Active VR Mode: applies pre-presentation clamp immediately
    {
        auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
        auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
        ASSERT_TRUE(activeOpenXrManager->Initialize());
        ASSERT_TRUE(activeOpenXrManager->CreateSession());

        DxgiProxy activeProxy(nullptr, activeOpenXrManager);

        RECT expectedClamped = DxgiProxy::ComputeClampedMirrorWindowRect(testWnd);
        int expectedW = expectedClamped.right - expectedClamped.left;
        int expectedH = expectedClamped.bottom - expectedClamped.top;

        DxgiProxy::Intercept_CreateSwapChainForHwnd(nullptr, nullptr, testWnd, nullptr, nullptr, nullptr, nullptr);

        EXPECT_TRUE(activeProxy.IsWindowSubclassed());
        EXPECT_EQ(activeProxy.GetGameHwnd(), testWnd);

        RECT currentRect{};
        GetWindowRect(testWnd, &currentRect);
        EXPECT_EQ(currentRect.right - currentRect.left, expectedW);
        EXPECT_EQ(currentRect.bottom - currentRect.top, expectedH);
        EXPECT_EQ(currentRect.left, expectedClamped.left);
        EXPECT_EQ(currentRect.top, expectedClamped.top);
    }
    DestroyWindow(testWnd);

    // 2. Pass-Through Mode: remains inert, leaving window at initial size
    HWND ptWnd = CreateWindowA("STATIC", "PassThroughClampTestWnd", WS_OVERLAPPEDWINDOW, 50, 50, 800, 600, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(ptWnd != nullptr);
    {
        RECT origRect{};
        GetWindowRect(ptWnd, &origRect);

        auto mockFailingDispatch = std::make_shared<MockFailingOpenXrDispatch>();
        auto ptOpenXrManager = std::make_shared<OpenXrManager>(mockFailingDispatch);
        ASSERT_TRUE(ptOpenXrManager->Initialize());
        EXPECT_TRUE(ptOpenXrManager->IsPassThrough());

        DxgiProxy ptProxy(nullptr, ptOpenXrManager);

        DxgiProxy::Intercept_CreateSwapChainForHwnd(nullptr, nullptr, ptWnd, nullptr, nullptr, nullptr, nullptr);

        EXPECT_FALSE(ptProxy.IsWindowSubclassed());

        RECT ptRect{};
        GetWindowRect(ptWnd, &ptRect);
        EXPECT_EQ(ptRect.right - ptRect.left, origRect.right - origRect.left);
        EXPECT_EQ(ptRect.bottom - ptRect.top, origRect.bottom - origRect.top);
        EXPECT_EQ(ptRect.left, origRect.left);
        EXPECT_EQ(ptRect.top, origRect.top);
    }
    DestroyWindow(ptWnd);
}

TEST(DxgiProxyTest, SubclassLifecycleTeardownOnNcDestroyAndDetachAll) {
    // 1. WM_NCDESTROY automatically detaches subclass
    HWND testWnd1 = CreateWindowA("STATIC", "NcDestroyTestWnd", WS_OVERLAPPEDWINDOW, 50, 50, 1000, 800, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd1 != nullptr);
    {
        auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
        auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
        ASSERT_TRUE(activeOpenXrManager->Initialize());
        ASSERT_TRUE(activeOpenXrManager->CreateSession());

        DxgiProxy activeProxy(nullptr, activeOpenXrManager);
        activeProxy.SetGameHwnd(testWnd1);
        EXPECT_TRUE(activeProxy.IsWindowSubclassed());

        DestroyWindow(testWnd1);
        EXPECT_FALSE(activeProxy.IsWindowSubclassed());
        EXPECT_EQ(activeProxy.GetGameHwnd(), nullptr);
    }

    // 2. DetachAll cleanly unhooks subclass before window destruction
    HWND testWnd2 = CreateWindowA("STATIC", "DetachAllTestWnd", WS_OVERLAPPEDWINDOW, 50, 50, 1000, 800, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd2 != nullptr);
    {
        auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
        auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
        ASSERT_TRUE(activeOpenXrManager->Initialize());
        ASSERT_TRUE(activeOpenXrManager->CreateSession());

        DxgiProxy activeProxy(nullptr, activeOpenXrManager);
        activeProxy.SetGameHwnd(testWnd2);
        EXPECT_TRUE(activeProxy.IsWindowSubclassed());

        activeProxy.DetachAll();
        EXPECT_FALSE(activeProxy.IsWindowSubclassed());

        DestroyWindow(testWnd2);
    }
}

TEST(DxgiProxyTest, InterceptCreateSwapChainForHwndEnforcesStretchScalingInActiveVrMode) {
    // 1. Active VR Mode: mutates pDesc->Scaling to DXGI_SCALING_STRETCH
    {
        auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
        auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
        ASSERT_TRUE(activeOpenXrManager->Initialize());
        ASSERT_TRUE(activeOpenXrManager->CreateSession());
        EXPECT_TRUE(activeOpenXrManager->IsActive());

        DxgiProxy activeProxy(nullptr, activeOpenXrManager);

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 3840;
        desc.Height = 2160;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Scaling = DXGI_SCALING_NONE;

        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc{};
        fullscreenDesc.Windowed = FALSE;

        DxgiProxy::Intercept_CreateSwapChainForHwnd(nullptr, nullptr, nullptr, &desc, &fullscreenDesc, nullptr, nullptr);

        EXPECT_EQ(desc.Scaling, DXGI_SCALING_STRETCH);
        EXPECT_TRUE(fullscreenDesc.Windowed == TRUE);
    }

    // 2. Pass-Through Mode: leaves pDesc->Scaling unchanged
    {
        auto mockFailingDispatch = std::make_shared<MockFailingOpenXrDispatch>();
        auto ptOpenXrManager = std::make_shared<OpenXrManager>(mockFailingDispatch);
        ASSERT_TRUE(ptOpenXrManager->Initialize());
        EXPECT_TRUE(ptOpenXrManager->IsPassThrough());
        EXPECT_FALSE(ptOpenXrManager->IsActive());

        DxgiProxy ptProxy(nullptr, ptOpenXrManager);

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 1920;
        desc.Height = 1080;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Scaling = DXGI_SCALING_NONE;

        DxgiProxy::Intercept_CreateSwapChainForHwnd(nullptr, nullptr, nullptr, &desc, nullptr, nullptr, nullptr);

        EXPECT_EQ(desc.Scaling, DXGI_SCALING_NONE);
    }

    // 3. Null pDesc handled gracefully
    {
        auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
        auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
        ASSERT_TRUE(activeOpenXrManager->Initialize());
        ASSERT_TRUE(activeOpenXrManager->CreateSession());

        DxgiProxy activeProxy(nullptr, activeOpenXrManager);
        EXPECT_NO_THROW(DxgiProxy::Intercept_CreateSwapChainForHwnd(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
    }
}

TEST(DxgiProxyTest, ClampedWindowRestoresFullNormalizedMouseRangeAcrossEdges) {
    HWND testWnd = CreateWindowA("STATIC", "ClampedMouseTestWnd", WS_OVERLAPPEDWINDOW, 50, 50, 3840, 2160, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(activeOpenXrManager->Initialize());
    ASSERT_TRUE(activeOpenXrManager->CreateSession());

    DxgiProxy activeProxy(nullptr, activeOpenXrManager);

    // Initial clamp applies and subclasses the window to fit the monitor work area
    EXPECT_TRUE(activeProxy.ApplyInitialWindowClamp(testWnd));
    activeProxy.SetGameHwnd(testWnd);
    EXPECT_TRUE(activeProxy.IsWindowSubclassed());

    RECT clientRect{};
    ASSERT_TRUE(GetClientRect(testWnd, &clientRect));
    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;
    ASSERT_GT(clientWidth, 0);
    ASSERT_GT(clientHeight, 0);

    // Convert client corner points to screen coordinates
    POINT topLeft{ 0, 0 };
    POINT topRight{ clientWidth, 0 };
    POINT bottomLeft{ 0, clientHeight };
    POINT bottomRight{ clientWidth, clientHeight };
    POINT centerPoint{ clientWidth / 2, clientHeight / 2 };

    ASSERT_TRUE(ClientToScreen(testWnd, &topLeft));
    ASSERT_TRUE(ClientToScreen(testWnd, &topRight));
    ASSERT_TRUE(ClientToScreen(testWnd, &bottomLeft));
    ASSERT_TRUE(ClientToScreen(testWnd, &bottomRight));
    ASSERT_TRUE(ClientToScreen(testWnd, &centerPoint));

    // Verify that the clamped Desktop Mirror Window client corners lie within the active monitor work area
    HMONITOR hMonitor = MonitorFromWindow(testWnd, MONITOR_DEFAULTTONEAREST);
    ASSERT_TRUE(hMonitor != nullptr);
    MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
    ASSERT_TRUE(GetMonitorInfo(hMonitor, &monitorInfo));
    EXPECT_GE(topLeft.x, monitorInfo.rcWork.left);
    EXPECT_GE(topLeft.y, monitorInfo.rcWork.top);
    EXPECT_LE(bottomRight.x, monitorInfo.rcWork.right);
    EXPECT_LE(bottomRight.y, monitorInfo.rcWork.bottom);

    // Map screen points to normalized client coordinates
    float normX = 0.0f, normY = 0.0f;

    // Top-Left corner maps to (0.0, 0.0)
    EXPECT_TRUE(DxgiProxy::ScreenToNormalizedClient(testWnd, topLeft, normX, normY));
    EXPECT_FLOAT_EQ(normX, 0.0f);
    EXPECT_FLOAT_EQ(normY, 0.0f);

    // Top-Right corner maps to (1.0, 0.0)
    EXPECT_TRUE(DxgiProxy::ScreenToNormalizedClient(testWnd, topRight, normX, normY));
    EXPECT_FLOAT_EQ(normX, 1.0f);
    EXPECT_FLOAT_EQ(normY, 0.0f);

    // Bottom-Left corner maps to (0.0, 1.0)
    EXPECT_TRUE(DxgiProxy::ScreenToNormalizedClient(testWnd, bottomLeft, normX, normY));
    EXPECT_FLOAT_EQ(normX, 0.0f);
    EXPECT_FLOAT_EQ(normY, 1.0f);

    // Bottom-Right corner maps to (1.0, 1.0)
    EXPECT_TRUE(DxgiProxy::ScreenToNormalizedClient(testWnd, bottomRight, normX, normY));
    EXPECT_FLOAT_EQ(normX, 1.0f);
    EXPECT_FLOAT_EQ(normY, 1.0f);

    // Center maps to (0.5, 0.5)
    EXPECT_TRUE(DxgiProxy::ScreenToNormalizedClient(testWnd, centerPoint, normX, normY));
    EXPECT_NEAR(normX, 0.5f, 0.005f);
    EXPECT_NEAR(normY, 0.5f, 0.005f);

    // Contrast with the 66% clipping issue on an unclamped window:
    // If the window were unclamped at 3840x2160 on a 2560x1440 display,
    // the OS clamps GetCursorPos to monitor boundaries (max screen X = 2560, max screen Y = 1440).
    int unclampedW = 3840;
    int unclampedH = 2160;
    float unclampedMaxNormX = 2560.0f / static_cast<float>(unclampedW);
    float unclampedMaxNormY = 1440.0f / static_cast<float>(unclampedH);
    EXPECT_NEAR(unclampedMaxNormX, 0.6667f, 0.001f);
    EXPECT_NEAR(unclampedMaxNormY, 0.6667f, 0.001f);
    // With resolution decoupling and Desktop Mirror Window clamping, the full [0.0, 1.0] range is restored.

    DestroyWindow(testWnd);
}

TEST(DxgiProxyTest, NormalizedMouseCoordinatesReachAbsoluteMenuQuadBoundariesInVr) {
    // Menu quad centered in front of user
    XrPosef menuQuadPose{};
    menuQuadPose.position = { 0.0f, 0.0f, -1.2f };
    menuQuadPose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

    XrExtent2Df menuQuadSize{ 1.6f, 0.9f }; // 16:9 aspect ratio
    float cursorSize = 0.04f;

    // 1. Clamped Desktop Mirror Window provides full [0.0, 1.0] normalized coordinate range
    float minNormX = 0.0f, maxNormX = 1.0f;
    float minNormY = 0.0f, maxNormY = 1.0f;

    // Top-Left corner: (0.0, 0.0) -> absolute top-left quad boundary
    XrPosef poseTopLeft = OpenXrManager::ComputeCursorQuadPose(menuQuadPose, menuQuadSize, minNormX, minNormY, cursorSize, cursorSize);
    float tipTopLeftX = poseTopLeft.position.x - 0.5f * cursorSize;
    float tipTopLeftY = poseTopLeft.position.y + 0.5f * cursorSize;
    EXPECT_NEAR(tipTopLeftX, -menuQuadSize.width / 2.0f, 1e-4f);
    EXPECT_NEAR(tipTopLeftY, menuQuadSize.height / 2.0f, 1e-4f);

    // Top-Right corner: (1.0, 0.0) -> absolute top-right quad boundary
    XrPosef poseTopRight = OpenXrManager::ComputeCursorQuadPose(menuQuadPose, menuQuadSize, maxNormX, minNormY, cursorSize, cursorSize);
    float tipTopRightX = poseTopRight.position.x - 0.5f * cursorSize;
    float tipTopRightY = poseTopRight.position.y + 0.5f * cursorSize;
    EXPECT_NEAR(tipTopRightX, menuQuadSize.width / 2.0f, 1e-4f);
    EXPECT_NEAR(tipTopRightY, menuQuadSize.height / 2.0f, 1e-4f);

    // Bottom-Left corner: (0.0, 1.0) -> absolute bottom-left quad boundary
    XrPosef poseBottomLeft = OpenXrManager::ComputeCursorQuadPose(menuQuadPose, menuQuadSize, minNormX, maxNormY, cursorSize, cursorSize);
    float tipBottomLeftX = poseBottomLeft.position.x - 0.5f * cursorSize;
    float tipBottomLeftY = poseBottomLeft.position.y + 0.5f * cursorSize;
    EXPECT_NEAR(tipBottomLeftX, -menuQuadSize.width / 2.0f, 1e-4f);
    EXPECT_NEAR(tipBottomLeftY, -menuQuadSize.height / 2.0f, 1e-4f);

    // Bottom-Right corner: (1.0, 1.0) -> absolute bottom-right quad boundary
    XrPosef poseBottomRight = OpenXrManager::ComputeCursorQuadPose(menuQuadPose, menuQuadSize, maxNormX, maxNormY, cursorSize, cursorSize);
    float tipBottomRightX = poseBottomRight.position.x - 0.5f * cursorSize;
    float tipBottomRightY = poseBottomRight.position.y + 0.5f * cursorSize;
    EXPECT_NEAR(tipBottomRightX, menuQuadSize.width / 2.0f, 1e-4f);
    EXPECT_NEAR(tipBottomRightY, -menuQuadSize.height / 2.0f, 1e-4f);

    // Total span reaches 100% of menu quad dimensions
    EXPECT_NEAR(tipTopRightX - tipTopLeftX, menuQuadSize.width, 1e-4f);
    EXPECT_NEAR(tipTopLeftY - tipBottomLeftY, menuQuadSize.height, 1e-4f);

    // 2. Contrast with the 66% clipping issue before resolution decoupling:
    // Without resolution decoupling, normalized cursor coords were clipped at ~0.6667
    float clippedNormX = 2560.0f / 3840.0f; // ~0.6667
    float clippedNormY = 1440.0f / 2160.0f; // ~0.6667
    XrPosef poseClipped = OpenXrManager::ComputeCursorQuadPose(menuQuadPose, menuQuadSize, clippedNormX, clippedNormY, cursorSize, cursorSize);
    float tipClippedX = poseClipped.position.x - 0.5f * cursorSize;
    float tipClippedY = poseClipped.position.y + 0.5f * cursorSize;

    // Rightmost reach was only ~0.267m instead of 0.800m (width/2)
    EXPECT_LT(tipClippedX, menuQuadSize.width / 4.0f);
    // Bottommost reach was only ~-0.150m instead of -0.450m (-height/2)
    EXPECT_GT(tipClippedY, -menuQuadSize.height / 4.0f);
}

TEST(DxgiProxyTest, OnPresentEnforcesClampedMirrorWindowDimensionsWhenOversized) {
    HWND testWnd = CreateWindowA("STATIC", "OnPresentClampWnd", WS_OVERLAPPEDWINDOW, 50, 50, 5000, 4000, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(activeOpenXrManager->Initialize());
    ASSERT_TRUE(activeOpenXrManager->CreateSession());

    DxgiProxy activeProxy(nullptr, activeOpenXrManager);
    activeProxy.SetGameHwnd(testWnd);

    // Initial state: window was deliberately set oversized (e.g. 5000x4000) at (80, 90)
    SetWindowPos(testWnd, nullptr, 80, 90, 5000, 4000, SWP_NOZORDER | SWP_NOACTIVATE);

    RECT expectedClamped = DxgiProxy::ComputeClampedMirrorWindowRect(testWnd);
    int maxW = expectedClamped.right - expectedClamped.left;
    int maxH = expectedClamped.bottom - expectedClamped.top;

    // Simulate frame present
    activeProxy.OnPresent(nullptr, 0, 0);

    RECT curRect{};
    GetWindowRect(testWnd, &curRect);
    EXPECT_EQ(curRect.right - curRect.left, maxW);
    EXPECT_EQ(curRect.bottom - curRect.top, maxH);
    // SWP_NOMOVE ensures window placement is preserved so user can drag/move freely
    EXPECT_EQ(curRect.left, 80);
    EXPECT_EQ(curRect.top, 90);

    // User moves the window to (200, 250); subsequent frames must preserve new position
    SetWindowPos(testWnd, nullptr, 200, 250, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    activeProxy.OnPresent(nullptr, 0, 0);

    GetWindowRect(testWnd, &curRect);
    EXPECT_EQ(curRect.left, 200);
    EXPECT_EQ(curRect.top, 250);
    EXPECT_EQ(curRect.right - curRect.left, maxW);
    EXPECT_EQ(curRect.bottom - curRect.top, maxH);

    activeProxy.DetachAll();
    DestroyWindow(testWnd);
}

TEST(DxgiProxyTest, SubclassHandlesGetMinMaxInfoAndCapsDimensions) {
    HWND testWnd = CreateWindowA("STATIC", "MinMaxInfoWnd", WS_OVERLAPPEDWINDOW, 50, 50, 1000, 800, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(activeOpenXrManager->Initialize());
    ASSERT_TRUE(activeOpenXrManager->CreateSession());

    DxgiProxy activeProxy(nullptr, activeOpenXrManager);
    activeProxy.AttachWindowSubclass(testWnd);

    RECT expectedClamped = DxgiProxy::ComputeClampedMirrorWindowRect(testWnd);
    int maxW = expectedClamped.right - expectedClamped.left;
    int maxH = expectedClamped.bottom - expectedClamped.top;

    MINMAXINFO mmi{};
    mmi.ptMinTrackSize = { 4000, 3000 };
    mmi.ptMaxTrackSize = { 10000, 10000 };
    mmi.ptMaxSize = { 10000, 10000 };

    SendMessageA(testWnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&mmi));

    EXPECT_EQ(mmi.ptMinTrackSize.x, maxW);
    EXPECT_EQ(mmi.ptMinTrackSize.y, maxH);
    EXPECT_EQ(mmi.ptMaxTrackSize.x, maxW);
    EXPECT_EQ(mmi.ptMaxTrackSize.y, maxH);
    EXPECT_EQ(mmi.ptMaxSize.x, maxW);
    EXPECT_EQ(mmi.ptMaxSize.y, maxH);

    activeProxy.DetachAll();
    DestroyWindow(testWnd);
}

TEST(DxgiProxyTest, SubclassSuppressesEnforcementDuringModalMoveAndSize) {
    HWND testWnd = CreateWindowA("STATIC", "SizeMoveWnd", WS_OVERLAPPEDWINDOW, 50, 50, 5000, 4000, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(activeOpenXrManager->Initialize());
    ASSERT_TRUE(activeOpenXrManager->CreateSession());

    DxgiProxy activeProxy(nullptr, activeOpenXrManager);
    activeProxy.SetGameHwnd(testWnd);

    // Initial state: not in size/move
    EXPECT_FALSE(activeProxy.IsInSizeMove());

    // Initial clamp
    activeProxy.OnPresent(nullptr, 0, 0);

    // User starts dragging/moving window (WM_ENTERSIZEMOVE)
    SendMessageA(testWnd, WM_ENTERSIZEMOVE, 0, 0);
    EXPECT_TRUE(activeProxy.IsInSizeMove());

    // OnPresent returns immediately during modal move/size and does not alter window
    activeProxy.OnPresent(nullptr, 0, 0);
    EXPECT_TRUE(activeProxy.IsInSizeMove());

    // User finishes dragging/moving window (WM_EXITSIZEMOVE)
    SendMessageA(testWnd, WM_EXITSIZEMOVE, 0, 0);
    EXPECT_FALSE(activeProxy.IsInSizeMove());

    // Next frame present enforces clamping cleanly
    activeProxy.OnPresent(nullptr, 0, 0);
    RECT postDragRect{};
    GetWindowRect(testWnd, &postDragRect);
    RECT expectedClamped = DxgiProxy::ComputeClampedMirrorWindowRect(testWnd);
    int maxW = expectedClamped.right - expectedClamped.left;
    int maxH = expectedClamped.bottom - expectedClamped.top;
    EXPECT_EQ(postDragRect.right - postDragRect.left, maxW);
    EXPECT_EQ(postDragRect.bottom - postDragRect.top, maxH);

    activeProxy.DetachAll();
    EXPECT_FALSE(activeProxy.IsInSizeMove());
    DestroyWindow(testWnd);
}

namespace {

struct TestSubclassObserverState {
    int lastWidth = 0;
    int lastHeight = 0;
    int lastMouseX = 0;
    int lastMouseY = 0;
    int wmSizeCount = 0;
};

LRESULT CALLBACK TestSubclassObserverProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR dwRefData) {
    auto* obs = reinterpret_cast<TestSubclassObserverState*>(dwRefData);
    if (obs) {
        if (uMsg == WM_SIZE) {
            obs->lastWidth = LOWORD(lParam);
            obs->lastHeight = HIWORD(lParam);
            obs->wmSizeCount++;
        } else if (uMsg == WM_MOUSEMOVE || uMsg == WM_LBUTTONDOWN) {
            obs->lastMouseX = static_cast<short>(LOWORD(lParam));
            obs->lastMouseY = static_cast<short>(HIWORD(lParam));
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static UINT s_lastResizeBuffersWidth = 0;
static UINT s_lastResizeBuffersHeight = 0;

HRESULT STDMETHODCALLTYPE MockResizeBuffersImpl(IDXGISwapChain*, UINT, UINT Width, UINT Height, DXGI_FORMAT, UINT) {
    s_lastResizeBuffersWidth = Width;
    s_lastResizeBuffersHeight = Height;
    return S_OK;
}

} // anonymous namespace

TEST(DxgiProxyTest, SubclassPreservesTargetRenderResolutionOnWmSizeAndWindowPosChanged) {
    HWND testWnd = CreateWindowA("STATIC", "ResDecoupleSizeWnd", WS_OVERLAPPEDWINDOW, 50, 50, 1000, 800, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    TestSubclassObserverState observer{};
    ASSERT_TRUE(SetWindowSubclass(testWnd, TestSubclassObserverProc, 0x9999, reinterpret_cast<DWORD_PTR>(&observer)));

    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(activeOpenXrManager->Initialize());
    ASSERT_TRUE(activeOpenXrManager->CreateSession());

    DxgiProxy activeProxy(nullptr, activeOpenXrManager);
    activeProxy.SetTargetRenderResolution(3840, 2160);
    EXPECT_TRUE(activeProxy.AttachWindowSubclass(testWnd));

    // Send a WM_SIZE with smaller physical dimensions (e.g. 1920x1080)
    SendMessageA(testWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(1920, 1080));

    // The subclass must intercept WM_SIZE and deliver the target render resolution (3840x2160)
    EXPECT_EQ(observer.lastWidth, 3840);
    EXPECT_EQ(observer.lastHeight, 2160);
    EXPECT_EQ(observer.wmSizeCount, 1);

    // Send WM_WINDOWPOSCHANGED; it must forward WM_SIZE with the target render resolution
    WINDOWPOS wp{};
    wp.hwnd = testWnd;
    wp.cx = 1920;
    wp.cy = 1080;
    wp.flags = 0;
    SendMessageA(testWnd, WM_WINDOWPOSCHANGED, 0, reinterpret_cast<LPARAM>(&wp));

    EXPECT_EQ(observer.lastWidth, 3840);
    EXPECT_EQ(observer.lastHeight, 2160);
    EXPECT_GE(observer.wmSizeCount, 2);

    activeProxy.DetachAll();
    RemoveWindowSubclass(testWnd, TestSubclassObserverProc, 0x9999);
    DestroyWindow(testWnd);
}

TEST(DxgiProxyTest, GetClientRectAndScreenToClientReportTargetRenderResolutionInActiveVrMode) {
    HWND testWnd = CreateWindowA("STATIC", "ResDecoupleDetourWnd", WS_OVERLAPPEDWINDOW, 50, 50, 1000, 800, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(activeOpenXrManager->Initialize());
    ASSERT_TRUE(activeOpenXrManager->CreateSession());

    DxgiProxy activeProxy(nullptr, activeOpenXrManager);
    activeProxy.SetTargetRenderResolution(3840, 2160);
    EXPECT_TRUE(activeProxy.AttachWindowSubclass(testWnd));
    EXPECT_TRUE(activeProxy.IsDetourGetClientRectInstalled());
    EXPECT_TRUE(activeProxy.IsDetourScreenToClientInstalled());
    EXPECT_TRUE(activeProxy.IsDetourSetCursorPosInstalled());
    EXPECT_TRUE(activeProxy.IsDetourClipCursorInstalled());

    // When the game queries GetClientRect in active VR, it must see 3840x2160
    RECT clientRc{};
    EXPECT_TRUE(GetClientRect(testWnd, &clientRc));
    EXPECT_EQ(clientRc.left, 0);
    EXPECT_EQ(clientRc.top, 0);
    EXPECT_EQ(clientRc.right, 3840);
    EXPECT_EQ(clientRc.bottom, 2160);

    // Physical window client width/height
    RECT unSubclassedRc{};
    {
        // When queried outside active VR or on another window, original dimensions are returned
        HWND otherWnd = CreateWindowA("STATIC", "OtherWnd", WS_OVERLAPPEDWINDOW, 0, 0, 400, 300, nullptr, nullptr, nullptr, nullptr);
        ASSERT_TRUE(otherWnd != nullptr);
        RECT otherRc{};
        EXPECT_TRUE(GetClientRect(otherWnd, &otherRc));
        EXPECT_NE(otherRc.right, 3840);
        DestroyWindow(otherWnd);
    }

    // Physical window client dimensions
    RECT realPhysRc{};
    // Detach temporarily to inspect physical size
    activeProxy.DetachWindowSubclass();
    EXPECT_TRUE(GetClientRect(testWnd, &realPhysRc));
    int physW = realPhysRc.right - realPhysRc.left;
    int physH = realPhysRc.bottom - realPhysRc.top;
    ASSERT_GT(physW, 0);
    ASSERT_GT(physH, 0);

    // Reattach subclass and detours
    EXPECT_TRUE(activeProxy.AttachWindowSubclass(testWnd));

    // ScreenToClient maps bottom-right of the physical window to target render extent (3840, 2160)
    POINT ptBottomRight{ physW, physH };
    EXPECT_TRUE(ClientToScreen(testWnd, &ptBottomRight));
    EXPECT_TRUE(ScreenToClient(testWnd, &ptBottomRight));
    EXPECT_NEAR(ptBottomRight.x, 3840, 1);
    EXPECT_NEAR(ptBottomRight.y, 2160, 1);

    // ScreenToNormalizedClient still produces normalized [0.0, 1.0] coordinates
    POINT screenPt{ physW, physH };
    EXPECT_TRUE(ClientToScreen(testWnd, &screenPt));
    float normX = 0.0f, normY = 0.0f;
    EXPECT_TRUE(DxgiProxy::ScreenToNormalizedClient(testWnd, screenPt, normX, normY));
    EXPECT_FLOAT_EQ(normX, 1.0f);
    EXPECT_FLOAT_EQ(normY, 1.0f);

    activeProxy.DetachAll();
    EXPECT_FALSE(activeProxy.IsDetourGetClientRectInstalled());
    EXPECT_FALSE(activeProxy.IsDetourScreenToClientInstalled());
    EXPECT_FALSE(activeProxy.IsDetourSetCursorPosInstalled());
    EXPECT_FALSE(activeProxy.IsDetourClipCursorInstalled());

    // After detach, GetClientRect returns physical dimensions again
    RECT restoredRc{};
    EXPECT_TRUE(GetClientRect(testWnd, &restoredRc));
    EXPECT_EQ(restoredRc.right, physW);
    EXPECT_EQ(restoredRc.bottom, physH);

    DestroyWindow(testWnd);
}

TEST(DxgiProxyTest, SetCursorPosCentersCursorAndEnablesBidirectionalLookParity) {
    // Replicate Desktop Mirror Window geometry (clamped on ultrawide/4K display).
    // The window must lie fully on screen (CI runners are 1024x768) or the OS clamps
    // ClipCursor to the desktop and the hooked clip rect can't be verified exactly.
    HWND testWnd = CreateWindowA("STATIC", "MouseLookCenterWnd", WS_OVERLAPPEDWINDOW,
        GetSystemMetrics(SM_XVIRTUALSCREEN) + 50, GetSystemMetrics(SM_YVIRTUALSCREEN) + 50, 800, 600,
        nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(activeOpenXrManager->Initialize());
    ASSERT_TRUE(activeOpenXrManager->CreateSession());

    DxgiProxy activeProxy(nullptr, activeOpenXrManager);
    activeProxy.SetTargetRenderResolution(3840, 2160);
    EXPECT_TRUE(activeProxy.AttachWindowSubclass(testWnd));
    EXPECT_TRUE(activeProxy.IsDetourSetCursorPosInstalled());
    EXPECT_TRUE(activeProxy.IsDetourClipCursorInstalled());

    // Query physical client rect using InternalWindowQueryScope
    RECT physRc{};
    {
        activeProxy.DetachWindowSubclass();
        EXPECT_TRUE(GetClientRect(testWnd, &physRc));
        EXPECT_TRUE(activeProxy.AttachWindowSubclass(testWnd));
    }
    int physW = physRc.right - physRc.left;
    int physH = physRc.bottom - physRc.top;

    POINT physTopLeft{ 0, 0 };
    EXPECT_TRUE(ClientToScreen(testWnd, &physTopLeft));

    int expectedPhysCenterX = physTopLeft.x + physW / 2;
    int expectedPhysCenterY = physTopLeft.y + physH / 2;

    // 1. Verify virtual cursor positioning calculation:
    // ScaleVirtualCursorToPhysical accurately transforms virtual center (1920, 1080) to physical center
    POINT warpVirtualCenter{ physTopLeft.x + 1920, physTopLeft.y + 1080 };
    int computedPhysX = 0;
    int computedPhysY = 0;
    EXPECT_TRUE(DxgiProxy::ScaleVirtualCursorToPhysical(testWnd, warpVirtualCenter.x, warpVirtualCenter.y, computedPhysX, computedPhysY));
    EXPECT_NEAR(computedPhysX, expectedPhysCenterX, 1);
    EXPECT_NEAR(computedPhysY, expectedPhysCenterY, 1);

    // Specifically verify it does NOT set x to physTopLeft.x + 1920 (which would be near/past right window edge)
    if (physW < 3800) {
        EXPECT_NE(computedPhysX, physTopLeft.x + 1920);
    }

    // 2. Verify virtual clip rectangle constraining calculation:
    RECT virtualClipRc{ physTopLeft.x, physTopLeft.y, physTopLeft.x + 3840, physTopLeft.y + 2160 };
    RECT constrainedClipRc{};
    EXPECT_TRUE(DxgiProxy::ConstrainVirtualClipRectToPhysical(testWnd, &virtualClipRc, constrainedClipRc));
    EXPECT_EQ(constrainedClipRc.left, physTopLeft.x);
    EXPECT_EQ(constrainedClipRc.top, physTopLeft.y);
    EXPECT_EQ(constrainedClipRc.right, physTopLeft.x + physW);
    EXPECT_EQ(constrainedClipRc.bottom, physTopLeft.y + physH);

    // 3. Verify hooked Win32 API invocations (handles interactive desktop or headless/CI ERROR_INVALID_ADDRESS / ERROR_ACCESS_DENIED)
    BOOL setPosRes = SetCursorPos(warpVirtualCenter.x, warpVirtualCenter.y);
    if (setPosRes) {
        POINT actualCursorPos{};
        if (GetCursorPos(&actualCursorPos)) {
            EXPECT_NEAR(actualCursorPos.x, expectedPhysCenterX, 1);
            EXPECT_NEAR(actualCursorPos.y, expectedPhysCenterY, 1);
        }
    } else {
        DWORD err = GetLastError();
        EXPECT_TRUE(err == ERROR_INVALID_ADDRESS || err == ERROR_ACCESS_DENIED);
    }

    BOOL clipRes = ClipCursor(&virtualClipRc);
    if (clipRes) {
        RECT actualClipRc{};
        if (GetClipCursor(&actualClipRc)) {
            EXPECT_NEAR(actualClipRc.left, physTopLeft.x, 1);
            EXPECT_NEAR(actualClipRc.top, physTopLeft.y, 1);
            EXPECT_NEAR(actualClipRc.right, physTopLeft.x + physW, 1);
            EXPECT_NEAR(actualClipRc.bottom, physTopLeft.y + physH, 1);
        }
        ClipCursor(nullptr);
    } else {
        DWORD err = GetLastError();
        EXPECT_TRUE(err == ERROR_INVALID_ADDRESS || err == ERROR_ACCESS_DENIED);
    }

    // 4. Bidirectional delta parity:
    // When the mouse is at physical center and user moves delta right or left,
    // ScreenToClient maps back symmetrically around 1920.
    POINT moveRightScreen{ expectedPhysCenterX + 20, expectedPhysCenterY };
    EXPECT_TRUE(ScreenToClient(testWnd, &moveRightScreen));
    int rightDelta = moveRightScreen.x - 1920;

    POINT moveLeftScreen{ expectedPhysCenterX - 20, expectedPhysCenterY };
    EXPECT_TRUE(ScreenToClient(testWnd, &moveLeftScreen));
    int leftDelta = 1920 - moveLeftScreen.x;

    EXPECT_GT(rightDelta, 0);
    EXPECT_GT(leftDelta, 0);
    EXPECT_NEAR(rightDelta, leftDelta, 1);

    activeProxy.DetachAll();
    EXPECT_FALSE(activeProxy.IsDetourSetCursorPosInstalled());
    EXPECT_FALSE(activeProxy.IsDetourClipCursorInstalled());
    DestroyWindow(testWnd);
}

TEST(DxgiProxyTest, SubclassScalesMouseMessagesAcrossFullTargetResolution) {
    HWND testWnd = CreateWindowA("STATIC", "ResDecoupleMouseWnd", WS_OVERLAPPEDWINDOW, 50, 50, 1000, 800, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    TestSubclassObserverState observer{};
    ASSERT_TRUE(SetWindowSubclass(testWnd, TestSubclassObserverProc, 0x8888, reinterpret_cast<DWORD_PTR>(&observer)));

    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(activeOpenXrManager->Initialize());
    ASSERT_TRUE(activeOpenXrManager->CreateSession());

    DxgiProxy activeProxy(nullptr, activeOpenXrManager);
    activeProxy.SetTargetRenderResolution(3840, 2160);
    EXPECT_TRUE(activeProxy.AttachWindowSubclass(testWnd));

    // Get physical dimensions
    activeProxy.DetachWindowSubclass();
    RECT physRc{};
    GetClientRect(testWnd, &physRc);
    int physW = physRc.right - physRc.left;
    int physH = physRc.bottom - physRc.top;
    EXPECT_TRUE(activeProxy.AttachWindowSubclass(testWnd));

    // Send mouse move at (physW, physH) (bottom-right of physical window)
    SendMessageA(testWnd, WM_MOUSEMOVE, 0, MAKELPARAM(static_cast<WORD>(physW), static_cast<WORD>(physH)));
    EXPECT_EQ(observer.lastMouseX, 3840);
    EXPECT_EQ(observer.lastMouseY, 2160);

    // Send mouse button down at center (physW / 2, physH / 2)
    SendMessageA(testWnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(static_cast<WORD>(physW / 2), static_cast<WORD>(physH / 2)));
    EXPECT_NEAR(observer.lastMouseX, 1920, 2);
    EXPECT_NEAR(observer.lastMouseY, 1080, 2);

    // Send mouse move at (0, 0)
    SendMessageA(testWnd, WM_MOUSEMOVE, 0, MAKELPARAM(0, 0));
    EXPECT_EQ(observer.lastMouseX, 0);
    EXPECT_EQ(observer.lastMouseY, 0);

    activeProxy.DetachAll();
    RemoveWindowSubclass(testWnd, TestSubclassObserverProc, 0x8888);
    DestroyWindow(testWnd);
}

TEST(DxgiProxyTest, AttachSwapChainDetoursResizeBuffersAndOverridesTargetResolution) {
    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(activeOpenXrManager->Initialize());
    ASSERT_TRUE(activeOpenXrManager->CreateSession());

    DxgiProxy activeProxy(nullptr, activeOpenXrManager);
    activeProxy.SetTargetRenderResolution(3840, 2160);

    std::vector<void*> scVTable(50, nullptr);
    scVTable[0] = reinterpret_cast<void*>(&MockSwapChainQI);
    scVTable[1] = reinterpret_cast<void*>(&MockAddRef);
    scVTable[2] = reinterpret_cast<void*>(&MockRelease);
    scVTable[7] = reinterpret_cast<void*>(&MockGetDevice);
    scVTable[kPresentVTableSlot] = reinterpret_cast<void*>(&MockPresent);
    scVTable[9] = reinterpret_cast<void*>(&MockGetBuffer);
    scVTable[12] = reinterpret_cast<void*>(&MockGetDesc);
    scVTable[kResizeBuffersVTableSlot] = reinterpret_cast<void*>(&MockResizeBuffersImpl);
    MockComObject mockSwapChain{ scVTable.data() };

    auto pSwapChain = reinterpret_cast<IDXGISwapChain*>(&mockSwapChain);

    EXPECT_TRUE(activeProxy.AttachSwapChain(pSwapChain));
    EXPECT_TRUE(activeProxy.IsDetourResizeBuffersInstalled());

    // When the game attempts to downsize buffers to 1920x1080, our detour enforces 3840x2160
    s_lastResizeBuffersWidth = 0;
    s_lastResizeBuffersHeight = 0;
    pSwapChain->ResizeBuffers(2, 1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, 0);

    EXPECT_EQ(s_lastResizeBuffersWidth, 3840u);
    EXPECT_EQ(s_lastResizeBuffersHeight, 2160u);

    // When the game passes 0x0 (which DXGI normally derives from window client rect)
    s_lastResizeBuffersWidth = 0;
    s_lastResizeBuffersHeight = 0;
    pSwapChain->ResizeBuffers(2, 0, 0, DXGI_FORMAT_UNKNOWN, 0);

    EXPECT_EQ(s_lastResizeBuffersWidth, 3840u);
    EXPECT_EQ(s_lastResizeBuffersHeight, 2160u);

    activeProxy.DetachAll();
    EXPECT_FALSE(activeProxy.IsDetourResizeBuffersInstalled());
}

} // namespace fs25vr


