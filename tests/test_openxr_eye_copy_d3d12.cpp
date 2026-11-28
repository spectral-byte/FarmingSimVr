#include <gtest/gtest.h>
#include "core/perf_trace.h"
#include "openxr/openxr_manager.h"
#include "openxr/d3d12_eye_copy.h"
#include "fake_openxr_runtime.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <map>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace fs25vr {
namespace {

class FakeD3D12OpenXrRuntime : public FakeOpenXrRuntime {
public:
    ID3D12Device* device = nullptr;
    uint32_t forcedImageIndex = UINT32_MAX;

    XrResult CreateSwapchain(XrSession session, const XrSwapchainCreateInfo* createInfo, XrSwapchain* swapchain) override {
        const XrResult result = FakeOpenXrRuntime::CreateSwapchain(session, createInfo, swapchain);
        if (XR_FAILED(result) || !createInfo || !swapchain) return result;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = createInfo->width;
        desc.Height = createInfo->height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = static_cast<DXGI_FORMAT>(createInfo->format);
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        std::vector<ComPtr<ID3D12Resource>>& images = m_images[*swapchain];
        images.resize(2);
        for (auto& image : images) {
            device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                            nullptr, IID_PPV_ARGS(&image));
        }
        return result;
    }
    XrResult DestroySwapchain(XrSwapchain swapchain) override {
        m_images.erase(swapchain);
        return FakeOpenXrRuntime::DestroySwapchain(swapchain);
    }
    XrResult EnumerateSwapchainImages(XrSwapchain swapchain, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* images) override {
        const auto found = m_images.find(swapchain);
        if (found == m_images.end()) return XR_ERROR_HANDLE_INVALID;
        if (count) *count = static_cast<uint32_t>(found->second.size());
        if (capacity >= found->second.size() && images) {
            auto* d3d12Images = reinterpret_cast<XrSwapchainImageD3D12KHR*>(images);
            for (size_t i = 0; i < found->second.size(); ++i) {
                d3d12Images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
                d3d12Images[i].next = nullptr;
                d3d12Images[i].texture = found->second[i].Get();
            }
        }
        return XR_SUCCESS;
    }
    XrResult AcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index) override {
        const auto found = m_images.find(swapchain);
        if (found == m_images.end()) return XR_ERROR_HANDLE_INVALID;
        uint32_t& next = m_nextIndex[swapchain];
        if (index) *index = (forcedImageIndex != UINT32_MAX) ? forcedImageIndex : next;
        next = (next + 1) % static_cast<uint32_t>(found->second.size());
        return XR_SUCCESS;
    }

private:
    std::map<XrSwapchain, std::vector<ComPtr<ID3D12Resource>>> m_images;
    std::map<XrSwapchain, uint32_t> m_nextIndex;
};

class WarpDebugDevice {
public:
    std::string unavailableReason;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12InfoQueue> infoQueue;

    WarpDebugDevice() {
        ComPtr<ID3D12Debug> debug;
        if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            unavailableReason = "the D3D12 debug layer is not installed (Windows optional feature Graphics Tools)";
            return;
        }
        debug->EnableDebugLayer();

        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> warp;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) {
            unavailableReason = "no WARP adapter";
            return;
        }
        if (FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
            unavailableReason = "could not create a WARP D3D12 device";
            return;
        }
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))) ||
            FAILED(device.As(&infoQueue))) {
            unavailableReason = "the WARP device has no debug layer info queue";
            device.Reset();
        }
    }

    ComPtr<ID3D12Resource> CreateEyeImage(UINT width, UINT height) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ComPtr<ID3D12Resource> image;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                        nullptr, IID_PPV_ARGS(&image));
        return image;
    }

    ComPtr<ID3D12Resource> CreateBackBuffer(UINT width, UINT height) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ComPtr<ID3D12Resource> backBuffer;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PRESENT,
                                        nullptr, IID_PPV_ARGS(&backBuffer));
        return backBuffer;
    }

    void WaitForQueue() {
        ComPtr<ID3D12Fence> fence;
        device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        queue->Signal(fence.Get(), 1);
        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
    }

    std::vector<std::string> Errors() {
        std::vector<std::string> errors;
        const UINT64 count = infoQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T length = 0;
            infoQueue->GetMessage(i, nullptr, &length);
            std::vector<char> storage(length);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (FAILED(infoQueue->GetMessage(i, message, &length))) continue;
            if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR || message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                errors.push_back(std::string(message->pDescription, message->DescriptionByteLength ? message->DescriptionByteLength - 1 : 0));
            }
        }
        return errors;
    }
};

}

