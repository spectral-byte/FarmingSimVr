#pragma once

#include "openxr_dispatch.h"
#include <vector>
#include <string>

namespace fs25vr {

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
