#include "core/companion_settings.h"

#include <windows.h>

#include <charconv>
#include <cmath>
#include <fstream>
#include <string>

namespace fs25vr {

namespace {

// FOLDERID_Documents, spelled out so the Injector needs neither knownfolders.h's definition (uuid.lib) nor shell32 at
// load time.
constexpr GUID kDocumentsFolderId = { 0xFDD39AD0, 0x238F, 0x46AF, { 0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7 } };

// SHGetKnownFolderPath and CoTaskMemFree are looked up when they are needed, so the Injector, which the game loads early
// as its version.dll, keeps shell32 and ole32 out of its import table. Both are loaded in the game process anyway.
std::filesystem::path KnownDocumentsFolder() {
    using GetKnownFolderPathFn = HRESULT(WINAPI*)(const GUID&, DWORD, HANDLE, PWSTR*);
    using TaskMemFreeFn = void(WINAPI*)(LPVOID);

    std::filesystem::path folder;
    HMODULE shell32 = LoadLibraryExW(L"shell32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE ole32 = LoadLibraryExW(L"ole32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (shell32 != nullptr && ole32 != nullptr) {
        auto getKnownFolderPath = reinterpret_cast<GetKnownFolderPathFn>(GetProcAddress(shell32, "SHGetKnownFolderPath"));
        auto taskMemFree = reinterpret_cast<TaskMemFreeFn>(GetProcAddress(ole32, "CoTaskMemFree"));
        PWSTR path = nullptr;
        if (getKnownFolderPath != nullptr && taskMemFree != nullptr &&
            SUCCEEDED(getKnownFolderPath(kDocumentsFolderId, 0, nullptr, &path)) && path != nullptr) {
            folder = path;
        }
        if (path != nullptr && taskMemFree != nullptr) taskMemFree(path);
    }
    if (ole32 != nullptr) FreeLibrary(ole32);
    if (shell32 != nullptr) FreeLibrary(shell32);
    return folder;
}

} // namespace

std::filesystem::path DefaultCompanionSettingsPath() {
    const std::filesystem::path documents = KnownDocumentsFolder();
    if (documents.empty()) return {};
    return documents / L"My Games" / L"FarmingSimulator2025" / L"modSettings" / L"FS25_VRCompanion" / L"settings.xml";
}

std::optional<float> ReadCompanionSettingFloat(const std::filesystem::path& file, std::string_view name) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    // The file holds a few dozen settings; anything this large is not one the Companion Mod wrote.
    constexpr std::streamsize kMaxBytes = 1 << 20;
    std::string text(static_cast<size_t>(kMaxBytes), '\0');
    in.read(text.data(), kMaxBytes);
    text.resize(static_cast<size_t>(in.gcount()));

    const std::string open = "<" + std::string(name) + ">";
    const std::string close = "</" + std::string(name) + ">";
    const size_t start = text.find(open);
    if (start == std::string::npos) return std::nullopt;
    const size_t valueStart = start + open.size();
    const size_t end = text.find(close, valueStart);
    if (end == std::string::npos) return std::nullopt;

    std::string_view value(text.data() + valueStart, end - valueStart);
    constexpr std::string_view kSpace = " \t\r\n";
    const size_t first = value.find_first_not_of(kSpace);
    if (first == std::string_view::npos) return std::nullopt;
    value = value.substr(first, value.find_last_not_of(kSpace) - first + 1);

    float number = 0.0f;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (ec != std::errc() || ptr != value.data() + value.size() || !std::isfinite(number)) return std::nullopt;
    return number;
}

} // namespace fs25vr
