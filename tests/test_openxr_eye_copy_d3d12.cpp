#include <gtest/gtest.h>
#include "openxr/openxr_manager.h"
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

// The fake OpenXR runtime with eye swapchain images that are real textures on a D3D12 device, in the
// state the OpenXR D3D12 extension hands them out in (RENDER_TARGET), so the eye copy runs against real
// resource and command list rules.
class FakeD3D12OpenXrRuntime : public FakeOpenXrRuntime {
public:
    ID3D12Device* device = nullptr;
    // The next acquired image has this index. Past the end of the images, the copy finds no destination texture.
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
            // The caller's array holds XrSwapchainImageD3D12KHR elements, so step by that size, not the base header's.
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

// A WARP device with the D3D12 debug layer on, a direct queue and a back buffer in the PRESENT state, as
// the game hands it to Present. The debug layer is process-wide for devices created after it is enabled,
// so every test here makes its own device and nothing else in the suite depends on the layer being off.
class WarpDebugDevice {
public:
    // Empty when the device is usable; otherwise why not (the debug layer is not installed, ...).
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

    // Blocks until everything submitted to the queue has finished on the GPU.
    void WaitForQueue() {
        ComPtr<ID3D12Fence> fence;
        device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        queue->Signal(fence.Get(), 1);
        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
    }

    // Every error the debug layer stored, as text.
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

}  // namespace

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
    {
        OpenXrManager manager(runtime);
        ASSERT_TRUE(manager.Initialize());
        ASSERT_TRUE(manager.CreateSession(gpu.device.Get(), gpu.queue.Get()));
        ASSERT_TRUE(manager.CreateTrackingSpaces());
        manager.PollEvents();
        ASSERT_TRUE(manager.CreateEyeSwapchains(kWidth, kHeight, DXGI_FORMAT_R8G8B8A8_UNORM));

        // More frames than the copy ring has slots (two), so every slot is reused.
        const uint32_t frames = 6;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            ASSERT_TRUE(manager.CopyBackBufferToEye(0, backBuffer.Get())) << "frame " << frame;
            ASSERT_TRUE(manager.CopyBackBufferToEye(1, backBuffer.Get())) << "frame " << frame;
            ASSERT_TRUE(manager.SubmitFramePair()) << "frame " << frame;
        }

        // A resolution change or a headset sleep recreates the eye swapchains, here at another size.
        manager.DestroyEyeSwapchains();
        ASSERT_TRUE(manager.CreateEyeSwapchains(kWidth / 2, kHeight / 2, DXGI_FORMAT_R8G8B8A8_UNORM));
        for (uint32_t frame = 0; frame < frames; ++frame) {
            ASSERT_TRUE(manager.CopyBackBufferToEye(0, backBuffer.Get())) << "frame " << frame;
            ASSERT_TRUE(manager.CopyBackBufferToEye(1, backBuffer.Get())) << "frame " << frame;
            ASSERT_TRUE(manager.SubmitFramePair()) << "frame " << frame;
        }

        // A copy that finds no destination texture records nothing, and the next copy still works.
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

}  // namespace fs25vr
