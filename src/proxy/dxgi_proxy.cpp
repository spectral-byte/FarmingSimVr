#include "dxgi_proxy.h"
#include "core/logger.h"
#include <utility>

namespace fs25vr {

namespace {

static DxgiProxy* s_activeProxy = nullptr;

template <typename PFN, typename... Args>
HRESULT ForwardFactoryCall(const char* funcName, PFN pfn, Args&&... args) {
    if (!pfn) {
        LogError(std::string("System ") + funcName + " pointer is null.");
        return E_FAIL;
    }
    return pfn(std::forward<Args>(args)...);
}

} // anonymous namespace

DxgiProxy& DxgiProxy::Instance() {
    static DxgiProxy instance(
        std::shared_ptr<ISystemDxgi>(&SystemDxgi::Instance(), [](auto*) {}),
        std::shared_ptr<OpenXrManager>(&OpenXrManager::Instance(), [](auto*) {}),
        std::shared_ptr<LuauInterceptor>(&LuauInterceptor::Instance(), [](auto*) {}),
        std::shared_ptr<GraphicsTelemetryProbe>(&GraphicsTelemetryProbe::Instance(), [](auto*) {}),
        std::shared_ptr<D3D12Interceptor>(&D3D12Interceptor::Instance(), [](auto*) {}),
        std::shared_ptr<VRBridge>(&VRBridge::Instance(), [](auto*) {})
    );
    return instance;
}

DxgiProxy::DxgiProxy(std::shared_ptr<ISystemDxgi> systemDxgi,
                     std::shared_ptr<OpenXrManager> openXrManager,
                     std::shared_ptr<LuauInterceptor> luauInterceptor,
                     std::shared_ptr<GraphicsTelemetryProbe> telemetryProbe,
                     std::shared_ptr<D3D12Interceptor> d3d12Interceptor,
                     std::shared_ptr<VRBridge> vrBridge)
    : m_systemDxgi(systemDxgi ? std::move(systemDxgi) : std::shared_ptr<ISystemDxgi>(&SystemDxgi::Instance(), [](auto*) {})),
      m_openXrManager(openXrManager ? std::move(openXrManager) : std::shared_ptr<OpenXrManager>(&OpenXrManager::Instance(), [](auto*) {})),
      m_luauInterceptor(luauInterceptor ? std::move(luauInterceptor) : std::shared_ptr<LuauInterceptor>(&LuauInterceptor::Instance(), [](auto*) {})),
      m_telemetryProbe(std::move(telemetryProbe)),
      m_d3d12Interceptor(d3d12Interceptor ? std::move(d3d12Interceptor) : std::shared_ptr<D3D12Interceptor>(&D3D12Interceptor::Instance(), [](auto*) {})),
      m_vrBridge(vrBridge ? std::move(vrBridge) : std::shared_ptr<VRBridge>(&VRBridge::Instance(), [](auto*) {})) {
    s_activeProxy = this;
}

DxgiProxy::~DxgiProxy() {
    DetachAll();
    if (s_activeProxy == this) {
        s_activeProxy = nullptr;
    }
}

void DxgiProxy::EnsureVrInitialized() {
    if (!m_vrInitialized) {
        m_vrInitialized = true;
        LogInfo("Intercepted DXGI startup sequence. Initializing OpenXR subsystem...");
        m_openXrManager->Initialize();
        if (m_openXrManager->IsPassThrough()) {
            LogInfo("Injector operating in Pass-Through Mode (forwarding all calls silently).");
        } else if (m_openXrManager->IsActive()) {
            LogInfo("Injector operating in Active VR Mode.");
            m_openXrManager->CreateSession();
            m_openXrManager->CreateTrackingSpaces();
        }
        if (m_luauInterceptor) {
            m_luauInterceptor->AttachToEngine();
        }
        LogInfo("In-process Luau state interception channel armed.");
        if (m_d3d12Interceptor) {
            m_d3d12Interceptor->SetTelemetryProbe(m_telemetryProbe);
            if (m_telemetryProbe) {
                m_telemetryProbe->SetCompletionCallback([this]() {
                    if (m_d3d12Interceptor) {
                        m_d3d12Interceptor->DetachAll();
                    }
                });
            }
        }
        LogInfo("Direct3D 12 stereo telemetry probe armed.");
    }
}

void DxgiProxy::OnPrePresent(IDXGISwapChain* /*swapChain*/) {
    if (m_telemetryProbe) {
        m_telemetryProbe->OnFrameBoundary();
    }
}

void DxgiProxy::SetD3D12DeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue) {
    m_d3d12Device = device;
    m_d3d12Queue = queue;
    if (m_openXrManager && device && queue) {
        m_openXrManager->SetD3D12DeviceAndQueue(device, queue);
    }
    if (m_d3d12Interceptor && queue) {
        m_d3d12Interceptor->AttachCommandQueue(queue);
    }
}

