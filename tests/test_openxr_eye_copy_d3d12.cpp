#include <gtest/gtest.h>
#include "core/perf_trace.h"
#include "openxr/openxr_manager.h"
#include "openxr/d3d12_eye_copy.h"
#include "fake_openxr_runtime.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <atomic>
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

constexpr GUID kEyeCopyMockFenceGuid = { 0xa1b2c3d4, 0x5e6f, 0x7081, { 0x92, 0xa3, 0xb4, 0xc5, 0xd6, 0xe7, 0xf8, 0x09 } };

class EyeCopyMockFence final : public ID3D12Fence {
public:
    EyeCopyMockFence(ComPtr<ID3D12Fence> inner, const std::atomic<bool>* returnDeviceRemoved)
        : m_inner(std::move(inner)), m_returnDeviceRemoved(returnDeviceRemoved) {}

    ID3D12Fence* Inner() const { return m_inner.Get(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == kEyeCopyMockFenceGuid) {
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
    UINT64 STDMETHODCALLTYPE GetCompletedValue() override {
        if (m_returnDeviceRemoved && m_returnDeviceRemoved->load()) return UINT64_MAX;
        return m_inner->GetCompletedValue();
    }
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 Value, HANDLE hEvent) override {
        return m_inner->SetEventOnCompletion(Value, hEvent);
    }
    HRESULT STDMETHODCALLTYPE Signal(UINT64 Value) override { return m_inner->Signal(Value); }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    ComPtr<ID3D12Fence> m_inner;
    const std::atomic<bool>* m_returnDeviceRemoved = nullptr;
};

class QueueHookWrapper final : public ID3D12CommandQueue {
public:
    explicit QueueHookWrapper(ID3D12CommandQueue* inner) : m_inner(inner) {}

