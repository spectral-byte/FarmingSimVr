#include <gtest/gtest.h>
#include "openxr/openxr_manager.h"
#include "core/dll_lifecycle.h"
#include "core/logger.h"
#include "core/perf_trace.h"
#include "fake_openxr_tracking.h"
#include "luau/vr_bridge.h"
#include "fake_openxr_events.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_set>
#include <unordered_map>

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

    std::vector<std::string> lastEnabledExtensions;
    // Runtime time = counter + offset, standing in for the runtime-defined epoch.
    int64_t convertedTimeOffset = 7000000000000LL;
    uint32_t convertCalls = 0;
    int64_t lastConvertedCounter = 0;

    XrResult ConvertWin32PerformanceCounterToTime(
        XrInstance, const LARGE_INTEGER* counter, XrTime* time) override {
        ++convertCalls;
        lastConvertedCounter = counter->QuadPart;
        *time = counter->QuadPart + convertedTimeOffset;
        return XR_SUCCESS;
    }

    XrResult EnumerateInstanceExtensionProperties(
        const char*,
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
        const XrInstanceCreateInfo* createInfo,
        XrInstance* instance) override {
        createInstanceCalled = true;
        lastEnabledExtensions.clear();
        for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i) {
            lastEnabledExtensions.push_back(createInfo->enabledExtensionNames[i]);
        }
        if (createInstanceResult != XR_SUCCESS) {
            return createInstanceResult;
        }
        if (instance) {
            *instance = reinterpret_cast<XrInstance>(static_cast<uintptr_t>(0xDEADBEEF));
        }
        return XR_SUCCESS;
    }

    XrResult DestroyInstance(
        XrInstance) override {
        destroyInstanceCalled = true;
        return destroyInstanceResult;
    }

    XrResult GetInstanceProperties(
        XrInstance,
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
        XrInstance,
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
        XrInstance,
        XrSystemId,
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

    XrResult enumerateViewConfigViewsResult = XR_SUCCESS;
    std::vector<XrViewConfigurationView> mockViewConfigViews = {
        { XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 2064, 4096, 2208, 4096, 1, 4 },
        { XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 2064, 4096, 2208, 4096, 1, 4 }
    };

    XrResult EnumerateViewConfigurationViews(
        XrInstance,
        XrSystemId,
        XrViewConfigurationType,
        uint32_t viewCapacityInput,
        uint32_t* viewCountOutput,
        XrViewConfigurationView* configurationViews) override {
        if (enumerateViewConfigViewsResult != XR_SUCCESS) {
            return enumerateViewConfigViewsResult;
        }
        if (viewCountOutput) {
            *viewCountOutput = static_cast<uint32_t>(mockViewConfigViews.size());
        }
        if (viewCapacityInput == 0) {
            return XR_SUCCESS;
        }
        uint32_t count = (viewCapacityInput < mockViewConfigViews.size())
                             ? viewCapacityInput
                             : static_cast<uint32_t>(mockViewConfigViews.size());
        for (uint32_t i = 0; i < count; ++i) {
            configurationViews[i] = mockViewConfigViews[i];
        }
        return XR_SUCCESS;
    }

    XrResult GetD3D12GraphicsRequirementsKHR(
        XrInstance,
        XrSystemId,
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
    XrResult beginSessionResult = XR_SUCCESS;
    XrResult endSessionResult = XR_SUCCESS;
    XrResult destroySessionResult = XR_SUCCESS;
    XrResult createReferenceSpaceResult = XR_SUCCESS;
    XrResult destroySpaceResult = XR_SUCCESS;

    bool createSessionCalled = false;
    bool beginSessionCalled = false;
    bool endSessionCalled = false;
    bool destroySessionCalled = false;
    XrSessionBeginInfo lastBeginSessionInfo{};

    FakeOpenXrTracking tracking;

    MockOpenXrDispatch() { tracking.SetEyeFov(0, { -0.8f, 0.8f, 0.8f, -0.8f }); }

    XrResult CreateSession(
        XrInstance,
        const XrSessionCreateInfo*,
        XrSession* session) override {
        createSessionCalled = true;
        if (createSessionResult != XR_SUCCESS) return createSessionResult;
        if (session) *session = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xCAFE0001));
        if (autoReadyOnCreateSession) events.PushSessionState(XR_SESSION_STATE_READY);
        return XR_SUCCESS;
    }

    FakeOpenXrEventQueue events;
    bool autoReadyOnCreateSession = true;
    uint32_t pollEventCalls = 0;
    XrResult PollEvent(XrInstance, XrEventDataBuffer* eventData) override {
        pollEventCalls++;
        return events.Poll(eventData);
    }

    XrResult BeginSession(
        XrSession,
        const XrSessionBeginInfo* beginInfo) override {
        beginSessionCalled = true;
        if (beginInfo) {
            lastBeginSessionInfo = *beginInfo;
        }
        return beginSessionResult;
    }

    XrResult EndSession(XrSession) override {
        endSessionCalled = true;
        return endSessionResult;
    }

    XrResult DestroySession(XrSession) override {
        destroySessionCalled = true;
        return destroySessionResult;
    }

    std::vector<XrReferenceSpaceCreateInfo> createdReferenceSpaces;
    uint32_t destroySpaceCalls = 0;
    std::vector<XrSpace> destroyedSpaces;

    XrResult CreateReferenceSpace(
        XrSession,
        const XrReferenceSpaceCreateInfo* createInfo,
        XrSpace* space) override {
        if (createReferenceSpaceResult != XR_SUCCESS) return createReferenceSpaceResult;
        if (createInfo) createdReferenceSpaces.push_back(*createInfo);
        return tracking.CreateReferenceSpace(createInfo, space);
    }

    XrResult DestroySpace(XrSpace space) override {
        destroySpaceCalls++;
        destroyedSpaces.push_back(space);
        tracking.DestroySpace(space);
        return destroySpaceResult;
    }

    XrResult LocateSpace(
        XrSpace,
        XrSpace baseSpace,
        XrTime,
        XrSpaceLocation* location) override {
        return tracking.LocateSpace(baseSpace, location);
    }

    XrResult LocateViews(
        XrSession,
        const XrViewLocateInfo*,
        XrViewState* viewState,
        uint32_t viewCapacityInput,
        uint32_t* viewCountOutput,
        XrView* views) override {
        return tracking.LocateViews(viewState, viewCapacityInput, viewCountOutput, views);
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

    bool enforceCallOrder = false;
    bool isFrameWaiting = false;
    bool isFrameBegun = false;
    std::unordered_set<XrSwapchain> acquiredSwapchains;
    uint32_t callOrderErrors = 0;
    XrBool32 mockShouldRender = XR_TRUE;
    XrTime mockPredictedDisplayTime = 1000000;

    std::vector<XrSwapchainCreateInfo> lastSwapchainCreateInfos;
    XrFrameEndInfo lastFrameEndInfo{};
    std::vector<XrCompositionLayerProjectionView> lastSubmittedProjectionViews;
    std::vector<XrCompositionLayerQuad> lastSubmittedQuadLayers;

    XrResult EnumerateSwapchainFormats(
        XrSession,
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
        XrSession,
        const XrSwapchainCreateInfo* createInfo,
        XrSwapchain* swapchain) override {
        createSwapchainCalls++;
        if (createSwapchainResult != XR_SUCCESS) return createSwapchainResult;
        if (createInfo) lastSwapchainCreateInfos.push_back(*createInfo);
        static uintptr_t s_swapchainHandle = 0x5A00;
        if (swapchain) *swapchain = reinterpret_cast<XrSwapchain>(s_swapchainHandle++);
        return XR_SUCCESS;
    }

    XrResult DestroySwapchain(XrSwapchain) override {
        destroySwapchainCalls++;
        return destroySwapchainResult;
    }

    XrResult EnumerateSwapchainImages(
        XrSwapchain,
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
        XrSwapchain swapchain,
        const XrSwapchainImageAcquireInfo*,
        uint32_t* index) override {
        acquireSwapchainImageCalls++;
        if (enforceCallOrder && (!isFrameBegun || acquiredSwapchains.count(swapchain) > 0)) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        if (acquireSwapchainImageResult != XR_SUCCESS) return acquireSwapchainImageResult;
        if (enforceCallOrder) acquiredSwapchains.insert(swapchain);
        if (index) *index = 0;
        return XR_SUCCESS;
    }

    XrResult WaitSwapchainImage(
        XrSwapchain swapchain,
        const XrSwapchainImageWaitInfo*) override {
        waitSwapchainImageCalls++;
        if (enforceCallOrder && (!isFrameBegun || acquiredSwapchains.count(swapchain) == 0)) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        return waitSwapchainImageResult;
    }

    XrResult ReleaseSwapchainImage(
        XrSwapchain swapchain,
        const XrSwapchainImageReleaseInfo*) override {
        releaseSwapchainImageCalls++;
        if (enforceCallOrder && (!isFrameBegun || acquiredSwapchains.count(swapchain) == 0)) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        if (enforceCallOrder) acquiredSwapchains.erase(swapchain);
        return releaseSwapchainImageResult;
    }

    XrResult WaitFrame(
        XrSession,
        const XrFrameWaitInfo*,
        XrFrameState* frameState) override {
        waitFrameCalls++;
        if (enforceCallOrder && isFrameBegun) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        if (waitFrameResult != XR_SUCCESS) return waitFrameResult;
        if (enforceCallOrder) isFrameWaiting = true;
        if (frameState) {
            frameState->type = XR_TYPE_FRAME_STATE;
            frameState->next = nullptr;
            frameState->predictedDisplayTime = mockPredictedDisplayTime;
            frameState->predictedDisplayPeriod = 11111111;
            frameState->shouldRender = mockShouldRender;
        }
        return XR_SUCCESS;
    }

    XrResult BeginFrame(
        XrSession,
        const XrFrameBeginInfo*) override {
        beginFrameCalls++;
        if (enforceCallOrder && (!isFrameWaiting || isFrameBegun)) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        if (beginFrameResult != XR_SUCCESS) return beginFrameResult;
        if (enforceCallOrder) {
            isFrameWaiting = false;
            isFrameBegun = true;
        }
        return XR_SUCCESS;
    }

    XrResult EndFrame(
        XrSession,
        const XrFrameEndInfo* frameEndInfo) override {
        endFrameCalls++;
        if (enforceCallOrder && (!isFrameBegun || !acquiredSwapchains.empty())) {
            callOrderErrors++;
            return XR_ERROR_CALL_ORDER_INVALID;
        }
        if (endFrameResult != XR_SUCCESS) return endFrameResult;
        if (enforceCallOrder) {
            isFrameBegun = false;
        }
        if (frameEndInfo) {
            lastFrameEndInfo = *frameEndInfo;
            lastSubmittedProjectionViews.clear();
            lastSubmittedQuadLayers.clear();
            if (frameEndInfo->layerCount > 0 && frameEndInfo->layers) {
                for (uint32_t l = 0; l < frameEndInfo->layerCount; ++l) {
                    const auto* baseHeader = frameEndInfo->layers[l];
                    if (baseHeader) {
                        if (baseHeader->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                            const auto* projLayer = reinterpret_cast<const XrCompositionLayerProjection*>(baseHeader);
                            if (projLayer && projLayer->views && projLayer->viewCount > 0) {
                                for (uint32_t v = 0; v < projLayer->viewCount; ++v) {
                                    lastSubmittedProjectionViews.push_back(projLayer->views[v]);
                                }
                            }
                        } else if (baseHeader->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
                            const auto* quadLayer = reinterpret_cast<const XrCompositionLayerQuad*>(baseHeader);
                            if (quadLayer) {
                                lastSubmittedQuadLayers.push_back(*quadLayer);
                            }
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

namespace {
constexpr int64_t kTenMhz = 10000000;
constexpr int64_t kDaysOfUptimeCounter = 5LL * 24 * 3600 * kTenMhz;

bool Contains(const std::vector<std::string>& v, const char* name) {
    return std::find(v.begin(), v.end(), name) != v.end();
}
}

TEST(OpenXrManagerTest, EnablesTimeConversionExtensionWhenRuntimeOffersIt) {
    auto dispatch = std::make_shared<MockOpenXrDispatch>();
    dispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME,
                                      XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME };
    OpenXrManager manager(dispatch);
    int64_t counter = kDaysOfUptimeCounter;
    manager.SetPerformanceCounterSource([&] { return OpenXrManager::PerformanceCounterSample{ counter, kTenMhz }; });
    ASSERT_TRUE(manager.Initialize());

    EXPECT_TRUE(Contains(dispatch->lastEnabledExtensions, XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME));
    EXPECT_TRUE(manager.GetRuntimeInfo().win32TimeConversionEnabled);

    EXPECT_EQ(manager.GetCurrentXrTime(), kDaysOfUptimeCounter + dispatch->convertedTimeOffset);
    EXPECT_EQ(dispatch->lastConvertedCounter, kDaysOfUptimeCounter);
    EXPECT_EQ(dispatch->convertCalls, 1u);
}

TEST(OpenXrManagerTest, InitializeSucceedsWithoutTimeConversionExtensionAndFallbackIsSaneAndMonotonic) {
    auto dispatch = std::make_shared<MockOpenXrDispatch>();
    dispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(dispatch);
    int64_t counter = kDaysOfUptimeCounter;
    manager.SetPerformanceCounterSource([&] { return OpenXrManager::PerformanceCounterSample{ counter, kTenMhz }; });

    ASSERT_TRUE(manager.Initialize());
    EXPECT_TRUE(manager.IsActive());
    EXPECT_FALSE(Contains(dispatch->lastEnabledExtensions, XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME));
    EXPECT_FALSE(manager.GetRuntimeInfo().win32TimeConversionEnabled);

    XrTime t0 = manager.GetCurrentXrTime();
    EXPECT_EQ(t0, QpcTicksToNanoseconds(kDaysOfUptimeCounter, kTenMhz));
    EXPECT_GT(t0, 0);
    counter += kTenMhz / 2;
    XrTime t1 = manager.GetCurrentXrTime();
    EXPECT_EQ(t1 - t0, 500000000LL);
    EXPECT_EQ(dispatch->convertCalls, 0u);
}

TEST(OpenXrManagerTest, ExtrapolatesFromLastPredictedDisplayTimeOnceFramesRun) {
    auto dispatch = std::make_shared<MockOpenXrDispatch>();
    dispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME,
                                      XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME };
    OpenXrManager manager(dispatch);
    int64_t counter = kDaysOfUptimeCounter;
    manager.SetPerformanceCounterSource([&] { return OpenXrManager::PerformanceCounterSample{ counter, kTenMhz }; });
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    manager.PollEvents();
    ASSERT_TRUE(manager.BeginFrame());

    XrTime predicted = 1000000;
    ASSERT_NE(predicted, 0);
    counter += kTenMhz;
    EXPECT_EQ(manager.GetCurrentXrTime(), predicted + 1000000000LL);
    EXPECT_EQ(dispatch->convertCalls, 0u);
}

TEST(OpenXrManagerTest, CreateTrackingSpacesCreatesBothBaseAndLocalSpaces) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    EXPECT_NE(manager.GetBaseLocalSpace(), XR_NULL_HANDLE);
    EXPECT_NE(manager.GetLocalSpace(), XR_NULL_HANDLE);
    EXPECT_NE(manager.GetBaseLocalSpace(), manager.GetLocalSpace());
    EXPECT_TRUE(manager.HasTrackingSpaces());

    ASSERT_GE(mockDispatch->createdReferenceSpaces.size(), 2u);
    EXPECT_EQ(mockDispatch->createdReferenceSpaces[0].referenceSpaceType, XR_REFERENCE_SPACE_TYPE_LOCAL);
    EXPECT_EQ(mockDispatch->createdReferenceSpaces[0].poseInReferenceSpace.position.x, 0.0f);
    EXPECT_EQ(mockDispatch->createdReferenceSpaces[0].poseInReferenceSpace.position.y, 0.0f);
    EXPECT_EQ(mockDispatch->createdReferenceSpaces[0].poseInReferenceSpace.position.z, 0.0f);
    EXPECT_EQ(mockDispatch->createdReferenceSpaces[0].poseInReferenceSpace.orientation.w, 1.0f);

    EXPECT_EQ(mockDispatch->createdReferenceSpaces[1].referenceSpaceType, XR_REFERENCE_SPACE_TYPE_LOCAL);
    EXPECT_EQ(mockDispatch->createdReferenceSpaces[1].poseInReferenceSpace.position.x, 0.0f);
    EXPECT_EQ(mockDispatch->createdReferenceSpaces[1].poseInReferenceSpace.position.y, 0.0f);
    EXPECT_EQ(mockDispatch->createdReferenceSpaces[1].poseInReferenceSpace.position.z, 0.0f);
    EXPECT_EQ(mockDispatch->createdReferenceSpaces[1].poseInReferenceSpace.orientation.w, 1.0f);
}

TEST(OpenXrManagerTest, PollsHeadPoseFromOpenXrSpaceAndCalculatesEulerAngles) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    float sinPitch = std::sin(0.5235988f * 0.5f);
    float cosPitch = std::cos(0.5235988f * 0.5f);
    mockDispatch->tracking.head.pose.position = { 0.1f, 0.2f, 0.3f };
    mockDispatch->tracking.head.pose.orientation = { sinPitch, 0.0f, 0.0f, cosPitch };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    HeadPose pose = manager.PollHeadPose();
    EXPECT_TRUE(pose.isValid);
    EXPECT_TRUE(pose.positionTracked);
    EXPECT_TRUE(pose.orientationTracked);
    EXPECT_NEAR(pose.position.x, 0.1f, 0.001f);
    EXPECT_NEAR(pose.position.y, 0.2f, 0.001f);
    EXPECT_NEAR(pose.position.z, 0.3f, 0.001f);
    EXPECT_NEAR(pose.Rotation().pitch, 0.5235988f, 0.005f);
    EXPECT_NEAR(pose.Rotation().yaw, 0.0f, 0.005f);
    EXPECT_NEAR(pose.Rotation().roll, 0.0f, 0.005f);
}

TEST(OpenXrManagerTest, RecenteringZeroesTranslationalAndYawOffsets) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    float sinYaw = std::sin(0.3f * 0.5f);
    float cosYaw = std::cos(0.3f * 0.5f);
    mockDispatch->tracking.head.pose.position = { 0.5f, 1.2f, -0.4f };
    mockDispatch->tracking.head.pose.orientation = { 0.0f, sinYaw, 0.0f, cosYaw };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    HeadPose before = manager.PollHeadPose();
    EXPECT_NEAR(before.position.x, 0.5f, 0.001f);
    EXPECT_NEAR(before.position.y, 1.2f, 0.001f);
    EXPECT_NEAR(before.position.z, -0.4f, 0.001f);

    manager.Recenter();

    HeadPose after = manager.PollHeadPose();
    EXPECT_NEAR(after.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(after.position.y, 0.0f, 0.001f);
    EXPECT_NEAR(after.position.z, 0.0f, 0.001f);
    EXPECT_NEAR(after.Rotation().yaw, 0.0f, 0.005f);
    EXPECT_NEAR(after.orientation.x, 0.0f, 0.001f);
    EXPECT_NEAR(after.orientation.y, 0.0f, 0.001f);
    EXPECT_NEAR(after.orientation.z, 0.0f, 0.001f);
    EXPECT_NEAR(after.orientation.w, 1.0f, 0.001f);

    mockDispatch->tracking.head.pose.position = { 0.6f, 1.25f, -0.42f };
    HeadPose moved = manager.PollHeadPose();
    EXPECT_NEAR(moved.position.y, 0.05f, 0.005f);
}

TEST(OpenXrManagerTest, RecenterRecreatesLocalSpaceWithPoseInReferenceSpaceAndPreservesBaseSpace) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    XrSpace baseSpace = manager.GetBaseLocalSpace();
    XrSpace initialLocalSpace = manager.GetLocalSpace();
    ASSERT_NE(baseSpace, XR_NULL_HANDLE);
    ASSERT_NE(initialLocalSpace, XR_NULL_HANDLE);
    ASSERT_NE(baseSpace, initialLocalSpace);

    float yaw = 0.5f;
    float halfYaw = yaw * 0.5f;
    Quaternion qCombined = QuaternionFromEuler(0.2f, yaw, 0.0f);

    mockDispatch->tracking.head.pose.position = { 1.5f, 0.8f, -2.0f };
    mockDispatch->tracking.head.pose.orientation = { qCombined.x, qCombined.y, qCombined.z, qCombined.w };

    uint32_t destroyCallsBefore = mockDispatch->destroySpaceCalls;
    size_t createdSpacesBefore = mockDispatch->createdReferenceSpaces.size();

    manager.Recenter();

    EXPECT_EQ(mockDispatch->destroySpaceCalls, destroyCallsBefore + 1);
    ASSERT_FALSE(mockDispatch->destroyedSpaces.empty());
    EXPECT_EQ(mockDispatch->destroyedSpaces.back(), initialLocalSpace);
    EXPECT_EQ(manager.GetBaseLocalSpace(), baseSpace);

    XrSpace newLocalSpace = manager.GetLocalSpace();
    EXPECT_NE(newLocalSpace, XR_NULL_HANDLE);
    EXPECT_NE(newLocalSpace, initialLocalSpace);
    EXPECT_NE(newLocalSpace, baseSpace);

    ASSERT_EQ(mockDispatch->createdReferenceSpaces.size(), createdSpacesBefore + 1);
    const auto& newSpaceInfo = mockDispatch->createdReferenceSpaces.back();
    EXPECT_EQ(newSpaceInfo.referenceSpaceType, XR_REFERENCE_SPACE_TYPE_LOCAL);
    EXPECT_NEAR(newSpaceInfo.poseInReferenceSpace.position.x, 1.5f, 0.001f);
    EXPECT_NEAR(newSpaceInfo.poseInReferenceSpace.position.y, 0.8f, 0.001f);
    EXPECT_NEAR(newSpaceInfo.poseInReferenceSpace.position.z, -2.0f, 0.001f);

    EXPECT_NEAR(newSpaceInfo.poseInReferenceSpace.orientation.x, 0.0f, 0.001f);
    EXPECT_NEAR(newSpaceInfo.poseInReferenceSpace.orientation.y, std::sin(halfYaw), 0.005f);
    EXPECT_NEAR(newSpaceInfo.poseInReferenceSpace.orientation.z, 0.0f, 0.001f);
    EXPECT_NEAR(newSpaceInfo.poseInReferenceSpace.orientation.w, std::cos(halfYaw), 0.005f);
}

TEST(OpenXrManagerTest, PollHeadPoseNativelyReflectsRecenteredReferenceFrameAndPreservesRawPose) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    float yaw = 0.6f;
    float pitch = 0.15f;
    Quaternion qHead = QuaternionFromEuler(pitch, yaw, 0.0f);

    mockDispatch->tracking.head.pose.position = { 2.0f, 1.2f, -1.0f };
    mockDispatch->tracking.head.pose.orientation = { qHead.x, qHead.y, qHead.z, qHead.w };

    manager.Recenter();

    HeadPose polled = manager.PollHeadPose();

    EXPECT_NEAR(polled.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(polled.position.y, 0.0f, 0.001f);
    EXPECT_NEAR(polled.position.z, 0.0f, 0.001f);
    EXPECT_NEAR(polled.Rotation().yaw, 0.0f, 0.005f);
    EXPECT_NEAR(polled.Rotation().pitch, pitch, 0.01f);

    HeadPose raw = manager.GetRawPose();
    EXPECT_NEAR(raw.position.x, 2.0f, 0.001f);
    EXPECT_NEAR(raw.position.y, 1.2f, 0.001f);
    EXPECT_NEAR(raw.position.z, -1.0f, 0.001f);
    EXPECT_NEAR(raw.Rotation().yaw, yaw, 0.01f);
    EXPECT_NEAR(raw.Rotation().pitch, pitch, 0.01f);
}

TEST(OpenXrManagerTest, RecenterRebasesLaterHeadMovementToTheRecenteredOrigin) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    HeadPose simPose;
    simPose.position = Vector3(1.0f, 2.0f, 3.0f);
    simPose.orientation = QuaternionFromEuler(0.0f, 0.4f, 0.0f);
    simPose.isValid = true;
    mockDispatch->tracking.SetHeadPose(simPose);

    manager.Recenter();

    HeadPose polled = manager.PollHeadPose();
    EXPECT_NEAR(polled.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(polled.position.y, 0.0f, 0.001f);
    EXPECT_NEAR(polled.position.z, 0.0f, 0.001f);
    EXPECT_NEAR(polled.Rotation().yaw, 0.0f, 0.005f);

    HeadPose movedPose;
    movedPose.position = Vector3(1.0f, 2.5f, 3.0f);
    movedPose.orientation = QuaternionFromEuler(0.0f, 0.4f, 0.0f);
    movedPose.isValid = true;
    mockDispatch->tracking.SetHeadPose(movedPose);

    HeadPose movedPolled = manager.PollHeadPose();
    EXPECT_NEAR(movedPolled.position.y, 0.5f, 0.001f);
}

TEST(OpenXrManagerTest, PollEyeProjectionReportsFovsDrivenThroughFakeTracking) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    XrFovf rightFov{ -0.80f, 0.90f, 0.70f, -0.60f };
    mockDispatch->tracking.SetEyeFov(0, leftFov);
    mockDispatch->tracking.SetEyeFov(1, rightFov);

    EyeProjection left = manager.PollEyeProjection(0);
    EyeProjection right = manager.PollEyeProjection(1);
    ASSERT_TRUE(left.isValid);
    ASSERT_TRUE(right.isValid);
    EXPECT_NEAR(left.angleLeft, -0.90f, 0.0001f);
    EXPECT_NEAR(left.angleRight, 0.80f, 0.0001f);
    EXPECT_NEAR(right.angleUp, 0.70f, 0.0001f);
    EXPECT_NEAR(right.angleDown, -0.60f, 0.0001f);
    EXPECT_NEAR(manager.GetEyeProjection(1).angleLeft, -0.80f, 0.0001f);
}

TEST(OpenXrManagerTest, PollHeadPoseReportsEulerPoseDrivenThroughFakeTracking) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    HeadPose placed;
    placed.position = Vector3(0.4f, 1.7f, -0.2f);
    placed.orientation = QuaternionFromEuler(0.25f, -0.15f, 0.05f);
    placed.isValid = true;
    mockDispatch->tracking.SetHeadPose(placed);

    HeadPose polled = manager.PollHeadPose();
    EXPECT_TRUE(polled.isValid);
    EXPECT_FALSE(polled.positionTracked);
    EXPECT_NEAR(polled.position.y, 1.7f, 0.0001f);
    EXPECT_NEAR(polled.Rotation().pitch, 0.25f, 0.001f);
    EXPECT_NEAR(polled.Rotation().yaw, -0.15f, 0.001f);
    EXPECT_NEAR(polled.Rotation().roll, 0.05f, 0.001f);
}
TEST(OpenXrManagerTest, ShutdownDestroysBaseLocalSpaceAndLocalSpace) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    XrSpace base = manager.GetBaseLocalSpace();
    XrSpace local = manager.GetLocalSpace();
    XrSpace view = manager.GetViewSpace();
    ASSERT_NE(base, XR_NULL_HANDLE);
    ASSERT_NE(local, XR_NULL_HANDLE);
    ASSERT_NE(view, XR_NULL_HANDLE);

    manager.Shutdown();

    EXPECT_EQ(manager.GetBaseLocalSpace(), XR_NULL_HANDLE);
    EXPECT_EQ(manager.GetLocalSpace(), XR_NULL_HANDLE);
    EXPECT_EQ(manager.GetViewSpace(), XR_NULL_HANDLE);
    EXPECT_FALSE(manager.HasTrackingSpaces());

    EXPECT_GE(mockDispatch->destroySpaceCalls, 3u);
    bool destroyedBase = false, destroyedLocal = false, destroyedView = false;
    for (XrSpace s : mockDispatch->destroyedSpaces) {
        if (s == base) destroyedBase = true;
        if (s == local) destroyedLocal = true;
        if (s == view) destroyedView = true;
    }
    EXPECT_TRUE(destroyedBase);
    EXPECT_TRUE(destroyedLocal);
    EXPECT_TRUE(destroyedView);
}

