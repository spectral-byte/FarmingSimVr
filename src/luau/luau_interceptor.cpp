#include "luau_interceptor.h"
#include "core/logger.h"
#include "core/dll_lifecycle.h"
#include "core/injector_runtime.h"
#include <windows.h>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace fs25vr {

LuauInterceptor::LuauInterceptor(std::shared_ptr<VRBridge> bridge)
    : m_bridge(std::move(bridge)) {
}

LuauInterceptor::~LuauInterceptor() {
    if (IsProcessTerminating()) return;
    Detach();
}

bool LuauInterceptor::Attach(void* targetQueryFunction) {
    if (!targetQueryFunction) {
        LogError("LuauInterceptor::Attach: Target query function is null.");
        return false;
    }

    if (!m_ownedByRuntime) {
        LogWarn("LuauInterceptor::Attach: Not owned by a live InjectorRuntime; detour not installed.");
        return false;
    }

    if (m_detour.IsInstalled()) {
        LogWarn("LuauInterceptor::Attach: Already attached, detaching first.");
        Detach();
    }

    bool success = m_detour.Install(targetQueryFunction, reinterpret_cast<void*>(&LuauInterceptor::DetourEntryPoint));
    if (success) {
        LogInfo("LuauInterceptor: Successfully attached detour to head-tracking availability query.");
    } else {
        LogError("LuauInterceptor: Failed to attach detour to target function.");
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

    static const char* const targetSymbols[] = {
        "isHeadTrackingAvailable",
        "getHeadTrackingTranslation",
        "getHeadTrackingRotation"
    };

    void* candidateQueryFunction = nullptr;
    const char* matchedSymbol = nullptr;

    for (const char* needle : targetSymbols) {
        size_t needleLen = std::strlen(needle) + 1;
        const uint8_t* stringAddr = nullptr;

        for (size_t i = 0; i + needleLen <= rdataSize; ++i) {
            if (std::memcmp(rdataBase + i, needle, needleLen) == 0) {
                stringAddr = rdataBase + i;
                break;
            }
        }

        if (!stringAddr) {
            continue;
        }

        LogInfo(std::string("LuauInterceptor: Found '") + needle + "' in .rdata. Scanning for function pointer...");
        candidateQueryFunction = FindQueryFunctionInText(textBase, textSize, stringAddr);
        if (candidateQueryFunction) {
            matchedSymbol = needle;
            break;
        }
    }

    if (candidateQueryFunction && matchedSymbol) {
        std::stringstream ss;
        ss << "LuauInterceptor: Discovered '" << matchedSymbol << "' function at 0x"
           << std::hex << reinterpret_cast<uintptr_t>(candidateQueryFunction)
           << ". Installing detour...";
        LogInfo(ss.str());
        return Attach(candidateQueryFunction);
    }

    LogInfo("LuauInterceptor: Query function scan complete. Interception channel armed.");
    return false;
}

void* LuauInterceptor::FindQueryFunctionInText(const uint8_t* textBase, size_t textSize, const uint8_t* stringAddr) {
    if (!textBase || textSize < 16 || !stringAddr) {
        return nullptr;
    }

    struct Candidate {
        void* funcPtr = nullptr;
        size_t distance = static_cast<size_t>(-1);
        size_t refOffset = 0;
    };

    std::vector<Candidate> candidates;

    for (size_t off = 0; off + 7 <= textSize; ++off) {
        const uint8_t* insn = textBase + off;
        if ((insn[0] == 0x48 || insn[0] == 0x4C) && (insn[1] == 0x8D || insn[1] == 0x8B)) {
            uint8_t modrm = insn[2];
            if (((modrm >> 6) & 3) == 0 && (modrm & 7) == 5) {
                int32_t disp = *reinterpret_cast<const int32_t*>(insn + 3);
                const uint8_t* target = insn + 7 + disp;
                if (target == stringAddr) {
                    std::stringstream ss;
                    ss << "LuauInterceptor: Found registration reference in .text at offset +0x"
                       << std::hex << off;
                    LogInfo(ss.str());

                    size_t scanStart = off > 128 ? off - 128 : 0;
                    size_t scanEnd = (off + 128 + 7 <= textSize) ? off + 128 : textSize - 7;

                    for (size_t j = scanStart; j <= scanEnd; ++j) {
                        if (j == off) continue;
                        const uint8_t* subInsn = textBase + j;
                        const uint8_t* funcPtr = nullptr;

                        // Case 1: 64-bit RIP-relative LEA (48 8D / 4C 8D with mod=00, rm=101)
                        // Covers all 16 registers (rax, rcx, rdx, rbx, rbp, rsi, rdi, r8-r15)
                        if ((subInsn[0] == 0x48 || subInsn[0] == 0x4C) && subInsn[1] == 0x8D) {
                            uint8_t subModRm = subInsn[2];
                            if (((subModRm >> 6) & 3) == 0 && (subModRm & 7) == 5) {
                                int32_t fdisp = *reinterpret_cast<const int32_t*>(subInsn + 3);
                                funcPtr = subInsn + 7 + fdisp;
                            }
                        }
                        // Case 2: 64-bit immediate MOV (48 B8..BF or 49 B8..BF)
                        else if (j + 10 <= textSize && (subInsn[0] == 0x48 || subInsn[0] == 0x49) &&
                                 (subInsn[1] >= 0xB8 && subInsn[1] <= 0xBF)) {
                            funcPtr = *reinterpret_cast<const uint8_t* const*>(subInsn + 2);
                        }

                        if (funcPtr && funcPtr >= textBase && funcPtr < textBase + textSize && funcPtr != stringAddr) {
                            if (funcPtr >= subInsn && funcPtr < subInsn + 7) {
                                continue;
                            }
                            if (funcPtr >= insn && funcPtr < insn + 7) {
                                continue;
                            }
                            uint8_t firstByte = *funcPtr;
                            if (firstByte == 0xCC || firstByte == 0xC3 || firstByte == 0x00 || firstByte == 0x90) {
                                continue;
                            }

                            size_t dist = (j > off) ? (j - off) : (off - j);
                            candidates.push_back({const_cast<uint8_t*>(funcPtr), dist, off});
                        }
                    }
                }
            }
        }
    }

    if (candidates.empty()) {
        return nullptr;
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.distance < b.distance;
    });

    return candidates.front().funcPtr;
}

void LuauInterceptor::Detach() {
    if (m_detour.IsInstalled()) {
        m_detour.Uninstall();
        LogInfo("LuauInterceptor: Detached from target function.");
    }
    m_activeState = nullptr;
    m_bridgeInjected = false;
}

int LuauInterceptor::HandleQuery(lua_State* L) {
    if (L && m_bridge) {
        m_activeState = L;
        if (!m_bridge->IsInjected(L)) {
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
    InjectorRuntime* runtime = InjectorRuntime::ForHook([](InjectorRuntime& r) {
        return r.GetLuauInterceptor().GetDetour().IsInstalled();
    });
    return runtime ? runtime->GetLuauInterceptor().HandleQuery(L) : 0;
}

} // namespace fs25vr
