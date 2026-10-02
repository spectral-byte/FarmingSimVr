#include "dxgi_proxy.h"
#include "core/logger.h"
#include <utility>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "comctl32.lib")

namespace fs25vr {

namespace {

static DxgiProxy* s_activeProxy = nullptr;

thread_local bool t_isInternalWindowQuery = false;

struct InternalWindowQueryScope {
    InternalWindowQueryScope() { t_isInternalWindowQuery = true; }
    ~InternalWindowQueryScope() { t_isInternalWindowQuery = false; }
};

template <typename PFN, typename... Args>
HRESULT ForwardFactoryCall(const char* funcName, PFN pfn, Args&&... args) {
    if (!pfn) {
        LogError(std::string("System ") + funcName + " pointer is null.");
        return E_FAIL;
    }
    return pfn(std::forward<Args>(args)...);
}

HWND GetRootWindow(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return nullptr;
    HWND root = GetAncestor(hWnd, GA_ROOT);
    return root ? root : hWnd;
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
    if (!m_gameHwnd) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow) {
            SetGameHwnd(desc.OutputWindow);
        }
    } else if (m_gameHwnd && !m_windowSubclassed && m_openXrManager && m_openXrManager->IsActive()) {
        AttachWindowSubclass(m_gameHwnd);
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

    void* fnResizeBuffers = vtable[kResizeBuffersVTableSlot];
    m_detourResizeBuffers.Install(
        fnResizeBuffers, reinterpret_cast<void*>(&DxgiProxy::Intercept_ResizeBuffers));

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain1)))) {
        void** vtable1 = *reinterpret_cast<void***>(swapChain1.Get());
        if (vtable1) {
            void* fnPresent1 = vtable1[kPresent1VTableSlot];
            m_detourPresent1.Install(
                fnPresent1, reinterpret_cast<void*>(&DxgiProxy::Intercept_Present1));
        }
    }

    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
    if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
        void** vtable3 = *reinterpret_cast<void***>(swapChain3.Get());
        if (vtable3) {
            void* fnResizeBuffers1 = vtable3[kResizeBuffers1VTableSlot];
            m_detourResizeBuffers1.Install(
                fnResizeBuffers1, reinterpret_cast<void*>(&DxgiProxy::Intercept_ResizeBuffers1));
        }
    }

    if (m_targetRenderWidth == 0 || m_targetRenderHeight == 0) {
        DXGI_SWAP_CHAIN_DESC scDesc{};
        if (SUCCEEDED(swapChain->GetDesc(&scDesc))) {
            if (scDesc.BufferDesc.Width > 0 && scDesc.BufferDesc.Height > 0) {
                m_targetRenderWidth = scDesc.BufferDesc.Width;
                m_targetRenderHeight = scDesc.BufferDesc.Height;
            }
        }
    }
    if (m_targetRenderWidth == 0 || m_targetRenderHeight == 0) {
        Microsoft::WRL::ComPtr<ID3D12Resource> testBuffer;
        if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&testBuffer))) && testBuffer) {
            D3D12_RESOURCE_DESC resDesc = testBuffer->GetDesc();
            if (resDesc.Width > 0 && resDesc.Height > 0) {
                m_targetRenderWidth = static_cast<uint32_t>(resDesc.Width);
                m_targetRenderHeight = resDesc.Height;
            }
        }
    }

    m_swapChainAttached = presentAttached;
    if (!m_swapChainAttached) {
        LogError("DxgiProxy::AttachSwapChain: Failed to install swap chain detours.");
    } else {
        LogInfo("DxgiProxy: Successfully attached swap chain presentation and buffer detours (Target: " +
                std::to_string(m_targetRenderWidth) + "x" + std::to_string(m_targetRenderHeight) + ").");
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
    DetachWindowSubclass();
    if (m_detourPresent.IsInstalled()) m_detourPresent.Uninstall();
    if (m_detourPresent1.IsInstalled()) m_detourPresent1.Uninstall();
    if (m_detourResizeBuffers.IsInstalled()) m_detourResizeBuffers.Uninstall();
    if (m_detourResizeBuffers1.IsInstalled()) m_detourResizeBuffers1.Uninstall();
    if (m_detourCreateSwapChain.IsInstalled()) m_detourCreateSwapChain.Uninstall();
    if (m_detourCreateSwapChainForHwnd.IsInstalled()) m_detourCreateSwapChainForHwnd.Uninstall();
    if (m_detourSetCursorPos.IsInstalled()) m_detourSetCursorPos.Uninstall();
    if (m_detourClipCursor.IsInstalled()) m_detourClipCursor.Uninstall();
    m_swapChainAttached = false;
    m_factoryAttached = false;
    m_inSizeMove = false;
    m_windowClampDirty = true;
    m_lastEnforcedWidth = 0;
    m_lastEnforcedHeight = 0;
    m_lastMonitor = nullptr;
    m_targetRenderWidth = 0;
    m_targetRenderHeight = 0;
    m_cachedSwapChain = nullptr;
    m_cachedSwapChain3.Reset();
}

