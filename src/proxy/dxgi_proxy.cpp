#include "dxgi_proxy.h"
#include "core/logger.h"
#include "core/thread_trace.h"
#include "core/injector_runtime.h"
#include "core/dll_lifecycle.h"
#include "ngx/ngx_interceptor.h"
#include <utility>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace fs25vr {

namespace {

template <typename PFN, typename... Args>
HRESULT ForwardFactoryCall(const char* funcName, PFN pfn, Args&&... args) {
    if (!pfn) {
        LogError(std::string("System ") + funcName + " pointer is null.");
        return E_FAIL;
    }
    return pfn(std::forward<Args>(args)...);
}

// The passthrough exports touch no Injector subsystem, so they reach the system dxgi.dll through a
// loader of their own: a PIX or app-compat call should not construct and wire every subsystem.
FARPROC SystemExport(const char* name) {
    static SystemDxgi passthrough;
    return passthrough.GetExport(name);
}

// The proxy whose VR initialization is running on this thread. The OpenXR runtime is loaded into the
// game process and its dxgi.dll import binds to this proxy, so a runtime that creates a DXGI factory
// during setup (VDXR's xrGetSystem does) calls back into EnsureVrInitialized on the same thread.
thread_local const DxgiProxy* t_initializingProxy = nullptr;

struct VrInitScope {
    explicit VrInitScope(const DxgiProxy* proxy) { t_initializingProxy = proxy; }
    ~VrInitScope() { t_initializingProxy = nullptr; }
    VrInitScope(const VrInitScope&) = delete;
    VrInitScope& operator=(const VrInitScope&) = delete;
};

} // anonymous namespace

bool IsHdrColorSpace(DXGI_COLOR_SPACE_TYPE colorSpace) {
    switch (colorSpace) {
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:          // scRGB
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:       // HDR10
    case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
    case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
    case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
    case DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020: // HLG
    case DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
        return true;
    default:
        return false;
    }
}

DxgiProxy* DxgiProxy::ForHook(FunctionDetour DxgiProxy::*detour) {
    InjectorRuntime* runtime = InjectorRuntime::ForHook([detour](InjectorRuntime& r) {
        return (r.GetDxgiProxy().*detour).IsInstalled();
    });
    return runtime ? &runtime->GetDxgiProxy() : nullptr;
}

DxgiProxy::DxgiProxy(std::shared_ptr<ISystemDxgi> systemDxgi,
                     std::shared_ptr<OpenXrManager> openXrManager,
                     std::shared_ptr<LuauInterceptor> luauInterceptor,
                     std::shared_ptr<VRBridge> vrBridge,
                     std::shared_ptr<MirrorWindowController> windowController,
                     std::shared_ptr<NgxInterceptor> ngxInterceptor)
    : m_systemDxgi(std::move(systemDxgi)),
      m_openXrManager(std::move(openXrManager)),
      m_luauInterceptor(std::move(luauInterceptor)),
      m_vrBridge(std::move(vrBridge)),
      m_windowController(std::move(windowController)),
      m_ngxInterceptor(std::move(ngxInterceptor)) {
    m_windowController->SetIsVrActiveCallback([this]() { return IsActive(); });
}

DxgiProxy::~DxgiProxy() {
    if (IsProcessTerminating()) return;
    DetachAll();
}

