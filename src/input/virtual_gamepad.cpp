#include "input/virtual_gamepad.h"

#include "core/dll_lifecycle.h"
#include "core/injector_runtime.h"
#include "core/logger.h"
#include "luau/vr_bridge.h"
#include "openxr/openxr_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <type_traits>

namespace fs25vr {

namespace {

constexpr const char* kReasonOff = "off";
constexpr const char* kReasonNoSession = "no OpenXR session";
constexpr const char* kReasonNoActions = "the OpenXR runtime refused the Motion Controller action set";
constexpr const char* kReasonSlotsFull = "all four XInput slots hold real pads";
constexpr const char* kReasonNoHooks = "the XInput hooks are not installed";
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

// The real XInput functions, kept by the hook layer itself (set when a pad installs its detours, cleared when it removes
// them): a hooked call that finds no pad to answer for it (a runtime going away under a call in flight) still reaches the
// real function, so a real pad never vanishes from the game.
std::atomic<VirtualGamepad::GetStateFn> g_originalGetState{nullptr};
std::atomic<VirtualGamepad::GetCapabilitiesFn> g_originalGetCapabilities{nullptr};
std::atomic<VirtualGamepad::SetStateFn> g_originalSetState{nullptr};

// The real function a call for a slot that is not the pad's goes to: the pad's own original while its detour is installed,
// otherwise the hook layer's (a hook entry point can be routed to a pad whose detour is not the installed one).
template <typename Fn>
Fn RealFunction(const FunctionDetour& detour, const std::atomic<Fn>& kept) {
    const Fn own = detour.IsInstalled() ? reinterpret_cast<Fn>(detour.GetOriginal()) : nullptr;
    return own ? own : kept.load();
}

} // namespace

VirtualGamepad::VirtualGamepad(std::shared_ptr<OpenXrManager> openXrManager)
    : m_openXrManager(std::move(openXrManager)) {
    if (m_openXrManager) m_openXrManager->SetFrameBeginObserver(this, [this] { OnFrameBegin(); });
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
    bool needSnapshot = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_live = live;
        needSnapshot = !m_snapshotTaken;
        if (enabled != m_enabled) {
            m_enabled = enabled;
            if (enabled) {
                m_hooksFailed = false;
                if (m_owned && !SessionProblemLocked()) InstallHooksLocked();
            } else {
                m_loggedSlotsFull = false;
            }
            EvaluateLocked();
            if (!enabled) UninstallHooksLocked();
        }
    }
    if (needSnapshot) RefreshSnapshot();
}

void VirtualGamepad::SetInputsInUse(uint32_t hand, uint32_t inputs) {
    if (hand >= m_inUse.size()) return;
    bool needSnapshot = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inUse[hand] = inputs;
        needSnapshot = !m_snapshotTaken;
    }
    // The Companion Mod runs on the render thread's side of the game: it may publish the first snapshot if no frame has yet.
    if (needSnapshot) RefreshSnapshot();
}

void VirtualGamepad::SetOwnedByRuntime(bool owned) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_owned = owned;
    m_hooksFailed = false;
    if (owned) {
        if (m_enabled && !SessionProblemLocked()) InstallHooksLocked();
    } else {
        UninstallHooksLocked();
    }
}

// The start of every OpenXR frame, on the render thread, after the hand state is synced.
void VirtualGamepad::OnFrameBegin() {
    RefreshSnapshot();
    EnsureHooks();
    PlayRumble();
}

// Publishes both hands as this frame began, the only thing the game's XInput thread reads. OpenXR is asked here, with no
// lock held; the game's thread never asks it.
void VirtualGamepad::RefreshSnapshot() {
    std::array<HandState, 2> hands{};
    if (m_openXrManager) {
        for (uint32_t hand = OpenXrInput::kLeft; hand <= OpenXrInput::kRight; ++hand) hands[hand] = m_openXrManager->GetHandState(hand);
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_snapshot = hands;
    m_snapshotTaken = true;
    for (uint32_t hand = OpenXrInput::kLeft; hand <= OpenXrInput::kRight; ++hand) {
        m_gripDown[hand] = hands[hand].tracked && hands[hand].grip >= (m_gripDown[hand] ? kGripRelease : kGripPress);
    }
}

void VirtualGamepad::EnsureHooks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled || !m_owned || m_hooksFailed || AllHooksInstalledLocked() || SessionProblemLocked()) return;
    InstallHooksLocked();
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

