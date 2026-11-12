// Imitates the game's startup with an FS25 VR Injector, then calls CreateDXGIFactory1 on a worker thread, as the game's
// first factory call. With the VDXR simulation layer enabled, the OpenXR runtime calls CreateDXGIFactory1 again from inside
// xrGetSystem while the Injector is still starting VR.
//
//   harness <Injector version.dll> [dxgi-first | dxgi-later]
//     The Injector from 1.2 (ADR-0038). It hooks CreateDXGIFactory/1/2 in the system's dxgi.dll, and the harness calls the
//     system's export, as the game does.
//     dxgi-first (default): the system's dxgi.dll is loaded before version.dll, as when the game imports it directly. The
//                           Injector hooks it at startup.
//     dxgi-later:           version.dll first, then the system's dxgi.dll, as when ReShade's dxgi.dll loads it on the
//                           game's first call. The Injector hooks it from its DLL load notification.
//   harness <proxy dxgi.dll>
//     An Injector up to 1.1.9, the game's dxgi.dll: the harness calls the proxy's own CreateDXGIFactory1 export.
//
// Exit code 0 = startup completed, 2 = still blocked after 20 s (deadlock), 3 = the Injector did not hook the system's
// CreateDXGIFactory1, 1 = bad arguments or a load failure.
#include <windows.h>
#include <dxgi.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

using PFN_Create = HRESULT(WINAPI*)(REFIID, void**);

static DWORD WINAPI CallFactory(LPVOID param) {
    auto create = reinterpret_cast<PFN_Create>(param);
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    printf("CreateDXGIFactory1 returned 0x%08lX\n", static_cast<unsigned long>(hr));
    if (factory) factory->Release();
    return 0;
}

static std::wstring SystemDxgiPath() {
    wchar_t dir[MAX_PATH] = {};
    GetSystemDirectoryW(dir, MAX_PATH);
    return std::wstring(dir) + L"\\dxgi.dll";
}

// The first bytes at `rva` in the system dxgi.dll file on disk, through its section table. (LoadLibraryEx as a data file or
// image resource hands back the loaded module, hooked, when it is already loaded.)
static bool ReadFileBytesAtRva(DWORD rva, unsigned char* out, DWORD count) {
    HANDLE file = CreateFileW(SystemDxgiPath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    auto readAt = [&](LONGLONG offset, void* buffer, DWORD size) {
        LARGE_INTEGER position{};
        position.QuadPart = offset;
        DWORD read = 0;
        return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) && ReadFile(file, buffer, size, &read, nullptr) && read == size;
    };
    bool ok = false;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (readAt(0, &dos, sizeof(dos)) && readAt(dos.e_lfanew, &nt, sizeof(nt))) {
        const LONGLONG sections = dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
        for (WORD i = 0; i < nt.FileHeader.NumberOfSections && !ok; ++i) {
            IMAGE_SECTION_HEADER section{};
            if (!readAt(sections + i * sizeof(section), &section, sizeof(section))) break;
            if (rva >= section.VirtualAddress && rva + count <= section.VirtualAddress + section.SizeOfRawData) {
                ok = readAt(section.PointerToRawData + (rva - section.VirtualAddress), out, count);
            }
        }
    }
    CloseHandle(file);
    return ok;
}

// Whether `live` (an export of the loaded system dxgi.dll) starts with a jmp that the file on disk does not have: the hook.
static bool IsHooked(HMODULE systemDxgi, const unsigned char* live) {
    unsigned char original[5] = {};
    const auto rva = static_cast<DWORD>(live - reinterpret_cast<const unsigned char*>(systemDxgi));
    if (!ReadFileBytesAtRva(rva, original, sizeof(original))) return false;
    return live[0] == 0xE9 && std::memcmp(original, live, sizeof(original)) != 0;
}

static HMODULE LoadOrFail(const std::wstring& path) {
    HMODULE module = LoadLibraryW(path.c_str());
    if (!module) fwprintf(stderr, L"LoadLibrary(%ls) failed: %lu\n", path.c_str(), GetLastError());
    return module;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"usage: harness <Injector version.dll> [dxgi-first | dxgi-later]\n"
                         L"       harness <proxy dxgi.dll>   (an Injector up to 1.1.9)\n");
        return 1;
    }
    const std::wstring injectorPath = argv[1];
    const std::wstring mode = argc >= 3 ? argv[2] : L"dxgi-first";
    if (mode != L"dxgi-first" && mode != L"dxgi-later") {
        fwprintf(stderr, L"unknown mode '%ls'\n", mode.c_str());
        return 1;
    }

    PFN_Create create = nullptr;
    const wchar_t* name = wcsrchr(injectorPath.c_str(), L'\\');
    name = name ? name + 1 : injectorPath.c_str();
    if (_wcsicmp(name, L"dxgi.dll") == 0) {
        // Up to 1.1.9: the proxy is the game's dxgi.dll and its export is the game's entry point.
        HMODULE proxy = LoadOrFail(injectorPath);
        if (!proxy) return 1;
        create = reinterpret_cast<PFN_Create>(GetProcAddress(proxy, "CreateDXGIFactory1"));
        printf("Mode: proxy dxgi.dll (an Injector up to 1.1.9)\n");
    } else {
        HMODULE systemDxgi = nullptr;
        if (mode == L"dxgi-first") {
            if (!(systemDxgi = LoadOrFail(SystemDxgiPath()))) return 1;
            if (!LoadOrFail(injectorPath)) return 1;
        } else {
            if (!LoadOrFail(injectorPath)) return 1;
            if (!(systemDxgi = LoadOrFail(SystemDxgiPath()))) return 1;
        }
        create = reinterpret_cast<PFN_Create>(GetProcAddress(systemDxgi, "CreateDXGIFactory1"));
        const bool hooked = create && IsHooked(systemDxgi, reinterpret_cast<const unsigned char*>(create));
        printf("Mode: Injector version.dll, %ls; the system's CreateDXGIFactory1 is %s\n", mode.c_str(),
               hooked ? "hooked" : "NOT hooked");
        if (!hooked) {
            printf("FAIL: the Injector did not hook the system's dxgi.dll (see fs25_vr.log next to version.dll)\n");
            fflush(stdout);
            return 3;
        }
    }
    if (!create) {
        fwprintf(stderr, L"CreateDXGIFactory1 not found\n");
        return 1;
    }

    HANDLE thread = CreateThread(nullptr, 0, CallFactory, reinterpret_cast<LPVOID>(create), 0, nullptr);
    if (WaitForSingleObject(thread, 20000) == WAIT_TIMEOUT) {
        printf("DEADLOCK: CreateDXGIFactory1 still blocked after 20 s\n");
        fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 2);
    }
    printf("OK: startup completed\n");
    return 0;
}
