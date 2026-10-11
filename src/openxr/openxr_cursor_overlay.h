#pragma once

#include "openxr_dispatch.h"
#include "core/math_utils.h"
#include "core/raii_utils.h"
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <string>

#include "openxr_types.h"

namespace fs25vr {

class OpenXrCursorOverlay {
public:
    OpenXrCursorOverlay() = default;
    ~OpenXrCursorOverlay();

    // Procedural cursor pattern decoding and pixel generation
    static std::vector<uint32_t> GenerateCursorPixels(uint32_t width, uint32_t height);

    // Cursor quad pose calculation
    static XrPosef ComputeCursorQuadPose(const XrPosef& menuQuadPose, const XrExtent2Df& menuQuadSize,
                                         const MenuCursorState& cursorState, float cursorWidth, float cursorHeight);
    static XrPosef ComputeCursorQuadPose(const XrPosef& menuQuadPose, const XrExtent2Df& menuQuadSize,
                                         float normX, float normY, float cursorWidth, float cursorHeight);

    // Swapchain lifecycle
    bool CreateSwapchain(XrSession session, std::shared_ptr<IOpenXrDispatch> dispatch,
                         uint32_t width = 64, uint32_t height = 64,
                         DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                         ID3D12Device* device = nullptr, ID3D12CommandQueue* queue = nullptr);
    void DestroySwapchain(std::shared_ptr<IOpenXrDispatch> dispatch);
    bool HasSwapchain() const { return m_hasSwapchain; }
    const EyeSwapchain& GetSwapchain() const { return m_swapchain; }

    // D3D12 Texture upload
    bool UploadTexture(ID3D12Device* device, ID3D12CommandQueue* queue, std::shared_ptr<IOpenXrDispatch> dispatch);
    bool IsTextureUploaded() const { return m_textureUploaded; }
    void ResetTextureUploaded() { m_textureUploaded = false; }

    // Composition layer quad generation
    bool PrepareCompositionLayer(const XrCompositionLayerQuad& menuQuad, const MenuCursorState& cursorState,
                                 XrSpace localSpace, XrCompositionLayerQuad& outCursorQuadLayer,
                                 ID3D12Device* device, ID3D12CommandQueue* queue,
                                 std::shared_ptr<IOpenXrDispatch> dispatch);

    const XrCompositionLayerQuad& GetLastSubmittedCursorQuadLayer() const { return m_lastSubmittedCursorQuadLayer; }
    bool HasLastSubmittedCursorQuadLayer() const { return m_hasLastSubmittedCursorQuadLayer; }
    void ResetLastSubmittedCursorQuadLayer() { m_hasLastSubmittedCursorQuadLayer = false; }

private:
    bool EnumerateSwapchainImages(std::shared_ptr<IOpenXrDispatch> dispatch);

    EyeSwapchain m_swapchain;
    bool m_hasSwapchain = false;
    bool m_textureUploaded = false;
    XrCompositionLayerQuad m_lastSubmittedCursorQuadLayer{ XR_TYPE_COMPOSITION_LAYER_QUAD };
    bool m_hasLastSubmittedCursorQuadLayer = false;
};

} // namespace fs25vr