TEST(OpenXrManagerTest, LocatesViewsAndExtractsAsymmetricFovTangents) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    XrView leftView{ XR_TYPE_VIEW, nullptr };
    leftView.fov = { -0.90f, 0.80f, 0.85f, -0.85f };
    leftView.pose.position = { -0.032f, 0.0f, 0.0f };
    leftView.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

    XrView rightView{ XR_TYPE_VIEW, nullptr };
    rightView.fov = { -0.80f, 0.90f, 0.85f, -0.85f };
    rightView.pose.position = { 0.032f, 0.0f, 0.0f };
    rightView.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

    mockDispatch->tracking.views = { leftView, rightView };

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
    // fovY is the rendered FOV: the outer side (tan 0.90) at the headset FOV's own aspect (no swapchains yet).
    EXPECT_NEAR(leftProj.fovY, 1.80f, 0.01f);

    EyeProjection rightProj = manager.PollEyeProjection(1);
    EXPECT_TRUE(rightProj.isValid);
    EXPECT_NEAR(rightProj.angleLeft, -0.80f, 0.001f);
    EXPECT_NEAR(rightProj.angleRight, 0.90f, 0.001f);
    EXPECT_NEAR(rightProj.tanLeft, -1.02964f, 0.005f);
    EXPECT_NEAR(rightProj.tanRight, 1.26016f, 0.005f);
    EXPECT_NEAR(rightProj.xOffset, 0.23052f, 0.005f);
    EXPECT_NEAR(rightProj.yOffset, 0.0f, 0.005f);
    EXPECT_NEAR(rightProj.fovY, 1.80f, 0.01f);

    EXPECT_LT(leftProj.xOffset, 0.0f);
    EXPECT_GT(rightProj.xOffset, 0.0f);
    EXPECT_NEAR(leftProj.xOffset, -rightProj.xOffset, 0.001f);
}