void DxgiProxy::OnPresent(IDXGISwapChain* swapChain, UINT /*syncInterval*/, UINT /*flags*/) {
    OnPrePresent(swapChain);

    if (!m_openXrManager || !m_openXrManager->IsActive()) {
        return;
    }

    if (!m_gameHwnd && swapChain) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow) {
            SetGameHwnd(desc.OutputWindow);
        }
    }
    if (m_gameHwnd && m_windowClampDirty) {
        EnforceClampedMirrorWindowDimensions(m_gameHwnd);
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
        if (!m_cachedSwapChain3 || m_cachedSwapChain != swapChain) {
            m_cachedSwapChain = swapChain;
            m_cachedSwapChain3.Reset();
            swapChain->QueryInterface(IID_PPV_ARGS(&m_cachedSwapChain3));
        }
        if (m_cachedSwapChain3) {
            bufferIndex = m_cachedSwapChain3->GetCurrentBackBufferIndex();
        }

        if (SUCCEEDED(swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBuffer)))) {
            pBackBuffer = backBuffer.Get();
        }
    }

    if (m_openXrManager->HasEyeSwapchains()) {
        bool isCompanionActive = m_vrBridge && m_vrBridge->IsCompanionActive();
        bool isMenuVisible = m_vrBridge && m_vrBridge->IsMenuVisible();
        bool is2DViewActive = m_vrBridge && m_vrBridge->Is2DViewActive();
        bool inQuadMode = !isCompanionActive || isMenuVisible || is2DViewActive;
        if (inQuadMode) {
            m_openXrManager->CopyBackBufferToEye(0, pBackBuffer);
            if (!m_wasInQuadMode) {
                m_openXrManager->AlignMenuQuadToHeadPose();
            }
            m_wasInQuadMode = true;

            if (!m_openXrManager->HasCursorSwapchain()) {
                m_openXrManager->CreateCursorSwapchain();
            }
            UpdateMenuCursorState(swapChain);

            // Floating quad layer mode:
            // Backbuffer is presented on an OpenXR Quad layer positioned in 3D tracking space
            // rather than warping across an immersive projection layer.
            // Active during pre-game / Main Menu, in-game GUI menus (ESC pause menu, shop, etc.),
            // or in 2D Floating Window Mode.
            m_openXrManager->SubmitMenuQuad();
        } else {
            if (m_wasInQuadMode) {
                // Seamlessly returning to 3D VR mode: reset AFR frame parity
                if (m_vrBridge) {
                    m_vrBridge->SetFrameParity(0);
                }
            }
            m_wasInQuadMode = false;
            if (m_openXrManager->GetMenuCursorState().visible) {
                m_openXrManager->SetMenuCursorState({ false, 0.0f, 0.0f, MenuCursorState::kDefaultCursorSize });
            }
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

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_ResizeBuffers(
    IDXGISwapChain* self, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    if (proxy) {
        proxy->m_cachedSwapChain = nullptr;
        proxy->m_cachedSwapChain3.Reset();
        proxy->m_windowClampDirty = true;
    }
    UINT effectiveWidth = Width;
    UINT effectiveHeight = Height;
    if (proxy && proxy->IsActive() && proxy->m_targetRenderWidth > 0 && proxy->m_targetRenderHeight > 0) {
        effectiveWidth = proxy->m_targetRenderWidth;
        effectiveHeight = proxy->m_targetRenderHeight;
        LogInfo("DxgiProxy::Intercept_ResizeBuffers: Overriding dimensions from (" +
                std::to_string(Width) + "x" + std::to_string(Height) + ") to target render resolution (" +
                std::to_string(effectiveWidth) + "x" + std::to_string(effectiveHeight) + ")");
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT)>(
        proxy ? proxy->m_detourResizeBuffers.GetOriginal() : nullptr
    );
    if (orig) {
        return orig(self, BufferCount, effectiveWidth, effectiveHeight, NewFormat, SwapChainFlags);
    }
    return E_FAIL;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_ResizeBuffers1(
    IDXGISwapChain3* self, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags,
    const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    if (proxy) {
        proxy->m_cachedSwapChain = nullptr;
        proxy->m_cachedSwapChain3.Reset();
        proxy->m_windowClampDirty = true;
    }
    UINT effectiveWidth = Width;
    UINT effectiveHeight = Height;
    if (proxy && proxy->IsActive() && proxy->m_targetRenderWidth > 0 && proxy->m_targetRenderHeight > 0) {
        effectiveWidth = proxy->m_targetRenderWidth;
        effectiveHeight = proxy->m_targetRenderHeight;
        LogInfo("DxgiProxy::Intercept_ResizeBuffers1: Overriding dimensions from (" +
                std::to_string(Width) + "x" + std::to_string(Height) + ") to target render resolution (" +
                std::to_string(effectiveWidth) + "x" + std::to_string(effectiveHeight) + ")");
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*)>(
        proxy ? proxy->m_detourResizeBuffers1.GetOriginal() : nullptr
    );
    if (orig) {
        return orig(self, BufferCount, effectiveWidth, effectiveHeight, NewFormat, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
    }
    return E_FAIL;
}

BOOL WINAPI DxgiProxy::Intercept_GetClientRect(HWND hWnd, LPRECT lpRect) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    if (t_isInternalWindowQuery) {
        auto orig = reinterpret_cast<BOOL(WINAPI*)(HWND, LPRECT)>(
            proxy ? proxy->m_detourGetClientRect.GetOriginal() : nullptr
        );
        return orig ? orig(hWnd, lpRect) : FALSE;
    }
    if (proxy && proxy->IsActive() && proxy->m_gameHwnd && lpRect) {
        if (hWnd == proxy->m_gameHwnd || GetRootWindow(hWnd) == proxy->m_gameHwnd) {
            if (proxy->m_targetRenderWidth > 0 && proxy->m_targetRenderHeight > 0) {
                lpRect->left = 0;
                lpRect->top = 0;
                lpRect->right = static_cast<LONG>(proxy->m_targetRenderWidth);
                lpRect->bottom = static_cast<LONG>(proxy->m_targetRenderHeight);
                return TRUE;
            }
        }
    }
    auto orig = reinterpret_cast<BOOL(WINAPI*)(HWND, LPRECT)>(
        proxy ? proxy->m_detourGetClientRect.GetOriginal() : nullptr
    );
    return orig ? orig(hWnd, lpRect) : FALSE;
}

BOOL WINAPI DxgiProxy::Intercept_ScreenToClient(HWND hWnd, LPPOINT lpPoint) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    auto orig = reinterpret_cast<BOOL(WINAPI*)(HWND, LPPOINT)>(
        proxy ? proxy->m_detourScreenToClient.GetOriginal() : nullptr
    );
    if (!orig) return FALSE;
    if (t_isInternalWindowQuery) {
        return orig(hWnd, lpPoint);
    }
    if (!orig(hWnd, lpPoint)) {
        return FALSE;
    }
    if (proxy && proxy->IsActive() && proxy->m_gameHwnd && lpPoint) {
        if (hWnd == proxy->m_gameHwnd || GetRootWindow(hWnd) == proxy->m_gameHwnd) {
            if (proxy->m_targetRenderWidth > 0 && proxy->m_targetRenderHeight > 0) {
                RECT physRect{};
                BOOL gotRect = FALSE;
                {
                    InternalWindowQueryScope scope;
                    auto origGCR = reinterpret_cast<BOOL(WINAPI*)(HWND, LPRECT)>(proxy->m_detourGetClientRect.GetOriginal());
                    gotRect = origGCR ? origGCR(hWnd, &physRect) : FALSE;
                }
                int physW = physRect.right - physRect.left;
                int physH = physRect.bottom - physRect.top;
                if (gotRect && physW > 0 && physH > 0) {
                    lpPoint->x = static_cast<LONG>(std::round((static_cast<double>(lpPoint->x) * proxy->m_targetRenderWidth) / physW));
                    lpPoint->y = static_cast<LONG>(std::round((static_cast<double>(lpPoint->y) * proxy->m_targetRenderHeight) / physH));
                }
            }
        }
    }
    return TRUE;
}

bool DxgiProxy::ScaleVirtualCursorToPhysical(HWND hWnd, int screenX, int screenY, int& outPhysScreenX, int& outPhysScreenY) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    if (!proxy || !proxy->IsActive() || !hWnd) return false;
    if (proxy->m_targetRenderWidth == 0 || proxy->m_targetRenderHeight == 0) return false;

    RECT physRect{};
    POINT clientOrigin{ 0, 0 };
    {
        InternalWindowQueryScope scope;
        auto origGCR = reinterpret_cast<BOOL(WINAPI*)(HWND, LPRECT)>(
            proxy->m_detourGetClientRect.GetOriginal()
        );
        if (origGCR) {
            origGCR(hWnd, &physRect);
        } else {
            GetClientRect(hWnd, &physRect);
        }
        ClientToScreen(hWnd, &clientOrigin);
    }

    int physW = physRect.right - physRect.left;
    int physH = physRect.bottom - physRect.top;
    if (physW <= 0 || physH <= 0) return false;

    int virtX = screenX - clientOrigin.x;
    int virtY = screenY - clientOrigin.y;

    if (virtX >= 0 && virtX <= static_cast<int>(proxy->m_targetRenderWidth) &&
        virtY >= 0 && virtY <= static_cast<int>(proxy->m_targetRenderHeight)) {
        int scaledPhysX = static_cast<int>(std::round(
            (static_cast<double>(virtX) * physW) / proxy->m_targetRenderWidth
        ));
        int scaledPhysY = static_cast<int>(std::round(
            (static_cast<double>(virtY) * physH) / proxy->m_targetRenderHeight
        ));
        outPhysScreenX = clientOrigin.x + scaledPhysX;
        outPhysScreenY = clientOrigin.y + scaledPhysY;
        return true;
    }

    return false;
}

bool DxgiProxy::ConstrainVirtualClipRectToPhysical(HWND hWnd, const RECT* lpRect, RECT& outPhysClipRect) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    if (!proxy || !proxy->IsActive() || !hWnd || !lpRect) return false;
    if (proxy->m_targetRenderWidth == 0 || proxy->m_targetRenderHeight == 0) return false;

    RECT physRect{};
    POINT clientOrigin{ 0, 0 };
    {
        InternalWindowQueryScope scope;
        auto origGCR = reinterpret_cast<BOOL(WINAPI*)(HWND, LPRECT)>(
            proxy->m_detourGetClientRect.GetOriginal()
        );
        if (origGCR) {
            origGCR(hWnd, &physRect);
        } else {
            GetClientRect(hWnd, &physRect);
        }
        ClientToScreen(hWnd, &clientOrigin);
    }

    int physW = physRect.right - physRect.left;
    int physH = physRect.bottom - physRect.top;
    if (physW <= 0 || physH <= 0) return false;

    outPhysClipRect.left = clientOrigin.x;
    outPhysClipRect.top = clientOrigin.y;
    outPhysClipRect.right = clientOrigin.x + physW;
    outPhysClipRect.bottom = clientOrigin.y + physH;
    return true;
}

