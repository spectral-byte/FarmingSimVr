#pragma once

#include "openxr_dispatch.h"
#include "core/math_utils.h"
#include <vector>
#include <string>

namespace fs25vr {

struct HeadPose {
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    Quaternion orientation{ 0.0f, 0.0f, 0.0f, 1.0f };

    bool isValid = false;
    bool positionTracked = false;
    bool orientationTracked = false;

    constexpr HeadPose() = default;
    constexpr HeadPose(const Vector3& pos, const Quaternion& ori,
                       bool valid = true, bool posTracked = true, bool oriTracked = true)
        : position(pos), orientation(ori),
          isValid(valid), positionTracked(posTracked), orientationTracked(oriTracked) {}

    EulerAngles Rotation() const { return QuaternionToEuler(orientation); }
};

inline Quaternion OrientationFromRuntime(const XrQuaternionf& q) {
    const float magnitudeSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (magnitudeSquared < 1e-4f) {
        return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    }
    return Quaternion(q.x, q.y, q.z, q.w);
}

struct HandState {
    bool tracked = false;
    HeadPose aim;
    bool gripTracked = false;
    HeadPose gripPose;
    float trigger = 0.0f;       // 0..1
    float grip = 0.0f;          // 0..1
    float thumbX = 0.0f;        // -1..1, +1 = right
    float thumbY = 0.0f;        // -1..1, +1 = up
    bool thumbClick = false;
    bool buttonLower = false;   // lower face button: Touch X (left) / A (right), Index A
    bool buttonUpper = false;   // upper face button: Touch Y (left) / B (right), Index B
    bool menu = false;          // menu button (Touch left only; Index has none)
};
struct PanelHit {
    bool hit = false;
    float u = 0.0f;
    float v = 0.0f;
    float distance = 0.0f;  // metres along the ray
};

struct PointerCursorOverride {
    bool active = false;
    float u = 0.0f;
    float v = 0.0f;
};

struct EyeSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::vector<XrSwapchainImageD3D12KHR> images;
    uint32_t lastCopiedIndex = 0;
    bool hasCopiedFrame = false;
};

using CursorSwapchain = EyeSwapchain;

struct MenuCursorState {
    static constexpr float kDefaultCursorSize = 0.035f;
    bool visible = false;
    float normX = 0.0f;
    float normY = 0.0f;
    float size = kDefaultCursorSize;
};

}
