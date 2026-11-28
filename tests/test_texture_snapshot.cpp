#include <gtest/gtest.h>
#include "core/texture_snapshot.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using Microsoft::WRL::ComPtr;

namespace {

uint32_t ReadLE32(const std::vector<uint8_t>& bytes, size_t at) {
    return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
}

// The B, G, R of the pixel at (x, y), counted from the top, of a 24-bit bottom-up BMP
std::array<uint8_t, 3> BmpPixel(const std::vector<uint8_t>& bmp, uint32_t x, uint32_t y) {
    const uint32_t width = ReadLE32(bmp, 18);
    const uint32_t height = ReadLE32(bmp, 22);
    const uint32_t rowBytes = (width * 3 + 3) & ~3u;
    const size_t at = ReadLE32(bmp, 10) + static_cast<size_t>(height - 1 - y) * rowBytes + static_cast<size_t>(x) * 3;
    return { bmp[at], bmp[at + 1], bmp[at + 2] };
}

}

TEST(TextureSnapshotTest, DecodesTheBackBufferFormatsTheGameUses) {
    uint8_t bgr[3];
    const uint8_t rgba[4] = { 10, 20, 30, 255 };
    ASSERT_TRUE(fs25vr::DecodePixelToBgr8(DXGI_FORMAT_R8G8B8A8_UNORM, rgba, bgr));
    EXPECT_EQ(bgr[0], 30); EXPECT_EQ(bgr[1], 20); EXPECT_EQ(bgr[2], 10);
    ASSERT_TRUE(fs25vr::DecodePixelToBgr8(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, rgba, bgr));
    EXPECT_EQ(bgr[0], 10); EXPECT_EQ(bgr[1], 20); EXPECT_EQ(bgr[2], 30);

    // 10-bit red 1023, green 512, blue 0: the top 8 bits of each
    const uint32_t packed = 1023u | (512u << 10) | (0u << 20) | (3u << 30);
    uint8_t bytes[4];
    std::memcpy(bytes, &packed, sizeof(bytes));
    ASSERT_TRUE(fs25vr::DecodePixelToBgr8(DXGI_FORMAT_R10G10B10A2_UNORM, bytes, bgr));
    EXPECT_EQ(bgr[2], 255); EXPECT_EQ(bgr[1], 128); EXPECT_EQ(bgr[0], 0);

    // Half floats: 1.0 is white, 0 black, above 1 clamps
    const uint16_t half[4] = { 0x3C00, 0x0000, 0x4400, 0x3C00 };  // 1.0, 0.0, 4.0, 1.0
    uint8_t halfBytes[8];
    std::memcpy(halfBytes, half, sizeof(halfBytes));
    ASSERT_TRUE(fs25vr::DecodePixelToBgr8(DXGI_FORMAT_R16G16B16A16_FLOAT, halfBytes, bgr));
    EXPECT_EQ(bgr[2], 255); EXPECT_EQ(bgr[1], 0); EXPECT_EQ(bgr[0], 255);

    EXPECT_FALSE(fs25vr::DecodePixelToBgr8(DXGI_FORMAT_BC1_UNORM, rgba, bgr));
}

TEST(TextureSnapshotTest, EncodesABottomUpBmpWithRowsPaddedToFourBytes) {
    // 3 x 2: top row red, green, blue; bottom row white, black, grey
    const std::vector<uint8_t> bgr = {
        0, 0, 255,   0, 255, 0,   255, 0, 0,
        255, 255, 255,   0, 0, 0,   128, 128, 128,
    };
    const std::vector<uint8_t> bmp = fs25vr::EncodeBmp24(3, 2, bgr);
    ASSERT_EQ(bmp.size(), 54u + 12u * 2u) << "each 9-byte row is padded to 12";
    EXPECT_EQ(bmp[0], 'B');
    EXPECT_EQ(bmp[1], 'M');
    EXPECT_EQ(ReadLE32(bmp, 2), bmp.size());
    EXPECT_EQ(ReadLE32(bmp, 18), 3u);
    EXPECT_EQ(ReadLE32(bmp, 22), 2u);
    EXPECT_EQ((std::array<uint8_t, 3>{ 0, 0, 255 }), BmpPixel(bmp, 0, 0)) << "the top-left pixel is the first one given";
    EXPECT_EQ((std::array<uint8_t, 3>{ 255, 0, 0 }), BmpPixel(bmp, 2, 0));
    EXPECT_EQ((std::array<uint8_t, 3>{ 128, 128, 128 }), BmpPixel(bmp, 2, 1));
}

