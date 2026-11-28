#include <gtest/gtest.h>
#include "openxr/openxr_cursor_overlay.h"
#include "core/raii_utils.h"
#include <dxgi1_4.h>
#include <atomic>
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

TEST(OpenXrCursorOverlayTest, UploadTextureReleasesImageOnWaitFailureAndDestroysStaticSwapchainForRetry) {
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
    EXPECT_EQ(mockDispatch->destroySwapchainCalls, 1u);
    EXPECT_EQ(overlay.GetLastXrResult(), XR_ERROR_RUNTIME_FAILURE);
    EXPECT_FALSE(overlay.HasSwapchain());

    EXPECT_FALSE(overlay.UploadTexture(dummyDevice, dummyQueue, mockDispatch));
    EXPECT_EQ(mockDispatch->acquireImageCalls, 1u);

    mockDispatch->waitImageResult = XR_SUCCESS;
    ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));
    EXPECT_EQ(mockDispatch->createSwapchainCalls, 2u);
    mockDispatch->acquireIndex = 99;
    EXPECT_FALSE(overlay.UploadTexture(dummyDevice, dummyQueue, mockDispatch));
    EXPECT_EQ(mockDispatch->acquireImageCalls, 2u);
    EXPECT_EQ(mockDispatch->releaseImageCalls, 2u);
    EXPECT_EQ(mockDispatch->destroySwapchainCalls, 2u);
    EXPECT_FALSE(overlay.HasSwapchain());

    ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));
    EXPECT_EQ(mockDispatch->createSwapchainCalls, 3u);
    mockDispatch->acquireIndex = 0;
    EXPECT_FALSE(overlay.UploadTexture(dummyDevice, dummyQueue, mockDispatch));
    EXPECT_EQ(mockDispatch->acquireImageCalls, 3u);
    EXPECT_EQ(mockDispatch->releaseImageCalls, 3u);
    EXPECT_EQ(mockDispatch->destroySwapchainCalls, 3u);
    EXPECT_FALSE(overlay.HasSwapchain());
}

namespace {

constexpr GUID kCursorDestroyTrackerGuid = { 0x9e6a52c3, 0x4c7d, 0x5d2e, { 0xaf, 0x31, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 } };
constexpr GUID kCursorMockFenceGuid = { 0x9e6a52c3, 0x4c7d, 0x5d2e, { 0xaf, 0x31, 0x22, 0x33, 0x44, 0x55, 0x66, 0x78 } };

class CursorDestroyTracker final : public IUnknown {
public:
    explicit CursorDestroyTracker(std::atomic<int>* destroyCount) : m_destroyCount(destroyCount) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == __uuidof(IUnknown)) {
            *ppvObject = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --m_refCount;
        if (remaining == 0) {
            if (m_destroyCount) ++(*m_destroyCount);
            delete this;
        }
        return remaining;
    }
private:
    std::atomic<ULONG> m_refCount{ 1 };
    std::atomic<int>* m_destroyCount = nullptr;
};

void AttachCursorDestroyTracker(ID3D12Object* object, std::atomic<int>* destroyCount) {
    if (!object || !destroyCount) return;
    Microsoft::WRL::ComPtr<CursorDestroyTracker> tracker;
    tracker.Attach(new CursorDestroyTracker(destroyCount));
    object->SetPrivateDataInterface(kCursorDestroyTrackerGuid, tracker.Get());
}

class CursorMockFence final : public ID3D12Fence {
public:
    CursorMockFence(Microsoft::WRL::ComPtr<ID3D12Fence> inner, std::atomic<int>* destroyCount,
                    bool returnDeviceRemoved, bool failSetEvent)
        : m_inner(std::move(inner)), m_destroyCount(destroyCount),
          m_returnDeviceRemoved(returnDeviceRemoved), m_failSetEvent(failSetEvent) {}
    ~CursorMockFence() {
        if (m_destroyCount) ++(*m_destroyCount);
    }

