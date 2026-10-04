#include "dxgi_proxy.h"
#include "core/logger.h"
#include "core/dll_lifecycle.h"
#include "ngx/ngx_interceptor.h"
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
        std::shared_ptr<VRBridge>(&VRBridge::Instance(), [](auto*) {}),
        std::shared_ptr<MirrorWindowController>(&MirrorWindowController::Instance(), [](auto*) {})
    );
    return instance;
}

DxgiProxy* DxgiProxy::GetActiveProxy() {
    return s_activeProxy;
}

DxgiProxy::DxgiProxy(std::shared_ptr<ISystemDxgi> systemDxgi,
                     std::shared_ptr<OpenXrManager> openXrManager,
                     std::shared_ptr<LuauInterceptor> luauInterceptor,
                     std::shared_ptr<GraphicsTelemetryProbe> telemetryProbe,
                     std::shared_ptr<D3D12Interceptor> d3d12Interceptor,
                     std::shared_ptr<VRBridge> vrBridge,
                     std::shared_ptr<MirrorWindowController> windowController)
    : m_systemDxgi(systemDxgi ? std::move(systemDxgi) : std::shared_ptr<ISystemDxgi>(&SystemDxgi::Instance(), [](auto*) {})),
      m_openXrManager(openXrManager ? std::move(openXrManager) : std::shared_ptr<OpenXrManager>(&OpenXrManager::Instance(), [](auto*) {})),
      m_luauInterceptor(luauInterceptor ? std::move(luauInterceptor) : std::shared_ptr<LuauInterceptor>(&LuauInterceptor::Instance(), [](auto*) {})),
      m_telemetryProbe(std::move(telemetryProbe)),
      m_d3d12Interceptor(d3d12Interceptor ? std::move(d3d12Interceptor) : std::shared_ptr<D3D12Interceptor>(&D3D12Interceptor::Instance(), [](auto*) {})),
      m_vrBridge(vrBridge ? std::move(vrBridge) : std::shared_ptr<VRBridge>(&VRBridge::Instance(), [](auto*) {})),
      m_windowController(windowController ? std::move(windowController) : std::make_shared<MirrorWindowController>()) {
    m_windowController->SetIsVrActiveCallback([this]() { return IsActive(); });
    MirrorWindowController::SetActiveController(m_windowController.get());
    s_activeProxy = this;
}

DxgiProxy::~DxgiProxy() {
    if (IsProcessTerminating()) return;
    DetachAll();
    if (s_activeProxy == this) {
        s_activeProxy = nullptr;
    }
}

