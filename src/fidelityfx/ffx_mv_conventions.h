#pragma once

#include "ffx_defs.h"
#include <algorithm>
#include <cmath>

namespace fs25vr {

struct FfxMvConventions {
    float mvScaleX = 1.0f; // pixels = stored vector * scale
    float mvScaleY = 1.0f;
    float jitterX = 0.0f;
    float jitterY = 0.0f;
    bool depthInverted = false;
    bool depthInfinite = false;
    bool hasProjection = false;
    float tanHalfFovY = 0.0f;
    float nearClip = 0.0f; // metres
    float farClip = 0.0f;
    const char* unsupportedReason = nullptr;
};

inline FfxMvConventions ReadFfxMvConventions(uint32_t createFlags, const ffxDispatchDescUpscale& desc) {
    FfxMvConventions c;
    c.mvScaleX = desc.motionVectorScale.x;
    c.mvScaleY = desc.motionVectorScale.y;
    c.jitterX = desc.jitterOffset.x;
    c.jitterY = desc.jitterOffset.y;
    c.depthInverted = (createFlags & FFX_UPSCALE_ENABLE_DEPTH_INVERTED) != 0;
    c.depthInfinite = (createFlags & FFX_UPSCALE_ENABLE_DEPTH_INFINITE) != 0;

    if (!c.depthInverted) {
        c.unsupportedReason = "FSR3 depth is not reverse-Z";
    } else if (createFlags & FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) {
        c.unsupportedReason = "FSR3 motion vectors are at display resolution";
    } else if (createFlags & FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION) {
        c.unsupportedReason = "FSR3 motion vectors include jitter";
    }

    const bool factorOk = std::isfinite(desc.viewSpaceToMetersFactor) && desc.viewSpaceToMetersFactor >= 0.0f;
    const float factor = desc.viewSpaceToMetersFactor > 0.0f ? desc.viewSpaceToMetersFactor : 1.0f;
    const float a = desc.cameraNear;
    const float b = desc.cameraFar;
    constexpr float kInfiniteFar = 1e30f;
    const float lo = (std::min)(a, b);
    const float hi = (std::max)(a, b);
    const float fov = desc.cameraFovAngleVertical;
    const bool fovOk = std::isfinite(fov) && fov > 0.0f && fov < 3.1415926f;
    const float tanHalfFovY = fovOk ? std::tan(fov * 0.5f) : 0.0f;
    const bool tanOk = fovOk && std::isfinite(tanHalfFovY) && tanHalfFovY > 0.0f;
    if (c.depthInfinite) {
        float nearPlane = 0.0f;
        for (float v : {a, b}) {
            if (std::isfinite(v) && v > 0.0f && v < kInfiniteFar && (nearPlane == 0.0f || v < nearPlane)) nearPlane = v;
        }
        const float scaledNear = nearPlane * factor;
        if (factorOk && nearPlane > 0.0f && tanOk && std::isfinite(scaledNear) && scaledNear > 0.0f) {
            c.hasProjection = true;
            c.tanHalfFovY = tanHalfFovY;
            c.nearClip = scaledNear;
            c.farClip = 0.0f;
        }
    } else if (factorOk && std::isfinite(lo) && std::isfinite(hi) && lo > 0.0f && hi > lo && hi < kInfiniteFar && tanOk) {
        const float scaledNear = lo * factor;
        const float scaledFar = hi * factor;
        if (std::isfinite(scaledNear) && std::isfinite(scaledFar) && scaledNear > 0.0f && scaledFar > scaledNear) {
            c.hasProjection = true;
            c.tanHalfFovY = tanHalfFovY;
            c.nearClip = scaledNear;
            c.farClip = scaledFar;
        }
    }
    return c;
}

}