TEST(OpenXrManagerTest, FallbackProjectionWhenPassThroughOrUninitialized) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = {};

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

TEST(OpenXrManagerTest, NegotiatesCopyCompatibleSrgbVariantOfBackBufferFormat) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->mockSwapchainFormats = {
        DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    EXPECT_TRUE(manager.CreateEyeSwapchains(1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM));
    EXPECT_EQ(manager.GetEyeSwapchain(0).format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    EXPECT_EQ(manager.GetEyeSwapchain(1).format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
}

TEST(OpenXrManagerTest, UnormBackBufferGetsSrgbEyeSwapchainEvenWhenUnormIsOffered) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->mockSwapchainFormats = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    EXPECT_TRUE(manager.CreateEyeSwapchains(1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM));
    EXPECT_EQ(manager.GetEyeSwapchain(0).format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    EXPECT_EQ(manager.GetEyeSwapchain(1).format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
}

TEST(OpenXrManagerTest, StaysInPassThroughWhenNoCompatibleSwapchainFormat) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->mockSwapchainFormats = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    manager.PollEvents();
    ASSERT_TRUE(manager.IsSessionRunning());

    EXPECT_FALSE(manager.CreateEyeSwapchains(1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM));
    EXPECT_FALSE(manager.HasEyeSwapchains());
    EXPECT_EQ(mockDispatch->createSwapchainCalls, 0u);
    EXPECT_TRUE(manager.IsPassThrough());
    EXPECT_FALSE(manager.HasSession());
    EXPECT_TRUE(mockDispatch->destroySessionCalled);
    EXPECT_TRUE(mockDispatch->destroyInstanceCalled);
}

TEST(OpenXrManagerTest, DestructorAfterTerminationDetachLeavesRuntimeAlone) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    {
        OpenXrManager manager(mockDispatch);
        ASSERT_TRUE(manager.Initialize());
        ASSERT_TRUE(manager.CreateSession());
        OnProcessDetach(reinterpret_cast<LPVOID>(1));
    }
    ResetProcessTerminatingForTesting();

    EXPECT_FALSE(mockDispatch->destroySessionCalled);
    EXPECT_FALSE(mockDispatch->destroyInstanceCalled);
}

TEST(OpenXrManagerTest, FailedFormatQueryRequestsBackBufferFormatInsteadOfPassThrough) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->enumerateSwapchainFormatsResult = XR_ERROR_RUNTIME_FAILURE;

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    EXPECT_TRUE(manager.EnsureBackBufferFormatSupported(DXGI_FORMAT_R8G8B8A8_UNORM));
    EXPECT_TRUE(manager.CreateEyeSwapchains(1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM));
    EXPECT_EQ(manager.GetEyeSwapchain(0).format, DXGI_FORMAT_R8G8B8A8_UNORM);
    EXPECT_TRUE(manager.IsActive());
}

TEST(OpenXrManagerTest, EmptyFormatListRequestsBackBufferFormatInsteadOfPassThrough) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->mockSwapchainFormats = {};

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    EXPECT_TRUE(manager.CreateEyeSwapchains(1920, 1080, DXGI_FORMAT_B8G8R8A8_UNORM));
    EXPECT_EQ(manager.GetEyeSwapchain(0).format, DXGI_FORMAT_B8G8R8A8_UNORM);
    EXPECT_TRUE(manager.IsActive());
}

TEST(OpenXrManagerTest, EnsureBackBufferFormatSupportedReleasesOpenXrOnlyWhenIncompatible) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());

    EXPECT_TRUE(manager.EnsureBackBufferFormatSupported(DXGI_FORMAT_R8G8B8A8_UNORM));
    EXPECT_TRUE(manager.IsActive());
    EXPECT_TRUE(manager.HasSession());

    EXPECT_FALSE(manager.EnsureBackBufferFormatSupported(DXGI_FORMAT_R16G16B16A16_FLOAT));
    EXPECT_TRUE(manager.IsPassThrough());
    EXPECT_FALSE(manager.HasSession());
    EXPECT_TRUE(mockDispatch->destroyInstanceCalled);

    EXPECT_FALSE(manager.EnsureBackBufferFormatSupported(DXGI_FORMAT_R8G8B8A8_UNORM));
    EXPECT_TRUE(manager.IsPassThrough());
}

TEST(OpenXrManagerTest, SelectCompatibleSwapchainFormatRules) {
    using M = OpenXrManager;
    const std::vector<int64_t> all = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM,
                                       DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM };
    EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_R8G8B8A8_UNORM, all), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, all), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                 { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB }),
              DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_R8G8B8A8_UNORM, { DXGI_FORMAT_R8G8B8A8_UNORM }), DXGI_FORMAT_R8G8B8A8_UNORM);
    EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, all), DXGI_FORMAT_B8G8R8A8_UNORM);
    EXPECT_NE(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_R8G8B8A8_TYPELESS, all), DXGI_FORMAT_UNKNOWN);
    EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, all), DXGI_FORMAT_R16G16B16A16_FLOAT);
    EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_R10G10B10A2_UNORM, all), DXGI_FORMAT_UNKNOWN);
    EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_R8G8B8A8_UNORM, { DXGI_FORMAT_B8G8R8A8_UNORM }), DXGI_FORMAT_UNKNOWN);
    EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_R8G8B8A8_UNORM, {}), DXGI_FORMAT_UNKNOWN);
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

    EXPECT_FALSE(manager.CopyBackBufferToEye(2, nullptr));
}

TEST(OpenXrManagerTest, ClearingANullBackBufferToBlackDoesNothing) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    EXPECT_FALSE(manager.ClearBackBufferToBlack(nullptr));
    EXPECT_EQ(mockDispatch->acquireSwapchainImageCalls, 0u);
}

TEST(OpenXrManagerTest, SubmitsPairedLayersToOpenXrCompositor) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.65f };
    XrFovf rightFov{ -0.80f, 0.90f, 0.85f, -0.65f };
    mockDispatch->tracking.SetEyeFov(0, leftFov);
    mockDispatch->tracking.SetEyeFov(1, rightFov);

    HeadPose simPose;
    simPose.position = Vector3(0.0f, 1.6f, 0.0f);
    simPose.orientation = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    simPose.isValid = true;
    mockDispatch->tracking.SetHeadPose(simPose);
    manager.SetIpd(0.064f);

    EXPECT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    EXPECT_FALSE(manager.SubmitFramePair());

    EXPECT_TRUE(manager.CopyBackBufferToEye(1, nullptr));

    EXPECT_EQ(manager.GetSubmittedFramePairCount(), 0u);
    EXPECT_TRUE(manager.SubmitFramePair());
    EXPECT_EQ(manager.GetSubmittedFramePairCount(), 1u);

    EXPECT_EQ(mockDispatch->waitFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->beginFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->endFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->lastFrameEndInfo.layerCount, 1u);
    ASSERT_EQ(mockDispatch->lastSubmittedProjectionViews.size(), 2u);

    const auto& leftView = mockDispatch->lastSubmittedProjectionViews[0];
    const auto& rightView = mockDispatch->lastSubmittedProjectionViews[1];

    // Verify the covering symmetric FOV at the swapchain aspect (1:1 square pixels for 1920x1080): the widest side
    // is the upper one (0.85; the average of the vertical span would be 0.759 and leave the top uncovered), and
    // the horizontal half-angle is atan(16/9 * tan 0.85) = 1.112.
    EXPECT_NEAR(leftView.fov.angleLeft, -1.112f, 0.01f);
    EXPECT_NEAR(leftView.fov.angleRight, 1.112f, 0.01f);
    EXPECT_NEAR(leftView.fov.angleUp, 0.85f, 0.001f);
    EXPECT_NEAR(leftView.fov.angleDown, -0.85f, 0.001f);

    EXPECT_NEAR(rightView.fov.angleLeft, -1.112f, 0.01f);
    EXPECT_NEAR(rightView.fov.angleRight, 1.112f, 0.01f);
    EXPECT_NEAR(rightView.fov.angleUp, 0.85f, 0.001f);
    EXPECT_NEAR(rightView.fov.angleDown, -0.85f, 0.001f);

    EXPECT_NEAR(leftView.pose.position.x, -0.032f, 0.001f);
    EXPECT_NEAR(rightView.pose.position.x, 0.032f, 0.001f);
    EXPECT_NEAR(rightView.pose.position.x - leftView.pose.position.x, 0.064f, 0.001f);
}

TEST(OpenXrManagerTest, CapturesSeparateTemporalPosesPerEyeAndRotatesByQuaternionIn3D) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));
    manager.SetIpd(0.064f);

    HeadPose leftPose;
    leftPose.position = Vector3(0.0f, 1.6f, 0.0f);
    leftPose.orientation = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    leftPose.isValid = true;
    mockDispatch->tracking.SetHeadPose(leftPose);

    EXPECT_TRUE(manager.CopyBackBufferToEye(0, nullptr));

    // Frame 1 (t_2k+1): Head moves to (0.1, 1.6, 0.2) and rotates 90 degrees around Y (yaw = +pi/2)
    // Quaternion for +90 deg around Y: (0, sin(45 deg), 0, cos(45 deg)) = (0, 0.7071068, 0, 0.7071068)
    HeadPose rightPose;
    rightPose.position = Vector3(0.1f, 1.6f, 0.2f);
    rightPose.orientation = Quaternion(0.0f, 0.7071068f, 0.0f, 0.7071068f);
    rightPose.isValid = true;
    mockDispatch->tracking.SetHeadPose(rightPose);
    manager.PollHeadPose(2000000);

    EXPECT_TRUE(manager.CopyBackBufferToEye(1, nullptr));

    EXPECT_TRUE(manager.SubmitFramePair());
    ASSERT_EQ(mockDispatch->lastSubmittedProjectionViews.size(), 2u);

    const auto& view0 = mockDispatch->lastSubmittedProjectionViews[0];
    const auto& view1 = mockDispatch->lastSubmittedProjectionViews[1];

    // Left eye was captured at t_2k (leftPose):
    // Left eye position: x = 0.0 - 0.032 = -0.032, y = 1.6, z = 0.0
    EXPECT_NEAR(view0.pose.position.x, -0.032f, 0.001f);
    EXPECT_NEAR(view0.pose.position.y, 1.6f, 0.001f);
    EXPECT_NEAR(view0.pose.position.z, 0.0f, 0.001f);
    EXPECT_NEAR(view0.pose.orientation.w, 1.0f, 0.001f);

    // Right eye was captured at t_2k+1 (rightPose):
    // Rotating (0.032, 0, 0) by +90 deg around Y yields (0, 0, -0.032) in standard right-handed coordinates
    // Right eye position: x = 0.1 + 0.0 = 0.1, y = 1.6, z = 0.2 - 0.032 = 0.168
    EXPECT_NEAR(view1.pose.position.x, 0.1f, 0.001f);
    EXPECT_NEAR(view1.pose.position.y, 1.6f, 0.001f);
    EXPECT_NEAR(view1.pose.position.z, 0.168f, 0.001f);
    EXPECT_NEAR(view1.pose.orientation.y, 0.7071068f, 0.001f);
}