const char* VirtualGamepad::SessionProblemLocked() const {
    if (!m_openXrManager || !m_openXrManager->IsActive() || !m_openXrManager->HasSession()) return kReasonNoSession;
    if (!m_openXrManager->HasMotionControllerActions()) return kReasonNoActions;
    return nullptr;
}

bool VirtualGamepad::AllHooksInstalledLocked() const {
    return m_detourGetState.IsInstalled() && m_detourGetCapabilities.IsInstalled() && m_detourSetState.IsInstalled();
}

bool VirtualGamepad::RealPadPresentLocked(DWORD slot) {
    if (m_realPadProbe) return m_realPadProbe(slot);
    const auto real = reinterpret_cast<GetStateFn>(m_detourGetState.GetOriginal());
    if (!real) return false;
    XINPUT_STATE ignored{};
    return real(slot, &ignored) == ERROR_SUCCESS;
}

VirtualGamepad::Status VirtualGamepad::EvaluateLocked() {
    const char* reason = kReasonPresent;
    if (!m_enabled) {
        reason = kReasonOff;
    } else if (const char* problem = SessionProblemLocked()) {
        reason = problem;
    } else if (m_owned && !AllHooksInstalledLocked()) {
        // A pad the game cannot see is no pad: the Companion Mod would write bindings for it and set its permanent flag.
        reason = kReasonNoHooks;
    }

    const auto dropSlot = [this](const std::string& why) {
        LogInfo("VirtualGamepad: removed from XInput slot " + std::to_string(m_slot) + " (" + why + ")");
        m_slot = kNoSlot;
        m_motorSpeed.fill(0);
        ResetRightStickChordLocked();
    };

    // A real pad that plugs into the pad's slot keeps it: the pad gives the slot up and takes a free one below.
    if (reason == kReasonPresent && m_slot != kNoSlot && NowMs() >= m_nextSlotCheckMs) {
        m_nextSlotCheckMs = NowMs() + kSlotRecheckMs;
        if (RealPadPresentLocked(static_cast<DWORD>(m_slot))) dropSlot("a real pad plugged into it");
    }

    if (reason == kReasonPresent && m_slot == kNoSlot) {
        // The pad appears: the highest slot no real pad occupies, so a real Xbox pad keeps the first one.
        for (int slot = kSlotCount - 1; slot >= 0; --slot) {
            if (!RealPadPresentLocked(static_cast<DWORD>(slot))) {
                m_slot = slot;
                m_nextSlotCheckMs = NowMs() + kSlotRecheckMs;
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
        dropSlot(reason);
    }

    m_reason = (m_slot != kNoSlot) ? kReasonPresent : reason;
    Status status;
    status.present = (m_slot != kNoSlot);
    status.slot = m_slot;
    status.reason = m_reason;
    status.gripHeld = m_gripDown;
    return status;
}

bool VirtualGamepad::IsPadSlotLocked(DWORD userIndex) {
    const Status status = EvaluateLocked();
    return status.present && static_cast<DWORD>(status.slot) == userIndex;
}

XINPUT_GAMEPAD VirtualGamepad::BuildGamepadLocked() {
    XINPUT_GAMEPAD pad{};
    if (!m_live) {
        ResetRightStickChordLocked();
        return pad;
    }
    // Each physical control on each hand is its own pad input (ADR-0028). Back is never fed; the D-pad comes only from the right stick chord below.
    for (uint32_t hand = OpenXrInput::kLeft; hand <= OpenXrInput::kRight; ++hand) {
        HandState state = m_snapshot[hand];  // as the last frame began: the game's thread asks OpenXR for nothing
        if (!state.tracked) {
            if (hand == OpenXrInput::kRight) ResetRightStickChordLocked();
            continue;
        }
        const bool left = (hand == OpenXrInput::kLeft);

        // An input the Companion Mod is using reads as released (the grip's hysteresis keeps tracking underneath).
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
    const GetStateFn real = RealFunction(m_detourGetState, g_originalGetState);
    lock.unlock();
    return real ? real(userIndex, state) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD VirtualGamepad::GetCapabilities(DWORD userIndex, DWORD flags, XINPUT_CAPABILITIES* capabilities) {
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
    const GetCapabilitiesFn real = RealFunction(m_detourGetCapabilities, g_originalGetCapabilities);
    lock.unlock();
    return real ? real(userIndex, flags, capabilities) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD VirtualGamepad::SetState(DWORD userIndex, XINPUT_VIBRATION* vibration) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (IsPadSlotLocked(userIndex)) {
        // Played from the next OpenXR frame on (PlayRumble); the left motor is the left hand's.
        m_motorSpeed[OpenXrInput::kLeft] = vibration ? vibration->wLeftMotorSpeed : 0;
        m_motorSpeed[OpenXrInput::kRight] = vibration ? vibration->wRightMotorSpeed : 0;
        return ERROR_SUCCESS;
    }
    const SetStateFn real = RealFunction(m_detourSetState, g_originalSetState);
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
    if (!m_owned || m_hooksFailed || AllHooksInstalledLocked()) return;
    HMODULE xinput = LoadLibraryW(L"xinput1_4.dll");
    if (!xinput) {
        LogWarn("VirtualGamepad: xinput1_4.dll is not available; the Virtual Gamepad cannot answer the game.");
        m_hooksFailed = true;
        return;
    }
    bool ok = true;
    const auto install = [xinput, &ok](FunctionDetour& detour, const char* name, void* intercept) {
        if (detour.IsInstalled()) return;
        void* target = reinterpret_cast<void*>(GetProcAddress(xinput, name));
        if (!target || !detour.Install(target, intercept)) {
            LogWarn(std::string("VirtualGamepad: could not hook ") + name);
            ok = false;
        }
    };
    install(m_detourGetCapabilities, "XInputGetCapabilities", reinterpret_cast<void*>(&VirtualGamepad::Intercept_XInputGetCapabilities));
    install(m_detourSetState, "XInputSetState", reinterpret_cast<void*>(&VirtualGamepad::Intercept_XInputSetState));
    // The state hook goes last: it is the one the other calls and the slot probe wait for.
    install(m_detourGetState, "XInputGetState", reinterpret_cast<void*>(&VirtualGamepad::Intercept_XInputGetState));
    if (!ok) {
        // Half a set of hooks would show the game a pad it cannot poll: none, and no pad (see EvaluateLocked).
        RollBackHooksLocked();
        m_hooksFailed = true;
        return;
    }
    g_originalGetCapabilities.store(reinterpret_cast<GetCapabilitiesFn>(m_detourGetCapabilities.GetOriginal()));
    g_originalSetState.store(reinterpret_cast<SetStateFn>(m_detourSetState.GetOriginal()));
    g_originalGetState.store(reinterpret_cast<GetStateFn>(m_detourGetState.GetOriginal()));
}

void VirtualGamepad::RollBackHooksLocked() {
    UninstallHooksLocked();
}

void VirtualGamepad::UninstallHooksLocked() {
    // The kept originals go before the trampolines they point into.
    const auto forget = [](auto& kept, const FunctionDetour& detour) {
        using Fn = typename std::decay_t<decltype(kept)>::value_type;
        Fn expected = reinterpret_cast<Fn>(detour.GetOriginal());
        if (detour.IsInstalled()) kept.compare_exchange_strong(expected, nullptr);
    };
    forget(g_originalGetState, m_detourGetState);
    forget(g_originalGetCapabilities, m_detourGetCapabilities);
    forget(g_originalSetState, m_detourSetState);
    m_detourGetState.Uninstall();
    m_detourGetCapabilities.Uninstall();
    m_detourSetState.Uninstall();
}

void VirtualGamepad::SetHookOriginalsForTesting(GetStateFn getState, GetCapabilitiesFn getCapabilities, SetStateFn setState) {
    g_originalGetState.store(getState);
    g_originalGetCapabilities.store(getCapabilities);
    g_originalSetState.store(setState);
}

DWORD WINAPI VirtualGamepad::Intercept_XInputGetState(DWORD userIndex, XINPUT_STATE* state) {
    if (VirtualGamepad* pad = ForHook(&VirtualGamepad::m_detourGetState)) return pad->GetState(userIndex, state);
    const GetStateFn real = g_originalGetState.load();
    return real ? real(userIndex, state) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI VirtualGamepad::Intercept_XInputGetCapabilities(DWORD userIndex, DWORD flags, XINPUT_CAPABILITIES* capabilities) {
    if (VirtualGamepad* pad = ForHook(&VirtualGamepad::m_detourGetCapabilities)) return pad->GetCapabilities(userIndex, flags, capabilities);
    const GetCapabilitiesFn real = g_originalGetCapabilities.load();
    return real ? real(userIndex, flags, capabilities) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI VirtualGamepad::Intercept_XInputSetState(DWORD userIndex, XINPUT_VIBRATION* vibration) {
    if (VirtualGamepad* pad = ForHook(&VirtualGamepad::m_detourSetState)) return pad->SetState(userIndex, vibration);
    const SetStateFn real = g_originalSetState.load();
    return real ? real(userIndex, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}
} // namespace fs25vr