TEST(TextureSnapshotTest, ReadsBackATextureInThePresentStateAndWritesEveryStepthPixel) {
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
        FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        GTEST_SKIP() << "no WARP D3D12 device";
    }
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));

    // An 8 x 4 back buffer: the left half red, the right half blue, the bottom-right pixel green
    constexpr UINT kWidth = 8;
    constexpr UINT kHeight = 4;
    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> texture;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                          D3D12_RESOURCE_STATE_PRESENT, nullptr, IID_PPV_ARGS(&texture))));

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 uploadBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadBytes);
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upload;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))));
    uint8_t* mapped = nullptr;
    ASSERT_TRUE(SUCCEEDED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped))));
    for (UINT y = 0; y < kHeight; ++y) {
        for (UINT x = 0; x < kWidth; ++x) {
            uint8_t* pixel = mapped + footprint.Offset + y * footprint.Footprint.RowPitch + x * 4;
            const bool green = (x == kWidth - 1 && y == kHeight - 1);
            pixel[0] = (!green && x < kWidth / 2) ? 255 : 0;
            pixel[1] = green ? 255 : 0;
            pixel[2] = (!green && x >= kWidth / 2) ? 255 : 0;
            pixel[3] = 255;
        }
    }
    upload->Unmap(0, nullptr);

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = texture.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = upload.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    ASSERT_TRUE(SUCCEEDED(list->Close()));
    ID3D12CommandList* lists[] = { list.Get() };
    queue->ExecuteCommandLists(1, lists);

    const std::filesystem::path file = std::filesystem::temp_directory_path() / "fs25vr_texture_snapshot_test.bmp";
    auto readFile = [&]() {
        std::ifstream in(file, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };

    ASSERT_EQ(fs25vr::SnapshotTextureToBmp(device.Get(), queue.Get(), texture.Get(), D3D12_RESOURCE_STATE_PRESENT, file, 1), "");
    std::vector<uint8_t> bmp = readFile();
    ASSERT_GE(bmp.size(), 54u);
    EXPECT_EQ(ReadLE32(bmp, 18), kWidth);
    EXPECT_EQ(ReadLE32(bmp, 22), kHeight);
    EXPECT_EQ((std::array<uint8_t, 3>{ 0, 0, 255 }), BmpPixel(bmp, 0, 0)) << "top left is red";
    EXPECT_EQ((std::array<uint8_t, 3>{ 255, 0, 0 }), BmpPixel(bmp, kWidth - 1, 0)) << "top right is blue";
    EXPECT_EQ((std::array<uint8_t, 3>{ 0, 255, 0 }), BmpPixel(bmp, kWidth - 1, kHeight - 1)) << "bottom right is green";

    // Every 2nd pixel of every 2nd row: 4 x 2, starting at the top left
    ASSERT_EQ(fs25vr::SnapshotTextureToBmp(device.Get(), queue.Get(), texture.Get(), D3D12_RESOURCE_STATE_PRESENT, file, 2), "");
    bmp = readFile();
    EXPECT_EQ(ReadLE32(bmp, 18), kWidth / 2);
    EXPECT_EQ(ReadLE32(bmp, 22), kHeight / 2);
    EXPECT_EQ((std::array<uint8_t, 3>{ 0, 0, 255 }), BmpPixel(bmp, 1, 1));
    EXPECT_EQ((std::array<uint8_t, 3>{ 255, 0, 0 }), BmpPixel(bmp, 2, 0));

    EXPECT_TRUE(SUCCEEDED(device->GetDeviceRemovedReason()));
    std::filesystem::remove(file);

    EXPECT_NE(fs25vr::SnapshotTextureToBmp(nullptr, queue.Get(), texture.Get(), D3D12_RESOURCE_STATE_PRESENT, file, 1), "");
}

namespace {

constexpr GUID kSnapshotDestroyTrackerGuid = { 0x8d5f41b2, 0x3b6c, 0x4c1d, { 0x9e, 0x20, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 } };
constexpr GUID kTrackingCommandListGuid = { 0x8d5f41b2, 0x3b6c, 0x4c1d, { 0x9e, 0x20, 0x11, 0x22, 0x33, 0x44, 0x55, 0x67 } };
constexpr GUID kSnapshotMockFenceGuid = { 0x8d5f41b2, 0x3b6c, 0x4c1d, { 0x9e, 0x20, 0x11, 0x22, 0x33, 0x44, 0x55, 0x68 } };

class SnapshotDestroyTracker final : public IUnknown {
public:
    explicit SnapshotDestroyTracker(std::atomic<int>* destroyCount) : m_destroyCount(destroyCount) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == __uuidof(IUnknown)) {
            *ppvObject = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --m_refCount;
        if (remaining == 0) {
            if (m_destroyCount) ++(*m_destroyCount);
            delete this;
        }
        return remaining;
    }
private:
    std::atomic<ULONG> m_refCount{ 1 };
    std::atomic<int>* m_destroyCount = nullptr;
};

void AttachDestroyTracker(ID3D12Object* object, std::atomic<int>* destroyCount) {
    if (!object) return;
    ComPtr<SnapshotDestroyTracker> tracker;
    tracker.Attach(new SnapshotDestroyTracker(destroyCount));
    object->SetPrivateDataInterface(kSnapshotDestroyTrackerGuid, tracker.Get());
}

class TrackingCommandList final : public ID3D12GraphicsCommandList {
public:
    TrackingCommandList(ComPtr<ID3D12GraphicsCommandList> inner, std::vector<D3D12_RESOURCE_BARRIER>* barriers)
        : m_inner(std::move(inner)), m_barriers(barriers) {}

