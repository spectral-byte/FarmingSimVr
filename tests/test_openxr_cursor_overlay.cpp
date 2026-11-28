#include <gtest/gtest.h>
#include "openxr/openxr_cursor_overlay.h"
#include "core/raii_utils.h"
#include <dxgi1_4.h>
#include <cmath>
#include <unordered_set>

namespace fs25vr {

namespace {

class MockCursorDispatch : public IOpenXrDispatch {
public:
    std::vector<int64_t> mockSwapchainFormats = {
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_R8G8B8A8_UNORM
    };

    uint32_t createSwapchainCalls = 0;
    uint32_t destroySwapchainCalls = 0;
    uint32_t acquireImageCalls = 0;
    uint32_t waitImageCalls = 0;
    uint32_t releaseImageCalls = 0;

    XrResult createSwapchainResult = XR_SUCCESS;
    XrResult enumerateImagesResult = XR_SUCCESS;
    XrResult acquireImageResult = XR_SUCCESS;
    XrResult waitImageResult = XR_SUCCESS;
    XrResult releaseImageResult = XR_SUCCESS;
    uint32_t acquireIndex = 0;
    XrSwapchainCreateInfo lastCreateInfo{};
    std::vector<ID3D12Resource*> swapchainTextures{ nullptr, nullptr };

    XrResult EnumerateInstanceExtensionProperties(const char*, uint32_t, uint32_t*, XrExtensionProperties*) override { return XR_SUCCESS; }
    XrResult CreateInstance(const XrInstanceCreateInfo*, XrInstance*) override { return XR_SUCCESS; }
    XrResult DestroyInstance(XrInstance) override { return XR_SUCCESS; }
    XrResult GetInstanceProperties(XrInstance, XrInstanceProperties*) override { return XR_SUCCESS; }
    XrResult GetSystem(XrInstance, const XrSystemGetInfo*, XrSystemId*) override { return XR_SUCCESS; }
    XrResult GetSystemProperties(XrInstance, XrSystemId, XrSystemProperties*) override { return XR_SUCCESS; }
    XrResult EnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t, uint32_t* viewCountOutput, XrViewConfigurationView*) override {
        if (viewCountOutput) *viewCountOutput = 2;
        return XR_SUCCESS;
    }
    XrResult GetD3D12GraphicsRequirementsKHR(XrInstance, XrSystemId, XrGraphicsRequirementsD3D12KHR*) override { return XR_SUCCESS; }
    XrResult CreateSession(XrInstance, const XrSessionCreateInfo*, XrSession*) override { return XR_SUCCESS; }
    XrResult DestroySession(XrSession) override { return XR_SUCCESS; }
    XrResult CreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo*, XrSpace*) override { return XR_SUCCESS; }
    XrResult DestroySpace(XrSpace) override { return XR_SUCCESS; }
    XrResult LocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation*) override { return XR_SUCCESS; }
    XrResult LocateViews(XrSession, const XrViewLocateInfo*, XrViewState*, uint32_t, uint32_t*, XrView*) override { return XR_SUCCESS; }

    XrResult EnumerateSwapchainFormats(
        XrSession,
        uint32_t formatCapacityInput,
        uint32_t* formatCountOutput,
        int64_t* formats) override {
        if (formatCountOutput) *formatCountOutput = static_cast<uint32_t>(mockSwapchainFormats.size());
        if (formatCapacityInput == 0) return XR_SUCCESS;
        uint32_t count = (formatCapacityInput < mockSwapchainFormats.size())
            ? formatCapacityInput : static_cast<uint32_t>(mockSwapchainFormats.size());
        for (uint32_t i = 0; i < count; ++i) {
            formats[i] = mockSwapchainFormats[i];
        }
        return XR_SUCCESS;
    }

    XrResult CreateSwapchain(
        XrSession,
        const XrSwapchainCreateInfo* createInfo,
        XrSwapchain* swapchain) override {
        createSwapchainCalls++;
        if (createInfo) {
            lastCreateInfo = *createInfo;
        }
        if (createSwapchainResult != XR_SUCCESS) {
            return createSwapchainResult;
        }
        if (swapchain) {
            *swapchain = reinterpret_cast<XrSwapchain>(static_cast<uintptr_t>(0xC0015000 + createSwapchainCalls));
        }
        return XR_SUCCESS;
    }

    XrResult DestroySwapchain(XrSwapchain) override {
        destroySwapchainCalls++;
        return XR_SUCCESS;
    }

    XrResult EnumerateSwapchainImages(
        XrSwapchain,
        uint32_t imageCapacityInput,
        uint32_t* imageCountOutput,
        XrSwapchainImageBaseHeader* images) override {
        if (enumerateImagesResult != XR_SUCCESS) {
            return enumerateImagesResult;
        }
        uint32_t total = static_cast<uint32_t>(swapchainTextures.size());
        if (imageCountOutput) *imageCountOutput = total;
        if (imageCapacityInput == 0) return XR_SUCCESS;
        uint32_t count = std::min(imageCapacityInput, total);
        auto* d3d12Images = reinterpret_cast<XrSwapchainImageD3D12KHR*>(images);
        for (uint32_t i = 0; i < count; ++i) {
            d3d12Images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
            d3d12Images[i].next = nullptr;
            d3d12Images[i].texture = swapchainTextures[i];
        }
        return XR_SUCCESS;
    }

    XrResult AcquireSwapchainImage(XrSwapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index) override {
        acquireImageCalls++;
        if (acquireImageResult != XR_SUCCESS) {
            return acquireImageResult;
        }
        if (index) *index = acquireIndex;
        return XR_SUCCESS;
    }

    XrResult WaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo*) override {
        waitImageCalls++;
        return waitImageResult;
    }

    XrResult ReleaseSwapchainImage(XrSwapchain, const XrSwapchainImageReleaseInfo*) override {
        releaseImageCalls++;
        return releaseImageResult;
    }

    XrResult BeginSession(XrSession, const XrSessionBeginInfo*) override { return XR_SUCCESS; }
    XrResult EndSession(XrSession) override { return XR_SUCCESS; }
    XrResult WaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState*) override { return XR_SUCCESS; }
    XrResult BeginFrame(XrSession, const XrFrameBeginInfo*) override { return XR_SUCCESS; }
    XrResult EndFrame(XrSession, const XrFrameEndInfo*) override { return XR_SUCCESS; }
};

}

