#include "menu_quad.h"
#include <algorithm>
#include <cmath>

namespace fs25vr {

float MenuQuad::ClampDistance(float distance) {
    if (std::isnan(distance)) return kDefaultDistance;
    return std::clamp(distance, kMinDistance, kMaxDistance);
}

void MenuQuad::SetDistance(float distance) {
    distance = ClampDistance(distance);
    if (m_hasPose) {
        const Quaternion facing(m_pose.orientation.x, m_pose.orientation.y,
                                m_pose.orientation.z, m_pose.orientation.w);
        const Vector3 shift = RotateVectorByQuaternion(facing, Vector3(0.0f, 0.0f, m_distance - distance));
        m_pose.position.x += shift.x;
        m_pose.position.y += shift.y;
        m_pose.position.z += shift.z;
    }
    m_distance = distance;
}

XrPosef MenuQuad::PoseFromHeadPose(const HeadPose& headPose, float distance) {
    float halfYaw = headPose.Rotation().yaw * 0.5f;
    Quaternion qYaw(0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw));

    Vector3 forwardOffset(0.0f, 0.0f, -distance);
    Vector3 rotatedOffset = RotateVectorByQuaternion(qYaw, forwardOffset);

    XrPosef pose{};
    pose.position.x = headPose.position.x + rotatedOffset.x;
    pose.position.y = headPose.position.y;
    pose.position.z = headPose.position.z + rotatedOffset.z;

    pose.orientation.x = qYaw.x;
    pose.orientation.y = qYaw.y;
    pose.orientation.z = qYaw.z;
    pose.orientation.w = qYaw.w;

    return pose;
}

void MenuQuad::AlignToHeadPose(const HeadPose& head) {
    m_pose = PoseFromHeadPose(head, m_distance);
    m_hasPose = true;
    m_awaitsHead = !head.isValid;
    m_awaitsHeadAfter = 0;
}

XrPosef MenuQuad::Pose() const {
    if (m_hasPose) {
        return m_pose;
    }
    XrPosef pose{};
    pose.position.x = 0.0f;
    pose.position.y = 0.0f;
    pose.position.z = -m_distance;
    pose.orientation.w = 1.0f;
    return pose;
}

XrExtent2Df MenuQuad::Size(uint32_t eyeWidth, uint32_t eyeHeight) const {
    if (m_hasCustomSize) {
        return m_customSize;
    }
    float height = 0.9f;
    float aspect = 16.0f / 9.0f;
    if (eyeHeight > 0) {
        aspect = static_cast<float>(eyeWidth) / static_cast<float>(eyeHeight);
    }
    return XrExtent2Df{ height * aspect, height };
}

PanelHit MenuQuad::RayHit(const XrPosef& quadPose, const XrExtent2Df& quadSize,
                          const Vector3& rayOrigin, const Quaternion& rayOrientation) {
    return RayHitAlong(quadPose, quadSize, rayOrigin,
                       RotateVectorByQuaternion(rayOrientation, Vector3(0.0f, 0.0f, -1.0f)));
}

PanelHit MenuQuad::RayHitAlong(const XrPosef& quadPose, const XrExtent2Df& quadSize,
                               const Vector3& rayOrigin, const Vector3& rayDirection) {
    PanelHit miss;
    if (quadSize.width <= 0.0f || quadSize.height <= 0.0f) {
        return miss;
    }
    const float length = std::sqrt(rayDirection.x * rayDirection.x + rayDirection.y * rayDirection.y + rayDirection.z * rayDirection.z);
    if (!(length > 1e-6f)) {
        return miss;
    }
    const Quaternion quadToWorld(quadPose.orientation.x, quadPose.orientation.y, quadPose.orientation.z, quadPose.orientation.w);
    const Quaternion worldToQuad(-quadToWorld.x, -quadToWorld.y, -quadToWorld.z, quadToWorld.w);
    const Vector3 offset(rayOrigin.x - quadPose.position.x, rayOrigin.y - quadPose.position.y, rayOrigin.z - quadPose.position.z);
    const Vector3 origin = RotateVectorByQuaternion(worldToQuad, offset);
    const Vector3 worldDir(rayDirection.x / length, rayDirection.y / length, rayDirection.z / length);
    const Vector3 dir = RotateVectorByQuaternion(worldToQuad, worldDir);

    constexpr float kMinApproach = 1e-6f;
    if (dir.z > -kMinApproach || origin.z <= 0.0f) {
        return miss;
    }
    const float t = -origin.z / dir.z;
    const float x = origin.x + dir.x * t;
    const float y = origin.y + dir.y * t;
    const float u = x / quadSize.width + 0.5f;
    const float v = 0.5f - y / quadSize.height;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return miss;
    }
    PanelHit hit;
    hit.hit = true;
    hit.u = u;
    hit.v = v;
    hit.distance = t;
    return hit;
}

}