    ID3D12GraphicsCommandList* Inner() const { return m_inner.Get(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == kTrackingCommandListGuid) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
            riid == __uuidof(ID3D12CommandList) || riid == __uuidof(ID3D12GraphicsCommandList)) {
            *ppvObject = static_cast<ID3D12GraphicsCommandList*>(this);
            AddRef();
            return S_OK;
        }
        return m_inner->QueryInterface(riid, ppvObject);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --m_refCount;
        if (remaining == 0) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override {
        return m_inner->GetPrivateData(guid, pDataSize, pData);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override {
        return m_inner->SetPrivateData(guid, DataSize, pData);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override {
        return m_inner->SetPrivateDataInterface(guid, pData);
    }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override { return m_inner->SetName(Name); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppvDevice) override { return m_inner->GetDevice(riid, ppvDevice); }
    D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType() override { return m_inner->GetType(); }
    HRESULT STDMETHODCALLTYPE Close() override { return m_inner->Close(); }
    HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator* pAllocator, ID3D12PipelineState* pInitialState) override {
        return m_inner->Reset(pAllocator, pInitialState);
    }
    void STDMETHODCALLTYPE ClearState(ID3D12PipelineState* pPipelineState) override { m_inner->ClearState(pPipelineState); }
    void STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation,
                                         UINT StartInstanceLocation) override {
        m_inner->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
    }
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation,
                                                INT BaseVertexLocation, UINT StartInstanceLocation) override {
        m_inner->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
    }
    void STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override {
        m_inner->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
    }
    void STDMETHODCALLTYPE CopyBufferRegion(ID3D12Resource* pDstBuffer, UINT64 DstOffset, ID3D12Resource* pSrcBuffer,
                                            UINT64 SrcOffset, UINT64 NumBytes) override {
        m_inner->CopyBufferRegion(pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, NumBytes);
    }
    void STDMETHODCALLTYPE CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* pDst, UINT DstX, UINT DstY, UINT DstZ,
                                             const D3D12_TEXTURE_COPY_LOCATION* pSrc, const D3D12_BOX* pSrcBox) override {
        m_inner->CopyTextureRegion(pDst, DstX, DstY, DstZ, pSrc, pSrcBox);
    }
    void STDMETHODCALLTYPE CopyResource(ID3D12Resource* pDstResource, ID3D12Resource* pSrcResource) override {
        m_inner->CopyResource(pDstResource, pSrcResource);
    }
    void STDMETHODCALLTYPE CopyTiles(ID3D12Resource* pTiledResource, const D3D12_TILED_RESOURCE_COORDINATE* pTileRegionStartCoordinate,
                                     const D3D12_TILE_REGION_SIZE* pTileRegionSize, ID3D12Resource* pBuffer, UINT64 BufferStartOffsetInBytes,
                                     D3D12_TILE_COPY_FLAGS Flags) override {
        m_inner->CopyTiles(pTiledResource, pTileRegionStartCoordinate, pTileRegionSize, pBuffer, BufferStartOffsetInBytes, Flags);
    }
    void STDMETHODCALLTYPE ResolveSubresource(ID3D12Resource* pDstResource, UINT DstSubresource, ID3D12Resource* pSrcResource,
                                              UINT SrcSubresource, DXGI_FORMAT Format) override {
        m_inner->ResolveSubresource(pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format);
    }
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology) override {
        m_inner->IASetPrimitiveTopology(PrimitiveTopology);
    }
    void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D12_VIEWPORT* pViewports) override {
        m_inner->RSSetViewports(NumViewports, pViewports);
    }
    void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D12_RECT* pRects) override {
        m_inner->RSSetScissorRects(NumRects, pRects);
    }
    void STDMETHODCALLTYPE OMSetBlendFactor(const FLOAT BlendFactor[4]) override { m_inner->OMSetBlendFactor(BlendFactor); }
    void STDMETHODCALLTYPE OMSetStencilRef(UINT StencilRef) override { m_inner->OMSetStencilRef(StencilRef); }
    void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState* pPipelineState) override { m_inner->SetPipelineState(pPipelineState); }
    void STDMETHODCALLTYPE ResourceBarrier(UINT NumBarriers, const D3D12_RESOURCE_BARRIER* pBarriers) override {
        if (m_barriers && pBarriers) {
            for (UINT i = 0; i < NumBarriers; ++i) m_barriers->push_back(pBarriers[i]);
        }
        m_inner->ResourceBarrier(NumBarriers, pBarriers);
    }
    void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList* pCommandList) override { m_inner->ExecuteBundle(pCommandList); }
    void STDMETHODCALLTYPE SetDescriptorHeaps(UINT NumDescriptorHeaps, ID3D12DescriptorHeap* const* ppDescriptorHeaps) override {
        m_inner->SetDescriptorHeaps(NumDescriptorHeaps, ppDescriptorHeaps);
    }
    void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature* pRootSignature) override {
        m_inner->SetComputeRootSignature(pRootSignature);
    }
    void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature* pRootSignature) override {
        m_inner->SetGraphicsRootSignature(pRootSignature);
    }
    void STDMETHODCALLTYPE SetComputeRootDescriptorTable(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override {
        m_inner->SetComputeRootDescriptorTable(RootParameterIndex, BaseDescriptor);
    }
    void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override {
        m_inner->SetGraphicsRootDescriptorTable(RootParameterIndex, BaseDescriptor);
    }
    void STDMETHODCALLTYPE SetComputeRoot32BitConstant(UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) override {
        m_inner->SetComputeRoot32BitConstant(RootParameterIndex, SrcData, DestOffsetIn32BitValues);
    }
    void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) override {
        m_inner->SetGraphicsRoot32BitConstant(RootParameterIndex, SrcData, DestOffsetIn32BitValues);
    }
    void STDMETHODCALLTYPE SetComputeRoot32BitConstants(UINT RootParameterIndex, UINT Num32BitValuesToSet, const void* pSrcData,
                                                        UINT DestOffsetIn32BitValues) override {
        m_inner->SetComputeRoot32BitConstants(RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
    }
    void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(UINT RootParameterIndex, UINT Num32BitValuesToSet, const void* pSrcData,
                                                         UINT DestOffsetIn32BitValues) override {
        m_inner->SetGraphicsRoot32BitConstants(RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
    }
    void STDMETHODCALLTYPE SetComputeRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {
        m_inner->SetComputeRootConstantBufferView(RootParameterIndex, BufferLocation);
    }
    void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {
        m_inner->SetGraphicsRootConstantBufferView(RootParameterIndex, BufferLocation);
    }
    void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {
        m_inner->SetComputeRootShaderResourceView(RootParameterIndex, BufferLocation);
    }
    void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {
        m_inner->SetGraphicsRootShaderResourceView(RootParameterIndex, BufferLocation);
    }
    void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {
        m_inner->SetComputeRootUnorderedAccessView(RootParameterIndex, BufferLocation);
    }
    void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {
        m_inner->SetGraphicsRootUnorderedAccessView(RootParameterIndex, BufferLocation);
    }
    void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* pView) override { m_inner->IASetIndexBuffer(pView); }
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW* pViews) override {
        m_inner->IASetVertexBuffers(StartSlot, NumViews, pViews);
    }
    void STDMETHODCALLTYPE SOSetTargets(UINT StartSlot, UINT NumViews, const D3D12_STREAM_OUTPUT_BUFFER_VIEW* pViews) override {
        m_inner->SOSetTargets(StartSlot, NumViews, pViews);
    }
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
                                              BOOL RTsSingleHandleToDescriptorRange,
                                              const D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor) override {
        m_inner->OMSetRenderTargets(NumRenderTargetDescriptors, pRenderTargetDescriptors, RTsSingleHandleToDescriptorRange,
                                    pDepthStencilDescriptor);
    }
    void STDMETHODCALLTYPE ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView, D3D12_CLEAR_FLAGS ClearFlags, FLOAT Depth,
                                                 UINT8 Stencil, UINT NumRects, const D3D12_RECT* pRects) override {
        m_inner->ClearDepthStencilView(DepthStencilView, ClearFlags, Depth, Stencil, NumRects, pRects);
    }
    void STDMETHODCALLTYPE ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView, const FLOAT ColorRGBA[4], UINT NumRects,
                                                 const D3D12_RECT* pRects) override {
        m_inner->ClearRenderTargetView(RenderTargetView, ColorRGBA, NumRects, pRects);
    }
    void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
                                                       D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource* pResource,
                                                       const UINT Values[4], UINT NumRects, const D3D12_RECT* pRects) override {
        m_inner->ClearUnorderedAccessViewUint(ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, Values, NumRects, pRects);
    }
    void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
                                                        D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource* pResource,
                                                        const FLOAT Values[4], UINT NumRects, const D3D12_RECT* pRects) override {
        m_inner->ClearUnorderedAccessViewFloat(ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, Values, NumRects, pRects);
    }
    void STDMETHODCALLTYPE DiscardResource(ID3D12Resource* pResource, const D3D12_DISCARD_REGION* pRegion) override {
        m_inner->DiscardResource(pResource, pRegion);
    }
    void STDMETHODCALLTYPE BeginQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) override {
        m_inner->BeginQuery(pQueryHeap, Type, Index);
    }
    void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) override {
        m_inner->EndQuery(pQueryHeap, Type, Index);
    }
    void STDMETHODCALLTYPE ResolveQueryData(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT StartIndex, UINT NumQueries,
                                            ID3D12Resource* pDestinationBuffer, UINT64 AlignedDestinationBufferOffset) override {
        m_inner->ResolveQueryData(pQueryHeap, Type, StartIndex, NumQueries, pDestinationBuffer, AlignedDestinationBufferOffset);
    }
    void STDMETHODCALLTYPE SetPredication(ID3D12Resource* pBuffer, UINT64 AlignedBufferOffset, D3D12_PREDICATION_OP Operation) override {
        m_inner->SetPredication(pBuffer, AlignedBufferOffset, Operation);
    }
    void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void* pData, UINT Size) override {
        m_inner->SetMarker(Metadata, pData, Size);
    }
    void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void* pData, UINT Size) override {
        m_inner->BeginEvent(Metadata, pData, Size);
    }
    void STDMETHODCALLTYPE EndEvent() override { m_inner->EndEvent(); }
    void STDMETHODCALLTYPE ExecuteIndirect(ID3D12CommandSignature* pCommandSignature, UINT MaxCommandCount, ID3D12Resource* pArgumentBuffer,
                                           UINT64 ArgumentBufferOffset, ID3D12Resource* pCountBuffer, UINT64 CountBufferOffset) override {
        m_inner->ExecuteIndirect(pCommandSignature, MaxCommandCount, pArgumentBuffer, ArgumentBufferOffset, pCountBuffer, CountBufferOffset);
    }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    ComPtr<ID3D12GraphicsCommandList> m_inner;
    std::vector<D3D12_RESOURCE_BARRIER>* m_barriers = nullptr;
};

