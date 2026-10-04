#include "detour.h"
#include "logger.h"
#include "dll_lifecycle.h"
#include <windows.h>
#include <cstring>
#include <cstdint>

namespace fs25vr {

namespace {

struct InstructionInfo {
    size_t length = 0;
    bool isRipRelative = false;
    bool isRet = false;
    size_t dispOffset = 0;
};

void DecodeModRm(const uint8_t* code, size_t& offset, InstructionInfo& info) {
    uint8_t modrm = code[offset++];
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t rm = modrm & 7;
    bool hasSib = (mod != 3 && rm == 4);
    uint8_t sib = hasSib ? code[offset++] : 0;
    if (mod == 0) {
        if (rm == 5) {
            info.isRipRelative = true;
            info.dispOffset = offset;
            offset += 4;
        } else if (hasSib && (sib & 7) == 5) {
            offset += 4;
        }
    } else if (mod == 1) {
        offset += 1;
    } else if (mod == 2) {
        offset += 4;
    }
}

InstructionInfo DecodeInstruction(const uint8_t* code) {
    InstructionInfo info;
    size_t offset = 0;
    bool hasRexW = false;
    bool hasPrefix = true;

    // 1. Process prefixes
    while (hasPrefix) {
        uint8_t b = code[offset];
        if (b >= 0x40 && b <= 0x4F) { // REX prefix
            if (b & 0x08) hasRexW = true;
            offset++;
        } else if (b == 0x66 || b == 0x67 || b == 0x2E || b == 0x36 || b == 0x3E ||
                   b == 0x26 || b == 0x64 || b == 0x65 || b == 0xF0 || b == 0xF2 || b == 0xF3) {
            offset++;
        } else {
            hasPrefix = false;
        }
    }

    uint8_t opcode = code[offset++];

    // Ret detection
    if (opcode == 0xC3 || opcode == 0xCB) {
        info.length = offset;
        info.isRet = true;
        return info;
    }
    if (opcode == 0xC2 || opcode == 0xCA) {
        info.length = offset + 2;
        info.isRet = true;
        return info;
    }

    // 2-byte escape opcode (0x0F ...)
    if (opcode == 0x0F) {
        uint8_t opcode2 = code[offset++];
        if (opcode2 == 0x1F || opcode2 == 0x28 || opcode2 == 0x29 || opcode2 == 0x10 ||
            opcode2 == 0x11 || opcode2 == 0x57 || opcode2 == 0xB6 || opcode2 == 0xB7 ||
            opcode2 == 0xBE || opcode2 == 0xBF) {
            DecodeModRm(code, offset, info);
            info.length = offset;
            return info;
        }
        if (opcode2 >= 0x80 && opcode2 <= 0x8F) { // Jcc rel32
            info.length = offset + 4;
            return info;
        }
        info.length = offset;
        return info;
    }

    // 1-byte opcodes without ModR/M
    if ((opcode >= 0x50 && opcode <= 0x5F) || opcode == 0x90 || opcode == 0xCC) {
        info.length = offset;
        return info;
    }

    // Direct jumps / calls
    if (opcode == 0xE8 || opcode == 0xE9) { // call/jmp rel32
        info.length = offset + 4;
        return info;
    }
    if (opcode == 0xEB || (opcode >= 0x70 && opcode <= 0x7F)) { // jmp/jcc rel8
        info.length = offset + 1;
        return info;
    }

    // Immediate to register
    if (opcode >= 0xB8 && opcode <= 0xBF) {
        info.length = offset + (hasRexW ? 8 : 4);
        return info;
    }
    if (opcode >= 0xB0 && opcode <= 0xB7) {
        info.length = offset + 1;
        return info;
    }

    // Standard ModR/M opcodes
    if (opcode == 0x88 || opcode == 0x89 || opcode == 0x8A || opcode == 0x8B ||
        opcode == 0x8D || opcode == 0x01 || opcode == 0x03 || opcode == 0x29 ||
        opcode == 0x2B || opcode == 0x31 || opcode == 0x33 || opcode == 0x39 ||
        opcode == 0x3B || opcode == 0x85) {
        DecodeModRm(code, offset, info);
        info.length = offset;
        return info;
    }

    // Group 1 immediate (0x80, 0x81, 0x82, 0x83)
    if (opcode == 0x80 || opcode == 0x81 || opcode == 0x82 || opcode == 0x83) {
        DecodeModRm(code, offset, info);
        offset += (opcode == 0x81 ? 4 : 1);
        info.length = offset;
        return info;
    }

    // Mov r/m, imm (0xC6, 0xC7)
    if (opcode == 0xC6 || opcode == 0xC7) {
        DecodeModRm(code, offset, info);
        offset += (opcode == 0xC7 ? 4 : 1);
        info.length = offset;
        return info;
    }

    // Group 5 (0xFF: call, jmp, inc, dec, push)
    if (opcode == 0xFF) {
        DecodeModRm(code, offset, info);
        info.length = offset;
        return info;
    }

    info.length = offset;
    return info;
}

void WriteIndirectJump(uint8_t* destination, void* targetAddress) {
    destination[0] = 0xFF;
    destination[1] = 0x25;
    destination[2] = 0x00;
    destination[3] = 0x00;
    destination[4] = 0x00;
    destination[5] = 0x00;
    std::memcpy(destination + 6, &targetAddress, sizeof(void*));
}

void* AllocateNear(void* target, size_t size, DWORD protect = PAGE_READWRITE) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t step = si.dwAllocationGranularity;
    if (step == 0) step = 0x10000;

