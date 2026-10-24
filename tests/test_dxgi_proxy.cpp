#include <gtest/gtest.h>
#include "proxy/dxgi_proxy.h"
#include "core/dll_lifecycle.h"
#include "core/injector_runtime.h"
#include "proxy/system_dxgi.h"
#include "openxr/openxr_manager.h"
#include "fake_openxr_events.h"
#include "fake_openxr_tracking.h"
#include "fake_openxr_runtime.h"
#include "ngx/ngx_interceptor.h"
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <thread>
#include <unordered_set>

using Microsoft::WRL::ComPtr;

// A passthrough export, defined in dxgi_proxy.cpp and exported through dxgi_exports.def.
extern "C" DWORD WINAPI Proxy_PIXGetCaptureState();

namespace fs25vr {

// Detours are installed only by a proxy a live runtime owns, and hook entry points resolve their
// subsystem through the live runtimes, so tests that drive them build their proxy through one. Each
// runtime gets its own window controller and Luau interceptor.
static std::unique_ptr<InjectorRuntime> MakeRuntime(
    std::shared_ptr<OpenXrManager> openXrManager,
    std::shared_ptr<VRBridge> vrBridge = nullptr) {
    InjectorRuntime::Dependencies deps;
    deps.openXrManager = std::move(openXrManager);
    deps.vrBridge = std::move(vrBridge);
    return std::make_unique<InjectorRuntime>(std::move(deps));
}

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
    SystemDxgi systemDxgi;
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
    auto systemDxgi = std::make_shared<SystemDxgi>();

    DxgiProxy proxy(systemDxgi, openXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);

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
    auto systemDxgi = std::make_shared<SystemDxgi>();

    DxgiProxy proxy(systemDxgi, openXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);

    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = proxy.CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

    EXPECT_EQ(hr, S_OK);
    ASSERT_NE(factory.Get(), nullptr);
    EXPECT_TRUE(openXrManager->IsPassThrough());
}

// Like VDXR: xrGetSystem creates a DXGI factory, and the runtime's dxgi.dll import binds to the proxy.
class DxgiCallingOpenXrRuntime : public FakeOpenXrRuntime {
public:
    DxgiProxy* proxy = nullptr;
    HRESULT nestedResult = E_PENDING;
    ComPtr<IDXGIFactory1> nestedFactory;

    XrResult GetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) override {
        nestedResult = proxy->CreateDXGIFactory1(IID_PPV_ARGS(&nestedFactory));
        return FakeOpenXrRuntime::GetSystem(instance, getInfo, systemId);
    }
};

TEST(DxgiProxyTest, RuntimeCreatingAFactoryDuringInitializationDoesNotDeadlock) {
    // Leaked on a timeout: the blocked worker thread still uses them.
    auto* runtime = new std::shared_ptr<DxgiCallingOpenXrRuntime>(std::make_shared<DxgiCallingOpenXrRuntime>());
    auto* openXrManager = new std::shared_ptr<OpenXrManager>(std::make_shared<OpenXrManager>(*runtime));
    auto* proxy = new DxgiProxy(std::make_shared<SystemDxgi>(), *openXrManager, nullptr, nullptr,
                                std::make_shared<MirrorWindowController>(), nullptr);
    (*runtime)->proxy = proxy;

    // A detached thread rather than std::async, whose pool would keep a deadlocked process from exiting.
    auto result = std::make_shared<std::promise<bool>>();
    std::future<bool> outerCall = result->get_future();
    std::thread([proxy, result] {
        ComPtr<IDXGIFactory1> factory;
        HRESULT hr = proxy->CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        result->set_value(SUCCEEDED(hr) && factory != nullptr);
    }).detach();
    if (outerCall.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        FAIL() << "CreateDXGIFactory1 deadlocked when the runtime re-entered it during OpenXR initialization.";
    }

    EXPECT_TRUE(outerCall.get());
    EXPECT_EQ((*runtime)->nestedResult, S_OK);
    EXPECT_NE((*runtime)->nestedFactory.Get(), nullptr);
    EXPECT_TRUE((*openXrManager)->IsActive());

    (*runtime)->nestedFactory.Reset();
    delete proxy;
    delete openXrManager;
    delete runtime;
}

// Like a real runtime: a session needs a graphics binding once a graphics extension is enabled.
class GraphicsBindingRequiredOpenXrRuntime : public FakeOpenXrRuntime {
public:
    uint32_t createSessionCalls = 0;

    XrResult CreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session) override {
        ++createSessionCalls;
        if (!createInfo || !createInfo->next) return XR_ERROR_GRAPHICS_DEVICE_INVALID;
        return FakeOpenXrRuntime::CreateSession(instance, createInfo, session);
    }
};

TEST(DxgiProxyTest, SessionWaitsForTheGamesD3D12DeviceInsteadOfBeingCreatedAtStartup) {
    auto runtime = std::make_shared<GraphicsBindingRequiredOpenXrRuntime>();
    auto openXrManager = std::make_shared<OpenXrManager>(runtime);
    DxgiProxy proxy(std::make_shared<SystemDxgi>(), openXrManager, nullptr, nullptr,
                    std::make_shared<MirrorWindowController>(), nullptr);

    // The game asks for a factory before it has a device to bind a session to.
    ComPtr<IDXGIFactory1> factory;
    ASSERT_EQ(proxy.CreateDXGIFactory1(IID_PPV_ARGS(&factory)), S_OK);
    EXPECT_TRUE(openXrManager->IsActive());
    EXPECT_EQ(runtime->createSessionCalls, 0u);
    EXPECT_FALSE(openXrManager->HasSession());

    // Its swap chain then supplies the device and queue, and the session is created once, bound to them.
    auto* device = reinterpret_cast<ID3D12Device*>(static_cast<uintptr_t>(0x1000));
    auto* queue = reinterpret_cast<ID3D12CommandQueue*>(static_cast<uintptr_t>(0x2000));
    openXrManager->SetD3D12DeviceAndQueue(device, queue);
    EXPECT_EQ(runtime->createSessionCalls, 1u);
    EXPECT_TRUE(openXrManager->HasSession());
    EXPECT_TRUE(openXrManager->HasTrackingSpaces());
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
    // Fake runtime event queue; a created session reports READY like a real runtime would.
    FakeOpenXrEventQueue events;
    XrResult PollEvent(XrInstance, XrEventDataBuffer* eventData) override { return events.Poll(eventData); }
    XrResult CreateSession(XrInstance, const XrSessionCreateInfo*, XrSession* session) override {
        if (session) *session = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xCAFE0001));
        events.PushSessionState(XR_SESSION_STATE_READY);
        return XR_SUCCESS;
    }
    XrResult BeginSession(XrSession, const XrSessionBeginInfo*) override { return XR_SUCCESS; }
    XrResult EndSession(XrSession) override { return XR_SUCCESS; }
    XrResult DestroySession(XrSession) override { return XR_SUCCESS; }
    // Head at (0, 1.5, 0) with the eyes 64 mm apart, both with the same asymmetric FOV; tests
    // move the head and change FOVs through tracking.
    FakeOpenXrTracking tracking = MakeDefaultTracking();
    static FakeOpenXrTracking MakeDefaultTracking() {
        FakeOpenXrTracking tracking;
        tracking.head.pose.position = { 0.0f, 1.5f, 0.0f };
        for (uint32_t i = 0; i < 2; ++i) {
            tracking.SetEyeFov(i, { -0.90f, 0.80f, 0.85f, -0.85f });
            tracking.views[i].pose.position = { (i == 0 ? -0.032f : 0.032f), 1.5f, 0.0f };
        }
        return tracking;
    }
    XrResult CreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space) override {
        return tracking.CreateReferenceSpace(createInfo, space);
    }
    XrResult DestroySpace(XrSpace space) override {
        tracking.DestroySpace(space);
        return XR_SUCCESS;
    }
    std::vector<XrTime> locateSpaceTimes;
    XrResult LocateSpace(XrSpace, XrSpace baseSpace, XrTime time, XrSpaceLocation* location) override {
        locateSpaceTimes.push_back(time);
        return tracking.LocateSpace(baseSpace, location);
    }
    XrResult LocateViews(XrSession, const XrViewLocateInfo*, XrViewState* viewState, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views) override {
        return tracking.LocateViews(viewState, viewCapacityInput, viewCountOutput, views);
    }
    XrResult EnumerateSwapchainFormats(XrSession, uint32_t formatCapacityInput, uint32_t* formatCountOutput, int64_t* formats) override {
        if (formatCountOutput) *formatCountOutput = 1;
        if (formatCapacityInput > 0 && formats) {
            formats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        }
        return XR_SUCCESS;
    }
    bool failCreateSwapchain = false;
    XrResult CreateSwapchain(XrSession, const XrSwapchainCreateInfo*, XrSwapchain* swapchain) override {
        if (failCreateSwapchain) return XR_ERROR_RUNTIME_FAILURE;
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
            frameState->predictedDisplayTime = 1000000 + static_cast<XrTime>(waitFrameCalls - 1) * 11111111;
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
// What one real MockPresent call adds to s_mockPresentCalls.
constexpr int kMockPresentIncrement = 3;

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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

TEST(InjectorRuntimeTest, WiresSubsystemsWithFakesAndDrivesPresentEndToEnd) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();

    InjectorRuntime::Dependencies deps;
    deps.openXrManager = openXrManager;
    deps.vrBridge = vrBridge;
    deps.luauInterceptor = std::make_shared<LuauInterceptor>(vrBridge);
    deps.windowController = std::make_shared<MirrorWindowController>();
    deps.ngxInterceptor = std::make_shared<NgxInterceptor>();
    auto ngx = deps.ngxInterceptor;
    DxgiProxy* runtimeProxy = nullptr;

    {
        InjectorRuntime runtime(std::move(deps));
        runtimeProxy = &runtime.GetDxgiProxy();

        // One object owns and wires every subsystem.
        EXPECT_EQ(&runtime.GetOpenXrManager(), openXrManager.get());
        EXPECT_EQ(&runtime.GetVrBridge(), vrBridge.get());
        EXPECT_EQ(runtime.GetDxgiProxy().GetOpenXrManager(), openXrManager.get());
        EXPECT_EQ(runtime.GetDxgiProxy().GetVrBridge(), vrBridge.get());
        EXPECT_EQ(runtime.GetDxgiProxy().GetLuauInterceptor(), &runtime.GetLuauInterceptor());
        EXPECT_EQ(&runtime.GetDxgiProxy().GetWindowController(), &runtime.GetWindowController());
        EXPECT_EQ(ngx->GetVRBridge(), vrBridge.get());
        EXPECT_EQ(ngx->GetOpenXrManager(), openXrManager.get());
        EXPECT_EQ(ngx->GetDxgiProxy(), &runtime.GetDxgiProxy());

        // Drive an AFR present pair through the runtime-owned proxy.
        runtime.GetDxgiProxy().OnPresent(nullptr, 0, 0);
        EXPECT_TRUE(openXrManager->GetEyeSwapchain(0).hasCopiedFrame);
        EXPECT_EQ(vrBridge->GetFrameParity(), 1u);
        runtime.GetDxgiProxy().OnPresent(nullptr, 0, 0);
        EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
        EXPECT_EQ(mockDispatch->endFrameCalls, 1u);
    }

    // The runtime does not leave the NGX interceptor pointing at freed subsystems.
    EXPECT_NE(ngx->GetDxgiProxy(), runtimeProxy);
    EXPECT_NE(ngx->GetVRBridge(), vrBridge.get());
    EXPECT_NE(ngx->GetOpenXrManager(), openXrManager.get());
}