void DxgiProxy::EnsureVrInitialized() {
    // Deferred from DllMain (loader lock). Runs before the game creates its D3D12 device.
    // Installed on every factory call (idempotent and locked), not only the first, so a concurrent
    // caller blocks until the LoadLibrary hooks exist instead of loading nvngx unhooked.
    NgxInterceptor::Instance().InstallModuleLoadHooks();
    if (!m_vrInitialized.exchange(true)) {
        NgxInterceptor::Instance().CheckAndAttachLoadedModule();
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
    HWND gameHwnd = GetGameHwnd();
    if (!gameHwnd) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow) {
            SetGameHwnd(desc.OutputWindow);
        }
    } else if (gameHwnd && !IsWindowSubclassed() && m_openXrManager && m_openXrManager->IsActive()) {
        AttachWindowSubclass(gameHwnd);
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

    if (GetTargetRenderWidth() == 0 || GetTargetRenderHeight() == 0) {
        DXGI_SWAP_CHAIN_DESC scDesc{};
        if (SUCCEEDED(swapChain->GetDesc(&scDesc))) {
            if (scDesc.BufferDesc.Width > 0 && scDesc.BufferDesc.Height > 0) {
                SetTargetRenderResolution(scDesc.BufferDesc.Width, scDesc.BufferDesc.Height);
            }
        }
    }
    if (GetTargetRenderWidth() == 0 || GetTargetRenderHeight() == 0) {
        Microsoft::WRL::ComPtr<ID3D12Resource> testBuffer;
        if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&testBuffer))) && testBuffer) {
            D3D12_RESOURCE_DESC resDesc = testBuffer->GetDesc();
            if (resDesc.Width > 0 && resDesc.Height > 0) {
                SetTargetRenderResolution(static_cast<uint32_t>(resDesc.Width), resDesc.Height);
            }
        }
    }

    m_swapChainAttached = presentAttached;
    if (!m_swapChainAttached) {
        LogError("DxgiProxy::AttachSwapChain: Failed to install swap chain detours.");
    } else {
        LogInfo("DxgiProxy: Successfully attached swap chain presentation and buffer detours (Target: " +
                std::to_string(GetTargetRenderWidth()) + "x" + std::to_string(GetTargetRenderHeight()) + ").");
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
    if (m_windowController) {
        m_windowController->DetachAll();
    }
    if (m_detourPresent.IsInstalled()) m_detourPresent.Uninstall();
    if (m_detourPresent1.IsInstalled()) m_detourPresent1.Uninstall();
    if (m_detourResizeBuffers.IsInstalled()) m_detourResizeBuffers.Uninstall();
    if (m_detourResizeBuffers1.IsInstalled()) m_detourResizeBuffers1.Uninstall();
    if (m_detourCreateSwapChain.IsInstalled()) m_detourCreateSwapChain.Uninstall();
    if (m_detourCreateSwapChainForHwnd.IsInstalled()) m_detourCreateSwapChainForHwnd.Uninstall();
    m_swapChainAttached = false;
    m_factoryAttached = false;
    m_cachedSwapChain = nullptr;
    m_cachedSwapChain3.Reset();
    m_cachedBackBuffers.clear();
    m_lastPresentSuppressed = false;
    m_pendingRenderingMode.reset();
}

void DxgiProxy::LatchPendingRenderingMode() {
    if (m_pendingRenderingMode.has_value()) {
        VRRenderingMode newMode = *m_pendingRenderingMode;
        m_pendingRenderingMode.reset();
        if (m_vrBridge) {
            m_vrBridge->SetRenderingMode(newMode);
        }
        LogInfo("DxgiProxy: Latched pending rendering mode on completed compositor frame boundary: " +
                std::string(newMode == VRRenderingMode::SequentialDualPass ? "SequentialDualPass" : "AFR"));
    }
}