    uintptr_t targetAddr = reinterpret_cast<uintptr_t>(target);
    uintptr_t minAddr = targetAddr > 0x70000000 ? targetAddr - 0x70000000 : 0x10000;
    uintptr_t maxAddr = targetAddr + 0x70000000;

    minAddr = (minAddr + step - 1) & ~(step - 1);

    // Try allocating nearby within ±2GB range
    for (uintptr_t addr = (targetAddr + step) & ~(step - 1); addr < maxAddr; addr += step) {
        void* mem = VirtualAlloc(reinterpret_cast<void*>(addr), size, MEM_COMMIT | MEM_RESERVE, protect);
        if (mem) return mem;
    }
    for (uintptr_t addr = (targetAddr > step ? targetAddr - step : 0) & ~(step - 1); addr > minAddr; addr -= step) {
        void* mem = VirtualAlloc(reinterpret_cast<void*>(addr), size, MEM_COMMIT | MEM_RESERVE, protect);
        if (mem) return mem;
    }

    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, protect);
}

} // anonymous namespace

FunctionDetour::FunctionDetour() = default;

FunctionDetour::~FunctionDetour() {
    // At termination the patch stays and the trampoline is never freed (see IsProcessTerminating).
    if (IsProcessTerminating()) return;
    Uninstall();
}

FunctionDetour::FunctionDetour(FunctionDetour&& other) noexcept
    : m_target(other.m_target),
      m_detour(other.m_detour),
      m_trampoline(other.m_trampoline),
      m_trampolineEntry(other.m_trampolineEntry),
      m_originalBytes(std::move(other.m_originalBytes)),
      m_hookSize(other.m_hookSize),
      m_installed(other.m_installed) {
    other.m_target = nullptr;
    other.m_detour = nullptr;
    other.m_trampoline = nullptr;
    other.m_trampolineEntry = nullptr;
    other.m_hookSize = 0;
    other.m_installed = false;
}