TEST(OpenXrEyeCopyD3D12Test, EyeCopyOnARealDeviceReportsNoDebugLayerErrors) {
    WarpDebugDevice gpu;
    if (!gpu.unavailableReason.empty()) {
        GTEST_SKIP() << "Cannot run the eye copy on a real D3D12 device: " << gpu.unavailableReason;
    }
    constexpr UINT kWidth = 256;
    constexpr UINT kHeight = 128;
    ComPtr<ID3D12Resource> backBuffer = gpu.CreateBackBuffer(kWidth, kHeight);
    ASSERT_TRUE(backBuffer);

    auto runtime = std::make_shared<FakeD3D12OpenXrRuntime>();
    runtime->device = gpu.device.Get();
    runtime->tracking.SetEyeFov(0, { -0.8f, 0.8f, 0.8f, -0.8f });
    {
        OpenXrManager manager(runtime);
        ASSERT_TRUE(manager.Initialize());
        ASSERT_TRUE(manager.CreateSession(gpu.device.Get(), gpu.queue.Get()));
        ASSERT_TRUE(manager.CreateTrackingSpaces());
        manager.PollEvents();
        ASSERT_TRUE(manager.CreateEyeSwapchains(kWidth, kHeight, DXGI_FORMAT_R8G8B8A8_UNORM));

        const uint32_t frames = 6;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            ASSERT_TRUE(manager.CopyBackBufferToEye(0, backBuffer.Get())) << "frame " << frame;
            ASSERT_TRUE(manager.CopyBackBufferToEye(1, backBuffer.Get())) << "frame " << frame;
            ASSERT_TRUE(manager.SubmitFramePair()) << "frame " << frame;
        }

        manager.DestroyEyeSwapchains();
        ASSERT_TRUE(manager.CreateEyeSwapchains(kWidth / 2, kHeight / 2, DXGI_FORMAT_R8G8B8A8_UNORM));
        for (uint32_t frame = 0; frame < frames; ++frame) {
            ASSERT_TRUE(manager.CopyBackBufferToEye(0, backBuffer.Get())) << "frame " << frame;
            ASSERT_TRUE(manager.CopyBackBufferToEye(1, backBuffer.Get())) << "frame " << frame;
            ASSERT_TRUE(manager.SubmitFramePair()) << "frame " << frame;
        }

        runtime->forcedImageIndex = 99;
        ASSERT_TRUE(manager.CopyBackBufferToEye(0, backBuffer.Get()));
        ASSERT_TRUE(manager.CopyBackBufferToEye(1, backBuffer.Get()));
        ASSERT_TRUE(manager.SubmitFramePair());
        runtime->forcedImageIndex = UINT32_MAX;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            ASSERT_TRUE(manager.CopyBackBufferToEye(0, backBuffer.Get())) << "frame " << frame;
            ASSERT_TRUE(manager.CopyBackBufferToEye(1, backBuffer.Get())) << "frame " << frame;
            ASSERT_TRUE(manager.SubmitFramePair()) << "frame " << frame;
        }

        gpu.WaitForQueue();
        manager.DestroyEyeSwapchains();
    }
    gpu.WaitForQueue();

    const std::vector<std::string> errors = gpu.Errors();
    for (const std::string& error : errors) {
        ADD_FAILURE() << "D3D12 debug layer: " << error;
    }
    EXPECT_TRUE(errors.empty());
}

