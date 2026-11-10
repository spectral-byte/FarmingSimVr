#include "dxgi_proxy.h"
#include "core/crash_reporter.h"
#include "core/logger.h"
#include "core/thread_trace.h"
#include "core/injector_runtime.h"
#include "core/dll_lifecycle.h"
#include "core/perf_trace.h"
#include "core/texture_snapshot.h"
#include "ngx/ngx_interceptor.h"
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <utility>
#include <vector>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

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

// The proxy whose VR initialization is running on this thread. A runtime that creates a DXGI factory during setup
// (VDXR's xrGetSystem does) does it on this thread. In the game, DxgiExportHooks passes such a call straight to
// the system; a caller that reaches the proxy itself (tests do) calls back into EnsureVrInitialized.
thread_local const DxgiProxy* t_initializingProxy = nullptr;

std::string ModuleNameAt(const void* address) {
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH * 2] = {};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module) ||
        GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))) == 0) {
        return "an unknown module";
    }
    const wchar_t* name = std::wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    char narrow[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, static_cast<int>(sizeof(narrow)), nullptr, nullptr);
    return narrow;
}

// "module+0xoffset" for each caller on this thread's stack, innermost first, skipping `skip` frames.
std::string DescribeCallers(unsigned long skip, unsigned long maxFrames = 32) {
    void* frames[32] = {};
    const USHORT count = RtlCaptureStackBackTrace(
        skip + 1, static_cast<DWORD>((std::min)(maxFrames, static_cast<unsigned long>(std::size(frames)))), frames, nullptr);
    std::ostringstream ss;
    for (USHORT i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        if (i) ss << " <- ";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCWSTR>(frames[i]), &module)) {
            ss << ModuleNameAt(frames[i]) << "+0x" << std::hex
               << (reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(module)) << std::dec;
        } else {
            ss << frames[i];
        }
    }
    return ss.str();
}

// The proxy whose D3D11On12CreateDevice hook is installed.
DxgiProxy* g_d3d11On12Owner = nullptr;
using PFN_D3D11On12CreateDevice = HRESULT(WINAPI*)(IUnknown*, UINT, const D3D_FEATURE_LEVEL*, UINT, IUnknown* const*,
                                                   UINT, UINT, void**, void**, D3D_FEATURE_LEVEL*);

HMODULE ModuleOf(const void* address) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(address), &module);
    return module;
}

// The module that implements a COM object, from its vtable.
HMODULE ImplementingModule(const void* object) {
    return object ? ModuleOf(*reinterpret_cast<void* const*>(object)) : nullptr;
}

std::string DescribeObject(const void* object) {
    if (!object) return "null";
    std::ostringstream ss;
    ss << object << " (" << ModuleNameAt(*reinterpret_cast<void* const*>(object)) << ")";
    return ss.str();
}

bool SameAdapter(ID3D12Device* a, ID3D12Device* b) {
    const LUID la = a->GetAdapterLuid();
    const LUID lb = b->GetAdapterLuid();
    return la.LowPart == lb.LowPart && la.HighPart == lb.HighPart;
}

// The system's factory behind `factory` when `factory` is a wrapper whose methods are not the system's, as a
// dxgi.dll in the game folder (ReShade's) hands out; null when `factory` is the system's own or none is found.
// An adapter is the system's even when the factory is wrapped, and its parent is the system's factory.
Microsoft::WRL::ComPtr<IDXGIFactory> SystemFactoryBehind(IDXGIFactory* factory) {
    void** vtable = *reinterpret_cast<void***>(factory);
    if (!vtable || IsInSystemDxgi(vtable[kCreateSwapChainVTableSlot])) return nullptr;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory1;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory1))) || !factory1) return nullptr;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory1->EnumAdapters1(0, &adapter)) || !adapter) return nullptr;
    Microsoft::WRL::ComPtr<IDXGIFactory> parent;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&parent))) || !parent) return nullptr;
    void** parentVtable = *reinterpret_cast<void***>(parent.Get());
    if (!parentVtable || !IsInSystemDxgi(parentVtable[kCreateSwapChainVTableSlot])) return nullptr;
    return parent;
}

