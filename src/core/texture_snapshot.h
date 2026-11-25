#pragma once

#include <d3d12.h>
#include <dxgiformat.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs25vr {

bool DecodePixelToBgr8(DXGI_FORMAT format, const uint8_t* pixel, uint8_t bgr[3]);

std::vector<uint8_t> EncodeBmp24(uint32_t width, uint32_t height, const std::vector<uint8_t>& bgrTopDown);

// Copies subresource 0 of `texture`, which is in `state` on `queue` and is put back in it, and writes every `step`-th pixel
// of every `step`-th row to `file` as a BMP. Empty on success, otherwise why it was not written.
std::string SnapshotTextureToBmp(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* texture,
                                 D3D12_RESOURCE_STATES state, const std::filesystem::path& file, uint32_t step);

}
