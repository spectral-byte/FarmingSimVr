#pragma once

#include "openxr/openxr_input.h"
#include "fake_openxr_tracking.h"
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs25vr {

// Motion Controller model for fake OpenXR dispatches. Tests place each hand and set its inputs; the
// fake forwards the action calls (xrStringToPath ... xrApplyHapticFeedback) here and behaves like a
// runtime: it records what was declared, refuses what the knobs say to refuse, latches inputs at
// xrSyncActions, and locates the aim spaces in the same re-based tracking origin as the head.
//
// Hand ids: 0 = left, 1 = right.
class FakeOpenXrControllers {
public:
    struct Inputs {
        float trigger = 0.0f;
        float grip = 0.0f;
        float thumbX = 0.0f;
        float thumbY = 0.0f;
        bool thumbClick = false;
        bool buttonLower = false;
        bool buttonUpper = false;
        bool menu = false;
    };

    struct Hand {
        XrPosef pose{ {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };  // in the tracking origin
        XrPosef gripPose{ {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };  // in the tracking origin
        bool gripTracked = true; // the grip pose alone is valid (a controller that is off has neither pose)
        bool tracked = true;     // the controller is on and its pose is valid
        Inputs inputs;           // the live values; xrSyncActions latches them
    };
    Hand hands[2];

    // ---- knobs: make a step fail -------------------------------------------------------------
    XrResult stringToPathResult = XR_SUCCESS;
    XrResult createActionSetResult = XR_SUCCESS;
    XrResult createActionResult = XR_SUCCESS;
    XrResult suggestBindingsResult = XR_SUCCESS;     // every profile
    std::set<std::string> unsupportedProfiles;       // these profiles fail with XR_ERROR_PATH_UNSUPPORTED
    XrResult attachResult = XR_SUCCESS;
    XrResult createActionSpaceResult = XR_SUCCESS;
    XrResult createGripActionResult = XR_SUCCESS;       // only the grip pose action
    XrResult createGripActionSpaceResult = XR_SUCCESS;  // only the grip pose action's spaces
    XrResult syncActionsResult = XR_SUCCESS;         // e.g. XR_SESSION_NOT_FOCUSED
    XrResult applyHapticResult = XR_SUCCESS;

    // A runtime that refuses the whole action set at its first step.
    void RefuseActionSet(bool refuse = true) {
        createActionSetResult = refuse ? XR_ERROR_RUNTIME_FAILURE : XR_SUCCESS;
    }

    // ---- records -----------------------------------------------------------------------------
    struct ActionRecord {
        std::string name;
        XrActionType type = XR_ACTION_TYPE_BOOLEAN_INPUT;
        std::vector<std::string> subactionPaths;
    };
    std::vector<std::string> actionSetNames;
    std::vector<ActionRecord> actions;
    // profile path -> (action name, binding path) pairs of the last suggestion for that profile.
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> suggestedBindings;
    uint32_t suggestCalls = 0;
    uint32_t attachCalls = 0;
    uint32_t actionSetsAttached = 0;
    uint32_t syncCalls = 0;
    uint32_t actionSpacesCreated = 0;
    uint32_t destroyActionSetCalls = 0;
    uint32_t destroyedActionSpaces = 0;

    struct LocateRecord {
        int hand = -1;
        std::string action;     // the pose action whose space was located (aim_pose / grip_pose)
        XrSpace baseSpace = XR_NULL_HANDLE;
        XrTime time = 0;
    };
    std::vector<LocateRecord> locates;      // every pose space located

    struct HapticPulse {
        int hand = -1;
        XrDuration duration = 0;   // nanoseconds
        float frequency = 0.0f;
        float amplitude = 0.0f;
    };
    std::vector<HapticPulse> hapticPulses;
    uint32_t stopHapticCalls = 0;

    explicit FakeOpenXrControllers(FakeOpenXrTracking& tracking) : m_tracking(tracking) {}

    // ---- dispatch side -----------------------------------------------------------------------
    XrResult StringToPath(const char* pathString, XrPath* path) {
        if (stringToPathResult != XR_SUCCESS) return stringToPathResult;
        if (!pathString || !path) return XR_ERROR_VALIDATION_FAILURE;
        auto it = m_pathIds.find(pathString);
        if (it == m_pathIds.end()) {
            m_pathStrings.push_back(pathString);
            it = m_pathIds.emplace(pathString, static_cast<XrPath>(m_pathStrings.size())).first;
        }
        *path = it->second;
        return XR_SUCCESS;
    }

    XrResult CreateActionSet(const XrActionSetCreateInfo* createInfo, XrActionSet* actionSet) {
        if (createActionSetResult != XR_SUCCESS) return createActionSetResult;
        if (!createInfo || !actionSet) return XR_ERROR_VALIDATION_FAILURE;
        actionSetNames.push_back(createInfo->actionSetName);
        *actionSet = reinterpret_cast<XrActionSet>(static_cast<uintptr_t>(0xAC710000 + actionSetNames.size()));
        m_liveActionSets.insert(*actionSet);
        return XR_SUCCESS;
    }

    XrResult DestroyActionSet(XrActionSet actionSet) {
        ++destroyActionSetCalls;
        m_liveActionSets.erase(actionSet);
        return XR_SUCCESS;
    }

    XrResult CreateAction(XrActionSet actionSet, const XrActionCreateInfo* createInfo, XrAction* action) {
        if (createActionResult != XR_SUCCESS) return createActionResult;
        if (!createInfo || !action || m_liveActionSets.count(actionSet) == 0) return XR_ERROR_HANDLE_INVALID;
        if (createGripActionResult != XR_SUCCESS && std::string(createInfo->actionName) == OpenXrInput::kGripPoseAction) {
            return createGripActionResult;
        }
        ActionRecord record;
        record.name = createInfo->actionName;
        record.type = createInfo->actionType;
        for (uint32_t i = 0; i < createInfo->countSubactionPaths; ++i) {
            record.subactionPaths.push_back(PathString(createInfo->subactionPaths[i]));
        }
        actions.push_back(record);
        XrAction handle = reinterpret_cast<XrAction>(static_cast<uintptr_t>(0xAC710100 + actions.size()));
        m_actionRecords[handle] = record;
        *action = handle;
        return XR_SUCCESS;
    }

    XrResult SuggestInteractionProfileBindings(const XrInteractionProfileSuggestedBinding* suggested) {
        ++suggestCalls;
        if (!suggested) return XR_ERROR_VALIDATION_FAILURE;
        if (suggestBindingsResult != XR_SUCCESS) return suggestBindingsResult;
        const std::string profile = PathString(suggested->interactionProfile);
        if (unsupportedProfiles.count(profile) != 0) return XR_ERROR_PATH_UNSUPPORTED;
        if (m_attachedSessions != 0) return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
        auto& list = suggestedBindings[profile];
        list.clear();
        for (uint32_t i = 0; i < suggested->countSuggestedBindings; ++i) {
            const auto it = m_actionRecords.find(suggested->suggestedBindings[i].action);
            if (it == m_actionRecords.end()) return XR_ERROR_HANDLE_INVALID;
            list.emplace_back(it->second.name, PathString(suggested->suggestedBindings[i].binding));
        }
        return XR_SUCCESS;
    }

    XrResult AttachSessionActionSets(const XrSessionActionSetsAttachInfo* attachInfo) {
        ++attachCalls;
        if (attachResult != XR_SUCCESS) return attachResult;
        if (!attachInfo) return XR_ERROR_VALIDATION_FAILURE;
        if (m_attachedSessions != 0) return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
        for (uint32_t i = 0; i < attachInfo->countActionSets; ++i) {
            if (m_liveActionSets.count(attachInfo->actionSets[i]) == 0) return XR_ERROR_HANDLE_INVALID;
        }
        actionSetsAttached += attachInfo->countActionSets;
        m_attachedSessions = 1;
        return XR_SUCCESS;
    }

    XrResult CreateActionSpace(const XrActionSpaceCreateInfo* createInfo, XrSpace* space) {
        if (createActionSpaceResult != XR_SUCCESS) return createActionSpaceResult;
        if (!createInfo || !space) return XR_ERROR_VALIDATION_FAILURE;
        const auto it = m_actionRecords.find(createInfo->action);
        if (it == m_actionRecords.end() || it->second.type != XR_ACTION_TYPE_POSE_INPUT) {
            return XR_ERROR_ACTION_TYPE_MISMATCH;
        }
        const int hand = HandOf(createInfo->subactionPath);
        if (hand < 0) return XR_ERROR_PATH_UNSUPPORTED;
        if (createGripActionSpaceResult != XR_SUCCESS && it->second.name == OpenXrInput::kGripPoseAction) {
            return createGripActionSpaceResult;
        }
        static uintptr_t s_spaceHandle = 0xAC7A0001;
        XrSpace handle = reinterpret_cast<XrSpace>(s_spaceHandle++);
        m_actionSpaces[handle] = { hand, it->second.name };
        ++actionSpacesCreated;
        *space = handle;
        return XR_SUCCESS;
    }

    bool OwnsSpace(XrSpace space) const { return m_actionSpaces.count(space) != 0; }

    // Called from the dispatch's DestroySpace; true when the space was an action space.
    bool DestroyActionSpace(XrSpace space) {
        if (m_actionSpaces.erase(space) == 0) return false;
        ++destroyedActionSpaces;
        return true;
    }

    // Called from the dispatch's DestroySession: a new session may attach again.
    void OnSessionDestroyed() {
        m_attachedSessions = 0;
        m_latchedValid = false;
    }

    XrResult SyncActions(const XrActionsSyncInfo* syncInfo) {
        ++syncCalls;
        if (!syncInfo) return XR_ERROR_VALIDATION_FAILURE;
        if (m_attachedSessions == 0) return XR_ERROR_ACTIONSET_NOT_ATTACHED;
        if (syncActionsResult != XR_SUCCESS && syncActionsResult != XR_SESSION_NOT_FOCUSED) return syncActionsResult;
        if (syncActionsResult == XR_SESSION_NOT_FOCUSED) {
            // A session that is not focused delivers no input: every action is inactive.
            m_latchedValid = false;
            return XR_SESSION_NOT_FOCUSED;
        }
        for (int h = 0; h < 2; ++h) {
            m_latched[h] = hands[h];
        }
        m_latchedValid = true;
        return XR_SUCCESS;
    }

    XrResult GetActionStateBoolean(const XrActionStateGetInfo* info, XrActionStateBoolean* state) {
        return ReadState(info, XR_ACTION_TYPE_BOOLEAN_INPUT, [&](const std::string& name, const Hand& h) {
            state->isActive = XR_TRUE;
            bool value = false;
            if (name == OpenXrInput::kThumbstickClickAction) value = h.inputs.thumbClick;
            else if (name == OpenXrInput::kButtonLowerAction) value = h.inputs.buttonLower;
            else if (name == OpenXrInput::kButtonUpperAction) value = h.inputs.buttonUpper;
            else if (name == OpenXrInput::kMenuAction) value = h.inputs.menu;
            state->currentState = value ? XR_TRUE : XR_FALSE;
        }, [&] { state->isActive = XR_FALSE; state->currentState = XR_FALSE; });
    }

    XrResult GetActionStateFloat(const XrActionStateGetInfo* info, XrActionStateFloat* state) {
        return ReadState(info, XR_ACTION_TYPE_FLOAT_INPUT, [&](const std::string& name, const Hand& h) {
            state->isActive = XR_TRUE;
            state->currentState = name == OpenXrInput::kTriggerAction ? h.inputs.trigger : h.inputs.grip;
        }, [&] { state->isActive = XR_FALSE; state->currentState = 0.0f; });
    }

    XrResult GetActionStateVector2f(const XrActionStateGetInfo* info, XrActionStateVector2f* state) {
        return ReadState(info, XR_ACTION_TYPE_VECTOR2F_INPUT, [&](const std::string&, const Hand& h) {
            state->isActive = XR_TRUE;
            state->currentState = { h.inputs.thumbX, h.inputs.thumbY };
        }, [&] { state->isActive = XR_FALSE; state->currentState = { 0.0f, 0.0f }; });
    }

    XrResult GetActionStatePose(const XrActionStateGetInfo* info, XrActionStatePose* state) {
        return ReadState(info, XR_ACTION_TYPE_POSE_INPUT, [&](const std::string& name, const Hand& h) {
            const bool grip = name == OpenXrInput::kGripPoseAction;
            state->isActive = (h.tracked && (!grip || h.gripTracked)) ? XR_TRUE : XR_FALSE;
        }, [&] { state->isActive = XR_FALSE; });
    }

    // Locates an aim or grip space. Valid only while the latched pose action is active (a controller that
    // is off, a grip pose that alone is untracked or a session that is not focused has no pose).
    XrResult LocateActionSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location) {
        const auto it = m_actionSpaces.find(space);
        if (it == m_actionSpaces.end()) return XR_ERROR_HANDLE_INVALID;
        const int hand = it->second.hand;
        const bool grip = it->second.action == OpenXrInput::kGripPoseAction;
        locates.push_back({ hand, it->second.action, baseSpace, time });
        if (!location) return XR_SUCCESS;
        location->locationFlags = 0;
        location->pose = { {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };
        if (m_latchedValid && m_latched[hand].tracked && (!grip || m_latched[hand].gripTracked)) {
            location->pose = m_tracking.RebasePose(baseSpace, grip ? hands[hand].gripPose : hands[hand].pose);
            location->locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                                      XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        }
        return XR_SUCCESS;
    }

    XrResult ApplyHapticFeedback(const XrHapticActionInfo* info, const XrHapticBaseHeader* feedback) {
        if (applyHapticResult != XR_SUCCESS) return applyHapticResult;
        if (!info || !feedback || feedback->type != XR_TYPE_HAPTIC_VIBRATION) return XR_ERROR_VALIDATION_FAILURE;
        const auto it = m_actionRecords.find(info->action);
        if (it == m_actionRecords.end() || it->second.type != XR_ACTION_TYPE_VIBRATION_OUTPUT) {
            return XR_ERROR_ACTION_TYPE_MISMATCH;
        }
        const int hand = HandOf(info->subactionPath);
        if (hand < 0) return XR_ERROR_PATH_UNSUPPORTED;
        const auto* vibration = reinterpret_cast<const XrHapticVibration*>(feedback);
        hapticPulses.push_back({ hand, vibration->duration, vibration->frequency, vibration->amplitude });
        return XR_SUCCESS;
    }

    XrResult StopHapticFeedback(const XrHapticActionInfo* info) {
        ++stopHapticCalls;
        return info ? XR_SUCCESS : XR_ERROR_VALIDATION_FAILURE;
    }

    std::string PathString(XrPath path) const {
        return (path >= 1 && path <= m_pathStrings.size()) ? m_pathStrings[path - 1] : std::string();
    }

    // The handle of a declared action (XR_NULL_HANDLE when none has that name) and a hand's path.
    XrAction ActionByName(const std::string& name) const {
        for (const auto& [handle, record] : m_actionRecords) {
            if (record.name == name) return handle;
        }
        return XR_NULL_HANDLE;
    }
    XrPath HandPath(int hand) const {
        const auto it = m_pathIds.find(hand == 0 ? "/user/hand/left" : "/user/hand/right");
        return it == m_pathIds.end() ? XR_NULL_PATH : it->second;
    }

    // The binding paths a profile suggested for an action name (empty when none).
    std::vector<std::string> BindingsFor(const std::string& profile, const std::string& actionName) const {
        std::vector<std::string> out;
        const auto it = suggestedBindings.find(profile);
        if (it == suggestedBindings.end()) return out;
        for (const auto& [name, path] : it->second) {
            if (name == actionName) out.push_back(path);
        }
        return out;
    }

private:
    FakeOpenXrTracking& m_tracking;
    std::unordered_map<std::string, XrPath> m_pathIds;
    std::vector<std::string> m_pathStrings;
    std::set<XrActionSet> m_liveActionSets;
    std::unordered_map<XrAction, ActionRecord> m_actionRecords;
    struct SpaceInfo {
        int hand = -1;
        std::string action;
    };
    std::unordered_map<XrSpace, SpaceInfo> m_actionSpaces;
    uint32_t m_attachedSessions = 0;
    Hand m_latched[2];
    bool m_latchedValid = false;

    int HandOf(XrPath path) const {
        const std::string s = PathString(path);
        if (s == "/user/hand/left") return 0;
        if (s == "/user/hand/right") return 1;
        return -1;
    }

    template <typename OnActive, typename OnInactive>
    XrResult ReadState(const XrActionStateGetInfo* info, XrActionType expected, OnActive onActive, OnInactive onInactive) {
        if (!info) return XR_ERROR_VALIDATION_FAILURE;
        if (m_attachedSessions == 0) return XR_ERROR_ACTIONSET_NOT_ATTACHED;
        const auto it = m_actionRecords.find(info->action);
        if (it == m_actionRecords.end()) return XR_ERROR_HANDLE_INVALID;
        if (it->second.type != expected) return XR_ERROR_ACTION_TYPE_MISMATCH;
        const int hand = HandOf(info->subactionPath);
        if (hand < 0) return XR_ERROR_PATH_UNSUPPORTED;
        if (!m_latchedValid || !m_latched[hand].tracked) {
            onInactive();
        } else {
            onActive(it->second.name, m_latched[hand]);
        }
        return XR_SUCCESS;
    }
};

} // namespace fs25vr