class SnapshotMockFence final : public ID3D12Fence {
public:
    SnapshotMockFence(ComPtr<ID3D12Fence> inner, std::atomic<int>* destroyCount, HANDLE* capturedEvent, bool hangWait)
        : m_inner(std::move(inner)), m_destroyCount(destroyCount), m_capturedEvent(capturedEvent), m_hangWait(hangWait) {}
    ~SnapshotMockFence() {
        if (m_destroyCount) ++(*m_destroyCount);
    }

    ID3D12Fence* Inner() const { return m_inner.Get(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == kSnapshotMockFenceGuid) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
            riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12Fence)) {
            *ppvObject = static_cast<ID3D12Fence*>(this);
            AddRef();
            return S_OK;
        }
        return m_inner->QueryInterface(riid, ppvObject);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --m_refCount;
        if (remaining == 0) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override {
        return m_inner->GetPrivateData(guid, pDataSize, pData);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override {
        return m_inner->SetPrivateData(guid, DataSize, pData);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override {
        return m_inner->SetPrivateDataInterface(guid, pData);
    }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override { return m_inner->SetName(Name); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppvDevice) override { return m_inner->GetDevice(riid, ppvDevice); }
    UINT64 STDMETHODCALLTYPE GetCompletedValue() override { return m_hangWait ? 0 : m_inner->GetCompletedValue(); }
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 Value, HANDLE hEvent) override {
        if (m_capturedEvent) *m_capturedEvent = hEvent;
        if (m_hangWait) return S_OK;
        return m_inner->SetEventOnCompletion(Value, hEvent);
    }
    HRESULT STDMETHODCALLTYPE Signal(UINT64 Value) override { return m_inner->Signal(Value); }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    ComPtr<ID3D12Fence> m_inner;
    std::atomic<int>* m_destroyCount = nullptr;
    HANDLE* m_capturedEvent = nullptr;
    bool m_hangWait = false;
};

