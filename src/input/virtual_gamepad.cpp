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

std::atomic<VirtualGamepad::GetStateFn> g_originalGetState{nullptr};
std::atomic<VirtualGamepad::GetCapabilitiesFn> g_originalGetCapabilities{nullptr};
std::atomic<VirtualGamepad::SetStateFn> g_originalSetState{nullptr};

template <typename Fn>
Fn RealFunction(const FunctionDetour& detour, const std::atomic<Fn>& kept, Fn unhookedTarget) {
    if (detour.IsInstalled()) {
        if (const Fn own = reinterpret_cast<Fn>(detour.GetOriginal())) return own;
    }
    if (const Fn fallback = kept.load()) return fallback;
    return detour.IsInstalled() ? nullptr : unhookedTarget;
}

}

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
        std::unique_lock<std::mutex> lock(m_mutex);
        m_live = live;
        needSnapshot = !m_snapshotTaken;
        if (enabled != m_enabled) {
            m_enabled = enabled;
            if (enabled) {
                m_hooksFailed = false;
                m_nextSlotCheckMs = 0.0;
                if (m_owned && !SessionProblemLocked()) InstallHooksLocked();
            } else {
                m_loggedSlotsFull = false;
                m_nextSlotCheckMs = 0.0;
            }
            Evaluate(lock);
            if (!m_enabled) UninstallHooksLocked();
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
        if (hand == OpenXrInput::kRight && (inputs & kInputStickClick)) {
            m_rsPulseUntilMs = 0.0;
        }
        needSnapshot = !m_snapshotTaken;
    }
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

void VirtualGamepad::OnFrameBegin() {
    RefreshSnapshot();
    EnsureHooks();
    PlayRumble();
}

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

std::span<const char* const> VirtualGamepad::Reasons() {
    static constexpr const char* reasons[] = {kReasonOff, kReasonNoSession, kReasonNoActions,
                                              kReasonSlotsFull, kReasonNoHooks, kReasonPresent};
    return reasons;
}

VirtualGamepad::Status VirtualGamepad::GetStatus() {
    std::unique_lock<std::mutex> lock(m_mutex);
    return Evaluate(lock);
}

const char* VirtualGamepad::SessionProblemLocked() const {
    if (!m_openXrManager || !m_openXrManager->IsActive() || !m_openXrManager->HasSession()) return kReasonNoSession;
    if (!m_openXrManager->HasMotionControllerActions()) return kReasonNoActions;
    return nullptr;
}

bool VirtualGamepad::AllHooksInstalledLocked() const {
    return m_detourGetState.IsInstalled() && m_detourGetCapabilities.IsInstalled() && m_detourSetState.IsInstalled();
}

bool VirtualGamepad::ProbeRealPad(const RealPadProbe& probe, GetStateFn realGetState, DWORD slot) {
    if (probe) return probe(slot);
    if (!realGetState) return false;
    XINPUT_STATE ignored{};
    return realGetState(slot, &ignored) == ERROR_SUCCESS;
}