void DxgiProxy::EnsureVrInitialized() {
    // Deferred from DllMain (loader lock). Runs before the game creates its D3D12 device.
    // Installed on every factory call (idempotent and locked), not only the first, so a concurrent
    // caller blocks until the LoadLibrary hooks exist instead of loading nvngx unhooked.
    if (m_ngxInterceptor) m_ngxInterceptor->InstallModuleLoadHooks();
    // A runtime calling back in from inside our own initialization would wait on call_once forever;
    // it gets its factory from the system dxgi.dll with the OpenXR state as initialization left it.
    if (t_initializingProxy == this) return;
    // call_once, not a flag: a concurrent caller waits until initialization finishes, because it
    // decides from the OpenXR state whether to hook the factory it is about to return.
    std::call_once(m_vrInitOnce, [this] {
        VrInitScope scope(this);
        if (m_ngxInterceptor) m_ngxInterceptor->CheckAndAttachLoadedModule();
        if (!m_openXrManager) {
            LogWarn("DxgiProxy: No OpenXR manager supplied; forwarding all calls silently.");
        } else {
            LogInfo("Intercepted DXGI startup sequence. Initializing OpenXR subsystem...");
            // Info lines are buffered; flush so a runtime that hangs below still leaves a log.
            Logger::Instance().Flush();
            m_openXrManager->Initialize();
            if (m_openXrManager->IsPassThrough()) {
                LogInfo("Injector operating in Pass-Through Mode (forwarding all calls silently).");
            } else if (m_openXrManager->IsActive()) {
                // No session yet: the game has no D3D12 device to bind one to until it creates its
                // swap chain, and SetD3D12DeviceAndQueue creates the session then.
                LogInfo("Injector operating in Active VR Mode.");
            }
        }
        if (m_luauInterceptor && m_luauInterceptor->AttachToEngine()) {
            LogInfo("In-process Luau state interception channel armed.");
        } else {
            LogWarn("In-process Luau state interception channel not armed; VRBridge will not be injected.");
        }
    });
}

void DxgiProxy::SetD3D12DeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue) {
    m_d3d12Device = device;
    m_d3d12Queue = queue;
    if (m_openXrManager && device && queue) {
        m_openXrManager->SetD3D12DeviceAndQueue(device, queue);
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
    HWND gameHwnd = m_windowController->GetGameHwnd();
    if (!gameHwnd) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow) {
            m_windowController->SetGameHwnd(desc.OutputWindow);
        }
    } else if (gameHwnd && !m_windowController->IsWindowSubclassed() && m_openXrManager && m_openXrManager->IsActive()) {
        m_windowController->AttachWindowSubclass(gameHwnd);
    }
    if (m_swapChainAttached) return true;
    if (!m_ownedByRuntime) {
        LogWarn("DxgiProxy::AttachSwapChain: Not owned by a live InjectorRuntime; detours not installed.");
        return false;
    }

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
            m_detourCheckColorSpaceSupport.Install(
                vtable3[kCheckColorSpaceSupportVTableSlot], reinterpret_cast<void*>(&DxgiProxy::Intercept_CheckColorSpaceSupport));
            m_detourSetColorSpace1.Install(
                vtable3[kSetColorSpace1VTableSlot], reinterpret_cast<void*>(&DxgiProxy::Intercept_SetColorSpace1));
        }
    }

    if (m_windowController->GetTargetRenderWidth() == 0 || m_windowController->GetTargetRenderHeight() == 0) {
        DXGI_SWAP_CHAIN_DESC scDesc{};
        if (SUCCEEDED(swapChain->GetDesc(&scDesc))) {
            if (scDesc.BufferDesc.Width > 0 && scDesc.BufferDesc.Height > 0) {
                m_windowController->SetTargetRenderResolution(scDesc.BufferDesc.Width, scDesc.BufferDesc.Height);
            }
        }
    }
    if (m_windowController->GetTargetRenderWidth() == 0 || m_windowController->GetTargetRenderHeight() == 0) {
        Microsoft::WRL::ComPtr<ID3D12Resource> testBuffer;
        if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&testBuffer))) && testBuffer) {
            D3D12_RESOURCE_DESC resDesc = testBuffer->GetDesc();
            if (resDesc.Width > 0 && resDesc.Height > 0) {
                m_windowController->SetTargetRenderResolution(static_cast<uint32_t>(resDesc.Width), resDesc.Height);
            }
        }
    }

    m_swapChainAttached = presentAttached;
    if (!m_swapChainAttached) {
        LogError("DxgiProxy::AttachSwapChain: Failed to install swap chain detours.");
    } else {
        LogInfo("DxgiProxy: Successfully attached swap chain presentation and buffer detours (Target: " +
                std::to_string(m_windowController->GetTargetRenderWidth()) + "x" + std::to_string(m_windowController->GetTargetRenderHeight()) + ").");
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
    if (m_factoryAttached) {
        AttachFirstOutput(factory);
        return true;
    }
    if (!m_ownedByRuntime) {
        LogWarn("DxgiProxy::AttachFactory: Not owned by a live InjectorRuntime; detours not installed.");
        return false;
    }

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
    // The game asks the display whether it is HDR before it creates its swap chain.
    AttachFirstOutput(factory);
    return m_factoryAttached;
}

