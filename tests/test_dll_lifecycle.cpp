#include <gtest/gtest.h>
#include "core/detour.h"
#include "core/dll_lifecycle.h"
#include "core/injector_runtime.h"
#include "ngx/ngx_interceptor.h"
#include "openxr/openxr_manager.h"
#include "proxy/dxgi_proxy.h"
#include "proxy/system_dxgi.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
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

// Returns from factory calls immediately, so a racing caller is not hidden behind real DXGI latency.
class ImmediateSystemDxgi : public ISystemDxgi {
public:
    bool LoadSystemDxgi() override { return true; }
    FARPROC GetExport(const char*) override { return nullptr; }
    PFN_CreateDXGIFactory GetCreateDXGIFactory() override { return &ImmediateCreateFactory; }
    PFN_CreateDXGIFactory1 GetCreateDXGIFactory1() override { return &ImmediateCreateFactory; }
    PFN_CreateDXGIFactory2 GetCreateDXGIFactory2() override { return nullptr; }
    PFN_DXGIDeclareAdapterRemovalSupport GetDXGIDeclareAdapterRemovalSupport() override { return nullptr; }
    PFN_DXGIGetDebugInterface1 GetDXGIGetDebugInterface1() override { return nullptr; }
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

// Holds OpenXR initialization at its first runtime call until released, so a test can keep the
// first factory call inside EnsureVrInitialized while a second one races it.
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

    // The proxy installs the NGX hooks on the interceptor it is given; these tests observe m_ngx.
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

    // Use the test exe's own module as a stand-in for the Injector DLL handle.
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
    OnProcessDetach(nullptr);  // FreeLibrary: cleanup allowed
    EXPECT_FALSE(IsProcessTerminating());
    EXPECT_FALSE(m_ngx->AreModuleHooksInstalled());
}

TEST_F(DllLifecycleTest, ProcessDetachUnwindsTheHookOwnerNotTheNewestRuntime) {
    ASSERT_TRUE(m_ngx->InstallModuleLoadHooks());

    // A newer runtime with an interceptor of its own must not hide the hooks m_ngx owns.
    InjectorRuntime::Dependencies deps;
    deps.openXrManager = std::make_shared<OpenXrManager>(std::make_shared<FailingOpenXrDispatch>());
    InjectorRuntime newer(std::move(deps));
    ASSERT_NE(&newer.GetNgxInterceptor(), m_ngx.get());

    OnProcessDetach(nullptr);

    EXPECT_FALSE(m_ngx->AreModuleHooksInstalled());
}

TEST_F(DllLifecycleTest, DestructorsRunAfterTerminationDetachLeaveHooksInPlace) {
    ASSERT_EQ(TerminationTarget(5), 6);

    void* patched = nullptr;  // past any ILT thunk
    {
        FunctionDetour detour;
        ASSERT_TRUE(detour.Install(reinterpret_cast<void*>(&TerminationTarget), reinterpret_cast<void*>(&TerminationDetour)));
        patched = detour.GetTarget();
        OnProcessDetach(reinterpret_cast<LPVOID>(1));
    }  // ~FunctionDetour runs here, as the CRT's static destructors do after DllMain returns.

    EXPECT_EQ(TerminationTarget(5), -5);

    // The destructor deliberately left the hook. Remove it through MinHook so its
    // hook table forgets the target too, or a later Install (e.g. --gtest_repeat) fails.
    ASSERT_EQ(MH_RemoveHook(patched), MH_OK);
    EXPECT_EQ(TerminationTarget(5), 6);
}

TEST_F(DllLifecycleTest, ConcurrentFirstFactoryCallsReturnOnlyAfterHooksInstalled) {
    // The race is timing-dependent; several fresh rounds make a regression show up reliably.
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
                // On return the game may load nvngx immediately; the hooks must already exist.
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
    dispatch->entered.get_future().wait();  // the first caller is inside OpenXR initialization

    std::atomic<bool> secondReturned{false};
    std::thread second([&]() {
        void* factory = nullptr;
        proxy.CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
        secondReturned = true;
    });

    // The second caller decides from the OpenXR state whether to hook its factory, so it must not
    // return while that state is still being established.
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

} // namespace
} // namespace fs25vr
