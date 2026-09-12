#include <gtest/gtest.h>
#include "openxr/openxr_manager.h"
#include <cstring>

namespace fs25vr {

class MockOpenXrDispatch : public IOpenXrDispatch {
public:
    XrResult enumerateExtensionResult = XR_SUCCESS;
    std::vector<std::string> availableExtensions;

    XrResult createInstanceResult = XR_SUCCESS;
    XrResult destroyInstanceResult = XR_SUCCESS;
    XrResult getInstancePropertiesResult = XR_SUCCESS;
    XrResult getSystemResult = XR_SUCCESS;
    XrResult getSystemPropertiesResult = XR_SUCCESS;

    std::string mockRuntimeName = "MockVR Runtime";
    XrVersion mockRuntimeVersion = XR_MAKE_VERSION(1, 0, 30);
    XrSystemId mockSystemId = 12345;
    std::string mockSystemName = "Mock HMD Device";

    bool createInstanceCalled = false;
    bool destroyInstanceCalled = false;

    XrResult EnumerateInstanceExtensionProperties(
        const char* /*layerName*/,
        uint32_t propertyCapacityInput,
        uint32_t* propertyCountOutput,
        XrExtensionProperties* properties) override {
        if (enumerateExtensionResult != XR_SUCCESS) {
            return enumerateExtensionResult;
        }

        if (propertyCountOutput) {
            *propertyCountOutput = static_cast<uint32_t>(availableExtensions.size());
        }

        if (propertyCapacityInput == 0) {
            return XR_SUCCESS;
        }

        uint32_t countToCopy = (propertyCapacityInput < availableExtensions.size())
                                   ? propertyCapacityInput
                                   : static_cast<uint32_t>(availableExtensions.size());

        for (uint32_t i = 0; i < countToCopy; ++i) {
            properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
            properties[i].next = nullptr;
            strncpy_s(properties[i].extensionName, availableExtensions[i].c_str(), XR_MAX_EXTENSION_NAME_SIZE - 1);
            properties[i].extensionVersion = 1;
        }

        return XR_SUCCESS;
    }

    XrResult CreateInstance(
        const XrInstanceCreateInfo* /*createInfo*/,
        XrInstance* instance) override {
        createInstanceCalled = true;
        if (createInstanceResult != XR_SUCCESS) {
            return createInstanceResult;
        }
        if (instance) {
            *instance = reinterpret_cast<XrInstance>(static_cast<uintptr_t>(0xDEADBEEF));
        }
        return XR_SUCCESS;
    }

    XrResult DestroyInstance(
        XrInstance /*instance*/) override {
        destroyInstanceCalled = true;
        return destroyInstanceResult;
    }

    XrResult GetInstanceProperties(
        XrInstance /*instance*/,
        XrInstanceProperties* instanceProperties) override {
        if (getInstancePropertiesResult != XR_SUCCESS) {
            return getInstancePropertiesResult;
        }
        if (instanceProperties) {
            strncpy_s(instanceProperties->runtimeName, mockRuntimeName.c_str(), XR_MAX_RUNTIME_NAME_SIZE - 1);
            instanceProperties->runtimeVersion = mockRuntimeVersion;
        }
        return XR_SUCCESS;
    }

    XrResult GetSystem(
        XrInstance /*instance*/,
        const XrSystemGetInfo* getInfo,
        XrSystemId* systemId) override {
        if (getSystemResult != XR_SUCCESS) {
            return getSystemResult;
        }
        if (getInfo->formFactor == XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY && systemId) {
            *systemId = mockSystemId;
            return XR_SUCCESS;
        }
        return XR_ERROR_FORM_FACTOR_UNAVAILABLE;
    }

    XrResult getD3D12RequirementsResult = XR_SUCCESS;
    D3D_FEATURE_LEVEL mockMinFeatureLevel = D3D_FEATURE_LEVEL_12_0;

