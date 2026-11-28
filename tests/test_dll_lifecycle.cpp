#include <gtest/gtest.h>
#include "core/detour.h"
#include "core/dll_lifecycle.h"
#include "core/injector_runtime.h"
#include "core/logger.h"
#include "ngx/ngx_interceptor.h"
#include "openxr/openxr_manager.h"
#include "proxy/dxgi_proxy.h"
#include "proxy/system_dxgi.h"
#include <MinHook.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <thread>
#include <vector>

#ifndef FAKE_NVNGX_PATH
#error FAKE_NVNGX_PATH must be defined by the build
#endif

namespace fs25vr {
namespace {

__declspec(noinline) int TerminationTarget(int x) {
    volatile int v = x;
    return v + 1;
}

int TerminationDetour(int x) {
    return -x;
}

HRESULT WINAPI ImmediateCreateFactory(REFIID, void** ppFactory) {
    if (ppFactory) *ppFactory = nullptr;
    return E_NOTIMPL;
}

class ImmediateSystemDxgi : public ISystemDxgi {
public:
    bool LoadSystemDxgi() override { return true; }
    FARPROC GetExport(const char*) override { return nullptr; }
    PFN_CreateDXGIFactory GetCreateDXGIFactory() override { return &ImmediateCreateFactory; }
    PFN_CreateDXGIFactory1 GetCreateDXGIFactory1() override { return &ImmediateCreateFactory; }
    PFN_CreateDXGIFactory2 GetCreateDXGIFactory2() override { return nullptr; }
};

class FailingOpenXrDispatch : public IOpenXrDispatch {
public:
    XrResult EnumerateInstanceExtensionProperties(const char*, uint32_t, uint32_t*, XrExtensionProperties*) override { return XR_ERROR_RUNTIME_UNAVAILABLE; }
    XrResult CreateInstance(const XrInstanceCreateInfo*, XrInstance*) override { return XR_ERROR_RUNTIME_UNAVAILABLE; }
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

class BlockingOpenXrDispatch : public FailingOpenXrDispatch {
public:
    XrResult EnumerateInstanceExtensionProperties(const char*, uint32_t, uint32_t*, XrExtensionProperties*) override {
        entered.set_value();
        released.wait();
        return XR_ERROR_RUNTIME_UNAVAILABLE;
    }

    std::promise<void> entered;
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
};

class DllLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_ngx = std::make_shared<NgxInterceptor>();
    }
    void TearDown() override {
        ResetProcessTerminatingForTesting();
        m_ngx->DetachAll();
        if (m_fakeNgx) {
            FreeLibrary(m_fakeNgx);
            m_fakeNgx = nullptr;
        }
        m_ngx.reset();
    }

    HMODULE LoadFakeNgx() {
        m_fakeNgx = LoadLibraryA(FAKE_NVNGX_PATH);
        return m_fakeNgx;
    }

    HRESULT CreateFactoryThroughNewProxy() {
        auto openXrManager = std::make_shared<OpenXrManager>(std::make_shared<FailingOpenXrDispatch>());
        auto systemDxgi = std::make_shared<SystemDxgi>();
        DxgiProxy proxy(systemDxgi, openXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), m_ngx);
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        return proxy.CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    }

    std::shared_ptr<NgxInterceptor> m_ngx;
    HMODULE m_fakeNgx = nullptr;
};

TEST_F(DllLifecycleTest, ProcessAttachInstallsNoHooksAndDoesNotAttachNgx) {
    ASSERT_NE(LoadFakeNgx(), nullptr);

    OnProcessAttach(GetModuleHandleW(nullptr));

    EXPECT_FALSE(m_ngx->AreModuleHooksInstalled());
    EXPECT_FALSE(m_ngx->IsAttached());
}

