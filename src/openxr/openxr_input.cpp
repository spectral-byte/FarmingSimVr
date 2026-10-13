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
    m_gripPoseActive = {};
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

// The grip pose is optional: a runtime that refuses it leaves the aim pose and every other input
// working, and the grip pose then reports untracked (the Virtual Hand has nothing to be drawn at).
void OpenXrInput::CreateGripPoseAction(IOpenXrDispatch& dispatch) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = XR_ACTION_TYPE_POSE_INPUT;
    strncpy_s(info.actionName, kGripPoseAction, XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy_s(info.localizedActionName, "Grip pose", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    info.countSubactionPaths = kHandCount;
    info.subactionPaths = m_handPaths.data();
    const XrResult res = dispatch.CreateAction(m_actionSet, &info, &m_gripPoseAction);
    if (XR_FAILED(res)) {
        m_gripPoseAction = XR_NULL_HANDLE;
        LogWarn("OpenXrInput: creating the grip pose action failed (result " + std::to_string(res) +
                "). The grip pose reports untracked; the aim pose and the other inputs are unchanged.");
    }
}

bool OpenXrInput::SuggestBindings(IOpenXrDispatch& dispatch, XrInstance instance) {
    const std::vector<Profile> profiles = {
        { "/interaction_profiles/oculus/touch_controller", {
            { m_aimPoseAction, "input/aim/pose", "input/aim/pose" },
            { m_gripPoseAction, "input/grip/pose", "input/grip/pose" },
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
            { m_gripPoseAction, "input/grip/pose", "input/grip/pose" },
            { m_triggerAction, "input/trigger/value", "input/trigger/value" },
            { m_gripAction, "input/squeeze/value", "input/squeeze/value" },
            { m_thumbstickAction, "input/thumbstick", "input/thumbstick" },
            { m_thumbstickClickAction, "input/thumbstick/click", "input/thumbstick/click" },
            { m_buttonLowerAction, "input/a/click", "input/a/click" },
            { m_buttonUpperAction, "input/b/click", "input/b/click" },
            { m_hapticAction, "output/haptic", "output/haptic" } } },
        { "/interaction_profiles/htc/vive_controller", {
            { m_aimPoseAction, "input/aim/pose", "input/aim/pose" },
            { m_gripPoseAction, "input/grip/pose", "input/grip/pose" },
            { m_triggerAction, "input/trigger/value", "input/trigger/value" },
            { m_gripAction, "input/squeeze/click", "input/squeeze/click" },
            { m_thumbstickAction, "input/trackpad", "input/trackpad" },
            { m_thumbstickClickAction, "input/trackpad/click", "input/trackpad/click" },
            { m_menuAction, "input/menu/click", "input/menu/click" },
            { m_hapticAction, "output/haptic", "output/haptic" } } },
        { "/interaction_profiles/khr/simple_controller", {
            { m_aimPoseAction, "input/aim/pose", "input/aim/pose" },
            { m_gripPoseAction, "input/grip/pose", "input/grip/pose" },
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
            if (binding.action == XR_NULL_HANDLE) continue;  // an optional action the runtime refused
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
    CreateGripPoseAction(dispatch);

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
        if (m_gripPoseAction == XR_NULL_HANDLE) continue;
        spaceInfo.action = m_gripPoseAction;
        res = dispatch.CreateActionSpace(session, &spaceInfo, &m_gripSpaces[hand]);
        if (XR_FAILED(res)) {
            // Optional like the action itself: this hand's grip pose reports untracked.
            m_gripSpaces[hand] = XR_NULL_HANDLE;
            LogWarn("OpenXrInput: creating the grip action space failed (result " + std::to_string(res) +
                    "). The grip pose reports untracked; the aim pose is unchanged.");
        }
        spaceInfo.action = m_aimPoseAction;
    }

    m_ready = true;
    LogInfo("OpenXrInput: Motion Controller action set declared and attached.");
    return true;
}

void OpenXrInput::Sync(IOpenXrDispatch& dispatch, XrSession session) {
    if (!m_ready) return;

    XrActiveActionSet active{};
    active.actionSet = m_actionSet;
    active.subactionPath = XR_NULL_PATH;  // both hands
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &active;
    const XrResult res = dispatch.SyncActions(session, &syncInfo);
    if (res == XR_SESSION_NOT_FOCUSED) {
        // Another application has the input focus: no input is delivered, which is not an error.
        ResetHands();
        return;
    }
    if (XR_FAILED(res)) {
        static uint32_t s_syncFailures = 0;
        if (++s_syncFailures % 300 == 1) {
            LogWarn("OpenXrInput: xrSyncActions failed (result " + std::to_string(res) + "); hands report untracked.");
        }
        ResetHands();
        return;
    }

    for (uint32_t hand = 0; hand < kHandCount; ++hand) {
        auto getInfo = [&](XrAction action) {
            XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
            info.action = action;
            info.subactionPath = m_handPaths[hand];
            return info;
        };
        HandState& state = m_hands[hand];

        XrActionStatePose pose{XR_TYPE_ACTION_STATE_POSE};
        XrActionStateGetInfo info = getInfo(m_aimPoseAction);
        m_poseActive[hand] = XR_SUCCEEDED(dispatch.GetActionStatePose(session, &info, &pose)) && pose.isActive;
        m_gripPoseActive[hand] = false;
        if (m_gripPoseAction != XR_NULL_HANDLE) {
            XrActionStatePose gripState{XR_TYPE_ACTION_STATE_POSE};
            XrActionStateGetInfo gripInfo = getInfo(m_gripPoseAction);
            m_gripPoseActive[hand] = XR_SUCCEEDED(dispatch.GetActionStatePose(session, &gripInfo, &gripState)) && gripState.isActive;
        }

        auto readFloat = [&](XrAction action) {
            XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
            XrActionStateGetInfo i = getInfo(action);
            return (XR_SUCCEEDED(dispatch.GetActionStateFloat(session, &i, &value)) && value.isActive) ? value.currentState : 0.0f;
        };
        auto readBool = [&](XrAction action) {
            XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};
            XrActionStateGetInfo i = getInfo(action);
            return XR_SUCCEEDED(dispatch.GetActionStateBoolean(session, &i, &value)) && value.isActive && value.currentState;
        };
        state.trigger = readFloat(m_triggerAction);
        state.grip = readFloat(m_gripAction);
        XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};
        XrActionStateGetInfo stickInfo = getInfo(m_thumbstickAction);
        const bool stickActive = XR_SUCCEEDED(dispatch.GetActionStateVector2f(session, &stickInfo, &stick)) && stick.isActive;
        state.thumbX = stickActive ? stick.currentState.x : 0.0f;
        state.thumbY = stickActive ? stick.currentState.y : 0.0f;
        state.thumbClick = readBool(m_thumbstickClickAction);
        state.buttonLower = readBool(m_buttonLowerAction);
        state.buttonUpper = readBool(m_buttonUpperAction);
        state.menu = readBool(m_menuAction);
    }
}

// Locates one pose space in `baseSpace` at `time` into `out` (the head pose's convention). False, and
// `out` left untouched, when the pose is inactive or the runtime can not give a valid position and
// orientation for it.
static bool LocatePoseSpace(IOpenXrDispatch& dispatch, XrSpace space, bool active, XrSpace baseSpace, XrTime time,
                            HeadPose& out) {
    if (!active || space == XR_NULL_HANDLE || baseSpace == XR_NULL_HANDLE) return false;

    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(dispatch.LocateSpace(space, baseSpace, time, &location))) return false;

    const bool positionValid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
    const bool orientationValid = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
    if (!positionValid || !orientationValid) return false;

    out.position = Vector3(location.pose.position.x, location.pose.position.y, location.pose.position.z);
    out.orientation = Quaternion(location.pose.orientation.x, location.pose.orientation.y,
                                 location.pose.orientation.z, location.pose.orientation.w);
    out.rotation = QuaternionToEuler(out.orientation);
    out.isValid = true;
    out.positionTracked = (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    out.orientationTracked = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
    return true;
}

void OpenXrInput::LocatePoses(IOpenXrDispatch& dispatch, XrSpace baseSpace, XrTime time) {
    if (!m_ready) return;
    for (uint32_t hand = 0; hand < kHandCount; ++hand) {
        HandState& state = m_hands[hand];
        state.aim = HeadPose{};
        state.gripPose = HeadPose{};
        state.tracked = LocatePoseSpace(dispatch, m_aimSpaces[hand], m_poseActive[hand], baseSpace, time, state.aim);
        // The grip pose is located whether or not the aim pose is: each reports its own tracked flag.
        state.gripTracked = LocatePoseSpace(dispatch, m_gripSpaces[hand], m_gripPoseActive[hand], baseSpace, time,
                                            state.gripPose);
    }
}
bool OpenXrInput::PulseHaptic(IOpenXrDispatch& dispatch, XrSession session, uint32_t hand, int64_t durationNs,
                              float amplitude) {
    if (!m_ready || hand >= kHandCount || session == XR_NULL_HANDLE || m_hapticAction == XR_NULL_HANDLE) {
        return false;
    }
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.duration = durationNs;
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    vibration.amplitude = amplitude;
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = m_hapticAction;
    info.subactionPath = m_handPaths[hand];
    return XR_SUCCEEDED(dispatch.ApplyHapticFeedback(session, &info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration)));
}

void OpenXrInput::Destroy(IOpenXrDispatch& dispatch) {
    for (auto* spaces : { &m_aimSpaces, &m_gripSpaces }) {
        for (auto& space : *spaces) {
            if (space != XR_NULL_HANDLE) {
                dispatch.DestroySpace(space);
                space = XR_NULL_HANDLE;
            }
        }
    }
    if (m_actionSet != XR_NULL_HANDLE) {
        dispatch.DestroyActionSet(m_actionSet);
        m_actionSet = XR_NULL_HANDLE;
    }
    m_aimPoseAction = m_gripPoseAction = m_triggerAction =m_gripAction = m_thumbstickAction = XR_NULL_HANDLE;
    m_thumbstickClickAction = m_buttonLowerAction = m_buttonUpperAction = m_menuAction = XR_NULL_HANDLE;
    m_hapticAction = XR_NULL_HANDLE;
    m_handPaths = {};
    m_session = XR_NULL_HANDLE;
    m_ready = false;
    m_attempted = false;
    ResetHands();
}

} // namespace fs25vr