    XrResult GetSystemProperties(
        XrInstance /*instance*/,
        XrSystemId /*systemId*/,
        XrSystemProperties* properties) override {
        if (getSystemPropertiesResult != XR_SUCCESS) {
            return getSystemPropertiesResult;
        }
        if (properties) {
            strncpy_s(properties->systemName, mockSystemName.c_str(), XR_MAX_SYSTEM_NAME_SIZE - 1);
            properties->systemId = mockSystemId;
            properties->graphicsProperties.maxSwapchainImageWidth = 4096;
            properties->graphicsProperties.maxSwapchainImageHeight = 4096;
            properties->graphicsProperties.maxLayerCount = 16;
            properties->trackingProperties.orientationTracking = XR_TRUE;
            properties->trackingProperties.positionTracking = XR_TRUE;
        }
        return XR_SUCCESS;
    }

    XrResult GetD3D12GraphicsRequirementsKHR(
        XrInstance /*instance*/,
        XrSystemId /*systemId*/,
        XrGraphicsRequirementsD3D12KHR* graphicsRequirements) override {
        if (getD3D12RequirementsResult != XR_SUCCESS) {
            return getD3D12RequirementsResult;
        }
        if (graphicsRequirements) {
            graphicsRequirements->minFeatureLevel = mockMinFeatureLevel;
            graphicsRequirements->adapterLuid = { 0x1234, 0x5678 };
        }
        return XR_SUCCESS;
    }

    XrResult createSessionResult = XR_SUCCESS;
    XrResult destroySessionResult = XR_SUCCESS;
    XrResult createReferenceSpaceResult = XR_SUCCESS;
    XrResult destroySpaceResult = XR_SUCCESS;
    XrResult locateSpaceResult = XR_SUCCESS;

    bool createSessionCalled = false;
    bool destroySessionCalled = false;
    XrSpaceLocation mockSpaceLocation{
        XR_TYPE_SPACE_LOCATION,
        nullptr,
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT,
        { {0.0f, 0.0f, 0.0f, 1.0f}, {0.1f, 0.2f, -0.3f} }
    };

