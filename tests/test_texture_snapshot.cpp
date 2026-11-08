// Eye snapshots (`vrEyeSnapshot`): the texture an eye is handed, read back to the CPU and written as a BMP that can be looked
// at. The pixel decoding and the BMP layout are checked on their own; the readback runs on a WARP device against a texture in
// the PRESENT state, as the back buffer is at Present.
#include <gtest/gtest.h>
#include "core/texture_snapshot.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
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

} // namespace

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

    // The texture is back in PRESENT: a second snapshot from that state works again (above), and the device is fine
    EXPECT_TRUE(SUCCEEDED(device->GetDeviceRemovedReason()));
    std::filesystem::remove(file);

    EXPECT_NE(fs25vr::SnapshotTextureToBmp(nullptr, queue.Get(), texture.Get(), D3D12_RESOURCE_STATE_PRESENT, file, 1), "");
}
