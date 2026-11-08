// The exports of the system's version.dll. The Injector is loaded as the game's version.dll (ADR-0038), so every
// module in the game process that imports version.dll binds to these. Each one calls the same function of the
// system's version.dll, which is loaded from the system directory on first use (never from DllMain).
// VerLanguageNameA and VerLanguageNameW are forwarded to kernel32 in version_exports.def, as the system's
// version.dll forwards them.
//
// The Injector must never import version.dll itself: that import would bind to the Injector.
// scripts/check_injector_imports.ps1 checks it.
#include <windows.h>
#include <winver.h>

namespace {

HMODULE SystemVersionDll() {
    static const HMODULE module = [] {
        wchar_t path[MAX_PATH];
        const UINT length = GetSystemDirectoryW(path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH || wcscat_s(path, L"\\version.dll") != 0) return HMODULE{};
        return LoadLibraryW(path);
    }();
    return module;
}

template <typename Fn>
Fn SystemVersionFunction(const char* name) {
    const HMODULE module = SystemVersionDll();
    return module ? reinterpret_cast<Fn>(GetProcAddress(module, name)) : nullptr;
}

// GetFileVersionInfoByHandle is not in the Windows SDK. Its integer and pointer arguments (at most four) arrive
// in the first four argument registers, so passing on four of them passes on whatever the caller gave.
using GenericFourArgFn = INT_PTR(WINAPI*)(INT_PTR, INT_PTR, INT_PTR, INT_PTR);

} // namespace

// A stub that cannot find the system function returns `fail`, as the system function reports an error (FALSE or 0
// with ERROR_PROC_NOT_FOUND; VerFindFile and VerInstallFile have their own error bits). The .def file exports
// each Fs25Vr_<name> as <name>.
#define FS25VR_VERSION_STUB(ret, name, fail, params, args)                          \
    extern "C" ret WINAPI Fs25Vr_##name params {                                    \
        static const auto fn = SystemVersionFunction<decltype(&::name)>(#name);     \
        if (!fn) {                                                                  \
            SetLastError(ERROR_PROC_NOT_FOUND);                                     \
            return fail;                                                            \
        }                                                                           \
        return fn args;                                                             \
    }

FS25VR_VERSION_STUB(BOOL, GetFileVersionInfoA, FALSE,
    (LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData),
    (lptstrFilename, dwHandle, dwLen, lpData))
FS25VR_VERSION_STUB(BOOL, GetFileVersionInfoW, FALSE,
    (LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData),
    (lptstrFilename, dwHandle, dwLen, lpData))
FS25VR_VERSION_STUB(BOOL, GetFileVersionInfoExA, FALSE,
    (DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData),
    (dwFlags, lpwstrFilename, dwHandle, dwLen, lpData))
FS25VR_VERSION_STUB(BOOL, GetFileVersionInfoExW, FALSE,
    (DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData),
    (dwFlags, lpwstrFilename, dwHandle, dwLen, lpData))
FS25VR_VERSION_STUB(DWORD, GetFileVersionInfoSizeA, 0,
    (LPCSTR lptstrFilename, LPDWORD lpdwHandle),
    (lptstrFilename, lpdwHandle))
FS25VR_VERSION_STUB(DWORD, GetFileVersionInfoSizeW, 0,
    (LPCWSTR lptstrFilename, LPDWORD lpdwHandle),
    (lptstrFilename, lpdwHandle))
FS25VR_VERSION_STUB(DWORD, GetFileVersionInfoSizeExA, 0,
    (DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle),
    (dwFlags, lpwstrFilename, lpdwHandle))
FS25VR_VERSION_STUB(DWORD, GetFileVersionInfoSizeExW, 0,
    (DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle),
    (dwFlags, lpwstrFilename, lpdwHandle))
FS25VR_VERSION_STUB(DWORD, VerFindFileA, VFF_BUFFTOOSMALL,
    (DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT puCurDirLen,
     LPSTR szDestDir, PUINT puDestDirLen),
    (uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen))
FS25VR_VERSION_STUB(DWORD, VerFindFileW, VFF_BUFFTOOSMALL,
    (DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT puCurDirLen,
     LPWSTR szDestDir, PUINT puDestDirLen),
    (uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen))
FS25VR_VERSION_STUB(DWORD, VerInstallFileA, VIF_CANNOTREADSRC,
    (DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir,
     LPCSTR szCurDir, LPSTR szTmpFile, PUINT puTmpFileLen),
    (uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen))
FS25VR_VERSION_STUB(DWORD, VerInstallFileW, VIF_CANNOTREADSRC,
    (DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir,
     LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT puTmpFileLen),
    (uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen))
FS25VR_VERSION_STUB(BOOL, VerQueryValueA, FALSE,
    (LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen),
    (pBlock, lpSubBlock, lplpBuffer, puLen))
FS25VR_VERSION_STUB(BOOL, VerQueryValueW, FALSE,
    (LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen),
    (pBlock, lpSubBlock, lplpBuffer, puLen))

#undef FS25VR_VERSION_STUB

extern "C" INT_PTR WINAPI Fs25Vr_GetFileVersionInfoByHandle(INT_PTR a, INT_PTR b, INT_PTR c, INT_PTR d) {
    static const auto fn = SystemVersionFunction<GenericFourArgFn>("GetFileVersionInfoByHandle");
    if (!fn) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return fn(a, b, c, d);
}