TEST(InjectorRuntimeTest, ProductionRuntimeCreatesAndWiresEverySubsystem) {
    InjectorRuntime& runtime = InjectorRuntime::Instance();
    NgxInterceptor& ngx = runtime.GetNgxInterceptor();
    EXPECT_EQ(ngx.GetDxgiProxy(), &runtime.GetDxgiProxy());
    EXPECT_EQ(ngx.GetVRBridge(), &runtime.GetVrBridge());
    EXPECT_EQ(ngx.GetOpenXrManager(), &runtime.GetOpenXrManager());
    EXPECT_EQ(runtime.GetDxgiProxy().GetVrBridge(), &runtime.GetVrBridge());
    EXPECT_EQ(runtime.GetDxgiProxy().GetOpenXrManager(), &runtime.GetOpenXrManager());
    EXPECT_EQ(runtime.GetDxgiProxy().GetSystemDxgi(), &runtime.GetSystemDxgi());
    EXPECT_EQ(runtime.GetVrBridge().GetOpenXrManager().get(), &runtime.GetOpenXrManager());
}

TEST(InjectorRuntimeTest, NgxInterceptorIgnoresAnInactiveProxyInPassThrough) {
    // In Pass-Through the NGX hooks must not take the proxy's mirror target resolution, or DLSS would be
    // created at that size instead of the game's own output size.
    InjectorRuntime& runtime = InjectorRuntime::Instance();
    DxgiProxy& proxy = runtime.GetDxgiProxy();
    ASSERT_FALSE(proxy.IsActive());
    proxy.GetWindowController().SetTargetRenderResolution(1280, 720);

    uint32_t width = 123, height = 456;
    runtime.GetNgxInterceptor().GetTargetUpscalingDimensions(width, height);
    EXPECT_EQ(width, 0u);
    EXPECT_EQ(height, 0u);

    proxy.GetWindowController().SetTargetRenderResolution(0, 0);
}

TEST(InjectorRuntimeTest, UnwiredNgxInterceptorFindsNoSubsystemsThroughSingletons) {
    NgxInterceptor ngx;
    EXPECT_EQ(ngx.GetVRBridge(), nullptr);
    EXPECT_EQ(ngx.GetDxgiProxy(), nullptr);
    EXPECT_EQ(ngx.GetOpenXrManager(), nullptr);
    EXPECT_FALSE(ngx.IsSingleContextBypassActive());
    EXPECT_EQ(ngx.GetActiveDualPassEyeIndex(), 0u);
}

TEST(InjectorRuntimeTest, RuntimeWithoutInjectedNgxCreatesAndWiresItsOwn) {
    auto openXrManager = std::make_shared<OpenXrManager>(std::make_shared<MockActiveOpenXrDispatch>());
    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);

    InjectorRuntime::Dependencies deps;
    deps.openXrManager = openXrManager;
    deps.vrBridge = vrBridge;
    InjectorRuntime runtime(std::move(deps));

    NgxInterceptor& ngx = runtime.GetNgxInterceptor();
    EXPECT_EQ(ngx.GetVRBridge(), vrBridge.get());
    EXPECT_EQ(ngx.GetOpenXrManager(), openXrManager.get());
    EXPECT_EQ(ngx.GetDxgiProxy(), &runtime.GetDxgiProxy());
}

TEST(InjectorRuntimeTest, DestructorTakesNoNgxLockWhileProcessTerminates) {
    // A thread killed at termination may own the NGX mutex; the destructor must not touch it.
    auto ngx = std::make_shared<NgxInterceptor>();
    InjectorRuntime::Dependencies deps;
    deps.openXrManager = std::make_shared<OpenXrManager>(std::make_shared<MockActiveOpenXrDispatch>());
    deps.ngxInterceptor = ngx;
    DxgiProxy* runtimeProxy = nullptr;
    {
        InjectorRuntime runtime(std::move(deps));
        runtimeProxy = &runtime.GetDxgiProxy();
        OnProcessDetach(reinterpret_cast<LPVOID>(1));  // non-null lpReserved: process terminating
    }
    ResetProcessTerminatingForTesting();

    // The wiring was left untouched, so the destructor never reached the locking setters.
    EXPECT_EQ(ngx->GetDxgiProxy(), runtimeProxy);
    ngx->SetDxgiProxy(nullptr);
    ngx->SetVRBridge(nullptr);
    ngx->SetOpenXrManager(nullptr);
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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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

namespace {
fs25vr::CameraRecord CameraAt(uint32_t eye, double x) {
    fs25vr::CameraRecord r;
    r.eye = eye;
    r.position[0] = x;
    r.fovY = 1.0f;
    return r;
}
} // namespace

TEST(DxgiProxyTest, EveryStereoPresentAdvancesTheRenderedFrameThatStampsCameraRecords) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::SequentialDualPass);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);
    CameraRecordStore& records = vrBridge->GetCameraRecords();

    ASSERT_EQ(records.GetFrame(), 0u);
    ASSERT_TRUE(records.Report(CameraAt(0, 0.0)));          // sub-pass 0 renders the left eye
    EXPECT_FALSE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(records.GetFrame(), 1u);                       // a suppressed sub-pass present counts too

    ASSERT_TRUE(records.Report(CameraAt(1, 0.064)));         // sub-pass 1 renders the right eye
    auto resolved = records.Resolve(1);
    ASSERT_TRUE(resolved.current.has_value());
    EXPECT_EQ(resolved.current->frame, 1u);
    ASSERT_TRUE(resolved.previousFrame.has_value());
    EXPECT_EQ(resolved.previousFrame->eye, 0u);
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(records.GetFrame(), 2u);
}