TEST(ScopedEventHandleTest, ManagesHandleLifecycleAndClosesAutomatically) {
    std::string eventName = "fs25vr_test_scoped_event_" + std::to_string(GetCurrentProcessId()) +
                            "_" + std::to_string(GetCurrentThreadId());
    {
        ScopedEventHandle eventHandle = ScopedEventHandle::Create(nullptr, FALSE, FALSE, eventName.c_str());
        ASSERT_TRUE(eventHandle.IsValid());
        ASSERT_NE(eventHandle.Get(), nullptr);

        EXPECT_EQ(WaitForSingleObject(eventHandle.Get(), 0), WAIT_TIMEOUT);

        HANDLE reopened = OpenEventA(SYNCHRONIZE, FALSE, eventName.c_str());
        ASSERT_NE(reopened, nullptr);
        EXPECT_EQ(WaitForSingleObject(reopened, 0), WAIT_TIMEOUT);
        CloseHandle(reopened);

        HANDLE duplicate = nullptr;
        ASSERT_TRUE(DuplicateHandle(GetCurrentProcess(), eventHandle.Get(), GetCurrentProcess(),
                                    &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS));
        EXPECT_EQ(WaitForSingleObject(duplicate, 0), WAIT_TIMEOUT);
        SetEvent(duplicate);
        EXPECT_EQ(WaitForSingleObject(eventHandle.Get(), 0), WAIT_OBJECT_0);
        CloseHandle(duplicate);
    }
    SetLastError(ERROR_SUCCESS);
    HANDLE afterClose = OpenEventA(SYNCHRONIZE, FALSE, eventName.c_str());
    EXPECT_EQ(afterClose, nullptr);
    EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
}

TEST(ScopedEventHandleTest, SupportsMoveSemanticsWithoutDuplicateClose) {
    ScopedEventHandle original = ScopedEventHandle::Create(nullptr, FALSE, FALSE, nullptr);
    EXPECT_TRUE(original.IsValid());
    HANDLE raw = original.Get();

    ScopedEventHandle moved(std::move(original));
    EXPECT_FALSE(original.IsValid());
    EXPECT_EQ(original.Get(), nullptr);
    EXPECT_TRUE(moved.IsValid());
    EXPECT_EQ(moved.Get(), raw);

    ScopedEventHandle target;
    target = std::move(moved);
    EXPECT_FALSE(moved.IsValid());
    EXPECT_TRUE(target.IsValid());
    EXPECT_EQ(target.Get(), raw);
}