    bool failGetTimestampFrequency = false;
    bool failSignal = false;
    uint32_t clockCalibrationCalls = 0;
    uint32_t executeCalls = 0;

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
        ++executeCalls;
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
        void* raw = nullptr;
        if (pFence && SUCCEEDED(pFence->QueryInterface(kEyeCopyMockFenceGuid, &raw)) && raw) {
            ComPtr<ID3D12Fence> wrapped;
            wrapped.Attach(static_cast<ID3D12Fence*>(raw));
            return m_inner->Signal(static_cast<EyeCopyMockFence*>(wrapped.Get())->Inner(), Value);
        }
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

TEST(OpenXrEyeCopyD3D12Test, SignalFailureBlocksUnfencedAllocatorReuseAndReleaseDetachesInFlightResources) {
    WarpDevice gpu;
    if (!gpu.unavailableReason.empty()) {
        GTEST_SKIP() << gpu.unavailableReason;
    }
    constexpr UINT kWidth = 64;
    constexpr UINT kHeight = 64;
    ComPtr<ID3D12Resource> bb = gpu.CreateBackBuffer(kWidth, kHeight);
    ComPtr<ID3D12Resource> eye = gpu.CreateEyeImage(kWidth, kHeight);
    ASSERT_TRUE(bb && eye);

    auto deviceRefCount = [&]() {
        gpu.device->AddRef();
        return gpu.device->Release();
    };
    const ULONG baselineRefs = deviceRefCount();

    QueueHookWrapper queue(gpu.queue.Get());
    D3D12EyeCopy eyeCopy;
    eyeCopy.SetDeviceAndQueue(gpu.device.Get(), &queue);
    for (uint32_t i = 0; i < D3D12EyeCopy::kRingSize; ++i) {
        eyeCopy.Copy(bb.Get(), eye.Get(), kWidth, kHeight);
    }
    ASSERT_TRUE(eyeCopy.ClearToBlack(bb.Get()));
    ASSERT_GT(deviceRefCount(), baselineRefs);

    queue.failSignal = true;
    const uint32_t execBeforeClear = queue.executeCalls;
    EXPECT_FALSE(eyeCopy.ClearToBlack(bb.Get()));
    EXPECT_EQ(queue.executeCalls, execBeforeClear + 1u);

    const uint32_t execBeforeFaultedCopy = queue.executeCalls;
    eyeCopy.Copy(bb.Get(), eye.Get(), kWidth, kHeight);
    EXPECT_EQ(queue.executeCalls, execBeforeFaultedCopy + 1u);

    queue.failSignal = false;
    const uint32_t execBeforeClearRetry = queue.executeCalls;
    EXPECT_FALSE(eyeCopy.ClearToBlack(bb.Get()));
    EXPECT_EQ(queue.executeCalls, execBeforeClearRetry);

    for (uint32_t i = 0; i < D3D12EyeCopy::kRingSize - 1; ++i) {
        const uint32_t before = queue.executeCalls;
        eyeCopy.Copy(bb.Get(), eye.Get(), kWidth, kHeight);
        EXPECT_EQ(queue.executeCalls, before + 1u);
    }
    const uint32_t execBeforeFaultedSlotWrap = queue.executeCalls;
    eyeCopy.Copy(bb.Get(), eye.Get(), kWidth, kHeight);
    EXPECT_EQ(queue.executeCalls, execBeforeFaultedSlotWrap);

    gpu.WaitForQueue();
    const ULONG refsBeforeRelease = deviceRefCount();
    eyeCopy.Release();
    EXPECT_EQ(deviceRefCount(), refsBeforeRelease);
    EXPECT_GT(deviceRefCount(), baselineRefs);
}

namespace {

class EyeCopyMockDevice final : public ID3D12Device {
public:
    EyeCopyMockDevice(ComPtr<ID3D12Device> inner, const std::atomic<bool>* returnDeviceRemoved)
        : m_inner(std::move(inner)), m_returnDeviceRemoved(returnDeviceRemoved) {}

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
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a, UINT* b, void* c) override { return m_inner->GetPrivateData(a, b, c); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a, UINT b, const void* c) override { return m_inner->SetPrivateData(a, b, c); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a, const IUnknown* b) override { return m_inner->SetPrivateDataInterface(a, b); }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR a) override { return m_inner->SetName(a); }
    UINT STDMETHODCALLTYPE GetNodeCount() override { return m_inner->GetNodeCount(); }
    HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* a, REFIID b, void** c) override {
        return m_inner->CreateCommandQueue(a, b, c);
    }
    HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE a, REFIID b, void** c) override {
        return m_inner->CreateCommandAllocator(a, b, c);
    }
    HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* a, REFIID b, void** c) override {
        return m_inner->CreateGraphicsPipelineState(a, b, c);
    }
    HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* a, REFIID b, void** c) override {
        return m_inner->CreateComputePipelineState(a, b, c);
    }
    HRESULT STDMETHODCALLTYPE CreateCommandList(UINT a, D3D12_COMMAND_LIST_TYPE b, ID3D12CommandAllocator* c,
                                                ID3D12PipelineState* d, REFIID e, void** f) override {
        return m_inner->CreateCommandList(a, b, c, d, e, f);
    }
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE a, void* b, UINT c) override {
        return m_inner->CheckFeatureSupport(a, b, c);
    }
    HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* a, REFIID b, void** c) override {
        return m_inner->CreateDescriptorHeap(a, b, c);
    }
    UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE a) override {
        return m_inner->GetDescriptorHandleIncrementSize(a);
    }
    HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT a, const void* b, SIZE_T c, REFIID d, void** e) override {
        return m_inner->CreateRootSignature(a, b, c, d, e);
    }
    void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* a, D3D12_CPU_DESCRIPTOR_HANDLE b) override {
        m_inner->CreateConstantBufferView(a, b);
    }
    void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource* a, const D3D12_SHADER_RESOURCE_VIEW_DESC* b,
                                                    D3D12_CPU_DESCRIPTOR_HANDLE c) override {
        m_inner->CreateShaderResourceView(a, b, c);
    }
    void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource* a, ID3D12Resource* b,
                                                     const D3D12_UNORDERED_ACCESS_VIEW_DESC* c, D3D12_CPU_DESCRIPTOR_HANDLE d) override {
        m_inner->CreateUnorderedAccessView(a, b, c, d);
    }
    void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource* a, const D3D12_RENDER_TARGET_VIEW_DESC* b,
                                                  D3D12_CPU_DESCRIPTOR_HANDLE c) override {
        m_inner->CreateRenderTargetView(a, b, c);
    }
    void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource* a, const D3D12_DEPTH_STENCIL_VIEW_DESC* b,
                                                  D3D12_CPU_DESCRIPTOR_HANDLE c) override {
        m_inner->CreateDepthStencilView(a, b, c);
    }
    void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC* a, D3D12_CPU_DESCRIPTOR_HANDLE b) override {
        m_inner->CreateSampler(a, b);
    }
    void STDMETHODCALLTYPE CopyDescriptors(UINT a, const D3D12_CPU_DESCRIPTOR_HANDLE* b, const UINT* c, UINT d,
                                           const D3D12_CPU_DESCRIPTOR_HANDLE* e, const UINT* f, D3D12_DESCRIPTOR_HEAP_TYPE g) override {
        m_inner->CopyDescriptors(a, b, c, d, e, f, g);
    }
    void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT a, D3D12_CPU_DESCRIPTOR_HANDLE b, D3D12_CPU_DESCRIPTOR_HANDLE c,
                                                 D3D12_DESCRIPTOR_HEAP_TYPE d) override {
        m_inner->CopyDescriptorsSimple(a, b, c, d);
    }
    D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(UINT a, UINT b, const D3D12_RESOURCE_DESC* c) override {
        return m_inner->GetResourceAllocationInfo(a, b, c);
    }
    D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(UINT a, D3D12_HEAP_TYPE b) override {
        return m_inner->GetCustomHeapProperties(a, b);
    }
    HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES* a, D3D12_HEAP_FLAGS b, const D3D12_RESOURCE_DESC* c,
                                                      D3D12_RESOURCE_STATES d, const D3D12_CLEAR_VALUE* e, REFIID f, void** g) override {
        return m_inner->CreateCommittedResource(a, b, c, d, e, f, g);
    }
    HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC* a, REFIID b, void** c) override { return m_inner->CreateHeap(a, b, c); }
    HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap* a, UINT64 b, const D3D12_RESOURCE_DESC* c, D3D12_RESOURCE_STATES d,
                                                   const D3D12_CLEAR_VALUE* e, REFIID f, void** g) override {
        return m_inner->CreatePlacedResource(a, b, c, d, e, f, g);
    }
    HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC* a, D3D12_RESOURCE_STATES b,
                                                     const D3D12_CLEAR_VALUE* c, REFIID d, void** e) override {
        return m_inner->CreateReservedResource(a, b, c, d, e);
    }
    HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild* a, const SECURITY_ATTRIBUTES* b, DWORD c, LPCWSTR d, HANDLE* e) override {
        return m_inner->CreateSharedHandle(a, b, c, d, e);
    }
    HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE a, REFIID b, void** c) override { return m_inner->OpenSharedHandle(a, b, c); }
    HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR a, DWORD b, HANDLE* c) override { return m_inner->OpenSharedHandleByName(a, b, c); }
    HRESULT STDMETHODCALLTYPE MakeResident(UINT a, ID3D12Pageable* const* b) override { return m_inner->MakeResident(a, b); }
    HRESULT STDMETHODCALLTYPE Evict(UINT a, ID3D12Pageable* const* b) override { return m_inner->Evict(a, b); }
    HRESULT STDMETHODCALLTYPE CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void** ppFence) override {
        ComPtr<ID3D12Fence> innerFence;
        const HRESULT hr = m_inner->CreateFence(InitialValue, Flags, IID_PPV_ARGS(&innerFence));
        if (FAILED(hr)) return hr;
        ComPtr<EyeCopyMockFence> wrapped;
        wrapped.Attach(new EyeCopyMockFence(innerFence, m_returnDeviceRemoved));
        return wrapped->QueryInterface(riid, ppFence);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override { return m_inner->GetDeviceRemovedReason(); }
    void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC* a, UINT b, UINT c, UINT64 d,
                                                 D3D12_PLACED_SUBRESOURCE_FOOTPRINT* e, UINT* f, UINT64* g, UINT64* h) override {
        m_inner->GetCopyableFootprints(a, b, c, d, e, f, g, h);
    }
    HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* a, REFIID b, void** c) override {
        return m_inner->CreateQueryHeap(a, b, c);
    }
    HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL a) override { return m_inner->SetStablePowerState(a); }
    HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* a, ID3D12RootSignature* b, REFIID c, void** d) override {
        return m_inner->CreateCommandSignature(a, b, c, d);
    }
    void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource* a, UINT* b, D3D12_PACKED_MIP_INFO* c,
                                             D3D12_TILE_SHAPE* d, UINT* e, UINT f, D3D12_SUBRESOURCE_TILING* g) override {
        m_inner->GetResourceTiling(a, b, c, d, e, f, g);
    }
    LUID STDMETHODCALLTYPE GetAdapterLuid() override { return m_inner->GetAdapterLuid(); }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    ComPtr<ID3D12Device> m_inner;
    const std::atomic<bool>* m_returnDeviceRemoved = nullptr;
};

}