TEST(OpenXrEyeCopyD3D12Test, TheModuleCopiesAndClearsWithoutAnyOpenXrHandle) {
    WarpDebugDevice gpu;
    if (!gpu.unavailableReason.empty()) {
        GTEST_SKIP() << "Cannot run the eye copy on a real D3D12 device: " << gpu.unavailableReason;
    }
    constexpr UINT kWidth = 256;
    constexpr UINT kHeight = 128;
    ComPtr<ID3D12Resource> backBuffer = gpu.CreateBackBuffer(kWidth, kHeight);
    ComPtr<ID3D12Resource> sameSize = gpu.CreateEyeImage(kWidth, kHeight);
    ComPtr<ID3D12Resource> smaller = gpu.CreateEyeImage(kWidth / 2, kHeight / 2);
    ASSERT_TRUE(backBuffer && sameSize && smaller);

    struct Mismatch { uint64_t srcWidth; uint32_t srcHeight; uint64_t dstWidth; uint32_t dstHeight; };
    std::vector<Mismatch> mismatches;
    {
        D3D12EyeCopy eyeCopy;
        eyeCopy.SetSizeMismatchHandler([&](uint64_t sw, uint32_t sh, uint64_t dw, uint32_t dh) {
            mismatches.push_back({sw, sh, dw, dh});
        });

        EXPECT_FALSE(eyeCopy.ClearToBlack(backBuffer.Get()));
        eyeCopy.Copy(backBuffer.Get(), sameSize.Get(), kWidth, kHeight);
        EXPECT_TRUE(mismatches.empty());

        eyeCopy.SetDeviceAndQueue(gpu.device.Get(), gpu.queue.Get());
        EXPECT_FALSE(eyeCopy.ClearToBlack(nullptr));

        for (uint32_t i = 0; i < 6; ++i) {
            eyeCopy.Copy(backBuffer.Get(), sameSize.Get(), kWidth, kHeight);
        }
        EXPECT_TRUE(mismatches.empty());

        eyeCopy.Copy(backBuffer.Get(), smaller.Get(), kWidth / 2, kHeight / 2);
        ASSERT_EQ(mismatches.size(), 1u);
        EXPECT_EQ(mismatches[0].srcWidth, kWidth);
        EXPECT_EQ(mismatches[0].srcHeight, kHeight);
        EXPECT_EQ(mismatches[0].dstWidth, kWidth / 2);
        EXPECT_EQ(mismatches[0].dstHeight, kHeight / 2);

        eyeCopy.Copy(backBuffer.Get(), nullptr, kWidth, kHeight);
        eyeCopy.Copy(backBuffer.Get(), sameSize.Get(), kWidth, kHeight);

        EXPECT_TRUE(eyeCopy.ClearToBlack(backBuffer.Get()));
        EXPECT_TRUE(eyeCopy.ClearToBlack(backBuffer.Get()));
        eyeCopy.Copy(backBuffer.Get(), sameSize.Get(), kWidth, kHeight);

        gpu.WaitForQueue();
        eyeCopy.Release();
        eyeCopy.Copy(backBuffer.Get(), sameSize.Get(), kWidth, kHeight);
        EXPECT_TRUE(eyeCopy.ClearToBlack(backBuffer.Get()));

        eyeCopy.Release();
    }
    gpu.WaitForQueue();

    const std::vector<std::string> errors = gpu.Errors();
    for (const std::string& error : errors) {
        ADD_FAILURE() << "D3D12 debug layer: " << error;
    }
    EXPECT_TRUE(errors.empty());
}

class WarpDevice {
public:
    std::string unavailableReason;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;

    WarpDevice() {
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> warp;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) {
            unavailableReason = "no WARP adapter";
            return;
        }
        if (FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
            unavailableReason = "could not create a WARP D3D12 device";
            return;
        }
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) {
            unavailableReason = "could not create command queue";
            device.Reset();
        }
    }

    ComPtr<ID3D12Resource> CreateEyeImage(UINT width, UINT height) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ComPtr<ID3D12Resource> image;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                        nullptr, IID_PPV_ARGS(&image));
        return image;
    }

    ComPtr<ID3D12Resource> CreateBackBuffer(UINT width, UINT height) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ComPtr<ID3D12Resource> backBuffer;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PRESENT,
                                        nullptr, IID_PPV_ARGS(&backBuffer));
        return backBuffer;
    }

    void WaitForQueue() {
        ComPtr<ID3D12Fence> fence;
        device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        queue->Signal(fence.Get(), 1);
        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
    }
};