TEST(OpenXrManagerTest, StructuredMathTypesConstructAndSupportOperations) {
    Vector3 v(1.0f, 2.0f, 3.0f);
    EXPECT_EQ(v.x, 1.0f);
    EXPECT_EQ(v.y, 2.0f);
    EXPECT_EQ(v.z, 3.0f);

    Vector3 vDef;
    EXPECT_EQ(vDef.x, 0.0f);
    EXPECT_EQ(vDef.y, 0.0f);
    EXPECT_EQ(vDef.z, 0.0f);
    EXPECT_TRUE(v == Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_FALSE(v == vDef);

    Quaternion q(0.1f, 0.2f, 0.3f, 0.9f);
    EXPECT_EQ(q.x, 0.1f);
    EXPECT_EQ(q.y, 0.2f);
    EXPECT_EQ(q.z, 0.3f);
    EXPECT_EQ(q.w, 0.9f);

    Quaternion qDef;
    EXPECT_EQ(qDef.w, 1.0f);
    EXPECT_TRUE(q == Quaternion(0.1f, 0.2f, 0.3f, 0.9f));

    float sinHalf45 = std::sin((3.14159265f / 4.0f) * 0.5f);
    float cosHalf45 = std::cos((3.14159265f / 4.0f) * 0.5f);
    Quaternion qYaw45(0.0f, sinHalf45, 0.0f, cosHalf45);
    Quaternion qYaw90 = qYaw45 * qYaw45;
    EulerAngles yawAngles = OpenXrManager::QuaternionToEuler(qYaw90);
    EXPECT_NEAR(yawAngles.yaw, 3.14159265f / 2.0f, 0.001f);

    EulerAngles ea(0.4f, 0.5f, 0.6f);
    EXPECT_EQ(ea.pitch, 0.4f);
    EXPECT_EQ(ea.yaw, 0.5f);
    EXPECT_EQ(ea.roll, 0.6f);

    EulerAngles eaDef;
    EXPECT_EQ(eaDef.pitch, 0.0f);
}

TEST(OpenXrManagerTest, QuaternionToEulerConvertsStructuredQuaternionToEulerAngles) {
    EulerAngles zeroAngles = OpenXrManager::QuaternionToEuler(Quaternion(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(zeroAngles.pitch, 0.0f, 0.0001f);
    EXPECT_NEAR(zeroAngles.yaw, 0.0f, 0.0001f);
    EXPECT_NEAR(zeroAngles.roll, 0.0f, 0.0001f);

    float sinHalfPitch = std::sin(0.5235988f * 0.5f);
    float cosHalfPitch = std::cos(0.5235988f * 0.5f);
    EulerAngles pitchAngles = OpenXrManager::QuaternionToEuler(Quaternion(sinHalfPitch, 0.0f, 0.0f, cosHalfPitch));
    EXPECT_NEAR(pitchAngles.pitch, 0.5235988f, 0.001f);
    EXPECT_NEAR(pitchAngles.yaw, 0.0f, 0.001f);
    EXPECT_NEAR(pitchAngles.roll, 0.0f, 0.001f);

    float sinHalfYaw = std::sin(0.7853982f * 0.5f);
    float cosHalfYaw = std::cos(0.7853982f * 0.5f);
    EulerAngles yawAngles = OpenXrManager::QuaternionToEuler(Quaternion(0.0f, sinHalfYaw, 0.0f, cosHalfYaw));
    EXPECT_NEAR(yawAngles.pitch, 0.0f, 0.001f);
    EXPECT_NEAR(yawAngles.yaw, 0.7853982f, 0.001f);
    EXPECT_NEAR(yawAngles.roll, 0.0f, 0.001f);
}

TEST(OpenXrManagerTest, RotateVectorByQuaternionRotatesVector3Correctly) {
    float sin45 = std::sin(3.14159265f / 4.0f);
    float cos45 = std::cos(3.14159265f / 4.0f);
    Quaternion qYaw90(0.0f, sin45, 0.0f, cos45);
    Vector3 vX(1.0f, 0.0f, 0.0f);

    Vector3 rotated = OpenXrManager::RotateVectorByQuaternion(qYaw90, vX);
    EXPECT_NEAR(rotated.x, 0.0f, 0.001f);
    EXPECT_NEAR(rotated.y, 0.0f, 0.001f);
    EXPECT_NEAR(rotated.z, -1.0f, 0.001f);

    Quaternion qPitch90(sin45, 0.0f, 0.0f, cos45);
    Vector3 vY(0.0f, 1.0f, 0.0f);
    Vector3 rotatedPitch = OpenXrManager::RotateVectorByQuaternion(qPitch90, vY);
    EXPECT_NEAR(rotatedPitch.x, 0.0f, 0.001f);
    EXPECT_NEAR(rotatedPitch.y, 0.0f, 0.001f);
    EXPECT_NEAR(rotatedPitch.z, 1.0f, 0.001f);
}

TEST(OpenXrManagerTest, ComputesStereoEyePositionsAndPosesCorrectly) {
    HeadPose headPose(
        Vector3(0.0f, 1.7f, 0.0f),
        Quaternion(0.0f, 0.0f, 0.0f, 1.0f),
        true, true, true
    );

    float ipd = 0.064f;
    Vector3 leftEye = OpenXrManager::ComputeEyePosition(headPose, ipd, 0);
    Vector3 rightEye = OpenXrManager::ComputeEyePosition(headPose, ipd, 1);

    EXPECT_NEAR(leftEye.x, -0.032f, 0.001f);
    EXPECT_NEAR(leftEye.y, 1.7f, 0.001f);
    EXPECT_NEAR(leftEye.z, 0.0f, 0.001f);

    EXPECT_NEAR(rightEye.x, 0.032f, 0.001f);
    EXPECT_NEAR(rightEye.y, 1.7f, 0.001f);
    EXPECT_NEAR(rightEye.z, 0.0f, 0.001f);

    HeadPose leftEyePose = OpenXrManager::ComputeEyePose(headPose, ipd, 0);
    EXPECT_TRUE(leftEyePose.isValid);
    EXPECT_NEAR(leftEyePose.position.x, -0.032f, 0.001f);
    EXPECT_NEAR(leftEyePose.position.y, 1.7f, 0.001f);
}

TEST(OpenXrManagerTest, HeadPoseStoresTheQuaternionAndDerivesTheEulerAnglesFromIt) {
    HeadPose pose(
        Vector3(1.5f, 2.5f, -3.5f),
        Quaternion(0.1f, 0.2f, 0.3f, 0.9f)
    );

    EXPECT_EQ(pose.position.x, 1.5f);
    EXPECT_EQ(pose.position.y, 2.5f);
    EXPECT_EQ(pose.position.z, -3.5f);

    EXPECT_EQ(pose.orientation.x, 0.1f);
    EXPECT_EQ(pose.orientation.y, 0.2f);
    EXPECT_EQ(pose.orientation.z, 0.3f);
    EXPECT_EQ(pose.orientation.w, 0.9f);

    EXPECT_EQ(pose.Rotation(), QuaternionToEuler(Quaternion(0.1f, 0.2f, 0.3f, 0.9f)));

    const HeadPose turned(Vector3(), QuaternionFromEuler(0.15f, -0.25f, 0.05f));
    EXPECT_NEAR(turned.Rotation().pitch, 0.15f, 1e-5f);
    EXPECT_NEAR(turned.Rotation().yaw, -0.25f, 1e-5f);
    EXPECT_NEAR(turned.Rotation().roll, 0.05f, 1e-5f);
}

TEST(OpenXrManagerTest, QueriesRecommendedViewConfigurationDimensions) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->mockViewConfigViews = {
        { XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 2160, 4096, 2160, 4096, 1, 4 },
        { XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 2160, 4096, 2160, 4096, 1, 4 }
    };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());

    EXPECT_EQ(manager.GetRuntimeInfo().recommendedEyeWidth, 2160u);
    EXPECT_EQ(manager.GetRuntimeInfo().recommendedEyeHeight, 2160u);
    EXPECT_EQ(manager.GetRecommendedEyeWidth(0), 2160u);
    EXPECT_EQ(manager.GetRecommendedEyeHeight(0), 2160u);
    EXPECT_EQ(manager.GetRecommendedEyeWidth(1), 2160u);
    EXPECT_EQ(manager.GetRecommendedEyeHeight(1), 2160u);

    const auto& views = manager.GetViewConfigurationViews();
    ASSERT_EQ(views.size(), 2u);
    EXPECT_EQ(views[0].recommendedImageRectWidth, 2160u);
    EXPECT_EQ(views[0].recommendedImageRectHeight, 2160u);
}

TEST(OpenXrManagerTest, CreatesEyeSwapchainsWithRecommendedDimensionsWhenDefaulted) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->mockViewConfigViews = {
        { XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 2064, 4096, 2208, 4096, 1, 4 },
        { XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 2064, 4096, 2208, 4096, 1, 4 }
    };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    EXPECT_TRUE(manager.CreateEyeSwapchains(0, 0, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));
    EXPECT_TRUE(manager.HasEyeSwapchains());

    const auto& leftChain = manager.GetEyeSwapchain(0);
    EXPECT_EQ(leftChain.width, 2064u);
    EXPECT_EQ(leftChain.height, 2208u);

    const auto& rightChain = manager.GetEyeSwapchain(1);
    EXPECT_EQ(rightChain.width, 2064u);
    EXPECT_EQ(rightChain.height, 2208u);

    ASSERT_GE(mockDispatch->lastSwapchainCreateInfos.size(), 2u);
    EXPECT_EQ(mockDispatch->lastSwapchainCreateInfos[0].width, 2064u);
    EXPECT_EQ(mockDispatch->lastSwapchainCreateInfos[0].height, 2208u);
}

TEST(OpenXrManagerTest, EnforcesStrictOpenXrFrameLifecycleOrderingAcrossSwapchainAcquireAndRelease) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->enforceCallOrder = true;

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    EXPECT_FALSE(manager.IsFrameBegun());
    EXPECT_EQ(mockDispatch->waitFrameCalls, 0u);
    EXPECT_EQ(mockDispatch->beginFrameCalls, 0u);

    EXPECT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    EXPECT_TRUE(manager.IsFrameBegun());
    EXPECT_EQ(mockDispatch->waitFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->beginFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->acquireSwapchainImageCalls, 1u);
    EXPECT_EQ(mockDispatch->waitSwapchainImageCalls, 1u);
    EXPECT_EQ(mockDispatch->releaseSwapchainImageCalls, 1u);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);

    EXPECT_TRUE(manager.CopyBackBufferToEye(1, nullptr));
    EXPECT_TRUE(manager.IsFrameBegun());
    EXPECT_EQ(mockDispatch->waitFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->beginFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->acquireSwapchainImageCalls, 2u);
    EXPECT_EQ(mockDispatch->waitSwapchainImageCalls, 2u);
    EXPECT_EQ(mockDispatch->releaseSwapchainImageCalls, 2u);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);

    EXPECT_TRUE(manager.SubmitFramePair());
    EXPECT_FALSE(manager.IsFrameBegun());
    EXPECT_EQ(mockDispatch->endFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);
    EXPECT_EQ(manager.GetSubmittedFramePairCount(), 1u);
}

TEST(OpenXrManagerTest, LifecycleHandlesMultipleFramePairsWithoutCallOrderErrors) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->enforceCallOrder = true;

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    const uint32_t kPairCount = 5;
    for (uint32_t pair = 0; pair < kPairCount; ++pair) {
        EXPECT_FALSE(manager.IsFrameBegun());
        EXPECT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
        EXPECT_TRUE(manager.IsFrameBegun());

        EXPECT_TRUE(manager.CopyBackBufferToEye(1, nullptr));
        EXPECT_TRUE(manager.IsFrameBegun());

        EXPECT_TRUE(manager.SubmitFramePair());
        EXPECT_FALSE(manager.IsFrameBegun());
    }

    EXPECT_EQ(manager.GetSubmittedFramePairCount(), kPairCount);
    EXPECT_EQ(mockDispatch->waitFrameCalls, kPairCount);
    EXPECT_EQ(mockDispatch->beginFrameCalls, kPairCount);
    EXPECT_EQ(mockDispatch->endFrameCalls, kPairCount);
    EXPECT_EQ(mockDispatch->acquireSwapchainImageCalls, kPairCount * 2);
    EXPECT_EQ(mockDispatch->releaseSwapchainImageCalls, kPairCount * 2);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);
}

TEST(OpenXrManagerTest, ExplicitBeginFrameAndEndFrameControls) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->enforceCallOrder = true;

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    EXPECT_FALSE(manager.IsFrameBegun());
    EXPECT_FALSE(manager.SubmitFramePair());

    EXPECT_FALSE(manager.EndFrame());

    EXPECT_TRUE(manager.BeginFrame());
    EXPECT_TRUE(manager.IsFrameBegun());

    EXPECT_TRUE(manager.BeginFrame());
    EXPECT_EQ(mockDispatch->beginFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);

    EXPECT_TRUE(manager.EndFrame());
    EXPECT_FALSE(manager.IsFrameBegun());
    EXPECT_EQ(mockDispatch->endFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);
}

TEST(OpenXrManagerTest, SuppressesSwapchainAcquisitionWhenShouldRenderIsFalse) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->enforceCallOrder = true;
    mockDispatch->mockShouldRender = XR_FALSE;

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    EXPECT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    EXPECT_TRUE(manager.IsFrameBegun());
    EXPECT_EQ(mockDispatch->acquireSwapchainImageCalls, 0u);
    EXPECT_EQ(mockDispatch->releaseSwapchainImageCalls, 0u);

    EXPECT_TRUE(manager.CopyBackBufferToEye(1, nullptr));
    EXPECT_EQ(mockDispatch->acquireSwapchainImageCalls, 0u);
    EXPECT_EQ(mockDispatch->releaseSwapchainImageCalls, 0u);

    EXPECT_TRUE(manager.SubmitFramePair());
    EXPECT_FALSE(manager.IsFrameBegun());
    EXPECT_EQ(mockDispatch->endFrameCalls, 1u);
    EXPECT_EQ(mockDispatch->lastFrameEndInfo.layerCount, 0u);
    EXPECT_EQ(mockDispatch->callOrderErrors, 0u);
}

TEST(OpenXrManagerTest, CreateSessionDoesNotBeginUntilRuntimeReportsReady) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->autoReadyOnCreateSession = false;

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_FALSE(manager.IsSessionRunning());

    ASSERT_TRUE(manager.CreateSession());
    EXPECT_TRUE(mockDispatch->createSessionCalled);
    EXPECT_FALSE(manager.IsSessionRunning());
    EXPECT_FALSE(mockDispatch->beginSessionCalled);

    manager.PollEvents();
    EXPECT_FALSE(manager.IsSessionRunning());

    mockDispatch->events.PushSessionState(XR_SESSION_STATE_READY);
    manager.PollEvents();
    EXPECT_TRUE(manager.IsSessionRunning());
    EXPECT_EQ(manager.GetSessionState(), XR_SESSION_STATE_READY);
    EXPECT_TRUE(mockDispatch->beginSessionCalled);
    EXPECT_EQ(mockDispatch->lastBeginSessionInfo.primaryViewConfigurationType, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO);

    manager.Shutdown();
    EXPECT_FALSE(manager.IsSessionRunning());
    EXPECT_TRUE(mockDispatch->endSessionCalled);
    EXPECT_TRUE(mockDispatch->destroySessionCalled);
}

TEST(OpenXrManagerTest, BeginSessionIsRetriedAfterFailingOnReady) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->beginSessionResult = XR_ERROR_RUNTIME_FAILURE;

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());

    manager.PollEvents();
    EXPECT_FALSE(manager.IsSessionRunning());
    EXPECT_TRUE(mockDispatch->beginSessionCalled);
    EXPECT_TRUE(manager.IsActive());
    EXPECT_FALSE(manager.BeginFrame());

    mockDispatch->beginSessionResult = XR_SUCCESS;
    manager.PollEvents();
    EXPECT_TRUE(manager.IsSessionRunning());
    EXPECT_TRUE(manager.BeginFrame());
}

TEST(OpenXrManagerTest, SessionLostFromWaitFrameEntersPassThrough) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    manager.PollEvents();
    ASSERT_TRUE(manager.IsSessionRunning());

    mockDispatch->waitFrameResult = XR_ERROR_SESSION_LOST;
    EXPECT_FALSE(manager.BeginFrame());
    EXPECT_TRUE(manager.IsPassThrough());
    EXPECT_FALSE(manager.HasSession());
    EXPECT_TRUE(mockDispatch->destroySessionCalled);
}

TEST(OpenXrManagerTest, InstanceLostFromEndFrameEntersPassThrough) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    manager.PollEvents();
    ASSERT_TRUE(manager.BeginFrame());

    mockDispatch->endFrameResult = XR_ERROR_INSTANCE_LOST;
    EXPECT_FALSE(manager.EndFrame());
    EXPECT_TRUE(manager.IsPassThrough());
    EXPECT_FALSE(manager.HasSession());
}

TEST(OpenXrManagerTest, OtherFrameErrorsKeepVrActive) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    manager.PollEvents();

    mockDispatch->waitFrameResult = XR_ERROR_RUNTIME_FAILURE;
    EXPECT_FALSE(manager.BeginFrame());
    EXPECT_TRUE(manager.IsActive());
    EXPECT_TRUE(manager.IsSessionRunning());

    mockDispatch->waitFrameResult = XR_SUCCESS;
    EXPECT_TRUE(manager.BeginFrame());
}

