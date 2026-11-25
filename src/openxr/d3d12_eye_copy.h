#pragma once

#include "core/raii_utils.h"
#include <cstdint>
#include <d3d12.h>
#include <functional>
#include <utility>
#include <wrl/client.h>

namespace fs25vr {

class D3D12EyeCopy {
public:
    static constexpr uint32_t kRingSize = 2;

    void SetDeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue);

    // Told, on every copy whose back buffer is not the size the destination was made for, the back buffer's and
    // the destination's sizes. The caller decides how often that is worth a log line.
    using SizeMismatchHandler = std::function<void(uint64_t srcWidth, uint32_t srcHeight, uint64_t dstWidth, uint32_t dstHeight)>;
    void SetSizeMismatchHandler(SizeMismatchHandler handler) { m_sizeMismatchHandler = std::move(handler); }

    // Copies `backBuffer` (in the PRESENT state, and left in it) into `destination` (in the RENDER_TARGET
    // state, and left in it) on the game's queue. `expectedWidth` x `expectedHeight` is the size the
    // destination was made for: a back buffer of another size is copied as far as both reach, and the
    // size mismatch handler is told. A null `destination` records nothing (the ring still moves on).
    // Does nothing without a back buffer, device and queue. While a trace runs it also reports the GPU
    // time of the slot's previous copy, and the lag before the GPU started it, to the PerfTrace.
    void Copy(ID3D12Resource* backBuffer, ID3D12Resource* destination, uint32_t expectedWidth, uint32_t expectedHeight);

    // Clears the game's back buffer to black on the game's queue and leaves it in the PRESENT state the
    // game expects. Returns false, and does nothing, without a back buffer, device or queue.
    bool ClearToBlack(ID3D12Resource* backBuffer);

    void Release();

private:
    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_queue = nullptr;
    SizeMismatchHandler m_sizeMismatchHandler;

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_copyAllocators[kRingSize];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_copyCommandLists[kRingSize];
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    uint64_t m_copyFenceValues[kRingSize] = {0, 0};
    uint64_t m_currentFenceValue = 0;
    uint32_t m_ringIndex = 0;
    ScopedEventHandle m_fenceEvent;
    void EnsureFence();

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_clearAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_clearCommandList;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_clearRtvHeap;
    uint64_t m_clearFenceValue = 0;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_timestampHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_timestampReadback;
    bool m_timestampsFailed = false;
    bool m_timestampPending[kRingSize] = {false, false};
    int64_t m_copySubmitNs[kRingSize] = {0, 0};
    uint64_t m_gpuTimestampFrequency = 0;
    uint64_t m_gpuCalibrationTicks = 0;
    int64_t m_gpuCalibrationCpuNs = 0;
    uint32_t m_copiesSinceCalibration = 0;
    bool EnsureTimestamps();
    void ReportTimestamps(uint32_t ringSlot);
};

}
