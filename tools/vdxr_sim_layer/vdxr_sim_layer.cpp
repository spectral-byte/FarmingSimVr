// Test-only OpenXR API layer that imitates VDXR's xrGetSystem: it creates a DXGI factory through
// its static dxgi.dll import before passing the call on to the real runtime. Loaded into the game,
// that import binds to the FS25 VR dxgi.dll proxy, the same path that deadlocked under VDXR.
#define XR_NO_PROTOTYPES
#include <windows.h>
#include <dxgi.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <cstdio>
#include <cstring>
#include <share.h>

namespace {

PFN_xrGetInstanceProcAddr g_nextGetInstanceProcAddr = nullptr;
PFN_xrGetSystem g_nextGetSystem = nullptr;

// The exe name of this process, so lines from the game and other OpenXR apps can be told apart.
const wchar_t* ProcessName() {
    static wchar_t name[MAX_PATH] = {};
    if (!name[0]) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        const wchar_t* slash = wcsrchr(path, L'\\');
        wcscpy_s(name, slash ? slash + 1 : path);
    }
    return name;
}

// Each line is flushed, so a hang inside CreateDXGIFactory1 still shows the "calling" line.
// The log is appended to and shared, so every process that loads the layer keeps its lines.
void Log(const char* format, ...) {
    static FILE* file = [] {
        wchar_t path[MAX_PATH] = {};
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Log), &self);
        GetModuleFileNameW(self, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"vdxr_sim_layer.log");
        return _wfsopen(path, L"a", _SH_DENYNO);
    }();
    if (!file) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(file, "%04d-%02d-%02d %02d:%02d:%02d.%03d [%ls pid %lu tid %lu] ", t.wYear, t.wMonth, t.wDay,
            t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, ProcessName(), GetCurrentProcessId(),
            GetCurrentThreadId());
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fflush(file);
}

XrResult XRAPI_CALL Layer_xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) {
    wchar_t dxgiPath[MAX_PATH] = L"(not loaded)";
    if (HMODULE dxgi = GetModuleHandleW(L"dxgi.dll")) {
        GetModuleFileNameW(dxgi, dxgiPath, MAX_PATH);
    }
    Log("xrGetSystem: dxgi.dll in this process is %ls", dxgiPath);
    Log("xrGetSystem: calling CreateDXGIFactory1 (as VDXR's ensureOVRSession does)...");

    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    Log("xrGetSystem: CreateDXGIFactory1 returned 0x%08lX, factory %p", static_cast<unsigned long>(hr), factory);
    if (factory) factory->Release();

    XrResult result = g_nextGetSystem ? g_nextGetSystem(instance, getInfo, systemId) : XR_ERROR_FUNCTION_UNSUPPORTED;
    Log("xrGetSystem: runtime returned %d", static_cast<int>(result));
    return result;
}

XrResult XRAPI_CALL Layer_xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    if (name && strcmp(name, "xrGetSystem") == 0) {
        *function = reinterpret_cast<PFN_xrVoidFunction>(&Layer_xrGetSystem);
        return XR_SUCCESS;
    }
    if (!g_nextGetInstanceProcAddr) return XR_ERROR_HANDLE_INVALID;
    return g_nextGetInstanceProcAddr(instance, name, function);
}

XrResult XRAPI_CALL Layer_xrCreateApiLayerInstance(const XrInstanceCreateInfo* info,
                                                   const XrApiLayerCreateInfo* layerInfo, XrInstance* instance) {
    if (!layerInfo || !layerInfo->nextInfo) return XR_ERROR_INITIALIZATION_FAILED;
    g_nextGetInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;

    XrApiLayerCreateInfo nextLayerInfo = *layerInfo;
    nextLayerInfo.nextInfo = layerInfo->nextInfo->next;
    XrResult result = layerInfo->nextInfo->nextCreateApiLayerInstance(info, &nextLayerInfo, instance);
    Log("xrCreateInstance: next returned %d", static_cast<int>(result));
    if (XR_SUCCEEDED(result)) {
        g_nextGetInstanceProcAddr(*instance, "xrGetSystem", reinterpret_cast<PFN_xrVoidFunction*>(&g_nextGetSystem));
    }
    return result;
}

} // anonymous namespace

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo, const char* /*layerName*/, XrNegotiateApiLayerRequest* request) {
    if (!loaderInfo || !request || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr = &Layer_xrGetInstanceProcAddr;
    request->createApiLayerInstance = &Layer_xrCreateApiLayerInstance;
    Log("Layer negotiated with the OpenXR loader.");
    return XR_SUCCESS;
}