BOOL WINAPI DxgiProxy::Intercept_SetCursorPos(int X, int Y) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    auto orig = reinterpret_cast<BOOL(WINAPI*)(int, int)>(
        proxy ? proxy->m_detourSetCursorPos.GetOriginal() : nullptr
    );
    if (!orig) return FALSE;
    if (t_isInternalWindowQuery) {
        return orig(X, Y);
    }

    if (proxy && proxy->IsActive() && proxy->m_gameHwnd) {
        int physX = X;
        int physY = Y;
        if (ScaleVirtualCursorToPhysical(proxy->m_gameHwnd, X, Y, physX, physY)) {
            return orig(physX, physY);
        }
    }

    return orig(X, Y);
}

BOOL WINAPI DxgiProxy::Intercept_ClipCursor(const RECT* lpRect) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    auto orig = reinterpret_cast<BOOL(WINAPI*)(const RECT*)>(
        proxy ? proxy->m_detourClipCursor.GetOriginal() : nullptr
    );
    if (!orig) return FALSE;
    if (t_isInternalWindowQuery) {
        return orig(lpRect);
    }
    if (!lpRect) {
        return orig(nullptr);
    }

    if (proxy && proxy->IsActive() && proxy->m_gameHwnd) {
        RECT physClip{};
        if (ConstrainVirtualClipRectToPhysical(proxy->m_gameHwnd, lpRect, physClip)) {
            return orig(&physClip);
        }
    }

    return orig(lpRect);
}