bool DxgiProxy::ExtractD3D12DeviceAndQueue(
    IUnknown* source,
    Microsoft::WRL::ComPtr<ID3D12Device>& outDevice,
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>& outQueue) {
    if (!source) {
        return false;
    }

    // 1. Check if source is directly an ID3D12CommandQueue
    if (!outQueue) {
        source->QueryInterface(IID_PPV_ARGS(&outQueue));
    }

    // 2. Check if source is an IDXGISwapChain
    if (!outQueue || !outDevice) {
        Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
        if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&swapChain)))) {
            if (!outQueue) {
                swapChain->GetDevice(IID_PPV_ARGS(&outQueue));
            }
            if (!outDevice) {
                swapChain->GetDevice(IID_PPV_ARGS(&outDevice));
            }
        }
    }

    // 3. Check if source is directly an ID3D12Device
    if (!outDevice) {
        source->QueryInterface(IID_PPV_ARGS(&outDevice));
    }

    // 4. If queue is present but device is still null, query device from queue
    if (outQueue && !outDevice) {
        outQueue->GetDevice(IID_PPV_ARGS(&outDevice));
    }

    return (outDevice != nullptr || outQueue != nullptr);
}

bool DxgiProxy::ResolveD3D12DeviceAndQueue(IUnknown* source) {
    if (!source) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID3D12Device> device = m_d3d12Device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue = m_d3d12Queue;

    if (ExtractD3D12DeviceAndQueue(source, device, queue)) {
        SetD3D12DeviceAndQueue(device.Get(), queue.Get());
        return true;
    }
    return false;
}

bool DxgiProxy::AttachSwapChain(IDXGISwapChain* swapChain) {
    if (!m_openXrManager || !m_openXrManager->IsActive()) {
        return false;
    }
    if (!swapChain) {
        LogError("DxgiProxy::AttachSwapChain: Swap chain pointer is null.");
        return false;
    }
    if (m_swapChainAttached) return true;

    s_activeProxy = this;
    void** vtable = *reinterpret_cast<void***>(swapChain);
    if (!vtable) {
        LogError("DxgiProxy::AttachSwapChain: VTable pointer is null.");
        return false;
    }

    void* fnPresent = vtable[kPresentVTableSlot];
    bool presentAttached = m_detourPresent.Install(
        fnPresent, reinterpret_cast<void*>(&DxgiProxy::Intercept_Present));

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain1)))) {
        void** vtable1 = *reinterpret_cast<void***>(swapChain1.Get());
        if (vtable1) {
            void* fnPresent1 = vtable1[kPresent1VTableSlot];
            m_detourPresent1.Install(
                fnPresent1, reinterpret_cast<void*>(&DxgiProxy::Intercept_Present1));
        }
    }

    m_swapChainAttached = presentAttached;
    if (!m_swapChainAttached) {
        LogError("DxgiProxy::AttachSwapChain: Failed to install swap chain detours.");
    } else {
        LogInfo("DxgiProxy: Successfully attached swap chain presentation detours.");
    }
    return m_swapChainAttached;
}

