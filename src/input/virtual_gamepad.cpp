#include "input/virtual_gamepad.h"

#include "core/dll_lifecycle.h"
#include "core/injector_runtime.h"
#include "core/logger.h"
#include "luau/vr_bridge.h"
#include "openxr/openxr_manager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

namespace fs25vr {

namespace {

constexpr const char* kReasonOff = "off";
constexpr const char* kReasonNoSession = "no OpenXR session";
constexpr const char* kReasonNoActions = "the OpenXR runtime refused the Motion Controller action set";
constexpr const char* kReasonSlotsFull = "all four XInput slots hold real pads";
constexpr const char* kReasonPresent = "present";

BYTE ToTrigger(float value) {
    return static_cast<BYTE>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

SHORT ToAxis(float value) {
    return static_cast<SHORT>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
}

bool Engaged(float axis, float other, float engage) {
    return std::fabs(axis) >= engage && std::fabs(axis) > std::fabs(other);
}

} // namespace

VirtualGamepad::VirtualGamepad(std::shared_ptr<OpenXrManager> openXrManager)
    : m_openXrManager(std::move(openXrManager)) {
    if (m_openXrManager) m_openXrManager->SetFrameBeginObserver(this, [this] { PlayRumble(); });
}

VirtualGamepad::~VirtualGamepad() {
    if (IsProcessTerminating()) return;
    if (m_openXrManager) m_openXrManager->ClearFrameBeginObserver(this);
    std::lock_guard<std::mutex> lock(m_mutex);
    UninstallHooksLocked();
}

VirtualGamepad* VirtualGamepad::ForHook(FunctionDetour VirtualGamepad::*detour) {
    InjectorRuntime* runtime = InjectorRuntime::ForHook([detour](InjectorRuntime& r) {
        return (r.GetVrBridge().GetVirtualGamepad().*detour).IsInstalled();
    });
    return runtime ? &runtime->GetVrBridge().GetVirtualGamepad() : nullptr;
}

void VirtualGamepad::Configure(bool enabled, bool live) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_live = live;
    if (enabled == m_enabled) return;
    m_enabled = enabled;
    if (enabled) {
        InstallHooksLocked();
    } else {
        m_loggedSlotsFull = false;
    }
    EvaluateLocked();
    if (!enabled) UninstallHooksLocked();
}

void VirtualGamepad::SetInputsInUse(uint32_t hand, uint32_t inputs) {
    if (hand >= m_inUse.size()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_inUse[hand] = inputs;
}

void VirtualGamepad::SetOwnedByRuntime(bool owned) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_owned = owned;
    if (owned && m_enabled) {
        InstallHooksLocked();
    } else if (!owned) {
        UninstallHooksLocked();
    }
}

void VirtualGamepad::SetClockForTesting(ClockMs clock) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clock = std::move(clock);
}

double VirtualGamepad::NowMs() const {
    if (m_clock) return m_clock();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void VirtualGamepad::SetRealPadProbeForTesting(RealPadProbe probe) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_realPadProbe = std::move(probe);
}

VirtualGamepad::Status VirtualGamepad::GetStatus() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return EvaluateLocked();
}

bool VirtualGamepad::RealPadPresentLocked(DWORD slot) {
    if (m_realPadProbe) return m_realPadProbe(slot);
    using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    const auto real = reinterpret_cast<GetStateFn>(m_detourGetState.GetOriginal());
    if (!real) return false;
    XINPUT_STATE ignored{};
    return real(slot, &ignored) == ERROR_SUCCESS;
}

