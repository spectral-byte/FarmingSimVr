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

} // namespace fs25vr