bool DxgiProxy::AttachFactory(IDXGIFactory* factory) {
    if (!m_openXrManager || !m_openXrManager->IsActive()) {
        return false;
    }
    if (!factory) {
        LogError("DxgiProxy::AttachFactory: Factory pointer is null.");
        return false;
    }
    if (m_factoryAttached) return true;

    s_activeProxy = this;
    void** vtable = *reinterpret_cast<void***>(factory);
    if (!vtable) {
        LogError("DxgiProxy::AttachFactory: VTable pointer is null.");
        return false;
    }

    void* fnCreateSwapChain = vtable[kCreateSwapChainVTableSlot];
    bool createAttached = m_detourCreateSwapChain.Install(
        fnCreateSwapChain, reinterpret_cast<void*>(&DxgiProxy::Intercept_CreateSwapChain));

    Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory2)))) {
        void** vtable2 = *reinterpret_cast<void***>(factory2.Get());
        if (vtable2) {
            void* fnCreateForHwnd = vtable2[kCreateSwapChainForHwndVTableSlot];
            m_detourCreateSwapChainForHwnd.Install(
                fnCreateForHwnd, reinterpret_cast<void*>(&DxgiProxy::Intercept_CreateSwapChainForHwnd));
        }
    }

    m_factoryAttached = createAttached;
    if (!m_factoryAttached) {
        LogError("DxgiProxy::AttachFactory: Failed to install factory detours.");
    } else {
        LogInfo("DxgiProxy: Successfully attached factory swap chain creation detours.");
    }
    return m_factoryAttached;
}

void DxgiProxy::DetachAll() {
    if (m_detourPresent.IsInstalled()) m_detourPresent.Uninstall();
    if (m_detourPresent1.IsInstalled()) m_detourPresent1.Uninstall();
    if (m_detourCreateSwapChain.IsInstalled()) m_detourCreateSwapChain.Uninstall();
    if (m_detourCreateSwapChainForHwnd.IsInstalled()) m_detourCreateSwapChainForHwnd.Uninstall();
    m_swapChainAttached = false;
    m_factoryAttached = false;
}