TEST_F(DllLifecycleTest, ProcessDetachDuringTerminationSkipsHookRemoval) {
    ASSERT_TRUE(m_ngx->InstallModuleLoadHooks());

    OnProcessDetach(reinterpret_cast<LPVOID>(1));  // non-null lpReserved: process terminating
    EXPECT_TRUE(IsProcessTerminating());
    EXPECT_TRUE(m_ngx->AreModuleHooksInstalled());

    ResetProcessTerminatingForTesting();
    OnProcessDetach(nullptr);
    EXPECT_FALSE(IsProcessTerminating());
    EXPECT_FALSE(m_ngx->AreModuleHooksInstalled());
}

TEST_F(DllLifecycleTest, ProcessDetachUnwindsTheHookOwnerNotTheNewestRuntime) {
    ASSERT_TRUE(m_ngx->InstallModuleLoadHooks());

    InjectorRuntime::Dependencies deps;
    deps.openXrManager = std::make_shared<OpenXrManager>(std::make_shared<FailingOpenXrDispatch>());
    InjectorRuntime newer(std::move(deps));
    ASSERT_NE(&newer.GetNgxInterceptor(), m_ngx.get());

    OnProcessDetach(nullptr);

    EXPECT_FALSE(m_ngx->AreModuleHooksInstalled());
}

TEST_F(DllLifecycleTest, DestructorsRunAfterTerminationDetachLeaveHooksInPlace) {
    ASSERT_EQ(TerminationTarget(5), 6);

    void* patched = nullptr;
    {
        FunctionDetour detour;
        ASSERT_TRUE(detour.Install(reinterpret_cast<void*>(&TerminationTarget), reinterpret_cast<void*>(&TerminationDetour)));
        patched = detour.GetTarget();
        OnProcessDetach(reinterpret_cast<LPVOID>(1));
    }

    EXPECT_EQ(TerminationTarget(5), -5);

    ASSERT_EQ(MH_RemoveHook(patched), MH_OK);
    EXPECT_EQ(TerminationTarget(5), 6);
}

TEST_F(DllLifecycleTest, ConcurrentFirstFactoryCallsReturnOnlyAfterHooksInstalled) {
    constexpr int kRounds = 20;
    constexpr int kThreads = 8;
    std::atomic<int> sawHooksMissing{0};
    for (int round = 0; round < kRounds; ++round) {
        m_ngx->DetachAll();
        auto openXrManager = std::make_shared<OpenXrManager>(std::make_shared<FailingOpenXrDispatch>());
        DxgiProxy proxy(std::make_shared<ImmediateSystemDxgi>(), openXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), m_ngx);

        std::atomic<int> ready{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&]() {
                ready.fetch_add(1);
                while (ready.load() < kThreads) {}
                void* factory = nullptr;
                proxy.CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
                if (!m_ngx->AreModuleHooksInstalled()) {
                    sawHooksMissing.fetch_add(1);
                }
            });
        }
        for (auto& t : threads) t.join();
    }

    EXPECT_EQ(sawHooksMissing.load(), 0);
}