class SnapshotMockQueue final : public ID3D12CommandQueue {
public:
    SnapshotMockQueue(ComPtr<ID3D12CommandQueue> inner, HRESULT signalResult)
        : m_inner(std::move(inner)), m_signalResult(signalResult) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
            riid == __uuidof(ID3D12CommandQueue)) {
            *ppvObject = static_cast<ID3D12CommandQueue*>(this);
            AddRef();
            return S_OK;
        }
        return m_inner->QueryInterface(riid, ppvObject);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --m_refCount;
        if (remaining == 0) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override {
        return m_inner->GetPrivateData(guid, pDataSize, pData);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override {
        return m_inner->SetPrivateData(guid, DataSize, pData);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override {
        return m_inner->SetPrivateDataInterface(guid, pData);
    }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override { return m_inner->SetName(Name); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppvDevice) override { return m_inner->GetDevice(riid, ppvDevice); }
    void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource* pResource, UINT NumResourceRegions,
                                              const D3D12_TILED_RESOURCE_COORDINATE* pResourceRegionStartCoordinates,
                                              const D3D12_TILE_REGION_SIZE* pResourceRegionSizes, ID3D12Heap* pHeap,
                                              UINT NumRanges, const D3D12_TILE_RANGE_FLAGS* pRangeFlags, const UINT* pHeapRangeStartOffsets,
                                              const UINT* pRangeTileCounts, D3D12_TILE_MAPPING_FLAGS Flags) override {
        m_inner->UpdateTileMappings(pResource, NumResourceRegions, pResourceRegionStartCoordinates, pResourceRegionSizes, pHeap, NumRanges,
                                    pRangeFlags, pHeapRangeStartOffsets, pRangeTileCounts, Flags);
    }
    void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource* pDstResource, const D3D12_TILED_RESOURCE_COORDINATE* pDstRegionStartCoordinate,
                                            ID3D12Resource* pSrcResource, const D3D12_TILED_RESOURCE_COORDINATE* pSrcRegionStartCoordinate,
                                            const D3D12_TILE_REGION_SIZE* pRegionSize, D3D12_TILE_MAPPING_FLAGS Flags) override {
        m_inner->CopyTileMappings(pDstResource, pDstRegionStartCoordinate, pSrcResource, pSrcRegionStartCoordinate, pRegionSize, Flags);
    }
    void STDMETHODCALLTYPE ExecuteCommandLists(UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) override {
        std::vector<ID3D12CommandList*> unwrapped;
        std::vector<ComPtr<TrackingCommandList>> refs;
        unwrapped.reserve(NumCommandLists);
        for (UINT i = 0; i < NumCommandLists; ++i) {
            void* raw = nullptr;
            if (ppCommandLists[i] && SUCCEEDED(ppCommandLists[i]->QueryInterface(kTrackingCommandListGuid, &raw)) && raw) {
                ComPtr<TrackingCommandList> tracked;
                tracked.Attach(static_cast<TrackingCommandList*>(raw));
                unwrapped.push_back(tracked->Inner());
                refs.push_back(std::move(tracked));
            } else {
                unwrapped.push_back(ppCommandLists[i]);
            }
        }
        m_inner->ExecuteCommandLists(NumCommandLists, unwrapped.data());
    }
    void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void* pData, UINT Size) override {
        m_inner->SetMarker(Metadata, pData, Size);
    }
    void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void* pData, UINT Size) override {
        m_inner->BeginEvent(Metadata, pData, Size);
    }
    void STDMETHODCALLTYPE EndEvent() override { m_inner->EndEvent(); }
    HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* pFence, UINT64 Value) override {
        if (FAILED(m_signalResult)) return m_signalResult;
        void* raw = nullptr;
        if (pFence && SUCCEEDED(pFence->QueryInterface(kSnapshotMockFenceGuid, &raw)) && raw) {
            ComPtr<SnapshotMockFence> wrapped;
            wrapped.Attach(static_cast<SnapshotMockFence*>(raw));
            return m_inner->Signal(wrapped->Inner(), Value);
        }
        return m_inner->Signal(pFence, Value);
    }
    HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* pFence, UINT64 Value) override { return m_inner->Wait(pFence, Value); }
    HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* pFrequency) override { return m_inner->GetTimestampFrequency(pFrequency); }
    HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* pGpuTimestamp, UINT64* pCpuTimestamp) override {
        return m_inner->GetClockCalibration(pGpuTimestamp, pCpuTimestamp);
    }
    D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc() override { return m_inner->GetDesc(); }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    ComPtr<ID3D12CommandQueue> m_inner;
    HRESULT m_signalResult = S_OK;
};

