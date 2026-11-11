#pragma once

#include "core/math_utils.h"
#include "openxr_dispatch.h"
#include "openxr_types.h"

namespace fs25vr {

// The menu quad's geometry (spec #247): where the panel for menus and 2D Floating Window Mode sits, how big it is,
// how far in front of the head it is placed (Menu Distance), and where a ray lands on it (the Pointer Ray's panel
// hit). Plain maths on poses and vectors: it holds no OpenXR handle, and the OpenXR manager keeps the spaces and
// the layer submission and asks it for the pose and size to submit. Single-threaded, like its caller (ADR-0018).
class MenuQuad {
public:
    // Menu Distance: how far in front of the head the quad is placed, in metres. The quad keeps its size, so further
    // away it fills less of the view. Clamped to kMinDistance..kMaxDistance; NaN counts as the default.
    static constexpr float kDefaultDistance = 1.5f;
    static constexpr float kMinDistance = 1.0f;
    static constexpr float kMaxDistance = 5.0f;
    static float ClampDistance(float distance);

    // Sets the Menu Distance. A quad already placed moves straight back or forward along the way it faces, so it
    // stays in front of the head that placed it.
    void SetDistance(float distance);
    float Distance() const { return m_distance; }

    // Places the quad `Distance()` in front of `head`, at eye level along the head's yaw. The head must be located in
    // the space the quad is submitted in. A head that is not valid (the first menu frame of a session often has none)
    // leaves the quad waiting for one: see AwaitsHead.
    void AlignToHeadPose(const HeadPose& head);
    static XrPosef PoseFromHeadPose(const HeadPose& head, float distance);

    // True once the quad has been placed from a head pose.
    bool HasPose() const { return m_hasPose; }
    // The pose to submit: the placed pose, else `Distance()` straight ahead of the space's origin.
    XrPosef Pose() const;

    // The size to submit: the one set by hand, else 0.9 m high at the aspect of the eye swapchain
    // (`eyeWidth` x `eyeHeight`; 16:9 while there is none, which is height 0).
    XrExtent2Df Size(uint32_t eyeWidth, uint32_t eyeHeight) const;
    void SetCustomSize(float width, float height) { m_customSize = { width, height }; m_hasCustomSize = true; }
    void ClearCustomSize() { m_hasCustomSize = false; }

    // The Menu Distance and the size back to their defaults, and the quad unplaced.
    void ResetSettings() { m_distance = kDefaultDistance; m_hasCustomSize = false; m_hasPose = false; }

    // The quad moves to the head on the first rendered frame with a valid head pose predicted at or after the time
    // given to AwaitHeadAfter: it was placed from no pose, the runtime recentered the local space under it, or the
    // running session was focused for the first time. AlignToHeadPose settles it (or waits again, without a pose).
    void AwaitHeadAfter(XrTime time) { m_awaitsHead = true; m_awaitsHeadAfter = time; }
    bool AwaitsHead(bool headPoseValid, XrTime predictedDisplayTime) const {
        return m_awaitsHead && m_hasPose && headPoseValid && predictedDisplayTime >= m_awaitsHeadAfter;
    }

    // Pure ray (origin, -Z of rayOrientation) against a quad of `quadSize` at `quadPose`; the quad is hit from its
    // front side only, like it is seen.
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

}  // namespace fs25vr
