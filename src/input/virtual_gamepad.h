#pragma once

#include "core/detour.h"
#include "openxr/openxr_types.h"
#include <windows.h>
#include <xinput.h>
#include <array>
#include <atomic>
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
// The pad is wanted from the start (the Companion Mod's saved setting can turn it off, and tells this class through VRBridge
// with Configure, as it does the Motion Controller switch). The XInput hooks are installed once a live InjectorRuntime owns
// the pad, it is wanted, and the OpenXR session is active with its Motion Controller action set: the main menu and the
// Controls screen reached from it already list the pad, and a game in Pass-Through Mode (no session) has no hook at all.
//
// Threads: the game polls XInput from a thread of its own. That thread never calls OpenXR: the render thread publishes
// an immutable snapshot of both hands at the start of every OpenXR frame (and once at start) and the poll reads only that,
// under the pad's mutex.
class VirtualGamepad {
public:
    explicit VirtualGamepad(std::shared_ptr<OpenXrManager> openXrManager);
    ~VirtualGamepad();

    VirtualGamepad(const VirtualGamepad&) = delete;
    VirtualGamepad& operator=(const VirtualGamepad&) = delete;

    // `enabled`: the vrVirtualGamepad setting (the pad is wanted). `live`: Motion Controller input is on
    // (vrMotionControllers); while it is off the pad stays present but reads neutral.
    void Configure(bool enabled, bool live);

    // The Companion Mod's own uses of the Motion Controllers (a menu, a Click Point click, the Steering Wheel): while it
    // uses an input, that input reads as released or centred on the pad, so a use never also fires a binding. One bit set
    // per input of the hand (0 = left, 1 = right), sent by the Companion Mod each update; the Companion Mod applies the
    // held-input rule before it asks, so an input already held for a binding is simply not in the set.
    static constexpr uint32_t kInputTrigger = 1u << 0;
    static constexpr uint32_t kInputGrip = 1u << 1;
    static constexpr uint32_t kInputStick = 1u << 2;        // both axes
    static constexpr uint32_t kInputStickClick = 1u << 3;
    static constexpr uint32_t kInputLowerButton = 1u << 4;  // A (right) / X (left)
    static constexpr uint32_t kInputUpperButton = 1u << 5;  // B (right) / Y (left)
    static constexpr uint32_t kInputMenuButton = 1u << 6;   // Start (left hand only)
    void SetInputsInUse(uint32_t hand, uint32_t inputs);

    struct Status {
        bool present = false;
        int slot = -1;                      // 0..3 while present
        const char* reason = "off";         // why there is no pad, or "present"
        std::array<bool, 2> gripHeld{};     // per hand: the grip as the pad reads it (LB / RB), with its hysteresis
    };
    // Whether the pad exists now and in which slot. It exists while it is enabled, the OpenXR session is active
    // with its Motion Controller action set attached, the XInput hooks are installed (when a runtime owns the pad), and a
    // slot is free. A real pad that plugs into the pad's slot makes the pad give the slot up and take a free one (looked
    // at about once a second).
    Status GetStatus();

    // The answers the XInput hooks route the game's calls to. Calls for any slot but the pad's go to the real
    // function (ERROR_DEVICE_NOT_CONNECTED when there is none to call).
    DWORD GetState(DWORD userIndex, XINPUT_STATE* state);
    DWORD GetCapabilities(DWORD userIndex, DWORD flags, XINPUT_CAPABILITIES* capabilities);
    DWORD SetState(DWORD userIndex, XINPUT_VIBRATION* vibration);

    // The Companion Mod played a pulse of its own on `hand` (a Click Point tick, a Wheel Grab pulse) lasting
    // `durationMs`: the game's rumble stays off that hand while it plays, then resumes.
    void NoteCompanionPulse(uint32_t hand, double durationMs);

    // Detours are installed only while a live InjectorRuntime owns the pad, so the hooks can always find the pad
    // that holds their originals. They are put in when the pad is wanted and the OpenXR session has its action set (checked
    // when ownership begins, when the setting changes and at the start of every OpenXR frame).
    void SetOwnedByRuntime(bool owned);
    bool AreHooksInstalled() const { return m_detourGetState.IsInstalled(); }

