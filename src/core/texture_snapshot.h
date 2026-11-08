#pragma once

#include <d3d12.h>
#include <dxgiformat.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs25vr {

// Diagnostics (`vrEyeSnapshot`): what the Injector hands an eye, written to a picture file that can be looked at. The
// texture is copied to the CPU on the given queue and the call blocks until the GPU has done the copy, so it is for one-off
// use only, never every frame.

// One pixel of `format` as 8-bit blue, green, red (16-bit float is clamped and given an sRGB-like curve, 10-bit keeps its top
// 8 bits). False for a format it does not read.
bool DecodePixelToBgr8(DXGI_FORMAT format, const uint8_t* pixel, uint8_t bgr[3]);

// A 24-bit BMP file of width x height from `bgrTopDown`: 3 bytes per pixel, rows from the top, no padding.
std::vector<uint8_t> EncodeBmp24(uint32_t width, uint32_t height, const std::vector<uint8_t>& bgrTopDown);

// Copies subresource 0 of `texture`, which is in `state` on `queue` and is put back in it, and writes every `step`-th pixel
// of every `step`-th row to `file` as a BMP. Empty on success, otherwise why it was not written.
std::string SnapshotTextureToBmp(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* texture,
                                 D3D12_RESOURCE_STATES state, const std::filesystem::path& file, uint32_t step);

} // namespace fs25vr