void DxgiProxy::OnPresent(IDXGISwapChain* swapChain, UINT /*syncInterval*/, UINT /*flags*/) {
    OnPrePresent(swapChain);

    if (!m_openXrManager || !m_openXrManager->IsActive()) {
        return;
    }

    ID3D12Resource* pBackBuffer = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;

    if (swapChain) {
        // Ensure D3D12 device and command queue via consolidated helper
        if (!m_d3d12Queue || !m_d3d12Device) {
            ResolveD3D12DeviceAndQueue(swapChain);
        }

        // Ensure OpenXR eye swapchains exist matching the game backbuffer dimensions
        if (!m_openXrManager->HasEyeSwapchains()) {
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            uint32_t bufferWidth = 0;
            uint32_t bufferHeight = 0;
            DXGI_SWAP_CHAIN_DESC desc{};
            if (SUCCEEDED(swapChain->GetDesc(&desc))) {
                if (desc.BufferDesc.Format != DXGI_FORMAT_UNKNOWN) {
                    format = desc.BufferDesc.Format;
                }
                bufferWidth = desc.BufferDesc.Width;
                bufferHeight = desc.BufferDesc.Height;
            }

            // Always verify against actual backbuffer resource description if available
            Microsoft::WRL::ComPtr<ID3D12Resource> testBuffer;
            if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&testBuffer))) && testBuffer) {
                D3D12_RESOURCE_DESC resDesc = testBuffer->GetDesc();
                if (resDesc.Format != DXGI_FORMAT_UNKNOWN) {
                    format = resDesc.Format;
                }
                if (resDesc.Width > 0 && resDesc.Height > 0) {
                    bufferWidth = static_cast<uint32_t>(resDesc.Width);
                    bufferHeight = resDesc.Height;
                }
            }

            // Create eye swapchains matching the backbuffer resolution for direct zero-overhead D3D12 copy.
            // If dimensions are unavailable (0), CreateEyeSwapchains will query runtime recommended views as fallback.
            m_openXrManager->CreateEyeSwapchains(bufferWidth, bufferHeight, format);
        }

        UINT bufferIndex = 0;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
        if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
            bufferIndex = swapChain3->GetCurrentBackBufferIndex();
        }

        if (SUCCEEDED(swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBuffer)))) {
            pBackBuffer = backBuffer.Get();
        }
    }

    if (m_openXrManager->HasEyeSwapchains()) {
        bool isCompanionActive = m_vrBridge && m_vrBridge->IsCompanionActive();
        bool isMenuVisible = m_vrBridge && m_vrBridge->IsMenuVisible();
        if (!isCompanionActive || isMenuVisible) {
            // Floating quad layer mode:
            // Backbuffer is presented on an OpenXR Quad layer positioned in 3D tracking space
            // rather than warping across an immersive projection layer.
            // Active during pre-game / Main Menu AND in-game GUI menus (ESC pause menu, shop, etc.).
            m_openXrManager->CopyBackBufferToEye(0, pBackBuffer);
            m_openXrManager->SubmitMenuQuad();
        } else {
            bool isNativeStereo = m_telemetryProbe && m_telemetryProbe->HasNativeStereoDetected();
            if (isNativeStereo) {
                // In native stereo mode, both eye views are rendered simultaneously in the current backbuffer pass.
                // Submit both eyes directly to the OpenXR compositor without skipping or suppressing frames.
                // If mid-pair transition occurred and eye 0 was already copied in the active frame, avoid redundant acquisition.
                if (!m_openXrManager->GetEyeSwapchain(0).hasCopiedFrame) {
                    m_openXrManager->CopyBackBufferToEye(0, pBackBuffer);
                }
                m_openXrManager->CopyBackBufferToEye(1, pBackBuffer);
                m_openXrManager->SubmitFramePair();

                if (m_vrBridge) {
                    m_vrBridge->AdvanceFrame();
                }
            } else {
                // Alternate Frame Rendering (AFR) pacing pipeline: alternate sequential frames
                uint32_t parity = m_vrBridge ? m_vrBridge->GetFrameParity() : 0;
                m_openXrManager->CopyBackBufferToEye(parity, pBackBuffer);

                if (parity == 1) {
                    m_openXrManager->SubmitFramePair();
                }

                if (m_vrBridge) {
                    m_vrBridge->AdvanceFrame();
                }
            }
        }
    }
}

