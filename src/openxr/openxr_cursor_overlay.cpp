#include "openxr_cursor_overlay.h"
#include "core/logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace {

constexpr DWORD kCursorUploadWaitTimeoutMs = 5000;

bool UploadPixelsToResource(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* destRes,
                            uint32_t width, uint32_t height, const std::vector<uint32_t>& pixels) {
    D3D12_RESOURCE_DESC textureDesc = destRes->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);
    if (totalBytes == 0 || numRows == 0 || numRows > height) {
        return false;
    }

    const size_t copyBytes = static_cast<size_t>(width) * sizeof(uint32_t);
    if (copyBytes > footprint.Footprint.RowPitch || pixels.size() < static_cast<size_t>(numRows) * width) {
        return false;
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = totalBytes;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
    if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&uploadBuffer))) || !uploadBuffer) {
        return false;
    }

    {
        fs25vr::ScopedResourceMap<uint8_t> mappedBuffer(uploadBuffer.Get(), 0);
        if (!mappedBuffer) {
            return false;
        }
        const uint8_t* pSrc = reinterpret_cast<const uint8_t*>(pixels.data());
        uint8_t* pDst = mappedBuffer.Get() + footprint.Offset;
        for (UINT r = 0; r < numRows; ++r) {
            memcpy(pDst + static_cast<size_t>(r) * footprint.Footprint.RowPitch,
                   pSrc + static_cast<size_t>(r) * copyBytes,
                   copyBytes);
        }
    }

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmdAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc))) || !cmdAlloc) {
        return false;
    }
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr,
                                         IID_PPV_ARGS(&cmdList))) || !cmdList) {
        return false;
    }
    fs25vr::ScopedCommandListRecording cmdListScope(cmdList.Get());

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) || !fence) {
        return false;
    }
    fs25vr::ScopedEventHandle hEvent = fs25vr::ScopedEventHandle::Create(nullptr, FALSE, FALSE, nullptr);
    if (!hEvent.IsValid()) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = destRes;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = destRes;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = uploadBuffer.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = footprint;

    cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    cmdList->ResourceBarrier(1, &barrier);

    if (FAILED(cmdListScope.Close())) {
        return false;
    }
    ID3D12CommandList* lists[] = { cmdList.Get() };
    queue->ExecuteCommandLists(1, lists);

    if (FAILED(queue->Signal(fence.Get(), 1))) {
        return false;
    }
    if (fence->GetCompletedValue() < 1) {
        if (FAILED(fence->SetEventOnCompletion(1, hEvent.Get()))) {
            return false;
        }
        if (WaitForSingleObject(hEvent.Get(), kCursorUploadWaitTimeoutMs) != WAIT_OBJECT_0) {
            return false;
        }
    }
    return true;
}

static const char* const s_arrowCursorPattern[32] = {
    "X                               ",
    "XX                              ",
    "X.X                             ",
    "X..X                            ",
    "X...X                           ",
    "X....X                          ",
    "X.....X                         ",
    "X......X                        ",
    "X.......X                       ",
    "X........X                      ",
    "X.....XXXXX                     ",
    "X..X..X                         ",
    "X.X X..X                        ",
    "XX   X..X                       ",
    "X     X..X                      ",
    "       X..X                     ",
    "        XX                      ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                "
};

}

namespace fs25vr {

OpenXrCursorOverlay::~OpenXrCursorOverlay() = default;

std::vector<uint32_t> OpenXrCursorOverlay::GenerateCursorPixels(uint32_t width, uint32_t height) {
    std::vector<uint32_t> pixels(width * height, 0x00000000);
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t srcY = (y * 32) / height;
        const char* rowStr = (srcY < 32) ? s_arrowCursorPattern[srcY] : "";
        size_t rowLen = strlen(rowStr);
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t srcX = (x * 32) / width;
            char ch = (srcX < rowLen) ? rowStr[srcX] : ' ';
            uint32_t color = 0x00000000;
            if (ch == 'X') {
                color = 0xFF000000;
            } else if (ch == '.') {
                color = 0xFFFFFFFF;
            }
            pixels[y * width + x] = color;
        }
    }
    return pixels;
}

XrPosef OpenXrCursorOverlay::ComputeCursorQuadPose(const XrPosef& menuQuadPose, const XrExtent2Df& menuQuadSize,
                                                   const MenuCursorState& cursorState, float cursorWidth, float cursorHeight) {
    return ComputeCursorQuadPose(menuQuadPose, menuQuadSize, cursorState.normX, cursorState.normY, cursorWidth, cursorHeight);
}

