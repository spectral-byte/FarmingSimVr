#pragma once

#include <memory>
#include "vr_bridge.h"
#include "core/detour.h"

namespace fs25vr {

// The Luau release the Injector links, and which FS25 (GIANTS Engine 10) embeds. See the ADR-0002 amendment.
// Must equal GIT_TAG in CMakeLists.txt (FS25VR_LUAU_TAG); a test enforces it.
constexpr const char* kExpectedLuauVersion = "0.662";

// Fast-flag names the engine keeps as NUL-terminated strings in .rdata. Finding all of them identifies
// the embedded Luau release, for both halves of it:
// - Ast/Compiler flags pin the front end to 0.662 exactly (0.660-0.661 lack LuauFixDoBlockEndLocation,
//   0.663 and later drop LuauCompileMathLerp and LuauAllowFragmentParsing).
// - VM flags pin the VM, which is what the Injector's API calls touch, to 0.661-0.662 (0.660 and earlier
//   lack LuauLibWhereErrorAutoreserve and LuauStringFormatFixC, 0.663 and later drop LuauMathLerp and
//   LuauVector2Constructor). VM/ is identical between 0.661 and 0.662.
// LuauBufferBitMethods2 is deliberately absent: it is a 0.662 VM flag, but the FS25 executable lacks it.
constexpr const char* kHostLuauFingerprint[] = {
    // Ast / Compiler
    "LuauStoreCSTData",
    "LuauFixDoBlockEndLocation",
    "ParserNoErrorLimit",
    "LuauCompileMathLerp",
    "LuauAllowFragmentParsing",
    // VM
    "LuauLibWhereErrorAutoreserve",
    "LuauStringFormatFixC",
    "LuauMathLerp",
    "LuauVector2Constructor",
};

// True when every fingerprint marker appears in the section as a whole NUL-delimited string.
bool HostEmbedsMatchingLuau(const uint8_t* rdataBase, size_t rdataSize);

class LuauInterceptor {
public:
    // A null bridge leaves the interceptor with nothing to inject; there is no singleton fallback.
    // Production wiring comes from InjectorRuntime.
    explicit LuauInterceptor(std::shared_ptr<VRBridge> bridge = nullptr);
    ~LuauInterceptor();

    // Attach installs the detour only while a live InjectorRuntime owns the interceptor, so the hook
    // can always find the interceptor that holds its original.
    void SetOwnedByRuntime(bool owned) { m_ownedByRuntime = owned; }
    bool Attach(void* targetQueryFunction);
    bool AttachToEngine();
    // The part of AttachToEngine after the host's sections are located: refuses a Luau mismatch, then
    // resolves the head-tracking query and attaches to it. Returns true only when the detour is installed.
    bool AttachToHostSections(const uint8_t* rdataBase, size_t rdataSize, const uint8_t* textBase, size_t textSize);
    void Detach();

    bool IsIntercepted() const { return m_activeState != nullptr; }
    lua_State* GetActiveState() const { return m_activeState; }

    int HandleQuery(lua_State* L);

    // Hook entry point: forwards to the interceptor that installed the detour.
    static int DetourEntryPoint(lua_State* L);
    static void* FindQueryFunctionInText(const uint8_t* textBase, size_t textSize, const uint8_t* stringAddr);

    FunctionDetour& GetDetour() { return m_detour; }

private:
    std::shared_ptr<VRBridge> m_bridge;
    FunctionDetour m_detour;
    lua_State* m_activeState = nullptr;
    bool m_bridgeInjected = false;
    bool m_ownedByRuntime = false;
};

} // namespace fs25vr