VirtualGamepad::Status VirtualGamepad::Evaluate(std::unique_lock<std::mutex>& lock) {
    const auto problemReason = [this]() -> const char* {
        if (!m_enabled) return kReasonOff;
        if (const char* problem = SessionProblemLocked()) return problem;
        if (m_owned && !AllHooksInstalledLocked()) return kReasonNoHooks;
        return nullptr;
    };
    const auto dropSlot = [this](const std::string& why) {
        LogInfo("VirtualGamepad: removed from XInput slot " + std::to_string(m_slot) + " (" + why + ")");
        m_slot = kNoSlot;
        m_motorSpeed.fill(0);
        ResetRightStickChordLocked();
    };
    const auto buildStatus = [this](const char* reason) {
        m_reason = (m_slot != kNoSlot) ? kReasonPresent : reason;
        Status status;
        status.present = (m_slot != kNoSlot);
        status.slot = m_slot;
        status.reason = m_reason;
        status.gripHeld = m_gripDown;
        return status;
    };

    if (const char* problem = problemReason()) {
        if (m_slot != kNoSlot) dropSlot(problem);
        return buildStatus(problem);
    }

    if (m_slot != kNoSlot && NowMs() >= m_nextSlotCheckMs) {
        const int checkedSlot = m_slot;
        m_nextSlotCheckMs = NowMs() + kSlotRecheckMs;
        const RealPadProbe probe = m_realPadProbe;
        const auto realGetState = reinterpret_cast<GetStateFn>(m_detourGetState.GetOriginal());
        lock.unlock();
        const bool occupied = ProbeRealPad(probe, realGetState, static_cast<DWORD>(checkedSlot));
        lock.lock();

        if (const char* problem = problemReason()) {
            if (m_slot != kNoSlot) dropSlot(problem);
            return buildStatus(problem);
        }
        if (occupied && m_slot == checkedSlot) {
            dropSlot("a real pad plugged into it");
            m_nextSlotCheckMs = 0.0;
        }
    }

    const char* reason = kReasonPresent;
    if (m_slot == kNoSlot) {
        if (NowMs() >= m_nextSlotCheckMs) {
            m_nextSlotCheckMs = NowMs() + kSlotRecheckMs;
            const RealPadProbe probe = m_realPadProbe;
            const auto realGetState = reinterpret_cast<GetStateFn>(m_detourGetState.GetOriginal());
            lock.unlock();
            int freeSlot = kNoSlot;
            for (int slot = kSlotCount - 1; slot >= 0; --slot) {
                if (!ProbeRealPad(probe, realGetState, static_cast<DWORD>(slot))) {
                    freeSlot = slot;
                    break;
                }
            }
            lock.lock();

            if (const char* problem = problemReason()) {
                if (m_slot != kNoSlot) dropSlot(problem);
                return buildStatus(problem);
            }
            if (m_slot == kNoSlot) {
                if (freeSlot != kNoSlot) {
                    m_slot = freeSlot;
                    m_nextSlotCheckMs = NowMs() + kSlotRecheckMs;
                    m_loggedSlotsFull = false;
                    LogInfo("VirtualGamepad: appeared in XInput slot " + std::to_string(freeSlot));
                } else {
                    reason = kReasonSlotsFull;
                    if (!m_loggedSlotsFull) {
                        m_loggedSlotsFull = true;
                        LogWarn("VirtualGamepad: not created, all four XInput slots hold real pads.");
                    }
                }
            }
        } else {
            reason = kReasonSlotsFull;
        }
    }

    return buildStatus(reason);
}

bool VirtualGamepad::IsPadSlot(std::unique_lock<std::mutex>& lock, DWORD userIndex) {
    const Status status = Evaluate(lock);
    return status.present && static_cast<DWORD>(status.slot) == userIndex;
}

XINPUT_GAMEPAD VirtualGamepad::BuildGamepadLocked() {
    XINPUT_GAMEPAD pad{};
    if (!m_live) {
        ResetRightStickChordLocked();
        return pad;
    }
    for (uint32_t hand = OpenXrInput::kLeft; hand <= OpenXrInput::kRight; ++hand) {
        HandState state = m_snapshot[hand];
        if (!state.tracked) {
            if (hand == OpenXrInput::kRight) ResetRightStickChordLocked();
            continue;
        }
        const bool left = (hand == OpenXrInput::kLeft);

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
        if (state.buttonLower) pad.wButtons |= left ? XINPUT_GAMEPAD_X : XINPUT_GAMEPAD_A;
        if (state.buttonUpper) pad.wButtons |= left ? XINPUT_GAMEPAD_Y : XINPUT_GAMEPAD_B;
        if (left && state.menu) pad.wButtons |= XINPUT_GAMEPAD_START;
        if (!left) ApplyRightStickChordLocked(state, pad, (used & kInputStickClick) != 0);
    }
    return pad;
}

void VirtualGamepad::ResetRightStickChordLocked() {
    m_rightClickDown = false;
    m_rightPush = Push::None;
    m_rightClickPushed = false;
    m_rsPulseUntilMs = 0.0;
}