struct VrInitScope {
    explicit VrInitScope(const DxgiProxy* proxy) { t_initializingProxy = proxy; }
    ~VrInitScope() { t_initializingProxy = nullptr; }
    VrInitScope(const VrInitScope&) = delete;
    VrInitScope& operator=(const VrInitScope&) = delete;
};

// The swapchain fields shared by both descriptor structs, in one log-friendly string. Flags are hex with
// the bits worth reading off (tearing, frame-latency waitable object) named, so a log shows at a glance
// whether the game's swapchain can present without vsync or paces itself on a waitable handle.
std::string FormatSwapChainFields(UINT width, UINT height, DXGI_FORMAT format, UINT bufferCount,
                                  DXGI_SWAP_EFFECT swapEffect, UINT flags, UINT sampleCount) {
    const char* effect = "UNKNOWN";
    switch (swapEffect) {
        case DXGI_SWAP_EFFECT_DISCARD: effect = "DISCARD"; break;
        case DXGI_SWAP_EFFECT_SEQUENTIAL: effect = "SEQUENTIAL"; break;
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: effect = "FLIP_SEQUENTIAL"; break;
        case DXGI_SWAP_EFFECT_FLIP_DISCARD: effect = "FLIP_DISCARD"; break;
        default: break;
    }
    char hexFlags[16];
    std::snprintf(hexFlags, sizeof(hexFlags), "0x%X", flags);
    std::string flagNames;
    const auto addFlag = [&](UINT bit, const char* name) {
        if (!(flags & bit)) return;
        if (!flagNames.empty()) flagNames += "|";
        flagNames += name;
    };
    addFlag(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING, "ALLOW_TEARING");
    addFlag(DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT, "FRAME_LATENCY_WAITABLE_OBJECT");

    std::string text = std::to_string(width) + "x" + std::to_string(height) +
        " format=" + std::to_string(static_cast<int>(format)) +
        " buffers=" + std::to_string(bufferCount) +
        " swapEffect=" + effect + "(" + std::to_string(static_cast<int>(swapEffect)) + ")" +
        " flags=" + hexFlags;
    if (!flagNames.empty()) text += " (" + flagNames + ")";
    text += " samples=" + std::to_string(sampleCount);
    return text;
}

std::string FormatSwapChainDesc(const DXGI_SWAP_CHAIN_DESC& desc) {
    return FormatSwapChainFields(desc.BufferDesc.Width, desc.BufferDesc.Height, desc.BufferDesc.Format,
                                 desc.BufferCount, desc.SwapEffect, desc.Flags, desc.SampleDesc.Count) +
        " modeScaling=" + std::to_string(static_cast<int>(desc.BufferDesc.Scaling)) +
        " windowed=" + (desc.Windowed ? "1" : "0");
}

std::string FormatSwapChainDesc1(const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc) {
    const char* scaling = "UNKNOWN";
    switch (desc->Scaling) {
        case DXGI_SCALING_STRETCH: scaling = "STRETCH"; break;
        case DXGI_SCALING_NONE: scaling = "NONE"; break;
        case DXGI_SCALING_ASPECT_RATIO_STRETCH: scaling = "ASPECT_RATIO_STRETCH"; break;
        default: break;
    }
    // No fullscreen descriptor means the swapchain is windowed.
    const bool windowed = !fullscreenDesc || fullscreenDesc->Windowed;
    return FormatSwapChainFields(desc->Width, desc->Height, desc->Format, desc->BufferCount, desc->SwapEffect,
                                 desc->Flags, desc->SampleDesc.Count) +
        " scaling=" + scaling + "(" + std::to_string(static_cast<int>(desc->Scaling)) + ")" +
        " alphaMode=" + std::to_string(static_cast<int>(desc->AlphaMode)) +
        " windowed=" + (windowed ? "1" : "0");
}

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
    if (device && queue) {
        // A device or queue whose vtable is outside D3D12Core.dll is a wrapper (ReShade's are in its dxgi.dll).
        std::ostringstream ss;
        ss << "DxgiProxy: D3D12 device " << static_cast<const void*>(device) << " (vtable in "
           << ModuleNameAt(*reinterpret_cast<void* const*>(device)) << "), queue " << static_cast<const void*>(queue)
           << " (vtable in " << ModuleNameAt(*reinterpret_cast<void* const*>(queue)) << ").";
        LogInfo(ss.str());
        // SteamVR draws quad layers through a D3D11On12 device it creates during Present. With ReShade it passes
        // ReShade's wrapped device and queue, which ReShade passes on unwrapped while inside Present, and D3D11On12 on
        // wrapped D3D12 objects gets the device removed (DXGI_ERROR_INVALID_CALL). The hook hands it the real ones.
        if (m_ownedByRuntime && !m_detourD3D11On12CreateDevice.IsInstalled()) {
            HMODULE d3d11 = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            void* target = d3d11 ? reinterpret_cast<void*>(GetProcAddress(d3d11, "D3D11On12CreateDevice")) : nullptr;
            if (target) {
                g_d3d11On12Owner = this;
                if (!m_detourD3D11On12CreateDevice.Install(
                        target, reinterpret_cast<void*>(&DxgiProxy::Intercept_D3D11On12CreateDevice))) {
                    g_d3d11On12Owner = nullptr;
                    LogWarn("DxgiProxy: Could not hook D3D11On12CreateDevice; quad layers may fail under a D3D12 wrapper.");
                }
            }
        }
    }
    if (m_openXrManager && device && queue) {
        m_openXrManager->SetD3D12DeviceAndQueue(device, queue);
    }
}

