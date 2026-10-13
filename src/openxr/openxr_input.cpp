#include "openxr_input.h"
#include "core/logger.h"
#include <cstring>
#include <string>
#include <vector>

namespace fs25vr {

namespace {

constexpr const char* kHandPaths[OpenXrInput::kHandCount] = { "/user/hand/left", "/user/hand/right" };

// One binding of an action on both hands. A null suffix means that hand has no such input.
struct HandBinding {
    XrAction action;
    const char* leftSuffix;
    const char* rightSuffix;
};

struct Profile {
    const char* path;
    std::vector<HandBinding> bindings;
};

} // namespace

void OpenXrInput::ResetHands() {
    for (auto& hand : m_hands) hand = HandState{};
    m_poseActive = {};
}

void OpenXrInput::FailCreate(IOpenXrDispatch& dispatch, const char* step, XrResult result) {
    LogWarn(std::string("OpenXrInput: ") + step + " failed (result " + std::to_string(result) +
            "). Motion Controllers are unavailable; head tracking and presentation are unchanged.");
    // Keeps m_attempted: a refused session is not asked again.
    const bool attempted = m_attempted;
    Destroy(dispatch);
    m_attempted = attempted;
}

bool OpenXrInput::CreateAction(IOpenXrDispatch& dispatch, const char* name, const char* localizedName,
                               XrActionType type, XrAction* out) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = type;
    strncpy_s(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy_s(info.localizedActionName, localizedName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    info.countSubactionPaths = kHandCount;
    info.subactionPaths = m_handPaths.data();
    const XrResult res = dispatch.CreateAction(m_actionSet, &info, out);
    if (XR_FAILED(res)) {
        FailCreate(dispatch, (std::string("creating action '") + name + "'").c_str(), res);
        return false;
    }
    return true;
}

bool OpenXrInput::SuggestBindings(IOpenXrDispatch& dispatch, XrInstance instance) {
    const std::vector<Profile> profiles = {
        { "/interaction_profiles/oculus/touch_controller", {
            { m_aimPoseAction, "input/aim/pose", "input/aim/pose" },
            { m_triggerAction, "input/trigger/value", "input/trigger/value" },
            { m_gripAction, "input/squeeze/value", "input/squeeze/value" },
            { m_thumbstickAction, "input/thumbstick", "input/thumbstick" },
            { m_thumbstickClickAction, "input/thumbstick/click", "input/thumbstick/click" },
            { m_buttonLowerAction, "input/x/click", "input/a/click" },
            { m_buttonUpperAction, "input/y/click", "input/b/click" },
            { m_menuAction, "input/menu/click", nullptr },
            { m_hapticAction, "output/haptic", "output/haptic" } } },
        { "/interaction_profiles/valve/index_controller", {
            { m_aimPoseAction, "input/aim/pose", "input/aim/pose" },
            { m_triggerAction, "input/trigger/value", "input/trigger/value" },
            { m_gripAction, "input/squeeze/value", "input/squeeze/value" },
            { m_thumbstickAction, "input/thumbstick", "input/thumbstick" },
            { m_thumbstickClickAction, "input/thumbstick/click", "input/thumbstick/click" },
            { m_buttonLowerAction, "input/a/click", "input/a/click" },
            { m_buttonUpperAction, "input/b/click", "input/b/click" },
            { m_hapticAction, "output/haptic", "output/haptic" } } },
        { "/interaction_profiles/htc/vive_controller", {
            { m_aimPoseAction, "input/aim/pose", "input/aim/pose" },
            { m_triggerAction, "input/trigger/value", "input/trigger/value" },
            { m_gripAction, "input/squeeze/click", "input/squeeze/click" },
            { m_thumbstickAction, "input/trackpad", "input/trackpad" },
            { m_thumbstickClickAction, "input/trackpad/click", "input/trackpad/click" },
            { m_menuAction, "input/menu/click", "input/menu/click" },
            { m_hapticAction, "output/haptic", "output/haptic" } } },
        { "/interaction_profiles/khr/simple_controller", {
            { m_aimPoseAction, "input/aim/pose", "input/aim/pose" },
            { m_triggerAction, "input/select/click", "input/select/click" },
            { m_menuAction, "input/menu/click", "input/menu/click" },
            { m_hapticAction, "output/haptic", "output/haptic" } } },
    };

    uint32_t accepted = 0;
    XrResult lastFailure = XR_SUCCESS;
    for (const Profile& profile : profiles) {
        XrPath profilePath = XR_NULL_PATH;
        XrResult res = dispatch.StringToPath(instance, profile.path, &profilePath);
        std::vector<XrActionSuggestedBinding> suggested;
        for (const HandBinding& binding : profile.bindings) {
            const char* suffixes[kHandCount] = { binding.leftSuffix, binding.rightSuffix };
            for (uint32_t hand = 0; hand < kHandCount && XR_SUCCEEDED(res); ++hand) {
                if (!suffixes[hand]) continue;
                XrActionSuggestedBinding entry{};
                entry.action = binding.action;
                res = dispatch.StringToPath(instance, (std::string(kHandPaths[hand]) + "/" + suffixes[hand]).c_str(),
                                            &entry.binding);
                suggested.push_back(entry);
            }
        }
        if (XR_SUCCEEDED(res)) {
            XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            info.interactionProfile = profilePath;
            info.countSuggestedBindings = static_cast<uint32_t>(suggested.size());
            info.suggestedBindings = suggested.data();
            res = dispatch.SuggestInteractionProfileBindings(instance, &info);
        }
        if (XR_SUCCEEDED(res)) {
            ++accepted;
        } else {
            // A runtime that does not know a controller profile says so; the others still bind.
            lastFailure = res;
            LogInfo(std::string("OpenXrInput: suggested bindings for ") + profile.path + " were refused (result " +
                    std::to_string(res) + ").");
        }
    }
    if (accepted == 0) {
        FailCreate(dispatch, "suggesting interaction profile bindings", lastFailure);
        return false;
    }
    return true;
}

bool OpenXrInput::Create(IOpenXrDispatch& dispatch, XrInstance instance, XrSession session) {
    if (m_attempted) return m_ready;
    m_attempted = true;
    m_session = session;

    for (uint32_t hand = 0; hand < kHandCount; ++hand) {
        const XrResult res = dispatch.StringToPath(instance, kHandPaths[hand], &m_handPaths[hand]);
        if (XR_FAILED(res)) {
            FailCreate(dispatch, "resolving the hand paths", res);
            return false;
        }
    }

    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    strncpy_s(setInfo.actionSetName, kActionSetName, XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy_s(setInfo.localizedActionSetName, "FS25 VR Motion Controllers", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    setInfo.priority = 0;
    XrResult res = dispatch.CreateActionSet(instance, &setInfo, &m_actionSet);
    if (XR_FAILED(res)) {
        m_actionSet = XR_NULL_HANDLE;
        FailCreate(dispatch, "creating the action set", res);
        return false;
    }

    if (!CreateAction(dispatch, kAimPoseAction, "Aim pose", XR_ACTION_TYPE_POSE_INPUT, &m_aimPoseAction) ||
        !CreateAction(dispatch, kTriggerAction, "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, &m_triggerAction) ||
        !CreateAction(dispatch, kGripAction, "Grip", XR_ACTION_TYPE_FLOAT_INPUT, &m_gripAction) ||
        !CreateAction(dispatch, kThumbstickAction, "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, &m_thumbstickAction) ||
        !CreateAction(dispatch, kThumbstickClickAction, "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT, &m_thumbstickClickAction) ||
        !CreateAction(dispatch, kButtonLowerAction, "Lower face button", XR_ACTION_TYPE_BOOLEAN_INPUT, &m_buttonLowerAction) ||
        !CreateAction(dispatch, kButtonUpperAction, "Upper face button", XR_ACTION_TYPE_BOOLEAN_INPUT, &m_buttonUpperAction) ||
        !CreateAction(dispatch, kMenuAction, "Menu button", XR_ACTION_TYPE_BOOLEAN_INPUT, &m_menuAction) ||
        !CreateAction(dispatch, kHapticAction, "Haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, &m_hapticAction)) {
        return false;
    }

    if (!SuggestBindings(dispatch, instance)) return false;

    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &m_actionSet;
    res = dispatch.AttachSessionActionSets(session, &attachInfo);
    if (XR_FAILED(res)) {
        FailCreate(dispatch, "attaching the action set", res);
        return false;
    }

    for (uint32_t hand = 0; hand < kHandCount; ++hand) {
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = m_aimPoseAction;
        spaceInfo.subactionPath = m_handPaths[hand];
        spaceInfo.poseInActionSpace = { {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };
        res = dispatch.CreateActionSpace(session, &spaceInfo, &m_aimSpaces[hand]);
        if (XR_FAILED(res)) {
            m_aimSpaces[hand] = XR_NULL_HANDLE;
            FailCreate(dispatch, "creating the aim action space", res);
            return false;
        }
    }

    m_ready = true;
    LogInfo("OpenXrInput: Motion Controller action set declared and attached.");
    return true;
}

void OpenXrInput::Sync(IOpenXrDispatch&, XrSession) {}

void OpenXrInput::LocatePoses(IOpenXrDispatch&, XrSpace, XrTime) {}

void OpenXrInput::Destroy(IOpenXrDispatch& dispatch) {
    for (auto& space : m_aimSpaces) {
        if (space != XR_NULL_HANDLE) {
            dispatch.DestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }
    if (m_actionSet != XR_NULL_HANDLE) {
        dispatch.DestroyActionSet(m_actionSet);
        m_actionSet = XR_NULL_HANDLE;
    }
    m_aimPoseAction = m_triggerAction = m_gripAction = m_thumbstickAction = XR_NULL_HANDLE;
    m_thumbstickClickAction = m_buttonLowerAction = m_buttonUpperAction = m_menuAction = XR_NULL_HANDLE;
    m_hapticAction = XR_NULL_HANDLE;
    m_handPaths = {};
    m_session = XR_NULL_HANDLE;
    m_ready = false;
    m_attempted = false;
    ResetHands();
}

} // namespace fs25vr