    ID3D12Fence* Inner() const { return m_inner.Get(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == kCursorMockFenceGuid) {
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
        if (m_returnDeviceRemoved) return UINT64_MAX;
        if (m_failSetEvent) return 0;
        return m_inner->GetCompletedValue();
    }
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 Value, HANDLE hEvent) override {
        if (m_failSetEvent) return E_FAIL;
        return m_inner->SetEventOnCompletion(Value, hEvent);
    }
    HRESULT STDMETHODCALLTYPE Signal(UINT64 Value) override { return m_inner->Signal(Value); }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    Microsoft::WRL::ComPtr<ID3D12Fence> m_inner;
    std::atomic<int>* m_destroyCount = nullptr;
    bool m_returnDeviceRemoved = false;
    bool m_failSetEvent = false;
};

class CursorMockQueue final : public ID3D12CommandQueue {
public:
    CursorMockQueue(Microsoft::WRL::ComPtr<ID3D12CommandQueue> inner, HRESULT signalResult)
        : m_inner(std::move(inner)), m_signalResult(signalResult) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
            riid == __uuidof(ID3D12CommandQueue)) {
            *ppvObject = static_cast<ID3D12CommandQueue*>(this);
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
        if (FAILED(m_signalResult)) return m_signalResult;
        void* raw = nullptr;
        if (pFence && SUCCEEDED(pFence->QueryInterface(kCursorMockFenceGuid, &raw)) && raw) {
            Microsoft::WRL::ComPtr<CursorMockFence> wrapped;
            wrapped.Attach(static_cast<CursorMockFence*>(raw));
            return m_inner->Signal(wrapped->Inner(), Value);
        }
        return m_inner->Signal(pFence, Value);
    }
    HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* pFence, UINT64 Value) override { return m_inner->Wait(pFence, Value); }
    HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* pFrequency) override { return m_inner->GetTimestampFrequency(pFrequency); }
    HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* pGpuTimestamp, UINT64* pCpuTimestamp) override {
        return m_inner->GetClockCalibration(pGpuTimestamp, pCpuTimestamp);
    }
    D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc() override { return m_inner->GetDesc(); }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_inner;
    HRESULT m_signalResult = S_OK;
};