TEST(OpenXrEyeCopyD3D12Test, ReleaseDetachesGpuObjectsWhenFenceWaitReportsDeviceRemoved) {
    WarpDevice gpu;
    if (!gpu.unavailableReason.empty()) {
        GTEST_SKIP() << gpu.unavailableReason;
    }
    constexpr UINT kWidth = 64;
    constexpr UINT kHeight = 64;
    ComPtr<ID3D12Resource> bb = gpu.CreateBackBuffer(kWidth, kHeight);
    ComPtr<ID3D12Resource> eye = gpu.CreateEyeImage(kWidth, kHeight);
    ASSERT_TRUE(bb && eye);

    auto deviceRefCount = [&]() {
        gpu.device->AddRef();
        return gpu.device->Release();
    };
    const ULONG baselineRefs = deviceRefCount();

    std::atomic<bool> returnDeviceRemoved{ false };
    ComPtr<EyeCopyMockDevice> mockDevice;
    mockDevice.Attach(new EyeCopyMockDevice(gpu.device, &returnDeviceRemoved));
    QueueHookWrapper queue(gpu.queue.Get());

    D3D12EyeCopy eyeCopy;
    eyeCopy.SetDeviceAndQueue(mockDevice.Get(), &queue);
    eyeCopy.Copy(bb.Get(), eye.Get(), kWidth, kHeight);
    ASSERT_TRUE(eyeCopy.ClearToBlack(bb.Get()));
    gpu.WaitForQueue();
    const ULONG refsBeforeRelease = deviceRefCount();
    ASSERT_GT(refsBeforeRelease, baselineRefs);

    returnDeviceRemoved.store(true);
    eyeCopy.Release();
    EXPECT_EQ(deviceRefCount(), refsBeforeRelease);
}

}