bool DxgiProxy::OnPresent(IDXGISwapChain* swapChain, UINT /*syncInterval*/, UINT /*flags*/) {
    m_lastPresentSuppressed = false;
    OnPrePresent(swapChain);

    if (!m_openXrManager || !m_openXrManager->IsActive()) {
        return true;
    }

    // Poll runtime events once per presented frame; this may begin/end the session or
    // drop the Injector into Pass-Through Mode (EXITING, LOSS_PENDING, instance loss).
    m_openXrManager->PollEvents();
    if (!m_openXrManager->IsActive()) {
        m_lastPresentSuppressed = false;
        return true;
    }

    if (!GetGameHwnd() && swapChain) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow) {
            SetGameHwnd(desc.OutputWindow);
        }
    }
    if (GetGameHwnd() && m_windowController && m_windowController->IsWindowClampDirty()) {
        EnforceClampedMirrorWindowDimensions(GetGameHwnd());
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
            m_cachedBackBuffers.clear();
            swapChain->QueryInterface(IID_PPV_ARGS(&m_cachedSwapChain3));
        }
        if (m_cachedSwapChain3) {
            bufferIndex = m_cachedSwapChain3->GetCurrentBackBufferIndex();
        }

        if (bufferIndex < m_cachedBackBuffers.size() && m_cachedBackBuffers[bufferIndex]) {
            pBackBuffer = m_cachedBackBuffers[bufferIndex].Get();
        } else {
            if (SUCCEEDED(swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBuffer)))) {
                pBackBuffer = backBuffer.Get();
                if (bufferIndex >= m_cachedBackBuffers.size()) {
                    m_cachedBackBuffers.resize(bufferIndex + 1);
                }
                m_cachedBackBuffers[bufferIndex] = backBuffer;
            }
        }
    }

    if (m_openXrManager->HasEyeSwapchains() && !m_openXrManager->IsSessionRunning()) {
        // Session is IDLE/STOPPING (headset asleep, dashboard) or not yet READY: submit no VR
        // frames and let the game keep presenting to the Desktop Mirror Window.
        // On resume, start a fresh eye pair and re-align the menu quad to the new head pose.
        if (m_vrBridge) {
            m_vrBridge->SetFrameParity(0);
            if (m_vrBridge->GetSubPassIndex() != 0) {
                m_vrBridge->ResetSubPass();
            }
        }
        m_wasInQuadMode = false;
        m_lastPresentSuppressed = false;
        return true;
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
            m_lastPresentSuppressed = false;
            return true;
        } else {
            if (m_wasInQuadMode) {
                // Seamlessly returning to stereoscopic 3D VR view: reset AFR frame parity and subPassIndex
                if (m_vrBridge) {
                    m_vrBridge->SetFrameParity(0);
                    m_vrBridge->ResetSubPass();
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
                LatchPendingRenderingMode();
                m_lastPresentSuppressed = false;
                return true;
            } else {
                VRRenderingMode renderingMode = m_vrBridge ? m_vrBridge->GetRenderingMode() : VRRenderingMode::AFR;
                if (renderingMode == VRRenderingMode::SequentialDualPass) {
                    uint32_t subPass = m_vrBridge ? m_vrBridge->GetSubPassIndex() : 0;
                    if (subPass == 0) {
                        // Sub-Pass 0 (Left Eye): Copies backbuffer to OpenXR eye swapchain 0, suppresses physical present, advances subPassIndex to 1
                        m_openXrManager->CopyBackBufferToEye(0, pBackBuffer);
                        if (m_vrBridge) {
                            m_vrBridge->SetSubPassIndex(1);
                        }
                        m_lastPresentSuppressed = true;
                        return false;
                    } else {
                        // Sub-Pass 1 (Right Eye): Copies backbuffer to OpenXR eye swapchain 1, submits pair, executes physical present, resets subPassIndex to 0
                        m_openXrManager->CopyBackBufferToEye(1, pBackBuffer);
                        m_openXrManager->SubmitFramePair();
                        if (m_vrBridge) {
                            m_vrBridge->AdvanceFrame();
                        }
                        LatchPendingRenderingMode();
                        m_lastPresentSuppressed = false;
                        return true;
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

                    if (parity == 1) {
                        LatchPendingRenderingMode();
                    }
                    m_lastPresentSuppressed = false;
                    return true;
                }
            }
        }
    }
    m_lastPresentSuppressed = false;
    return true;
}

UINT DxgiProxy::EffectiveSyncInterval(UINT gameSyncInterval) const {
    // xrWaitFrame paces frames only while the session runs. While it is paused (headset asleep,
    // not yet READY) keep the game's own vsync so the Desktop Mirror Window does not run uncapped.
    bool xrPacing = m_openXrManager && m_openXrManager->IsActive() && m_openXrManager->IsSessionRunning();
    return xrPacing ? 0 : gameSyncInterval;
}