TEST(ScopedEventHandleTest, ResetAndCloseBehaveSafely) {
    ScopedEventHandle handle = ScopedEventHandle::Create(nullptr, FALSE, FALSE, nullptr);
    EXPECT_TRUE(handle.IsValid());
    handle.Close();
    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(handle.Get(), nullptr);

    handle.Close();
    EXPECT_FALSE(handle.IsValid());

    HANDLE newRaw = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    handle.Reset(newRaw);
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.Get(), newRaw);
}

TEST(OpenXrCursorOverlayTest, GenerateCursorPixelsCreatesCorrectArrowPattern) {
    uint32_t width = 32;
    uint32_t height = 32;
    auto pixels = OpenXrCursorOverlay::GenerateCursorPixels(width, height);
    ASSERT_EQ(pixels.size(), width * height);

    EXPECT_EQ(pixels[0], 0xFF000000);

    EXPECT_EQ(pixels[1], 0x00000000);

    EXPECT_EQ(pixels[2 * width + 0], 0xFF000000);
    EXPECT_EQ(pixels[2 * width + 1], 0xFFFFFFFF);
    EXPECT_EQ(pixels[2 * width + 2], 0xFF000000);

    EXPECT_EQ(pixels[31 * width + 31], 0x00000000);
}

TEST(OpenXrCursorOverlayTest, GenerateCursorPixelsScalesToArbitraryResolution) {
    uint32_t width = 64;
    uint32_t height = 64;
    auto pixels = OpenXrCursorOverlay::GenerateCursorPixels(width, height);
    ASSERT_EQ(pixels.size(), width * height);

    EXPECT_EQ(pixels[0], 0xFF000000);

    bool hasWhite = false;
    for (uint32_t p : pixels) {
        if (p == 0xFFFFFFFF) {
            hasWhite = true;
            break;
        }
    }
    EXPECT_TRUE(hasWhite);
}