TEST(OpenXrManagerTest, StoppingEndsSessionAndLaterReadyResumesVr) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    manager.PollEvents();
    ASSERT_TRUE(manager.IsSessionRunning());

    mockDispatch->events.PushSessionState(XR_SESSION_STATE_STOPPING);
    manager.PollEvents();
    EXPECT_FALSE(manager.IsSessionRunning());
    EXPECT_TRUE(mockDispatch->endSessionCalled);
    EXPECT_TRUE(manager.IsActive());
    EXPECT_TRUE(manager.HasSession());
    EXPECT_FALSE(manager.BeginFrame());

    mockDispatch->beginSessionCalled = false;
    mockDispatch->events.PushSessionState(XR_SESSION_STATE_IDLE);
    mockDispatch->events.PushSessionState(XR_SESSION_STATE_READY);
    manager.PollEvents();
    EXPECT_TRUE(manager.IsSessionRunning());
    EXPECT_TRUE(mockDispatch->beginSessionCalled);
    EXPECT_TRUE(manager.IsActive());
}

TEST(OpenXrManagerTest, LossPendingReleasesResourcesAndEntersPassThrough) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    manager.PollEvents();
    ASSERT_TRUE(manager.IsSessionRunning());

    mockDispatch->destroyInstanceCalled = false;
    mockDispatch->events.PushSessionState(XR_SESSION_STATE_LOSS_PENDING);
    manager.PollEvents();

    EXPECT_TRUE(manager.IsPassThrough());
    EXPECT_FALSE(manager.IsActive());
    EXPECT_FALSE(manager.IsSessionRunning());
    EXPECT_FALSE(manager.HasSession());
    EXPECT_TRUE(mockDispatch->destroySessionCalled);
    EXPECT_TRUE(mockDispatch->destroyInstanceCalled);

    uint32_t pollsBefore = mockDispatch->pollEventCalls;
    manager.PollEvents();
    EXPECT_EQ(mockDispatch->pollEventCalls, pollsBefore);
    EXPECT_FALSE(manager.BeginFrame());
}

TEST(OpenXrManagerTest, ExitingReleasesResourcesAndEntersPassThrough) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    manager.PollEvents();

    mockDispatch->events.PushSessionState(XR_SESSION_STATE_STOPPING);
    mockDispatch->events.PushSessionState(XR_SESSION_STATE_EXITING);
    manager.PollEvents();

    EXPECT_TRUE(manager.IsPassThrough());
    EXPECT_FALSE(manager.HasSession());
    EXPECT_TRUE(mockDispatch->destroyInstanceCalled);
}

TEST(OpenXrManagerTest, InstanceLossPendingEntersPassThrough) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    manager.PollEvents();

    mockDispatch->events.PushInstanceLossPending();
    manager.PollEvents();
    EXPECT_TRUE(manager.IsPassThrough());
    EXPECT_FALSE(manager.HasSession());
}

TEST(OpenXrManagerTest, BeginFramePollsEventsAndRefusesFramesWhileSessionNotRunning) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->autoReadyOnCreateSession = false;

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());

    EXPECT_FALSE(manager.BeginFrame());
    EXPECT_GT(mockDispatch->pollEventCalls, 0u);

    mockDispatch->events.PushSessionState(XR_SESSION_STATE_READY);
    EXPECT_TRUE(manager.BeginFrame());
    EXPECT_TRUE(manager.IsSessionRunning());
}

TEST(OpenXrManagerTest, SuccessCodesOtherThanXrSuccessAreNotTreatedAsFailures) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    mockDispatch->beginSessionResult = XR_SESSION_LOSS_PENDING;

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    manager.PollEvents();
    EXPECT_TRUE(manager.IsSessionRunning());
}
TEST(OpenXrManagerTest, SubmitFramePairSubmitsTheRenderedFovAndNormalizedOrientation) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    HeadPose scaledHead;
    scaledHead.position = Vector3(0.1f, 1.6f, -0.2f);
    scaledHead.orientation = Quaternion(0.0f, 1.2f, 0.0f, 1.6f);
    scaledHead.isValid = true;
    mockDispatch->tracking.SetHeadPose(scaledHead);

    EXPECT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    EXPECT_TRUE(manager.CopyBackBufferToEye(1, nullptr));

    EXPECT_TRUE(manager.SubmitFramePair());
    ASSERT_EQ(mockDispatch->lastSubmittedProjectionViews.size(), 2u);

    for (int i = 0; i < 2; ++i) {
        const auto& pv = mockDispatch->lastSubmittedProjectionViews[i];
        EXPECT_LT(pv.fov.angleLeft, 0.0f);
        EXPECT_GT(pv.fov.angleRight, 0.0f);
        EXPECT_GT(pv.fov.angleUp, 0.0f);
        EXPECT_LT(pv.fov.angleDown, 0.0f);

        float lenSq = pv.pose.orientation.x * pv.pose.orientation.x +
                      pv.pose.orientation.y * pv.pose.orientation.y +
                      pv.pose.orientation.z * pv.pose.orientation.z +
                      pv.pose.orientation.w * pv.pose.orientation.w;
        EXPECT_NEAR(lenSq, 1.0f, 0.001f);
    }
}

TEST(OpenXrManagerTest, AnUnlocatedHeadWithAZeroQuaternionPollsAsIdentityAndSubmitsIdentity) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    HeadPose unlocated;
    unlocated.position = Vector3(0.1f, 1.6f, -0.2f);
    unlocated.orientation = Quaternion(0.0f, 0.0f, 0.0f, 0.0f);
    unlocated.isValid = false;
    mockDispatch->tracking.SetHeadPose(unlocated);

    const HeadPose polled = manager.PollHeadPose();
    EXPECT_FALSE(polled.isValid);
    EXPECT_EQ(polled.orientation, Quaternion(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(polled.Rotation(), EulerAngles(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(manager.GetRawPose().orientation, Quaternion(0.0f, 0.0f, 0.0f, 1.0f));

    EXPECT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    EXPECT_TRUE(manager.CopyBackBufferToEye(1, nullptr));
    EXPECT_TRUE(manager.SubmitFramePair());
    ASSERT_EQ(mockDispatch->lastSubmittedProjectionViews.size(), 2u);
    for (const auto& pv : mockDispatch->lastSubmittedProjectionViews) {
        EXPECT_EQ(pv.pose.orientation.x, 0.0f);
        EXPECT_EQ(pv.pose.orientation.y, 0.0f);
        EXPECT_EQ(pv.pose.orientation.z, 0.0f);
        EXPECT_EQ(pv.pose.orientation.w, 1.0f);
    }
}

TEST(OpenXrManagerTest, AHeadOrientationThatTheRuntimeReportsOffUnitLengthPollsAsTheUnitQuaternionOfTheSameRotation) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    HeadPose head;
    head.position = Vector3(0.1f, 1.6f, -0.2f);
    head.orientation = Quaternion(0.0f, 1.2f, 0.0f, 1.6f);
    head.isValid = true;
    mockDispatch->tracking.SetHeadPose(head);

    const HeadPose polled = manager.PollHeadPose();
    EXPECT_NEAR(polled.orientation.x, 0.0f, 1e-6f);
    EXPECT_NEAR(polled.orientation.y, 0.6f, 1e-6f);
    EXPECT_NEAR(polled.orientation.z, 0.0f, 1e-6f);
    EXPECT_NEAR(polled.orientation.w, 0.8f, 1e-6f);
    EXPECT_NEAR(manager.GetRawPose().orientation.y, 0.6f, 1e-6f);
    EXPECT_NEAR(manager.GetRawPose().orientation.w, 0.8f, 1e-6f);
}

TEST(OpenXrManagerTest, RecenterOnAHeadOrientationOffUnitLengthFacesTheNewOriginTheWayTheHeadLooks) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    const float yaw = 0.5f;
    const Quaternion unit = QuaternionFromEuler(0.0f, yaw, 0.0f);
    HeadPose head;
    head.position = Vector3(1.5f, 0.8f, -2.0f);
    head.orientation = Quaternion(unit.x * 2.0f, unit.y * 2.0f, unit.z * 2.0f, unit.w * 2.0f);
    head.isValid = true;
    mockDispatch->tracking.SetHeadPose(head);

    manager.Recenter();

    const auto& created = mockDispatch->createdReferenceSpaces.back();
    EXPECT_NEAR(created.poseInReferenceSpace.orientation.y, std::sin(yaw * 0.5f), 0.005f);
    EXPECT_NEAR(created.poseInReferenceSpace.orientation.w, std::cos(yaw * 0.5f), 0.005f);
    const Quaternion raw = manager.GetRawPose().orientation;
    EXPECT_NEAR(raw.x * raw.x + raw.y * raw.y + raw.z * raw.z + raw.w * raw.w, 1.0f, 1e-5f);
}

TEST(OpenXrManagerTest, BeginFrameLocatesViewsAndRefreshesProjectionsAndPose) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };

    XrView leftView{ XR_TYPE_VIEW };
    leftView.fov = { -0.85f, 0.82f, 0.78f, -0.75f };
    leftView.pose.position = { -0.033f, 0.0f, 0.0f };
    leftView.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

    XrView rightView{ XR_TYPE_VIEW };
    rightView.fov = { -0.82f, 0.85f, 0.78f, -0.75f };
    rightView.pose.position = { 0.033f, 0.0f, 0.0f };
    rightView.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

    mockDispatch->tracking.views = { leftView, rightView };

    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    EXPECT_TRUE(manager.BeginFrame());
    EXPECT_TRUE(manager.IsFrameBegun());

    const auto& leftProj = manager.GetEyeProjection(0);
    EXPECT_TRUE(leftProj.isValid);
    EXPECT_NEAR(leftProj.angleLeft, -0.85f, 0.001f);
    EXPECT_NEAR(leftProj.angleRight, 0.82f, 0.001f);

    const auto& rightProj = manager.GetEyeProjection(1);
    EXPECT_TRUE(rightProj.isValid);
    EXPECT_NEAR(rightProj.angleLeft, -0.82f, 0.001f);
    EXPECT_NEAR(rightProj.angleRight, 0.85f, 0.001f);

    EXPECT_NEAR(manager.GetIpd(), 0.066f, 0.001f);

    const auto& pose = manager.GetLastPose();
    EXPECT_TRUE(pose.isValid);
}

TEST(OpenXrManagerTest, SubmitMenuQuadRejectsWhenNoEyeSwapchains) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());

    EXPECT_FALSE(manager.SubmitMenuQuad());
}

TEST(OpenXrManagerTest, SubmitMenuQuadRejectsWhenLocalSpaceMissing) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    EXPECT_FALSE(manager.SubmitMenuQuad());
}

TEST(OpenXrManagerTest, SubmitMenuQuadRejectsWhenEye0NotCopied) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    EXPECT_TRUE(manager.BeginFrame());
    EXPECT_FALSE(manager.SubmitMenuQuad());
}

TEST(OpenXrManagerTest, SubmitMenuQuadSubmitsValidXrCompositionLayerQuad) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    EXPECT_EQ(manager.GetSubmittedQuadFrameCount(), 0u);

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    EXPECT_TRUE(manager.IsFrameBegun());
    EXPECT_TRUE(manager.GetEyeSwapchain(0).hasCopiedFrame);

    ASSERT_TRUE(manager.SubmitMenuQuad());
    EXPECT_FALSE(manager.IsFrameBegun());
    EXPECT_FALSE(manager.GetEyeSwapchain(0).hasCopiedFrame);
    EXPECT_EQ(manager.GetSubmittedQuadFrameCount(), 1u);
    EXPECT_EQ(mockDispatch->endFrameCalls, 1u);

    ASSERT_EQ(mockDispatch->lastSubmittedQuadLayers.size(), 1u);
    const auto& quad = mockDispatch->lastSubmittedQuadLayers[0];

    EXPECT_EQ(quad.type, XR_TYPE_COMPOSITION_LAYER_QUAD);
    EXPECT_EQ(quad.next, nullptr);
    EXPECT_EQ(quad.layerFlags, 0u);
    EXPECT_EQ(quad.space, manager.GetLocalSpace());
    EXPECT_EQ(quad.eyeVisibility, XR_EYE_VISIBILITY_BOTH);

    EXPECT_EQ(quad.subImage.swapchain, manager.GetEyeSwapchain(0).handle);
    EXPECT_EQ(quad.subImage.imageRect.offset.x, 0);
    EXPECT_EQ(quad.subImage.imageRect.offset.y, 0);
    EXPECT_EQ(quad.subImage.imageRect.extent.width, 1920);
    EXPECT_EQ(quad.subImage.imageRect.extent.height, 1080);
    EXPECT_EQ(quad.subImage.imageArrayIndex, 0u);

    EXPECT_FLOAT_EQ(quad.pose.position.x, 0.0f);
    EXPECT_FLOAT_EQ(quad.pose.position.y, 0.0f);
    EXPECT_FLOAT_EQ(quad.pose.position.z, -1.5f);
    EXPECT_FLOAT_EQ(quad.pose.orientation.x, 0.0f);
    EXPECT_FLOAT_EQ(quad.pose.orientation.y, 0.0f);
    EXPECT_FLOAT_EQ(quad.pose.orientation.z, 0.0f);
    EXPECT_FLOAT_EQ(quad.pose.orientation.w, 1.0f);

    EXPECT_NEAR(quad.size.width, 1.6f, 0.001f);
    EXPECT_NEAR(quad.size.height, 0.9f, 0.001f);
    EXPECT_NEAR(quad.size.width / quad.size.height, 16.0f / 9.0f, 0.001f);

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.SubmitMenuQuad());
    EXPECT_EQ(manager.GetSubmittedQuadFrameCount(), 2u);
    EXPECT_EQ(mockDispatch->endFrameCalls, 2u);
}

TEST(OpenXrManagerTest, SubmitMenuQuadRespectsRecenterOffset) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    HeadPose simPose;
    simPose.position = Vector3(1.0f, 0.5f, -2.0f);
    simPose.orientation = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    simPose.isValid = true;
    mockDispatch->tracking.SetHeadPose(simPose);

    manager.Recenter();

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.SubmitMenuQuad());

    ASSERT_EQ(mockDispatch->lastSubmittedQuadLayers.size(), 1u);
    const auto& quad = mockDispatch->lastSubmittedQuadLayers[0];

    // The quad is submitted in the recentered local space, whose origin is now the head at
    // (1.0, 0.5, -2.0): 1.5 m in front (-Z) of that origin, at its eye level.
    EXPECT_NEAR(quad.pose.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(quad.pose.position.y, 0.0f, 0.001f);
    EXPECT_NEAR(quad.pose.position.z, -1.5f, 0.001f);
}