bool DxgiProxy::OnPresent1(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* /*params*/) {
    return OnPresent(swapChain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_Present(IDXGISwapChain* self, UINT syncInterval, UINT flags) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    bool shouldPresent = proxy->OnPresent(self, syncInterval, flags);
    if (!shouldPresent) {
        return S_OK;
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT)>(
        proxy->m_detourPresent.GetOriginal()
    );
    if (orig) {
        return orig(self, proxy->EffectiveSyncInterval(syncInterval), flags);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_Present1(IDXGISwapChain1* self, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* params) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    bool shouldPresent = proxy->OnPresent1(self, syncInterval, flags, params);
    if (!shouldPresent) {
        return S_OK;
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*)>(
        proxy->m_detourPresent1.GetOriginal()
    );
    if (orig) {
        return orig(self, proxy->EffectiveSyncInterval(syncInterval), flags, params);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_ResizeBuffers(
    IDXGISwapChain* self, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    if (proxy) {
        proxy->m_cachedSwapChain = nullptr;
        proxy->m_cachedSwapChain3.Reset();
        proxy->m_cachedBackBuffers.clear();
        proxy->GetWindowController().SetWindowClampDirty(true);
    }
    UINT effectiveWidth = Width;
    UINT effectiveHeight = Height;
    if (proxy && proxy->IsActive() && proxy->GetTargetRenderWidth() > 0 && proxy->GetTargetRenderHeight() > 0) {
        effectiveWidth = proxy->GetTargetRenderWidth();
        effectiveHeight = proxy->GetTargetRenderHeight();
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
        proxy->m_cachedBackBuffers.clear();
        proxy->GetWindowController().SetWindowClampDirty(true);
    }
    UINT effectiveWidth = Width;
    UINT effectiveHeight = Height;
    if (proxy && proxy->IsActive() && proxy->GetTargetRenderWidth() > 0 && proxy->GetTargetRenderHeight() > 0) {
        effectiveWidth = proxy->GetTargetRenderWidth();
        effectiveHeight = proxy->GetTargetRenderHeight();
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

HWND DxgiProxy::GetGameHwnd() const {
    return m_windowController ? m_windowController->GetGameHwnd() : nullptr;
}

void DxgiProxy::SetGameHwnd(HWND hWnd) {
    if (m_windowController) {
        m_windowController->SetGameHwnd(hWnd);
    }
}

void DxgiProxy::UpdateMenuCursorState(IDXGISwapChain* swapChain) {
    if (m_windowController) {
        m_windowController->UpdateMenuCursorState(swapChain, m_openXrManager.get());
    }
}

bool DxgiProxy::ScreenToNormalizedClient(HWND hWnd, POINT screenPt, float& outNormX, float& outNormY) {
    return MirrorWindowController::ScreenToNormalizedClient(hWnd, screenPt, outNormX, outNormY);
}

bool DxgiProxy::ScaleVirtualCursorToPhysical(HWND hWnd, int screenX, int screenY, int& outPhysScreenX, int& outPhysScreenY) {
    auto* ctrl = s_activeProxy ? &s_activeProxy->GetWindowController() : &MirrorWindowController::Instance();
    return ctrl->ScaleVirtualCursorToPhysical(hWnd, screenX, screenY, outPhysScreenX, outPhysScreenY);
}

bool DxgiProxy::ConstrainVirtualClipRectToPhysical(HWND hWnd, const RECT* lpRect, RECT& outPhysClipRect) {
    auto* ctrl = s_activeProxy ? &s_activeProxy->GetWindowController() : &MirrorWindowController::Instance();
    return ctrl->ConstrainVirtualClipRectToPhysical(hWnd, lpRect, outPhysClipRect);
}

RECT DxgiProxy::ComputeClampedMirrorWindowRect(const RECT& workArea, DWORD dwStyle, DWORD dwExStyle, bool hasMenu) {
    return MirrorWindowController::ComputeClampedMirrorWindowRect(workArea, dwStyle, dwExStyle, hasMenu);
}

RECT DxgiProxy::ComputeClampedMirrorWindowRect(HWND hWnd) {
    return MirrorWindowController::ComputeClampedMirrorWindowRect(hWnd);
}

bool DxgiProxy::AttachWindowSubclass(HWND hWnd) {
    return m_windowController ? m_windowController->AttachWindowSubclass(hWnd) : false;
}

bool DxgiProxy::DetachWindowSubclass() {
    return m_windowController ? m_windowController->DetachWindowSubclass() : false;
}

bool DxgiProxy::IsWindowSubclassed() const {
    return m_windowController ? m_windowController->IsWindowSubclassed() : false;
}

bool DxgiProxy::IsInSizeMove() const {
    return m_windowController ? m_windowController->IsInSizeMove() : false;
}

uint32_t DxgiProxy::GetTargetRenderWidth() const {
    return m_windowController ? m_windowController->GetTargetRenderWidth() : 0;
}

uint32_t DxgiProxy::GetTargetRenderHeight() const {
    return m_windowController ? m_windowController->GetTargetRenderHeight() : 0;
}

void DxgiProxy::SetTargetRenderResolution(uint32_t width, uint32_t height) {
    if (m_windowController) {
        m_windowController->SetTargetRenderResolution(width, height);
    }
}

void DxgiProxy::GetDecoupledTargetResolution(uint32_t& outWidth, uint32_t& outHeight) const {
    if (m_windowController) {
        m_windowController->GetDecoupledTargetResolution(outWidth, outHeight, m_openXrManager.get());
    } else {
        outWidth = 0;
        outHeight = 0;
    }
}

bool DxgiProxy::ApplyInitialWindowClamp(HWND hWnd) {
    return m_windowController ? m_windowController->ApplyInitialWindowClamp(hWnd) : false;
}

void DxgiProxy::EnforceClampedMirrorWindowDimensions(HWND hWnd) {
    if (m_windowController) {
        m_windowController->EnforceClampedMirrorWindowDimensions(hWnd);
    }
}

LRESULT CALLBACK DxgiProxy::MirrorWindowSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    return MirrorWindowController::MirrorWindowSubclassProc(hWnd, uMsg, wParam, lParam, uIdSubclass, dwRefData);
}

BOOL WINAPI DxgiProxy::Intercept_GetClientRect(HWND hWnd, LPRECT lpRect) {
    return MirrorWindowController::Intercept_GetClientRect(hWnd, lpRect);
}

BOOL WINAPI DxgiProxy::Intercept_ScreenToClient(HWND hWnd, LPPOINT lpPoint) {
    return MirrorWindowController::Intercept_ScreenToClient(hWnd, lpPoint);
}

BOOL WINAPI DxgiProxy::Intercept_SetCursorPos(int X, int Y) {
    return MirrorWindowController::Intercept_SetCursorPos(X, Y);
}

BOOL WINAPI DxgiProxy::Intercept_ClipCursor(const RECT* lpRect) {
    return MirrorWindowController::Intercept_ClipCursor(lpRect);
}

bool DxgiProxy::IsDetourGetClientRectInstalled() const {
    return m_windowController ? m_windowController->IsDetourGetClientRectInstalled() : false;
}

bool DxgiProxy::IsDetourScreenToClientInstalled() const {
    return m_windowController ? m_windowController->IsDetourScreenToClientInstalled() : false;
}

bool DxgiProxy::IsDetourSetCursorPosInstalled() const {
    return m_windowController ? m_windowController->IsDetourSetCursorPosInstalled() : false;
}

bool DxgiProxy::IsDetourClipCursorInstalled() const {
    return m_windowController ? m_windowController->IsDetourClipCursorInstalled() : false;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_CreateSwapChain(
    IDXGIFactory* self, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {
    DxgiProxy* proxy = s_activeProxy ? s_activeProxy : &DxgiProxy::Instance();
    if (pDevice) {
        proxy->ResolveD3D12DeviceAndQueue(pDevice);
    }
    if (pDesc && proxy->IsActive()) {
        // Before any override below: an unusable format must leave the swap chain untouched.
        proxy->m_openXrManager->EnsureBackBufferFormatSupported(pDesc->BufferDesc.Format);
    }
    if (pDesc && proxy->IsActive()) {
        if (pDesc->BufferDesc.Width > 0 && pDesc->BufferDesc.Height > 0) {
            proxy->SetTargetRenderResolution(pDesc->BufferDesc.Width, pDesc->BufferDesc.Height);
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
        // Before any override below: an unusable format must leave the swap chain untouched.
        proxy->m_openXrManager->EnsureBackBufferFormatSupported(pDesc->Format);
    }
    if (pDesc && proxy->IsActive()) {
        if (pDesc->Width > 0 && pDesc->Height > 0) {
            proxy->SetTargetRenderResolution(pDesc->Width, pDesc->Height);
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