bool DxgiProxy::ScreenToNormalizedClient(HWND hWnd, POINT screenPt, float& outNormX, float& outNormY) {
    if (!hWnd) return false;
    POINT pt = screenPt;
    InternalWindowQueryScope scope;
    if (!ScreenToClient(hWnd, &pt)) return false;
    RECT clientRect{};
    if (!GetClientRect(hWnd, &clientRect)) return false;
    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;
    if (clientWidth <= 0 || clientHeight <= 0) return false;
    if (pt.x < 0 || pt.x > clientWidth || pt.y < 0 || pt.y > clientHeight) return false;
    outNormX = static_cast<float>(pt.x) / static_cast<float>(clientWidth);
    outNormY = static_cast<float>(pt.y) / static_cast<float>(clientHeight);
    return true;
}

RECT DxgiProxy::ComputeClampedMirrorWindowRect(const RECT& workArea, DWORD dwStyle, DWORD dwExStyle, bool hasMenu) {
    int workWidth = workArea.right - workArea.left;
    int workHeight = workArea.bottom - workArea.top;
    if (workWidth <= 0 || workHeight <= 0) {
        return workArea;
    }

    // Determine non-client frame margins (title bar, borders, menu) via AdjustWindowRectEx
    RECT frameRect{ 0, 0, 0, 0 };
    AdjustWindowRectEx(&frameRect, dwStyle, hasMenu ? TRUE : FALSE, dwExStyle);
    int frameWidth = frameRect.right - frameRect.left;
    int frameHeight = frameRect.bottom - frameRect.top;
    if (frameWidth < 0) frameWidth = 0;
    if (frameHeight < 0) frameHeight = 0;

    int maxClientWidth = workWidth - frameWidth;
    int maxClientHeight = workHeight - frameHeight;
    if (maxClientWidth <= 0 || maxClientHeight <= 0) {
        return workArea;
    }

    // Determine the maximum 16:9 client rectangle fitting within maxClientWidth x maxClientHeight
    constexpr int64_t kAspectWidth = 16;
    constexpr int64_t kAspectHeight = 9;

    int clientWidth = 0;
    int clientHeight = 0;
    if (static_cast<int64_t>(maxClientWidth) * kAspectHeight < static_cast<int64_t>(maxClientHeight) * kAspectWidth) {
        // Width-constrained
        clientWidth = maxClientWidth;
        clientHeight = static_cast<int>((static_cast<int64_t>(clientWidth) * kAspectHeight) / kAspectWidth);
    } else {
        // Height-constrained
        clientHeight = maxClientHeight;
        clientWidth = static_cast<int>((static_cast<int64_t>(clientHeight) * kAspectWidth) / kAspectHeight);
    }

    int outerWidth = clientWidth + frameWidth;
    int outerHeight = clientHeight + frameHeight;

    // Center within the monitor work area
    int outerX = workArea.left + (workWidth - outerWidth) / 2;
    int outerY = workArea.top + (workHeight - outerHeight) / 2;

    return RECT{ outerX, outerY, outerX + outerWidth, outerY + outerHeight };
}