    XrResult CreateSession(
        XrInstance /*instance*/,
        const XrSessionCreateInfo* /*createInfo*/,
        XrSession* session) override {
        createSessionCalled = true;
        if (createSessionResult != XR_SUCCESS) return createSessionResult;
        if (session) *session = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xCAFE0001));
        return XR_SUCCESS;
    }

    XrResult DestroySession(XrSession /*session*/) override {
        destroySessionCalled = true;
        return destroySessionResult;
    }

    XrResult CreateReferenceSpace(
        XrSession /*session*/,
        const XrReferenceSpaceCreateInfo* /*createInfo*/,
        XrSpace* space) override {
        if (createReferenceSpaceResult != XR_SUCCESS) return createReferenceSpaceResult;
        static uintptr_t s_spaceHandle = 0x50ACE001;
        if (space) *space = reinterpret_cast<XrSpace>(s_spaceHandle++);
        return XR_SUCCESS;
    }

    XrResult DestroySpace(XrSpace /*space*/) override {
        return destroySpaceResult;
    }

    XrResult LocateSpace(
        XrSpace /*space*/,
        XrSpace /*baseSpace*/,
        XrTime /*time*/,
        XrSpaceLocation* location) override {
        if (locateSpaceResult != XR_SUCCESS) return locateSpaceResult;
        if (location) {
            *location = mockSpaceLocation;
        }
        return XR_SUCCESS;
    }

    XrResult locateViewsResult = XR_SUCCESS;
    XrViewState mockViewState{
        XR_TYPE_VIEW_STATE,
        nullptr,
        XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT |
        XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT
    };
    std::vector<XrView> mockViews;

    XrResult LocateViews(
        XrSession /*session*/,
        const XrViewLocateInfo* /*viewLocateInfo*/,
        XrViewState* viewState,
        uint32_t viewCapacityInput,
        uint32_t* viewCountOutput,
        XrView* views) override {
        if (locateViewsResult != XR_SUCCESS) return locateViewsResult;
        if (viewState) {
            *viewState = mockViewState;
        }
        if (viewCountOutput) {
            *viewCountOutput = static_cast<uint32_t>(mockViews.size());
        }
        if (viewCapacityInput == 0) {
            return XR_SUCCESS;
        }
        uint32_t count = (viewCapacityInput < mockViews.size()) ? viewCapacityInput : static_cast<uint32_t>(mockViews.size());
        for (uint32_t i = 0; i < count; ++i) {
            views[i] = mockViews[i];
        }
        return XR_SUCCESS;
    }

    XrResult enumerateSwapchainFormatsResult = XR_SUCCESS;
    std::vector<int64_t> mockSwapchainFormats = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM };

    XrResult createSwapchainResult = XR_SUCCESS;
    XrResult destroySwapchainResult = XR_SUCCESS;
    XrResult enumerateSwapchainImagesResult = XR_SUCCESS;
    XrResult acquireSwapchainImageResult = XR_SUCCESS;
    XrResult waitSwapchainImageResult = XR_SUCCESS;
    XrResult releaseSwapchainImageResult = XR_SUCCESS;
    XrResult waitFrameResult = XR_SUCCESS;
    XrResult beginFrameResult = XR_SUCCESS;
    XrResult endFrameResult = XR_SUCCESS;

    uint32_t createSwapchainCalls = 0;
    uint32_t destroySwapchainCalls = 0;
    uint32_t acquireSwapchainImageCalls = 0;
    uint32_t waitSwapchainImageCalls = 0;
    uint32_t releaseSwapchainImageCalls = 0;
    uint32_t waitFrameCalls = 0;
    uint32_t beginFrameCalls = 0;
    uint32_t endFrameCalls = 0;

    std::vector<XrSwapchainCreateInfo> lastSwapchainCreateInfos;
    XrFrameEndInfo lastFrameEndInfo{};
    std::vector<XrCompositionLayerProjectionView> lastSubmittedProjectionViews;

    XrResult EnumerateSwapchainFormats(
        XrSession /*session*/,
        uint32_t formatCapacityInput,
        uint32_t* formatCountOutput,
        int64_t* formats) override {
        if (enumerateSwapchainFormatsResult != XR_SUCCESS) return enumerateSwapchainFormatsResult;
        if (formatCountOutput) *formatCountOutput = static_cast<uint32_t>(mockSwapchainFormats.size());
        if (formatCapacityInput == 0) return XR_SUCCESS;
        uint32_t count = (formatCapacityInput < mockSwapchainFormats.size()) ? formatCapacityInput : static_cast<uint32_t>(mockSwapchainFormats.size());
        for (uint32_t i = 0; i < count; ++i) {
            formats[i] = mockSwapchainFormats[i];
        }
        return XR_SUCCESS;
    }

    XrResult CreateSwapchain(
        XrSession /*session*/,
        const XrSwapchainCreateInfo* createInfo,
        XrSwapchain* swapchain) override {
        createSwapchainCalls++;
        if (createSwapchainResult != XR_SUCCESS) return createSwapchainResult;
        if (createInfo) lastSwapchainCreateInfos.push_back(*createInfo);
        static uintptr_t s_swapchainHandle = 0x5A00;
        if (swapchain) *swapchain = reinterpret_cast<XrSwapchain>(s_swapchainHandle++);
        return XR_SUCCESS;
    }

    XrResult DestroySwapchain(XrSwapchain /*swapchain*/) override {
        destroySwapchainCalls++;
        return destroySwapchainResult;
    }

    XrResult EnumerateSwapchainImages(
        XrSwapchain /*swapchain*/,
        uint32_t imageCapacityInput,
        uint32_t* imageCountOutput,
        XrSwapchainImageBaseHeader* images) override {
        if (enumerateSwapchainImagesResult != XR_SUCCESS) return enumerateSwapchainImagesResult;
        if (imageCountOutput) *imageCountOutput = 2;
        if (imageCapacityInput == 0) return XR_SUCCESS;
        uint32_t count = (imageCapacityInput < 2) ? imageCapacityInput : 2;
        for (uint32_t i = 0; i < count; ++i) {
            auto* d3d12Img = reinterpret_cast<XrSwapchainImageD3D12KHR*>(&images[i]);
            d3d12Img->type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
            d3d12Img->next = nullptr;
            d3d12Img->texture = nullptr;
        }
        return XR_SUCCESS;
    }

    XrResult AcquireSwapchainImage(
        XrSwapchain /*swapchain*/,
        const XrSwapchainImageAcquireInfo* /*acquireInfo*/,
        uint32_t* index) override {
        acquireSwapchainImageCalls++;
        if (acquireSwapchainImageResult != XR_SUCCESS) return acquireSwapchainImageResult;
        if (index) *index = 0;
        return XR_SUCCESS;
    }

    XrResult WaitSwapchainImage(
        XrSwapchain /*swapchain*/,
        const XrSwapchainImageWaitInfo* /*waitInfo*/) override {
        waitSwapchainImageCalls++;
        return waitSwapchainImageResult;
    }

    XrResult ReleaseSwapchainImage(
        XrSwapchain /*swapchain*/,
        const XrSwapchainImageReleaseInfo* /*releaseInfo*/) override {
        releaseSwapchainImageCalls++;
        return releaseSwapchainImageResult;
    }

    XrResult WaitFrame(
        XrSession /*session*/,
        const XrFrameWaitInfo* /*frameWaitInfo*/,
        XrFrameState* frameState) override {
        waitFrameCalls++;
        if (waitFrameResult != XR_SUCCESS) return waitFrameResult;
        if (frameState) {
            frameState->type = XR_TYPE_FRAME_STATE;
            frameState->next = nullptr;
            frameState->predictedDisplayTime = 1000000;
            frameState->predictedDisplayPeriod = 11111111;
            frameState->shouldRender = XR_TRUE;
        }
        return XR_SUCCESS;
    }

    XrResult BeginFrame(
        XrSession /*session*/,
        const XrFrameBeginInfo* /*frameBeginInfo*/) override {
        beginFrameCalls++;
        return beginFrameResult;
    }

    XrResult EndFrame(
        XrSession /*session*/,
        const XrFrameEndInfo* frameEndInfo) override {
        endFrameCalls++;
        if (endFrameResult != XR_SUCCESS) return endFrameResult;
        if (frameEndInfo) {
            lastFrameEndInfo = *frameEndInfo;
            lastSubmittedProjectionViews.clear();
            if (frameEndInfo->layerCount > 0 && frameEndInfo->layers) {
                const auto* baseHeader = frameEndInfo->layers[0];
                if (baseHeader && baseHeader->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                    const auto* projLayer = reinterpret_cast<const XrCompositionLayerProjection*>(baseHeader);
                    if (projLayer && projLayer->views && projLayer->viewCount > 0) {
                        for (uint32_t v = 0; v < projLayer->viewCount; ++v) {
                            lastSubmittedProjectionViews.push_back(projLayer->views[v]);
                        }
                    }
                }
            }
        }
        return XR_SUCCESS;
    }
};