TEST(OpenXrManagerTest, CustomMenuQuadDistanceAndSize) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    manager.SetMenuQuadDistance(2.5f);
    EXPECT_FLOAT_EQ(manager.GetMenuQuadDistance(), 2.5f);

    manager.SetMenuQuadSize(2.4f, 1.35f);
    auto size = manager.GetMenuQuadSize();
    EXPECT_FLOAT_EQ(size.width, 2.4f);
    EXPECT_FLOAT_EQ(size.height, 1.35f);

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.SubmitMenuQuad());

    ASSERT_EQ(mockDispatch->lastSubmittedQuadLayers.size(), 1u);
    const auto& quad = mockDispatch->lastSubmittedQuadLayers[0];

    EXPECT_FLOAT_EQ(quad.pose.position.z, -2.5f);
    EXPECT_FLOAT_EQ(quad.size.width, 2.4f);
    EXPECT_FLOAT_EQ(quad.size.height, 1.35f);

    manager.ResetMenuQuadSettings();
    EXPECT_FLOAT_EQ(manager.GetMenuQuadDistance(), 1.5f);
    auto defaultSize = manager.GetMenuQuadSize();
    EXPECT_NEAR(defaultSize.width, 1.6f, 0.001f);
    EXPECT_NEAR(defaultSize.height, 0.9f, 0.001f);
}

TEST(OpenXrManagerTest, MenuQuadDistanceIsClampedToOneToFiveMetresAndOutlivesShutdown) {
    EXPECT_FLOAT_EQ(OpenXrManager::ClampMenuQuadDistance(2.5f), 2.5f);
    EXPECT_FLOAT_EQ(OpenXrManager::ClampMenuQuadDistance(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(OpenXrManager::ClampMenuQuadDistance(5.0f), 5.0f);
    EXPECT_FLOAT_EQ(OpenXrManager::ClampMenuQuadDistance(0.2f), 1.0f);
    EXPECT_FLOAT_EQ(OpenXrManager::ClampMenuQuadDistance(-3.0f), 1.0f);
    EXPECT_FLOAT_EQ(OpenXrManager::ClampMenuQuadDistance(12.0f), 5.0f);
    EXPECT_FLOAT_EQ(OpenXrManager::ClampMenuQuadDistance(std::numeric_limits<float>::infinity()), 5.0f);
    EXPECT_FLOAT_EQ(OpenXrManager::ClampMenuQuadDistance(std::nanf("")), 1.5f) << "NaN is the default";

    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    EXPECT_FLOAT_EQ(manager.GetMenuQuadDistance(), 1.5f) << "1.5 m by default";
    manager.SetMenuQuadDistance(9.0f);
    EXPECT_FLOAT_EQ(manager.GetMenuQuadDistance(), 5.0f) << "the manager clamps on its own";

    ASSERT_TRUE(manager.Initialize());
    manager.SetMenuQuadDistance(3.0f);
    manager.Shutdown();
    EXPECT_FLOAT_EQ(manager.GetMenuQuadDistance(), 3.0f) << "the player's setting is not session state";
}

TEST(OpenXrManagerTest, ChangingTheMenuDistanceMovesAPlacedQuadAlongTheWayItFacesAndKeepsItsSize) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    HeadPose head;
    head.position = Vector3(2.0f, 1.7f, 3.0f);
    head.orientation = QuaternionFromEuler(0.0f, -1.5707963f, 0.0f);
    head.isValid = true;
    manager.AlignMenuQuadToHeadPose(&head);
    const XrPosef placed = manager.ComputeMenuQuadPose();
    ASSERT_NEAR(placed.position.x, 3.5f, 0.01f);
    const XrExtent2Df sizeBefore = manager.ComputeMenuQuadSize();

    manager.SetMenuQuadDistance(3.0f);

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.SubmitMenuQuad());
    ASSERT_EQ(mockDispatch->lastSubmittedQuadLayers.size(), 1u);
    const auto& quad = mockDispatch->lastSubmittedQuadLayers[0];
    EXPECT_NEAR(quad.pose.position.x, 5.0f, 0.01f) << "3 m along the same gaze from the same head";
    EXPECT_NEAR(quad.pose.position.y, 1.7f, 0.001f);
    EXPECT_NEAR(quad.pose.position.z, 3.0f, 0.01f);
    EXPECT_FLOAT_EQ(quad.pose.orientation.x, placed.orientation.x);
    EXPECT_FLOAT_EQ(quad.pose.orientation.y, placed.orientation.y);
    EXPECT_FLOAT_EQ(quad.pose.orientation.z, placed.orientation.z);
    EXPECT_FLOAT_EQ(quad.pose.orientation.w, placed.orientation.w);
    EXPECT_FLOAT_EQ(quad.size.width, sizeBefore.width) << "the quad keeps its size, so it fills less of the view";
    EXPECT_FLOAT_EQ(quad.size.height, sizeBefore.height);

    manager.SetMenuQuadDistance(1.0f);
    EXPECT_NEAR(manager.ComputeMenuQuadPose().position.x, 3.0f, 0.01f);
    HeadPose ahead;
    ahead.position = Vector3(0.0f, 1.6f, 0.0f);
    ahead.isValid = true;
    manager.SetMenuQuadDistance(4.0f);
    manager.AlignMenuQuadToHeadPose(&ahead);
    EXPECT_NEAR(manager.ComputeMenuQuadPose().position.z, -4.0f, 0.001f);
    EXPECT_NEAR(manager.ComputeMenuQuadPose().position.x, 0.0f, 0.001f);
}

TEST(OpenXrManagerTest, SubmitMenuQuadResetsCopiedFlagsForBothSwapchains) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.CopyBackBufferToEye(1, nullptr));
    EXPECT_TRUE(manager.GetEyeSwapchain(0).hasCopiedFrame);
    EXPECT_TRUE(manager.GetEyeSwapchain(1).hasCopiedFrame);

    ASSERT_TRUE(manager.SubmitMenuQuad());

    EXPECT_FALSE(manager.GetEyeSwapchain(0).hasCopiedFrame);
    EXPECT_FALSE(manager.GetEyeSwapchain(1).hasCopiedFrame);
}

TEST(OpenXrManagerTest, ComputeMenuQuadPosePlacesQuadAtGazeVectorAndDecouplesPitchAndRoll) {
    HeadPose headPoseYawRight;
    headPoseYawRight.position = Vector3(2.0f, 1.7f, 3.0f);
    headPoseYawRight.orientation = QuaternionFromEuler(0.4f, -1.5707963f, -0.3f);
    headPoseYawRight.isValid = true;

    XrPosef quadPose = OpenXrManager::ComputeMenuQuadPoseFromHeadPose(headPoseYawRight, 1.5f);

    // Quad position: rotated (0, 0, -1.5) by yaw -pi/2 gives (1.5, 0, 0)
    EXPECT_NEAR(quadPose.position.x, 3.5f, 0.01f);
    EXPECT_NEAR(quadPose.position.y, 1.7f, 0.001f);
    EXPECT_NEAR(quadPose.position.z, 3.0f, 0.01f);

    Quaternion qQuad(quadPose.orientation.x, quadPose.orientation.y, quadPose.orientation.z, quadPose.orientation.w);
    EulerAngles angles = OpenXrManager::QuaternionToEuler(qQuad);
    EXPECT_NEAR(angles.yaw, -1.5707963f, 0.01f);
    EXPECT_NEAR(angles.pitch, 0.0f, 0.001f);
    EXPECT_NEAR(angles.roll, 0.0f, 0.001f);

    HeadPose headPoseForward;
    headPoseForward.position = Vector3(-1.0f, 1.5f, 2.0f);
    headPoseForward.orientation = QuaternionFromEuler(-0.5f, 0.0f, 0.2f);
    headPoseForward.isValid = true;

    XrPosef quadPoseFwd = OpenXrManager::ComputeMenuQuadPoseFromHeadPose(headPoseForward, 1.5f);
    EXPECT_NEAR(quadPoseFwd.position.x, -1.0f, 0.001f);
    EXPECT_NEAR(quadPoseFwd.position.y, 1.5f, 0.001f);
    EXPECT_NEAR(quadPoseFwd.position.z, 0.5f, 0.001f);
    Quaternion qQuadFwd(quadPoseFwd.orientation.x, quadPoseFwd.orientation.y, quadPoseFwd.orientation.z, quadPoseFwd.orientation.w);
    EulerAngles anglesFwd = OpenXrManager::QuaternionToEuler(qQuadFwd);
    EXPECT_NEAR(anglesFwd.yaw, 0.0f, 0.001f);
    EXPECT_NEAR(anglesFwd.pitch, 0.0f, 0.001f);
    EXPECT_NEAR(anglesFwd.roll, 0.0f, 0.001f);
}

TEST(OpenXrManagerTest, SubmitMenuQuadAlignsToHeadPoseOnRecenter) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    HeadPose simPose;
    simPose.position = Vector3(3.0f, 1.0f, -1.0f);
    float halfYaw = 0.785398f * 0.5f;
    simPose.orientation = Quaternion(0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw));
    simPose.isValid = true;
    mockDispatch->tracking.SetHeadPose(simPose);

    manager.Recenter();

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.SubmitMenuQuad());

    ASSERT_EQ(mockDispatch->lastSubmittedQuadLayers.size(), 1u);
    const auto& quad = mockDispatch->lastSubmittedQuadLayers[0];

    // The recentered local space has its origin at the head and its forward along the 45 degree gaze,
    // and the quad is submitted in it: straight ahead of the origin, facing it. In the runtime's own
    // local space that is 1.5 m along the gaze from (3.0, 1.0, -1.0), as before the recenter.
    EXPECT_NEAR(quad.pose.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(quad.pose.position.y, 0.0f, 0.001f);
    EXPECT_NEAR(quad.pose.position.z, -1.5f, 0.001f);

    Quaternion qQuad(quad.pose.orientation.x, quad.pose.orientation.y, quad.pose.orientation.z, quad.pose.orientation.w);
    EulerAngles angles = OpenXrManager::QuaternionToEuler(qQuad);
    EXPECT_NEAR(angles.yaw, 0.0f, 0.001f);
    EXPECT_NEAR(angles.pitch, 0.0f, 0.001f);
    EXPECT_NEAR(angles.roll, 0.0f, 0.001f);
}

TEST(OpenXrManagerTest, MenuQuadRemainsAnchoredInLocalSpaceAcrossHeadMotion) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    HeadPose initialPose;
    initialPose.position = Vector3(0.0f, 1.7f, 0.0f);
    initialPose.isValid = true;
    mockDispatch->tracking.SetHeadPose(initialPose);
    manager.PollHeadPose();

    manager.AlignMenuQuadToHeadPose();

    HeadPose movedPose;
    movedPose.position = Vector3(0.0f, 1.7f, -0.5f);
    movedPose.orientation = QuaternionFromEuler(0.3f, 0.2f, 0.1f);
    movedPose.isValid = true;
    mockDispatch->tracking.SetHeadPose(movedPose);

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.SubmitMenuQuad());

    ASSERT_EQ(mockDispatch->lastSubmittedQuadLayers.size(), 1u);
    const auto& quad = mockDispatch->lastSubmittedQuadLayers[0];

    EXPECT_NEAR(quad.pose.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(quad.pose.position.y, 1.7f, 0.001f);
    EXPECT_NEAR(quad.pose.position.z, -1.5f, 0.001f);
}

namespace {

struct MenuQuadSession {
    std::shared_ptr<MockOpenXrDispatch> dispatch = std::make_shared<MockOpenXrDispatch>();
    OpenXrManager manager{dispatch};

    MenuQuadSession() {
        dispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
        EXPECT_TRUE(manager.Initialize());
        EXPECT_TRUE(manager.CreateSession());
        EXPECT_TRUE(manager.CreateTrackingSpaces());
        EXPECT_TRUE(manager.CreateEyeSwapchains(1920, 1080));
    }

    void SetHead(float x, float y, float z, float yaw, bool valid) {
        HeadPose pose;
        pose.position = Vector3(x, y, z);
        pose.orientation = QuaternionFromEuler(0.0f, yaw, 0.0f);
        pose.isValid = valid;
        pose.positionTracked = valid;
        pose.orientationTracked = valid;
        dispatch->tracking.SetHeadPose(pose);
    }

    XrPosef PresentQuadFrame(bool entersQuadMode = false) {
        dispatch->mockPredictedDisplayTime += 11111111;
        EXPECT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
        if (entersQuadMode) {
            manager.AlignMenuQuadToHeadPose();
        }
        EXPECT_TRUE(manager.SubmitMenuQuad());
        return manager.GetLastSubmittedQuadLayer().pose;
    }
};

}

TEST(OpenXrManagerTest, MenuQuadPlacedBeforeTheHeadIsTrackedMovesToTheHeadOnceTracked) {
    MenuQuadSession s;
    s.SetHead(0.0f, 0.0f, 0.0f, 0.0f, false);
    XrPosef first = s.PresentQuadFrame(true);
    EXPECT_NEAR(first.position.y, 0.0f, 0.001f);

    s.SetHead(0.2f, 0.6f, 0.1f, 0.0f, true);
    XrPosef tracked = s.PresentQuadFrame();
    EXPECT_NEAR(tracked.position.x, 0.2f, 0.001f);
    EXPECT_NEAR(tracked.position.y, 0.6f, 0.001f);
    EXPECT_NEAR(tracked.position.z, 0.1f - 1.5f, 0.001f);

    s.SetHead(-0.3f, 0.9f, 0.4f, 0.5f, true);
    XrPosef later = s.PresentQuadFrame();
    EXPECT_NEAR(later.position.x, 0.2f, 0.001f);
    EXPECT_NEAR(later.position.y, 0.6f, 0.001f);
    EXPECT_NEAR(later.position.z, 0.1f - 1.5f, 0.001f);
}

TEST(OpenXrManagerTest, MenuQuadPlacedWhileTheRuntimeSkipsRenderingMovesToTheHeadOnceItRenders) {
    MenuQuadSession s;
    s.SetHead(0.0f, 0.6f, 0.0f, 0.0f, true);
    s.dispatch->mockShouldRender = XR_FALSE;
    s.dispatch->mockPredictedDisplayTime += 11111111;
    s.manager.CopyBackBufferToEye(0, nullptr);
    s.manager.AlignMenuQuadToHeadPose();
    EXPECT_NEAR(s.manager.ComputeMenuQuadPose().position.y, 0.0f, 0.001f);
    s.manager.EndFrame();

    s.dispatch->mockShouldRender = XR_TRUE;
    XrPosef rendered = s.PresentQuadFrame();
    EXPECT_NEAR(rendered.position.y, 0.6f, 0.001f);
    EXPECT_NEAR(rendered.position.z, -1.5f, 0.001f);
}