std::string DxgiProxy::UnwrapForD3D11On12(IUnknown*& device, IUnknown** queues, UINT numQueues) {
    std::ostringstream ss;
    ss << "device " << DescribeObject(device);
    ID3D12Device* realDevice = m_d3d12Device.Get();
    // A wrapper's methods live in another module than the real object's (ReShade's in its dxgi.dll).
    const HMODULE realModule = ImplementingModule(realDevice);
    Microsoft::WRL::ComPtr<ID3D12Device> passedDevice;
    if (realDevice && device && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&passedDevice))) &&
        passedDevice.Get() != realDevice && ImplementingModule(passedDevice.Get()) != realModule &&
        SameAdapter(passedDevice.Get(), realDevice)) {
        // D3D12 keeps one device per adapter, so a device wrapper on the game's adapter wraps the game's device.
        device = realDevice;
        ss << " -> real " << DescribeObject(realDevice);
    }
    ID3D12CommandQueue* realQueue = m_d3d12Queue.Get();
    for (UINT i = 0; i < numQueues; ++i) {
        ss << ", queue " << DescribeObject(queues[i]);
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> passedQueue;
        Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
        if (realQueue && realDevice && queues[i] && SUCCEEDED(queues[i]->QueryInterface(IID_PPV_ARGS(&passedQueue))) &&
            passedQueue.Get() != realQueue && ImplementingModule(passedQueue.Get()) != ImplementingModule(realQueue) &&
            passedQueue->GetDesc().Type == realQueue->GetDesc().Type &&
            SUCCEEDED(passedQueue->GetDevice(IID_PPV_ARGS(&queueDevice))) && SameAdapter(queueDevice.Get(), realDevice)) {
            // The game makes one queue of each type, so a wrapper of the swap chain queue's type wraps that queue.
            queues[i] = realQueue;
            ss << " -> real " << DescribeObject(realQueue);
        }
    }
    return ss.str();
}

