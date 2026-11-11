#include "d3d12_eye_copy.h"
#include "core/logger.h"
#include "core/perf_trace.h"
#include <algorithm>
#include <utility>

namespace fs25vr {

void D3D12EyeCopy::SetDeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue) {
    m_device = device;
    m_queue = queue;
}

void D3D12EyeCopy::Release() {
    if (m_fence && m_queue && m_currentFenceValue > 0) {
        if (m_fence->GetCompletedValue() < m_currentFenceValue) {
            if (m_fenceEvent.IsValid()) {
                m_fence->SetEventOnCompletion(m_currentFenceValue, m_fenceEvent.Get());
                WaitForSingleObject(m_fenceEvent.Get(), INFINITE);
            }
        }
    }
    m_fence.Reset();
    m_currentFenceValue = 0;
    m_clearFenceValue = 0;
    m_clearCommandList.Reset();
    m_clearAllocator.Reset();
    m_clearRtvHeap.Reset();
    for (uint32_t r = 0; r < kRingSize; ++r) {
        m_copyFenceValues[r] = 0;
        m_copyCommandLists[r].Reset();
        m_copyAllocators[r].Reset();
        m_timestampPending[r] = false;
    }
    m_timestampHeap.Reset();
    m_timestampReadback.Reset();
    m_ringIndex = 0;
    m_fenceEvent.Close();
}

void D3D12EyeCopy::EnsureFence() {
    if (m_fence || !m_device) return;
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    m_currentFenceValue = 0;
    m_clearFenceValue = 0;
    for (uint32_t r = 0; r < kRingSize; ++r) {
        m_copyFenceValues[r] = 0;
    }
    if (!m_fenceEvent.IsValid()) {
        m_fenceEvent = ScopedEventHandle::Create(nullptr, FALSE, FALSE, nullptr);
    }
}

bool D3D12EyeCopy::EnsureTimestamps() {
    if (m_timestampHeap && m_timestampReadback) return true;
    if (m_timestampsFailed || !m_device || !m_queue || !m_fence) return false;

    D3D12_QUERY_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = kRingSize * 2;
    D3D12_HEAP_PROPERTIES props{};
    props.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kRingSize * 2 * sizeof(uint64_t);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    UINT64 frequency = 0;
    if (FAILED(m_device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_timestampHeap))) ||
        FAILED(m_device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                 nullptr, IID_PPV_ARGS(&m_timestampReadback))) ||
        FAILED(m_queue->GetTimestampFrequency(&frequency)) || frequency == 0) {
        // The trace still has its CPU timings; only the GPU eye copy lines are missing.
        LogWarn("OpenXrManager: GPU timestamps unavailable; vrPerfTrace logs no GPU eye copy timing");
        m_timestampHeap.Reset();
        m_timestampReadback.Reset();
        m_timestampsFailed = true;
        return false;
    }
    m_gpuTimestampFrequency = frequency;
    m_copiesSinceCalibration = 0;
    PerfTrace::Instance().SetGpuTimestampFrequency(frequency);
    return true;
}

void D3D12EyeCopy::ReportTimestamps(uint32_t ringSlot) {
    if (!m_timestampPending[ringSlot]) return;
    m_timestampPending[ringSlot] = false;
    if (!m_timestampReadback || !PerfTrace::Instance().IsActive()) return;

    const D3D12_RANGE readRange{ringSlot * 2 * sizeof(uint64_t), (ringSlot * 2 + 2) * sizeof(uint64_t)};
    void* mapped = nullptr;
    if (FAILED(m_timestampReadback->Map(0, &readRange, &mapped)) || !mapped) return;
    const uint64_t* stamps = static_cast<const uint64_t*>(mapped) + ringSlot * 2;
    const uint64_t start = stamps[0];
    const uint64_t end = stamps[1];
    const D3D12_RANGE written{0, 0};
    m_timestampReadback->Unmap(0, &written);
    if (end < start) return;

    // The GPU and CPU clocks drift apart slowly; recalibrating every few dozen copies keeps the lag exact.
    if (m_copiesSinceCalibration++ % 64 == 0) {
        UINT64 gpuTicks = 0;
        UINT64 cpuQpc = 0;
        if (SUCCEEDED(m_queue->GetClockCalibration(&gpuTicks, &cpuQpc))) {
            m_gpuCalibrationTicks = gpuTicks;
            m_gpuCalibrationCpuNs = QpcTicksToNanoseconds(static_cast<int64_t>(cpuQpc), QpcFrequency());
        }
    }
    PerfTrace& trace = PerfTrace::Instance();
    trace.AddSample(PerfSection::GpuEyeCopy, GpuTicksToNanoseconds(end - start, m_gpuTimestampFrequency));
    if (m_gpuCalibrationTicks != 0) {
        const int64_t startNs = GpuTimestampToCpuNanoseconds(start, m_gpuTimestampFrequency, m_gpuCalibrationTicks,
                                                             m_gpuCalibrationCpuNs);
        trace.AddSample(PerfSection::GpuQueueLag, startNs - m_copySubmitNs[ringSlot]);
    }
}