void DxgiProxy::OnPresent1(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* /*params*/) {
    OnPresent(swapChain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_Present(IDXGISwapChain* self, UINT syncInterval, UINT flags) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    proxy->OnPresent(self, syncInterval, flags);
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT)>(
        proxy->m_detourPresent.GetOriginal()
    );
    if (orig) {
        UINT effectiveSyncInterval = (proxy->m_openXrManager && proxy->m_openXrManager->IsActive()) ? 0 : syncInterval;
        return orig(self, effectiveSyncInterval, flags);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_Present1(IDXGISwapChain1* self, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* params) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    proxy->OnPresent1(self, syncInterval, flags, params);
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*)>(
        proxy->m_detourPresent1.GetOriginal()
    );
    if (orig) {
        UINT effectiveSyncInterval = (proxy->m_openXrManager && proxy->m_openXrManager->IsActive()) ? 0 : syncInterval;
        return orig(self, effectiveSyncInterval, flags, params);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_CreateSwapChain(
    IDXGIFactory* self, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    if (pDevice) {
        proxy->ResolveD3D12DeviceAndQueue(pDevice);
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**)>(
        proxy->m_detourCreateSwapChain.GetOriginal()
    );
    HRESULT hr = orig ? orig(self, pDevice, pDesc, ppSwapChain) : E_FAIL;
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        proxy->AttachSwapChain(*ppSwapChain);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_CreateSwapChainForHwnd(
    IDXGIFactory2* self, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    if (pDevice) {
        proxy->ResolveD3D12DeviceAndQueue(pDevice);
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**)>(
        proxy->m_detourCreateSwapChainForHwnd.GetOriginal()
    );
    HRESULT hr = orig ? orig(self, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain) : E_FAIL;
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        proxy->AttachSwapChain(*ppSwapChain);
    }
    return hr;
}

HRESULT DxgiProxy::CreateDXGIFactory(REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    HRESULT hr = ForwardFactoryCall("CreateDXGIFactory", m_systemDxgi->GetCreateDXGIFactory(), riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory && m_openXrManager && m_openXrManager->IsActive()) {
        Microsoft::WRL::ComPtr<IDXGIFactory> factory;
        if (SUCCEEDED(reinterpret_cast<IUnknown*>(*ppFactory)->QueryInterface(IID_PPV_ARGS(&factory)))) {
            AttachFactory(factory.Get());
        }
    }
    return hr;
}

HRESULT DxgiProxy::CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    HRESULT hr = ForwardFactoryCall("CreateDXGIFactory1", m_systemDxgi->GetCreateDXGIFactory1(), riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory && m_openXrManager && m_openXrManager->IsActive()) {
        Microsoft::WRL::ComPtr<IDXGIFactory> factory;
        if (SUCCEEDED(reinterpret_cast<IUnknown*>(*ppFactory)->QueryInterface(IID_PPV_ARGS(&factory)))) {
            AttachFactory(factory.Get());
        }
    }
    return hr;
}

HRESULT DxgiProxy::CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    HRESULT hr = ForwardFactoryCall("CreateDXGIFactory2", m_systemDxgi->GetCreateDXGIFactory2(), Flags, riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory && m_openXrManager && m_openXrManager->IsActive()) {
        Microsoft::WRL::ComPtr<IDXGIFactory> factory;
        if (SUCCEEDED(reinterpret_cast<IUnknown*>(*ppFactory)->QueryInterface(IID_PPV_ARGS(&factory)))) {
            AttachFactory(factory.Get());
        }
    }
    return hr;
}

HRESULT DxgiProxy::DXGIDeclareAdapterRemovalSupport() {
    auto pfn = m_systemDxgi->GetDXGIDeclareAdapterRemovalSupport();
    return pfn ? pfn() : E_FAIL;
}

HRESULT DxgiProxy::DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    auto pfn = m_systemDxgi->GetDXGIGetDebugInterface1();
    return pfn ? pfn(Flags, riid, pDebug) : E_FAIL;
}

} // namespace fs25vr

extern "C" {

HRESULT WINAPI Proxy_CreateDXGIFactory(REFIID riid, void** ppFactory) {
    return fs25vr::DxgiProxy::Instance().CreateDXGIFactory(riid, ppFactory);
}

HRESULT WINAPI Proxy_CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    return fs25vr::DxgiProxy::Instance().CreateDXGIFactory1(riid, ppFactory);
}

HRESULT WINAPI Proxy_CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    return fs25vr::DxgiProxy::Instance().CreateDXGIFactory2(Flags, riid, ppFactory);
}

HRESULT WINAPI Proxy_DXGIDeclareAdapterRemovalSupport() {
    return fs25vr::DxgiProxy::Instance().DXGIDeclareAdapterRemovalSupport();
}

HRESULT WINAPI Proxy_DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    return fs25vr::DxgiProxy::Instance().DXGIGetDebugInterface1(Flags, riid, pDebug);
}

