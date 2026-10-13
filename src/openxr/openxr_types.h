#pragma once

#include "openxr_dispatch.h"
#include "core/math_utils.h"
#include <vector>
#include <string>

namespace fs25vr {

struct HeadPose {
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    Quaternion orientation{ 0.0f, 0.0f, 0.0f, 1.0f };
    EulerAngles rotation{ 0.0f, 0.0f, 0.0f };

    bool isValid = false;
    bool positionTracked = false;
    bool orientationTracked = false;

    constexpr HeadPose() = default;
    constexpr HeadPose(const Vector3& pos, const Quaternion& ori, const EulerAngles& rot,
                       bool valid = true, bool posTracked = true, bool oriTracked = true)
        : position(pos), orientation(ori), rotation(rot),
          isValid(valid), positionTracked(posTracked), orientationTracked(oriTracked) {}
};

// One hand's Motion Controller state as the Companion Mod sees it (VRBridge.getHandState).
// Hand ids: 0 = left, 1 = right. `aim` is the aim pose in the head pose's convention: the same
// reference space (the Recenter-aware local space) and the same predicted display time as the
// head pose. `gripPose` is the grip pose (where a Virtual Hand is drawn) in the same convention, with
// its own `gripTracked` flag: it is untracked whenever the hand is, and also when the runtime refuses
// the grip pose or can not locate it. An untracked hand reports tracked = false, a default aim pose,
// an untracked default grip pose and zeroed inputs.
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
// Where a Motion Controller's ray lands on the floating menu / 2D Floating Window Mode panel.
// u runs left to right and v top to bottom, both 0..1 (the drawn cursor's convention, which is not
// the game's GUI convention: the GUI's y runs bottom to top). A miss has hit = false and zeros.
struct PanelHit {
    bool hit = false;
    float u = 0.0f;
    float v = 0.0f;
    float distance = 0.0f;  // metres along the ray
};

// The drawn menu cursor placed by the Pointer Ray instead of the desk mouse (ADR-0009). While active,
// the cursor layer is drawn at (u, v) in PanelHit coordinates and the Win32 cursor is ignored.
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

} // namespace fs25vr