TEST(OpenXrManagerTest, RuntimeRecenterOfTheLocalSpaceMovesMenuQuadToTheHead) {
    MenuQuadSession s;
    s.SetHead(0.0f, 0.0f, 0.0f, 0.0f, true);
    s.PresentQuadFrame(true);

    s.dispatch->events.PushReferenceSpaceChangePending(XR_REFERENCE_SPACE_TYPE_LOCAL, 0);
    const float yaw = 0.6f;
    s.SetHead(0.1f, 0.5f, -0.2f, yaw, true);
    XrPosef recentered = s.PresentQuadFrame();
    EXPECT_NEAR(recentered.position.x, 0.1f - 1.5f * std::sin(yaw), 0.001f);
    EXPECT_NEAR(recentered.position.y, 0.5f, 0.001f);
    EXPECT_NEAR(recentered.position.z, -0.2f - 1.5f * std::cos(yaw), 0.001f);
    EXPECT_NEAR(OpenXrManager::QuaternionToEuler(Quaternion(recentered.orientation.x, recentered.orientation.y,
                                                            recentered.orientation.z, recentered.orientation.w)).yaw,
                yaw, 0.001f);

    s.SetHead(0.4f, 0.8f, 0.3f, 0.0f, true);
    XrPosef later = s.PresentQuadFrame();
    EXPECT_NEAR(later.position.y, 0.5f, 0.001f);
}

TEST(OpenXrManagerTest, MenuQuadWaitsForTheChangeTimeOfARuntimeRecenter) {
    MenuQuadSession s;
    s.SetHead(0.0f, 0.0f, 0.0f, 0.0f, true);
    s.PresentQuadFrame(true);

    const XrTime changeTime = s.dispatch->mockPredictedDisplayTime + 2 * 11111111;
    s.dispatch->events.PushReferenceSpaceChangePending(XR_REFERENCE_SPACE_TYPE_LOCAL, changeTime);
    s.SetHead(0.0f, 0.3f, 0.0f, 0.0f, true);
    XrPosef early = s.PresentQuadFrame();
    EXPECT_NEAR(early.position.y, 0.0f, 0.001f);

    s.SetHead(0.0f, 0.7f, 0.0f, 0.0f, true);
    XrPosef atChange = s.PresentQuadFrame();
    EXPECT_NEAR(atChange.position.y, 0.7f, 0.001f);
}

TEST(OpenXrManagerTest, MenuQuadMovesToTheHeadTheFirstTimeTheSessionIsFocused) {
    MenuQuadSession s;
    s.SetHead(0.5f, -0.3f, -0.4f, -0.2f, true);
    s.PresentQuadFrame(true);

    s.dispatch->events.PushSessionState(XR_SESSION_STATE_SYNCHRONIZED);
    s.dispatch->events.PushSessionState(XR_SESSION_STATE_VISIBLE);
    s.dispatch->events.PushSessionState(XR_SESSION_STATE_FOCUSED);
    s.SetHead(0.0f, 0.1f, 0.0f, 0.0f, true);
    XrPosef worn = s.PresentQuadFrame();
    EXPECT_NEAR(worn.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(worn.position.y, 0.1f, 0.001f);
    EXPECT_NEAR(worn.position.z, -1.5f, 0.001f);

    s.dispatch->events.PushSessionState(XR_SESSION_STATE_VISIBLE);
    s.dispatch->events.PushSessionState(XR_SESSION_STATE_FOCUSED);
    s.SetHead(0.3f, 0.4f, 0.2f, 0.5f, true);
    XrPosef afterDashboard = s.PresentQuadFrame();
    EXPECT_NEAR(afterDashboard.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(afterDashboard.position.y, 0.1f, 0.001f);
    EXPECT_NEAR(afterDashboard.position.z, -1.5f, 0.001f);

    s.dispatch->events.PushSessionState(XR_SESSION_STATE_STOPPING);
    s.dispatch->events.PushSessionState(XR_SESSION_STATE_IDLE);
    s.dispatch->events.PushSessionState(XR_SESSION_STATE_READY);
    s.dispatch->events.PushSessionState(XR_SESSION_STATE_FOCUSED);
    s.SetHead(-0.2f, 0.2f, 0.1f, 0.0f, true);
    XrPosef resumed = s.PresentQuadFrame();
    EXPECT_NEAR(resumed.position.x, -0.2f, 0.001f);
    EXPECT_NEAR(resumed.position.y, 0.2f, 0.001f);
    EXPECT_NEAR(resumed.position.z, 0.1f - 1.5f, 0.001f);
}

TEST(OpenXrManagerTest, MenuOpenedAfterARecenterIsPlacedInFrontOfTheHeadInTheRecenteredSpace) {
    MenuQuadSession s;
    s.SetHead(5.3f, 23.7f, 5.0f, 0.8f, true);
    s.manager.Recenter();

    const float turn = 0.3f;
    s.SetHead(5.3f, 23.8f, 5.0f, 0.8f + turn, true);
    XrPosef opened = s.PresentQuadFrame(true);

    EXPECT_NEAR(opened.position.x, -1.5f * std::sin(turn), 0.001f);
    EXPECT_NEAR(opened.position.y, 0.1f, 0.001f);
    EXPECT_NEAR(opened.position.z, -1.5f * std::cos(turn), 0.001f);
    EXPECT_NEAR(OpenXrManager::QuaternionToEuler(Quaternion(opened.orientation.x, opened.orientation.y,
                                                            opened.orientation.z, opened.orientation.w)).yaw,
                turn, 0.001f);
}

TEST(OpenXrManagerTest, ChangeOfAnotherReferenceSpaceLeavesMenuQuadInPlace) {
    MenuQuadSession s;
    s.SetHead(0.0f, 0.0f, 0.0f, 0.0f, true);
    s.PresentQuadFrame(true);

    s.dispatch->events.PushReferenceSpaceChangePending(XR_REFERENCE_SPACE_TYPE_STAGE, 0);
    s.SetHead(0.0f, 0.5f, 0.0f, 0.0f, true);
    XrPosef after = s.PresentQuadFrame();
    EXPECT_NEAR(after.position.y, 0.0f, 0.001f);
}

TEST(OpenXrManagerTest, ComputeCursorQuadPoseProjectsNormalizedCoordinatesWithHotspotOffset) {
    XrPosef menuPose{};
    menuPose.position = { 0.0f, 1.5f, -1.5f };
    menuPose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

    XrExtent2Df menuSize{ 1.6f, 0.9f };
    float cursorSize = 0.035f;

    XrPosef centerCursor = OpenXrManager::ComputeCursorQuadPose(menuPose, menuSize, 0.5f, 0.5f, cursorSize, cursorSize);
    EXPECT_NEAR(centerCursor.position.x, 0.0f + 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(centerCursor.position.y, 1.5f - 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(centerCursor.position.z, -1.5f + 0.002f, 0.001f);
    EXPECT_FLOAT_EQ(centerCursor.orientation.w, 1.0f);

    XrPosef tlCursor = OpenXrManager::ComputeCursorQuadPose(menuPose, menuSize, 0.0f, 0.0f, cursorSize, cursorSize);
    EXPECT_NEAR(tlCursor.position.x, -0.8f + 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(tlCursor.position.y, 1.95f - 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(tlCursor.position.z, -1.5f + 0.002f, 0.001f);

    XrPosef brCursor = OpenXrManager::ComputeCursorQuadPose(menuPose, menuSize, 1.0f, 1.0f, cursorSize, cursorSize);
    EXPECT_NEAR(brCursor.position.x, 0.8f + 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(brCursor.position.y, 1.05f - 0.5f * cursorSize, 0.001f);
    EXPECT_NEAR(brCursor.position.z, -1.5f + 0.002f, 0.001f);
}

TEST(OpenXrManagerTest, ComputeCursorQuadPoseRotatesWithMenuQuadOrientation) {
    XrPosef menuPose{};
    menuPose.position = { -1.5f, 1.5f, 0.0f };
    float halfAngle = 3.14159265f / 4.0f;
    menuPose.orientation = { 0.0f, std::sin(halfAngle), 0.0f, std::cos(halfAngle) };

    XrExtent2Df menuSize{ 1.6f, 0.9f };
    float cursorSize = 0.035f;

    XrPosef cursorPose = OpenXrManager::ComputeCursorQuadPose(menuPose, menuSize, 0.5f, 0.5f, cursorSize, cursorSize);
    EXPECT_NEAR(cursorPose.orientation.y, menuPose.orientation.y, 0.001f);
    EXPECT_NEAR(cursorPose.orientation.w, menuPose.orientation.w, 0.001f);
    EXPECT_GT(cursorPose.position.x, menuPose.position.x);
}

TEST(OpenXrManagerTest, CreateCursorSwapchainInitializes64x64RgbaSwapchain) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());

    EXPECT_FALSE(manager.HasCursorSwapchain());
    ASSERT_TRUE(manager.CreateCursorSwapchain());
    EXPECT_TRUE(manager.HasCursorSwapchain());

    const auto& cursorSwapchain = manager.GetCursorSwapchain();
    EXPECT_EQ(cursorSwapchain.width, 64u);
    EXPECT_EQ(cursorSwapchain.height, 64u);
    EXPECT_NE(cursorSwapchain.handle, XR_NULL_HANDLE);
    EXPECT_EQ(cursorSwapchain.format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    manager.DestroyCursorSwapchain();
    EXPECT_FALSE(manager.HasCursorSwapchain());
}

TEST(OpenXrManagerTest, SubmitMenuQuadSubmitsTwoLayersWhenCursorVisible) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));
    ASSERT_TRUE(manager.CreateCursorSwapchain());

    MenuCursorState cursorState{ true, 0.25f, 0.75f, MenuCursorState::kDefaultCursorSize };
    manager.SetMenuCursorState(cursorState);
    EXPECT_TRUE(manager.GetMenuCursorState().visible);

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.SubmitMenuQuad());

    ASSERT_EQ(mockDispatch->lastSubmittedQuadLayers.size(), 2u);

    const auto& menuQuad = mockDispatch->lastSubmittedQuadLayers[0];
    EXPECT_EQ(menuQuad.type, XR_TYPE_COMPOSITION_LAYER_QUAD);
    EXPECT_EQ(menuQuad.subImage.swapchain, manager.GetEyeSwapchain(0).handle);
    EXPECT_EQ(menuQuad.layerFlags, 0u);

    const auto& cursorQuad = mockDispatch->lastSubmittedQuadLayers[1];
    EXPECT_EQ(cursorQuad.type, XR_TYPE_COMPOSITION_LAYER_QUAD);
    EXPECT_EQ(cursorQuad.space, manager.GetLocalSpace());
    EXPECT_EQ(cursorQuad.eyeVisibility, XR_EYE_VISIBILITY_BOTH);
    EXPECT_EQ(cursorQuad.subImage.swapchain, manager.GetCursorSwapchain().handle);
    EXPECT_EQ(cursorQuad.layerFlags, XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
    EXPECT_FLOAT_EQ(cursorQuad.size.width, MenuCursorState::kDefaultCursorSize);
    EXPECT_FLOAT_EQ(cursorQuad.size.height, MenuCursorState::kDefaultCursorSize);

    XrPosef expectedPose = OpenXrManager::ComputeCursorQuadPose(menuQuad.pose, menuQuad.size, cursorState, MenuCursorState::kDefaultCursorSize, MenuCursorState::kDefaultCursorSize);
    EXPECT_NEAR(cursorQuad.pose.position.x, expectedPose.position.x, 0.001f);
    EXPECT_NEAR(cursorQuad.pose.position.y, expectedPose.position.y, 0.001f);
    EXPECT_NEAR(cursorQuad.pose.position.z, expectedPose.position.z, 0.001f);
}

TEST(OpenXrManagerTest, SubmitMenuQuadSubmitsSingleLayerWhenCursorHidden) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));
    ASSERT_TRUE(manager.CreateCursorSwapchain());

    manager.SetMenuCursorState({ false, 0.5f, 0.5f, MenuCursorState::kDefaultCursorSize });

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.SubmitMenuQuad());

    ASSERT_EQ(mockDispatch->lastSubmittedQuadLayers.size(), 1u);
    const auto& menuQuad = mockDispatch->lastSubmittedQuadLayers[0];
    EXPECT_EQ(menuQuad.subImage.swapchain, manager.GetEyeSwapchain(0).handle);
}

namespace {

HeadPose MakeRenderTestPose(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.orientation = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    pose.isValid = true;
    return pose;
}

struct RenderPoseFixture {
    std::shared_ptr<MockOpenXrDispatch> dispatch = std::make_shared<MockOpenXrDispatch>();
    std::shared_ptr<OpenXrManager> manager;
    std::unique_ptr<VRBridge> bridge;

    explicit RenderPoseFixture(VRRenderingMode mode) {
        dispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
        manager = std::make_shared<OpenXrManager>(dispatch);
        EXPECT_TRUE(manager->Initialize());
        EXPECT_TRUE(manager->CreateSession());
        EXPECT_TRUE(manager->CreateTrackingSpaces());
        manager->PollEvents();
        EXPECT_TRUE(manager->IsSessionRunning());
        EXPECT_TRUE(manager->CreateEyeSwapchains(1920, 1080));
        manager->SetIpd(0.064f);
        bridge = std::make_unique<VRBridge>(manager, nullptr);
        bridge->SetRenderingMode(mode);
    }
};

}

TEST(OpenXrManagerTest, SequentialDualPassSubmitsEachEyeWithPoseItWasRenderedWith) {
    RenderPoseFixture f(VRRenderingMode::SequentialDualPass);

    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(0.0f, 1.6f, 0.0f));
    f.bridge->GetHeadPose();
    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(5.0f, 1.6f, 5.0f));
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(0, nullptr));

    f.bridge->SetSubPassIndex(1);
    f.bridge->GetHeadPose();
    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(9.0f, 9.0f, 9.0f));
    f.manager->PollHeadPose(2000000);
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(1, nullptr));
    ASSERT_TRUE(f.manager->SubmitFramePair());

    ASSERT_EQ(f.dispatch->lastSubmittedProjectionViews.size(), 2u);
    const auto& v0 = f.dispatch->lastSubmittedProjectionViews[0];
    const auto& v1 = f.dispatch->lastSubmittedProjectionViews[1];
    EXPECT_NEAR(v0.pose.position.x, -0.032f, 0.001f);
    EXPECT_NEAR(v0.pose.position.y, 1.6f, 0.001f);
    EXPECT_NEAR(v0.pose.position.z, 0.0f, 0.001f);
    EXPECT_NEAR(v1.pose.position.x, 5.0f + 0.032f, 0.001f);
    EXPECT_NEAR(v1.pose.position.y, 1.6f, 0.001f);
    EXPECT_NEAR(v1.pose.position.z, 5.0f, 0.001f);
}

