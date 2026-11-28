#include "proxy/version_exports.h"

namespace fs25vr {

HMODULE SystemVersionDll() {
    static const HMODULE module = [] {
        wchar_t path[MAX_PATH];
        const UINT length = GetSystemDirectoryW(path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH || wcscat_s(path, L"\\version.dll") != 0) return HMODULE{};
        return LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }();
    return module;
}

}
