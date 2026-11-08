#include "core/texture_snapshot.h"

#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

using Microsoft::WRL::ComPtr;

namespace fs25vr {

namespace {

float HalfToFloat(uint16_t half) {
    const uint32_t sign = (half >> 15) & 1u;
    const uint32_t exponent = (half >> 10) & 0x1Fu;
    const uint32_t mantissa = half & 0x3FFu;
    float value;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 31) {
        value = mantissa ? 0.0f : 65504.0f;  // NaN reads as black, infinity as the largest value
    } else {
        value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    }
    return sign ? -value : value;
}

// A linear channel (scRGB, 1.0 = SDR white) as an 8-bit value with an sRGB-like curve
uint8_t LinearToByte(float linear) {
    if (!(linear > 0.0f)) return 0;
    const float encoded = std::pow(std::min(linear, 1.0f), 1.0f / 2.2f);
    return static_cast<uint8_t>(std::lround(encoded * 255.0f));
}

void PutLE16(std::vector<uint8_t>& out, size_t at, uint16_t value) {
    out[at] = static_cast<uint8_t>(value);
    out[at + 1] = static_cast<uint8_t>(value >> 8);
}

void PutLE32(std::vector<uint8_t>& out, size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) out[at + i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t BytesPerPixel(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return 4;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return 8;
        default:
            return 0;
    }
}

} // namespace

bool DecodePixelToBgr8(DXGI_FORMAT format, const uint8_t* pixel, uint8_t bgr[3]) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            bgr[0] = pixel[2];
            bgr[1] = pixel[1];
            bgr[2] = pixel[0];
            return true;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            bgr[0] = pixel[0];
            bgr[1] = pixel[1];
            bgr[2] = pixel[2];
            return true;
        case DXGI_FORMAT_R10G10B10A2_UNORM: {
            uint32_t value;
            std::memcpy(&value, pixel, sizeof(value));
            bgr[2] = static_cast<uint8_t>((value >> 2) & 0xFFu);   // red: bits 0-9
            bgr[1] = static_cast<uint8_t>((value >> 12) & 0xFFu);  // green: bits 10-19
            bgr[0] = static_cast<uint8_t>((value >> 22) & 0xFFu);  // blue: bits 20-29
            return true;
        }
        case DXGI_FORMAT_R16G16B16A16_FLOAT: {
            uint16_t channels[3];
            std::memcpy(channels, pixel, sizeof(channels));
            bgr[2] = LinearToByte(HalfToFloat(channels[0]));
            bgr[1] = LinearToByte(HalfToFloat(channels[1]));
            bgr[0] = LinearToByte(HalfToFloat(channels[2]));
            return true;
        }
        default:
            return false;
    }
}

std::vector<uint8_t> EncodeBmp24(uint32_t width, uint32_t height, const std::vector<uint8_t>& bgrTopDown) {
    constexpr uint32_t kHeaderBytes = 14 + 40;
    const uint32_t rowBytes = (width * 3 + 3) & ~3u;
    const uint32_t imageBytes = rowBytes * height;
    std::vector<uint8_t> out(kHeaderBytes + imageBytes, 0);
    out[0] = 'B';
    out[1] = 'M';
    PutLE32(out, 2, kHeaderBytes + imageBytes);
    PutLE32(out, 10, kHeaderBytes);
    PutLE32(out, 14, 40);
    PutLE32(out, 18, width);
    PutLE32(out, 22, height);  // positive: rows stored bottom-up
    PutLE16(out, 26, 1);
    PutLE16(out, 28, 24);
    PutLE32(out, 34, imageBytes);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* source = bgrTopDown.data() + static_cast<size_t>(y) * width * 3;
        uint8_t* destination = out.data() + kHeaderBytes + static_cast<size_t>(height - 1 - y) * rowBytes;
        std::memcpy(destination, source, static_cast<size_t>(width) * 3);
    }
    return out;
}

std::string SnapshotTextureToBmp(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* texture,
                                 D3D12_RESOURCE_STATES state, const std::filesystem::path& file, uint32_t step) {
    if (!device || !queue || !texture) return "no device, queue or texture";
    step = std::max(step, 1u);
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    const uint32_t bytesPerPixel = BytesPerPixel(desc.Format);
    if (bytesPerPixel == 0) return "format " + std::to_string(static_cast<int>(desc.Format)) + " is not read";

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowSize, &totalBytes);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&readback)))) {
        return "could not create the readback buffer";
    }
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)))) {
        return "could not create a command list";
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture;
    barrier.Transition.StateBefore = state;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = texture;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    if (FAILED(list->Close())) return "could not record the copy";

    ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return "could not create a fence";
    ID3D12CommandList* lists[] = { list.Get() };
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) {
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) return "could not create an event";
        fence->SetEventOnCompletion(1, event);
        const DWORD waited = WaitForSingleObject(event, 5000);
        CloseHandle(event);
        if (waited != WAIT_OBJECT_0) return "the GPU did not finish the copy within 5 s";
    }

    void* mapped = nullptr;
    const D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(totalBytes) };
    if (FAILED(readback->Map(0, &readRange, &mapped)) || !mapped) return "could not map the readback buffer";
    const uint32_t width = static_cast<uint32_t>(desc.Width);
    const uint32_t height = desc.Height;
    const uint32_t outWidth = (width + step - 1) / step;
    const uint32_t outHeight = (height + step - 1) / step;
    std::vector<uint8_t> bgr(static_cast<size_t>(outWidth) * outHeight * 3);
    const auto* base = static_cast<const uint8_t*>(mapped) + footprint.Offset;
    for (uint32_t oy = 0; oy < outHeight; ++oy) {
        const uint8_t* row = base + static_cast<size_t>(oy) * step * footprint.Footprint.RowPitch;
        for (uint32_t ox = 0; ox < outWidth; ++ox) {
            DecodePixelToBgr8(desc.Format, row + static_cast<size_t>(ox) * step * bytesPerPixel,
                              &bgr[(static_cast<size_t>(oy) * outWidth + ox) * 3]);
        }
    }
    const D3D12_RANGE noWrite{ 0, 0 };
    readback->Unmap(0, &noWrite);

    const std::vector<uint8_t> bmp = EncodeBmp24(outWidth, outHeight, bgr);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return "could not open " + file.string();
    out.write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
    return out ? std::string() : "could not write " + file.string();
}

} // namespace fs25vr