TEST(DxgiProxyTest, PresentsOutsideStereoPresentationDiscardCameraRecordsInsteadOfLettingThemGoStale) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::SequentialDualPass);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);
    CameraRecordStore& records = vrBridge->GetCameraRecords();

    ASSERT_TRUE(records.Report(CameraAt(0, 0.0)));
    vrBridge->Set2DViewActive(true); // 2D Floating Window Mode presents as a quad
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(records.GetFrame(), 0u); // the counter only advances in stereo presentation
    EXPECT_FALSE(records.Resolve(0).current.has_value());
}

// Drives eye pairs through OnPresent/OnPresentReturned the way the Present detour does. For each
// pair the head is at x = pairIndex when the frame begins and has moved on by the time the
// Companion Mod asks for either eye's pose; both eyes must still get the pose located at the
// pair's predicted display time.
static void ExpectBothEyesLocatedAtThePairsDisplayTime(VRRenderingMode mode) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    mockDispatch->enforceCallOrder = true;
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(mode);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);
    openXrManager->PollEvents();  // READY: the session runs
    ASSERT_TRUE(openXrManager->IsSessionRunning());

    // A Present returned: the first pair's frame is waited on and begun before the engine updates.
    mockDispatch->tracking.head.pose.position = { 1.0f, 1.5f, 0.0f };
    proxy.OnPresentReturned();

    for (uint32_t pair = 1; pair <= 3; ++pair) {
        ASSERT_TRUE(openXrManager->IsFrameBegun());

        for (uint32_t eye = 0; eye < 2; ++eye) {
            mockDispatch->tracking.head.pose.position = { 100.0f + eye, 1.5f, 0.0f };
            HeadPose pose = vrBridge->GetHeadPose();  // the eye of the pair now being rendered
            EXPECT_NEAR(pose.position.x, static_cast<float>(pair), 1e-4f) << "pair " << pair << " eye " << eye;
            ASSERT_TRUE(openXrManager->IsFrameBegun());
            proxy.OnPresent(nullptr, 0, 0);
            // After the pair's second Present the next frame begins with the head at pair + 1.
            mockDispatch->tracking.head.pose.position = { static_cast<float>(pair + 1), 1.5f, 0.0f };
            proxy.OnPresentReturned();
            if (eye == 0) {
                EXPECT_TRUE(openXrManager->IsFrameBegun()) << "the pair's frame stays begun until it is submitted";
            }
        }
        EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), pair);
    }

    // Every pose lookup used a frame's predicted display time, never a time derived from "now".
    for (XrTime time : mockDispatch->locateSpaceTimes) {
        EXPECT_EQ((time - 1000000) % 11111111, 0) << "time " << time;
    }
    EXPECT_EQ(mockDispatch->waitFrameCalls, 4u);  // 3 pairs + the frame begun after the last submit
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);
}

TEST(DxgiProxyTest, SequentialDualPassBothEyesAreLocatedAtThePairsDisplayTime) {
    ExpectBothEyesLocatedAtThePairsDisplayTime(VRRenderingMode::SequentialDualPass);
}

TEST(DxgiProxyTest, AfrBothEyesAreLocatedAtThePairsDisplayTime) {
    ExpectBothEyesLocatedAtThePairsDisplayTime(VRRenderingMode::AFR);
}

TEST(DxgiProxyTest, OnPresentReturnedDoesNotBeginAFrameWithoutARunningSessionAndSwapchains) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);

    proxy.OnPresentReturned();  // no eye swapchains yet

    EXPECT_FALSE(openXrManager->IsFrameBegun());
    EXPECT_EQ(mockDispatch->waitFrameCalls, 0u);
}

TEST(DxgiProxyTest, OnPresentReturnedDoesNotBeginAFrameAfterAFailedPresent) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));
    DxgiProxy proxy(nullptr, openXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    openXrManager->PollEvents();  // READY: the session runs
    ASSERT_TRUE(openXrManager->IsSessionRunning());

    proxy.OnPresentReturned(DXGI_ERROR_DEVICE_REMOVED);
    EXPECT_FALSE(openXrManager->IsFrameBegun());
    EXPECT_EQ(mockDispatch->waitFrameCalls, 0u);

    proxy.OnPresentReturned(DXGI_STATUS_OCCLUDED);  // a success code: the next frame begins
    EXPECT_TRUE(openXrManager->IsFrameBegun());
    EXPECT_EQ(mockDispatch->waitFrameCalls, 1u);
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
    auto proxyRuntime = MakeRuntime(openXrManager, vrBridge);
    DxgiProxy& proxy = proxyRuntime->GetDxgiProxy();

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

    // Sub-Pass 0
    EXPECT_FALSE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(vrBridge->GetSubPassIndex(), 1u);

    // Mode change requested during Sub-Pass 0
    vrBridge->SetPendingRenderingMode(VRRenderingMode::AFR);
    EXPECT_EQ(vrBridge->GetPendingRenderingMode(), VRRenderingMode::AFR);
    // Mode must NOT have changed yet because compositor frame is mid-pair
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::SequentialDualPass);

    // Sub-Pass 1 completes the pair
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    // Completed frame boundary: pending mode is latched atomically
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);
    EXPECT_FALSE(vrBridge->GetPendingRenderingMode().has_value());
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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

    // Queue pending mode change to SequentialDualPass
    vrBridge->SetPendingRenderingMode(VRRenderingMode::SequentialDualPass);
    EXPECT_EQ(vrBridge->GetPendingRenderingMode(), VRRenderingMode::SequentialDualPass);
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);

    // AFR Frame 0 (parity = 0, Left Eye): does NOT complete pair
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);
    // Mode must NOT be latched yet!
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);
    EXPECT_EQ(vrBridge->GetPendingRenderingMode(), VRRenderingMode::SequentialDualPass);

    // AFR Frame 1 (parity = 1, Right Eye): completes pair and calls SubmitFramePair()
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    // Completed frame boundary: pending mode is latched atomically
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::SequentialDualPass);
    EXPECT_FALSE(vrBridge->GetPendingRenderingMode().has_value());
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
    auto proxyRuntime = MakeRuntime(openXrManager, vrBridge);
    DxgiProxy& proxy = proxyRuntime->GetDxgiProxy();

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

    // Mode is queued in VRBridge and NOT latched mid-pair
    EXPECT_EQ(vrBridge->GetPendingRenderingMode(), VRRenderingMode::AFR);
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::SequentialDualPass);

    // Sub-Pass 1 (Right Eye) completes the pair
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);

    // Latched strictly on completed compositor frame boundary
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);
    EXPECT_FALSE(vrBridge->GetPendingRenderingMode().has_value());

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
    auto proxyRuntime = MakeRuntime(openXrManager, vrBridge);
    DxgiProxy& proxy = proxyRuntime->GetDxgiProxy();

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

TEST(DxgiProxyTest, PresentHookActsForTheRuntimeThatInstalledItAndIsRemovedWithIt) {
    s_mockPresentCalls = 0;
    auto makeActive = []() {
        auto openXr = std::make_shared<OpenXrManager>(std::make_shared<MockActiveOpenXrDispatch>());
        EXPECT_TRUE(openXr->Initialize());
        EXPECT_TRUE(openXr->CreateSession());
        EXPECT_TRUE(openXr->CreateTrackingSpaces());
        EXPECT_TRUE(openXr->CreateEyeSwapchains(1920, 1080));
        return openXr;
    };
    auto installerXr = makeActive();
    auto installerBridge = std::make_shared<VRBridge>(installerXr, nullptr);
    installerBridge->PerformHandshake();
    installerBridge->SetRenderingMode(VRRenderingMode::AFR);
    auto installer = MakeRuntime(installerXr, installerBridge);

    std::vector<void*> vtable(30, nullptr);
    vtable[0] = reinterpret_cast<void*>(&MockQueryInterface);
    vtable[1] = reinterpret_cast<void*>(&MockAddRef);
    vtable[2] = reinterpret_cast<void*>(&MockRelease);
    vtable[7] = reinterpret_cast<void*>(&MockGetDevice);
    vtable[kPresentVTableSlot] = reinterpret_cast<void*>(&MockPresent);
    vtable[9] = reinterpret_cast<void*>(&MockGetBuffer);
    vtable[12] = reinterpret_cast<void*>(&MockGetDesc);
    MockComObject mockSwapChain{ vtable.data() };
    auto pSwapChain = reinterpret_cast<IDXGISwapChain*>(&mockSwapChain);
    ASSERT_TRUE(installer->GetDxgiProxy().AttachSwapChain(pSwapChain));

    // A newer runtime that installed nothing must not take over the installer's hook.
    auto newerXr = makeActive();
    auto newerBridge = std::make_shared<VRBridge>(newerXr, nullptr);
    newerBridge->PerformHandshake();
    newerBridge->SetRenderingMode(VRRenderingMode::AFR);
    auto newer = MakeRuntime(newerXr, newerBridge);
    ASSERT_EQ(InjectorRuntime::Current(), newer.get());

    auto pfnPresent = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT)>(vtable[kPresentVTableSlot]);
    EXPECT_EQ(pfnPresent(pSwapChain, 1, 0), S_OK);
    EXPECT_EQ(s_mockPresentCalls, kMockPresentIncrement) << "Present must reach the real function through the installer's original";
    EXPECT_EQ(installerBridge->GetFrameParity(), 1u);
    EXPECT_EQ(newerBridge->GetFrameParity(), 0u);

    // Destroying the installer removes its detour before it leaves the registry.
    installer.reset();
    EXPECT_EQ(pfnPresent(pSwapChain, 1, 0), S_OK);
    EXPECT_EQ(s_mockPresentCalls, 2 * kMockPresentIncrement);
    EXPECT_EQ(newerBridge->GetFrameParity(), 0u) << "an unhooked Present must not reach any proxy";
}