HRESULT WINAPI DxgiProxy::Intercept_D3D11On12CreateDevice(IUnknown* pDevice, UINT flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT featureLevels, IUnknown* const* ppCommandQueues, UINT numQueues,
    UINT nodeMask, void** ppDevice, void** ppImmediateContext, D3D_FEATURE_LEVEL* pChosenFeatureLevel) {
    DxgiProxy* proxy = g_d3d11On12Owner;
    auto original = proxy ? reinterpret_cast<PFN_D3D11On12CreateDevice>(proxy->m_detourD3D11On12CreateDevice.GetOriginal())
                          : nullptr;
    if (!original) return E_FAIL;
    std::vector<IUnknown*> queues;
    if (ppCommandQueues && numQueues) queues.assign(ppCommandQueues, ppCommandQueues + numQueues);
    IUnknown* device = pDevice;
    const std::string passed = proxy->UnwrapForD3D11On12(device, queues.data(), static_cast<UINT>(queues.size()));
    LogInfo("DxgiProxy: D3D11On12CreateDevice with " + passed + "; called from " + DescribeCallers(0, 4) + ".");
    const HRESULT hr = original(device, flags, pFeatureLevels, featureLevels, queues.empty() ? ppCommandQueues : queues.data(),
                                numQueues, nodeMask, ppDevice, ppImmediateContext, pChosenFeatureLevel);
    if (FAILED(hr)) {
        std::ostringstream ss;
        ss << "DxgiProxy: D3D11On12CreateDevice failed with 0x" << std::hex << static_cast<unsigned long>(hr) << ".";
        LogWarn(ss.str());
    }
    return hr;
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
    // The game may have installed a crash filter of its own since the first factory call (a no-op unless the Injector
    // installed its crash reporter).
    ReinstallCrashReporter();
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
    // Recorded after the inactive check on purpose: Pass-Through is final. Initialize runs once, before the game has a
    // swap chain, and an Active session that is lost only ever falls back to Pass-Through; nothing makes the manager
    // Active again. So a swap chain created while inactive can never become the one to act on, and leaving it
    // unrecorded keeps every hook passing straight through, which is what Pass-Through means. (A replaced swap chain
    // stays recorded until the next attach, and a new one at a recycled address would be taken for it: both narrow
    // and harmless, as the old chain was the game's.)
    m_gameSwapChain = swapChain;
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

    // Hooking a wrapper's methods would put the swap chain hooks outside it: the eye copy would run before what the
    // wrapper draws on Present (ReShade's effects), and the swap chain and queue seen would be its wrappers.
    // The system's factory inside it is hooked instead, so whatever wraps DXGI runs first.
    Microsoft::WRL::ComPtr<IDXGIFactory> systemFactory = SystemFactoryBehind(factory);
    if (systemFactory) {
        LogInfo("DxgiProxy: The DXGI factory is a wrapper from " +
                ModuleNameAt((*reinterpret_cast<void***>(factory))[kCreateSwapChainVTableSlot]) +
                "; hooking the system's factory behind it.");
        factory = systemFactory.Get();
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
    if (m_detourD3D11On12CreateDevice.IsInstalled()) m_detourD3D11On12CreateDevice.Uninstall();
    if (g_d3d11On12Owner == this) g_d3d11On12Owner = nullptr;
    m_swapChainAttached = false;
    m_factoryAttached = false;
    m_outputAttached = false;
    m_lastSetColorSpace.reset();
    m_loggedHiddenColorSpaces = 0;
    m_loggedHiddenOutputHdr = false;
    m_gameSwapChain = nullptr;
    ReleaseCachedBackBuffers();
    m_lastPresentSuppressed = false;
    m_desktopMirrorOffAnnounced = false;
    m_desktopMirrorWasShowing = false;
    if (m_vrBridge) {
        m_vrBridge->ClearPendingRenderingMode();
    }
}

void DxgiProxy::ReleaseCachedBackBuffers() {
    m_cachedSwapChain = nullptr;
    m_cachedSwapChain3.Reset();
    m_cachedBackBuffers.clear();
}

bool DxgiProxy::IsForGameWindow(HWND window) const {
    // The game's window is first learnt from the first swap chain created through the hooked factory (the creation
    // hooks, AttachSwapChain and the per-frame mouse update, which sees only the game's Present, are the only places
    // that set it), so until then the first creation is taken as the game's, as before #264. An overlay creating its
    // swap chain ahead of the game's would be taken for it: there is no earlier, sound source to tell them apart.
    const HWND gameWindow = m_windowController->GetGameHwnd();
    if (!window || !gameWindow) return true;
    const HWND root = GetAncestor(window, GA_ROOT);
    return (root ? root : window) == gameWindow;
}

bool DxgiProxy::PrepareGameSwapChainCreation(const char* hookName, HWND window) {
    if (!IsForGameWindow(window)) {
        // Not the game's window: its swap chain is never the game's, so it gets none of the game's treatment.
        LogInfo(std::string("DxgiProxy: ") + hookName + " for a window other than the game's; passed on as requested.");
        return false;
    }
    // The game's window gets a new swap chain: nothing of the old one may outlive the creation.
    ReleaseCachedBackBuffers();
    return true;
}

void DxgiProxy::LatchPendingRenderingMode() {
    if (m_vrBridge) {
        m_vrBridge->LatchPendingRenderingMode();
    }
}

bool DxgiProxy::OnPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    m_presentWasStereo = false;
    m_mouseConfinementUpdated = false;
    bool shouldPresent;
    {
        PerfScope timing(PerfSection::InjectorPresent);
        shouldPresent = OnPresentImpl(swapChain, syncInterval, flags);
        UpdateDesktopMirrorTitle();
        if (!m_mouseConfinementUpdated) UpdateMouseConfinement();
    }
    PerfTrace::Instance().MarkFrame(m_presentWasStereo,
                                    m_vrBridge && m_vrBridge->GetRenderingMode() == VRRenderingMode::SequentialDualPass);
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
        m_desktopMirrorWasShowing = true;
        return true;
    }

    // Poll runtime events before deciding how to present; this may begin/end the session or
    // drop the Injector into Pass-Through Mode (EXITING, LOSS_PENDING, instance loss).
    m_openXrManager->PollEvents();
    if (!m_openXrManager->IsActive()) {
        // Dropped to Pass-Through after a mode was queued; the released eye pair is abandoned.
        LatchPendingRenderingMode();
        m_lastPresentSuppressed = false;
        m_desktopMirrorWasShowing = true;
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
        m_desktopMirrorWasShowing = true;  // the mirror shows by itself: a resume with it off re-triggers the black frame
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
            // Mouse Confinement first: when it starts holding it brings the Desk Mouse to the middle of
            // the picture, and this frame's drawn cursor reads where the Desk Mouse is.
            UpdateMouseConfinement();
            m_windowController->UpdateMenuCursorState(swapChain, m_openXrManager.get());

            // Floating quad layer mode:
            // Backbuffer is presented on an OpenXR Quad layer positioned in 3D tracking space
            // rather than warping across an immersive projection layer.
            // Active during pre-game / Main Menu, in-game GUI menus (ESC pause menu, shop, etc.),
            // or in 2D Floating Window Mode.
            m_openXrManager->SubmitMenuQuad();
            // The quad frame is a completed compositor frame and no eye pair is in flight.
            LatchPendingRenderingMode();
            return ShouldCallRealPresent(pBackBuffer);
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
                    SnapshotEyeIfRequested(0, pBackBuffer);
                    if (m_vrBridge) {
                        m_vrBridge->SetSubPassIndex(1);
                    }
                    m_lastPresentSuppressed = true;
                    return false;
                } else {
                    // Sub-Pass 1: copies the right eye, submits the pair, executes the physical present, resets subPassIndex to 0
                    m_openXrManager->CopyBackBufferToEye(1, pBackBuffer);
                    SnapshotEyeIfRequested(1, pBackBuffer);
                    m_openXrManager->SubmitFramePair();
                    if (m_vrBridge) {
                        m_vrBridge->AdvanceFrame();
                    }
                    LatchPendingRenderingMode();
                    return ShouldCallRealPresent(pBackBuffer);
                }
            } else {
                // Alternate Frame Rendering (AFR) pacing pipeline: alternate sequential frames
                uint32_t parity = m_vrBridge ? m_vrBridge->GetFrameParity() : 0;
                m_openXrManager->CopyBackBufferToEye(parity, pBackBuffer);
                SnapshotEyeIfRequested(parity, pBackBuffer);

                if (parity == 1) {
                    m_openXrManager->SubmitFramePair();
                }

                if (m_vrBridge) {
                    m_vrBridge->AdvanceFrame();
                }

                if (parity == 1) {
                    LatchPendingRenderingMode();
                }
                return ShouldCallRealPresent(pBackBuffer);
            }
        }
    }
    // No eye swapchains (creation failed or Pass-Through): no eye pair can be in flight.
    LatchPendingRenderingMode();
    m_lastPresentSuppressed = false;
    m_desktopMirrorWasShowing = true;
    return true;
}

