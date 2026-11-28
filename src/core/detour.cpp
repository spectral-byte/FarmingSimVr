#include "detour.h"
#include "logger.h"
#include <MinHook.h>
#include "dll_lifecycle.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace fs25vr {

namespace {

std::once_flag g_minHookInitOnce;
bool g_minHookReady = false;

enum class HookState { Live, Orphaned };
std::mutex g_hookStateMutex;
std::unordered_map<void*, HookState> g_hookStates;
std::atomic<bool> g_failNextRemoval{false};

void SetHookState(void* target, HookState state) {
    std::lock_guard<std::mutex> lock(g_hookStateMutex);
    g_hookStates[target] = state;
}

void ForgetHook(void* target) {
    std::lock_guard<std::mutex> lock(g_hookStateMutex);
    g_hookStates.erase(target);
}

bool IsHooked(void* target) {
    std::lock_guard<std::mutex> lock(g_hookStateMutex);
    return g_hookStates.count(target) != 0;
}

bool TakeOrphan(void* target) {
    std::lock_guard<std::mutex> lock(g_hookStateMutex);
    const auto it = g_hookStates.find(target);
    if (it == g_hookStates.end() || it->second != HookState::Orphaned) return false;
    g_hookStates.erase(it);
    return true;
}

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

void* ResolveThunks(void* function) {
    constexpr int kMaxHops = 8;
    for (int hop = 0; hop < kMaxHops; ++hop) {
        if (IsHooked(function)) return function;
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

}

FunctionDetour::FunctionDetour() = default;

FunctionDetour::~FunctionDetour() {
    if (IsProcessTerminating()) return;
    Uninstall();
}

FunctionDetour::FunctionDetour(FunctionDetour&& other) noexcept
    : m_target(std::exchange(other.m_target, nullptr)),
      m_detour(std::exchange(other.m_detour, nullptr)),
      m_original(other.m_original.exchange(nullptr)),
      m_installed(other.m_installed.exchange(false)) {}

FunctionDetour& FunctionDetour::operator=(FunctionDetour&& other) noexcept {
    if (this != &other) {
        Uninstall();
        m_target = std::exchange(other.m_target, nullptr);
        m_detour = std::exchange(other.m_detour, nullptr);
        m_original.store(other.m_original.exchange(nullptr));
        m_installed.store(other.m_installed.exchange(false));
    }
    return *this;
}

bool FunctionDetour::Install(void* targetFunction, void* detourFunction) {
    if (!targetFunction || !detourFunction) {
        LogError("FunctionDetour: Target or detour function pointer is null.");
        return false;
    }
    if (m_installed.load(std::memory_order_acquire)) {
        LogWarn("FunctionDetour: Detour already installed.");
        return false;
    }
    if (!EnsureMinHookInitialized()) {
        return false;
    }

    targetFunction = ResolveThunks(targetFunction);

    void* original = nullptr;
    MH_STATUS status = MH_CreateHook(targetFunction, detourFunction, &original);
    if (status == MH_ERROR_ALREADY_CREATED && TakeOrphan(targetFunction)) {
        LogWarn("FunctionDetour: Removing a stale hook left by a failed uninstall.");
        const MH_STATUS removeStatus = MH_RemoveHook(targetFunction);
        if (removeStatus != MH_OK) {
            SetHookState(targetFunction, HookState::Orphaned);
            LogError(std::string("FunctionDetour: MH_RemoveHook of stale hook failed: ") + MH_StatusToString(removeStatus));
            return false;
        }
        status = MH_CreateHook(targetFunction, detourFunction, &original);
    }
    if (status != MH_OK) {
        LogError(std::string("FunctionDetour: MH_CreateHook failed: ") + MH_StatusToString(status));
        return false;
    }
    SetHookState(targetFunction, HookState::Live);

    m_target = targetFunction;
    m_detour = detourFunction;
    m_original.store(original, std::memory_order_release);
    m_installed.store(true, std::memory_order_release);

    status = MH_EnableHook(targetFunction);
    if (status != MH_OK) {
        m_installed.store(false, std::memory_order_release);
        m_original.store(nullptr, std::memory_order_release);
        LogError(std::string("FunctionDetour: MH_EnableHook failed: ") + MH_StatusToString(status));
        if (MH_RemoveHook(targetFunction) == MH_OK) {
            ForgetHook(targetFunction);
        } else {
            SetHookState(targetFunction, HookState::Orphaned);
        }
        m_target = nullptr;
        m_detour = nullptr;
        return false;
    }

    LogInfo("FunctionDetour: Successfully installed detour.");
    return true;
}

bool FunctionDetour::Uninstall() {
    if (!m_installed.load(std::memory_order_acquire) || !m_target) {
        return false;
    }

    const MH_STATUS disableStatus = MH_DisableHook(m_target);
    if (disableStatus != MH_OK && disableStatus != MH_ERROR_DISABLED && disableStatus != MH_ERROR_NOT_CREATED) {
        LogError(std::string("FunctionDetour: MH_DisableHook failed: ") + MH_StatusToString(disableStatus));
        return false;
    }
    m_installed.store(false, std::memory_order_release);
    m_original.store(nullptr, std::memory_order_release);
    const MH_STATUS removeStatus = g_failNextRemoval.exchange(false) ? MH_ERROR_MEMORY_PROTECT : MH_RemoveHook(m_target);
    if (removeStatus == MH_OK) {
        ForgetHook(m_target);
    } else {
        SetHookState(m_target, HookState::Orphaned);
        LogError(std::string("FunctionDetour: MH_RemoveHook failed: ") + MH_StatusToString(removeStatus));
    }

    m_target = nullptr;
    m_detour = nullptr;

    LogInfo("FunctionDetour: Successfully uninstalled detour.");
    return removeStatus == MH_OK;
}

void FailNextHookRemovalForTesting() {
    g_failNextRemoval = true;
}

}