TEST(DxgiProxyTest, StandaloneProxyInstallsNoDetours) {
    s_mockPresentCalls = 0;
    auto openXr = std::make_shared<OpenXrManager>(std::make_shared<MockActiveOpenXrDispatch>());
    ASSERT_TRUE(openXr->Initialize());
    ASSERT_TRUE(openXr->CreateSession());
    DxgiProxy proxy(nullptr, openXr, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);

    std::vector<void*> vtable(30, nullptr);
    vtable[0] = reinterpret_cast<void*>(&MockQueryInterface);
    vtable[1] = reinterpret_cast<void*>(&MockAddRef);
    vtable[2] = reinterpret_cast<void*>(&MockRelease);
    vtable[kPresentVTableSlot] = reinterpret_cast<void*>(&MockPresent);
    vtable[12] = reinterpret_cast<void*>(&MockGetDesc);
    MockComObject mockSwapChain{ vtable.data() };
    auto pSwapChain = reinterpret_cast<IDXGISwapChain*>(&mockSwapChain);

    // No runtime owns this proxy, so a hook could not find its original: it must not detour.
    EXPECT_FALSE(proxy.AttachSwapChain(pSwapChain));
    EXPECT_FALSE(proxy.IsSwapChainAttached());
    auto pfnPresent = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT)>(vtable[kPresentVTableSlot]);
    EXPECT_EQ(pfnPresent(pSwapChain, 1, 0), S_OK);
    EXPECT_EQ(s_mockPresentCalls, kMockPresentIncrement);
}

TEST(DxgiProxyTest, ProxyWithNullCollaboratorsForwardsSafely) {
    DxgiProxy proxy(nullptr, nullptr, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    EXPECT_EQ(proxy.GetOpenXrManager(), nullptr);
    EXPECT_EQ(proxy.GetSystemDxgi(), nullptr);
    EXPECT_EQ(proxy.GetLuauInterceptor(), nullptr);
    EXPECT_EQ(proxy.GetVrBridge(), nullptr);
    EXPECT_FALSE(proxy.IsActive());

    // No NGX interceptor, OpenXR manager or system DXGI: initialization is skipped, the call fails cleanly.
    void* factory = nullptr;
    EXPECT_EQ(proxy.CreateDXGIFactory(__uuidof(IDXGIFactory), &factory), E_FAIL);
    EXPECT_EQ(factory, nullptr);

    NgxInterceptor ngx;
    ngx.SetDxgiProxy(&proxy);
    EXPECT_EQ(ngx.GetOpenXrManager(), nullptr);
    ngx.SetDxgiProxy(nullptr);
}

TEST(InjectorRuntimeTest, CurrentIsTheNewestLiveRuntimeWhateverTheDestructionOrder) {
    auto make = []() {
        InjectorRuntime::Dependencies deps;
        deps.openXrManager = std::make_shared<OpenXrManager>(std::make_shared<MockFailingOpenXrDispatch>());
        return std::make_unique<InjectorRuntime>(std::move(deps));
    };
    InjectorRuntime* before = InjectorRuntime::Current();

    auto a = make();
    auto b = make();
    auto c = make();
    EXPECT_EQ(InjectorRuntime::Current(), c.get());

    b.reset();  // not the newest: Current() is unchanged
    EXPECT_EQ(InjectorRuntime::Current(), c.get());

    c.reset();  // falls back to a, never to the destroyed b
    EXPECT_EQ(InjectorRuntime::Current(), a.get());

    a.reset();
    EXPECT_EQ(InjectorRuntime::Current(), before);
}

TEST(InjectorRuntimeTest, AdoptsTheCollaboratorsOfASuppliedVrBridge) {
    auto openXrManager = std::make_shared<OpenXrManager>(std::make_shared<MockFailingOpenXrDispatch>());
    auto ngx = std::make_shared<NgxInterceptor>();
    InjectorRuntime::Dependencies deps;
    deps.vrBridge = std::make_shared<VRBridge>(openXrManager, ngx);
    InjectorRuntime runtime(std::move(deps));

    // The bridge's DLSS tuning and the hooks must reach the same interceptor and OpenXR manager.
    EXPECT_EQ(&runtime.GetOpenXrManager(), openXrManager.get());
    EXPECT_EQ(&runtime.GetNgxInterceptor(), ngx.get());
    EXPECT_EQ(ngx->GetDxgiProxy(), &runtime.GetDxgiProxy());
}

// These need a process with no live runtime, but other tests leave InjectorRuntime::Instance() alive
// in the shared test process. EXPECT_EXIT runs each body in a fresh child process instead; the child
// reports each failed check on stderr and exits with the number of failures.
#define FS25VR_CHILD_CHECK(cond) \
    do { if (!(cond)) { std::fprintf(stderr, "check failed: %s\n", #cond); failures++; } } while (0)

static int PassthroughExportsLeaveNoRuntimeChild() {
    int failures = 0;
    FS25VR_CHILD_CHECK(InjectorRuntime::Current() == nullptr);
    Proxy_PIXGetCaptureState();
    FS25VR_CHILD_CHECK(InjectorRuntime::Current() == nullptr);
    return failures;
}

static int HookEntryPointsFailSafeChild() {
    int failures = 0;
    FS25VR_CHILD_CHECK(InjectorRuntime::Current() == nullptr);
    FS25VR_CHILD_CHECK(DxgiProxy::Intercept_Present(nullptr, 0, 0) == S_OK);
    FS25VR_CHILD_CHECK(DxgiProxy::Intercept_Present1(nullptr, 0, 0, nullptr) == S_OK);
    FS25VR_CHILD_CHECK(DxgiProxy::Intercept_ResizeBuffers(nullptr, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0) == E_FAIL);
    FS25VR_CHILD_CHECK(DxgiProxy::Intercept_ResizeBuffers1(nullptr, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0, nullptr, nullptr) == E_FAIL);
    FS25VR_CHILD_CHECK(DxgiProxy::Intercept_CreateSwapChain(nullptr, nullptr, nullptr, nullptr) == E_FAIL);
    FS25VR_CHILD_CHECK(DxgiProxy::Intercept_CreateSwapChainForHwnd(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == E_FAIL);
    FS25VR_CHILD_CHECK(LuauInterceptor::DetourEntryPoint(nullptr) == 0);

    // The Win32 hooks fall through to the real, undetoured functions.
    HWND wnd = CreateWindowA("STATIC", "NoRuntimeWnd", WS_OVERLAPPEDWINDOW, 10, 10, 640, 480, nullptr, nullptr, nullptr, nullptr);
    FS25VR_CHILD_CHECK(wnd != nullptr);
    if (wnd) {
        RECT rc{};
        FS25VR_CHILD_CHECK(MirrorWindowController::Intercept_GetClientRect(wnd, &rc));
        POINT pt{ 100, 100 };
        FS25VR_CHILD_CHECK(MirrorWindowController::Intercept_ScreenToClient(wnd, &pt));
        FS25VR_CHILD_CHECK(MirrorWindowController::Intercept_ClipCursor(nullptr));
        DestroyWindow(wnd);
    }
    return failures;
}

#undef FS25VR_CHILD_CHECK

TEST(InjectorRuntimeTest, PassthroughExportsDoNotConstructTheRuntime) {
    EXPECT_EXIT(std::exit(PassthroughExportsLeaveNoRuntimeChild()), ::testing::ExitedWithCode(0), "");
}

TEST(InjectorRuntimeTest, HookEntryPointsFailSafeWithNoLiveRuntime) {
    EXPECT_EXIT(std::exit(HookEntryPointsFailSafeChild()), ::testing::ExitedWithCode(0), "");
}

TEST(DxgiProxyTest, AttachFactoryHooksCreateSwapChain) {
    s_mockCreateSwapChainCalls = 0;
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    auto proxyRuntime = MakeRuntime(openXrManager, vrBridge);
    DxgiProxy& proxy = proxyRuntime->GetDxgiProxy();

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

TEST(DxgiProxyTest, OnPresentPacesAfrFromTheFirstFrame) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::AFR);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
}

TEST(DxgiProxyTest, EnforcesStrictOpenXrLifecycleAcrossAfrPresentation) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    mockDispatch->enforceCallOrder = true;

    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    vrBridge->PerformHandshake();
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

    // AFR pairs submit without call order error from the very first frame
    proxy.OnPresent(nullptr, 0, 0);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
    EXPECT_FALSE(openXrManager->IsFrameBegun());

    proxy.OnPresent(nullptr, 0, 0);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 2u);
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

    DxgiProxy proxy(nullptr, nullptr, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
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

    DxgiProxy proxy(nullptr, nullptr, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
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

    auto proxyRuntime = MakeRuntime(nullptr);
    DxgiProxy& proxy = proxyRuntime->GetDxgiProxy();
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

__declspec(noinline) void STDMETHODCALLTYPE MockExecuteCommandLists(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) {
    static volatile int calls = 0;
    calls = calls + 1;
}

TEST(DxgiProxyTest, ResolvingTheD3D12QueueInstallsNoCommandQueueHooks) {
    // ADR-0017: the Injector installs only DXGI hooks; the game's command queue is never detoured.
    constexpr size_t kExecuteCommandListsSlot = 10;
    MockD3D12Environment env;
    env.queueVTable[kExecuteCommandListsSlot] = reinterpret_cast<void*>(&MockExecuteCommandLists);

    const void* slotBefore = env.queueVTable[kExecuteCommandListsSlot];
    std::array<uint8_t, 16> codeBefore{};
    std::memcpy(codeBefore.data(), slotBefore, codeBefore.size());

    auto proxyRuntime = MakeRuntime(nullptr);
    DxgiProxy& proxy = proxyRuntime->GetDxgiProxy();
    DxgiProxy::Intercept_CreateSwapChain(nullptr, env.GetQueueUnknown(), nullptr, nullptr);
    ASSERT_EQ(proxy.GetD3D12CommandQueue(), env.GetQueue());
    proxy.SetD3D12DeviceAndQueue(env.GetDevice(), env.GetQueue());

    std::array<uint8_t, 16> codeAfter{};
    std::memcpy(codeAfter.data(), env.queueVTable[kExecuteCommandListsSlot], codeAfter.size());
    EXPECT_EQ(env.queueVTable[kExecuteCommandListsSlot], slotBefore) << "queue vtable must not be patched";
    EXPECT_EQ(codeAfter, codeBefore) << "ExecuteCommandLists must not be detoured";

    proxy.SetD3D12DeviceAndQueue(nullptr, nullptr);
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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

    // 4 frames in menu mode -> 4 quad submissions, frame parity never advances to 1
    for (int i = 0; i < 4; ++i) {
        proxy.OnPresent(nullptr, 0, 0);
        EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), static_cast<uint64_t>(i + 1));
        EXPECT_EQ(vrBridge->GetFrameParity(), 0u);
    }
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
}

TEST(DxgiProxyTest, OnPresentPollsEventsPausesVrWhileStoppedAndResumesOnReady) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

    // READY is consumed by the first present, which then submits a quad frame.
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_TRUE(openXrManager->IsSessionRunning());
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);

    // STOPPING: no VR submissions, but the game keeps presenting to the mirror window.
    mockDispatch->events.PushSessionState(XR_SESSION_STATE_STOPPING);
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_FALSE(openXrManager->IsSessionRunning());
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);

    // READY again: VR resumes.
    mockDispatch->events.PushSessionState(XR_SESSION_STATE_READY);
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_TRUE(openXrManager->IsSessionRunning());
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 2u);
}