class CursorMockDevice final : public ID3D12Device {
public:
    CursorMockDevice(Microsoft::WRL::ComPtr<ID3D12Device> inner, std::atomic<int>* destroyCount,
                     bool returnDeviceRemoved, bool failSetEvent, bool failCreateResource = false)
        : m_inner(std::move(inner)), m_destroyCount(destroyCount),
          m_returnDeviceRemoved(returnDeviceRemoved), m_failSetEvent(failSetEvent),
          m_failCreateResource(failCreateResource) {}

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
    UINT STDMETHODCALLTYPE GetNodeCount() override { return m_inner->GetNodeCount(); }
    HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue) override {
        return m_inner->CreateCommandQueue(pDesc, riid, ppCommandQueue);
    }
    HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID riid, void** ppCommandAllocator) override {
        const HRESULT hr = m_inner->CreateCommandAllocator(type, riid, ppCommandAllocator);
        if (SUCCEEDED(hr) && ppCommandAllocator && *ppCommandAllocator) {
            AttachCursorDestroyTracker(static_cast<ID3D12Object*>(*ppCommandAllocator), m_destroyCount);
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) override {
        return m_inner->CreateGraphicsPipelineState(pDesc, riid, ppPipelineState);
    }
    HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) override {
        return m_inner->CreateComputePipelineState(pDesc, riid, ppPipelineState);
    }
    HRESULT STDMETHODCALLTYPE CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* pCommandAllocator,
                                                ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) override {
        const HRESULT hr = m_inner->CreateCommandList(nodeMask, type, pCommandAllocator, pInitialState, riid, ppCommandList);
        if (SUCCEEDED(hr) && ppCommandList && *ppCommandList) {
            AttachCursorDestroyTracker(static_cast<ID3D12Object*>(*ppCommandList), m_destroyCount);
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE Feature, void* pFeatureSupportData, UINT FeatureSupportDataSize) override {
        return m_inner->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
    }
    HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc, REFIID riid, void** ppvHeap) override {
        return m_inner->CreateDescriptorHeap(pDescriptorHeapDesc, riid, ppvHeap);
    }
    UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) override {
        return m_inner->GetDescriptorHandleIncrementSize(DescriptorHeapType);
    }
    HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT nodeMask, const void* pBlobWithRootSignature, SIZE_T blobLengthInBytes,
                                                  REFIID riid, void** ppvRootSignature) override {
        return m_inner->CreateRootSignature(nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature);
    }
    void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateConstantBufferView(pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateShaderResourceView(pResource, pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource* pCounterResource,
                                                     const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateUnorderedAccessView(pResource, pCounterResource, pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateRenderTargetView(pResource, pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource* pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateDepthStencilView(pResource, pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override {
        m_inner->CreateSampler(pDesc, DestDescriptor);
    }
    void STDMETHODCALLTYPE CopyDescriptors(UINT a, const D3D12_CPU_DESCRIPTOR_HANDLE* b, const UINT* c, UINT d,
                                           const D3D12_CPU_DESCRIPTOR_HANDLE* e, const UINT* f, D3D12_DESCRIPTOR_HEAP_TYPE g) override {
        m_inner->CopyDescriptors(a, b, c, d, e, f, g);
    }
    void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT a, D3D12_CPU_DESCRIPTOR_HANDLE b, D3D12_CPU_DESCRIPTOR_HANDLE c, D3D12_DESCRIPTOR_HEAP_TYPE d) override {
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
        if (m_failCreateResource) return E_OUTOFMEMORY;
        const HRESULT hr = m_inner->CreateCommittedResource(a, b, c, d, e, f, g);
        if (SUCCEEDED(hr) && g && *g) {
            AttachCursorDestroyTracker(static_cast<ID3D12Object*>(*g), m_destroyCount);
        }
        return hr;
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
        Microsoft::WRL::ComPtr<ID3D12Fence> innerFence;
        const HRESULT hr = m_inner->CreateFence(InitialValue, Flags, IID_PPV_ARGS(&innerFence));
        if (FAILED(hr)) return hr;
        Microsoft::WRL::ComPtr<CursorMockFence> wrapped;
        wrapped.Attach(new CursorMockFence(innerFence, m_destroyCount, m_returnDeviceRemoved, m_failSetEvent));
        return wrapped->QueryInterface(riid, ppFence);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override { return m_inner->GetDeviceRemovedReason(); }
    void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC* a, UINT b, UINT c, UINT64 d,
                                                 D3D12_PLACED_SUBRESOURCE_FOOTPRINT* e, UINT* f, UINT64* g, UINT64* h) override {
        m_inner->GetCopyableFootprints(a, b, c, d, e, f, g, h);
    }
    HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* a, REFIID b, void** c) override { return m_inner->CreateQueryHeap(a, b, c); }
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
    Microsoft::WRL::ComPtr<ID3D12Device> m_inner;
    std::atomic<int>* m_destroyCount = nullptr;
    bool m_returnDeviceRemoved = false;
    bool m_failSetEvent = false;
    bool m_failCreateResource = false;
};

}

TEST(OpenXrCursorOverlayTest, UploadTextureSucceedsOnceOnWarpDeviceAndDestroysStaticSwapchainOnFailure) {
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
        EXPECT_EQ(mockDispatch->destroySwapchainCalls, 1u);
        EXPECT_FALSE(overlay.HasSwapchain());
        EXPECT_EQ(overlay.GetLastXrResult(), XR_ERROR_RUNTIME_FAILURE);

        mockDispatch->releaseImageResult = XR_SUCCESS;
        EXPECT_FALSE(overlay.UploadTexture(device.Get(), queue.Get(), mockDispatch));
        EXPECT_EQ(mockDispatch->acquireImageCalls, 1u);

        ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));
        EXPECT_EQ(mockDispatch->createSwapchainCalls, 2u);
        EXPECT_TRUE(overlay.UploadTexture(device.Get(), queue.Get(), mockDispatch));
        EXPECT_EQ(mockDispatch->acquireImageCalls, 2u);

        EXPECT_TRUE(overlay.UploadTexture(device.Get(), queue.Get(), mockDispatch));
        EXPECT_EQ(mockDispatch->acquireImageCalls, 2u);
    }

    {
        std::atomic<int> destroyCount{ 0 };
        Microsoft::WRL::ComPtr<CursorMockDevice> removedFenceDevice;
        removedFenceDevice.Attach(new CursorMockDevice(device, &destroyCount, true, false));
        Microsoft::WRL::ComPtr<CursorMockQueue> mockQueue;
        mockQueue.Attach(new CursorMockQueue(queue, S_OK));

        OpenXrCursorOverlay overlay;
        ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));
        XrCompositionLayerQuad menuQuad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
        menuQuad.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
        menuQuad.size = { 1.6f, 0.9f };
        MenuCursorState cursorState{ true, 0.5f, 0.5f, MenuCursorState::kDefaultCursorSize };
        XrSpace localSpace = reinterpret_cast<XrSpace>(static_cast<uintptr_t>(0x1234));
        XrCompositionLayerQuad outLayer{};

        EXPECT_FALSE(overlay.PrepareCompositionLayer(menuQuad, cursorState, localSpace, outLayer,
                                                     removedFenceDevice.Get(), mockQueue.Get(), mockDispatch));
        EXPECT_FALSE(overlay.HasLastSubmittedCursorQuadLayer());
        EXPECT_FALSE(overlay.HasSwapchain());
        EXPECT_EQ(destroyCount.load(), 0);

        const uint32_t acquiresAfterFail = mockDispatch->acquireImageCalls;
        EXPECT_FALSE(overlay.UploadTexture(device.Get(), queue.Get(), mockDispatch));
        EXPECT_EQ(mockDispatch->acquireImageCalls, acquiresAfterFail);

        ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));
        EXPECT_TRUE(overlay.PrepareCompositionLayer(menuQuad, cursorState, localSpace, outLayer,
                                                    device.Get(), queue.Get(), mockDispatch));
        EXPECT_TRUE(overlay.HasLastSubmittedCursorQuadLayer());
    }

    {
        std::atomic<int> destroyCount{ 0 };
        Microsoft::WRL::ComPtr<CursorMockDevice> failEventDevice;
        failEventDevice.Attach(new CursorMockDevice(device, &destroyCount, false, true));
        Microsoft::WRL::ComPtr<CursorMockQueue> mockQueue;
        mockQueue.Attach(new CursorMockQueue(queue, S_OK));

        OpenXrCursorOverlay overlay;
        ASSERT_TRUE(overlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));
        EXPECT_FALSE(overlay.UploadTexture(failEventDevice.Get(), mockQueue.Get(), mockDispatch));
        EXPECT_FALSE(overlay.HasSwapchain());
        EXPECT_EQ(destroyCount.load(), 0);
    }

    {
        std::atomic<int> destroyCount{ 0 };
        Microsoft::WRL::ComPtr<CursorMockDevice> failCreateDevice;
        failCreateDevice.Attach(new CursorMockDevice(device, &destroyCount, false, false, true));
        Microsoft::WRL::ComPtr<CursorMockQueue> mockQueue;
        mockQueue.Attach(new CursorMockQueue(queue, S_OK));

        OpenXrCursorOverlay failedOverlay;
        const uint32_t acquiresBefore = mockDispatch->acquireImageCalls;
        ASSERT_TRUE(failedOverlay.CreateSwapchain(mockSession, mockDispatch, 64, 64));
        EXPECT_FALSE(failedOverlay.UploadTexture(failCreateDevice.Get(), mockQueue.Get(), mockDispatch));
        EXPECT_EQ(mockDispatch->acquireImageCalls, acquiresBefore + 1u);
        EXPECT_FALSE(failedOverlay.HasSwapchain());
        EXPECT_FALSE(failedOverlay.UploadTexture(failCreateDevice.Get(), mockQueue.Get(), mockDispatch));
        EXPECT_EQ(mockDispatch->acquireImageCalls, acquiresBefore + 1u);
    }
}

}
