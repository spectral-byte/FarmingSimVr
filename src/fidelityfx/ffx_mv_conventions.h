#pragma once

#include "ffx_defs.h"
#include <algorithm>
#include <cmath>

namespace fs25vr {

// The conventions the motion vector correction needs for one FSR3 dispatch. FSR3 carries them in its own
// descriptors, so they are read from the dispatch description (scale, jitter, camera near/far/FOV) and the
// flags the context was created with (depth convention), never from the DLSS creation flags.
struct FfxMvConventions {
    float mvScaleX = 1.0f; // pixels = stored vector * scale
    float mvScaleY = 1.0f;
    float jitterX = 0.0f;
    float jitterY = 0.0f;
    bool depthInverted = false;
    bool depthInfinite = false;
    // The dispatch's camera fields give a usable projection (finite near/far, a sane FOV). Otherwise the
    // camera record's projection is used.
    bool hasProjection = false;
    float tanHalfFovY = 0.0f;
    float nearClip = 0.0f; // metres
    float farClip = 0.0f;
    // Why the correction cannot honour this dispatch's conventions; null when it can.
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

    // The pass is built for the engine's reverse-Z depth, render-resolution vectors with no jitter in them.
    if (!c.depthInverted) {
        c.unsupportedReason = "FSR3 depth is not reverse-Z";
    } else if (c.depthInfinite) {
        c.unsupportedReason = "FSR3 depth has an infinite far plane";
    } else if (createFlags & FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) {
        c.unsupportedReason = "FSR3 motion vectors are at display resolution";
    } else if (createFlags & FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION) {
        c.unsupportedReason = "FSR3 motion vectors include jitter";
    }

    // FSR3 takes near and far in view-space units (scaled to metres by viewSpaceToMetersFactor, 0 meaning
    // unset) and, for reverse depth, applications pass them either way round: normalise.
    const float factor = desc.viewSpaceToMetersFactor > 0.0f ? desc.viewSpaceToMetersFactor : 1.0f;
    const float a = desc.cameraNear;
    const float b = desc.cameraFar;
    constexpr float kInfiniteFar = 1e30f;
    const float lo = (std::min)(a, b);
    const float hi = (std::max)(a, b);
    const float fov = desc.cameraFovAngleVertical;
    if (std::isfinite(lo) && std::isfinite(hi) && lo > 0.0f && hi > lo && hi < kInfiniteFar &&
        std::isfinite(fov) && fov > 0.0f && fov < 3.1415926f) {
        c.hasProjection = true;
        c.tanHalfFovY = std::tan(fov * 0.5f);
        c.nearClip = lo * factor;
        c.farClip = hi * factor;
    }
    return c;
}

} // namespace fs25vr