TEST(DxgiProxyTest, KeepsGameSyncIntervalWhileSessionIsPaused) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

    // Before READY nothing calls xrWaitFrame, so the game's vsync must stay in effect.
    EXPECT_EQ(proxy.EffectiveSyncInterval(1), 1u);

    proxy.OnPresent(nullptr, 1, 0);
    ASSERT_TRUE(openXrManager->IsSessionRunning());
    EXPECT_EQ(proxy.EffectiveSyncInterval(1), 0u);

    mockDispatch->events.PushSessionState(XR_SESSION_STATE_STOPPING);
    proxy.OnPresent(nullptr, 1, 0);
    ASSERT_FALSE(openXrManager->IsSessionRunning());
    EXPECT_EQ(proxy.EffectiveSyncInterval(1), 1u);

    mockDispatch->events.PushSessionState(XR_SESSION_STATE_READY);
    proxy.OnPresent(nullptr, 1, 0);
    EXPECT_EQ(proxy.EffectiveSyncInterval(1), 0u);
}

TEST(DxgiProxyTest, KeepsGameSyncIntervalWhileSessionRunsWithoutEyeSwapchains) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    // CreateEyeSwapchains is deliberately skipped: it failed, so OnPresent never calls xrWaitFrame.

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

    proxy.OnPresent(nullptr, 1, 0);
    ASSERT_TRUE(openXrManager->IsSessionRunning());
    ASSERT_FALSE(openXrManager->HasEyeSwapchains());
    EXPECT_EQ(proxy.EffectiveSyncInterval(1), 1u);

    // Once eye swapchains exist, xrWaitFrame paces frames again.
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));
    EXPECT_EQ(proxy.EffectiveSyncInterval(1), 0u);
}

TEST(DxgiProxyTest, AfrResumesWithLeftEyeAfterSessionPause) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::AFR);

    // Pause right after the left eye of a pair.
    proxy.OnPresent(nullptr, 0, 0);
    ASSERT_EQ(vrBridge->GetFrameParity(), 1u);
    mockDispatch->events.PushSessionState(XR_SESSION_STATE_STOPPING);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(vrBridge->GetFrameParity(), 0u);

    // Resume: the first frame is a fresh left eye, not the right half of the abandoned pair.
    mockDispatch->events.PushSessionState(XR_SESSION_STATE_READY);
    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
    EXPECT_EQ(vrBridge->GetFrameParity(), 1u);

    proxy.OnPresent(nullptr, 0, 0);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 1u);
}

TEST(DxgiProxyTest, PendingRenderingModeLatchesWhileSessionIsPaused) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::SequentialDualPass);

    proxy.OnPresent(nullptr, 0, 0);  // consumes READY
    mockDispatch->events.PushSessionState(XR_SESSION_STATE_STOPPING);
    proxy.OnPresent(nullptr, 0, 0);
    ASSERT_FALSE(openXrManager->IsSessionRunning());

    // Headset asleep: no eye pair will be submitted, so the change must not wait for one.
    vrBridge->SetPendingRenderingMode(VRRenderingMode::AFR);
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_FALSE(vrBridge->GetPendingRenderingMode().has_value());
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);
    EXPECT_EQ(openXrManager->GetSubmittedFramePairCount(), 0u);
}

TEST(DxgiProxyTest, PendingRenderingModeLatchesOnMenuQuadFrame) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::SequentialDualPass);
    vrBridge->SetMenuVisible(true);

    vrBridge->SetPendingRenderingMode(VRRenderingMode::AFR);
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);
    EXPECT_FALSE(vrBridge->GetPendingRenderingMode().has_value());
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);
}

TEST(DxgiProxyTest, PendingRenderingModeLatchesWhenNoEyeSwapchainsExist) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    mockDispatch->failCreateSwapchain = true;
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    EXPECT_FALSE(openXrManager->CreateEyeSwapchains(1920, 1080));
    ASSERT_FALSE(openXrManager->HasEyeSwapchains());
    ASSERT_TRUE(openXrManager->IsActive());

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::SequentialDualPass);

    // No eye pair can ever be in flight, so the queued change must not wait for a pair boundary.
    vrBridge->SetPendingRenderingMode(VRRenderingMode::AFR);
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_FALSE(vrBridge->GetPendingRenderingMode().has_value());
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);
}

TEST(DxgiProxyTest, PendingRenderingModeLatchesWhenDroppingToPassThrough) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);
    vrBridge->PerformHandshake();
    vrBridge->SetRenderingMode(VRRenderingMode::SequentialDualPass);

    // Left eye of a pair is in flight, then the mode is queued and the runtime ends the session.
    proxy.OnPresent(nullptr, 0, 0);
    ASSERT_EQ(vrBridge->GetSubPassIndex(), 1u);
    vrBridge->SetPendingRenderingMode(VRRenderingMode::AFR);
    mockDispatch->events.PushSessionState(XR_SESSION_STATE_EXITING);
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    ASSERT_TRUE(openXrManager->IsPassThrough());
    EXPECT_FALSE(vrBridge->GetPendingRenderingMode().has_value());
    EXPECT_EQ(vrBridge->GetRenderingMode(), VRRenderingMode::AFR);
}