RECT DxgiProxy::ComputeClampedMirrorWindowRect(HWND hWnd) {
    hWnd = GetRootWindow(hWnd);
    if (!hWnd) {
        return RECT{ 0, 0, 0, 0 };
    }

    HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (!hMon) {
        return RECT{ 0, 0, 0, 0 };
    }
    MONITORINFO mi{ sizeof(MONITORINFO) };
    if (!GetMonitorInfo(hMon, &mi)) {
        return RECT{ 0, 0, 0, 0 };
    }
    DWORD dwStyle = static_cast<DWORD>(GetWindowLongPtr(hWnd, GWL_STYLE)) & ~(WS_MAXIMIZE | WS_MINIMIZE);
    DWORD dwExStyle = static_cast<DWORD>(GetWindowLongPtr(hWnd, GWL_EXSTYLE));
    bool hasMenu = (GetMenu(hWnd) != nullptr);
    return ComputeClampedMirrorWindowRect(mi.rcWork, dwStyle, dwExStyle, hasMenu);
}

void DxgiProxy::SetGameHwnd(HWND hWnd) {
    hWnd = GetRootWindow(hWnd);
    if (!hWnd) return;

    if (m_gameHwnd != hWnd) {
        if (m_gameHwnd && m_windowSubclassed) {
            DetachWindowSubclass();
        }
        m_gameHwnd = hWnd;
    }
    if (m_gameHwnd && !m_windowSubclassed && IsActive()) {
        AttachWindowSubclass(m_gameHwnd);
    }
}

bool DxgiProxy::AttachWindowSubclass(HWND hWnd) {
    if (!IsActive()) {
        return false;
    }
    hWnd = GetRootWindow(hWnd);
    if (!hWnd) {
        return false;
    }

    if (m_windowSubclassed && m_gameHwnd == hWnd) {
        return true;
    }
    if (m_windowSubclassed && m_gameHwnd != hWnd) {
        DetachWindowSubclass();
    }
    m_gameHwnd = hWnd;
    BOOL res = SetWindowSubclass(hWnd, &DxgiProxy::MirrorWindowSubclassProc, kMirrorWindowSubclassId, reinterpret_cast<DWORD_PTR>(this));
    m_windowSubclassed = (res != FALSE);
    if (m_windowSubclassed) {
        LogInfo("DxgiProxy: Subclassed Desktop Mirror Window HWND=" + std::to_string(reinterpret_cast<uintptr_t>(hWnd)) + " for resolution decoupling.");
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32) {
            if (!m_detourGetClientRect.IsInstalled()) {
                void* pfnGetClientRect = reinterpret_cast<void*>(GetProcAddress(hUser32, "GetClientRect"));
                if (pfnGetClientRect) {
                    if (m_detourGetClientRect.Install(pfnGetClientRect, reinterpret_cast<void*>(&DxgiProxy::Intercept_GetClientRect))) {
                        LogInfo("DxgiProxy: Successfully installed GetClientRect detour for resolution decoupling.");
                    } else {
                        LogWarn("DxgiProxy: Failed to install GetClientRect detour.");
                    }
                }
            }
            if (!m_detourScreenToClient.IsInstalled()) {
                void* pfnScreenToClient = reinterpret_cast<void*>(GetProcAddress(hUser32, "ScreenToClient"));
                if (pfnScreenToClient) {
                    if (m_detourScreenToClient.Install(pfnScreenToClient, reinterpret_cast<void*>(&DxgiProxy::Intercept_ScreenToClient))) {
                        LogInfo("DxgiProxy: Successfully installed ScreenToClient detour for resolution decoupling.");
                    } else {
                        LogWarn("DxgiProxy: Failed to install ScreenToClient detour.");
                    }
                }
            }
            if (!m_detourSetCursorPos.IsInstalled()) {
                void* pfnSetCursorPos = reinterpret_cast<void*>(GetProcAddress(hUser32, "SetCursorPos"));
                if (pfnSetCursorPos) {
                    if (m_detourSetCursorPos.Install(pfnSetCursorPos, reinterpret_cast<void*>(&DxgiProxy::Intercept_SetCursorPos))) {
                        LogInfo("DxgiProxy: Successfully installed SetCursorPos detour for relative mouse centering.");
                    } else {
                        LogWarn("DxgiProxy: Failed to install SetCursorPos detour.");
                    }
                }
            }
            if (!m_detourClipCursor.IsInstalled()) {
                void* pfnClipCursor = reinterpret_cast<void*>(GetProcAddress(hUser32, "ClipCursor"));
                if (pfnClipCursor) {
                    if (m_detourClipCursor.Install(pfnClipCursor, reinterpret_cast<void*>(&DxgiProxy::Intercept_ClipCursor))) {
                        LogInfo("DxgiProxy: Successfully installed ClipCursor detour for desktop window bounds.");
                    } else {
                        LogWarn("DxgiProxy: Failed to install ClipCursor detour.");
                    }
                }
            }
        }
    } else {
        LogError("DxgiProxy: Failed to subclass Desktop Mirror Window HWND=" + std::to_string(reinterpret_cast<uintptr_t>(hWnd)));
    }
    return m_windowSubclassed;
}