TEST(OpenXrManagerTest, EntersPassThroughWhenRuntimeUnavailable) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->createInstanceResult = XR_ERROR_RUNTIME_UNAVAILABLE;

    OpenXrManager manager(mockDispatch);
    EXPECT_TRUE(manager.Initialize());
    EXPECT_EQ(manager.GetState(), OpenXrState::PassThrough);
    EXPECT_TRUE(manager.IsPassThrough());
    EXPECT_FALSE(manager.IsActive());
}

TEST(OpenXrManagerTest, EntersPassThroughWhenD3D12ExtensionMissing) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { "XR_KHR_vulkan_enable", "XR_KHR_opengl_enable" };

    OpenXrManager manager(mockDispatch);
    EXPECT_TRUE(manager.Initialize());
    EXPECT_EQ(manager.GetState(), OpenXrState::PassThrough);
    EXPECT_TRUE(manager.IsPassThrough());
    EXPECT_FALSE(manager.IsActive());
    // CreateInstance should not even be called if required D3D12 extension is missing
    EXPECT_FALSE(mockDispatch->createInstanceCalled);
}

TEST(OpenXrManagerTest, EntersPassThroughWhenFormFactorUnavailable) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->getSystemResult = XR_ERROR_FORM_FACTOR_UNAVAILABLE;

    OpenXrManager manager(mockDispatch);
    EXPECT_TRUE(manager.Initialize());
    EXPECT_EQ(manager.GetState(), OpenXrState::PassThrough);
    EXPECT_TRUE(manager.IsPassThrough());
    EXPECT_FALSE(manager.IsActive());
    // Instance was created then destroyed upon entering Pass-Through Mode
    EXPECT_TRUE(mockDispatch->createInstanceCalled);
    EXPECT_TRUE(mockDispatch->destroyInstanceCalled);
}