TEST_F(DllLifecycleTest, ConcurrentFactoryCallWaitsForOpenXrInitialization) {
    auto dispatch = std::make_shared<BlockingOpenXrDispatch>();
    auto openXrManager = std::make_shared<OpenXrManager>(dispatch);
    DxgiProxy proxy(std::make_shared<ImmediateSystemDxgi>(), openXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), m_ngx);

    std::thread first([&]() {
        void* factory = nullptr;
        proxy.CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
    });
    dispatch->entered.get_future().wait();

    std::atomic<bool> secondReturned{false};
    std::thread second([&]() {
        void* factory = nullptr;
        proxy.CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
        secondReturned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(secondReturned.load());

    dispatch->release.set_value();
    first.join();
    second.join();
    EXPECT_TRUE(secondReturned.load());
    EXPECT_TRUE(openXrManager->IsPassThrough());
}

TEST_F(DllLifecycleTest, FirstDxgiFactoryCallInstallsHooks) {
    ASSERT_FALSE(m_ngx->AreModuleHooksInstalled());

    EXPECT_EQ(CreateFactoryThroughNewProxy(), S_OK);

    EXPECT_TRUE(m_ngx->AreModuleHooksInstalled());
}

TEST_F(DllLifecycleTest, FirstDxgiFactoryCallAttachesToAlreadyLoadedNgx) {
    ASSERT_NE(LoadFakeNgx(), nullptr);
    ASSERT_FALSE(m_ngx->IsAttached());

    EXPECT_EQ(CreateFactoryThroughNewProxy(), S_OK);

    EXPECT_TRUE(m_ngx->IsAttached());
}

TEST_F(DllLifecycleTest, HooksInstalledOnFactoryCallAttachToNgxLoadedLater) {
    EXPECT_EQ(CreateFactoryThroughNewProxy(), S_OK);
    ASSERT_FALSE(m_ngx->IsAttached());

    ASSERT_NE(LoadFakeNgx(), nullptr);

    EXPECT_TRUE(m_ngx->IsAttached());
}

namespace {

thread_local bool t_pauseInsideOutputDebugString = false;
FunctionDetour* s_outputDebugDetour = nullptr;
std::promise<void>* s_logEnteredPromise = nullptr;
std::shared_future<void>* s_logReleaseFuture = nullptr;

void WINAPI PausingOutputDebugStringA(LPCSTR lpOutputString) {
    if (t_pauseInsideOutputDebugString && s_logEnteredPromise && s_logReleaseFuture) {
        t_pauseInsideOutputDebugString = false;
        s_logEnteredPromise->set_value();
        s_logReleaseFuture->wait_for(std::chrono::seconds(2));
    }
    if (s_outputDebugDetour) {
        auto orig = reinterpret_cast<decltype(&OutputDebugStringA)>(s_outputDebugDetour->GetOriginal());
        if (orig) orig(lpOutputString);
    }
}

FunctionDetour* s_freeLibraryDetour = nullptr;
HMODULE s_watchedFreeModule = nullptr;
std::atomic<int> s_watchedFreeCalls{0};

BOOL WINAPI CountingFreeLibrary(HMODULE hLibModule) {
    if (hLibModule && hLibModule == s_watchedFreeModule) {
        s_watchedFreeCalls.fetch_add(1);
    }
    auto orig = reinterpret_cast<decltype(&FreeLibrary)>(s_freeLibraryDetour->GetOriginal());
    return orig ? orig(hLibModule) : TRUE;
}

struct CountingComObject {
    void* const* vtable;
    std::atomic<ULONG> refCount{1};
    std::atomic<int> releaseCalls{0};

    static ULONG STDMETHODCALLTYPE AddRef(void* self) {
        return ++static_cast<CountingComObject*>(self)->refCount;
    }
    static ULONG STDMETHODCALLTYPE Release(void* self) {
        auto* obj = static_cast<CountingComObject*>(self);
        obj->releaseCalls.fetch_add(1);
        return --obj->refCount;
    }
    static HRESULT STDMETHODCALLTYPE QueryInterface(void*, REFIID, void** out) {
        if (out) *out = nullptr;
        return E_NOINTERFACE;
    }
    static void* const* Vtable() {
        static void* const slots[] = {
            reinterpret_cast<void*>(&QueryInterface),
            reinterpret_cast<void*>(&AddRef),
            reinterpret_cast<void*>(&Release),
        };
        return slots;
    }
    CountingComObject() : vtable(Vtable()) {}
};

ULONG ComRefCount(IUnknown* obj) {
    obj->AddRef();
    return obj->Release();
}

}

TEST_F(DllLifecycleTest, ProcessTerminationSkipsLoggerMutex) {
    std::promise<void> logEntered;
    std::promise<void> releaseLog;
    std::shared_future<void> releaseFuture = releaseLog.get_future().share();
    s_logEnteredPromise = &logEntered;
    s_logReleaseFuture = &releaseFuture;

    FunctionDetour odsDetour;
    s_outputDebugDetour = &odsDetour;
    ASSERT_TRUE(odsDetour.Install(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "OutputDebugStringA")),
        reinterpret_cast<void*>(&PausingOutputDebugStringA)));

    std::thread holder([&]() {
        t_pauseInsideOutputDebugString = true;
        LogInfo("holding logger mutex during termination test");
    });
    ASSERT_EQ(logEntered.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);

    std::atomic<bool> completed{false};
    std::thread terminator([&]() {
        OnProcessDetach(reinterpret_cast<LPVOID>(1));
        LogInfo("log during termination");
        Logger::Instance().Flush();
        Logger::Instance().Close();
        completed.store(true);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!completed.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool didNotBlock = completed.load();

    releaseLog.set_value();
    holder.join();
    terminator.join();

    odsDetour.Uninstall();
    s_outputDebugDetour = nullptr;
    s_logEnteredPromise = nullptr;
    s_logReleaseFuture = nullptr;

    EXPECT_TRUE(didNotBlock);
}

TEST_F(DllLifecycleTest, SystemDxgiDestructorSkipsFreeLibraryDuringTermination) {
    auto systemDxgi = std::make_unique<SystemDxgi>();
    ASSERT_TRUE(systemDxgi->LoadSystemDxgi());
    HMODULE sysDxgiMod = GetModuleHandleW(SystemDxgiPath().c_str());
    ASSERT_NE(sysDxgiMod, nullptr);

    FunctionDetour freeDetour;
    s_freeLibraryDetour = &freeDetour;
    s_watchedFreeModule = sysDxgiMod;
    s_watchedFreeCalls.store(0);
    ASSERT_TRUE(freeDetour.Install(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "FreeLibrary")),
        reinterpret_cast<void*>(&CountingFreeLibrary)));

    OnProcessDetach(reinterpret_cast<LPVOID>(1));
    systemDxgi.reset();

    freeDetour.Uninstall();
    s_freeLibraryDetour = nullptr;
    s_watchedFreeModule = nullptr;

    EXPECT_EQ(s_watchedFreeCalls.load(), 0);
}

