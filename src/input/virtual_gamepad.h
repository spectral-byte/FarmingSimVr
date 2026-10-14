#pragma once

#include "core/detour.h"
#include <windows.h>
#include <xinput.h>
#include <array>
#include <functional>
#include <memory>
#include <mutex>

namespace fs25vr {

class OpenXrManager;

// The Virtual Gamepad (ADR-0028): the Motion Controllers' triggers, grips, thumbsticks and buttons presented to
// the game as an Xbox pad it reads through XInput. The Injector answers the game's XInputGetState,
// XInputGetCapabilities and XInputSetState for one slot, the highest no real pad occupies, and passes every
// other slot through to the real xinput1_4.dll. The pad is built from the per-hand raw inputs the OpenXR action
// set already delivers (ADR-0025); what it feeds is fixed here, what each input does is bound in the game's own
// Controls screen.
//
// The Companion Mod owns the saved setting and the Motion Controller switch and tells this class through VRBridge
// (Configure). The XInput hooks are installed only while the pad is enabled and a live InjectorRuntime owns it,
// so a player who never turns it on has no hook in the game.
class VirtualGamepad {
public:
    explicit VirtualGamepad(std::shared_ptr<OpenXrManager> openXrManager);
    ~VirtualGamepad();

    VirtualGamepad(const VirtualGamepad&) = delete;
    VirtualGamepad& operator=(const VirtualGamepad&) = delete;

    // `enabled`: the vrVirtualGamepad setting (the pad is wanted). `live`: Motion Controller input is on
    // (vrMotionControllers); while it is off the pad stays present but reads neutral.
    void Configure(bool enabled, bool live);

    struct Status {
        bool present = false;
        int slot = -1;                      // 0..3 while present
        const char* reason = "off";         // why there is no pad, or "present"
    };
    // Whether the pad exists now and in which slot. It exists while it is enabled, the OpenXR session is active
    // with its Motion Controller action set attached, and a slot is free.
    Status GetStatus();

    // The answers the XInput hooks route the game's calls to. Calls for any slot but the pad's go to the real
    // function (ERROR_DEVICE_NOT_CONNECTED when there is none to call).
    DWORD GetState(DWORD userIndex, XINPUT_STATE* state);
    DWORD GetCapabilities(DWORD userIndex, DWORD flags, XINPUT_CAPABILITIES* capabilities);
    DWORD SetState(DWORD userIndex, XINPUT_VIBRATION* vibration);

    // Detours are installed only while a live InjectorRuntime owns the pad, so the hooks can always find the pad
    // that holds their originals.
    void SetOwnedByRuntime(bool owned);
    bool AreHooksInstalled() const { return m_detourGetState.IsInstalled(); }

    // Tests only: replaces the question "does a real pad occupy this slot?", which is otherwise asked of the real
    // XInputGetState (and is "no" for every slot while the hooks are not installed).
    using RealPadProbe = std::function<bool(DWORD slot)>;
    void SetRealPadProbeForTesting(RealPadProbe probe);

    // Detour entry points, acting for the pad of the live runtime that installed them.
    static DWORD WINAPI Intercept_XInputGetState(DWORD userIndex, XINPUT_STATE* state);
    static DWORD WINAPI Intercept_XInputGetCapabilities(DWORD userIndex, DWORD flags, XINPUT_CAPABILITIES* capabilities);
    static DWORD WINAPI Intercept_XInputSetState(DWORD userIndex, XINPUT_VIBRATION* vibration);

private:
    static constexpr int kSlotCount = 4;
    static constexpr int kNoSlot = -1;

    // Grips are digital on the pad (LB / RB): pressed above this, released below the other.
    static constexpr float kGripPress = 0.6f;
    static constexpr float kGripRelease = 0.4f;

    static VirtualGamepad* ForHook(FunctionDetour VirtualGamepad::*detour);

    // The three below run with m_mutex held.
    Status EvaluateLocked();
    bool IsPadSlotLocked(DWORD userIndex);
    bool RealPadPresentLocked(DWORD slot);
    XINPUT_GAMEPAD BuildGamepadLocked();

    void InstallHooksLocked();
    void UninstallHooksLocked();

    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::mutex m_mutex;
    bool m_enabled = false;
    bool m_live = true;
    bool m_owned = false;
    int m_slot = kNoSlot;
    const char* m_reason = "off";
    bool m_loggedSlotsFull = false;

    XINPUT_GAMEPAD m_lastGamepad{};
    DWORD m_packetNumber = 0;
    std::array<bool, 2> m_gripDown{};

    RealPadProbe m_realPadProbe;
    FunctionDetour m_detourGetState;
    FunctionDetour m_detourGetCapabilities;
    FunctionDetour m_detourSetState;
};

} // namespace fs25vr