TEST(OpenXrManagerTest, EntersPassThroughWhenD3D12RequirementsFails) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->getD3D12RequirementsResult = XR_ERROR_RUNTIME_FAILURE;

    OpenXrManager manager(mockDispatch);
    EXPECT_TRUE(manager.Initialize());
    EXPECT_EQ(manager.GetState(), OpenXrState::PassThrough);
    EXPECT_TRUE(manager.IsPassThrough());
    EXPECT_FALSE(manager.IsActive());
    EXPECT_TRUE(mockDispatch->createInstanceCalled);
    EXPECT_TRUE(mockDispatch->destroyInstanceCalled);
}

TEST(OpenXrManagerTest, InitializesActiveWhenHmdPresent) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    EXPECT_TRUE(manager.Initialize());
    EXPECT_EQ(manager.GetState(), OpenXrState::Active);
    EXPECT_FALSE(manager.IsPassThrough());
    EXPECT_TRUE(manager.IsActive());

    const auto& info = manager.GetRuntimeInfo();
    EXPECT_EQ(info.runtimeName, "MockVR Runtime");
    EXPECT_EQ(info.runtimeVersionMajor, 1);
    EXPECT_EQ(info.runtimeVersionMinor, 0);
    EXPECT_EQ(info.runtimeVersionPatch, 30);
    EXPECT_EQ(info.systemId, 12345);
    EXPECT_EQ(info.systemName, "Mock HMD Device");
    EXPECT_TRUE(info.d3d12ExtensionSupported);
    EXPECT_EQ(info.maxSwapchainImageWidth, 4096);
    EXPECT_EQ(info.maxSwapchainImageHeight, 4096);
    EXPECT_EQ(info.maxLayerCount, 16);
    EXPECT_TRUE(info.orientationTracking);
    EXPECT_TRUE(info.positionTracking);
    EXPECT_EQ(info.minFeatureLevel, D3D_FEATURE_LEVEL_12_0);

    manager.Shutdown();
    EXPECT_TRUE(mockDispatch->destroyInstanceCalled);
    EXPECT_EQ(manager.GetState(), OpenXrState::Uninitialized);
}

TEST(OpenXrManagerTest, PollsHeadPoseFromOpenXrSpaceAndCalculatesEulerAngles) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    float sinPitch = std::sin(0.5235988f * 0.5f);
    float cosPitch = std::cos(0.5235988f * 0.5f);
    mockDispatch->mockSpaceLocation.pose.position = { 0.1f, 0.2f, 0.3f };
    mockDispatch->mockSpaceLocation.pose.orientation = { sinPitch, 0.0f, 0.0f, cosPitch };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    HeadPose pose = manager.PollHeadPose();
    EXPECT_TRUE(pose.isValid);
    EXPECT_TRUE(pose.positionTracked);
    EXPECT_TRUE(pose.orientationTracked);
    EXPECT_NEAR(pose.x, 0.1f, 0.001f);
    EXPECT_NEAR(pose.y, 0.2f, 0.001f);
    EXPECT_NEAR(pose.z, 0.3f, 0.001f);
    EXPECT_NEAR(pose.pitch, 0.5235988f, 0.005f);
    EXPECT_NEAR(pose.yaw, 0.0f, 0.005f);
    EXPECT_NEAR(pose.roll, 0.0f, 0.005f);
}

