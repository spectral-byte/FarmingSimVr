#pragma once

#include "openxr_dispatch.h"
#include "openxr_types.h"
#include <array>
#include <cstdint>

namespace fs25vr {

// Motion Controller input (ADR-0025): one OpenXR action set of raw per-hand inputs, declared and
// attached once per session, synced once per frame, and read back as HandState values. The meaning
// of a button lives in the Companion Mod; this class only reports raw inputs.
//
// Any refused step (a runtime that does not support input, a profile it does not know, a failed
// attach) is logged once; both hands then report untracked and the session carries on unchanged.
class OpenXrInput {
public:
    static constexpr uint32_t kHandCount = 2;
    static constexpr uint32_t kLeft = 0;
    static constexpr uint32_t kRight = 1;

    // Action names inside the action set (the fake runtime identifies actions by these).
    static constexpr const char* kActionSetName = "fs25vr_input";
    static constexpr const char* kAimPoseAction = "aim_pose";
    static constexpr const char* kGripPoseAction = "grip_pose";
    static constexpr const char* kTriggerAction = "trigger";
    static constexpr const char* kGripAction = "grip";
    static constexpr const char* kThumbstickAction = "thumbstick";
    static constexpr const char* kThumbstickClickAction = "thumbstick_click";
    static constexpr const char* kButtonLowerAction = "button_lower";
    static constexpr const char* kButtonUpperAction = "button_upper";
    static constexpr const char* kMenuAction = "menu";
    static constexpr const char* kHapticAction = "haptic";

    // Declares the action set and its actions, suggests bindings for the Oculus Touch, Valve Index,
    // HTC Vive and Khronos simple controller profiles, creates one aim action space per hand and
    // attaches the set to the session. Attempted once per session: later calls return the first
    // outcome. Returns whether Motion Controllers are usable.
    bool Create(IOpenXrDispatch& dispatch, XrInstance instance, XrSession session);

    // True once the action set is attached and at least one profile accepted its bindings.
    bool IsReady() const { return m_ready; }

    // xrSyncActions plus a read of every action. A session that is not FOCUSED reports no input
    // (hands untracked); that is not an error.
    void Sync(IOpenXrDispatch& dispatch, XrSession session);

    // Locates each hand's aim and grip spaces against `baseSpace` at `time`. Run after Sync().
    void LocatePoses(IOpenXrDispatch& dispatch, XrSpace baseSpace, XrTime time);

    const HandState& Get(uint32_t hand) const {
        return m_hands[hand < kHandCount ? hand : 0];
    }

    // Destroys the action spaces and the action set and resets both hands to untracked.
    void Destroy(IOpenXrDispatch& dispatch);

    // Plays a vibration on one hand's haptic output: `durationNs` nanoseconds at `amplitude` (0..1)
    // and the runtime's default frequency. False when the set is not ready, the hand does not exist or
    // the runtime fails the call (a runtime that is not focused may; that is not an error here).
    bool PulseHaptic(IOpenXrDispatch& dispatch, XrSession session, uint32_t hand, int64_t durationNs, float amplitude);

private:
    bool m_attempted = false;
    bool m_ready = false;
    XrSession m_session = XR_NULL_HANDLE;
    XrActionSet m_actionSet = XR_NULL_HANDLE;
    std::array<XrPath, kHandCount> m_handPaths{};
    XrAction m_aimPoseAction = XR_NULL_HANDLE;
    XrAction m_gripPoseAction = XR_NULL_HANDLE;   // stays null when the runtime refuses it: grip pose untracked
    XrAction m_triggerAction = XR_NULL_HANDLE;
    XrAction m_gripAction = XR_NULL_HANDLE;
    XrAction m_thumbstickAction = XR_NULL_HANDLE;
    XrAction m_thumbstickClickAction = XR_NULL_HANDLE;
    XrAction m_buttonLowerAction = XR_NULL_HANDLE;
    XrAction m_buttonUpperAction = XR_NULL_HANDLE;
    XrAction m_menuAction = XR_NULL_HANDLE;
    XrAction m_hapticAction = XR_NULL_HANDLE;
    std::array<XrSpace, kHandCount> m_aimSpaces{};
    std::array<XrSpace, kHandCount> m_gripSpaces{};
    std::array<HandState, kHandCount> m_hands{};
    // Whether the pose action was active at the last sync (a controller that is off is inactive).
    std::array<bool, kHandCount> m_poseActive{};
    std::array<bool, kHandCount> m_gripPoseActive{};

    bool CreateAction(IOpenXrDispatch& dispatch, const char* name, const char* localizedName,
                      XrActionType type, XrAction* out);
    void CreateGripPoseAction(IOpenXrDispatch& dispatch);
    bool SuggestBindings(IOpenXrDispatch& dispatch, XrInstance instance);
    void ResetHands();
    void FailCreate(IOpenXrDispatch& dispatch, const char* step, XrResult result);
};

} // namespace fs25vr
