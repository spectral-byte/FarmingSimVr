#pragma once

#include "openxr_dispatch.h"
#include "openxr_types.h"
#include <array>
#include <cstdint>

namespace fs25vr {

class OpenXrInput {
public:
    static constexpr uint32_t kHandCount = 2;
    static constexpr uint32_t kLeft = 0;
    static constexpr uint32_t kRight = 1;

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

    bool Create(IOpenXrDispatch& dispatch, XrInstance instance, XrSession session);

    bool IsReady() const { return m_ready; }

    void Sync(IOpenXrDispatch& dispatch, XrSession session);

    void LocatePoses(IOpenXrDispatch& dispatch, XrSpace baseSpace, XrTime time);

    const HandState& Get(uint32_t hand) const {
        return m_hands[hand < kHandCount ? hand : 0];
    }

    void Destroy(IOpenXrDispatch& dispatch);

    bool PulseHaptic(IOpenXrDispatch& dispatch, XrSession session, uint32_t hand, int64_t durationNs, float amplitude);

private:
    bool m_attempted = false;
    bool m_ready = false;
    XrSession m_session = XR_NULL_HANDLE;
    XrActionSet m_actionSet = XR_NULL_HANDLE;
    std::array<XrPath, kHandCount> m_handPaths{};
    XrAction m_aimPoseAction = XR_NULL_HANDLE;
    XrAction m_gripPoseAction = XR_NULL_HANDLE;
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
    std::array<bool, kHandCount> m_poseActive{};
    std::array<bool, kHandCount> m_gripPoseActive{};

    bool CreateAction(IOpenXrDispatch& dispatch, const char* name, const char* localizedName,
                      XrActionType type, XrAction* out);
    void CreateGripPoseAction(IOpenXrDispatch& dispatch);
    bool SuggestBindings(IOpenXrDispatch& dispatch, XrInstance instance);
    void ResetHands();
    void FailCreate(IOpenXrDispatch& dispatch, const char* step, XrResult result);
};

}