HRESULT WINAPI Proxy_DXGID3D10CreateDevice(HMODULE hModule, IDXGIAdapter* pAdapter, UINT Flags, DWORD RuntimeVersion, void* pOutputs) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(HMODULE, IDXGIAdapter*, UINT, DWORD, void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGID3D10CreateDevice"));
    return pfn ? pfn(hModule, pAdapter, Flags, RuntimeVersion, pOutputs) : E_FAIL;
}

HRESULT WINAPI Proxy_DXGID3D10CreateLayeredDevice(void* a, void* b, void* c, void* d, void* e) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(void*, void*, void*, void*, void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGID3D10CreateLayeredDevice"));
    return pfn ? pfn(a, b, c, d, e) : E_FAIL;
}

SIZE_T WINAPI Proxy_DXGID3D10GetLayeredDeviceSize(const void* pConfiguration, UINT Size) {
    auto pfn = reinterpret_cast<SIZE_T(WINAPI*)(const void*, UINT)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGID3D10GetLayeredDeviceSize"));
    return pfn ? pfn(pConfiguration, Size) : 0;
}

HRESULT WINAPI Proxy_DXGID3D10RegisterLayers(const void* pLayers, UINT NumLayers) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(const void*, UINT)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGID3D10RegisterLayers"));
    return pfn ? pfn(pLayers, NumLayers) : E_FAIL;
}

HRESULT WINAPI Proxy_DXGIDisableVBlankVirtualization() {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)()>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGIDisableVBlankVirtualization"));
    return pfn ? pfn() : E_FAIL;
}

HRESULT WINAPI Proxy_DXGIReportAdapterConfiguration(void* pConf) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGIReportAdapterConfiguration"));
    return pfn ? pfn(pConf) : E_FAIL;
}

void WINAPI Proxy_DXGIDumpJournal(void* pJournal) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGIDumpJournal"));
    if (pfn) pfn(pJournal);
}

void WINAPI Proxy_PIXBeginCapture(DWORD captureFlags, const void* captureArgs) {
    auto pfn = reinterpret_cast<void(WINAPI*)(DWORD, const void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("PIXBeginCapture"));
    if (pfn) pfn(captureFlags, captureArgs);
}

void WINAPI Proxy_PIXEndCapture() {
    auto pfn = reinterpret_cast<void(WINAPI*)()>(
        fs25vr::SystemDxgi::Instance().GetExport("PIXEndCapture"));
    if (pfn) pfn();
}

DWORD WINAPI Proxy_PIXGetCaptureState() {
    auto pfn = reinterpret_cast<DWORD(WINAPI*)()>(
        fs25vr::SystemDxgi::Instance().GetExport("PIXGetCaptureState"));
    return pfn ? pfn() : 0;
}

HRESULT WINAPI Proxy_SetAppCompatStringPointer(DWORD unknown, const char* str) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(DWORD, const char*)>(
        fs25vr::SystemDxgi::Instance().GetExport("SetAppCompatStringPointer"));
    return pfn ? pfn(unknown, str) : E_FAIL;
}

void WINAPI Proxy_UpdateHMDEmulationStatus(BOOL status) {
    auto pfn = reinterpret_cast<void(WINAPI*)(BOOL)>(
        fs25vr::SystemDxgi::Instance().GetExport("UpdateHMDEmulationStatus"));
    if (pfn) pfn(status);
}

void WINAPI Proxy_ApplyCompatResolutionQuirking(void* a, void* b, void* c, void* d) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*, void*, void*, void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("ApplyCompatResolutionQuirking"));
    if (pfn) pfn(a, b, c, d);
}

void WINAPI Proxy_CompatString(void* a, void* b, void* c, void* d) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*, void*, void*, void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("CompatString"));
    if (pfn) pfn(a, b, c, d);
}

void WINAPI Proxy_CompatValue(void* a, void* b, void* c, void* d) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*, void*, void*, void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("CompatValue"));
    if (pfn) pfn(a, b, c, d);
}

} // extern "C"