void DxgiProxy::AttachFirstOutput(IDXGIFactory* factory) {
    if (m_outputAttached || !m_ownedByRuntime) return;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory1;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory1)))) return;
    // An adapter with no display (a render-only GPU) has no outputs, so look past it.
    for (UINT a = 0; a < 16 && !m_outputAttached; ++a) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory1->EnumAdapters1(a, &adapter)) || !adapter) break;
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        if (SUCCEEDED(adapter->EnumOutputs(0, &output)) && output) {
            AttachOutput(output.Get());
        }
    }
}

bool DxgiProxy::AttachOutput(IDXGIOutput* output) {
    if (m_outputAttached) return true;
    if (!output || !m_ownedByRuntime) return false;
    Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
    if (FAILED(output->QueryInterface(IID_PPV_ARGS(&output6))) || !output6) return false;
    void** vtable6 = *reinterpret_cast<void***>(output6.Get());
    if (!vtable6) return false;
    m_outputAttached = m_detourOutputGetDesc1.Install(
        vtable6[kOutputGetDesc1VTableSlot], reinterpret_cast<void*>(&DxgiProxy::Intercept_OutputGetDesc1));
    if (m_outputAttached) {
        LogInfo("DxgiProxy: Attached display output detour; HDR is hidden from the game while VR is active.");
    }
    return m_outputAttached;
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
    if (m_detourCheckColorSpaceSupport.IsInstalled()) m_detourCheckColorSpaceSupport.Uninstall();
    if (m_detourSetColorSpace1.IsInstalled()) m_detourSetColorSpace1.Uninstall();
    if (m_detourOutputGetDesc1.IsInstalled()) m_detourOutputGetDesc1.Uninstall();
    m_swapChainAttached = false;
    m_factoryAttached = false;
    m_outputAttached = false;
    m_lastSetColorSpace.reset();
    m_loggedHiddenColorSpaces = 0;
    m_loggedHiddenOutputHdr = false;
    m_cachedSwapChain = nullptr;
    m_cachedSwapChain3.Reset();
    m_cachedBackBuffers.clear();
    m_lastPresentSuppressed = false;
    if (m_vrBridge) {
        m_vrBridge->ClearPendingRenderingMode();
    }
}

void DxgiProxy::LatchPendingRenderingMode() {
    if (m_vrBridge) {
        m_vrBridge->LatchPendingRenderingMode();
    }
}

bool DxgiProxy::OnPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    m_presentWasStereo = false;
    bool shouldPresent = OnPresentImpl(swapChain, syncInterval, flags);
    // The rendered-frame counter stamps the Companion Mod's camera records and resolves them for DLSS
    // evaluations. It advances per stereo Present only; a Present outside stereo presentation (menu,
    // 2D Floating Window Mode, Pass-Through) renders no eye camera, so its records are dropped
    // rather than left to read as the current frame's.
    if (m_vrBridge) {
        if (m_presentWasStereo) {
            m_vrBridge->GetCameraRecords().AdvanceFrame();
        } else {
            m_vrBridge->GetCameraRecords().Clear();
        }
    }
    return shouldPresent;
}