TEST(OpenXrCursorOverlayTest, ComputeCursorQuadPoseCalculatesCorrectOffset) {
    XrPosef menuPose{};
    menuPose.position = { 0.0f, 1.5f, -1.5f };
    menuPose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

    XrExtent2Df menuSize{ 1.6f, 0.9f };
    float cursorSize = 0.035f;

    XrPosef center = OpenXrCursorOverlay::ComputeCursorQuadPose(menuPose, menuSize, 0.5f, 0.5f, cursorSize, cursorSize);
    EXPECT_NEAR(center.position.x, 0.0f + 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(center.position.y, 1.5f - 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(center.position.z, -1.5f + 0.002f, 0.001f);
    EXPECT_FLOAT_EQ(center.orientation.w, 1.0f);

    XrPosef tl = OpenXrCursorOverlay::ComputeCursorQuadPose(menuPose, menuSize, 0.0f, 0.0f, cursorSize, cursorSize);
    EXPECT_NEAR(tl.position.x, -0.8f + 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(tl.position.y, 1.95f - 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(tl.position.z, -1.5f + 0.002f, 0.001f);

    XrPosef br = OpenXrCursorOverlay::ComputeCursorQuadPose(menuPose, menuSize, 1.0f, 1.0f, cursorSize, cursorSize);
    EXPECT_NEAR(br.position.x, 0.8f + 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(br.position.y, 1.05f - 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(br.position.z, -1.5f + 0.002f, 0.001f);
}

TEST(OpenXrCursorOverlayTest, ComputeCursorQuadPoseRotatesWithMenuOrientation) {
    XrPosef menuPose{};
    menuPose.position = { -1.5f, 1.5f, 0.0f };
    float halfAngle = 3.14159265f / 4.0f;
    menuPose.orientation = { 0.0f, std::sin(halfAngle), 0.0f, std::cos(halfAngle) };

    XrExtent2Df menuSize{ 1.6f, 0.9f };
    float cursorSize = 0.035f;

    XrPosef cursorPose = OpenXrCursorOverlay::ComputeCursorQuadPose(menuPose, menuSize, 0.5f, 0.5f, cursorSize, cursorSize);
    EXPECT_NEAR(cursorPose.orientation.y, menuPose.orientation.y, 0.001f);
    EXPECT_NEAR(cursorPose.orientation.w, menuPose.orientation.w, 0.001f);
    EXPECT_GT(cursorPose.position.x, menuPose.position.x);
}

TEST(OpenXrCursorOverlayTest, SwapchainCreationAndDestructionLifecycle) {
    auto mockDispatch = std::make_shared<MockCursorDispatch>();
    XrSession mockSession = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xBEEF));

    OpenXrCursorOverlay overlay;
    EXPECT_FALSE(overlay.HasSwapchain());

    ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));
    EXPECT_TRUE(overlay.HasSwapchain());
    EXPECT_EQ(mockDispatch->createSwapchainCalls, 1u);

    const auto& sc = overlay.GetSwapchain();
    EXPECT_EQ(sc.width, 64u);
    EXPECT_EQ(sc.height, 64u);
    EXPECT_EQ(sc.format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    EXPECT_NE(sc.handle, XR_NULL_HANDLE);

    overlay.DestroySwapchain(mockDispatch);
    EXPECT_FALSE(overlay.HasSwapchain());
    EXPECT_EQ(mockDispatch->destroySwapchainCalls, 1u);
}

TEST(OpenXrCursorOverlayTest, PrepareCompositionLayerBuildsValidLayerWhenVisible) {
    auto mockDispatch = std::make_shared<MockCursorDispatch>();
    XrSession mockSession = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xBEEF));

    OpenXrCursorOverlay overlay;
    ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));

    XrCompositionLayerQuad menuQuad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
    menuQuad.pose.position = { 0.0f, 1.5f, -1.5f };
    menuQuad.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
    menuQuad.size = { 1.6f, 0.9f };

    MenuCursorState cursorState{ true, 0.25f, 0.75f, MenuCursorState::kDefaultCursorSize };
    XrSpace mockLocalSpace = reinterpret_cast<XrSpace>(static_cast<uintptr_t>(0x1234));

    XrCompositionLayerQuad outCursorQuad{};
    bool prepared = overlay.PrepareCompositionLayer(menuQuad, cursorState, mockLocalSpace, outCursorQuad,
                                                    nullptr, nullptr, mockDispatch);
    EXPECT_TRUE(prepared);
    EXPECT_EQ(outCursorQuad.type, XR_TYPE_COMPOSITION_LAYER_QUAD);
    EXPECT_EQ(outCursorQuad.space, mockLocalSpace);
    EXPECT_EQ(outCursorQuad.eyeVisibility, XR_EYE_VISIBILITY_BOTH);
    EXPECT_EQ(outCursorQuad.layerFlags, XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
    EXPECT_EQ(outCursorQuad.subImage.swapchain, overlay.GetSwapchain().handle);
    EXPECT_FLOAT_EQ(outCursorQuad.size.width, MenuCursorState::kDefaultCursorSize);
    EXPECT_FLOAT_EQ(outCursorQuad.size.height, MenuCursorState::kDefaultCursorSize);

    EXPECT_TRUE(overlay.HasLastSubmittedCursorQuadLayer());
    EXPECT_EQ(overlay.GetLastSubmittedCursorQuadLayer().subImage.swapchain, overlay.GetSwapchain().handle);
}

TEST(OpenXrCursorOverlayTest, PrepareCompositionLayerReturnsFalseWhenHiddenOrNoSwapchain) {
    auto mockDispatch = std::make_shared<MockCursorDispatch>();
    OpenXrCursorOverlay overlay;

    XrCompositionLayerQuad menuQuad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
    menuQuad.pose.position = { 0.0f, 1.5f, -1.5f };
    menuQuad.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
    menuQuad.size = { 1.6f, 0.9f };

    XrSpace mockLocalSpace = reinterpret_cast<XrSpace>(static_cast<uintptr_t>(0x1234));
    XrCompositionLayerQuad outCursorQuad{};

    MenuCursorState visibleState{ true, 0.5f, 0.5f, MenuCursorState::kDefaultCursorSize };
    EXPECT_FALSE(overlay.PrepareCompositionLayer(menuQuad, visibleState, mockLocalSpace, outCursorQuad,
                                                 nullptr, nullptr, mockDispatch));

    XrSession mockSession = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xBEEF));
    ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));

    MenuCursorState hiddenState{ false, 0.5f, 0.5f, MenuCursorState::kDefaultCursorSize };
    EXPECT_FALSE(overlay.PrepareCompositionLayer(menuQuad, hiddenState, mockLocalSpace, outCursorQuad,
                                                 nullptr, nullptr, mockDispatch));
    EXPECT_FALSE(overlay.HasLastSubmittedCursorQuadLayer());
}