void DxgiProxy::SnapshotEyeIfRequested(uint32_t eyeIndex, ID3D12Resource* backBuffer) {
    if (!m_vrBridge || !m_vrBridge->ConsumeEyeSnapshotRequest(eyeIndex)) return;
    const std::string name = "fs25_vr_eye" + std::to_string(eyeIndex) + ".bmp";
    const std::filesystem::path file = Logger::DefaultLogPath().parent_path() / name;
    std::ostringstream ss;
    ss << "[DxgiProxy] Eye snapshot: eye " << eyeIndex << " ("
       << (m_vrBridge->IsSequentialDualPass() ? "Sequential Dual-Pass sub-pass " : "AFR parity ")
       << (m_vrBridge->IsSequentialDualPass() ? m_vrBridge->GetSubPassIndex() : m_vrBridge->GetFrameParity())
       << ", frame " << m_vrBridge->GetFrameIndex() << ") ";
    const std::string error = SnapshotTextureToBmp(m_d3d12Device.Get(), m_d3d12Queue.Get(), backBuffer,
                                                   D3D12_RESOURCE_STATE_PRESENT, file, 2);
    if (error.empty()) {
        ss << "written to " << name;
        LogInfo(ss.str());
    } else {
        ss << "not written: " << error;
        LogError(ss.str());
    }
}

