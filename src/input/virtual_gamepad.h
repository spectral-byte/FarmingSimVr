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
#include <span>

namespace fs25vr {

class OpenXrManager;

class VirtualGamepad {
public:
    explicit VirtualGamepad(std::shared_ptr<OpenXrManager> openXrManager);
    ~VirtualGamepad();

    VirtualGamepad(const VirtualGamepad&) = delete;
    VirtualGamepad& operator=(const VirtualGamepad&) = delete;

    void Configure(bool enabled, bool live);

    static constexpr uint32_t kInputTrigger = 1u << 0;
    static constexpr uint32_t kInputGrip = 1u << 1;
    static constexpr uint32_t kInputStick = 1u << 2;        // both axes
    static constexpr uint32_t kInputStickClick = 1u << 3;
    static constexpr uint32_t kInputLowerButton = 1u << 4;  // A (right) / X (left)
    static constexpr uint32_t kInputUpperButton = 1u << 5;  // B (right) / Y (left)
    static constexpr uint32_t kInputMenuButton = 1u << 6;   // Start (left hand only)
    void SetInputsInUse(uint32_t hand, uint32_t inputs);

    static std::span<const char* const> Reasons();

    struct Status {
        bool present = false;
        int slot = -1;                      // 0..3 while present
        const char* reason = "off";         // why there is no pad, or "present"
        std::array<bool, 2> gripHeld{};     // per hand: the grip as the pad reads it (LB / RB), with its hysteresis
    };
    Status GetStatus();

    DWORD GetState(DWORD userIndex, XINPUT_STATE* state);
    DWORD GetCapabilities(DWORD userIndex, DWORD flags, XINPUT_CAPABILITIES* capabilities);
    DWORD SetState(DWORD userIndex, XINPUT_VIBRATION* vibration);

    void NoteCompanionPulse(uint32_t hand, double durationMs);

    void SetOwnedByRuntime(bool owned);
    bool AreHooksInstalled() const { return m_detourGetState.IsInstalled(); }

    using RealPadProbe = std::function<bool(DWORD slot)>;
    void SetRealPadProbeForTesting(RealPadProbe probe);

    using ClockMs = std::function<double()>;
    void SetClockForTesting(ClockMs clock);

    using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using GetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    using SetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    static void SetHookOriginalsForTesting(GetStateFn getState, GetCapabilitiesFn getCapabilities, SetStateFn setState);

    static DWORD WINAPI Intercept_XInputGetState(DWORD userIndex, XINPUT_STATE* state);
    static DWORD WINAPI Intercept_XInputGetCapabilities(DWORD userIndex, DWORD flags, XINPUT_CAPABILITIES* capabilities);
    static DWORD WINAPI Intercept_XInputSetState(DWORD userIndex, XINPUT_VIBRATION* vibration);

private:
    static constexpr int kSlotCount = 4;
    static constexpr int kNoSlot = -1;

    static constexpr float kGripPress = 0.6f;
    static constexpr float kGripRelease = 0.4f;

    static constexpr float kRumbleScale = 0.5f;
    static constexpr double kRumblePulseMs = 50.0;

    static constexpr float kStickEngage = 0.7f;
    static constexpr float kStickRelease = 0.4f;

    static constexpr double kRsReleasePulseMs = 100.0;

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
    Status Evaluate(std::unique_lock<std::mutex>& lock);
    bool IsPadSlot(std::unique_lock<std::mutex>& lock, DWORD userIndex);
    static bool ProbeRealPad(const RealPadProbe& probe, GetStateFn realGetState, DWORD slot);
    XINPUT_GAMEPAD BuildGamepadLocked();
    void ApplyRightStickChordLocked(const HandState& right, XINPUT_GAMEPAD& pad, bool stickClickSuppressed);
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
    GetStateFn m_targetGetState = nullptr;
    GetCapabilitiesFn m_targetGetCapabilities = nullptr;
    SetStateFn m_targetSetState = nullptr;
};

}