TEST(OpenXrCursorOverlayTest, CreateSwapchainRequestsStaticImageBit) {
    auto mockDispatch = std::make_shared<MockCursorDispatch>();
    XrSession mockSession = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xBEEF));

    OpenXrCursorOverlay overlay;
    ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));
    EXPECT_NE(mockDispatch->lastCreateInfo.createFlags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT, 0u);
}

TEST(OpenXrCursorOverlayTest, UploadTextureReleasesImageOnWaitFailureAndAllowsRetry) {
    auto mockDispatch = std::make_shared<MockCursorDispatch>();
    XrSession mockSession = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xBEEF));
    OpenXrCursorOverlay overlay;
    ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));

    auto* dummyDevice = reinterpret_cast<ID3D12Device*>(static_cast<uintptr_t>(0x1000));
    auto* dummyQueue = reinterpret_cast<ID3D12CommandQueue*>(static_cast<uintptr_t>(0x2000));

    mockDispatch->waitImageResult = XR_ERROR_RUNTIME_FAILURE;
    EXPECT_FALSE(overlay.UploadTexture(dummyDevice, dummyQueue, mockDispatch));
    EXPECT_EQ(mockDispatch->acquireImageCalls, 1u);
    EXPECT_EQ(mockDispatch->waitImageCalls, 1u);
    EXPECT_EQ(mockDispatch->releaseImageCalls, 1u);

    mockDispatch->waitImageResult = XR_SUCCESS;
    mockDispatch->acquireIndex = 99;
    EXPECT_FALSE(overlay.UploadTexture(dummyDevice, dummyQueue, mockDispatch));
    EXPECT_EQ(mockDispatch->acquireImageCalls, 2u);
    EXPECT_EQ(mockDispatch->releaseImageCalls, 2u);

    mockDispatch->acquireIndex = 0;
    EXPECT_FALSE(overlay.UploadTexture(dummyDevice, dummyQueue, mockDispatch));
    EXPECT_EQ(mockDispatch->acquireImageCalls, 3u);
    EXPECT_EQ(mockDispatch->releaseImageCalls, 3u);
}

TEST(OpenXrCursorOverlayTest, UploadTextureSucceedsOnceOnWarpDeviceAndLeavesUnuploadedOnD3D12Failure) {
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
        FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        GTEST_SKIP() << "no WARP D3D12 device";
    }
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                          D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                          IID_PPV_ARGS(&texture))));

    auto mockDispatch = std::make_shared<MockCursorDispatch>();
    mockDispatch->swapchainTextures = { texture.Get(), texture.Get() };
    XrSession mockSession = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xBEEF));

    {
        OpenXrCursorOverlay overlay;
        mockDispatch->releaseImageResult = XR_ERROR_RUNTIME_FAILURE;
        ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));
        EXPECT_FALSE(overlay.UploadTexture(device.Get(), queue.Get(), mockDispatch));
        EXPECT_EQ(mockDispatch->acquireImageCalls, 1u);

        mockDispatch->releaseImageResult = XR_SUCCESS;
        EXPECT_TRUE(overlay.UploadTexture(device.Get(), queue.Get(), mockDispatch));
        EXPECT_EQ(mockDispatch->acquireImageCalls, 2u);

        EXPECT_TRUE(overlay.UploadTexture(device.Get(), queue.Get(), mockDispatch));
        EXPECT_EQ(mockDispatch->acquireImageCalls, 2u);
    }

    Microsoft::WRL::ComPtr<ID3D12Device5> device5;
    if (SUCCEEDED(device.As(&device5))) {
        device5->RemoveDevice();
        OpenXrCursorOverlay failedOverlay;
        ASSERT_TRUE(failedOverlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));
        EXPECT_FALSE(failedOverlay.UploadTexture(device.Get(), queue.Get(), mockDispatch));
        EXPECT_EQ(mockDispatch->acquireImageCalls, 3u);
        EXPECT_EQ(mockDispatch->releaseImageCalls, 3u);
        EXPECT_FALSE(failedOverlay.UploadTexture(device.Get(), queue.Get(), mockDispatch));
        EXPECT_EQ(mockDispatch->acquireImageCalls, 4u);
    }
}

}