bool DxgiProxy::IsDesktopMirrorOffNow() const {
    return m_vrBridge && m_vrBridge->IsCompanionActive() && !m_vrBridge->IsDesktopMirrorEnabled() &&
           m_openXrManager && m_openXrManager->IsActive() && m_openXrManager->IsSessionRunning() &&
           m_openXrManager->IsSessionFocused() && m_openXrManager->HasEyeSwapchains();
}

void DxgiProxy::UpdateDesktopMirrorTitle() {
    const bool off = IsDesktopMirrorOffNow();
    if (off) {
        // The transition is logged once. The title is applied whenever it is not shown, because it can be
        // missing while the mirror stays off: the game window may be unknown yet, or may have changed.
        if (!m_desktopMirrorOffAnnounced) {
            m_desktopMirrorOffAnnounced = true;
            LogInfo("DxgiProxy: Desktop mirror off; window title says so until it shows again.");
        }
        if (m_windowController && !m_windowController->IsDesktopMirrorOffTitleShown()) {
            m_windowController->ShowDesktopMirrorOffTitle();
        }
        return;
    }
    if (!m_desktopMirrorOffAnnounced) return;
    m_desktopMirrorOffAnnounced = false;

    // The handshake and Pass-Through checks come first: a savegame unload also resets the flag to on.
    const char* reason = "by command (vrDesktopMirror on)";
    if (!m_openXrManager || !m_openXrManager->IsActive()) {
        reason = "by itself (Pass-Through Mode)";
    } else if (!m_vrBridge || !m_vrBridge->IsCompanionActive()) {
        reason = "by itself (Companion Mod handshake lost)";
    } else if (!m_openXrManager->HasEyeSwapchains() || !m_openXrManager->IsSessionRunning()) {
        reason = "by itself (session not running)";
    } else if (!m_openXrManager->IsSessionFocused()) {
        reason = "by itself (headset not in use)";
    }
    LogInfo(std::string("DxgiProxy: Desktop mirror on ") + reason + "; window title restored.");
    if (m_windowController) m_windowController->RestoreTitle();
}

void DxgiProxy::UpdateMouseConfinement() {
    if (!m_windowController) return;
    using State = MirrorWindowController::MouseConfinementState;
    // The Injector is inert in Pass-Through Mode; "session running" is the one that decides whether the
    // Desktop Mirror Window shows by itself (a running session that is not focused still counts).
    // The vrMouseConfinement setting only counts once a savegame has loaded (the Companion Mod's handshake):
    // before that, in the main menu, it always holds. It is tested before the focus so that a setting turned
    // off while the game window is not in front is the reason that is logged when it is turned on again.
    State state = State::Holding;
    if (!m_openXrManager || !m_openXrManager->IsActive()) {
        state = State::PassThroughMode;
    } else if (!m_openXrManager->HasEyeSwapchains() || !m_openXrManager->IsSessionRunning()) {
        state = State::SessionNotRunning;
    } else if (m_vrBridge && m_vrBridge->IsCompanionActive() && !m_vrBridge->IsMouseConfinementEnabled()) {
        state = State::TurnedOffByCommand;
    } else if (!m_windowController->IsGameWindowForeground()) {
        state = State::FocusLost;
    }
    m_mouseConfinementUpdated = true;
    m_windowController->UpdateMouseConfinement(state);
}