TEST(OpenXrManagerTest, RecenteringZeroesTranslationalAndYawOffsets) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    float sinYaw = std::sin(0.3f * 0.5f);
    float cosYaw = std::cos(0.3f * 0.5f);
    mockDispatch->mockSpaceLocation.pose.position = { 0.5f, 1.2f, -0.4f };
    mockDispatch->mockSpaceLocation.pose.orientation = { 0.0f, sinYaw, 0.0f, cosYaw };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    HeadPose before = manager.PollHeadPose();
    EXPECT_NEAR(before.x, 0.5f, 0.001f);
    EXPECT_NEAR(before.y, 1.2f, 0.001f);
    EXPECT_NEAR(before.z, -0.4f, 0.001f);

    manager.Recenter();

    HeadPose after = manager.PollHeadPose();
    EXPECT_NEAR(after.x, 0.0f, 0.001f);
    EXPECT_NEAR(after.y, 0.0f, 0.001f);
    EXPECT_NEAR(after.z, 0.0f, 0.001f);
    EXPECT_NEAR(after.yaw, 0.0f, 0.005f);

    mockDispatch->mockSpaceLocation.pose.position = { 0.6f, 1.25f, -0.42f };
    HeadPose moved = manager.PollHeadPose();
    EXPECT_NEAR(moved.y, 0.05f, 0.005f);
}

TEST(OpenXrManagerTest, LocatesViewsAndExtractsAsymmetricFovTangents) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    XrView leftView{ XR_TYPE_VIEW, nullptr };
    leftView.fov = { -0.90f, 0.80f, 0.85f, -0.85f };
    leftView.pose.position = { -0.032f, 0.0f, 0.0f }; // -IPD/2
    leftView.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

    XrView rightView{ XR_TYPE_VIEW, nullptr };
    rightView.fov = { -0.80f, 0.90f, 0.85f, -0.85f };
    rightView.pose.position = { 0.032f, 0.0f, 0.0f }; // +IPD/2
    rightView.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

    mockDispatch->mockViews = { leftView, rightView };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    EyeProjection leftProj = manager.PollEyeProjection(0);
    EXPECT_TRUE(leftProj.isValid);
    EXPECT_NEAR(leftProj.angleLeft, -0.90f, 0.001f);
    EXPECT_NEAR(leftProj.angleRight, 0.80f, 0.001f);
    EXPECT_NEAR(leftProj.tanLeft, -1.26016f, 0.005f);
    EXPECT_NEAR(leftProj.tanRight, 1.02964f, 0.005f);
    EXPECT_NEAR(leftProj.xOffset, -0.23052f, 0.005f);
    EXPECT_NEAR(leftProj.yOffset, 0.0f, 0.005f);
    EXPECT_NEAR(leftProj.fovY, 1.70f, 0.01f);

    EyeProjection rightProj = manager.PollEyeProjection(1);
    EXPECT_TRUE(rightProj.isValid);
    EXPECT_NEAR(rightProj.angleLeft, -0.80f, 0.001f);
    EXPECT_NEAR(rightProj.angleRight, 0.90f, 0.001f);
    EXPECT_NEAR(rightProj.tanLeft, -1.02964f, 0.005f);
    EXPECT_NEAR(rightProj.tanRight, 1.26016f, 0.005f);
    EXPECT_NEAR(rightProj.xOffset, 0.23052f, 0.005f);
    EXPECT_NEAR(rightProj.yOffset, 0.0f, 0.005f);
    EXPECT_NEAR(rightProj.fovY, 1.70f, 0.01f);

    // Asymmetry verify: opposite signs for nasal/temporal convergence
    EXPECT_LT(leftProj.xOffset, 0.0f);
    EXPECT_GT(rightProj.xOffset, 0.0f);
    EXPECT_NEAR(leftProj.xOffset, -rightProj.xOffset, 0.001f);
}

TEST(OpenXrManagerTest, FallbackProjectionWhenPassThroughOrUninitialized) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = {}; // Force Pass-Through

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    EXPECT_TRUE(manager.IsPassThrough());

    EyeProjection proj = manager.PollEyeProjection(0);
    EXPECT_FALSE(proj.isValid);
    EXPECT_EQ(proj.xOffset, 0.0f);
    EXPECT_EQ(proj.yOffset, 0.0f);
}