XrPosef OpenXrCursorOverlay::ComputeCursorQuadPose(const XrPosef& menuQuadPose, const XrExtent2Df& menuQuadSize,
                                                   float normX, float normY, float cursorWidth, float cursorHeight) {
    float u = std::max(0.0f, std::min(1.0f, normX));
    float v = std::max(0.0f, std::min(1.0f, normY));

    float localX = (u - 0.5f) * menuQuadSize.width;
    float localY = (0.5f - v) * menuQuadSize.height;

    Vector3 localOffset(
        localX + 0.5f * cursorWidth,
        localY - 0.5f * cursorHeight,
        0.002f
    );

    Quaternion q(menuQuadPose.orientation.x, menuQuadPose.orientation.y,
                 menuQuadPose.orientation.z, menuQuadPose.orientation.w);
    Vector3 rotatedOffset = RotateVectorByQuaternion(q, localOffset);

    XrPosef cursorPose{};
    cursorPose.position.x = menuQuadPose.position.x + rotatedOffset.x;
    cursorPose.position.y = menuQuadPose.position.y + rotatedOffset.y;
    cursorPose.position.z = menuQuadPose.position.z + rotatedOffset.z;
    cursorPose.orientation = menuQuadPose.orientation;

    return cursorPose;
}

bool OpenXrCursorOverlay::CreateSwapchain(XrSession session, std::shared_ptr<IOpenXrDispatch> dispatch,
                                         uint32_t width, uint32_t height, DXGI_FORMAT format,
                                         ID3D12Device* device, ID3D12CommandQueue* queue) {
    m_lastXrResult = XR_SUCCESS;
    if (session == XR_NULL_HANDLE || !dispatch) {
        return false;
    }
    if (m_hasSwapchain) {
        return true;
    }

    DXGI_FORMAT selectedFormat = format;
    uint32_t formatCount = 0;
    if (XR_SUCCEEDED(dispatch->EnumerateSwapchainFormats(session, 0, &formatCount, nullptr)) && formatCount > 0) {
        std::vector<int64_t> supportedFormats(formatCount);
        if (XR_SUCCEEDED(dispatch->EnumerateSwapchainFormats(session, formatCount, &formatCount, supportedFormats.data()))) {
            bool formatSupported = false;
            for (int64_t sf : supportedFormats) {
                if (sf == format) {
                    formatSupported = true;
                    break;
                }
            }
            if (!formatSupported) {
                for (int64_t sf : supportedFormats) {
                    if (sf == DXGI_FORMAT_R8G8B8A8_UNORM || sf == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || sf == DXGI_FORMAT_B8G8R8A8_UNORM) {
                        selectedFormat = static_cast<DXGI_FORMAT>(sf);
                        break;
                    }
                }
            }
        }
    }

    XrSwapchainCreateInfo createInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    createInfo.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    createInfo.format = selectedFormat;
    createInfo.sampleCount = 1;
    createInfo.width = width;
    createInfo.height = height;
    createInfo.faceCount = 1;
    createInfo.arraySize = 1;
    createInfo.mipCount = 1;

    XrResult res = dispatch->CreateSwapchain(session, &createInfo, &m_swapchain.handle);
    if (XR_FAILED(res)) {
        m_lastXrResult = res;
        LogError("OpenXrCursorOverlay::CreateSwapchain: CreateSwapchain failed (result " + std::to_string(res) + ")");
        return false;
    }

    if (!EnumerateSwapchainImages(dispatch)) {
        dispatch->DestroySwapchain(m_swapchain.handle);
        m_swapchain.handle = XR_NULL_HANDLE;
        m_swapchain.images.clear();
        return false;
    }

    m_swapchain.width = width;
    m_swapchain.height = height;
    m_swapchain.format = selectedFormat;
    m_swapchain.lastCopiedIndex = 0;
    m_swapchain.hasCopiedFrame = false;
    m_hasSwapchain = true;
    m_textureUploaded = false;

    if (device && queue) {
        UploadTexture(device, queue, dispatch);
    }

    LogInfo("OpenXrCursorOverlay: Created cursor swapchain (" + std::to_string(width) + "x" + std::to_string(height) + ").");
    return true;
}

void OpenXrCursorOverlay::DestroySwapchain(std::shared_ptr<IOpenXrDispatch> dispatch) {
    if (m_swapchain.handle != XR_NULL_HANDLE && dispatch) {
        dispatch->DestroySwapchain(m_swapchain.handle);
        m_swapchain.handle = XR_NULL_HANDLE;
    }
    m_swapchain.images.clear();
    m_hasSwapchain = false;
    m_textureUploaded = false;
    m_lastXrResult = XR_SUCCESS;
    m_hasLastSubmittedCursorQuadLayer = false;
}

