// Stand-in for NVIDIA's nvngx.dll used by tests. Exports just enough NGX entry points
// for NgxInterceptor::AttachModule to hook.
#include <windows.h>

namespace {
volatile long g_calls = 0;
}

extern "C" {

__declspec(dllexport) int NVSDK_NGX_D3D12_CreateFeature(void*, int, void*, void** outHandle) {
    g_calls = g_calls + 1;
    if (outHandle) *outHandle = nullptr;
    return 1;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_ReleaseFeature(void* handle) {
    g_calls = g_calls + 2;
    return handle ? 1 : 0;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_EvaluateFeature(void*, const void*, const void*, void*) {
    g_calls = g_calls + 3;
    return 1;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_AllocateParameters(void** out) {
    g_calls = g_calls + 4;
    if (out) *out = nullptr;
    return 1;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_DestroyParameters(void* p) {
    g_calls = g_calls + 5;
    return p ? 1 : 0;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_GetCapabilityParameters(void** out) {
    g_calls = g_calls + 6;
    if (out) *out = nullptr;
    return 1;
}

}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
