#include "core/injector_startup.h"

#include "core/injector_runtime.h"
#include "core/logger.h"
#include "proxy/dxgi_export_hooks.h"
#include "proxy/dxgi_proxy.h"
#include "proxy/system_dxgi.h"

#include <winternl.h>
#include <cstring>
#include <iterator>
#include <string>

namespace fs25vr {

namespace {

// The DLL load notification of ntdll (documented, but not declared in the Windows SDK headers).
constexpr ULONG kDllNotificationReasonLoaded = 1;

struct DllLoadedNotificationData {
    ULONG Flags;
    const UNICODE_STRING* FullDllName;
    const UNICODE_STRING* BaseDllName;
    PVOID DllBase;
    ULONG SizeOfImage;
};

using DllNotificationCallback = VOID(CALLBACK*)(ULONG reason, const DllLoadedNotificationData* data, PVOID context);
using LdrRegisterDllNotificationFn = LONG(NTAPI*)(ULONG flags, DllNotificationCallback callback, PVOID context, PVOID* cookie);
using LdrUnregisterDllNotificationFn = LONG(NTAPI*)(PVOID cookie);

// Passes each caught factory call to the process's InjectorRuntime, which is created by the first one.
class RuntimeFactoryEntry final : public IDxgiFactoryEntry {
public:
    HRESULT CreateDXGIFactory(REFIID riid, void** ppFactory) override {
        return InjectorRuntime::Instance().GetDxgiProxy().CreateDXGIFactory(riid, ppFactory);
    }
    HRESULT CreateDXGIFactory1(REFIID riid, void** ppFactory) override {
        return InjectorRuntime::Instance().GetDxgiProxy().CreateDXGIFactory1(riid, ppFactory);
    }
    HRESULT CreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory) override {
        return InjectorRuntime::Instance().GetDxgiProxy().CreateDXGIFactory2(flags, riid, ppFactory);
    }
};

RuntimeFactoryEntry g_runtimeEntry;
DxgiExportHooks g_exportHooks(g_runtimeEntry);
PVOID g_notificationCookie = nullptr;

std::string Narrow(const wchar_t* text, int length) {
    if (length <= 0) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
    return result;
}

std::string ModulePath(HMODULE module) {
    wchar_t path[MAX_PATH * 2];
    const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    return Narrow(path, static_cast<int>(length));
}

// Hooks the system's dxgi.dll, kept loaded from then on so the patched code never goes away under the hooks.
void HookSystemDxgi(HMODULE systemDxgi, const char* when) {
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(systemDxgi), &pinned)) {
        LogWarn("Injector: Could not keep the system's dxgi.dll loaded (error " + std::to_string(GetLastError()) + ").");
    }
    if (g_exportHooks.Install(systemDxgi)) {
        LogInfo(std::string("Injector: Hooked the system's dxgi.dll ") + when + ".");
    }
}

bool IsSystemDxgiPath(const UNICODE_STRING* name) {
    const std::wstring& expected = SystemDxgiPath();
    if (!name || !name->Buffer || expected.empty()) return false;
    return CompareStringOrdinal(name->Buffer, static_cast<int>(name->Length / sizeof(wchar_t)), expected.c_str(),
                                static_cast<int>(expected.size()), TRUE) == CSTR_EQUAL;
}

// Runs with the loader lock held, after the module is mapped and before its own initialization.
VOID CALLBACK OnDllNotification(ULONG reason, const DllLoadedNotificationData* data, PVOID) {
    if (reason != kDllNotificationReasonLoaded || !data || g_exportHooks.IsInstalled()) return;
    if (!IsSystemDxgiPath(data->FullDllName)) return;
    HookSystemDxgi(static_cast<HMODULE>(data->DllBase), "as it loaded");
}

FARPROC NtdllFunction(const char* name) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll ? GetProcAddress(ntdll, name) : nullptr;
}

} // namespace

bool IsFs25VrModule(HMODULE module) {
    if (!module) return false;
    HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));  // VS_VERSION_INFO, RT_VERSION
    if (!info) return false;
    HGLOBAL loaded = LoadResource(module, info);
    const auto* data = loaded ? static_cast<const BYTE*>(LockResource(loaded)) : nullptr;
    const DWORD size = SizeofResource(module, info);
    if (!data || size == 0) return false;

    // A String entry of the version resource: header, the key with its terminating null, padding to 32 bits, then
    // the value. The resource itself starts 32-bit aligned.
    static constexpr wchar_t kKey[] = L"ProductName";
    static constexpr wchar_t kValue[] = L"FS25 VR";
    for (size_t offset = 0; offset + sizeof(kKey) <= size; offset += sizeof(wchar_t)) {
        if (std::memcmp(data + offset, kKey, sizeof(kKey)) != 0) continue;
        const size_t valueOffset = (offset + sizeof(kKey) + 3) & ~size_t{3};
        if (valueOffset + sizeof(kValue) <= size && std::memcmp(data + valueOffset, kValue, sizeof(kValue)) == 0) {
            return true;
        }
    }
    return false;
}

HMODULE FindEarlierInjector() {
    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    if (!dxgi || IsSystemDxgiModule(dxgi)) return nullptr;
    return IsFs25VrModule(dxgi) ? dxgi : nullptr;
}

void StartInjector() {
    if (HMODULE earlier = FindEarlierInjector()) {
        LogError("Injector: " + ModulePath(earlier) + " is FS25 VR from an earlier version, when FS25 VR was a dxgi.dll. "
                 "Delete it: FS25 VR is version.dll now. Until then this version does nothing and the earlier one runs.");
        return;
    }

    HMODULE systemDxgi = nullptr;
    const std::wstring& path = SystemDxgiPath();
    if (!path.empty() &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, path.c_str(), &systemDxgi) && systemDxgi) {
        HookSystemDxgi(systemDxgi, "at startup");
        return;
    }

    // Not loaded yet: the game's dxgi.dll is another one (ReShade's, for one), which loads the system's when the
    // game first calls it.
    auto registerNotification = reinterpret_cast<LdrRegisterDllNotificationFn>(NtdllFunction("LdrRegisterDllNotification"));
    if (!registerNotification || registerNotification(0, &OnDllNotification, nullptr, &g_notificationCookie) < 0) {
        g_notificationCookie = nullptr;
        LogError("Injector: Could not watch for the system's dxgi.dll to load; DXGI is not hooked.");
        return;
    }
    if (HMODULE game = GetModuleHandleW(L"dxgi.dll")) {
        LogInfo("Injector: The game's dxgi.dll is " + ModulePath(game) +
                "; the system's dxgi.dll is hooked when that one loads it.");
    } else {
        LogInfo("Injector: The system's dxgi.dll is hooked when it loads.");
    }
}

void StopInjector() {
    if (g_notificationCookie) {
        if (auto unregisterNotification =
                reinterpret_cast<LdrUnregisterDllNotificationFn>(NtdllFunction("LdrUnregisterDllNotification"))) {
            unregisterNotification(g_notificationCookie);
        }
        g_notificationCookie = nullptr;
    }
    g_exportHooks.Uninstall();
}

} // namespace fs25vr