TEST(OpenXrEyeCopyD3D12Test, UsesFourSlotRingAndReleasesPriorResourcesOnDeviceChange) {
    EXPECT_EQ(D3D12EyeCopy::kRingSize, 4u);

    WarpDevice gpu;
    if (!gpu.unavailableReason.empty()) {
        GTEST_SKIP() << gpu.unavailableReason;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue2;
    ASSERT_TRUE(SUCCEEDED(gpu.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue2))));

    constexpr UINT kWidth = 128;
    constexpr UINT kHeight = 64;
    ComPtr<ID3D12Resource> bb = gpu.CreateBackBuffer(kWidth, kHeight);
    ComPtr<ID3D12Resource> eye = gpu.CreateEyeImage(kWidth, kHeight);
    ASSERT_TRUE(bb && eye);

    auto deviceRefCount = [&]() {
        gpu.device->AddRef();
        return gpu.device->Release();
    };
    const ULONG baselineRefs = deviceRefCount();

    int64_t nowNs = 0;
    std::vector<std::string> traceLines;
    PerfTrace& trace = PerfTrace::Instance();
    trace.SetClock([&nowNs] { return nowNs += 100000; });
    trace.SetSink([&traceLines](const std::string& line) { traceLines.push_back(line); });
    trace.Start(10);

    {
        D3D12EyeCopy eyeCopy;
        eyeCopy.SetDeviceAndQueue(gpu.device.Get(), gpu.queue.Get());
        for (uint32_t i = 0; i < 8; ++i) {
            eyeCopy.Copy(bb.Get(), eye.Get(), kWidth, kHeight);
        }
        EXPECT_TRUE(eyeCopy.ClearToBlack(bb.Get()));
        EXPECT_GT(deviceRefCount(), baselineRefs);

        eyeCopy.SetDeviceAndQueue(gpu.device.Get(), queue2.Get());
        EXPECT_EQ(deviceRefCount(), baselineRefs);

        for (uint32_t i = 0; i < 8; ++i) {
            eyeCopy.Copy(bb.Get(), eye.Get(), kWidth, kHeight);
        }
        EXPECT_TRUE(eyeCopy.ClearToBlack(bb.Get()));
        EXPECT_GT(deviceRefCount(), baselineRefs);

        eyeCopy.SetDeviceAndQueue(nullptr, nullptr);
        EXPECT_EQ(deviceRefCount(), baselineRefs);
    }

    trace.Stop();
    trace.SetClock(nullptr);
    trace.SetSink(nullptr);

    gpu.WaitForQueue();
    EXPECT_TRUE(SUCCEEDED(gpu.device->GetDeviceRemovedReason()));
    ASSERT_FALSE(traceLines.empty());
    EXPECT_NE(traceLines.back().find("| GPU eye copy "), std::string::npos) << traceLines.back();
}

namespace {

class QueueHookWrapper final : public ID3D12CommandQueue {
public:
    explicit QueueHookWrapper(ID3D12CommandQueue* inner) : m_inner(inner) {}