void VirtualGamepad::ApplyRightStickChordLocked(const HandState& right, XINPUT_GAMEPAD& pad, bool stickClickSuppressed) {
    if (stickClickSuppressed) {
        ResetRightStickChordLocked();
        return;
    }
    const bool clickDown = right.thumbClick;
    if (!clickDown) {
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
    if (IsPadSlot(lock, userIndex)) {
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
    const GetStateFn real = RealFunction(m_detourGetState, g_originalGetState, m_targetGetState);
    lock.unlock();
    return real ? real(userIndex, state) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD VirtualGamepad::GetCapabilities(DWORD userIndex, DWORD flags, XINPUT_CAPABILITIES* capabilities) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (IsPadSlot(lock, userIndex)) {
        if (!capabilities) return ERROR_BAD_ARGUMENTS;
        std::memset(capabilities, 0, sizeof(*capabilities));
        capabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
        capabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
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
    const GetCapabilitiesFn real = RealFunction(m_detourGetCapabilities, g_originalGetCapabilities, m_targetGetCapabilities);
    lock.unlock();
    return real ? real(userIndex, flags, capabilities) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD VirtualGamepad::SetState(DWORD userIndex, XINPUT_VIBRATION* vibration) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (IsPadSlot(lock, userIndex)) {
        m_motorSpeed[OpenXrInput::kLeft] = vibration ? vibration->wLeftMotorSpeed : 0;
        m_motorSpeed[OpenXrInput::kRight] = vibration ? vibration->wRightMotorSpeed : 0;
        return ERROR_SUCCESS;
    }
    const SetStateFn real = RealFunction(m_detourSetState, g_originalSetState, m_targetSetState);
    lock.unlock();
    return real ? real(userIndex, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

void VirtualGamepad::NoteCompanionPulse(uint32_t hand, double durationMs) {
    if (hand >= m_companionPulseUntilMs.size() || !(durationMs > 0.0)) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_companionPulseUntilMs[hand] = (std::max)(m_companionPulseUntilMs[hand], NowMs() + durationMs);
}

void VirtualGamepad::PlayRumble() {
    std::array<float, 2> strengths{};
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!Evaluate(lock).present || !m_live) return;
        const double now = NowMs();
        for (uint32_t hand = OpenXrInput::kLeft; hand <= OpenXrInput::kRight; ++hand) {
            if (m_motorSpeed[hand] == 0 || now < m_companionPulseUntilMs[hand]) continue;
            strengths[hand] = kRumbleScale * static_cast<float>(m_motorSpeed[hand]) / 65535.0f;
        }
    }
    if (!m_openXrManager) return;
    for (uint32_t hand = OpenXrInput::kLeft; hand <= OpenXrInput::kRight; ++hand) {
        if (strengths[hand] > 0.0f) {
            m_openXrManager->PulseHaptic(hand, kRumblePulseMs / 1000.0, strengths[hand]);
        }
    }
}

void VirtualGamepad::InstallHooksLocked() {
    if (!m_owned || m_hooksFailed || AllHooksInstalledLocked()) return;
    HMODULE xinput = GetModuleHandleW(L"xinput1_4.dll");
    if (!xinput) xinput = LoadLibraryW(L"xinput1_4.dll");
    if (!xinput) {
        LogWarn("VirtualGamepad: xinput1_4.dll is not available; the Virtual Gamepad cannot answer the game.");
        m_hooksFailed = true;
        return;
    }
    bool ok = true;
    const auto install = [xinput, &ok](FunctionDetour& detour, const char* name, void* intercept, auto& kept, auto& cachedTarget) {
        if (detour.IsInstalled()) return;
        void* target = reinterpret_cast<void*>(GetProcAddress(xinput, name));
        using Fn = typename std::decay_t<decltype(kept)>::value_type;
        cachedTarget = reinterpret_cast<Fn>(target);
        if (!target || !detour.Install(target, intercept)) {
            LogWarn(std::string("VirtualGamepad: could not hook ") + name);
            ok = false;
            return;
        }
        kept.store(reinterpret_cast<Fn>(detour.GetOriginal()));
    };
    install(m_detourGetCapabilities, "XInputGetCapabilities",
            reinterpret_cast<void*>(&VirtualGamepad::Intercept_XInputGetCapabilities), g_originalGetCapabilities, m_targetGetCapabilities);
    install(m_detourSetState, "XInputSetState",
            reinterpret_cast<void*>(&VirtualGamepad::Intercept_XInputSetState), g_originalSetState, m_targetSetState);
    install(m_detourGetState, "XInputGetState",
            reinterpret_cast<void*>(&VirtualGamepad::Intercept_XInputGetState), g_originalGetState, m_targetGetState);
    if (!ok) {
        RollBackHooksLocked();
        m_hooksFailed = true;
    }
}

void VirtualGamepad::RollBackHooksLocked() {
    UninstallHooksLocked();
}

void VirtualGamepad::UninstallHooksLocked() {
    const auto uninstall = [](auto& kept, FunctionDetour& detour) {
        using Fn = typename std::decay_t<decltype(kept)>::value_type;
        Fn expected = detour.IsInstalled() ? reinterpret_cast<Fn>(detour.GetOriginal()) : nullptr;
        detour.Uninstall();
        if (!detour.IsInstalled() && expected) kept.compare_exchange_strong(expected, nullptr);
    };
    uninstall(g_originalGetState, m_detourGetState);
    uninstall(g_originalGetCapabilities, m_detourGetCapabilities);
    uninstall(g_originalSetState, m_detourSetState);
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
}