TEST(DxgiProxyTest, OnPresentContinuesInPassThroughAfterLossPending) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    ASSERT_EQ(openXrManager->GetSubmittedQuadFrameCount(), 1u);

    mockDispatch->events.PushSessionState(XR_SESSION_STATE_LOSS_PENDING);
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_TRUE(openXrManager->IsPassThrough());
    EXPECT_TRUE(proxy.OnPresent(nullptr, 0, 0));
    EXPECT_FALSE(proxy.IsPresentSuppressed());
}

TEST(DxgiProxyTest, SeamlesslyTransitionsBetweenQuadAndStereoOnCompanionHandshakeAndUnload) {
    auto mockDispatch = std::make_shared<MockActiveOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateTrackingSpaces());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains(1920, 1080));

    auto vrBridge = std::make_shared<VRBridge>(openXrManager, nullptr);
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    mockDispatch->tracking.SetHeadPose(simPose);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

    // Set head pose with offset and yaw
    HeadPose simPose;
    simPose.position = Vector3(1.0f, 1.6f, -2.0f);
    simPose.rotation = EulerAngles(0.0f, 0.0f, 0.0f);
    simPose.isValid = true;
    mockDispatch->tracking.SetHeadPose(simPose);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

    // Should not crash with null swapchain / null HWND
    EXPECT_NO_THROW(proxy.GetWindowController().UpdateMenuCursorState(nullptr, openXrManager.get()));
    EXPECT_FALSE(openXrManager->GetMenuCursorState().visible);
}

TEST(DxgiProxyTest, ScreenToNormalizedClientHandlesNullOrInvalidHwndSafely) {
    float normX = 0.0f, normY = 0.0f;
    POINT pt{ 100, 100 };
    EXPECT_FALSE(MirrorWindowController::ScreenToNormalizedClient(nullptr, pt, normX, normY));
    EXPECT_FALSE(MirrorWindowController::ScreenToNormalizedClient(reinterpret_cast<HWND>(static_cast<uintptr_t>(0xDEADBEEF)), pt, normX, normY));
}