FunctionDetour& FunctionDetour::operator=(FunctionDetour&& other) noexcept {
    if (this != &other) {
        Uninstall();
        m_target = other.m_target;
        m_detour = other.m_detour;
        m_trampoline = other.m_trampoline;
        m_trampolineEntry = other.m_trampolineEntry;
        m_originalBytes = std::move(other.m_originalBytes);
        m_hookSize = other.m_hookSize;
        m_installed = other.m_installed;

        other.m_target = nullptr;
        other.m_detour = nullptr;
        other.m_trampoline = nullptr;
        other.m_trampolineEntry = nullptr;
        other.m_hookSize = 0;
        other.m_installed = false;
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

    // Resolve through incremental link table (ILT) jmp thunks (0xE9 rel32)
    while (*reinterpret_cast<const uint8_t*>(targetFunction) == 0xE9) {
        const int32_t rel = *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(targetFunction) + 1);
        targetFunction = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(targetFunction) + 5 + rel);
    }

    // Resolve through 6-byte indirect import jump thunks (FF 25 disp32)
    if (reinterpret_cast<const uint8_t*>(targetFunction)[0] == 0xFF &&
        reinterpret_cast<const uint8_t*>(targetFunction)[1] == 0x25) {
        const int32_t disp = *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(targetFunction) + 2);
        void** targetPtr = reinterpret_cast<void**>(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(targetFunction) + 6 + disp));
        if (targetPtr && *targetPtr) {
            targetFunction = *targetPtr;
        }
    }

    // Minimum hook size: 5 bytes for relative jump E9 rel32
    constexpr size_t kMinHookSize = 5;
    size_t copiedBytes = 0;
    const uint8_t* codePtr = reinterpret_cast<const uint8_t*>(targetFunction);
    std::vector<InstructionInfo> instructions;

    while (copiedBytes < kMinHookSize) {
        InstructionInfo info = DecodeInstruction(codePtr + copiedBytes);
        if (info.isRet) {
            LogError("FunctionDetour: Target function is too short (< 5 bytes) or contains ret before hook size.");
            return false;
        }
        if (info.length == 0) info.length = 1;
        instructions.push_back(info);
        copiedBytes += info.length;
    }

    m_hookSize = copiedBytes;
    m_target = targetFunction;
    m_detour = detourFunction;

    // Save original bytes for uninstallation
    m_originalBytes.resize(m_hookSize);
    std::memcpy(m_originalBytes.data(), targetFunction, m_hookSize);

    // Layout of trampoline:
    // Offset 0: Detour relay (14 bytes): jmp qword ptr [detourFunction]
    // Offset 16: Trampoline entry (original prologue instructions + RIP fixups)
    // Offset 16 + copiedBytes: Return relay (14 bytes): jmp qword ptr [targetFunction + m_hookSize]
    constexpr size_t kRelayOffset = 0;
    constexpr size_t kPrologueOffset = 16;
    size_t totalTrampolineSize = kPrologueOffset + m_hookSize + 16;

    m_trampoline = AllocateNear(targetFunction, totalTrampolineSize, PAGE_READWRITE);
    if (!m_trampoline) {
        LogError("FunctionDetour: Failed to allocate trampoline memory near target.");
        m_originalBytes.clear();
        m_hookSize = 0;
        return false;
    }

    uint8_t* trampBase = reinterpret_cast<uint8_t*>(m_trampoline);

    // 1. Write detour relay at offset 0
    WriteIndirectJump(trampBase + kRelayOffset, detourFunction);

    // 2. Write prologue instructions at offset 16
    m_trampolineEntry = trampBase + kPrologueOffset;
    uint8_t* trampPrologue = reinterpret_cast<uint8_t*>(m_trampolineEntry);

    size_t curOffset = 0;
    for (const auto& info : instructions) {
        std::memcpy(trampPrologue + curOffset, m_originalBytes.data() + curOffset, info.length);
        if (info.isRipRelative) {
            int32_t origDisp = 0;
            std::memcpy(&origDisp, m_originalBytes.data() + curOffset + info.dispOffset, sizeof(int32_t));
            uint8_t* targetData = const_cast<uint8_t*>(codePtr + curOffset + info.length) + origDisp;
            uint8_t* trampRip = trampPrologue + curOffset + info.length;
            int64_t newDisp = reinterpret_cast<intptr_t>(targetData) - reinterpret_cast<intptr_t>(trampRip);
            int32_t newDisp32 = static_cast<int32_t>(newDisp);
            std::memcpy(trampPrologue + curOffset + info.dispOffset, &newDisp32, sizeof(int32_t));
        }
        curOffset += info.length;
    }

    // 3. Write return relay back to targetFunction + m_hookSize
    uint8_t* resumeAddress = reinterpret_cast<uint8_t*>(targetFunction) + m_hookSize;
    WriteIndirectJump(trampPrologue + m_hookSize, resumeAddress);

    // Check near distance for 5-byte relative jump from targetFunction to detour relay
    int64_t relJumpDisp = reinterpret_cast<intptr_t>(m_trampoline) - (reinterpret_cast<intptr_t>(targetFunction) + 5);
    bool canUseRelJump = (relJumpDisp >= -0x7FFFFFFF && relJumpDisp <= 0x7FFFFFFF);

    if (!canUseRelJump && m_hookSize < 14) {
        LogError("FunctionDetour: Trampoline is out of 32-bit displacement range and hook size < 14 bytes.");
        VirtualFree(m_trampoline, 0, MEM_RELEASE);
        m_trampoline = nullptr;
        m_trampolineEntry = nullptr;
        m_originalBytes.clear();
        m_hookSize = 0;
        return false;
    }

    // Transition trampoline memory from PAGE_READWRITE to PAGE_EXECUTE_READ (W^X / DEP compliance)
    DWORD trampOldProtect = 0;
    if (!VirtualProtect(m_trampoline, totalTrampolineSize, PAGE_EXECUTE_READ, &trampOldProtect)) {
        LogError("FunctionDetour: Failed to transition trampoline memory to PAGE_EXECUTE_READ.");
        VirtualFree(m_trampoline, 0, MEM_RELEASE);
        m_trampoline = nullptr;
        m_trampolineEntry = nullptr;
        m_originalBytes.clear();
        m_hookSize = 0;
        return false;
    }

    // Overwrite targetFunction with jump
    DWORD oldProtect = 0;
    if (!VirtualProtect(targetFunction, m_hookSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogError("FunctionDetour: Failed to change memory protection for target.");
        VirtualFree(m_trampoline, 0, MEM_RELEASE);
        m_trampoline = nullptr;
        m_trampolineEntry = nullptr;
        m_originalBytes.clear();
        m_hookSize = 0;
        return false;
    }

    uint8_t* targetCode = reinterpret_cast<uint8_t*>(targetFunction);
    if (canUseRelJump) {
        targetCode[0] = 0xE9; // JMP rel32
        int32_t disp32 = static_cast<int32_t>(relJumpDisp);
        std::memcpy(targetCode + 1, &disp32, sizeof(int32_t));
        for (size_t i = 5; i < m_hookSize; ++i) {
            targetCode[i] = 0x90; // NOP padding
        }
    } else {
        WriteIndirectJump(targetCode, detourFunction);
        for (size_t i = 14; i < m_hookSize; ++i) {
            targetCode[i] = 0x90;
        }
    }

    VirtualProtect(targetFunction, m_hookSize, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), targetFunction, m_hookSize);
    FlushInstructionCache(GetCurrentProcess(), m_trampoline, totalTrampolineSize);

    m_installed = true;
    LogInfo("FunctionDetour: Successfully installed detour (" + std::to_string(m_hookSize) + " bytes).");
    return true;
}

bool FunctionDetour::Uninstall() {
    if (!m_installed || !m_target || m_originalBytes.empty()) {
        return false;
    }

    DWORD oldProtect = 0;
    if (VirtualProtect(m_target, m_hookSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        std::memcpy(m_target, m_originalBytes.data(), m_hookSize);
        VirtualProtect(m_target, m_hookSize, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), m_target, m_hookSize);
    }

    if (m_trampoline) {
        VirtualFree(m_trampoline, 0, MEM_RELEASE);
        m_trampoline = nullptr;
        m_trampolineEntry = nullptr;
    }

    m_target = nullptr;
    m_detour = nullptr;
    m_originalBytes.clear();
    m_hookSize = 0;
    m_installed = false;

    LogInfo("FunctionDetour: Successfully uninstalled detour.");
    return true;
}

} // namespace fs25vr