bool DxgiProxy::OnPresentImpl(IDXGISwapChain* swapChain, UINT /*syncInterval*/, UINT /*flags*/) {
    FS25VR_TRACE_THREAD("DxgiProxy.Present");
    m_lastPresentSuppressed = false;

    if (!m_openXrManager || !m_openXrManager->IsActive()) {
        // Not Active: no eye pair can be in flight, so a queued mode change must not be lost.
        LatchPendingRenderingMode();
        return true;
    }

    // Poll runtime events before deciding how to present; this may begin/end the session or
    // drop the Injector into Pass-Through Mode (EXITING, LOSS_PENDING, instance loss).
    m_openXrManager->PollEvents();
    if (!m_openXrManager->IsActive()) {
        // Dropped to Pass-Through after a mode was queued; the released eye pair is abandoned.
        LatchPendingRenderingMode();
        m_lastPresentSuppressed = false;
        return true;
    }

    if (!m_windowController->GetGameHwnd() && swapChain) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow) {
            m_windowController->SetGameHwnd(desc.OutputWindow);
        }
    }
    if (m_windowController->GetGameHwnd() && m_windowController->IsWindowClampDirty()) {
        m_windowController->EnforceClampedMirrorWindowDimensions(m_windowController->GetGameHwnd());
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
        // No eye pair is in flight, so a queued Stereo Presentation Mode change is safe to apply now.
        LatchPendingRenderingMode();
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
            m_windowController->UpdateMenuCursorState(swapChain, m_openXrManager.get());

            // Floating quad layer mode:
            // Backbuffer is presented on an OpenXR Quad layer positioned in 3D tracking space
            // rather than warping across an immersive projection layer.
            // Active during pre-game / Main Menu, in-game GUI menus (ESC pause menu, shop, etc.),
            // or in 2D Floating Window Mode.
            m_openXrManager->SubmitMenuQuad();
            // The quad frame is a completed compositor frame and no eye pair is in flight.
            LatchPendingRenderingMode();
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
            m_presentWasStereo = true;
            if (m_openXrManager->GetMenuCursorState().visible) {
                m_openXrManager->SetMenuCursorState({ false, 0.0f, 0.0f, MenuCursorState::kDefaultCursorSize });
            }
            VRRenderingMode renderingMode = m_vrBridge ? m_vrBridge->GetRenderingMode() : VRRenderingMode::AFR;
            if (renderingMode == VRRenderingMode::SequentialDualPass) {
                uint32_t subPass = m_vrBridge ? m_vrBridge->GetSubPassIndex() : 0;
                if (subPass == 0) {
                    // Sub-Pass 0: copies the left eye, suppresses the physical present, advances subPassIndex to 1
                    m_openXrManager->CopyBackBufferToEye(0, pBackBuffer);
                    if (m_vrBridge) {
                        m_vrBridge->SetSubPassIndex(1);
                    }
                    m_lastPresentSuppressed = true;
                    return false;
                } else {
                    // Sub-Pass 1: copies the right eye, submits the pair, executes the physical present, resets subPassIndex to 0
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
    // No eye swapchains (creation failed or Pass-Through): no eye pair can be in flight.
    LatchPendingRenderingMode();
    m_lastPresentSuppressed = false;
    return true;
}

void DxgiProxy::OnPresentReturned(HRESULT presentResult) {
    // A failed Present (device removed or reset) leaves the next frame to the lazy begin in
    // CopyBackBufferToEye rather than blocking in xrWaitFrame ahead of device-lost handling.
    if (FAILED(presentResult)) return;
    if (m_openXrManager && m_openXrManager->IsActive() && m_openXrManager->IsSessionRunning() &&
        m_openXrManager->HasEyeSwapchains()) {
        m_openXrManager->BeginFrame();  // returns at once when a frame is already begun
    }
}

UINT DxgiProxy::EffectiveSyncInterval(UINT gameSyncInterval) const {
    // xrWaitFrame paces frames only while the session runs and eye swapchains exist (OnPresent calls it
    // only then). While it is paused (headset asleep, not yet READY) or swapchain creation failed, keep
    // the game's own vsync so the Desktop Mirror Window does not run uncapped.
    bool xrPacing = m_openXrManager && m_openXrManager->IsActive() && m_openXrManager->IsSessionRunning() &&
                    m_openXrManager->HasEyeSwapchains();
    return xrPacing ? 0 : gameSyncInterval;
}

bool DxgiProxy::OnPresent1(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* /*params*/) {
    return OnPresent(swapChain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_Present(IDXGISwapChain* self, UINT syncInterval, UINT flags) {
    DxgiProxy* proxy = ForHook(&DxgiProxy::m_detourPresent);
    if (!proxy) return S_OK;
    bool shouldPresent = proxy->OnPresent(self, syncInterval, flags);
    if (!shouldPresent) {
        proxy->OnPresentReturned();
        return S_OK;
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT)>(
        proxy->m_detourPresent.GetOriginal()
    );
    HRESULT hr = orig ? orig(self, proxy->EffectiveSyncInterval(syncInterval), flags) : S_OK;
    proxy->OnPresentReturned(hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_Present1(IDXGISwapChain1* self, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* params) {
    DxgiProxy* proxy = ForHook(&DxgiProxy::m_detourPresent1);
    if (!proxy) return S_OK;
    bool shouldPresent = proxy->OnPresent1(self, syncInterval, flags, params);
    if (!shouldPresent) {
        proxy->OnPresentReturned();
        return S_OK;
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*)>(
        proxy->m_detourPresent1.GetOriginal()
    );
    HRESULT hr = orig ? orig(self, proxy->EffectiveSyncInterval(syncInterval), flags, params) : S_OK;
    proxy->OnPresentReturned(hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_ResizeBuffers(
    IDXGISwapChain* self, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    DxgiProxy* proxy = ForHook(&DxgiProxy::m_detourResizeBuffers);
    if (proxy) {
        proxy->m_cachedSwapChain = nullptr;
        proxy->m_cachedSwapChain3.Reset();
        proxy->m_cachedBackBuffers.clear();
        proxy->m_windowController->SetWindowClampDirty(true);
    }
    UINT effectiveWidth = Width;
    UINT effectiveHeight = Height;
    if (proxy && proxy->IsActive() && proxy->m_windowController->GetTargetRenderWidth() > 0 && proxy->m_windowController->GetTargetRenderHeight() > 0) {
        effectiveWidth = proxy->m_windowController->GetTargetRenderWidth();
        effectiveHeight = proxy->m_windowController->GetTargetRenderHeight();
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
    DxgiProxy* proxy = ForHook(&DxgiProxy::m_detourResizeBuffers1);
    if (proxy) {
        proxy->m_cachedSwapChain = nullptr;
        proxy->m_cachedSwapChain3.Reset();
        proxy->m_cachedBackBuffers.clear();
        proxy->m_windowController->SetWindowClampDirty(true);
    }
    UINT effectiveWidth = Width;
    UINT effectiveHeight = Height;
    if (proxy && proxy->IsActive() && proxy->m_windowController->GetTargetRenderWidth() > 0 && proxy->m_windowController->GetTargetRenderHeight() > 0) {
        effectiveWidth = proxy->m_windowController->GetTargetRenderWidth();
        effectiveHeight = proxy->m_windowController->GetTargetRenderHeight();
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

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_CheckColorSpaceSupport(
    IDXGISwapChain3* self, DXGI_COLOR_SPACE_TYPE colorSpace, UINT* pColorSpaceSupport) {
    DxgiProxy* proxy = ForHook(&DxgiProxy::m_detourCheckColorSpaceSupport);
    if (!proxy) return E_FAIL;
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, DXGI_COLOR_SPACE_TYPE, UINT*)>(
        proxy->m_detourCheckColorSpaceSupport.GetOriginal());
    HRESULT hr = orig ? orig(self, colorSpace, pColorSpaceSupport) : E_FAIL;
    if (SUCCEEDED(hr) && pColorSpaceSupport && *pColorSpaceSupport != 0 &&
        proxy->IsActive() && IsHdrColorSpace(colorSpace)) {
        *pColorSpaceSupport = 0;
        const uint32_t bit = static_cast<uint32_t>(colorSpace) < 32 ? 1u << colorSpace : 0u;
        if (!(proxy->m_loggedHiddenColorSpaces & bit)) {
            proxy->m_loggedHiddenColorSpaces |= bit;
            LogInfo("DxgiProxy: Reporting HDR color space " + std::to_string(colorSpace) +
                    " as unsupported while VR is active.");
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_SetColorSpace1(IDXGISwapChain3* self, DXGI_COLOR_SPACE_TYPE colorSpace) {
    DxgiProxy* proxy = ForHook(&DxgiProxy::m_detourSetColorSpace1);
    if (!proxy) return E_FAIL;
    if (proxy->m_lastSetColorSpace != colorSpace) {
        proxy->m_lastSetColorSpace = colorSpace;
        const bool hdr = IsHdrColorSpace(colorSpace);
        LogInfo("DxgiProxy: Game set swap chain color space " + std::to_string(colorSpace) +
                (hdr ? " (HDR)." : " (SDR)."));
        if (hdr && proxy->IsActive()) {
            LogWarn("DxgiProxy: The game is outputting HDR while VR is active; the headset image will look washed out. "
                    "Turn off HDR in Windows display settings or in the game.");
        }
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, DXGI_COLOR_SPACE_TYPE)>(
        proxy->m_detourSetColorSpace1.GetOriginal());
    return orig ? orig(self, colorSpace) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_OutputGetDesc1(IDXGIOutput6* self, DXGI_OUTPUT_DESC1* pDesc) {
    DxgiProxy* proxy = ForHook(&DxgiProxy::m_detourOutputGetDesc1);
    if (!proxy) return E_FAIL;
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIOutput6*, DXGI_OUTPUT_DESC1*)>(
        proxy->m_detourOutputGetDesc1.GetOriginal());
    HRESULT hr = orig ? orig(self, pDesc) : E_FAIL;
    if (SUCCEEDED(hr) && pDesc && proxy->IsActive() && IsHdrColorSpace(pDesc->ColorSpace)) {
        if (!proxy->m_loggedHiddenOutputHdr) {
            proxy->m_loggedHiddenOutputHdr = true;
            LogInfo("DxgiProxy: Display reports HDR (color space " + std::to_string(pDesc->ColorSpace) +
                    "); reporting SDR to the game while VR is active.");
        }
        pDesc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }
    return hr;
}

void DxgiProxy::GetDecoupledTargetResolution(uint32_t& outWidth, uint32_t& outHeight) const {
    m_windowController->GetDecoupledTargetResolution(outWidth, outHeight, m_openXrManager.get());
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_CreateSwapChain(
    IDXGIFactory* self, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {
    DxgiProxy* proxy = ForHook(&DxgiProxy::m_detourCreateSwapChain);
    if (!proxy) return E_FAIL;
    if (pDevice) {
        proxy->ResolveD3D12DeviceAndQueue(pDevice);
    }
    if (pDesc && proxy->IsActive()) {
        // Before any override below: an unusable format must leave the swap chain untouched.
        proxy->m_openXrManager->EnsureBackBufferFormatSupported(pDesc->BufferDesc.Format);
    }
    if (pDesc && proxy->IsActive()) {
        if (pDesc->BufferDesc.Width > 0 && pDesc->BufferDesc.Height > 0) {
            proxy->m_windowController->SetTargetRenderResolution(pDesc->BufferDesc.Width, pDesc->BufferDesc.Height);
        }
        pDesc->Windowed = TRUE;
    }
    if (pDesc && pDesc->OutputWindow) {
        proxy->m_windowController->SetGameHwnd(pDesc->OutputWindow);
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**)>(
        proxy->m_detourCreateSwapChain.GetOriginal()
    );
    HRESULT hr = orig ? orig(self, pDevice, pDesc, ppSwapChain) : E_FAIL;
    if (SUCCEEDED(hr)) {
        if (pDesc && pDesc->OutputWindow && proxy->IsActive()) {
            proxy->m_windowController->ApplyInitialWindowClamp(pDesc->OutputWindow);
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
    DxgiProxy* proxy = ForHook(&DxgiProxy::m_detourCreateSwapChainForHwnd);
    if (!proxy) return E_FAIL;
    if (pDevice) {
        proxy->ResolveD3D12DeviceAndQueue(pDevice);
    }
    if (pDesc && proxy->IsActive()) {
        // Before any override below: an unusable format must leave the swap chain untouched.
        proxy->m_openXrManager->EnsureBackBufferFormatSupported(pDesc->Format);
    }
    if (pDesc && proxy->IsActive()) {
        if (pDesc->Width > 0 && pDesc->Height > 0) {
            proxy->m_windowController->SetTargetRenderResolution(pDesc->Width, pDesc->Height);
        }
    }
    if (hWnd) {
        proxy->m_windowController->SetGameHwnd(hWnd);
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
            proxy->m_windowController->ApplyInitialWindowClamp(hWnd);
        }
        if (ppSwapChain && *ppSwapChain) {
            proxy->AttachSwapChain(*ppSwapChain);
        }
    }
    return hr;
}

HRESULT DxgiProxy::CreateDXGIFactory(REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    HRESULT hr = ForwardFactoryCall("CreateDXGIFactory", SystemPfn(&ISystemDxgi::GetCreateDXGIFactory), riid, ppFactory);
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
    HRESULT hr = ForwardFactoryCall("CreateDXGIFactory1", SystemPfn(&ISystemDxgi::GetCreateDXGIFactory1), riid, ppFactory);
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
    HRESULT hr = ForwardFactoryCall("CreateDXGIFactory2", SystemPfn(&ISystemDxgi::GetCreateDXGIFactory2), Flags, riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory && m_openXrManager && m_openXrManager->IsActive()) {
        Microsoft::WRL::ComPtr<IDXGIFactory> factory;
        if (SUCCEEDED(reinterpret_cast<IUnknown*>(*ppFactory)->QueryInterface(IID_PPV_ARGS(&factory)))) {
            AttachFactory(factory.Get());
        }
    }
    return hr;
}

HRESULT DxgiProxy::DXGIDeclareAdapterRemovalSupport() {
    auto pfn = SystemPfn(&ISystemDxgi::GetDXGIDeclareAdapterRemovalSupport);
    return pfn ? pfn() : E_FAIL;
}

HRESULT DxgiProxy::DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    auto pfn = SystemPfn(&ISystemDxgi::GetDXGIGetDebugInterface1);
    return pfn ? pfn(Flags, riid, pDebug) : E_FAIL;
}

} // namespace fs25vr

extern "C" {

HRESULT WINAPI Proxy_CreateDXGIFactory(REFIID riid, void** ppFactory) {
    return fs25vr::InjectorRuntime::Instance().GetDxgiProxy().CreateDXGIFactory(riid, ppFactory);
}

HRESULT WINAPI Proxy_CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    return fs25vr::InjectorRuntime::Instance().GetDxgiProxy().CreateDXGIFactory1(riid, ppFactory);
}

HRESULT WINAPI Proxy_CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    return fs25vr::InjectorRuntime::Instance().GetDxgiProxy().CreateDXGIFactory2(Flags, riid, ppFactory);
}

HRESULT WINAPI Proxy_DXGIDeclareAdapterRemovalSupport() {
    return fs25vr::InjectorRuntime::Instance().GetDxgiProxy().DXGIDeclareAdapterRemovalSupport();
}

HRESULT WINAPI Proxy_DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    return fs25vr::InjectorRuntime::Instance().GetDxgiProxy().DXGIGetDebugInterface1(Flags, riid, pDebug);
}

// The remaining exports forward to the system dxgi.dll and bypass the runtime (see SystemExport).
HRESULT WINAPI Proxy_DXGID3D10CreateDevice(HMODULE hModule, IDXGIAdapter* pAdapter, UINT Flags, DWORD RuntimeVersion, void* pOutputs) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(HMODULE, IDXGIAdapter*, UINT, DWORD, void*)>(
        fs25vr::SystemExport("DXGID3D10CreateDevice"));
    return pfn ? pfn(hModule, pAdapter, Flags, RuntimeVersion, pOutputs) : E_FAIL;
}

HRESULT WINAPI Proxy_DXGID3D10CreateLayeredDevice(void* a, void* b, void* c, void* d, void* e) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(void*, void*, void*, void*, void*)>(
        fs25vr::SystemExport("DXGID3D10CreateLayeredDevice"));
    return pfn ? pfn(a, b, c, d, e) : E_FAIL;
}

SIZE_T WINAPI Proxy_DXGID3D10GetLayeredDeviceSize(const void* pConfiguration, UINT Size) {
    auto pfn = reinterpret_cast<SIZE_T(WINAPI*)(const void*, UINT)>(
        fs25vr::SystemExport("DXGID3D10GetLayeredDeviceSize"));
    return pfn ? pfn(pConfiguration, Size) : 0;
}

HRESULT WINAPI Proxy_DXGID3D10RegisterLayers(const void* pLayers, UINT NumLayers) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(const void*, UINT)>(
        fs25vr::SystemExport("DXGID3D10RegisterLayers"));
    return pfn ? pfn(pLayers, NumLayers) : E_FAIL;
}

HRESULT WINAPI Proxy_DXGIDisableVBlankVirtualization() {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)()>(
        fs25vr::SystemExport("DXGIDisableVBlankVirtualization"));
    return pfn ? pfn() : E_FAIL;
}

HRESULT WINAPI Proxy_DXGIReportAdapterConfiguration(void* pConf) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(void*)>(
        fs25vr::SystemExport("DXGIReportAdapterConfiguration"));
    return pfn ? pfn(pConf) : E_FAIL;
}

void WINAPI Proxy_DXGIDumpJournal(void* pJournal) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*)>(
        fs25vr::SystemExport("DXGIDumpJournal"));
    if (pfn) pfn(pJournal);
}

void WINAPI Proxy_PIXBeginCapture(DWORD captureFlags, const void* captureArgs) {
    auto pfn = reinterpret_cast<void(WINAPI*)(DWORD, const void*)>(
        fs25vr::SystemExport("PIXBeginCapture"));
    if (pfn) pfn(captureFlags, captureArgs);
}

void WINAPI Proxy_PIXEndCapture() {
    auto pfn = reinterpret_cast<void(WINAPI*)()>(
        fs25vr::SystemExport("PIXEndCapture"));
    if (pfn) pfn();
}

DWORD WINAPI Proxy_PIXGetCaptureState() {
    auto pfn = reinterpret_cast<DWORD(WINAPI*)()>(
        fs25vr::SystemExport("PIXGetCaptureState"));
    return pfn ? pfn() : 0;
}

HRESULT WINAPI Proxy_SetAppCompatStringPointer(DWORD unknown, const char* str) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(DWORD, const char*)>(
        fs25vr::SystemExport("SetAppCompatStringPointer"));
    return pfn ? pfn(unknown, str) : E_FAIL;
}

void WINAPI Proxy_UpdateHMDEmulationStatus(BOOL status) {
    auto pfn = reinterpret_cast<void(WINAPI*)(BOOL)>(
        fs25vr::SystemExport("UpdateHMDEmulationStatus"));
    if (pfn) pfn(status);
}

void WINAPI Proxy_ApplyCompatResolutionQuirking(void* a, void* b, void* c, void* d) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*, void*, void*, void*)>(
        fs25vr::SystemExport("ApplyCompatResolutionQuirking"));
    if (pfn) pfn(a, b, c, d);
}

void WINAPI Proxy_CompatString(void* a, void* b, void* c, void* d) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*, void*, void*, void*)>(
        fs25vr::SystemExport("CompatString"));
    if (pfn) pfn(a, b, c, d);
}

void WINAPI Proxy_CompatValue(void* a, void* b, void* c, void* d) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*, void*, void*, void*)>(
        fs25vr::SystemExport("CompatValue"));
    if (pfn) pfn(a, b, c, d);
}

} // extern "C"