    // Tests only: replaces the question "does a real pad occupy this slot?", which is otherwise asked of the real
    // XInputGetState (and is "no" for every slot while the hooks are not installed).
    using RealPadProbe = std::function<bool(DWORD slot)>;
    void SetRealPadProbeForTesting(RealPadProbe probe);

    // Tests only: the clock (milliseconds) that times the RS press sent when a click is let go, which is
    // otherwise the real steady clock; tests drive it with the game clock. It also times the game's rumble against the
    // Companion Mod's own pulses.
    using ClockMs = std::function<double()>;
    void SetClockForTesting(ClockMs clock);

    // Tests only: the originals of the three hooked functions as the hook layer holds them, to test a call that reaches no pad.
    using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using GetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    using SetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    static void SetHookOriginalsForTesting(GetStateFn getState, GetCapabilitiesFn getCapabilities, SetStateFn setState);

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

    // The game's rumble plays on the Motion Controllers as short pulses, renewed on every OpenXR frame while a
    // motor runs (a pulse that is not renewed dies out by itself), at a fixed fraction of a gamepad's strength.
    static constexpr float kRumbleScale = 0.5f;
    static constexpr double kRumblePulseMs = 50.0;

    // The right-stick D-pad chord (ADR-0028): with the right stick click held, a push of that stick is the D-pad
    // and the stick reads centred. The thresholds are the ones the Companion Mod's own Snap Turn push used before
    // that mapping was removed (0.7 / 0.4).
    static constexpr float kStickEngage = 0.7f;
    static constexpr float kStickRelease = 0.4f;

    // A click let go without a push is reported as RS pressed for this long, so the game sees a press.
    static constexpr double kRsReleasePulseMs = 100.0;

    // How often the pad looks again whether a real pad has plugged into its slot (a real XInputGetState call).
    static constexpr double kSlotRecheckMs = 1000.0;

    enum class Push { None, Up, Down, Left, Right };

    static VirtualGamepad* ForHook(FunctionDetour VirtualGamepad::*detour);

    void OnFrameBegin();
    void RefreshSnapshot();
    void EnsureHooks();
    void PlayRumble();

    // The ones below run with m_mutex held.
    const char* SessionProblemLocked() const;
    bool AllHooksInstalledLocked() const;
    Status EvaluateLocked();
    bool IsPadSlotLocked(DWORD userIndex);
    bool RealPadPresentLocked(DWORD slot);
    XINPUT_GAMEPAD BuildGamepadLocked();
    void ApplyRightStickChordLocked(const HandState& right, XINPUT_GAMEPAD& pad);
    void ResetRightStickChordLocked();

    void InstallHooksLocked();
    void RollBackHooksLocked();
    void UninstallHooksLocked();

    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::mutex m_mutex;
    bool m_enabled = true;
    bool m_live = true;
    bool m_owned = false;
    bool m_hooksFailed = false;      // an install was tried and failed: not tried again until the setting or the owner changes
    bool m_snapshotTaken = false;
    std::array<HandState, 2> m_snapshot{};  // both hands as the last OpenXR frame began; all the poll reads
    double m_nextSlotCheckMs = 0.0;
    int m_slot = kNoSlot;
    const char* m_reason = "off";
    bool m_loggedSlotsFull = false;

    XINPUT_GAMEPAD m_lastGamepad{};
    DWORD m_packetNumber = 0;
    std::array<bool, 2> m_gripDown{};
    std::array<uint32_t, 2> m_inUse{};
    std::array<WORD, 2> m_motorSpeed{};            // per hand: the left motor plays on the left hand, the right on the right
    std::array<double, 2> m_companionPulseUntilMs{}; // per hand, on the pad's clock

    bool m_rightClickDown = false;  // the right stick click was down at the last look
    Push m_rightPush = Push::None;  // the D-pad direction the chord reports now
    bool m_rightClickPushed = false;  // this click has pushed: it will never report RS
    double m_rsPulseUntilMs = 0.0;  // RS is reported pressed until this time (a click let go without a push)
    ClockMs m_clock;

    double NowMs() const;
    RealPadProbe m_realPadProbe;
    FunctionDetour m_detourGetState;
    FunctionDetour m_detourGetCapabilities;
    FunctionDetour m_detourSetState;
};

} // namespace fs25vr

