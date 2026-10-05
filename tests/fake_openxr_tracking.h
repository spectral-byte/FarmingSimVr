#pragma once

#include "openxr/openxr_manager.h"
#include <cmath>
#include <unordered_map>
#include <vector>

namespace fs25vr {

// Head and eye tracking model for fake OpenXR dispatches. Tests drive the head pose and the
// per-eye FOVs through SetHeadPose()/SetEyeFov(); fakes forward CreateReferenceSpace(),
// DestroySpace(), LocateSpace() and LocateViews() here so the OpenXrManager observes them
// exactly as it would from a runtime, including Recenter() re-basing the local space.
class FakeOpenXrTracking {
public:
    // Raw location of the view space in the runtime's tracking origin.
    XrSpaceLocation head{
        XR_TYPE_SPACE_LOCATION,
        nullptr,
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT,
        { {0.0f, 0.0f, 0.0f, 1.0f}, {0.1f, 0.2f, -0.3f} }
    };

    XrViewState viewState{
        XR_TYPE_VIEW_STATE,
        nullptr,
        XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT |
        XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT
    };
    std::vector<XrView> views;

    struct SpaceInfo {
        XrReferenceSpaceType type;
        XrPosef poseInReferenceSpace;
    };
    std::unordered_map<XrSpace, SpaceInfo> referenceSpaces;

    XrResult locateSpaceResult = XR_SUCCESS;
    XrResult locateViewsResult = XR_SUCCESS;

    // Places the head. Orientation may be given as a quaternion or as Euler angles (when the
    // quaternion is identity); validity and tracking flags follow the pose.
    void SetHeadPose(const HeadPose& pose) {
        head.pose.position = { pose.position.x, pose.position.y, pose.position.z };

        Quaternion orientation = pose.orientation;
        bool hasRotation = pose.rotation.pitch != 0.0f || pose.rotation.yaw != 0.0f || pose.rotation.roll != 0.0f;
        bool hasOrientation = orientation.x != 0.0f || orientation.y != 0.0f ||
                              orientation.z != 0.0f || orientation.w != 1.0f;
        if (hasRotation && !hasOrientation) {
            float halfYaw = pose.rotation.yaw * 0.5f;
            float halfPitch = pose.rotation.pitch * 0.5f;
            float halfRoll = pose.rotation.roll * 0.5f;
            Quaternion qYaw(0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw));
            Quaternion qPitch(std::sin(halfPitch), 0.0f, 0.0f, std::cos(halfPitch));
            Quaternion qRoll(0.0f, 0.0f, std::sin(halfRoll), std::cos(halfRoll));
            orientation = qYaw * qPitch * qRoll;
        }
        head.pose.orientation = { orientation.x, orientation.y, orientation.z, orientation.w };

        head.locationFlags = 0;
        if (pose.isValid) {
            head.locationFlags |= XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        }
        if (pose.positionTracked) head.locationFlags |= XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        if (pose.orientationTracked) head.locationFlags |= XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    }

    // Sets one eye's field of view. A runtime always reports both eyes, so an eye that was never
    // set mirrors the first one set (left/right swapped and negated). Eyes sit at the head origin
    // so the manager keeps its configured IPD.
    void SetEyeFov(uint32_t eyeIndex, const XrFovf& fov) {
        const uint32_t eye = eyeIndex >= 2 ? 0 : eyeIndex;
        if (EnsureViews()) {
            views[1 - eye].fov = { -fov.angleRight, -fov.angleLeft, fov.angleUp, fov.angleDown };
        }
        views[eye].fov = fov;
    }

    XrResult CreateReferenceSpace(const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space) {
        static uintptr_t s_spaceHandle = 0x50ACE001;
        XrSpace handle = reinterpret_cast<XrSpace>(s_spaceHandle++);
        if (space) *space = handle;
        if (createInfo) {
            referenceSpaces[handle] = { createInfo->referenceSpaceType, createInfo->poseInReferenceSpace };
        }
        return XR_SUCCESS;
    }

    void DestroySpace(XrSpace space) { referenceSpaces.erase(space); }

    // Locates the head relative to baseSpace; a LOCAL base is re-based by its pose in the
    // runtime's tracking origin.
    XrResult LocateSpace(XrSpace baseSpace, XrSpaceLocation* location) const {
        if (locateSpaceResult != XR_SUCCESS) return locateSpaceResult;
        if (!location) return XR_SUCCESS;

        *location = head;

        auto it = referenceSpaces.find(baseSpace);
        if (it != referenceSpaces.end() && it->second.type == XR_REFERENCE_SPACE_TYPE_LOCAL) {
            const XrPosef& refPose = it->second.poseInReferenceSpace;
            float dx = head.pose.position.x - refPose.position.x;
            float dy = head.pose.position.y - refPose.position.y;
            float dz = head.pose.position.z - refPose.position.z;

            Quaternion qRefInv(-refPose.orientation.x, -refPose.orientation.y, -refPose.orientation.z, refPose.orientation.w);
            Vector3 relPos = RotateVectorByQuaternion(qRefInv, Vector3(dx, dy, dz));

            Quaternion qHead(head.pose.orientation.x, head.pose.orientation.y,
                             head.pose.orientation.z, head.pose.orientation.w);
            Quaternion relOri = qRefInv * qHead;

            location->pose.position = { relPos.x, relPos.y, relPos.z };
            location->pose.orientation = { relOri.x, relOri.y, relOri.z, relOri.w };
        }
        return XR_SUCCESS;
    }

    XrResult LocateViews(XrViewState* outViewState, uint32_t viewCapacityInput,
                         uint32_t* viewCountOutput, XrView* outViews) const {
        if (locateViewsResult != XR_SUCCESS) return locateViewsResult;
        if (outViewState) *outViewState = viewState;
        if (viewCountOutput) *viewCountOutput = static_cast<uint32_t>(views.size());
        if (viewCapacityInput == 0) return XR_SUCCESS;
        uint32_t count = (viewCapacityInput < views.size()) ? viewCapacityInput : static_cast<uint32_t>(views.size());
        for (uint32_t i = 0; i < count; ++i) {
            outViews[i] = views[i];
        }
        return XR_SUCCESS;
    }

private:
    static Vector3 RotateVectorByQuaternion(const Quaternion& q, const Vector3& v) {
        return OpenXrManager::RotateVectorByQuaternion(q, v);
    }

    // Returns true when the views were just created.
    bool EnsureViews() {
        if (views.size() >= 2) return false;
        views.assign(2, XrView{ XR_TYPE_VIEW, nullptr });
        for (auto& view : views) {
            view.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
        }
        return true;
    }
};

} // namespace fs25vr
