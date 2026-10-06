#pragma once

// Minimal declarations of the FidelityFX API (ffx-api, amd_fidelityfx_loader_dx12.dll) that the
// interceptor touches: the five entry points, the header every descriptor starts with, and the
// upscaler's create and dispatch descriptors. Layouts follow the public ffx_api.h / ffx_upscale.h;
// only the upscale descriptors are interpreted, every other descriptor type is forwarded untouched.

#include <cstdint>

namespace fs25vr {

using ffxReturnCode_t = uint32_t;
inline constexpr ffxReturnCode_t FFX_API_RETURN_OK = 0;
inline constexpr ffxReturnCode_t FFX_API_RETURN_ERROR = 1;
inline constexpr ffxReturnCode_t FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE = 2;
inline constexpr ffxReturnCode_t FFX_API_RETURN_ERROR_RUNTIME_ERROR = 3;
inline constexpr ffxReturnCode_t FFX_API_RETURN_NO_PROVIDER = 4;
inline constexpr ffxReturnCode_t FFX_API_RETURN_ERROR_MEMORY = 5;
inline constexpr ffxReturnCode_t FFX_API_RETURN_ERROR_PARAMETER = 6;

using ffxContext = void*;
using ffxStructType_t = uint64_t;

struct ffxApiHeader {
    ffxStructType_t type;
    ffxApiHeader* pNext;
};
using ffxCreateContextDescHeader = ffxApiHeader;
using ffxConfigureDescHeader = ffxApiHeader;
using ffxQueryDescHeader = ffxApiHeader;
using ffxDispatchDescHeader = ffxApiHeader;

// Opaque to the interceptor: handed through to the loader as received.
struct ffxAllocationCallbacks;

inline constexpr ffxStructType_t FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE = 0x00010000u;
inline constexpr ffxStructType_t FFX_API_DISPATCH_DESC_TYPE_UPSCALE = 0x00010001u;

// FfxApiCreateContextUpscaleFlags
inline constexpr uint32_t FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE = 1u << 0;
inline constexpr uint32_t FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS = 1u << 1;
inline constexpr uint32_t FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION = 1u << 2;
inline constexpr uint32_t FFX_UPSCALE_ENABLE_DEPTH_INVERTED = 1u << 3;
inline constexpr uint32_t FFX_UPSCALE_ENABLE_DEPTH_INFINITE = 1u << 4;
inline constexpr uint32_t FFX_UPSCALE_ENABLE_AUTO_EXPOSURE = 1u << 5;
inline constexpr uint32_t FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION = 1u << 6;
inline constexpr uint32_t FFX_UPSCALE_ENABLE_DEBUG_CHECKING = 1u << 7;
inline constexpr uint32_t FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE = 1u << 8;
inline constexpr uint32_t FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION = 1u << 9;

struct ffxApiDimensions2D {
    uint32_t width;
    uint32_t height;
};

struct ffxApiFloatCoords2D {
    float x;
    float y;
};

struct ffxApiResourceDescription {
    uint32_t type;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t mipCount;
    uint32_t flags;
    uint32_t usage;
};

struct ffxApiResource {
    void* resource;
    ffxApiResourceDescription description;
    uint32_t state;
};

struct ffxCreateContextDescUpscale {
    ffxCreateContextDescHeader header;
    uint32_t flags;
    ffxApiDimensions2D maxRenderSize;
    ffxApiDimensions2D maxUpscaleSize;
    void* fpMessage;
};

struct ffxDispatchDescUpscale {
    ffxDispatchDescHeader header;
    void* commandList;
    ffxApiResource color;
    ffxApiResource depth;
    ffxApiResource motionVectors;
    ffxApiResource exposure;
    ffxApiResource reactive;
    ffxApiResource transparencyAndComposition;
    ffxApiResource output;
    ffxApiFloatCoords2D jitterOffset;
    ffxApiFloatCoords2D motionVectorScale;
    ffxApiDimensions2D renderSize;
    ffxApiDimensions2D upscaleSize;
    bool enableSharpening;
    float sharpness;
    float frameTimeDelta;
    float preExposure;
    bool reset;
    float cameraNear;
    float cameraFar;
    float cameraFovAngleVertical;
    float viewSpaceToMetersFactor;
    uint32_t flags;
};

using PFN_ffxCreateContext = ffxReturnCode_t (*)(ffxContext*, ffxCreateContextDescHeader*, const ffxAllocationCallbacks*);
using PFN_ffxDestroyContext = ffxReturnCode_t (*)(ffxContext*, const ffxAllocationCallbacks*);
using PFN_ffxConfigure = ffxReturnCode_t (*)(ffxContext*, const ffxConfigureDescHeader*);
using PFN_ffxQuery = ffxReturnCode_t (*)(ffxContext*, ffxQueryDescHeader*);
using PFN_ffxDispatch = ffxReturnCode_t (*)(ffxContext*, const ffxDispatchDescHeader*);

// Magic identifier to safely identify virtual FidelityFX contexts: 'VFFX'
inline constexpr uint32_t VIRTUAL_FFX_CONTEXT_MAGIC = 0x56464658;

// Opaque handle handed to the game in place of the two real upscaler contexts.
struct VirtualFfxContext {
    uint32_t magic = VIRTUAL_FFX_CONTEXT_MAGIC;
    uint32_t id = 0;
    ffxContext left = nullptr;
    ffxContext right = nullptr;
    uint32_t flags = 0;
    ffxApiDimensions2D maxRenderSize{};
    ffxApiDimensions2D maxUpscaleSize{};
};

} // namespace fs25vr
