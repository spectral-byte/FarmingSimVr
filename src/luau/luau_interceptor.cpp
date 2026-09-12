#include "luau_interceptor.h"
#include "core/logger.h"
#include <windows.h>
#include <cstring>
#include <string>

namespace fs25vr {

namespace {
static LuauInterceptor* s_activeInterceptor = nullptr;
} // anonymous namespace

LuauInterceptor& LuauInterceptor::Instance() {
    static LuauInterceptor instance;
    return instance;
}

LuauInterceptor::LuauInterceptor(std::shared_ptr<VRBridge> bridge)
    : m_bridge(bridge ? std::move(bridge) : std::shared_ptr<VRBridge>(&VRBridge::Instance(), [](auto*) {})) {
}

LuauInterceptor::~LuauInterceptor() {
    Detach();
}

bool LuauInterceptor::Attach(void* targetQueryFunction) {
    if (!targetQueryFunction) {
        LogError("LuauInterceptor::Attach: Target query function is null.");
        return false;
    }

    if (m_detour.IsInstalled()) {
        LogWarn("LuauInterceptor::Attach: Already attached, detaching first.");
        Detach();
    }

    s_activeInterceptor = this;
    bool success = m_detour.Install(targetQueryFunction, reinterpret_cast<void*>(&LuauInterceptor::DetourEntryPoint));
    if (success) {
        LogInfo("LuauInterceptor: Successfully attached detour to head-tracking availability query.");
    } else {
        LogError("LuauInterceptor: Failed to attach detour to target function.");
        if (s_activeInterceptor == this) {
            s_activeInterceptor = nullptr;
        }
    }
    return success;
}

bool LuauInterceptor::AttachToEngine() {
    HMODULE hModule = GetModuleHandleW(nullptr);
    if (!hModule) {
        LogError("LuauInterceptor::AttachToEngine: Failed to get process module handle.");
        return false;
    }

    WCHAR modulePath[MAX_PATH] = {0};
    GetModuleFileNameW(hModule, modulePath, MAX_PATH);
    std::wstring pathStr(modulePath);

    // If running in unit test runner, don't attempt host scan
    if (pathStr.find(L"fs25_vr_tests") != std::wstring::npos) {
        LogInfo("LuauInterceptor: Running in test executable context. Standing by for explicit attachment.");
        return false;
    }

    LogInfo("LuauInterceptor: Scanning engine module for head-tracking availability query...");

    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(hModule);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        LogError("LuauInterceptor::AttachToEngine: Invalid DOS header signature.");
        return false;
    }

    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(hModule) + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        LogError("LuauInterceptor::AttachToEngine: Invalid NT header signature.");
        return false;
    }

    auto* section = IMAGE_FIRST_SECTION(ntHeaders);
    const uint8_t* rdataBase = nullptr;
    size_t rdataSize = 0;
    const uint8_t* textBase = nullptr;
    size_t textSize = 0;

    uint8_t* moduleBase = reinterpret_cast<uint8_t*>(hModule);

    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++section) {
        char secName[9] = {0};
        std::memcpy(secName, section->Name, 8);
        if (std::strcmp(secName, ".rdata") == 0) {
            rdataBase = moduleBase + section->VirtualAddress;
            rdataSize = section->Misc.VirtualSize;
        } else if (std::strcmp(secName, ".text") == 0) {
            textBase = moduleBase + section->VirtualAddress;
            textSize = section->Misc.VirtualSize;
        }
    }

    if (!rdataBase || !textBase) {
        LogWarn("LuauInterceptor: Could not locate .rdata or .text sections in host module.");
        return false;
    }

    // Find the string "isHeadTrackingAvailable\0" in .rdata
    const char* needle = "isHeadTrackingAvailable";
    size_t needleLen = std::strlen(needle) + 1;
    const uint8_t* stringAddr = nullptr;

    for (size_t i = 0; i + needleLen <= rdataSize; ++i) {
        if (std::memcmp(rdataBase + i, needle, needleLen) == 0) {
            stringAddr = rdataBase + i;
            break;
        }
    }

    if (!stringAddr) {
        LogWarn("LuauInterceptor: 'isHeadTrackingAvailable' string symbol not found in .rdata; standing by.");
        return false;
    }

    LogInfo("LuauInterceptor: Found query symbol string in .rdata. Scanning for registration cross-references...");

    void* candidateQueryFunction = nullptr;

    for (size_t off = 0; off + 7 <= textSize; ++off) {
        const uint8_t* insn = textBase + off;
        if ((insn[0] == 0x48 || insn[0] == 0x4C) && (insn[1] == 0x8D || insn[1] == 0x8B)) {
            uint8_t modrm = insn[2];
            if (((modrm >> 6) & 3) == 0 && (modrm & 7) == 5) {
                int32_t disp = *reinterpret_cast<const int32_t*>(insn + 3);
                const uint8_t* target = insn + 7 + disp;
                if (target == stringAddr) {
                    LogInfo("LuauInterceptor: Found registration reference in .text at offset +0x" + 
                            std::to_string(off));

                    size_t scanStart = off > 64 ? off - 64 : 0;
                    size_t scanEnd = (off + 64 + 7 <= textSize) ? off + 64 : textSize - 7;

                    for (size_t j = scanStart; j <= scanEnd; ++j) {
                        if (j == off) continue;
                        const uint8_t* subInsn = textBase + j;
                        if (subInsn[0] == 0x48 && subInsn[1] == 0x8D && subInsn[2] == 0x15) {
                            int32_t fdisp = *reinterpret_cast<const int32_t*>(subInsn + 3);
                            const uint8_t* funcPtr = subInsn + 7 + fdisp;
                            if (funcPtr >= textBase && funcPtr < textBase + textSize) {
                                candidateQueryFunction = const_cast<uint8_t*>(funcPtr);
                                break;
                            }
                        }
                    }
                    if (candidateQueryFunction) break;
                }
            }
        }
    }

    if (candidateQueryFunction) {
        LogInfo("LuauInterceptor: Discovered head-tracking query function in memory. Installing detour...");
        return Attach(candidateQueryFunction);
    }

    LogInfo("LuauInterceptor: Query function scan complete. Interception channel armed.");
    return false;
}

void LuauInterceptor::Detach() {
    if (m_detour.IsInstalled()) {
        m_detour.Uninstall();
        LogInfo("LuauInterceptor: Detached from target function.");
    }
    if (s_activeInterceptor == this) {
        s_activeInterceptor = nullptr;
    }
    m_activeState = nullptr;
    m_bridgeInjected = false;
}

int LuauInterceptor::HandleQuery(lua_State* L) {
    if (L) {
        m_activeState = L;
        if (!m_bridgeInjected && m_bridge) {
            m_bridge->Inject(L);
            m_bridgeInjected = true;
            LogInfo("LuauInterceptor: Intercepted active Luau state pointer and injected VRBridge table.");
        }
    }

    auto originalQuery = reinterpret_cast<int(*)(lua_State*)>(m_detour.GetOriginal());
    if (originalQuery) {
        return originalQuery(L);
    }
    return 0;
}

int LuauInterceptor::DetourEntryPoint(lua_State* L) {
    if (s_activeInterceptor) {
        return s_activeInterceptor->HandleQuery(L);
    }
    return Instance().HandleQuery(L);
}

} // namespace fs25vr