TEST_F(DllLifecycleTest, DxgiProxyAndOpenXrManagerSkipComReleaseDuringTermination) {
    CountingComObject fakeDevice;
    CountingComObject fakeQueue;

    {
        auto proxy = std::make_unique<DxgiProxy>(
            std::make_shared<ImmediateSystemDxgi>(), nullptr, nullptr, nullptr,
            std::make_shared<MirrorWindowController>(), nullptr);
        proxy->SetD3D12DeviceAndQueue(
            reinterpret_cast<ID3D12Device*>(&fakeDevice),
            reinterpret_cast<ID3D12CommandQueue*>(&fakeQueue));
        ASSERT_EQ(fakeDevice.refCount.load(), 2u);
        ASSERT_EQ(fakeQueue.refCount.load(), 2u);

        OnProcessDetach(reinterpret_cast<LPVOID>(1));
        proxy.reset();
    }

    EXPECT_EQ(fakeDevice.releaseCalls.load(), 0);
    EXPECT_EQ(fakeQueue.releaseCalls.load(), 0);
    ResetProcessTerminatingForTesting();

    Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
    Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory))) &&
        SUCCEEDED(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) &&
        SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        ASSERT_TRUE(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Width = 64;
        resDesc.Height = 64;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        resDesc.SampleDesc.Count = 1;
        resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
        ASSERT_TRUE(SUCCEEDED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_PRESENT, nullptr, IID_PPV_ARGS(&backBuffer))));

        auto openXrManager = std::make_unique<OpenXrManager>(std::make_shared<FailingOpenXrDispatch>());
        openXrManager->SetD3D12DeviceAndQueue(device.Get(), queue.Get());
        openXrManager->ClearBackBufferToBlack(backBuffer.Get());

        const ULONG devRefBefore = ComRefCount(device.Get());
        const ULONG queueRefBefore = ComRefCount(queue.Get());

        OnProcessDetach(reinterpret_cast<LPVOID>(1));
        openXrManager.reset();

        EXPECT_EQ(ComRefCount(device.Get()), devRefBefore);
        EXPECT_EQ(ComRefCount(queue.Get()), queueRefBefore);
    }
}

}
}