void D3D12EyeCopy::Copy(ID3D12Resource* backBuffer, ID3D12Resource* destTexture, uint32_t scWidth, uint32_t scHeight) {
    if (!backBuffer || !m_device || !m_queue) {
        return;
    }
    EnsureFence();

    uint32_t ringSlot = m_ringIndex;
    m_ringIndex = (m_ringIndex + 1) % kRingSize;

    if (!m_copyAllocators[ringSlot]) {
        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_copyAllocators[ringSlot]));
    }
    if (!m_copyCommandLists[ringSlot] && m_copyAllocators[ringSlot]) {
        // A new list is created open. Close it so every use starts from a closed list that can be reset.
        if (SUCCEEDED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_copyAllocators[ringSlot].Get(), nullptr, IID_PPV_ARGS(&m_copyCommandLists[ringSlot])))) {
            m_copyCommandLists[ringSlot]->Close();
        }
    }

    auto& curAllocator = m_copyAllocators[ringSlot];
    auto& curCommandList = m_copyCommandLists[ringSlot];

    // A copy with no destination texture records nothing: the slot's list stays closed, nothing is
    // executed and its fence value is not advanced.
    if (!(curCommandList && curAllocator && destTexture)) {
        return;
    }

    // Synchronize with previous execution on this specific ring slot if GPU has not finished it
    uint64_t waitVal = m_copyFenceValues[ringSlot];
    {
        PerfScope fenceTiming(PerfSection::CopyFenceWait);
        if (m_fence && waitVal > 0) {
            if (m_fence->GetCompletedValue() < waitVal) {
                if (m_fenceEvent.IsValid()) {
                    m_fence->SetEventOnCompletion(waitVal, m_fenceEvent.Get());
                    WaitForSingleObject(m_fenceEvent.Get(), INFINITE);
                }
            }
        }
    }
    // The slot's earlier copy has run, so its timestamps are written.
    ReportTimestamps(ringSlot);
    const bool timed = PerfTrace::Instance().IsActive() && EnsureTimestamps();

    // The slot's fence has passed, so the allocator is idle; its list is closed, so it can be reset.
    curAllocator->Reset();
    curCommandList->Reset(curAllocator.Get(), nullptr);

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = backBuffer;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = destTexture;
    // OpenXR D3D12: acquired color swapchain images are in RENDER_TARGET and must be
    // returned to RENDER_TARGET before release.
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    if (timed) {
        curCommandList->EndQuery(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ringSlot * 2);
    }
    curCommandList->ResourceBarrier(2, barriers);

    D3D12_RESOURCE_DESC srcDesc = backBuffer->GetDesc();
    if (srcDesc.Width == scWidth && srcDesc.Height == scHeight) {
        curCommandList->CopyResource(destTexture, backBuffer);
    } else {
        D3D12_RESOURCE_DESC dstDesc = destTexture->GetDesc();
        if (m_sizeMismatchHandler) {
            m_sizeMismatchHandler(srcDesc.Width, srcDesc.Height, dstDesc.Width, dstDesc.Height);
        }
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = destTexture;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = backBuffer;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0;

        D3D12_BOX box{};
        box.left = 0;
        box.top = 0;
        box.front = 0;
        box.right = static_cast<UINT>(std::min(srcDesc.Width, static_cast<UINT64>(dstDesc.Width)));
        box.bottom = static_cast<UINT>(std::min(srcDesc.Height, dstDesc.Height));
        box.back = 1;

        curCommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &box);
    }

    std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
    std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
    curCommandList->ResourceBarrier(2, barriers);
    if (timed) {
        curCommandList->EndQuery(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ringSlot * 2 + 1);
        curCommandList->ResolveQueryData(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ringSlot * 2, 2,
                                         m_timestampReadback.Get(), ringSlot * 2 * sizeof(uint64_t));
    }

    curCommandList->Close();
    ID3D12CommandList* lists[] = { curCommandList.Get() };
    if (timed) {
        m_copySubmitNs[ringSlot] = PerfTrace::Instance().Now();
    }
    m_queue->ExecuteCommandLists(1, lists);

    if (m_fence) {
        m_currentFenceValue++;
        m_copyFenceValues[ringSlot] = m_currentFenceValue;
        m_queue->Signal(m_fence.Get(), m_currentFenceValue);
        m_timestampPending[ringSlot] = timed;
    }
}

bool D3D12EyeCopy::ClearToBlack(ID3D12Resource* backBuffer) {
    if (!backBuffer || !m_device || !m_queue) {
        return false;
    }
    EnsureFence();
    if (!m_fence) {
        return false;
    }

    if (!m_clearAllocator) {
        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_clearAllocator));
    }
    if (!m_clearCommandList && m_clearAllocator) {
        if (SUCCEEDED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_clearAllocator.Get(), nullptr, IID_PPV_ARGS(&m_clearCommandList)))) {
            m_clearCommandList->Close();
        }
    }
    if (!m_clearRtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = 1;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_clearRtvHeap));
    }
    if (!m_clearAllocator || !m_clearCommandList || !m_clearRtvHeap) {
        return false;
    }

    // The previous clear's allocator is reusable only once the GPU has finished with it.
    if (m_clearFenceValue > 0 && m_fence->GetCompletedValue() < m_clearFenceValue && m_fenceEvent.IsValid()) {
        m_fence->SetEventOnCompletion(m_clearFenceValue, m_fenceEvent.Get());
        WaitForSingleObject(m_fenceEvent.Get(), INFINITE);
    }
    m_clearAllocator->Reset();
    m_clearCommandList->Reset(m_clearAllocator.Get(), nullptr);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_clearRtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateRenderTargetView(backBuffer, nullptr, rtv);

    // The game hands the back buffer to Present in the PRESENT state (as Copy assumes);
    // it goes back to that state after the clear.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_clearCommandList->ResourceBarrier(1, &barrier);
    const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_clearCommandList->ClearRenderTargetView(rtv, black, 0, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    m_clearCommandList->ResourceBarrier(1, &barrier);
    m_clearCommandList->Close();

    ID3D12CommandList* lists[] = { m_clearCommandList.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    m_currentFenceValue++;
    m_clearFenceValue = m_currentFenceValue;
    m_queue->Signal(m_fence.Get(), m_currentFenceValue);
    return true;
}

} // namespace fs25vr