TEST(OpenXrManagerTest, AfrSubmitsEachEyeWithPoseItWasRenderedWith) {
    RenderPoseFixture f(VRRenderingMode::AFR);
    ASSERT_EQ(f.bridge->GetFrameParity(), 0u);

    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(0.0f, 1.6f, 0.0f));
    f.bridge->GetHeadPose();
    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(5.0f, 1.6f, 5.0f));
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(0, nullptr));
    f.bridge->AdvanceFrame();
    ASSERT_EQ(f.bridge->GetFrameParity(), 1u);

    f.bridge->GetHeadPose();
    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(9.0f, 9.0f, 9.0f));
    f.manager->PollHeadPose(2000000);
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(1, nullptr));
    ASSERT_TRUE(f.manager->SubmitFramePair());

    ASSERT_EQ(f.dispatch->lastSubmittedProjectionViews.size(), 2u);
    const auto& v0 = f.dispatch->lastSubmittedProjectionViews[0];
    const auto& v1 = f.dispatch->lastSubmittedProjectionViews[1];
    EXPECT_NEAR(v0.pose.position.x, -0.032f, 0.001f);
    EXPECT_NEAR(v0.pose.position.z, 0.0f, 0.001f);
    EXPECT_NEAR(v1.pose.position.x, 5.0f + 0.032f, 0.001f);
    EXPECT_NEAR(v1.pose.position.z, 5.0f, 0.001f);
}
TEST(OpenXrManagerTest, FrameBeginPollDoesNotOverwriteRecordedRenderPose) {
    RenderPoseFixture f(VRRenderingMode::SequentialDualPass);

    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(0.0f, 1.6f, 0.0f));
    f.bridge->GetHeadPose();
    ASSERT_FALSE(f.manager->IsFrameBegun());
    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(3.0f, 1.0f, 3.0f));
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(0, nullptr));
    EXPECT_NEAR(f.manager->GetCapturedEyePose(0).position.x, 0.0f, 0.001f);
    EXPECT_NEAR(f.manager->GetCapturedEyePose(0).position.z, 0.0f, 0.001f);
}

TEST(OpenXrManagerTest, FallsBackToPolledPoseWhenNoRenderPoseRecorded) {
    RenderPoseFixture f(VRRenderingMode::SequentialDualPass);

    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(1.0f, 1.5f, 2.0f));
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(0, nullptr));
    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(1.0f, 1.5f, 2.0f));
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(1, nullptr));
    ASSERT_TRUE(f.manager->SubmitFramePair());

    const auto& v0 = f.dispatch->lastSubmittedProjectionViews[0];
    EXPECT_NEAR(v0.pose.position.x, 1.0f - 0.032f, 0.001f);
    EXPECT_NEAR(v0.pose.position.z, 2.0f, 0.001f);
}

TEST(OpenXrManagerTest, RecordedRenderPoseIsConsumedByCopy) {
    RenderPoseFixture f(VRRenderingMode::SequentialDualPass);

    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(0.0f, 1.6f, 0.0f));
    f.bridge->GetHeadPose();
    EXPECT_TRUE(f.manager->HasRecordedRenderPose(0));
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(0, nullptr));
    EXPECT_FALSE(f.manager->HasRecordedRenderPose(0));
}

TEST(OpenXrManagerTest, RuntimeTrackedPosesSubmitEachEyeWithItsRenderPose) {
    RenderPoseFixture f(VRRenderingMode::SequentialDualPass);
    auto setTracked = [&](float x, float y, float z) {
        f.dispatch->tracking.head.pose.position = { x, y, z };
    };

    setTracked(0.0f, 1.6f, 0.0f);
    f.bridge->GetHeadPose();
    setTracked(5.0f, 1.6f, 5.0f);
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(0, nullptr));

    f.bridge->SetSubPassIndex(1);
    f.bridge->GetHeadPose();
    setTracked(9.0f, 9.0f, 9.0f);
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(1, nullptr));
    ASSERT_TRUE(f.manager->SubmitFramePair());

    ASSERT_EQ(f.dispatch->lastSubmittedProjectionViews.size(), 2u);
    const auto& v0 = f.dispatch->lastSubmittedProjectionViews[0];
    const auto& v1 = f.dispatch->lastSubmittedProjectionViews[1];
    EXPECT_NEAR(v0.pose.position.x, -0.032f, 0.001f);
    EXPECT_NEAR(v0.pose.position.z, 0.0f, 0.001f);
    EXPECT_NEAR(v1.pose.position.x, 5.0f + 0.032f, 0.001f);
    EXPECT_NEAR(v1.pose.position.z, 5.0f, 0.001f);
}

TEST(OpenXrManagerTest, InvalidPoseIsNotRecorded) {
    RenderPoseFixture f(VRRenderingMode::SequentialDualPass);
    HeadPose invalid = MakeRenderTestPose(1.0f, 1.0f, 1.0f);
    invalid.isValid = false;
    f.dispatch->tracking.SetHeadPose(invalid);
    f.bridge->GetHeadPose();
    EXPECT_FALSE(f.manager->HasRecordedRenderPose(0));
}

TEST(OpenXrManagerTest, GetEyePoseRecordsForTheRequestedEye) {
    RenderPoseFixture f(VRRenderingMode::SequentialDualPass);
    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(0.0f, 1.6f, 0.0f));
    f.bridge->GetEyePose(1);
    EXPECT_FALSE(f.manager->HasRecordedRenderPose(0));
    EXPECT_TRUE(f.manager->HasRecordedRenderPose(1));
}

TEST(OpenXrManagerTest, UnconsumedRenderPoseExpiresAtFrameEnd) {
    RenderPoseFixture f(VRRenderingMode::SequentialDualPass);
    f.dispatch->mockShouldRender = XR_FALSE;

    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(0.0f, 1.6f, 0.0f));
    f.bridge->GetHeadPose();
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(f.manager->HasRecordedRenderPose(0));
    ASSERT_TRUE(f.manager->CopyBackBufferToEye(1, nullptr));
    ASSERT_TRUE(f.manager->SubmitFramePair());

    EXPECT_FALSE(f.manager->HasRecordedRenderPose(0));
}

TEST(OpenXrManagerTest, RecenterDropsRecordedRenderPoses) {
    RenderPoseFixture f(VRRenderingMode::SequentialDualPass);
    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(0.0f, 1.6f, 0.0f));
    f.bridge->GetEyePose(0);
    f.bridge->GetEyePose(1);

    f.manager->Recenter();  // recorded poses are in the pre-recenter tracking space

    EXPECT_FALSE(f.manager->HasRecordedRenderPose(0));
    EXPECT_FALSE(f.manager->HasRecordedRenderPose(1));
}

TEST(OpenXrManagerTest, SessionStopDropsRecordedRenderPoses) {
    RenderPoseFixture f(VRRenderingMode::SequentialDualPass);
    f.dispatch->tracking.SetHeadPose(MakeRenderTestPose(0.0f, 1.6f, 0.0f));
    f.bridge->GetEyePose(0);

    f.dispatch->events.PushSessionState(XR_SESSION_STATE_STOPPING);
    f.manager->PollEvents();
    ASSERT_FALSE(f.manager->IsSessionRunning());

    EXPECT_FALSE(f.manager->HasRecordedRenderPose(0));
}

TEST(OpenXrManagerTest, DispatchWithoutInputSupportKeepsTheSessionAndReportsUntrackedHands) {
    auto mockDispatch = std::make_shared<MockOpenXrDispatch>();
    mockDispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    OpenXrManager manager(mockDispatch);
    ASSERT_TRUE(manager.Initialize());

    EXPECT_TRUE(manager.CreateSession());
    EXPECT_TRUE(manager.CreateTrackingSpaces());
    manager.PollEvents();

    EXPECT_TRUE(manager.IsSessionRunning());
    EXPECT_FALSE(manager.HasMotionControllerActions());
    EXPECT_FALSE(manager.GetHandState(0).tracked);
    EXPECT_FALSE(manager.GetHandState(1).tracked);
    EXPECT_EQ(mockDispatch->ApplyHapticFeedback(XR_NULL_HANDLE, nullptr, nullptr), XR_ERROR_FUNCTION_UNSUPPORTED);
    EXPECT_EQ(mockDispatch->StopHapticFeedback(XR_NULL_HANDLE, nullptr), XR_ERROR_FUNCTION_UNSUPPORTED);
}

namespace {
class OpenXrLogCapture {
public:
    explicit OpenXrLogCapture(const char* fileName) : m_path(std::filesystem::temp_directory_path() / fileName) {
        Logger::Instance().SetLogFile(m_path);
    }
    ~OpenXrLogCapture() {
        Logger::Instance().SetLogFile(Logger::DefaultLogPath());
        std::filesystem::remove(m_path);
    }
    size_t Count(const std::string& needle) const {
        Logger::Instance().Flush();
        std::ifstream file(m_path);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        size_t count = 0;
        for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) {
            ++count;
        }
        return count;
    }
private:
    std::filesystem::path m_path;
};

struct IpdRig {
    std::shared_ptr<MockOpenXrDispatch> dispatch = std::make_shared<MockOpenXrDispatch>();
    OpenXrManager manager{dispatch};
    IpdRig() {
        dispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
        XrView left{ XR_TYPE_VIEW, nullptr };
        left.fov = { -0.8f, 0.8f, 0.8f, -0.8f };
        left.pose.position = { -0.032f, 0.0f, 0.0f };
        left.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
        XrView right = left;
        right.pose.position = { 0.032f, 0.0f, 0.0f };
        dispatch->tracking.views = { left, right };
    }
    bool Start() { return manager.Initialize() && manager.CreateSession() && manager.CreateTrackingSpaces(); }
};
}

TEST(OpenXrManagerTest, TwoManagersInOneProcessEachLogTheirFirstIpdCalibration) {
    OpenXrLogCapture capture("fs25vr_openxr_ipd_log_test.log");
    const std::string calibrated = "OpenXrManager: Calibrated hardware IPD";

    IpdRig first;
    ASSERT_TRUE(first.Start());
    ASSERT_TRUE(first.manager.LocateActiveViews());
    EXPECT_EQ(capture.Count(calibrated), 1u);
    ASSERT_TRUE(first.manager.LocateActiveViews());
    EXPECT_EQ(capture.Count(calibrated), 1u) << "an unchanged IPD is not logged again";

    IpdRig second;
    ASSERT_TRUE(second.Start());
    ASSERT_TRUE(second.manager.LocateActiveViews());
    EXPECT_EQ(capture.Count(calibrated), 2u);
}

TEST(OpenXrManagerTest, PairWithoutAnEyeProjectionIsNotSubmittedAndTheLogSaysSoOncePerSession) {
    OpenXrLogCapture capture("fs25vr_openxr_missing_projection_log_test.log");
    const std::string missing = "SubmitFramePair: No eye projection";

    auto dispatch = std::make_shared<MockOpenXrDispatch>();
    dispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    dispatch->tracking.views.clear();
    OpenXrManager manager(dispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    for (uint32_t frame = 1; frame <= 2; ++frame) {
        ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
        ASSERT_TRUE(manager.CopyBackBufferToEye(1, nullptr));
        EXPECT_FALSE(manager.SubmitFramePair());

        EXPECT_EQ(dispatch->endFrameCalls, frame);
        EXPECT_EQ(dispatch->lastFrameEndInfo.layerCount, 0u);
        EXPECT_TRUE(dispatch->lastSubmittedProjectionViews.empty());
        EXPECT_FALSE(manager.IsFrameBegun());
        EXPECT_EQ(manager.GetSubmittedFramePairCount(), 0u);
        EXPECT_EQ(capture.Count(missing), 1u);
    }

    manager.Shutdown();
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));
    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.CopyBackBufferToEye(1, nullptr));
    EXPECT_FALSE(manager.SubmitFramePair());
    EXPECT_EQ(capture.Count(missing), 2u);
}

TEST(OpenXrManagerTest, PairIsSubmittedOnceTheRuntimeLocatesViews) {
    auto dispatch = std::make_shared<MockOpenXrDispatch>();
    dispatch->availableExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME };
    dispatch->tracking.locateViewsResult = XR_ERROR_TIME_INVALID;
    OpenXrManager manager(dispatch);
    ASSERT_TRUE(manager.Initialize());
    ASSERT_TRUE(manager.CreateSession());
    ASSERT_TRUE(manager.CreateTrackingSpaces());
    ASSERT_TRUE(manager.CreateEyeSwapchains(1920, 1080));

    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.CopyBackBufferToEye(1, nullptr));
    EXPECT_FALSE(manager.SubmitFramePair());
    EXPECT_EQ(dispatch->lastFrameEndInfo.layerCount, 0u);

    dispatch->tracking.locateViewsResult = XR_SUCCESS;
    ASSERT_TRUE(manager.CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(manager.CopyBackBufferToEye(1, nullptr));
    EXPECT_TRUE(manager.SubmitFramePair());
    EXPECT_EQ(dispatch->lastFrameEndInfo.layerCount, 1u);
    EXPECT_EQ(manager.GetSubmittedFramePairCount(), 1u);
}

TEST(OpenXrManagerTest, EyeCopySizeMismatchIsLoggedOncePerSessionAndAgainAfterShutdown) {
    OpenXrLogCapture capture("fs25vr_openxr_mismatch_log_test.log");
    const std::string mismatch = "Dimension mismatch! BackBuffer: 1920x1080, OpenXR swapchain: 2064x2208";

    IpdRig rig;
    ASSERT_TRUE(rig.Start());
    rig.manager.LogEyeCopySizeMismatchOnce(1920, 1080, 2064, 2208);
    rig.manager.LogEyeCopySizeMismatchOnce(1920, 1080, 2064, 2208);
    EXPECT_EQ(capture.Count(mismatch), 1u);

    rig.manager.Shutdown();
    ASSERT_TRUE(rig.Start());
    rig.manager.LogEyeCopySizeMismatchOnce(1920, 1080, 2064, 2208);
    EXPECT_EQ(capture.Count(mismatch), 2u);
}

TEST(OpenXrManagerTest, HeadPoseLocateFailuresAreLoggedAgainInANewManagerAndAfterShutdown) {
    OpenXrLogCapture capture("fs25vr_openxr_locate_fail_log_test.log");
    const std::string failed = "PollHeadPose: LocateSpace failed";

    auto failOnce = [&](IpdRig& rig) {
        rig.dispatch->tracking.locateSpaceResult = XR_ERROR_RUNTIME_FAILURE;
        rig.manager.PollHeadPose(1);
    };
    {
        IpdRig rig;
        ASSERT_TRUE(rig.Start());
        failOnce(rig);
        failOnce(rig);
        EXPECT_EQ(capture.Count(failed), 1u) << "only every 300th failure is logged";
        rig.manager.Shutdown();
        ASSERT_TRUE(rig.Start());
        failOnce(rig);
        EXPECT_EQ(capture.Count(failed), 2u);
    }
    IpdRig other;
    ASSERT_TRUE(other.Start());
    failOnce(other);
    EXPECT_EQ(capture.Count(failed), 3u);
}

}