bool DxgiProxy::ShouldCallRealPresent(ID3D12Resource* backBuffer) {
    if (!IsDesktopMirrorOffNow()) {
        m_desktopMirrorWasShowing = true;
        m_lastPresentSuppressed = false;
        return true;
    }
    if (ConsumeSwitchToOff(backBuffer)) {
        m_lastPresentSuppressed = false;
        return true;
    }
    m_lastPresentSuppressed = true;
    return false;
}

bool DxgiProxy::ConsumeSwitchToOff(ID3D12Resource* backBuffer) {
    if (!m_desktopMirrorWasShowing) return false;
    m_desktopMirrorWasShowing = false;
    // Called after this frame's eye copy, so the headset has the game's frame; only the Desktop
    // Mirror Window gets the black one. A null back buffer (unit tests) leaves nothing to clear.
    if (m_openXrManager) {
        m_openXrManager->ClearBackBufferToBlack(backBuffer);
    }
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
    if (!proxy->IsGameSwapChain(self)) {
        // Another swap chain in the process (an overlay, a capture tool): DXGI sees the call as its caller made it.
        auto passThrough = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT)>(
            proxy->m_detourPresent.GetOriginal());
        return passThrough ? passThrough(self, syncInterval, flags) : S_OK;
    }
    bool shouldPresent = proxy->OnPresent(self, syncInterval, flags);
    if (!shouldPresent) {
        proxy->OnPresentReturned();
        return S_OK;
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT)>(
        proxy->m_detourPresent.GetOriginal()
    );
    HRESULT hr = S_OK;
    if (orig) {
        PerfScope timing(PerfSection::DriverPresent);
        hr = orig(self, proxy->EffectiveSyncInterval(syncInterval), flags);
    }
    proxy->OnPresentReturned(hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_Present1(IDXGISwapChain1* self, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* params) {
    DxgiProxy* proxy = ForHook(&DxgiProxy::m_detourPresent1);
    if (!proxy) return S_OK;
    if (!proxy->IsGameSwapChain(self)) {
        auto passThrough = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*)>(
            proxy->m_detourPresent1.GetOriginal());
        return passThrough ? passThrough(self, syncInterval, flags, params) : S_OK;
    }
    bool shouldPresent = proxy->OnPresent1(self, syncInterval, flags, params);
    if (!shouldPresent) {
        proxy->OnPresentReturned();
        return S_OK;
    }
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*)>(
        proxy->m_detourPresent1.GetOriginal()
    );
    HRESULT hr = S_OK;
    if (orig) {
        PerfScope timing(PerfSection::DriverPresent);
        hr = orig(self, proxy->EffectiveSyncInterval(syncInterval), flags, params);
    }
    proxy->OnPresentReturned(hr);
    return hr;
}

void DxgiProxy::PrepareResize(DxgiProxy* proxy, const char* hookName, UINT& width, UINT& height) {
    if (!proxy) return;
    proxy->ReleaseCachedBackBuffers();
    proxy->m_windowController->SetWindowClampDirty(true);
    if (proxy->IsActive() && proxy->m_windowController->GetTargetRenderWidth() > 0 && proxy->m_windowController->GetTargetRenderHeight() > 0) {
        const UINT requestedWidth = width;
        const UINT requestedHeight = height;
        width = proxy->m_windowController->GetTargetRenderWidth();
        height = proxy->m_windowController->GetTargetRenderHeight();
        LogInfo(std::string("DxgiProxy::") + hookName + ": Overriding dimensions from (" +
                std::to_string(requestedWidth) + "x" + std::to_string(requestedHeight) + ") to target render resolution (" +
                std::to_string(width) + "x" + std::to_string(height) + ")");
    }
}