TEST(OpenXrManagerTest, CreatesEyeSwapchainsAndEnumeratesImages) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    EXPECT_FALSE(manager.HasEyeSwapchains());
    EXPECT_TRUE(manager.CreateEyeSwapchains(2160, 2160, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));
    EXPECT_TRUE(manager.HasEyeSwapchains());
    EXPECT_EQ(mockDispatch->createSwapchainCalls, 2u);

    const auto& leftChain = manager.GetEyeSwapchain(0);
    EXPECT_EQ(leftChain.width, 2160u);
    EXPECT_EQ(leftChain.height, 2160u);
    EXPECT_EQ(leftChain.format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    EXPECT_EQ(leftChain.images.size(), 2u);

    const auto& rightChain = manager.GetEyeSwapchain(1);
    EXPECT_EQ(rightChain.width, 2160u);
    EXPECT_EQ(rightChain.height, 2160u);
    EXPECT_EQ(rightChain.format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    EXPECT_EQ(rightChain.images.size(), 2u);

    manager.DestroyEyeSwapchains();
    EXPECT_FALSE(manager.HasEyeSwapchains());
    EXPECT_EQ(mockDispatch->destroySwapchainCalls, 2u);
}

TEST(OpenXrManagerTest, CopiesBackBufferAndPerformsImageAcquireWaitRelease) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    EXPECT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    EXPECT_EQ(mockDispatch->acquireSwapchainImageCalls, 1u);
    EXPECT_EQ(mockDispatch->waitSwapchainImageCalls, 1u);
    EXPECT_EQ(mockDispatch->releaseSwapchainImageCalls, 1u);
    EXPECT_TRUE(manager.GetEyeSwapchain(0).hasCopiedFrame);

    EXPECT_TRUE(manager.CopyBackBufferToEye(1, nullptr));
    EXPECT_EQ(mockDispatch->acquireSwapchainImageCalls, 2u);
    EXPECT_EQ(mockDispatch->waitSwapchainImageCalls, 2u);
    EXPECT_EQ(mockDispatch->releaseSwapchainImageCalls, 2u);
    EXPECT_TRUE(manager.GetEyeSwapchain(1).hasCopiedFrame);

    // Invalid eye index rejected
    EXPECT_FALSE(manager.CopyBackBufferToEye(2, nullptr));
}

TEST(OpenXrManagerTest, SubmitsPairedLayersToOpenXrCompositor) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    XrFovf rightFov{ -0.80f, 0.90f, 0.85f, -0.85f };
    manager.SetSimulatedProjection(0, OpenXrManager::ComputeProjectionFromFov(leftFov));
    manager.SetSimulatedProjection(1, OpenXrManager::ComputeProjectionFromFov(rightFov));

    HeadPose simPose;
    simPose.x = 0.0f;
    simPose.y = 1.6f;
    simPose.z = 0.0f;
    simPose.qw = 1.0f;
    simPose.isValid = true;
    manager.SetSimulatedPose(simPose);
    manager.SetIpd(0.064f);

    EXPECT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    EXPECT_TRUE(manager.CopyBackBufferToEye(1, nullptr));

    EXPECT_EQ(manager.GetSubmittedFramePairCount(), 0u);
    EXPECT_TRUE(manager.SubmitFramePair());
    EXPECT_EQ(manager.GetSubmittedFramePairCount(), 1u);

    EXPECT_EQ(mockDispatch->waitFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->beginFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->endFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->lastFrameEndInfo.layerCount, 1u);
    ASSERT_EQ(mockDispatch->lastSubmittedProjectionViews.size(), 2u);

    // Verify paired left and right views
    const auto& leftView = mockDispatch->lastSubmittedProjectionViews[0];
    const auto& rightView = mockDispatch->lastSubmittedProjectionViews[1];

    EXPECT_NEAR(leftView.fov.angleLeft, -0.90f, 0.001f);
    EXPECT_NEAR(rightView.fov.angleLeft, -0.80f, 0.001f);

    // Left eye shifted by -IPD/2, Right eye shifted by +IPD/2
    EXPECT_NEAR(leftView.pose.position.x, -0.032f, 0.001f);
    EXPECT_NEAR(rightView.pose.position.x, 0.032f, 0.001f);
    EXPECT_NEAR(rightView.pose.position.x - leftView.pose.position.x, 0.064f, 0.001f);
}

} // namespace fs25vr