TEST(DxgiProxyTest, ScreenToNormalizedClientNormalizesAndClipsBounds) {
    HWND testWnd = CreateWindowA("STATIC", "TestWindow", WS_POPUP, 100, 100, 200, 200, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd != nullptr);

    float normX = -1.0f, normY = -1.0f;
    // Point inside window (client center: screen x=200, y=200)
    POINT centerPt{ 200, 200 };
    EXPECT_TRUE(MirrorWindowController::ScreenToNormalizedClient(testWnd, centerPt, normX, normY));
    EXPECT_NEAR(normX, 0.5f, 0.05f);
    EXPECT_NEAR(normY, 0.5f, 0.05f);

    // Point outside client bounds (screen x=50, y=50)
    POINT outsidePt{ 50, 50 };
    EXPECT_FALSE(MirrorWindowController::ScreenToNormalizedClient(testWnd, outsidePt, normX, normY));

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
    DxgiProxy proxy(nullptr, openXrManager, nullptr, vrBridge, std::make_shared<MirrorWindowController>(), nullptr);

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
    RECT res1080 = MirrorWindowController::ComputeClampedMirrorWindowRect(work1080, WS_POPUP, 0, false);
    EXPECT_EQ(res1080.left, 0);
    EXPECT_EQ(res1080.top, 0);
    EXPECT_EQ(res1080.right, 1920);
    EXPECT_EQ(res1080.bottom, 1080);

    // 1440p borderless
    RECT work1440{ 0, 0, 2560, 1440 };
    RECT res1440 = MirrorWindowController::ComputeClampedMirrorWindowRect(work1440, WS_POPUP, 0, false);
    EXPECT_EQ(res1440.left, 0);
    EXPECT_EQ(res1440.top, 0);
    EXPECT_EQ(res1440.right, 2560);
    EXPECT_EQ(res1440.bottom, 1440);

    // 4K borderless
    RECT work4k{ 0, 0, 3840, 2160 };
    RECT res4k = MirrorWindowController::ComputeClampedMirrorWindowRect(work4k, WS_POPUP, 0, false);
    EXPECT_EQ(res4k.left, 0);
    EXPECT_EQ(res4k.top, 0);
    EXPECT_EQ(res4k.right, 3840);
    EXPECT_EQ(res4k.bottom, 2160);

    // 720p borderless
    RECT work720{ 0, 0, 1280, 720 };
    RECT res720 = MirrorWindowController::ComputeClampedMirrorWindowRect(work720, WS_POPUP, 0, false);
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
        RECT res = MirrorWindowController::ComputeClampedMirrorWindowRect(workArea, WS_OVERLAPPEDWINDOW, 0, false);
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
    RECT res2560 = MirrorWindowController::ComputeClampedMirrorWindowRect(work2560x1080, WS_POPUP, 0, false);
    EXPECT_EQ(res2560.left, 320);
    EXPECT_EQ(res2560.top, 0);
    EXPECT_EQ(res2560.right, 2240);
    EXPECT_EQ(res2560.bottom, 1080);
    EXPECT_EQ(res2560.right - res2560.left, 1920);
    EXPECT_EQ(res2560.bottom - res2560.top, 1080);

    // 21:9 3440x1440 borderless
    RECT work3440x1440{ 0, 0, 3440, 1440 };
    RECT res3440 = MirrorWindowController::ComputeClampedMirrorWindowRect(work3440x1440, WS_POPUP, 0, false);
    EXPECT_EQ(res3440.left, 440);
    EXPECT_EQ(res3440.top, 0);
    EXPECT_EQ(res3440.right, 3000);
    EXPECT_EQ(res3440.bottom, 1440);
    EXPECT_EQ(res3440.right - res3440.left, 2560);
    EXPECT_EQ(res3440.bottom - res3440.top, 1440);

    // 32:9 5120x1440 super-ultrawide borderless
    RECT work5120x1440{ 0, 0, 5120, 1440 };
    RECT res5120 = MirrorWindowController::ComputeClampedMirrorWindowRect(work5120x1440, WS_POPUP, 0, false);
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
    RECT res1080 = MirrorWindowController::ComputeClampedMirrorWindowRect(work1080x1920, WS_POPUP, 0, false);
    EXPECT_EQ(res1080.left, 0);
    EXPECT_EQ(res1080.right, 1080);
    int expectedH1080 = (1080 * 9) / 16; // 607
    EXPECT_EQ(res1080.bottom - res1080.top, expectedH1080);
    EXPECT_EQ(res1080.top, (1920 - expectedH1080) / 2);
    EXPECT_EQ(res1080.bottom, res1080.top + expectedH1080);

    // 9:16 1440x2560 borderless
    RECT work1440x2560{ 0, 0, 1440, 2560 };
    RECT res1440 = MirrorWindowController::ComputeClampedMirrorWindowRect(work1440x2560, WS_POPUP, 0, false);
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
    RECT resWindowed = MirrorWindowController::ComputeClampedMirrorWindowRect(workPortraitWindowed, WS_OVERLAPPEDWINDOW, 0, false);
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
    RECT resRight = MirrorWindowController::ComputeClampedMirrorWindowRect(workRight, WS_POPUP, 0, false);
    EXPECT_EQ(resRight.left, 1920);
    EXPECT_EQ(resRight.top, 0);
    EXPECT_EQ(resRight.right, 3840);
    EXPECT_EQ(resRight.bottom, 1080);

    // Secondary 1440p monitor positioned to the left and shifted down
    RECT workLeft{ -2560, 100, 0, 1540 };
    RECT resLeft = MirrorWindowController::ComputeClampedMirrorWindowRect(workLeft, WS_POPUP, 0, false);
    EXPECT_EQ(resLeft.left, -2560);
    EXPECT_EQ(resLeft.top, 100);
    EXPECT_EQ(resLeft.right, 0);
    EXPECT_EQ(resLeft.bottom, 1540);

    // Secondary ultrawide 21:9 monitor on negative coordinates
    RECT workLeftUltrawide{ -3440, 0, 0, 1440 };
    RECT resLeftUW = MirrorWindowController::ComputeClampedMirrorWindowRect(workLeftUltrawide, WS_POPUP, 0, false);
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
    RECT resZero = MirrorWindowController::ComputeClampedMirrorWindowRect(zeroWork, WS_POPUP, 0, false);
    EXPECT_EQ(resZero.left, 100);
    EXPECT_EQ(resZero.right, 100);

    RECT invertedWork{ 200, 200, 100, 100 };
    RECT resInverted = MirrorWindowController::ComputeClampedMirrorWindowRect(invertedWork, WS_POPUP, 0, false);
    EXPECT_EQ(resInverted.left, 200);
    EXPECT_EQ(resInverted.right, 100);

    // Work area smaller than non-client frame borders
    RECT tinyWork{ 0, 0, 10, 10 };
    RECT resTiny = MirrorWindowController::ComputeClampedMirrorWindowRect(tinyWork, WS_OVERLAPPEDWINDOW, 0, false);
    EXPECT_EQ(resTiny.left, 0);
    EXPECT_EQ(resTiny.top, 0);
    EXPECT_EQ(resTiny.right, 10);
    EXPECT_EQ(resTiny.bottom, 10);

    // HWND overload: null and invalid handles return zero RECT
    RECT nullRes = MirrorWindowController::ComputeClampedMirrorWindowRect(static_cast<HWND>(nullptr));
    EXPECT_EQ(nullRes.left, 0);
    EXPECT_EQ(nullRes.top, 0);
    EXPECT_EQ(nullRes.right, 0);
    EXPECT_EQ(nullRes.bottom, 0);

    HWND invalidHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0xDEADBEEF));
    RECT invalidRes = MirrorWindowController::ComputeClampedMirrorWindowRect(invalidHwnd);
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

    RECT hwndRes = MirrorWindowController::ComputeClampedMirrorWindowRect(testWnd);
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

        DxgiProxy ptProxy(nullptr, ptOpenXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
        ptProxy.GetWindowController().SetGameHwnd(testWnd);

        EXPECT_FALSE(ptProxy.GetWindowController().IsWindowSubclassed());
        EXPECT_FALSE(ptProxy.GetWindowController().AttachWindowSubclass(testWnd));
    }

    // 2. Active VR Mode: attaches subclass cleanly
    {
        auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
        auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
        ASSERT_TRUE(activeOpenXrManager->Initialize());
        ASSERT_TRUE(activeOpenXrManager->CreateSession());
        EXPECT_TRUE(activeOpenXrManager->IsActive());

        DxgiProxy activeProxy(nullptr, activeOpenXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
        activeProxy.GetWindowController().SetGameHwnd(testWnd);

        EXPECT_TRUE(activeProxy.GetWindowController().IsWindowSubclassed());
        EXPECT_EQ(activeProxy.GetWindowController().GetGameHwnd(), testWnd);

        activeProxy.DetachAll();
        EXPECT_FALSE(activeProxy.GetWindowController().IsWindowSubclassed());
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

    DxgiProxy activeProxy(nullptr, activeOpenXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    activeProxy.GetWindowController().SetGameHwnd(testWnd);
    ASSERT_TRUE(activeProxy.GetWindowController().IsWindowSubclassed());

    RECT expectedClamped = MirrorWindowController::ComputeClampedMirrorWindowRect(testWnd);
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

        auto activeProxyRuntime = MakeRuntime(activeOpenXrManager);
        DxgiProxy& activeProxy = activeProxyRuntime->GetDxgiProxy();

        RECT expectedClamped = MirrorWindowController::ComputeClampedMirrorWindowRect(testWnd);
        int expectedW = expectedClamped.right - expectedClamped.left;
        int expectedH = expectedClamped.bottom - expectedClamped.top;

        DxgiProxy::Intercept_CreateSwapChainForHwnd(nullptr, nullptr, testWnd, nullptr, nullptr, nullptr, nullptr);

        EXPECT_TRUE(activeProxy.GetWindowController().IsWindowSubclassed());
        EXPECT_EQ(activeProxy.GetWindowController().GetGameHwnd(), testWnd);

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

        auto ptProxyRuntime = MakeRuntime(ptOpenXrManager);
        DxgiProxy& ptProxy = ptProxyRuntime->GetDxgiProxy();

        DxgiProxy::Intercept_CreateSwapChainForHwnd(nullptr, nullptr, ptWnd, nullptr, nullptr, nullptr, nullptr);

        EXPECT_FALSE(ptProxy.GetWindowController().IsWindowSubclassed());

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

        DxgiProxy activeProxy(nullptr, activeOpenXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
        activeProxy.GetWindowController().SetGameHwnd(testWnd1);
        EXPECT_TRUE(activeProxy.GetWindowController().IsWindowSubclassed());

        DestroyWindow(testWnd1);
        EXPECT_FALSE(activeProxy.GetWindowController().IsWindowSubclassed());
        EXPECT_EQ(activeProxy.GetWindowController().GetGameHwnd(), nullptr);
    }

    // 2. DetachAll cleanly unhooks subclass before window destruction
    HWND testWnd2 = CreateWindowA("STATIC", "DetachAllTestWnd", WS_OVERLAPPEDWINDOW, 50, 50, 1000, 800, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(testWnd2 != nullptr);
    {
        auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
        auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
        ASSERT_TRUE(activeOpenXrManager->Initialize());
        ASSERT_TRUE(activeOpenXrManager->CreateSession());

        DxgiProxy activeProxy(nullptr, activeOpenXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
        activeProxy.GetWindowController().SetGameHwnd(testWnd2);
        EXPECT_TRUE(activeProxy.GetWindowController().IsWindowSubclassed());

        activeProxy.DetachAll();
        EXPECT_FALSE(activeProxy.GetWindowController().IsWindowSubclassed());

        DestroyWindow(testWnd2);
    }
}

TEST(DxgiProxyTest, InterceptCreateSwapChainForHwndLeavesSwapChainUntouchedWhenFormatUnsupported) {
    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();  // offers R8G8B8A8_UNORM only
    auto openXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    auto proxyRuntime = MakeRuntime(openXrManager);
    DxgiProxy& proxy = proxyRuntime->GetDxgiProxy();

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 3840;
    desc.Height = 2160;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.Scaling = DXGI_SCALING_NONE;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc{};
    fullscreenDesc.Windowed = FALSE;

    DxgiProxy::Intercept_CreateSwapChainForHwnd(nullptr, nullptr, nullptr, &desc, &fullscreenDesc, nullptr, nullptr);

    EXPECT_TRUE(openXrManager->IsPassThrough());
    EXPECT_FALSE(openXrManager->HasSession());
    EXPECT_EQ(desc.Scaling, DXGI_SCALING_NONE);
    EXPECT_TRUE(fullscreenDesc.Windowed == FALSE);
    EXPECT_EQ(proxy.GetWindowController().GetTargetRenderWidth(), 0u);
}

TEST(DxgiProxyTest, InterceptCreateSwapChainLeavesSwapChainUntouchedWhenFormatUnsupported) {
    auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();  // offers R8G8B8A8_UNORM only
    auto openXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->CreateSession());
    auto proxyRuntime = MakeRuntime(openXrManager);

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 3840;
    desc.BufferDesc.Height = 2160;
    desc.BufferDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.Windowed = FALSE;

    DxgiProxy::Intercept_CreateSwapChain(nullptr, nullptr, &desc, nullptr);

    EXPECT_TRUE(openXrManager->IsPassThrough());
    EXPECT_FALSE(openXrManager->HasSession());
    EXPECT_TRUE(desc.Windowed == FALSE);
}

TEST(DxgiProxyTest, InterceptCreateSwapChainForHwndEnforcesStretchScalingInActiveVrMode) {
    // 1. Active VR Mode: mutates pDesc->Scaling to DXGI_SCALING_STRETCH
    {
        auto mockActiveDispatch = std::make_shared<MockActiveOpenXrDispatch>();
        auto activeOpenXrManager = std::make_shared<OpenXrManager>(mockActiveDispatch);
        ASSERT_TRUE(activeOpenXrManager->Initialize());
        ASSERT_TRUE(activeOpenXrManager->CreateSession());
        EXPECT_TRUE(activeOpenXrManager->IsActive());

        auto activeProxyRuntime = MakeRuntime(activeOpenXrManager);

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

        auto ptProxyRuntime = MakeRuntime(ptOpenXrManager);

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

        auto activeProxyRuntime = MakeRuntime(activeOpenXrManager);
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

    auto activeProxyRuntime = MakeRuntime(activeOpenXrManager);
    DxgiProxy& activeProxy = activeProxyRuntime->GetDxgiProxy();

    // Initial clamp applies and subclasses the window to fit the monitor work area
    EXPECT_TRUE(activeProxy.GetWindowController().ApplyInitialWindowClamp(testWnd));
    activeProxy.GetWindowController().SetGameHwnd(testWnd);
    EXPECT_TRUE(activeProxy.GetWindowController().IsWindowSubclassed());

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
    EXPECT_TRUE(MirrorWindowController::ScreenToNormalizedClient(testWnd, topLeft, normX, normY));
    EXPECT_FLOAT_EQ(normX, 0.0f);
    EXPECT_FLOAT_EQ(normY, 0.0f);

    // Top-Right corner maps to (1.0, 0.0)
    EXPECT_TRUE(MirrorWindowController::ScreenToNormalizedClient(testWnd, topRight, normX, normY));
    EXPECT_FLOAT_EQ(normX, 1.0f);
    EXPECT_FLOAT_EQ(normY, 0.0f);

    // Bottom-Left corner maps to (0.0, 1.0)
    EXPECT_TRUE(MirrorWindowController::ScreenToNormalizedClient(testWnd, bottomLeft, normX, normY));
    EXPECT_FLOAT_EQ(normX, 0.0f);
    EXPECT_FLOAT_EQ(normY, 1.0f);

    // Bottom-Right corner maps to (1.0, 1.0)
    EXPECT_TRUE(MirrorWindowController::ScreenToNormalizedClient(testWnd, bottomRight, normX, normY));
    EXPECT_FLOAT_EQ(normX, 1.0f);
    EXPECT_FLOAT_EQ(normY, 1.0f);

    // Center maps to (0.5, 0.5)
    EXPECT_TRUE(MirrorWindowController::ScreenToNormalizedClient(testWnd, centerPoint, normX, normY));
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

    DxgiProxy activeProxy(nullptr, activeOpenXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    activeProxy.GetWindowController().SetGameHwnd(testWnd);

    // Initial state: window was deliberately set oversized (e.g. 5000x4000) at (80, 90)
    SetWindowPos(testWnd, nullptr, 80, 90, 5000, 4000, SWP_NOZORDER | SWP_NOACTIVATE);

    RECT expectedClamped = MirrorWindowController::ComputeClampedMirrorWindowRect(testWnd);
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

    DxgiProxy activeProxy(nullptr, activeOpenXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    activeProxy.GetWindowController().AttachWindowSubclass(testWnd);

    RECT expectedClamped = MirrorWindowController::ComputeClampedMirrorWindowRect(testWnd);
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

    DxgiProxy activeProxy(nullptr, activeOpenXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    activeProxy.GetWindowController().SetGameHwnd(testWnd);

    // Initial state: not in size/move
    EXPECT_FALSE(activeProxy.GetWindowController().IsInSizeMove());

    // Initial clamp
    activeProxy.OnPresent(nullptr, 0, 0);

    // User starts dragging/moving window (WM_ENTERSIZEMOVE)
    SendMessageA(testWnd, WM_ENTERSIZEMOVE, 0, 0);
    EXPECT_TRUE(activeProxy.GetWindowController().IsInSizeMove());

    // OnPresent returns immediately during modal move/size and does not alter window
    activeProxy.OnPresent(nullptr, 0, 0);
    EXPECT_TRUE(activeProxy.GetWindowController().IsInSizeMove());

    // User finishes dragging/moving window (WM_EXITSIZEMOVE)
    SendMessageA(testWnd, WM_EXITSIZEMOVE, 0, 0);
    EXPECT_FALSE(activeProxy.GetWindowController().IsInSizeMove());

    // Next frame present enforces clamping cleanly
    activeProxy.OnPresent(nullptr, 0, 0);
    RECT postDragRect{};
    GetWindowRect(testWnd, &postDragRect);
    RECT expectedClamped = MirrorWindowController::ComputeClampedMirrorWindowRect(testWnd);
    int maxW = expectedClamped.right - expectedClamped.left;
    int maxH = expectedClamped.bottom - expectedClamped.top;
    EXPECT_EQ(postDragRect.right - postDragRect.left, maxW);
    EXPECT_EQ(postDragRect.bottom - postDragRect.top, maxH);

    activeProxy.DetachAll();
    EXPECT_FALSE(activeProxy.GetWindowController().IsInSizeMove());
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

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockResizeBuffersImpl(IDXGISwapChain*, UINT, UINT Width, UINT Height, DXGI_FORMAT, UINT) {
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

    DxgiProxy activeProxy(nullptr, activeOpenXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    activeProxy.GetWindowController().SetTargetRenderResolution(3840, 2160);
    EXPECT_TRUE(activeProxy.GetWindowController().AttachWindowSubclass(testWnd));

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

    auto activeProxyRuntime = MakeRuntime(activeOpenXrManager);
    DxgiProxy& activeProxy = activeProxyRuntime->GetDxgiProxy();
    activeProxy.GetWindowController().SetTargetRenderResolution(3840, 2160);
    EXPECT_TRUE(activeProxy.GetWindowController().AttachWindowSubclass(testWnd));
    EXPECT_TRUE(activeProxy.GetWindowController().IsDetourGetClientRectInstalled());
    EXPECT_TRUE(activeProxy.GetWindowController().IsDetourScreenToClientInstalled());
    EXPECT_TRUE(activeProxy.GetWindowController().IsDetourSetCursorPosInstalled());
    EXPECT_TRUE(activeProxy.GetWindowController().IsDetourClipCursorInstalled());

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
    activeProxy.GetWindowController().DetachWindowSubclass();
    EXPECT_TRUE(GetClientRect(testWnd, &realPhysRc));
    int physW = realPhysRc.right - realPhysRc.left;
    int physH = realPhysRc.bottom - realPhysRc.top;
    ASSERT_GT(physW, 0);
    ASSERT_GT(physH, 0);

    // Reattach subclass and detours
    EXPECT_TRUE(activeProxy.GetWindowController().AttachWindowSubclass(testWnd));

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
    EXPECT_TRUE(MirrorWindowController::ScreenToNormalizedClient(testWnd, screenPt, normX, normY));
    EXPECT_FLOAT_EQ(normX, 1.0f);
    EXPECT_FLOAT_EQ(normY, 1.0f);

    activeProxy.DetachAll();
    EXPECT_FALSE(activeProxy.GetWindowController().IsDetourGetClientRectInstalled());
    EXPECT_FALSE(activeProxy.GetWindowController().IsDetourScreenToClientInstalled());
    EXPECT_FALSE(activeProxy.GetWindowController().IsDetourSetCursorPosInstalled());
    EXPECT_FALSE(activeProxy.GetWindowController().IsDetourClipCursorInstalled());

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

    auto activeProxyRuntime = MakeRuntime(activeOpenXrManager);
    DxgiProxy& activeProxy = activeProxyRuntime->GetDxgiProxy();
    activeProxy.GetWindowController().SetTargetRenderResolution(3840, 2160);
    EXPECT_TRUE(activeProxy.GetWindowController().AttachWindowSubclass(testWnd));
    EXPECT_TRUE(activeProxy.GetWindowController().IsDetourSetCursorPosInstalled());
    EXPECT_TRUE(activeProxy.GetWindowController().IsDetourClipCursorInstalled());

    // Query physical client rect using InternalWindowQueryScope
    RECT physRc{};
    {
        activeProxy.GetWindowController().DetachWindowSubclass();
        EXPECT_TRUE(GetClientRect(testWnd, &physRc));
        EXPECT_TRUE(activeProxy.GetWindowController().AttachWindowSubclass(testWnd));
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
    EXPECT_TRUE(activeProxy.GetWindowController().ScaleVirtualCursorToPhysical(testWnd, warpVirtualCenter.x, warpVirtualCenter.y, computedPhysX, computedPhysY));
    EXPECT_NEAR(computedPhysX, expectedPhysCenterX, 1);
    EXPECT_NEAR(computedPhysY, expectedPhysCenterY, 1);

    // Specifically verify it does NOT set x to physTopLeft.x + 1920 (which would be near/past right window edge)
    if (physW < 3800) {
        EXPECT_NE(computedPhysX, physTopLeft.x + 1920);
    }

    // 2. Verify virtual clip rectangle constraining calculation:
    RECT virtualClipRc{ physTopLeft.x, physTopLeft.y, physTopLeft.x + 3840, physTopLeft.y + 2160 };
    RECT constrainedClipRc{};
    EXPECT_TRUE(activeProxy.GetWindowController().ConstrainVirtualClipRectToPhysical(testWnd, &virtualClipRc, constrainedClipRc));
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
    EXPECT_FALSE(activeProxy.GetWindowController().IsDetourSetCursorPosInstalled());
    EXPECT_FALSE(activeProxy.GetWindowController().IsDetourClipCursorInstalled());
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

    DxgiProxy activeProxy(nullptr, activeOpenXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    activeProxy.GetWindowController().SetTargetRenderResolution(3840, 2160);
    EXPECT_TRUE(activeProxy.GetWindowController().AttachWindowSubclass(testWnd));

    // Get physical dimensions
    activeProxy.GetWindowController().DetachWindowSubclass();
    RECT physRc{};
    GetClientRect(testWnd, &physRc);
    int physW = physRc.right - physRc.left;
    int physH = physRc.bottom - physRc.top;
    EXPECT_TRUE(activeProxy.GetWindowController().AttachWindowSubclass(testWnd));

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

    auto activeProxyRuntime = MakeRuntime(activeOpenXrManager);
    DxgiProxy& activeProxy = activeProxyRuntime->GetDxgiProxy();
    activeProxy.GetWindowController().SetTargetRenderResolution(3840, 2160);

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


