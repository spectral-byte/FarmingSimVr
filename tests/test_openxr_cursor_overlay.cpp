#include <gtest/gtest.h>
#include "openxr/openxr_cursor_overlay.h"
#include "core/raii_utils.h"
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
        (void)createInfo;
        createSwapchainCalls++;
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
        if (imageCountOutput) *imageCountOutput = 2;
        if (imageCapacityInput == 0) return XR_SUCCESS;
        uint32_t count = std::min(imageCapacityInput, 2u);
        for (uint32_t i = 0; i < count; ++i) {
            auto d3d12Img = reinterpret_cast<XrSwapchainImageD3D12KHR*>(&images[i]);
            d3d12Img->type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
            d3d12Img->texture = nullptr;
        }
        return XR_SUCCESS;
    }

    XrResult AcquireSwapchainImage(XrSwapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index) override {
        acquireImageCalls++;
        if (index) *index = 0;
        return XR_SUCCESS;
    }

    XrResult WaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo*) override {
        waitImageCalls++;
        return XR_SUCCESS;
    }

    XrResult ReleaseSwapchainImage(XrSwapchain, const XrSwapchainImageReleaseInfo*) override {
        releaseImageCalls++;
        return XR_SUCCESS;
    }

    XrResult BeginSession(XrSession, const XrSessionBeginInfo*) override { return XR_SUCCESS; }
    XrResult EndSession(XrSession) override { return XR_SUCCESS; }
    XrResult WaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState*) override { return XR_SUCCESS; }
    XrResult BeginFrame(XrSession, const XrFrameBeginInfo*) override { return XR_SUCCESS; }
    XrResult EndFrame(XrSession, const XrFrameEndInfo*) override { return XR_SUCCESS; }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// ScopedEventHandle Tests
// ---------------------------------------------------------------------------

TEST(ScopedEventHandleTest, ManagesHandleLifecycleAndClosesAutomatically) {
    HANDLE rawHandle = nullptr;
    {
        ScopedEventHandle eventHandle = ScopedEventHandle::Create(nullptr, FALSE, FALSE, nullptr);
        EXPECT_TRUE(eventHandle.IsValid());
        EXPECT_NE(eventHandle.Get(), nullptr);
        rawHandle = eventHandle.Get();

        // Verify handle is a valid waitable object
        DWORD waitRes = WaitForSingleObject(rawHandle, 0);
        EXPECT_EQ(waitRes, WAIT_TIMEOUT);
    }
    // After destruction, rawHandle must be closed.
    // Calling WaitForSingleObject on closed handle returns WAIT_FAILED.
    DWORD waitAfter = WaitForSingleObject(rawHandle, 0);
    EXPECT_EQ(waitAfter, WAIT_FAILED);
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

    // Double close should be a no-op
    handle.Close();
    EXPECT_FALSE(handle.IsValid());

    HANDLE newRaw = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    handle.Reset(newRaw);
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.Get(), newRaw);
}

// ---------------------------------------------------------------------------
// OpenXrCursorOverlay Pixel Generation Tests
// ---------------------------------------------------------------------------

TEST(OpenXrCursorOverlayTest, GenerateCursorPixelsCreatesCorrectArrowPattern) {
    uint32_t width = 32;
    uint32_t height = 32;
    auto pixels = OpenXrCursorOverlay::GenerateCursorPixels(width, height);
    ASSERT_EQ(pixels.size(), width * height);

    // Top-left pixel (0, 0) is the tip 'X' -> Black outline (0xFF000000)
    EXPECT_EQ(pixels[0], 0xFF000000);

    // (1, 0) in row 0 is space ' ' -> Transparent (0x00000000)
    EXPECT_EQ(pixels[1], 0x00000000);

    // Row 2 is "X.X" -> (0, 2) is 'X', (1, 2) is '.', (2, 2) is 'X'
    EXPECT_EQ(pixels[2 * width + 0], 0xFF000000);
    EXPECT_EQ(pixels[2 * width + 1], 0xFFFFFFFF); // White interior
    EXPECT_EQ(pixels[2 * width + 2], 0xFF000000);

    // Bottom area is empty space
    EXPECT_EQ(pixels[31 * width + 31], 0x00000000);
}

TEST(OpenXrCursorOverlayTest, GenerateCursorPixelsScalesToArbitraryResolution) {
    uint32_t width = 64;
    uint32_t height = 64;
    auto pixels = OpenXrCursorOverlay::GenerateCursorPixels(width, height);
    ASSERT_EQ(pixels.size(), width * height);

    // (0, 0) must still be black outline
    EXPECT_EQ(pixels[0], 0xFF000000);

    // White interior region should be present
    bool hasWhite = false;
    for (uint32_t p : pixels) {
        if (p == 0xFFFFFFFF) {
            hasWhite = true;
            break;
        }
    }
    EXPECT_TRUE(hasWhite);
}

// ---------------------------------------------------------------------------
// Cursor Quad Pose Calculation Tests
// ---------------------------------------------------------------------------

TEST(OpenXrCursorOverlayTest, ComputeCursorQuadPoseCalculatesCorrectOffset) {
    XrPosef menuPose{};
    menuPose.position = { 0.0f, 1.5f, -1.5f };
    menuPose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

    XrExtent2Df menuSize{ 1.6f, 0.9f };
    float cursorSize = 0.035f;

    // Center cursor (0.5, 0.5)
    XrPosef center = OpenXrCursorOverlay::ComputeCursorQuadPose(menuPose, menuSize, 0.5f, 0.5f, cursorSize, cursorSize);
    EXPECT_NEAR(center.position.x, 0.0f + 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(center.position.y, 1.5f - 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(center.position.z, -1.5f + 0.002f, 0.001f);
    EXPECT_FLOAT_EQ(center.orientation.w, 1.0f);

    // Top-left cursor (0.0, 0.0) -> tip at (-0.8, 1.95)
    XrPosef tl = OpenXrCursorOverlay::ComputeCursorQuadPose(menuPose, menuSize, 0.0f, 0.0f, cursorSize, cursorSize);
    EXPECT_NEAR(tl.position.x, -0.8f + 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(tl.position.y, 1.95f - 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(tl.position.z, -1.5f + 0.002f, 0.001f);

    // Bottom-right cursor (1.0, 1.0) -> tip at (0.8, 1.05)
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

// ---------------------------------------------------------------------------
// Swapchain Lifecycle and Composition Layer Tests
// ---------------------------------------------------------------------------

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

    // Without swapchain created
    MenuCursorState visibleState{ true, 0.5f, 0.5f, MenuCursorState::kDefaultCursorSize };
    EXPECT_FALSE(overlay.PrepareCompositionLayer(menuQuad, visibleState, mockLocalSpace, outCursorQuad,
                                                 nullptr, nullptr, mockDispatch));

    // With swapchain, but visible = false
    XrSession mockSession = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xBEEF));
    ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));

    MenuCursorState hiddenState{ false, 0.5f, 0.5f, MenuCursorState::kDefaultCursorSize };
    EXPECT_FALSE(overlay.PrepareCompositionLayer(menuQuad, hiddenState, mockLocalSpace, outCursorQuad,
                                                 nullptr, nullptr, mockDispatch));
    EXPECT_FALSE(overlay.HasLastSubmittedCursorQuadLayer());
}

} // namespace fs25vr