HRESULT STDMETHODCALLTYPE DxgiProxy::Intercept_ResizeBuffers(
    IDXGISwapChain* self, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    DxgiProxy* proxy = ForHook(&DxgiProxy::m_detourResizeBuffers);
    UINT effectiveWidth = Width;
    UINT effectiveHeight = Height;
    // Another swap chain keeps the size it asked for and leaves the game's cached back buffers alone.
    if (proxy && proxy->IsGameSwapChain(self)) {
        PrepareResize(proxy, "Intercept_ResizeBuffers", effectiveWidth, effectiveHeight);
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
    UINT effectiveWidth = Width;
    UINT effectiveHeight = Height;
    if (proxy && proxy->IsGameSwapChain(self)) {
        PrepareResize(proxy, "Intercept_ResizeBuffers1", effectiveWidth, effectiveHeight);
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
    if (SUCCEEDED(hr) && pColorSpaceSupport && *pColorSpaceSupport != 0 && proxy->IsGameSwapChain(self) &&
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
    if (proxy->IsGameSwapChain(self) && proxy->m_lastSetColorSpace != colorSpace) {
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
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**)>(
        proxy->m_detourCreateSwapChain.GetOriginal()
    );
    if (!proxy->PrepareGameSwapChainCreation("CreateSwapChain", pDesc ? pDesc->OutputWindow : nullptr)) {
        return orig ? orig(self, pDevice, pDesc, ppSwapChain) : E_FAIL;
    }
    if (pDevice) {
        proxy->ResolveD3D12DeviceAndQueue(pDevice);
    }
    if (pDesc) {
        LogInfo("DxgiProxy: CreateSwapChain requested: " + FormatSwapChainDesc(*pDesc));
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
    if (pDesc) {
        LogInfo("DxgiProxy: CreateSwapChain passed on: " + FormatSwapChainDesc(*pDesc));
    }
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
    auto orig = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**)>(
        proxy->m_detourCreateSwapChainForHwnd.GetOriginal()
    );
    if (!proxy->PrepareGameSwapChainCreation("CreateSwapChainForHwnd", hWnd)) {
        return orig ? orig(self, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain) : E_FAIL;
    }
    if (pDevice) {
        proxy->ResolveD3D12DeviceAndQueue(pDevice);
    }
    if (pDesc) {
        // Before the overrides below, which also write through to the caller's descriptors.
        LogInfo("DxgiProxy: CreateSwapChainForHwnd requested: " + FormatSwapChainDesc1(pDesc, pFullscreenDesc));
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

    if (pEffectiveDesc) {
        LogInfo("DxgiProxy: CreateSwapChainForHwnd passed on: " +
                FormatSwapChainDesc1(pEffectiveDesc, pEffectiveFullscreenDesc));
    }
    HRESULT hr = orig ? orig(self, pDevice, hWnd, pEffectiveDesc, pEffectiveFullscreenDesc, pRestrictToOutput, ppSwapChain) : E_FAIL;
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

void DxgiProxy::AttachCreatedFactory(HRESULT hr, void** ppFactory) {
    if (!SUCCEEDED(hr) || !ppFactory || !*ppFactory || !m_openXrManager || !m_openXrManager->IsActive()) return;
    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    if (SUCCEEDED(reinterpret_cast<IUnknown*>(*ppFactory)->QueryInterface(IID_PPV_ARGS(&factory)))) {
        AttachFactory(factory.Get());
    }
}

HRESULT DxgiProxy::CreateDXGIFactory(REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    HRESULT hr = ForwardFactoryCall("CreateDXGIFactory", SystemPfn(&ISystemDxgi::GetCreateDXGIFactory), riid, ppFactory);
    AttachCreatedFactory(hr, ppFactory);
    return hr;
}

HRESULT DxgiProxy::CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    HRESULT hr = ForwardFactoryCall("CreateDXGIFactory1", SystemPfn(&ISystemDxgi::GetCreateDXGIFactory1), riid, ppFactory);
    AttachCreatedFactory(hr, ppFactory);
    return hr;
}

HRESULT DxgiProxy::CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    HRESULT hr = ForwardFactoryCall("CreateDXGIFactory2", SystemPfn(&ISystemDxgi::GetCreateDXGIFactory2), Flags, riid, ppFactory);
    AttachCreatedFactory(hr, ppFactory);
    return hr;
}

} // namespace fs25vr