class SnapshotMockDevice final : public ID3D12Device {
public:
    SnapshotMockDevice(ComPtr<ID3D12Device> inner, std::vector<D3D12_RESOURCE_BARRIER>* barriers,
                       std::atomic<int>* destroyCount, HANDLE* capturedEvent, bool hangWait)
        : m_inner(std::move(inner)), m_barriers(barriers), m_destroyCount(destroyCount),
          m_capturedEvent(capturedEvent), m_hangWait(hangWait) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12Device)) {
            *ppvObject = static_cast<ID3D12Device*>(this);
            AddRef();
            return S_OK;
        }
        return m_inner->QueryInterface(riid, ppvObject);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --m_refCount;
        if (remaining == 0) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override {
        return m_inner->GetPrivateData(guid, pDataSize, pData);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override {
        return m_inner->SetPrivateData(guid, DataSize, pData);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override {
        return m_inner->SetPrivateDataInterface(guid, pData);
    }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override { return m_inner->SetName(Name); }
    UINT STDMETHODCALLTYPE GetNodeCount() override { return m_inner->GetNodeCount(); }
    HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue) override {
        return m_inner->CreateCommandQueue(pDesc, riid, ppCommandQueue);
    }
    HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID riid, void** ppCommandAllocator) override {
        const HRESULT hr = m_inner->CreateCommandAllocator(type, riid, ppCommandAllocator);
        if (SUCCEEDED(hr) && ppCommandAllocator && *ppCommandAllocator && m_destroyCount) {
            AttachDestroyTracker(static_cast<ID3D12Object*>(*ppCommandAllocator), m_destroyCount);
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid,
                                                          void** ppPipelineState) override {
        return m_inner->CreateGraphicsPipelineState(pDesc, riid, ppPipelineState);
    }
    HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc, REFIID riid,
                                                         void** ppPipelineState) override {
        return m_inner->CreateComputePipelineState(pDesc, riid, ppPipelineState);
    }
    HRESULT STDMETHODCALLTYPE CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* pCommandAllocator,
                                                ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) override {
        ComPtr<ID3D12GraphicsCommandList> innerList;
        const HRESULT hr = m_inner->CreateCommandList(nodeMask, type, pCommandAllocator, pInitialState, IID_PPV_ARGS(&innerList));
        if (FAILED(hr)) return hr;
        if (m_destroyCount) AttachDestroyTracker(innerList.Get(), m_destroyCount);
        if (m_barriers) {
            ComPtr<TrackingCommandList> wrapped;
            wrapped.Attach(new TrackingCommandList(innerList, m_barriers));
            return wrapped->QueryInterface(riid, ppCommandList);
        }
        return innerList->QueryInterface(riid, ppCommandList);
    }
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE Feature, void* pFeatureSupportData, UINT FeatureSupportDataSize) override {
        return m_inner->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
    }
    HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc, REFIID riid,
                                                   void** ppvHeap) override {
        return m_inner->CreateDescriptorHeap(pDescriptorHeapDesc, riid, ppvHeap);
    }
    UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) override {
        return m_inner->GetDescriptorHandleIncrementSize(DescriptorHeapType);
    }
    HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT nodeMask, const void* pBlobWithRootSignature, SIZE_T blobLengthInBytes,
                                                  REFIID riid, void** ppvRootSignature) override {
        return m_inner->CreateRootSignature(nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature);
    }
    void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc,
                                                    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateConstantBufferView(pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
                                                    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateShaderResourceView(pResource, pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource* pCounterResource,
                                                     const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
                                                     D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateUnorderedAccessView(pResource, pCounterResource, pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
                                                  D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateRenderTargetView(pResource, pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource* pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc,
                                                  D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateDepthStencilView(pResource, pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateSampler(pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CopyDescriptors(UINT NumDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
                                           const UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptors,
                                           const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
                                           const UINT* pSrcDescriptorRangeSizes,
                                           D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) override {
        m_inner->CopyDescriptors(NumDescriptors, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes, NumSrcDescriptors,
                                 pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, DescriptorHeapType);
    }
    void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
                                                 D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
                                                 D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) override {
        m_inner->CopyDescriptorsSimple(NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart, DescriptorHeapType);
    }
    D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(UINT visibleMask, UINT numResourceDescs,
                                                                               const D3D12_RESOURCE_DESC* pResourceDescs) override {
        return m_inner->GetResourceAllocationInfo(visibleMask, numResourceDescs, pResourceDescs);
    }
    D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(UINT nodeMask, D3D12_HEAP_TYPE heapType) override {
        return m_inner->GetCustomHeapProperties(nodeMask, heapType);
    }
    HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags,
                                                      const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState,
                                                      const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource,
                                                      void** ppvResource) override {
        const HRESULT hr = m_inner->CreateCommittedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue,
                                                            riidResource, ppvResource);
        if (SUCCEEDED(hr) && ppvResource && *ppvResource && m_destroyCount) {
            AttachDestroyTracker(static_cast<ID3D12Object*>(*ppvResource), m_destroyCount);
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) override {
        return m_inner->CreateHeap(pDesc, riid, ppvHeap);
    }
    HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC* pDesc,
                                                   D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue,
                                                   REFIID riid, void** ppvResource) override {
        return m_inner->CreatePlacedResource(pHeap, HeapOffset, pDesc, InitialState, pOptimizedClearValue, riid, ppvResource);
    }
    HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState,
                                                     const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid,
                                                     void** ppvResource) override {
        return m_inner->CreateReservedResource(pDesc, InitialState, pOptimizedClearValue, riid, ppvResource);
    }
    HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild* pObject, const SECURITY_ATTRIBUTES* pAttributes, DWORD Access,
                                                 LPCWSTR Name, HANDLE* pHandle) override {
        return m_inner->CreateSharedHandle(pObject, pAttributes, Access, Name, pHandle);
    }
    HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE NTHandle, REFIID riid, void** ppvObj) override {
        return m_inner->OpenSharedHandle(NTHandle, riid, ppvObj);
    }
    HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR Name, DWORD Access, HANDLE* pNTHandle) override {
        return m_inner->OpenSharedHandleByName(Name, Access, pNTHandle);
    }
    HRESULT STDMETHODCALLTYPE MakeResident(UINT NumObjects, ID3D12Pageable* const* ppObjects) override {
        return m_inner->MakeResident(NumObjects, ppObjects);
    }
    HRESULT STDMETHODCALLTYPE Evict(UINT NumObjects, ID3D12Pageable* const* ppObjects) override {
        return m_inner->Evict(NumObjects, ppObjects);
    }
    HRESULT STDMETHODCALLTYPE CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void** ppFence) override {
        ComPtr<ID3D12Fence> innerFence;
        const HRESULT hr = m_inner->CreateFence(InitialValue, Flags, IID_PPV_ARGS(&innerFence));
        if (FAILED(hr)) return hr;
        if (m_destroyCount || m_capturedEvent || m_hangWait) {
            ComPtr<SnapshotMockFence> wrapped;
            wrapped.Attach(new SnapshotMockFence(innerFence, m_destroyCount, m_capturedEvent, m_hangWait));
            return wrapped->QueryInterface(riid, ppFence);
        }
        return innerFence->QueryInterface(riid, ppFence);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override { return m_inner->GetDeviceRemovedReason(); }
    void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC* pResourceDesc, UINT FirstSubresource, UINT NumSubresources,
                                                 UINT64 BaseOffset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts, UINT* pNumRows,
                                                 UINT64* pRowSizeInBytes, UINT64* pTotalBytes) override {
        m_inner->GetCopyableFootprints(pResourceDesc, FirstSubresource, NumSubresources, BaseOffset, pLayouts, pNumRows, pRowSizeInBytes,
                                       pTotalBytes);
    }
    HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) override {
        return m_inner->CreateQueryHeap(pDesc, riid, ppvHeap);
    }
    HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL Enable) override { return m_inner->SetStablePowerState(Enable); }
    HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* pDesc, ID3D12RootSignature* pRootSignature,
                                                     REFIID riid, void** ppvCommandSignature) override {
        return m_inner->CreateCommandSignature(pDesc, pRootSignature, riid, ppvCommandSignature);
    }
    void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource* pTiledResource, UINT* pNumTilesForEntireResource,
                                             D3D12_PACKED_MIP_INFO* pPackedMipDesc, D3D12_TILE_SHAPE* pStandardTileShapeForNonTiledResource,
                                             UINT* pNumSubresourceTilings, UINT FirstSubresourceTilingToGet,
                                             D3D12_SUBRESOURCE_TILING* pSubresourceTilingsForNonTiledResource) override {
        m_inner->GetResourceTiling(pTiledResource, pNumTilesForEntireResource, pPackedMipDesc, pStandardTileShapeForNonTiledResource,
                                   pNumSubresourceTilings, FirstSubresourceTilingToGet, pSubresourceTilingsForNonTiledResource);
    }
    LUID STDMETHODCALLTYPE GetAdapterLuid() override { return m_inner->GetAdapterLuid(); }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    ComPtr<ID3D12Device> m_inner;
    std::vector<D3D12_RESOURCE_BARRIER>* m_barriers = nullptr;
    std::atomic<int>* m_destroyCount = nullptr;
    HANDLE* m_capturedEvent = nullptr;
    bool m_hangWait = false;
};

}