VirtualGamepad::Status VirtualGamepad::EvaluateLocked() {
    const char* reason = kReasonPresent;
    if (!m_enabled) {
        reason = kReasonOff;
    } else if (!m_openXrManager || !m_openXrManager->IsActive() || !m_openXrManager->HasSession()) {
        reason = kReasonNoSession;
    } else if (!m_openXrManager->HasMotionControllerActions()) {
        reason = kReasonNoActions;
    }

    if (reason == kReasonPresent && m_slot == kNoSlot) {
        // The pad appears: the highest slot no real pad occupies, so a real Xbox pad keeps the first one.
        for (int slot = kSlotCount - 1; slot >= 0; --slot) {
            if (!RealPadPresentLocked(static_cast<DWORD>(slot))) {
                m_slot = slot;
                m_loggedSlotsFull = false;
                LogInfo("VirtualGamepad: appeared in XInput slot " + std::to_string(slot));
                break;
            }
        }
        if (m_slot == kNoSlot) {
            reason = kReasonSlotsFull;
            if (!m_loggedSlotsFull) {
                m_loggedSlotsFull = true;
                LogWarn("VirtualGamepad: not created, all four XInput slots hold real pads.");
            }
        }
    } else if (reason != kReasonPresent && m_slot != kNoSlot) {
        LogInfo(std::string("VirtualGamepad: removed from XInput slot ") + std::to_string(m_slot) + " (" + reason + ")");
        m_slot = kNoSlot;
        m_gripDown.fill(false);
        m_motorSpeed.fill(0);

        ResetRightStickChordLocked();
    }

    m_reason = (m_slot != kNoSlot) ? kReasonPresent : reason;
    Status status;
    status.present = (m_slot != kNoSlot);
    status.slot = m_slot;
    status.reason = m_reason;
    return status;
}

bool VirtualGamepad::IsPadSlotLocked(DWORD userIndex) {
    const Status status = EvaluateLocked();
    return status.present && static_cast<DWORD>(status.slot) == userIndex;
}

XINPUT_GAMEPAD VirtualGamepad::BuildGamepadLocked() {
    XINPUT_GAMEPAD pad{};
    if (!m_live) {
        m_gripDown.fill(false);
        ResetRightStickChordLocked();
        return pad;
    }
    // Each physical control on each hand is its own pad input (ADR-0028). Back is never fed; the D-pad comes only from the right stick chord below.
    for (uint32_t hand = OpenXrInput::kLeft; hand <= OpenXrInput::kRight; ++hand) {
        HandState state = m_openXrManager->GetHandState(hand);
        if (!state.tracked) {
            m_gripDown[hand] = false;
            if (hand == OpenXrInput::kRight) ResetRightStickChordLocked();
            continue;
        }
        const bool left = (hand == OpenXrInput::kLeft);
        m_gripDown[hand] = state.grip >= (m_gripDown[hand] ? kGripRelease : kGripPress);

        // An input the Companion Mod is using reads as released (the grip above keeps tracking its hysteresis).
        const uint32_t used = m_inUse[hand];
        if (used & kInputTrigger) state.trigger = 0.0f;
        if (used & kInputStick) state.thumbX = state.thumbY = 0.0f;
        if (used & kInputStickClick) state.thumbClick = false;
        if (used & kInputLowerButton) state.buttonLower = false;
        if (used & kInputUpperButton) state.buttonUpper = false;
        if (used & kInputMenuButton) state.menu = false;

        (left ? pad.bLeftTrigger : pad.bRightTrigger) = ToTrigger(state.trigger);
        (left ? pad.sThumbLX : pad.sThumbRX) = ToAxis(state.thumbX);
        (left ? pad.sThumbLY : pad.sThumbRY) = ToAxis(state.thumbY);
        if (m_gripDown[hand] && !(used & kInputGrip)) pad.wButtons |= left ? XINPUT_GAMEPAD_LEFT_SHOULDER : XINPUT_GAMEPAD_RIGHT_SHOULDER;
        if (state.thumbClick) pad.wButtons |= left ? XINPUT_GAMEPAD_LEFT_THUMB : XINPUT_GAMEPAD_RIGHT_THUMB;
        // The letters on Touch controllers: X / Y on the left hand, A / B on the right.
        if (state.buttonLower) pad.wButtons |= left ? XINPUT_GAMEPAD_X : XINPUT_GAMEPAD_A;
        if (state.buttonUpper) pad.wButtons |= left ? XINPUT_GAMEPAD_Y : XINPUT_GAMEPAD_B;
        if (left && state.menu) pad.wButtons |= XINPUT_GAMEPAD_START;
        if (!left) ApplyRightStickChordLocked(state, pad);
    }
    return pad;
}

void VirtualGamepad::ResetRightStickChordLocked() {
    m_rightClickDown = false;
    m_rightPush = Push::None;
    m_rightClickPushed = false;
    m_rsPulseUntilMs = 0.0;
}

