#pragma once

#include <atomic>

namespace fs25vr {

// Inline function hook backed by MinHook. MinHook is initialized exactly once
// per process; installing and removing hooks suspends the other threads while
// the target is patched.
class FunctionDetour {
public:
    FunctionDetour();
    ~FunctionDetour();

    FunctionDetour(const FunctionDetour&) = delete;
    FunctionDetour& operator=(const FunctionDetour&) = delete;

    FunctionDetour(FunctionDetour&& other) noexcept;
    FunctionDetour& operator=(FunctionDetour&& other) noexcept;

    bool Install(void* targetFunction, void* detourFunction);
    bool Uninstall();

    void* GetOriginal() const { return m_original; }
    void* GetTarget() const { return m_target; }
    void* GetDetour() const { return m_detour; }
    bool IsInstalled() const { return m_installed; }

private:
    void* m_target = nullptr;
    void* m_detour = nullptr;
    void* m_original = nullptr;
    // Read lock-free from hook entry points on game threads while Install/Uninstall write it.
    std::atomic<bool> m_installed{false};
};

} // namespace fs25vr