    bool failGetTimestampFrequency = false;
    bool failSignal = false;
    uint32_t clockCalibrationCalls = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        return m_inner->QueryInterface(riid, ppvObject);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return m_inner->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return m_inner->Release(); }
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
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppvDevice) override {
        return m_inner->GetDevice(riid, ppvDevice);
    }
    void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource* a, UINT b, const D3D12_TILED_RESOURCE_COORDINATE* c,
        const D3D12_TILE_REGION_SIZE* d, ID3D12Heap* e, UINT f, const D3D12_TILE_RANGE_FLAGS* g,
        const UINT* h, const UINT* i, D3D12_TILE_MAPPING_FLAGS j) override {
        m_inner->UpdateTileMappings(a, b, c, d, e, f, g, h, i, j);
    }
    void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource* a, const D3D12_TILED_RESOURCE_COORDINATE* b,
        ID3D12Resource* c, const D3D12_TILED_RESOURCE_COORDINATE* d, const D3D12_TILE_REGION_SIZE* e,
        D3D12_TILE_MAPPING_FLAGS f) override {
        m_inner->CopyTileMappings(a, b, c, d, e, f);
    }
    void STDMETHODCALLTYPE ExecuteCommandLists(UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) override {
        m_inner->ExecuteCommandLists(NumCommandLists, ppCommandLists);
    }
    void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void* pData, UINT Size) override {
        m_inner->SetMarker(Metadata, pData, Size);
    }
    void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void* pData, UINT Size) override {
        m_inner->BeginEvent(Metadata, pData, Size);
    }
    void STDMETHODCALLTYPE EndEvent() override { m_inner->EndEvent(); }
    HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* pFence, UINT64 Value) override {
        if (failSignal) return E_FAIL;
        return m_inner->Signal(pFence, Value);
    }
    HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* pFence, UINT64 Value) override {
        return m_inner->Wait(pFence, Value);
    }
    HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* pFrequency) override {
        if (failGetTimestampFrequency) return E_FAIL;
        return m_inner->GetTimestampFrequency(pFrequency);
    }
    HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* pGpuTimestamp, UINT64* pCpuTimestamp) override {
        ++clockCalibrationCalls;
        return m_inner->GetClockCalibration(pGpuTimestamp, pCpuTimestamp);
    }
    D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc() override { return m_inner->GetDesc(); }

private:
    ID3D12CommandQueue* m_inner;
};

}

TEST(OpenXrEyeCopyD3D12Test, ReleaseResetsTimestampFailureAndCalibrationStateAndHandlesSignalFailure) {
    WarpDevice gpu;
    if (!gpu.unavailableReason.empty()) {
        GTEST_SKIP() << gpu.unavailableReason;
    }
    constexpr UINT kWidth = 64;
    constexpr UINT kHeight = 64;
    ComPtr<ID3D12Resource> bb = gpu.CreateBackBuffer(kWidth, kHeight);
    ComPtr<ID3D12Resource> eye = gpu.CreateEyeImage(kWidth, kHeight);
    ASSERT_TRUE(bb && eye);

    QueueHookWrapper queue(gpu.queue.Get());
    queue.failGetTimestampFrequency = true;

    int64_t nowNs = 0;
    std::vector<std::string> traceLines;
    PerfTrace& trace = PerfTrace::Instance();
    trace.SetClock([&nowNs] { return nowNs += 100000; });
    trace.SetSink([&traceLines](const std::string& line) { traceLines.push_back(line); });
    trace.Start(10);

    D3D12EyeCopy eyeCopy;
    eyeCopy.SetDeviceAndQueue(gpu.device.Get(), &queue);
    for (uint32_t i = 0; i < 6; ++i) {
        eyeCopy.Copy(bb.Get(), eye.Get(), kWidth, kHeight);
    }
    gpu.WaitForQueue();
    eyeCopy.Release();

    queue.failGetTimestampFrequency = false;
    for (uint32_t i = 0; i < 6; ++i) {
        eyeCopy.Copy(bb.Get(), eye.Get(), kWidth, kHeight);
    }
    EXPECT_EQ(queue.clockCalibrationCalls, 1u);

    gpu.WaitForQueue();
    eyeCopy.Release();
    for (uint32_t i = 0; i < 5; ++i) {
        eyeCopy.Copy(bb.Get(), eye.Get(), kWidth, kHeight);
    }
    EXPECT_EQ(queue.clockCalibrationCalls, 2u);

    queue.failSignal = true;
    EXPECT_FALSE(eyeCopy.ClearToBlack(bb.Get()));
    eyeCopy.Copy(bb.Get(), eye.Get(), kWidth, kHeight);
    queue.failSignal = false;
    gpu.WaitForQueue();
    eyeCopy.Release();

    trace.Stop();
    trace.SetClock(nullptr);
    trace.SetSink(nullptr);

    EXPECT_TRUE(SUCCEEDED(gpu.device->GetDeviceRemovedReason()));
    ASSERT_FALSE(traceLines.empty());
    EXPECT_NE(traceLines.back().find("| GPU eye copy "), std::string::npos) << traceLines.back();
}

}