bool DxgiProxy::DetachWindowSubclass() {
    if (!m_windowSubclassed) {
        return false;
    }
    bool removed = false;
    if (m_gameHwnd && IsWindow(m_gameHwnd)) {
        removed = (RemoveWindowSubclass(m_gameHwnd, &DxgiProxy::MirrorWindowSubclassProc, kMirrorWindowSubclassId) != FALSE);
        if (removed) {
            LogInfo("DxgiProxy: Removed Desktop Mirror Window subclass from HWND=" + std::to_string(reinterpret_cast<uintptr_t>(m_gameHwnd)));
        }
    }
    if (m_detourGetClientRect.IsInstalled()) {
        m_detourGetClientRect.Uninstall();
        LogInfo("DxgiProxy: Uninstalled GetClientRect detour.");
    }
    if (m_detourScreenToClient.IsInstalled()) {
        m_detourScreenToClient.Uninstall();
        LogInfo("DxgiProxy: Uninstalled ScreenToClient detour.");
    }
    if (m_detourSetCursorPos.IsInstalled()) {
        m_detourSetCursorPos.Uninstall();
        LogInfo("DxgiProxy: Uninstalled SetCursorPos detour.");
    }
    if (m_detourClipCursor.IsInstalled()) {
        m_detourClipCursor.Uninstall();
        LogInfo("DxgiProxy: Uninstalled ClipCursor detour.");
    }
    m_windowSubclassed = false;
    m_inSizeMove = false;
    m_lastEnforcedWidth = 0;
    m_lastEnforcedHeight = 0;
    m_lastMonitor = nullptr;
    return removed;
}