bool OpenXrCursorOverlay::EnumerateSwapchainImages(std::shared_ptr<IOpenXrDispatch> dispatch) {
    if (m_swapchain.handle == XR_NULL_HANDLE || !dispatch) return false;
    uint32_t imgCount = 0;
    XrResult res = dispatch->EnumerateSwapchainImages(m_swapchain.handle, 0, &imgCount, nullptr);
    if (XR_FAILED(res) || imgCount == 0) {
        if (XR_FAILED(res)) m_lastXrResult = res;
        return false;
    }
    m_swapchain.images.resize(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
    res = dispatch->EnumerateSwapchainImages(m_swapchain.handle, imgCount, &imgCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(m_swapchain.images.data()));
    if (XR_FAILED(res)) {
        m_lastXrResult = res;
        m_swapchain.images.clear();
        return false;
    }
    return true;
}

bool OpenXrCursorOverlay::UploadTexture(ID3D12Device* device, ID3D12CommandQueue* queue, std::shared_ptr<IOpenXrDispatch> dispatch) {
    m_lastXrResult = XR_SUCCESS;
    if (m_textureUploaded) {
        return true;
    }
    if (!m_hasSwapchain || m_swapchain.handle == XR_NULL_HANDLE || m_swapchain.images.empty()) {
        return false;
    }
    if (!device || !queue || !dispatch) {
        return false;
    }

    uint32_t width = m_swapchain.width;
    uint32_t height = m_swapchain.height;
    std::vector<uint32_t> pixels = GenerateCursorPixels(width, height);

    XrSwapchainImageAcquireInfo acqInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    uint32_t imgIdx = 0;
    XrResult res = dispatch->AcquireSwapchainImage(m_swapchain.handle, &acqInfo, &imgIdx);
    if (XR_FAILED(res)) {
        m_lastXrResult = res;
        return false;
    }

    XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitInfo.timeout = XR_INFINITE_DURATION;
    res = dispatch->WaitSwapchainImage(m_swapchain.handle, &waitInfo);
    if (XR_FAILED(res)) {
        m_lastXrResult = res;
        XrSwapchainImageReleaseInfo relInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        XrResult relRes = dispatch->ReleaseSwapchainImage(m_swapchain.handle, &relInfo);
        if (XR_FAILED(relRes) && res != XR_ERROR_SESSION_LOST && res != XR_ERROR_INSTANCE_LOST) {
            m_lastXrResult = relRes;
        }
        return false;
    }

    bool uploaded = false;
    if (imgIdx < m_swapchain.images.size()) {
        ID3D12Resource* destRes = m_swapchain.images[imgIdx].texture;
        if (destRes) {
            uploaded = UploadPixelsToResource(device, queue, destRes, width, height, pixels);
        }
    }

    XrSwapchainImageReleaseInfo relInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XrResult relRes = dispatch->ReleaseSwapchainImage(m_swapchain.handle, &relInfo);
    if (XR_FAILED(relRes)) {
        m_lastXrResult = relRes;
        return false;
    }
    if (!uploaded) {
        return false;
    }

    m_textureUploaded = true;
    return true;
}

bool OpenXrCursorOverlay::PrepareCompositionLayer(const XrCompositionLayerQuad& menuQuad,
                                                 const MenuCursorState& cursorState,
                                                 XrSpace localSpace,
                                                 XrCompositionLayerQuad& outCursorQuadLayer,
                                                 ID3D12Device* device,
                                                 ID3D12CommandQueue* queue,
                                                 std::shared_ptr<IOpenXrDispatch> dispatch) {
    bool showCursor = cursorState.visible && m_hasSwapchain && (m_swapchain.handle != XR_NULL_HANDLE);
    if (!showCursor) {
        m_hasLastSubmittedCursorQuadLayer = false;
        return false;
    }

    outCursorQuadLayer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    outCursorQuadLayer.next = nullptr;
    outCursorQuadLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    outCursorQuadLayer.space = localSpace;
    outCursorQuadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    outCursorQuadLayer.subImage.swapchain = m_swapchain.handle;
    outCursorQuadLayer.subImage.imageRect.offset = { 0, 0 };
    outCursorQuadLayer.subImage.imageRect.extent = {
        static_cast<int32_t>(m_swapchain.width),
        static_cast<int32_t>(m_swapchain.height)
    };
    outCursorQuadLayer.subImage.imageArrayIndex = 0;

    if (!m_textureUploaded && device && queue && dispatch) {
        UploadTexture(device, queue, dispatch);
    }

    float cSize = cursorState.size > 0.0f ? cursorState.size : MenuCursorState::kDefaultCursorSize;
    outCursorQuadLayer.pose = ComputeCursorQuadPose(menuQuad.pose, menuQuad.size, cursorState, cSize, cSize);
    outCursorQuadLayer.size = { cSize, cSize };

    m_lastSubmittedCursorQuadLayer = outCursorQuadLayer;
    m_hasLastSubmittedCursorQuadLayer = true;
    return true;
}

}
