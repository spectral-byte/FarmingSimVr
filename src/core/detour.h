#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fs25vr {

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

    void* GetOriginal() const { return m_trampolineEntry ? m_trampolineEntry : m_trampoline; }
    void* GetTarget() const { return m_target; }
    void* GetDetour() const { return m_detour; }
    bool IsInstalled() const { return m_installed; }

private:
    void* m_target = nullptr;
    void* m_detour = nullptr;
    void* m_trampoline = nullptr;
    void* m_trampolineEntry = nullptr;
    std::vector<uint8_t> m_originalBytes;
    size_t m_hookSize = 0;
    bool m_installed = false;
};

} // namespace fs25vr