// With the right stick click held, the stick is the D-pad (the direction of the dominant axis, engaged at
// kStickEngage and held down to kStickRelease so a wobble between them neither drops nor flips it) and reads
// centred; RS is never reported while the click is down (ADR-0028).
void VirtualGamepad::ApplyRightStickChordLocked(const HandState& right, XINPUT_GAMEPAD& pad) {
    const bool clickDown = right.thumbClick;
    if (!clickDown) {
        // A click let go without a push is an RS press, sent now for a short fixed time.
        if (m_rightClickDown && !m_rightClickPushed) m_rsPulseUntilMs = NowMs() + kRsReleasePulseMs;
        m_rightClickDown = false;
        m_rightPush = Push::None;
        m_rightClickPushed = false;
        if (NowMs() < m_rsPulseUntilMs) pad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
        return;
    }
    if (!m_rightClickDown) {
        m_rightClickPushed = false;
        m_rsPulseUntilMs = 0.0;
    }
    m_rightClickDown = true;

    const float x = right.thumbX;
    const float y = right.thumbY;
    if (Engaged(x, y, kStickEngage)) {
        m_rightPush = x > 0.0f ? Push::Right : Push::Left;
    } else if (Engaged(y, x, kStickEngage)) {
        m_rightPush = y > 0.0f ? Push::Up : Push::Down;
    } else if (m_rightPush != Push::None) {
        const bool horizontal = (m_rightPush == Push::Left || m_rightPush == Push::Right);
        const float along = horizontal ? x : y;
        const bool positive = (m_rightPush == Push::Right || m_rightPush == Push::Up);
        if (std::fabs(along) < kStickRelease || (along > 0.0f) != positive) m_rightPush = Push::None;
    }

    switch (m_rightPush) {
        case Push::Up: pad.wButtons |= XINPUT_GAMEPAD_DPAD_UP; break;
        case Push::Down: pad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN; break;
        case Push::Left: pad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT; break;
        case Push::Right: pad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT; break;
        case Push::None: break;
    }
    if (m_rightPush != Push::None) m_rightClickPushed = true;
    pad.sThumbRX = 0;
    pad.sThumbRY = 0;
    pad.wButtons &= static_cast<WORD>(~XINPUT_GAMEPAD_RIGHT_THUMB);
}

