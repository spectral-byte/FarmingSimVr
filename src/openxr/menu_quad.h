#pragma once

#include "core/math_utils.h"
#include "openxr_dispatch.h"
#include "openxr_types.h"

namespace fs25vr {

class MenuQuad {
public:
    // Menu Distance: how far in front of the head the quad is placed, in metres. The quad keeps its size, so further
    // away it fills less of the view. Clamped to kMinDistance..kMaxDistance; NaN counts as the default.
    static constexpr float kDefaultDistance = 1.5f;
    static constexpr float kMinDistance = 1.0f;
    static constexpr float kMaxDistance = 5.0f;
    static float ClampDistance(float distance);

    void SetDistance(float distance);
    float Distance() const { return m_distance; }

    void AlignToHeadPose(const HeadPose& head);
    static XrPosef PoseFromHeadPose(const HeadPose& head, float distance);

    bool HasPose() const { return m_hasPose; }
    // The pose to submit: the placed pose, else `Distance()` straight ahead of the space's origin.
    XrPosef Pose() const;

    // The size to submit: the one set by hand, else 0.9 m high at the aspect of the eye swapchain
    // (`eyeWidth` x `eyeHeight`; 16:9 while there is none, which is height 0).
    XrExtent2Df Size(uint32_t eyeWidth, uint32_t eyeHeight) const;
    void SetCustomSize(float width, float height) { m_customSize = { width, height }; m_hasCustomSize = true; }
    void ClearCustomSize() { m_hasCustomSize = false; }

    void ResetSettings() { m_distance = kDefaultDistance; m_hasCustomSize = false; m_hasPose = false; }

    void AwaitHeadAfter(XrTime time) { m_awaitsHead = true; m_awaitsHeadAfter = time; }
    bool AwaitsHead(bool headPoseValid, XrTime predictedDisplayTime) const {
        return m_awaitsHead && m_hasPose && headPoseValid && predictedDisplayTime >= m_awaitsHeadAfter;
    }

    static PanelHit RayHit(const XrPosef& quadPose, const XrExtent2Df& quadSize,
                           const Vector3& rayOrigin, const Quaternion& rayOrientation);
    // The same for a ray with a direction vector (any length above zero; `distance` is along the unit direction).
    static PanelHit RayHitAlong(const XrPosef& quadPose, const XrExtent2Df& quadSize,
                                const Vector3& rayOrigin, const Vector3& rayDirection);

private:
    float m_distance = kDefaultDistance;
    XrExtent2Df m_customSize{ 1.6f, 0.9f };
    bool m_hasCustomSize = false;
    XrPosef m_pose{};
    bool m_hasPose = false;
    bool m_awaitsHead = false;
    XrTime m_awaitsHeadAfter = 0;
};

}