bool DxgiProxy::ApplyInitialWindowClamp(HWND hWnd) {
    if (!IsActive()) {
        return false;
    }
    hWnd = GetRootWindow(hWnd);
    if (!hWnd) {
        return false;
    }

    if (IsZoomed(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
    }

    RECT curRect{};
    if (!GetWindowRect(hWnd, &curRect)) return false;
    int curW = curRect.right - curRect.left;
    int curH = curRect.bottom - curRect.top;

    RECT clamped = ComputeClampedMirrorWindowRect(hWnd);
    int maxW = clamped.right - clamped.left;
    int maxH = clamped.bottom - clamped.top;
    if (maxW > 0 && maxH > 0 && (curW > maxW || curH > maxH)) {
        BOOL res = SetWindowPos(hWnd, nullptr, clamped.left, clamped.top, maxW, maxH,
                                SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (res) {
            m_lastEnforcedWidth = maxW;
            m_lastEnforcedHeight = maxH;
            m_lastMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
            LogInfo("DxgiProxy: Applied initial clamped Desktop Mirror Window dimensions [" +
                    std::to_string(maxW) + "x" + std::to_string(maxH) + " at (" +
                    std::to_string(clamped.left) + "," + std::to_string(clamped.top) + ")]");
            return true;
        }
    } else {
        m_lastEnforcedWidth = curW;
        m_lastEnforcedHeight = curH;
        m_lastMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        return true;
    }
    return false;
}

void DxgiProxy::EnforceClampedMirrorWindowDimensions(HWND hWnd) {
    if (!IsActive() || m_inSizeMove) return;
    m_windowClampDirty = false;

    hWnd = GetRootWindow(hWnd);
    if (!hWnd) return;

    if (IsZoomed(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
    }

    RECT curRect{};
    if (!GetWindowRect(hWnd, &curRect)) return;
    int curW = curRect.right - curRect.left;
    int curH = curRect.bottom - curRect.top;

    HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

    // Fast-path: if dimensions match our last enforced dimensions on the same monitor, skip
    if (hMon == m_lastMonitor && curW == m_lastEnforcedWidth && curH == m_lastEnforcedHeight) {
        return;
    }

    RECT clamped = ComputeClampedMirrorWindowRect(hWnd);
    int maxW = clamped.right - clamped.left;
    int maxH = clamped.bottom - clamped.top;

    if (maxW > 0 && maxH > 0 && (curW > maxW || curH > maxH)) {
        BOOL res = SetWindowPos(hWnd, nullptr, 0, 0, maxW, maxH,
                                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (res) {
            m_lastEnforcedWidth = maxW;
            m_lastEnforcedHeight = maxH;
            m_lastMonitor = hMon;
            LogInfo("DxgiProxy: Enforced clamped Desktop Mirror Window dimensions [" +
                    std::to_string(maxW) + "x" + std::to_string(maxH) + "]");
        }
    } else {
        m_lastEnforcedWidth = curW;
        m_lastEnforcedHeight = curH;
        m_lastMonitor = hMon;
    }
}

LRESULT CALLBACK DxgiProxy::MirrorWindowSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto* proxy = reinterpret_cast<DxgiProxy*>(dwRefData);
    if (!proxy) {
        proxy = s_activeProxy;
    }

    switch (uMsg) {
        case WM_ENTERSIZEMOVE: {
            if (proxy) {
                proxy->m_inSizeMove = true;
            }
            break;
        }
        case WM_EXITSIZEMOVE: {
            if (proxy) {
                proxy->m_inSizeMove = false;
                proxy->m_windowClampDirty = true;
            }
            break;
        }
        case WM_GETMINMAXINFO: {
            // First let the underlying window proc process the message
            LRESULT lres = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            if (mmi && proxy && proxy->IsActive()) {
                RECT clamped = ComputeClampedMirrorWindowRect(hWnd);
                int maxW = clamped.right - clamped.left;
                int maxH = clamped.bottom - clamped.top;
                if (maxW > 0 && maxH > 0) {
                    // Lower ptMinTrackSize if it exceeds maxW/maxH so Windows allows shrinking
                    if (mmi->ptMinTrackSize.x > maxW) {
                        mmi->ptMinTrackSize.x = maxW;
                    }
                    if (mmi->ptMinTrackSize.y > maxH) {
                        mmi->ptMinTrackSize.y = maxH;
                    }
                    // Cap ptMaxTrackSize and ptMaxSize so the window cannot expand past clamped bounds
                    mmi->ptMaxTrackSize.x = (std::min)(mmi->ptMaxTrackSize.x, static_cast<LONG>(maxW));
                    mmi->ptMaxTrackSize.y = (std::min)(mmi->ptMaxTrackSize.y, static_cast<LONG>(maxH));
                    mmi->ptMaxSize.x = (std::min)(mmi->ptMaxSize.x, static_cast<LONG>(maxW));
                    mmi->ptMaxSize.y = (std::min)(mmi->ptMaxSize.y, static_cast<LONG>(maxH));
                }
            }
            return lres;
        }
        case WM_WINDOWPOSCHANGING: {
            // First let the underlying window proc process the message
            LRESULT lres = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);
            if (wp && proxy && proxy->IsActive() && !(wp->flags & SWP_NOSIZE)) {
                RECT clamped = ComputeClampedMirrorWindowRect(hWnd);
                int maxW = clamped.right - clamped.left;
                int maxH = clamped.bottom - clamped.top;
                if (maxW > 0 && wp->cx > maxW) {
                    wp->cx = maxW;
                }
                if (maxH > 0 && wp->cy > maxH) {
                    wp->cy = maxH;
                }
                // NOTE: We deliberately do NOT modify wp->x or wp->y so the user can freely move/drag the window.
            }
            return lres;
        }
        case WM_SIZE: {
            if (proxy && proxy->IsActive() && proxy->m_targetRenderWidth > 0 && proxy->m_targetRenderHeight > 0) {
                if (wParam != SIZE_MINIMIZED) {
                    LPARAM targetLParam = MAKELPARAM(
                        static_cast<WORD>(proxy->m_targetRenderWidth),
                        static_cast<WORD>(proxy->m_targetRenderHeight)
                    );
                    return DefSubclassProc(hWnd, uMsg, wParam, targetLParam);
                }
            }
            return DefSubclassProc(hWnd, uMsg, wParam, lParam);
        }
        case WM_DISPLAYCHANGE: {
            if (proxy) {
                proxy->m_windowClampDirty = true;
            }
            return DefSubclassProc(hWnd, uMsg, wParam, lParam);
        }
        case WM_WINDOWPOSCHANGED: {
            if (proxy) {
                proxy->m_windowClampDirty = true;
            }
            LRESULT lres = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            if (proxy && proxy->IsActive() && proxy->m_targetRenderWidth > 0 && proxy->m_targetRenderHeight > 0) {
                auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);
                if (wp && !(wp->flags & SWP_NOSIZE)) {
                    LPARAM targetLParam = MAKELPARAM(
                        static_cast<WORD>(proxy->m_targetRenderWidth),
                        static_cast<WORD>(proxy->m_targetRenderHeight)
                    );
                    DefSubclassProc(hWnd, WM_SIZE, SIZE_RESTORED, targetLParam);
                }
            }
            return lres;
        }
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK: {
            if (proxy && proxy->IsActive() && proxy->m_targetRenderWidth > 0 && proxy->m_targetRenderHeight > 0) {
                RECT physRect{};
                BOOL gotRect = FALSE;
                {
                    InternalWindowQueryScope scope;
                    gotRect = GetClientRect(hWnd, &physRect);
                }
                int physW = physRect.right - physRect.left;
                int physH = physRect.bottom - physRect.top;
                if (gotRect && physW > 0 && physH > 0) {
                    int clientX = static_cast<short>(LOWORD(lParam));
                    int clientY = static_cast<short>(HIWORD(lParam));
                    int scaledX = static_cast<int>(std::round((static_cast<double>(clientX) * proxy->m_targetRenderWidth) / physW));
                    int scaledY = static_cast<int>(std::round((static_cast<double>(clientY) * proxy->m_targetRenderHeight) / physH));
                    scaledX = (std::max)(0, (std::min)(scaledX, static_cast<int>(proxy->m_targetRenderWidth)));
                    scaledY = (std::max)(0, (std::min)(scaledY, static_cast<int>(proxy->m_targetRenderHeight)));
                    LPARAM scaledLParam = MAKELPARAM(static_cast<WORD>(scaledX), static_cast<WORD>(scaledY));
                    return DefSubclassProc(hWnd, uMsg, wParam, scaledLParam);
                }
            }
            break;
        }
        case WM_NCDESTROY: {
            RemoveWindowSubclass(hWnd, &DxgiProxy::MirrorWindowSubclassProc, uIdSubclass);
            if (proxy) {
                proxy->m_windowSubclassed = false;
                proxy->m_inSizeMove = false;
                if (proxy->m_gameHwnd == hWnd) {
                    proxy->m_gameHwnd = nullptr;
                }
            }
            break;
        }
        default:
            break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void DxgiProxy::UpdateMenuCursorState(IDXGISwapChain* swapChain) {
    if (!m_openXrManager) return;

    HWND hWnd = m_gameHwnd;
    if (!hWnd && swapChain) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow) {
            SetGameHwnd(desc.OutputWindow);
            hWnd = m_gameHwnd;
        }
    }

    // If there is no game window associated with this swapchain, do not guess or alter state
    if (!hWnd) {
        return;
    }

    // Suppress cursor if the game window does not have foreground focus
    HWND fgWnd = GetForegroundWindow();
    if (fgWnd != hWnd && !IsChild(hWnd, fgWnd)) {
        m_openXrManager->SetMenuCursorState({ false, 0.0f, 0.0f, MenuCursorState::kDefaultCursorSize });
        return;
    }

    CURSORINFO ci{ sizeof(CURSORINFO) };
    bool cursorShowing = (GetCursorInfo(&ci) != FALSE) && ((ci.flags & CURSOR_SHOWING) != 0);

    if (cursorShowing) {
        POINT curPos{};
        POINT screenPt = GetCursorPos(&curPos) ? curPos : ci.ptScreenPos;
        float normX = 0.0f, normY = 0.0f;
        if (ScreenToNormalizedClient(hWnd, screenPt, normX, normY)) {
            m_openXrManager->SetMenuCursorState({ true, normX, normY, MenuCursorState::kDefaultCursorSize });
            return;
        }
    }

    m_openXrManager->SetMenuCursorState({ false, 0.0f, 0.0f, MenuCursorState::kDefaultCursorSize });
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_CreateSwapChain(
    IDXGIFactory* self, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    if (pDevice) {
        proxy->ResolveD3D12DeviceAndQueue(pDevice);
    }
    if (pDesc && proxy->IsActive()) {
        if (pDesc->BufferDesc.Width > 0 && pDesc->BufferDesc.Height > 0) {
            proxy->m_targetRenderWidth = pDesc->BufferDesc.Width;
            proxy->m_targetRenderHeight = pDesc->BufferDesc.Height;
        }
        pDesc->Windowed = TRUE;
    }
    if (pDesc && pDesc->OutputWindow) {
        proxy->SetGameHwnd(pDesc->OutputWindow);
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**)>(
        proxy->m_detourCreateSwapChain.GetOriginal()
    );
    HRESULT hr = orig ? orig(self, pDevice, pDesc, ppSwapChain) : E_FAIL;
    if (SUCCEEDED(hr)) {
        if (pDesc && pDesc->OutputWindow && proxy->IsActive()) {
            proxy->ApplyInitialWindowClamp(pDesc->OutputWindow);
        }
        if (ppSwapChain && *ppSwapChain) {
            proxy->AttachSwapChain(*ppSwapChain);
        }
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
    if (pDesc && proxy->IsActive()) {
        if (pDesc->Width > 0 && pDesc->Height > 0) {
            proxy->m_targetRenderWidth = pDesc->Width;
            proxy->m_targetRenderHeight = pDesc->Height;
        }
    }
    if (hWnd) {
        proxy->SetGameHwnd(hWnd);
    }

    DXGI_SWAP_CHAIN_DESC1 localDesc{};
    const DXGI_SWAP_CHAIN_DESC1* pEffectiveDesc = pDesc;
    if (pDesc && proxy->IsActive()) {
        localDesc = *pDesc;
        localDesc.Scaling = DXGI_SCALING_STRETCH;
        pEffectiveDesc = &localDesc;
        const_cast<DXGI_SWAP_CHAIN_DESC1*>(pDesc)->Scaling = DXGI_SCALING_STRETCH;
        LogInfo("DxgiProxy: Enforced DXGI_SCALING_STRETCH on swapchain descriptor for resolution decoupling.");
    }

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC localFullscreenDesc{};
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pEffectiveFullscreenDesc = pFullscreenDesc;
    if (pFullscreenDesc && proxy->IsActive()) {
        localFullscreenDesc = *pFullscreenDesc;
        localFullscreenDesc.Windowed = TRUE;
        pEffectiveFullscreenDesc = &localFullscreenDesc;
        const_cast<DXGI_SWAP_CHAIN_FULLSCREEN_DESC*>(pFullscreenDesc)->Windowed = TRUE;
        LogInfo("DxgiProxy: Enforced Windowed = TRUE on swapchain descriptor for resolution decoupling.");
    }

    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**)>(
        proxy->m_detourCreateSwapChainForHwnd.GetOriginal()
    );
    HRESULT hr = orig ? orig(self, pDevice, hWnd, pEffectiveDesc, pEffectiveFullscreenDesc, pRestrictToOutput, ppSwapChain) : S_OK;
    if (SUCCEEDED(hr)) {
        if (hWnd && proxy->IsActive()) {
            proxy->ApplyInitialWindowClamp(hWnd);
        }
        if (ppSwapChain && *ppSwapChain) {
            proxy->AttachSwapChain(*ppSwapChain);
        }
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