DWORD VirtualGamepad::GetState(DWORD userIndex, XINPUT_STATE* state) {
    using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    std::unique_lock<std::mutex> lock(m_mutex);
    if (IsPadSlotLocked(userIndex)) {
        if (!state) return ERROR_BAD_ARGUMENTS;
        const XINPUT_GAMEPAD pad = BuildGamepadLocked();
        if (std::memcmp(&pad, &m_lastGamepad, sizeof(pad)) != 0) {
            m_lastGamepad = pad;
            ++m_packetNumber;
        }
        state->dwPacketNumber = m_packetNumber;
        state->Gamepad = pad;
        return ERROR_SUCCESS;
    }
    const auto real = reinterpret_cast<GetStateFn>(m_detourGetState.GetOriginal());
    lock.unlock();
    return real ? real(userIndex, state) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD VirtualGamepad::GetCapabilities(DWORD userIndex, DWORD flags, XINPUT_CAPABILITIES* capabilities) {
    using GetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    std::unique_lock<std::mutex> lock(m_mutex);
    if (IsPadSlotLocked(userIndex)) {
        if (!capabilities) return ERROR_BAD_ARGUMENTS;
        std::memset(capabilities, 0, sizeof(*capabilities));
        capabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
        capabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        // What a wired Xbox pad reports: every button but the guide button, full-range triggers and sticks.
        capabilities->Gamepad.wButtons = 0xF3FF;
        capabilities->Gamepad.bLeftTrigger = 255;
        capabilities->Gamepad.bRightTrigger = 255;
        capabilities->Gamepad.sThumbLX = -64;
        capabilities->Gamepad.sThumbLY = -64;
        capabilities->Gamepad.sThumbRX = -64;
        capabilities->Gamepad.sThumbRY = -64;
        capabilities->Vibration.wLeftMotorSpeed = 255;
        capabilities->Vibration.wRightMotorSpeed = 255;
        return ERROR_SUCCESS;
    }
    const auto real = reinterpret_cast<GetCapabilitiesFn>(m_detourGetCapabilities.GetOriginal());
    lock.unlock();
    return real ? real(userIndex, flags, capabilities) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD VirtualGamepad::SetState(DWORD userIndex, XINPUT_VIBRATION* vibration) {
    using SetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    std::unique_lock<std::mutex> lock(m_mutex);
    if (IsPadSlotLocked(userIndex)) {
        // Played from the next OpenXR frame on (PlayRumble); the left motor is the left hand's.
        m_motorSpeed[OpenXrInput::kLeft] = vibration ? vibration->wLeftMotorSpeed : 0;
        m_motorSpeed[OpenXrInput::kRight] = vibration ? vibration->wRightMotorSpeed : 0;
        return ERROR_SUCCESS;
    }
    const auto real = reinterpret_cast<SetStateFn>(m_detourSetState.GetOriginal());
    lock.unlock();
    return real ? real(userIndex, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

void VirtualGamepad::NoteCompanionPulse(uint32_t hand, double durationMs) {
    if (hand >= m_companionPulseUntilMs.size()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_companionPulseUntilMs[hand] = NowMs() + durationMs;
}

void VirtualGamepad::PlayRumble() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EvaluateLocked().present || !m_live) return;
    const double now = NowMs();
    for (uint32_t hand = OpenXrInput::kLeft; hand <= OpenXrInput::kRight; ++hand) {
        if (m_motorSpeed[hand] == 0 || now < m_companionPulseUntilMs[hand]) continue;
        const float strength = kRumbleScale * static_cast<float>(m_motorSpeed[hand]) / 65535.0f;
        m_openXrManager->PulseHaptic(hand, kRumblePulseMs / 1000.0, strength);
    }
}

void VirtualGamepad::InstallHooksLocked() {
    if (!m_owned || m_detourGetState.IsInstalled()) return;
    HMODULE xinput = LoadLibraryW(L"xinput1_4.dll");
    if (!xinput) {
        LogWarn("VirtualGamepad: xinput1_4.dll is not available; the Virtual Gamepad cannot answer the game.");
        return;
    }
    const auto install = [xinput](FunctionDetour& detour, const char* name, void* intercept) {
        void* target = reinterpret_cast<void*>(GetProcAddress(xinput, name));
        if (!target || !detour.Install(target, intercept)) {
            LogWarn(std::string("VirtualGamepad: could not hook ") + name);
        }
    };
    install(m_detourGetCapabilities, "XInputGetCapabilities", reinterpret_cast<void*>(&VirtualGamepad::Intercept_XInputGetCapabilities));
    install(m_detourSetState, "XInputSetState", reinterpret_cast<void*>(&VirtualGamepad::Intercept_XInputSetState));
    // The state hook goes last: it is the one the other calls and the slot probe wait for.
    install(m_detourGetState, "XInputGetState", reinterpret_cast<void*>(&VirtualGamepad::Intercept_XInputGetState));
}

void VirtualGamepad::UninstallHooksLocked() {
    m_detourGetState.Uninstall();
    m_detourGetCapabilities.Uninstall();
    m_detourSetState.Uninstall();
}

DWORD WINAPI VirtualGamepad::Intercept_XInputGetState(DWORD userIndex, XINPUT_STATE* state) {
    VirtualGamepad* pad = ForHook(&VirtualGamepad::m_detourGetState);
    return pad ? pad->GetState(userIndex, state) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI VirtualGamepad::Intercept_XInputGetCapabilities(DWORD userIndex, DWORD flags, XINPUT_CAPABILITIES* capabilities) {
    VirtualGamepad* pad = ForHook(&VirtualGamepad::m_detourGetCapabilities);
    return pad ? pad->GetCapabilities(userIndex, flags, capabilities) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI VirtualGamepad::Intercept_XInputSetState(DWORD userIndex, XINPUT_VIBRATION* vibration) {
    VirtualGamepad* pad = ForHook(&VirtualGamepad::m_detourSetState);
    return pad ? pad->SetState(userIndex, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

} // namespace fs25vr

