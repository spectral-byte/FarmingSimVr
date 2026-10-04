#include "detour.h"
#include "logger.h"
#include <MinHook.h>
#include "dll_lifecycle.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace fs25vr {

namespace {

std::once_flag g_minHookInitOnce;
bool g_minHookReady = false;

bool EnsureMinHookInitialized() {
    std::call_once(g_minHookInitOnce, [] {
        const MH_STATUS status = MH_Initialize();
        g_minHookReady = (status == MH_OK);
        if (!g_minHookReady) {
            LogError(std::string("FunctionDetour: MH_Initialize failed: ") + MH_StatusToString(status));
        }
    });
    return g_minHookReady;
}

// Resolve through incremental link table (ILT) jmp thunks (E9 rel32) and
// 6-byte indirect import thunks (FF 25 disp32) to the real function body.
void* ResolveThunks(void* function) {
    // Real thunk chains are one or two hops; the cap stops a jmp cycle from hanging us.
    constexpr int kMaxHops = 8;
    for (int hop = 0; hop < kMaxHops; ++hop) {
        const auto* bytes = static_cast<const uint8_t*>(function);
        if (bytes[0] == 0xE9) {
            int32_t rel = 0;
            std::memcpy(&rel, bytes + 1, sizeof(rel));
            function = const_cast<uint8_t*>(bytes + 5 + rel);
        } else if (bytes[0] == 0xFF && bytes[1] == 0x25) {
            int32_t disp = 0;
            std::memcpy(&disp, bytes + 2, sizeof(disp));
            void* const* slot = reinterpret_cast<void* const*>(bytes + 6 + disp);
            if (!*slot) return function;
            function = *slot;
        } else {
            return function;
        }
    }
    return function;
}

} // anonymous namespace

FunctionDetour::FunctionDetour() = default;

FunctionDetour::~FunctionDetour() {
    // At termination the patch stays and the trampoline is never freed (see IsProcessTerminating).
    if (IsProcessTerminating()) return;
    Uninstall();
}

FunctionDetour::FunctionDetour(FunctionDetour&& other) noexcept
    : m_target(std::exchange(other.m_target, nullptr)),
      m_detour(std::exchange(other.m_detour, nullptr)),
      m_original(std::exchange(other.m_original, nullptr)),
      m_installed(std::exchange(other.m_installed, false)) {}

FunctionDetour& FunctionDetour::operator=(FunctionDetour&& other) noexcept {
    if (this != &other) {
        Uninstall();
        m_target = std::exchange(other.m_target, nullptr);
        m_detour = std::exchange(other.m_detour, nullptr);
        m_original = std::exchange(other.m_original, nullptr);
        m_installed = std::exchange(other.m_installed, false);
    }
    return *this;
}

bool FunctionDetour::Install(void* targetFunction, void* detourFunction) {
    if (!targetFunction || !detourFunction) {
        LogError("FunctionDetour: Target or detour function pointer is null.");
        return false;
    }
    if (m_installed) {
        LogWarn("FunctionDetour: Detour already installed.");
        return false;
    }
    if (!EnsureMinHookInitialized()) {
        return false;
    }

    targetFunction = ResolveThunks(targetFunction);

    void* original = nullptr;
    MH_STATUS status = MH_CreateHook(targetFunction, detourFunction, &original);
    if (status != MH_OK) {
        LogError(std::string("FunctionDetour: MH_CreateHook failed: ") + MH_StatusToString(status));
        return false;
    }

    // Publish the trampoline before the patch goes live: a thread entering the
    // detour the moment other threads resume must already see GetOriginal().
    m_target = targetFunction;
    m_detour = detourFunction;
    m_original = original;

    // MinHook suspends all other threads while patching the target.
    status = MH_EnableHook(targetFunction);
    if (status != MH_OK) {
        LogError(std::string("FunctionDetour: MH_EnableHook failed: ") + MH_StatusToString(status));
        MH_RemoveHook(targetFunction);
        m_target = nullptr;
        m_detour = nullptr;
        m_original = nullptr;
        return false;
    }

    m_installed = true;
    LogInfo("FunctionDetour: Successfully installed detour.");
    return true;
}

bool FunctionDetour::Uninstall() {
    if (!m_installed || !m_target) {
        return false;
    }

    // Disabling restores the original bytes with other threads suspended.
    const MH_STATUS disableStatus = MH_DisableHook(m_target);
    if (disableStatus != MH_OK && disableStatus != MH_ERROR_DISABLED && disableStatus != MH_ERROR_NOT_CREATED) {
        // The patch is still live, so callers still need the trampoline: stay installed.
        LogError(std::string("FunctionDetour: MH_DisableHook failed: ") + MH_StatusToString(disableStatus));
        return false;
    }
    const MH_STATUS removeStatus = MH_RemoveHook(m_target);
    if (removeStatus != MH_OK) {
        LogError(std::string("FunctionDetour: MH_RemoveHook failed: ") + MH_StatusToString(removeStatus));
    }

    m_target = nullptr;
    m_detour = nullptr;
    m_original = nullptr;
    m_installed = false;

    LogInfo("FunctionDetour: Successfully uninstalled detour.");
    return removeStatus == MH_OK;
}

} // namespace fs25vr