TEST(TextureSnapshotTest, SkipsRedundantBarriersWhenTextureIsAlreadyInCopySourceState) {
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
        FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        GTEST_SKIP() << "no WARP D3D12 device";
    }
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 4;
    desc.Height = 4;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> texture;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                          D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&texture))));

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    ComPtr<SnapshotMockDevice> mockDevice;
    mockDevice.Attach(new SnapshotMockDevice(device, &barriers, nullptr, nullptr, false));
    ComPtr<SnapshotMockQueue> mockQueue;
    mockQueue.Attach(new SnapshotMockQueue(queue, S_OK));

    const std::filesystem::path file = std::filesystem::temp_directory_path() / "fs25vr_texture_snapshot_copy_source.bmp";
    EXPECT_EQ(fs25vr::SnapshotTextureToBmp(mockDevice.Get(), mockQueue.Get(), texture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, file, 1), "");
    EXPECT_TRUE(barriers.empty());
    std::filesystem::remove(file);
}

TEST(TextureSnapshotTest, LeavesInFlightGpuObjectsAndEventAliveOnPostSubmitWaitFailure) {
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
        FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        GTEST_SKIP() << "no WARP D3D12 device";
    }
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 4;
    desc.Height = 4;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> texture;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                          D3D12_RESOURCE_STATE_PRESENT, nullptr, IID_PPV_ARGS(&texture))));

    const std::filesystem::path file = std::filesystem::temp_directory_path() / "fs25vr_texture_snapshot_wait_fail.bmp";

    {
        std::atomic<int> destroyCount{ 0 };
        ComPtr<SnapshotMockDevice> mockDevice;
        mockDevice.Attach(new SnapshotMockDevice(device, nullptr, &destroyCount, nullptr, false));
        ComPtr<SnapshotMockQueue> mockQueue;
        mockQueue.Attach(new SnapshotMockQueue(queue, E_FAIL));
        EXPECT_NE(fs25vr::SnapshotTextureToBmp(mockDevice.Get(), mockQueue.Get(), texture.Get(),
                                               D3D12_RESOURCE_STATE_PRESENT, file, 1),
                  "");
        EXPECT_EQ(destroyCount.load(), 0);
    }

    {
        std::atomic<int> destroyCount{ 0 };
        HANDLE capturedEvent = nullptr;
        ComPtr<SnapshotMockDevice> mockDevice;
        mockDevice.Attach(new SnapshotMockDevice(device, nullptr, &destroyCount, &capturedEvent, true));
        ComPtr<SnapshotMockQueue> mockQueue;
        mockQueue.Attach(new SnapshotMockQueue(queue, S_OK));
        EXPECT_NE(fs25vr::SnapshotTextureToBmp(mockDevice.Get(), mockQueue.Get(), texture.Get(),
                                               D3D12_RESOURCE_STATE_PRESENT, file, 1),
                  "");
        ASSERT_EQ(destroyCount.load(), 0);
        ASSERT_NE(capturedEvent, nullptr);
        DWORD handleFlags = 0;
        EXPECT_TRUE(GetHandleInformation(capturedEvent, &handleFlags));
        CloseHandle(capturedEvent);
    }
    std::filesystem::remove(file);
}
