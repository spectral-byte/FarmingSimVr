#include <gtest/gtest.h>
#include "fake_openxr_events.h"
#include "fake_openxr_runtime.h"
#include "ngx/ngx_defs.h"
#include "ngx/virtual_ngx_handle.h"
#include "ngx/ngx_interceptor.h"
#include "ngx/mv_pass.h"
#include "luau/vr_bridge.h"
#include "proxy/dxgi_proxy.h"
#include "proxy/system_dxgi.h"
#include "openxr/openxr_manager.h"
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "core/logger.h"

namespace fs25vr {
namespace {

static int s_mockCreateCount = 0;
static int s_mockReleaseCount = 0;
static int s_mockEvaluateCount = 0;

static std::vector<const NVSDK_NGX_Handle*> s_evaluatedHandles;
static std::vector<ID3D12GraphicsCommandList*> s_evaluatedCmdLists;
static std::vector<const NVSDK_NGX_Parameter*> s_evaluatedParams;
static std::vector<PFN_NVSDK_NGX_ProgressCallback> s_evaluatedCallbacks;
static std::vector<int> s_evaluatedResetValues;
static std::vector<float> s_evaluatedMvScaleX;
static std::vector<float> s_evaluatedMvScaleY;
static std::vector<NVSDK_NGX_Parameter*> s_createdParams;
static std::vector<ID3D12Resource*> s_evaluatedMotionVectors;

__declspec(noinline) NVSDK_NGX_Result STDMETHODCALLTYPE MockCreateFeature(
    ID3D12GraphicsCommandList* /*InCmdList*/,
    NVSDK_NGX_Feature /*InFeatureID*/,
    NVSDK_NGX_Parameter* InParameters,
    NVSDK_NGX_Handle** OutHandle
) {
    volatile int a = 1;
    volatile int b = 2;
    volatile int c = a + b;
    (void)c;

    s_mockCreateCount++;
    s_createdParams.push_back(InParameters);
    auto handle = new NVSDK_NGX_Handle();
    handle->Id = static_cast<unsigned int>(100 + s_mockCreateCount);
    *OutHandle = handle;
    return NVSDK_NGX_Result_Success;
}

__declspec(noinline) NVSDK_NGX_Result STDMETHODCALLTYPE MockReleaseFeature(
    NVSDK_NGX_Handle* InHandle
) {
    volatile int a = 3;
    volatile int b = 4;
    volatile int c = a + b;
    (void)c;

    s_mockReleaseCount++;
    delete InHandle;
    return NVSDK_NGX_Result_Success;
}

__declspec(noinline) NVSDK_NGX_Result STDMETHODCALLTYPE MockEvaluateFeature(
    ID3D12GraphicsCommandList* InCmdList,
    const NVSDK_NGX_Handle* InFeatureHandle,
    const NVSDK_NGX_Parameter* InParameters,
    PFN_NVSDK_NGX_ProgressCallback InCallback
) {
    volatile int a = 5;
    volatile int b = 6;
    volatile int c = a + b;
    (void)c;

    s_mockEvaluateCount++;
    s_evaluatedCmdLists.push_back(InCmdList);
    s_evaluatedHandles.push_back(InFeatureHandle);
    s_evaluatedParams.push_back(InParameters);
    s_evaluatedCallbacks.push_back(InCallback);

    int resetVal = 0;
    if (InParameters && NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_Reset, &resetVal))) {
        s_evaluatedResetValues.push_back(resetVal);
    } else {
        s_evaluatedResetValues.push_back(0);
    }

    ID3D12Resource* mvRes = nullptr;
    if (InParameters) InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &mvRes);
    s_evaluatedMotionVectors.push_back(mvRes);

    float mvX = 1.0f;
    if (InParameters && NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvX))) {
        s_evaluatedMvScaleX.push_back(mvX);
    } else {
        s_evaluatedMvScaleX.push_back(1.0f);
    }

    float mvY = 1.0f;
    if (InParameters && NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &mvY))) {
        s_evaluatedMvScaleY.push_back(mvY);
    } else {
        s_evaluatedMvScaleY.push_back(1.0f);
    }

    return NVSDK_NGX_Result_Success;
}

class DlssEvaluationTest : public ::testing::Test {
protected:
    // The interceptor reads this bridge; it has no global bridge to fall back to.
    std::shared_ptr<NgxInterceptor> m_ngxShared = std::make_shared<NgxInterceptor>();
    NgxInterceptor& m_ngx = *m_ngxShared;
    VRBridge m_bridge{nullptr, m_ngxShared};

    void SetUp() override {
        s_mockCreateCount = 0;
        s_mockReleaseCount = 0;
        s_mockEvaluateCount = 0;
        s_evaluatedHandles.clear();
        s_evaluatedCmdLists.clear();
        s_evaluatedParams.clear();
        s_evaluatedCallbacks.clear();
        s_evaluatedResetValues.clear();
        s_evaluatedMvScaleX.clear();
        s_evaluatedMvScaleY.clear();
        s_createdParams.clear();
        s_evaluatedMotionVectors.clear();

        m_ngx.DetachAll();
        m_ngx.SetDxgiProxy(nullptr);
        m_ngx.SetOpenXrManager(nullptr);
        m_ngx.SetVRBridge(nullptr);
        m_bridge.SetRenderingMode(VRRenderingMode::AFR);
        m_bridge.SetSubPassIndex(0);
        m_bridge.Set2DViewActive(false);
        m_bridge.SetMenuVisible(false);
        m_bridge.SetFrameParity(0);
        // Stereo presentation (and with it AFR eye alternation) starts at the Companion Mod handshake.
        m_bridge.SetCompanionActive(true);
        m_ngx.SetVRBridge(&m_bridge);
    }

    void TearDown() override {
        m_ngx.DetachAll();
        m_ngx.SetDxgiProxy(nullptr);
        m_ngx.SetOpenXrManager(nullptr);
        m_ngx.SetVRBridge(nullptr);
        m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
        m_bridge.SetSubPassIndex(0);
        m_bridge.Set2DViewActive(false);
        m_bridge.SetMenuVisible(false);
        m_bridge.SetFrameParity(0);
        m_bridge.SetCompanionActive(false);
        s_evaluatedMvScaleX.clear();
        s_evaluatedMvScaleY.clear();
        s_createdParams.clear();
    }
};

TEST_F(DlssEvaluationTest, DynamicDetourInstalledAndUnboxesVirtualHandle) {
    auto& interceptor = m_ngx;
    bool attached = interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);
    ASSERT_TRUE(attached);
    EXPECT_TRUE(interceptor.IsAttached());

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 2560u);
    params.Set(NVSDK_NGX_Parameter_Height, 1440u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    NVSDK_NGX_Result createRes = MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);
    ASSERT_EQ(createRes, NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);
    ASSERT_TRUE(interceptor.IsVirtualHandle(virtualHandle));

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);

    // Evaluate with virtual handle: hook must unbox and forward underlying handle (not virtualHandle)
    auto dummyCmdList = reinterpret_cast<ID3D12GraphicsCommandList*>(0x1234);
    NVSDK_NGX_Result evalRes = MockEvaluateFeature(dummyCmdList, virtualHandle, &params, nullptr);
    EXPECT_EQ(evalRes, NVSDK_NGX_Result_Success);
    EXPECT_EQ(s_mockEvaluateCount, 1);
    ASSERT_EQ(s_evaluatedHandles.size(), 1u);
    EXPECT_EQ(s_evaluatedHandles[0], vHandle->hLeft); // Parity 0 routes to hLeft
    EXPECT_NE(s_evaluatedHandles[0], virtualHandle); // Must not forward virtual handle

    // Evaluate with non-virtual raw handle: hook forwards raw handle directly
    NVSDK_NGX_Handle rawHandle;
    rawHandle.Id = 777;
    evalRes = MockEvaluateFeature(dummyCmdList, &rawHandle, &params, nullptr);
    EXPECT_EQ(evalRes, NVSDK_NGX_Result_Success);
    EXPECT_EQ(s_mockEvaluateCount, 2);
    ASSERT_EQ(s_evaluatedHandles.size(), 2u);
    EXPECT_EQ(s_evaluatedHandles[1], &rawHandle);

    // Evaluate with null handle returns invalid parameter
    evalRes = MockEvaluateFeature(dummyCmdList, nullptr, &params, nullptr);
    EXPECT_EQ(evalRes, NVSDK_NGX_Result_FAIL_InvalidParameter);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, AfrModeAlternatesLeftAndRightHandlesBasedOnFrameParity) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);
    ASSERT_NE(virtualHandle, nullptr);

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);
    ASSERT_NE(vHandle->hLeft, nullptr);
    ASSERT_NE(vHandle->hRight, nullptr);
    ASSERT_NE(vHandle->hLeft, vHandle->hRight);

    auto& bridge = m_bridge;
    bridge.Set2DViewActive(false);
    bridge.SetMenuVisible(false);

    auto dummyCmdList = reinterpret_cast<ID3D12GraphicsCommandList*>(0x5678);

    // Frame 0: Parity 0 -> must evaluate hLeft
    bridge.SetFrameParity(0);
    MockEvaluateFeature(dummyCmdList, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 1u);
    EXPECT_EQ(s_evaluatedHandles[0], vHandle->hLeft);

    // Frame 1: Parity 1 -> must evaluate hRight
    bridge.SetFrameParity(1);
    MockEvaluateFeature(dummyCmdList, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 2u);
    EXPECT_EQ(s_evaluatedHandles[1], vHandle->hRight);

    // Frame 2: Parity 0 -> must evaluate hLeft
    bridge.SetFrameParity(0);
    MockEvaluateFeature(dummyCmdList, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 3u);
    EXPECT_EQ(s_evaluatedHandles[2], vHandle->hLeft);

    // Frame 3: Parity 1 -> must evaluate hRight
    bridge.SetFrameParity(1);
    MockEvaluateFeature(dummyCmdList, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 4u);
    EXPECT_EQ(s_evaluatedHandles[3], vHandle->hRight);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, SingleContextBypassRoutesExclusivelyToLeftHandleIn2DAndMenuModes) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);
    ASSERT_NE(virtualHandle, nullptr);

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);

    auto& bridge = m_bridge;
    auto dummyCmdList = reinterpret_cast<ID3D12GraphicsCommandList*>(0x9999);

    // Case 1: 2D Floating Window Mode is active (Is2DViewActive = true)
    bridge.Set2DViewActive(true);
    bridge.SetMenuVisible(false);

    // With parity 0, routes to hLeft
    bridge.SetFrameParity(0);
    MockEvaluateFeature(dummyCmdList, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 1u);
    EXPECT_EQ(s_evaluatedHandles[0], vHandle->hLeft);

    // With parity 1, Single-Context Bypass MUST route 100% to hLeft (bypassing hRight)
    bridge.SetFrameParity(1);
    MockEvaluateFeature(dummyCmdList, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 2u);
    EXPECT_EQ(s_evaluatedHandles[1], vHandle->hLeft); // NOT hRight!

    // Case 2: GUI Menu Quad Mode is active (IsMenuVisible = true, Is2DViewActive = false)
    bridge.Set2DViewActive(false);
    bridge.SetMenuVisible(true);

    // With parity 0, routes to hLeft
    bridge.SetFrameParity(0);
    MockEvaluateFeature(dummyCmdList, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 3u);
    EXPECT_EQ(s_evaluatedHandles[2], vHandle->hLeft);

    // With parity 1, Single-Context Bypass MUST route 100% to hLeft
    bridge.SetFrameParity(1);
    MockEvaluateFeature(dummyCmdList, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 4u);
    EXPECT_EQ(s_evaluatedHandles[3], vHandle->hLeft); // NOT hRight!

    // Case 3: Transitioning back to 3D AFR mode resumes dual-handle alternating evaluation
    bridge.Set2DViewActive(false);
    bridge.SetMenuVisible(false);

    bridge.SetFrameParity(0);
    MockEvaluateFeature(dummyCmdList, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 5u);
    EXPECT_EQ(s_evaluatedHandles[4], vHandle->hLeft);

    bridge.SetFrameParity(1);
    MockEvaluateFeature(dummyCmdList, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 6u);
    EXPECT_EQ(s_evaluatedHandles[5], vHandle->hRight);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, EndToEndAfrAnd2DFloatingWindowModeCyclesParameterAndCommandListForwarding) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    NgxParameterImpl createParams;
    createParams.Set(NVSDK_NGX_Parameter_Width, 3840u);
    createParams.Set(NVSDK_NGX_Parameter_Height, 2160u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &createParams, &virtualHandle);
    ASSERT_NE(virtualHandle, nullptr);

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);

    auto& bridge = m_bridge;

    // Prepare simulated frames
    struct FrameConfig {
        uint64_t frameIndex;
        bool is2DMode;
        bool isMenuMode;
        uint32_t expectedParity;
        const NVSDK_NGX_Handle* expectedTarget;
        float jitterX;
        float jitterY;
        int resetDiscontinuity;
    };

    std::vector<FrameConfig> simulationFrames = {
        // Phase 1: 3D AFR Mode
        { 0, false, false, 0, vHandle->hLeft,  -0.35f,  0.20f, 0 },
        { 1, false, false, 1, vHandle->hRight,  0.35f, -0.20f, 0 },
        { 2, false, false, 0, vHandle->hLeft,  -0.10f,  0.15f, 0 },
        // Snap turn / teleport: Discontinuity flush (Reset = 1)
        { 3, false, false, 1, vHandle->hRight,  0.10f, -0.15f, 1 },

        // Phase 2: User activates 2D Floating Window Mode (Single-Context Bypass)
        { 4, true,  false, 0, vHandle->hLeft,   0.00f,  0.00f, 0 },
        { 5, true,  false, 1, vHandle->hLeft,   0.00f,  0.00f, 0 },

        // Phase 3: In-game GUI menu opened (Single-Context Bypass)
        { 6, false, true,  0, vHandle->hLeft,   0.00f,  0.00f, 0 },
        { 7, false, true,  1, vHandle->hLeft,   0.00f,  0.00f, 0 },

        // Phase 4: Resume 3D AFR Mode
        { 8, false, false, 0, vHandle->hLeft,  -0.25f,  0.10f, 0 },
        { 9, false, false, 1, vHandle->hRight,  0.25f, -0.10f, 0 }
    };

    for (size_t i = 0; i < simulationFrames.size(); ++i) {
        const auto& cfg = simulationFrames[i];
        bridge.Set2DViewActive(cfg.is2DMode);
        bridge.SetMenuVisible(cfg.isMenuMode);
        bridge.SetFrameIndex(cfg.frameIndex);

        auto dummyCmdList = reinterpret_cast<ID3D12GraphicsCommandList*>(0x1000 + i);

        NgxParameterImpl evalParams;
        evalParams.Set(NVSDK_NGX_Parameter_Jitter_Offset_X, cfg.jitterX);
        evalParams.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, cfg.jitterY);
        evalParams.Set(NVSDK_NGX_Parameter_Reset, cfg.resetDiscontinuity);
        evalParams.Set(NVSDK_NGX_Parameter_Sharpness, 0.5f);

        NVSDK_NGX_Result res = MockEvaluateFeature(dummyCmdList, virtualHandle, &evalParams, nullptr);
        EXPECT_EQ(res, NVSDK_NGX_Result_Success);

        ASSERT_EQ(s_evaluatedHandles.size(), i + 1);
        ASSERT_EQ(s_evaluatedCmdLists.size(), i + 1);
        ASSERT_EQ(s_evaluatedParams.size(), i + 1);

        // Verify correct handle routing
        EXPECT_EQ(s_evaluatedHandles[i], cfg.expectedTarget)
            << "Frame index " << cfg.frameIndex << " failed expected handle routing.";

        // Verify command list forwarding
        EXPECT_EQ(s_evaluatedCmdLists[i], dummyCmdList)
            << "Frame index " << cfg.frameIndex << " failed command list forwarding.";

        // Verify parameter forwarding untouched
        const NVSDK_NGX_Parameter* fwdParams = s_evaluatedParams[i];
        ASSERT_NE(fwdParams, nullptr);

        float fwdJitterX = 0.0f, fwdJitterY = 0.0f, fwdSharpness = 0.0f;
        int fwdReset = -1;
        EXPECT_EQ(fwdParams->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &fwdJitterX), NVSDK_NGX_Result_Success);
        EXPECT_EQ(fwdParams->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &fwdJitterY), NVSDK_NGX_Result_Success);
        EXPECT_EQ(fwdParams->Get(NVSDK_NGX_Parameter_Reset, &fwdReset), NVSDK_NGX_Result_Success);
        EXPECT_EQ(fwdParams->Get(NVSDK_NGX_Parameter_Sharpness, &fwdSharpness), NVSDK_NGX_Result_Success);

        EXPECT_FLOAT_EQ(fwdJitterX, cfg.jitterX);
        EXPECT_FLOAT_EQ(fwdJitterY, cfg.jitterY);
        EXPECT_EQ(fwdReset, cfg.resetDiscontinuity);
        EXPECT_FLOAT_EQ(fwdSharpness, 0.5f);
    }

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, CustomVrBridgeDependencyInjectionSupported) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    NgxParameterImpl params;
    NVSDK_NGX_Handle* virtualHandle = nullptr;
    MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);
    ASSERT_NE(virtualHandle, nullptr);

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);

    // Fixture bridge is configured in 3D AFR parity 0
    m_bridge.Set2DViewActive(false);
    m_bridge.SetMenuVisible(false);
    m_bridge.SetFrameParity(0);

    // Create a local isolated VRBridge configured in 2D Floating Window Mode
    VRBridge customBridge(nullptr, nullptr);
    customBridge.SetRenderingMode(VRRenderingMode::AFR);
    customBridge.Set2DViewActive(true);
    customBridge.SetFrameParity(1);

    interceptor.SetVRBridge(&customBridge);
    EXPECT_EQ(interceptor.GetVRBridge(), &customBridge);

    // Should evaluate through custom bridge in 2D Floating Window Mode -> routes to hLeft
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 1u);
    EXPECT_EQ(s_evaluatedHandles[0], vHandle->hLeft);

    // Change custom bridge to 3D AFR parity 1 -> routes to hRight
    customBridge.Set2DViewActive(false);
    customBridge.SetFrameParity(1);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedHandles.size(), 2u);
    EXPECT_EQ(s_evaluatedHandles[1], vHandle->hRight);

    // Reset injection -> no bridge, and no global fallback
    interceptor.SetVRBridge(nullptr);
    EXPECT_EQ(interceptor.GetVRBridge(), nullptr);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, DiscontinuityFlushInjectsResetForExactlyOneFrameAcrossBothContextsInAfr) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    NgxParameterImpl params;
    NVSDK_NGX_Handle* virtualHandle = nullptr;
    MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);
    ASSERT_NE(virtualHandle, nullptr);

    auto& bridge = m_bridge;
    bridge.Set2DViewActive(false);
    bridge.SetMenuVisible(false);

    // Initial state: normal evaluations have Reset = 0
    bridge.SetFrameParity(0);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedResetValues.size(), 1u);
    EXPECT_EQ(s_evaluatedResetValues[0], 0);

    bridge.SetFrameParity(1);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedResetValues.size(), 2u);
    EXPECT_EQ(s_evaluatedResetValues[1], 0);

    // Now trigger discontinuity flush
    bridge.SignalDiscontinuity();

    // Frame 0 (Parity 0 -> Left context): Reset MUST be 1
    bridge.SetFrameParity(0);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedResetValues.size(), 3u);
    EXPECT_EQ(s_evaluatedResetValues[2], 1);

    // After evaluation, params must have Reset restored (not permanently set to 1)
    int remainingReset = -1;
    params.Get(NVSDK_NGX_Parameter_Reset, &remainingReset);
    EXPECT_EQ(remainingReset, 0);

    // Frame 1 (Parity 1 -> Right context): Reset MUST be 1
    bridge.SetFrameParity(1);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedResetValues.size(), 4u);
    EXPECT_EQ(s_evaluatedResetValues[3], 1);

    params.Get(NVSDK_NGX_Parameter_Reset, &remainingReset);
    EXPECT_EQ(remainingReset, 0);

    // Frame 2 (Parity 0 -> Left context): Reset MUST be 0 (exactly one frame elapsed!)
    bridge.SetFrameParity(0);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedResetValues.size(), 5u);
    EXPECT_EQ(s_evaluatedResetValues[4], 0);

    // Frame 3 (Parity 1 -> Right context): Reset MUST be 0 (exactly one frame elapsed!)
    bridge.SetFrameParity(1);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedResetValues.size(), 6u);
    EXPECT_EQ(s_evaluatedResetValues[5], 0);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, DiscontinuityFlushInSingleContextBypassInjectsResetForExactlyOneFrameOnLeftContext) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    NgxParameterImpl params;
    NVSDK_NGX_Handle* virtualHandle = nullptr;
    MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);
    ASSERT_NE(virtualHandle, nullptr);

    auto& bridge = m_bridge;
    // 2D Floating Window Mode is active
    bridge.Set2DViewActive(true);
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    // Trigger discontinuity flush
    bridge.SignalDiscontinuity();

    // Frame 0: in 2D mode, evaluated on hLeft -> Reset MUST be 1
    bridge.SetFrameParity(0);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedResetValues.size(), 1u);
    EXPECT_EQ(s_evaluatedResetValues[0], 1);

    // Frame 1: subsequent frame in 2D mode -> Reset MUST be 0
    bridge.SetFrameParity(1); // even if parity changes, in 2D mode it routes to hLeft
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedResetValues.size(), 2u);
    EXPECT_EQ(s_evaluatedResetValues[1], 0);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, ViewTransitionAndLocomotionPropagatesResetToDlssEvaluate) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    NgxParameterImpl params;
    NVSDK_NGX_Handle* virtualHandle = nullptr;
    MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);
    ASSERT_NE(virtualHandle, nullptr);

    auto& bridge = m_bridge;
    bridge.Set2DViewActive(false);
    bridge.SetMenuVisible(false);
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    // 1. Recenter Action
    bridge.Recenter();
    bridge.SetFrameParity(0);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    EXPECT_EQ(s_evaluatedResetValues.back(), 1);

    bridge.SetFrameParity(1);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    EXPECT_EQ(s_evaluatedResetValues.back(), 1);

    // Subsequent frame is clean
    bridge.SetFrameParity(0);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    EXPECT_EQ(s_evaluatedResetValues.back(), 0);

    // 2. Mode Toggle (Switching to 2D Mode)
    bridge.Set2DViewActive(true);
    bridge.SetFrameParity(0);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    EXPECT_EQ(s_evaluatedResetValues.back(), 1);

    // Subsequent frame is clean
    bridge.SetFrameParity(0);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    EXPECT_EQ(s_evaluatedResetValues.back(), 0);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, DeriveDlssRenderResolutionScalesAcrossAllQualityModes) {
    const uint32_t targetW = 3840;
    const uint32_t targetH = 2160;

    // MaxPerformance: 0.5x scaling
    auto maxPerf = DeriveDlssRenderResolution(targetW, targetH, NVSDK_NGX_PerfQuality_Value_MaxPerf);
    EXPECT_EQ(maxPerf.renderWidth, 1920u);
    EXPECT_EQ(maxPerf.renderHeight, 1080u);
    EXPECT_FLOAT_EQ(maxPerf.scaleFactor, 0.5f);

    // Balanced: 1.0 / 1.7 scaling (~0.588235x)
    auto balanced = DeriveDlssRenderResolution(targetW, targetH, NVSDK_NGX_PerfQuality_Value_Balanced);
    EXPECT_EQ(balanced.renderWidth, static_cast<uint32_t>(std::round(targetW / 1.7f)));
    EXPECT_EQ(balanced.renderHeight, static_cast<uint32_t>(std::round(targetH / 1.7f)));
    EXPECT_FLOAT_EQ(balanced.scaleFactor, 1.0f / 1.7f);

    // MaxQuality: 2.0 / 3.0 scaling (~0.666667x)
    auto maxQuality = DeriveDlssRenderResolution(targetW, targetH, NVSDK_NGX_PerfQuality_Value_MaxQuality);
    EXPECT_EQ(maxQuality.renderWidth, 2560u);
    EXPECT_EQ(maxQuality.renderHeight, 1440u);
    EXPECT_FLOAT_EQ(maxQuality.scaleFactor, 2.0f / 3.0f);

    // UltraPerformance: 1.0 / 3.0 scaling (~0.333333x)
    auto ultraPerf = DeriveDlssRenderResolution(targetW, targetH, NVSDK_NGX_PerfQuality_Value_UltraPerformance);
    EXPECT_EQ(ultraPerf.renderWidth, 1280u);
    EXPECT_EQ(ultraPerf.renderHeight, 720u);
    EXPECT_FLOAT_EQ(ultraPerf.scaleFactor, 1.0f / 3.0f);

    // UltraQuality: 1.0 / 1.3 scaling (~0.769231x)
    auto ultraQuality = DeriveDlssRenderResolution(targetW, targetH, NVSDK_NGX_PerfQuality_Value_UltraQuality);
    EXPECT_EQ(ultraQuality.renderWidth, static_cast<uint32_t>(std::round(targetW / 1.3f)));
    EXPECT_EQ(ultraQuality.renderHeight, static_cast<uint32_t>(std::round(targetH / 1.3f)));
    EXPECT_FLOAT_EQ(ultraQuality.scaleFactor, 1.0f / 1.3f);

    // DLAA: 1.0x native resolution
    auto dlaa = DeriveDlssRenderResolution(targetW, targetH, NVSDK_NGX_PerfQuality_Value_DLAA);
    EXPECT_EQ(dlaa.renderWidth, 3840u);
    EXPECT_EQ(dlaa.renderHeight, 2160u);
    EXPECT_FLOAT_EQ(dlaa.scaleFactor, 1.0f);

    // Unknown quality value: gracefully defaults to 1.0f (native/DLAA, no downscale artifacts)
    auto fallback = DeriveDlssRenderResolution(targetW, targetH, static_cast<NVSDK_NGX_PerfQuality_Value>(999));
    EXPECT_EQ(fallback.renderWidth, 3840u);
    EXPECT_EQ(fallback.renderHeight, 2160u);
    EXPECT_FLOAT_EQ(fallback.scaleFactor, 1.0f);

    // Degenerate inputs (0x0): returns zeros
    auto zero = DeriveDlssRenderResolution(0, 0, NVSDK_NGX_PerfQuality_Value_MaxQuality);
    EXPECT_EQ(zero.renderWidth, 0u);
    EXPECT_EQ(zero.renderHeight, 0u);
}

TEST_F(DlssEvaluationTest, DecoupledTargetResolutionOverridesCreateFeatureParameters) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    auto systemDxgi = std::make_shared<SystemDxgi>();
    FakeTrackedHeadset headset;  // Pass-Through proxies are ignored, so the proxy needs an active session
    DxgiProxy proxy(systemDxgi, headset.manager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    proxy.GetWindowController().SetTargetRenderResolution(3840, 2160);
    interceptor.SetDxgiProxy(&proxy);

    uint32_t targetW = 0, targetH = 0;
    interceptor.GetTargetUpscalingDimensions(targetW, targetH);
    EXPECT_EQ(targetW, 3840u);
    EXPECT_EQ(targetH, 2160u);

    // The game passes desktop mirror window dimensions (e.g. 1920x1080)
    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality));

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    NVSDK_NGX_Result res = MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);
    ASSERT_EQ(res, NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);

    // Target dimensions stored on virtual handle must be decoupled target (3840x2160)
    EXPECT_EQ(vHandle->targetWidth, 3840u);
    EXPECT_EQ(vHandle->targetHeight, 2160u);
    // Render dimensions derived with MaxQuality (2/3) must be 2560x1440
    EXPECT_EQ(vHandle->renderWidth, 2560u);
    EXPECT_EQ(vHandle->renderHeight, 1440u);
    EXPECT_EQ(vHandle->perfQuality, NVSDK_NGX_PerfQuality_Value_MaxQuality);

    // Parameters passed to origCreate must preserve original parameters untouched without corruption
    ASSERT_FALSE(s_createdParams.empty());
    NVSDK_NGX_Parameter* passedParams = s_createdParams.back();
    ASSERT_NE(passedParams, nullptr);

    unsigned int passedW = 0, passedH = 0;
    int passedQuality = 0;

    EXPECT_EQ(passedParams->Get(NVSDK_NGX_Parameter_Width, &passedW), NVSDK_NGX_Result_Success);
    EXPECT_EQ(passedParams->Get(NVSDK_NGX_Parameter_Height, &passedH), NVSDK_NGX_Result_Success);
    EXPECT_EQ(passedParams->Get(NVSDK_NGX_Parameter_PerfQualityValue, &passedQuality), NVSDK_NGX_Result_Success);

    EXPECT_EQ(passedW, 1920u);
    EXPECT_EQ(passedH, 1080u);
    EXPECT_EQ(passedQuality, static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality));

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, DecoupledResolutionEnforcedDuringEvaluateFeature) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    auto systemDxgi = std::make_shared<SystemDxgi>();
    FakeTrackedHeadset headset;  // Pass-Through proxies are ignored, so the proxy needs an active session
    DxgiProxy proxy(systemDxgi, headset.manager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    proxy.GetWindowController().SetTargetRenderResolution(3840, 2160);
    interceptor.SetDxgiProxy(&proxy);

    NgxParameterImpl createParams;
    createParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    createParams.Set(NVSDK_NGX_Parameter_Height, 1080u);
    createParams.Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality));

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &createParams, &virtualHandle);
    ASSERT_NE(virtualHandle, nullptr);

    // During evaluation, the game supplies its per-frame parameters
    NgxParameterImpl evalParams;
    evalParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    evalParams.Set(NVSDK_NGX_Parameter_Height, 1080u);
    evalParams.Set(NVSDK_NGX_Parameter_InWidth, 1280u);
    evalParams.Set(NVSDK_NGX_Parameter_InHeight, 720u);

    NVSDK_NGX_Result evalRes = MockEvaluateFeature(nullptr, virtualHandle, &evalParams, nullptr);
    EXPECT_EQ(evalRes, NVSDK_NGX_Result_Success);

    ASSERT_FALSE(s_evaluatedParams.empty());
    const NVSDK_NGX_Parameter* fwdParams = s_evaluatedParams.back();
    ASSERT_NE(fwdParams, nullptr);

    unsigned int evalW = 0, evalH = 0;
    unsigned int evalInW = 0, evalInH = 0;

    EXPECT_EQ(fwdParams->Get(NVSDK_NGX_Parameter_Width, &evalW), NVSDK_NGX_Result_Success);
    EXPECT_EQ(fwdParams->Get(NVSDK_NGX_Parameter_Height, &evalH), NVSDK_NGX_Result_Success);
    EXPECT_EQ(fwdParams->Get(NVSDK_NGX_Parameter_InWidth, &evalInW), NVSDK_NGX_Result_Success);
    EXPECT_EQ(fwdParams->Get(NVSDK_NGX_Parameter_InHeight, &evalInH), NVSDK_NGX_Result_Success);

    // Evaluated parameters forwarded to native DLSS preserve the engine's textures and bounds untouched
    EXPECT_EQ(evalW, 1920u);
    EXPECT_EQ(evalH, 1080u);
    EXPECT_EQ(evalInW, 1280u);
    EXPECT_EQ(evalInH, 720u);

    MockReleaseFeature(virtualHandle);
}

class MockDlssOpenXrDispatch : public IOpenXrDispatch {
public:
    uint32_t mockWidth = 2160;
    uint32_t mockHeight = 2160;

    XrResult EnumerateInstanceExtensionProperties(const char*, uint32_t propertyCapacityInput, uint32_t* propertyCountOutput, XrExtensionProperties* properties) override {
        if (propertyCountOutput) *propertyCountOutput = 1;
        if (propertyCapacityInput > 0 && properties) {
            properties[0].type = XR_TYPE_EXTENSION_PROPERTIES;
            properties[0].next = nullptr;
            strncpy_s(properties[0].extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME, XR_MAX_EXTENSION_NAME_SIZE - 1);
            properties[0].extensionVersion = 1;
        }
        return XR_SUCCESS;
    }
    XrResult CreateInstance(const XrInstanceCreateInfo*, XrInstance* instance) override {
        if (instance) *instance = reinterpret_cast<XrInstance>(static_cast<uintptr_t>(0xDEADBEEF));
        return XR_SUCCESS;
    }
    XrResult DestroyInstance(XrInstance) override { return XR_SUCCESS; }
    XrResult GetInstanceProperties(XrInstance, XrInstanceProperties* props) override {
        if (props) {
            strncpy_s(props->runtimeName, "MockVR Runtime", XR_MAX_RUNTIME_NAME_SIZE - 1);
            props->runtimeVersion = XR_MAKE_VERSION(1, 0, 0);
        }
        return XR_SUCCESS;
    }
    XrResult GetSystem(XrInstance, const XrSystemGetInfo*, XrSystemId* systemId) override {
        if (systemId) *systemId = 12345;
        return XR_SUCCESS;
    }
    XrResult GetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* props) override {
        if (props) {
            props->systemId = 12345;
            props->trackingProperties.orientationTracking = XR_TRUE;
            props->trackingProperties.positionTracking = XR_TRUE;
        }
        return XR_SUCCESS;
    }
    XrResult EnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrViewConfigurationView* configurationViews) override {
        if (viewCountOutput) *viewCountOutput = 2;
        if (viewCapacityInput >= 2 && configurationViews) {
            for (uint32_t i = 0; i < 2; ++i) {
                configurationViews[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
                configurationViews[i].next = nullptr;
                configurationViews[i].recommendedImageRectWidth = mockWidth;
                configurationViews[i].recommendedImageRectHeight = mockHeight;
                configurationViews[i].maxImageRectWidth = 4096;
                configurationViews[i].maxImageRectHeight = 4096;
                configurationViews[i].recommendedSwapchainSampleCount = 1;
                configurationViews[i].maxSwapchainSampleCount = 4;
            }
        }
        return XR_SUCCESS;
    }
    XrResult GetD3D12GraphicsRequirementsKHR(XrInstance, XrSystemId, XrGraphicsRequirementsD3D12KHR* reqs) override {
        if (reqs) {
            reqs->minFeatureLevel = D3D_FEATURE_LEVEL_12_0;
            reqs->adapterLuid = { 0, 0 };
        }
        return XR_SUCCESS;
    }
    // Fake runtime event queue; a created session reports READY like a real runtime would.
    FakeOpenXrEventQueue events;
    XrResult PollEvent(XrInstance, XrEventDataBuffer* eventData) override { return events.Poll(eventData); }
    XrResult CreateSession(XrInstance, const XrSessionCreateInfo*, XrSession* session) override {
        if (session) *session = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0xCAFE0001));
        events.PushSessionState(XR_SESSION_STATE_READY);
        return XR_SUCCESS;
    }
    XrResult BeginSession(XrSession, const XrSessionBeginInfo*) override { return XR_SUCCESS; }
    XrResult EndSession(XrSession) override { return XR_SUCCESS; }
    XrResult DestroySession(XrSession) override { return XR_SUCCESS; }
    XrResult CreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo*, XrSpace* space) override {
        if (space) *space = reinterpret_cast<XrSpace>(static_cast<uintptr_t>(0x50ACE001));
        return XR_SUCCESS;
    }
    XrResult DestroySpace(XrSpace) override { return XR_SUCCESS; }
    XrResult LocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation* location) override {
        if (location) {
            location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
            location->pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
            location->pose.position = { 0.0f, 1.5f, 0.0f };
        }
        return XR_SUCCESS;
    }
    XrResult LocateViews(XrSession, const XrViewLocateInfo*, XrViewState* viewState, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views) override {
        if (viewCountOutput) *viewCountOutput = 2;
        if (viewState) {
            viewState->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
        }
        if (viewCapacityInput >= 2 && views) {
            for (uint32_t i = 0; i < 2; ++i) {
                views[i].type = XR_TYPE_VIEW;
                views[i].next = nullptr;
                views[i].pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
                views[i].pose.position = { (i == 0 ? -0.032f : 0.032f), 1.5f, 0.0f };
                views[i].fov = { -0.90f, 0.80f, 0.85f, -0.85f };
            }
        }
        return XR_SUCCESS;
    }
    XrResult EnumerateSwapchainFormats(XrSession, uint32_t formatCapacityInput, uint32_t* formatCountOutput, int64_t* formats) override {
        if (formatCountOutput) *formatCountOutput = 1;
        if (formatCapacityInput > 0 && formats) {
            formats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        }
        return XR_SUCCESS;
    }
    XrResult CreateSwapchain(XrSession, const XrSwapchainCreateInfo*, XrSwapchain* swapchain) override {
        static uintptr_t s_chain = 0x54AC0001;
        if (swapchain) *swapchain = reinterpret_cast<XrSwapchain>(s_chain++);
        return XR_SUCCESS;
    }
    XrResult DestroySwapchain(XrSwapchain) override { return XR_SUCCESS; }
    XrResult EnumerateSwapchainImages(XrSwapchain, uint32_t imageCapacityInput, uint32_t* imageCountOutput, XrSwapchainImageBaseHeader* images) override {
        if (imageCountOutput) *imageCountOutput = 2;
        if (imageCapacityInput >= 2 && images) {
            for (uint32_t i = 0; i < 2; ++i) {
                auto* d3d12Image = reinterpret_cast<XrSwapchainImageD3D12KHR*>(&images[i]);
                d3d12Image->type = XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR;
                d3d12Image->next = nullptr;
                d3d12Image->texture = nullptr;
            }
        }
        return XR_SUCCESS;
    }
    XrResult AcquireSwapchainImage(XrSwapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index) override {
        if (index) *index = 0;
        return XR_SUCCESS;
    }
    XrResult WaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo*) override { return XR_SUCCESS; }
    XrResult ReleaseSwapchainImage(XrSwapchain, const XrSwapchainImageReleaseInfo*) override { return XR_SUCCESS; }
    XrResult WaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState*) override { return XR_SUCCESS; }
    XrResult BeginFrame(XrSession, const XrFrameBeginInfo*) override { return XR_SUCCESS; }
    XrResult EndFrame(XrSession, const XrFrameEndInfo*) override { return XR_SUCCESS; }
};

TEST_F(DlssEvaluationTest, ResolutionNegotiationMatchesOpenXrRecommendedEyeDimensions1To1) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    auto mockDispatch = std::make_shared<MockDlssOpenXrDispatch>();
    mockDispatch->mockWidth = 2160;
    mockDispatch->mockHeight = 2160;

    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->IsActive());

    auto systemDxgi = std::make_shared<SystemDxgi>();
    DxgiProxy proxy(systemDxgi, openXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    interceptor.SetDxgiProxy(&proxy);

    // Verify proxy queries OpenXR recommended eye dimensions
    uint32_t decoupledW = 0, decoupledH = 0;
    proxy.GetDecoupledTargetResolution(decoupledW, decoupledH);
    EXPECT_EQ(decoupledW, 2160u);
    EXPECT_EQ(decoupledH, 2160u);

    // Verify interceptor queries decoupled dimensions from proxy
    uint32_t targetW = 0, targetH = 0;
    interceptor.GetTargetUpscalingDimensions(targetW, targetH);
    EXPECT_EQ(targetW, 2160u);
    EXPECT_EQ(targetH, 2160u);

    // Game attempts to create DLSS with 1080p desktop mirror dimensions and Balanced quality
    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced));

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    NVSDK_NGX_Result createRes = MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);
    ASSERT_EQ(createRes, NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);

    // DLSS target must be decoupled to exactly 2160x2160 (OpenXR recommended per eye)
    EXPECT_EQ(vHandle->targetWidth, 2160u);
    EXPECT_EQ(vHandle->targetHeight, 2160u);

    // DLSS internal render resolution scaled with Balanced mode (~1/1.7 = 1271x1271)
    EXPECT_EQ(vHandle->renderWidth, 1271u);
    EXPECT_EQ(vHandle->renderHeight, 1271u);

    // OpenXR eye swapchain creation
    ASSERT_TRUE(openXrManager->CreateSession());
    ASSERT_TRUE(openXrManager->CreateEyeSwapchains());
    ASSERT_TRUE(openXrManager->HasEyeSwapchains());
    const auto& eyeSwapchain = openXrManager->GetEyeSwapchain(0);

    // Direct 1:1 match verification: upscaled output matches OpenXR eye swapchain 1:1
    EXPECT_EQ(vHandle->targetWidth, eyeSwapchain.width);
    EXPECT_EQ(vHandle->targetHeight, eyeSwapchain.height);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, TargetResolutionPrefersSwapchainRenderResolutionOverOpenXrRecommendedView) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    auto mockDispatch = std::make_shared<MockDlssOpenXrDispatch>();
    mockDispatch->mockWidth = 4940;
    mockDispatch->mockHeight = 4136;

    auto openXrManager = std::make_shared<OpenXrManager>(mockDispatch);
    ASSERT_TRUE(openXrManager->Initialize());
    ASSERT_TRUE(openXrManager->IsActive());

    auto systemDxgi = std::make_shared<SystemDxgi>();
    DxgiProxy proxy(systemDxgi, openXrManager, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    proxy.GetWindowController().SetTargetRenderResolution(1920, 1080);
    interceptor.SetDxgiProxy(&proxy);

    // Target render resolution (1920x1080) MUST take precedence over supersampled HMD view (4940x4136)
    uint32_t decoupledW = 0, decoupledH = 0;
    proxy.GetDecoupledTargetResolution(decoupledW, decoupledH);
    EXPECT_EQ(decoupledW, 1920u);
    EXPECT_EQ(decoupledH, 1080u);

    uint32_t targetW = 0, targetH = 0;
    interceptor.GetTargetUpscalingDimensions(targetW, targetH);
    EXPECT_EQ(targetW, 1920u);
    EXPECT_EQ(targetH, 1080u);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality));

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    NVSDK_NGX_Result createRes = MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);
    ASSERT_EQ(createRes, NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);

    EXPECT_EQ(vHandle->targetWidth, 1920u);
    EXPECT_EQ(vHandle->targetHeight, 1080u);
    EXPECT_EQ(vHandle->renderWidth, 1280u);
    EXPECT_EQ(vHandle->renderHeight, 720u);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, ExposureAndTonemapperParametersMirroredAcrossBothHandles) {
    NgxParameterImpl source;
    source.Set(NVSDK_NGX_Parameter_Width, 1920u);
    source.Set(NVSDK_NGX_Parameter_Height, 1080u);
    source.Set(NVSDK_NGX_Parameter_PreExposure, 1.5f);
    source.Set(NVSDK_NGX_Parameter_TonemapperType, 2);
    source.Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, 11);

    NgxParameterImpl clone;
    clone.CopyFrom(&source);

    float preExp = 0.0f;
    int tonemapper = 0;
    int preset = 0;

    EXPECT_EQ(clone.Get(NVSDK_NGX_Parameter_PreExposure, &preExp), NVSDK_NGX_Result_Success);
    EXPECT_EQ(clone.Get(NVSDK_NGX_Parameter_TonemapperType, &tonemapper), NVSDK_NGX_Result_Success);
    EXPECT_EQ(clone.Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, &preset), NVSDK_NGX_Result_Success);

    EXPECT_FLOAT_EQ(preExp, 1.5f);
    EXPECT_EQ(tonemapper, 2);
    EXPECT_EQ(preset, 11);
}

TEST_F(DlssEvaluationTest, DlssPresetAndSharpnessOverridesAppliedDuringEvaluateFeature) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(MockCreateFeature, MockReleaseFeature, MockEvaluateFeature);
    interceptor.SetDlssPresetOverride(NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    interceptor.SetSharpnessOverride(0.35f);

    NgxParameterImpl createParams;
    createParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    createParams.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(interceptor.Intercept_CreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &createParams, &virtualHandle), NVSDK_NGX_Result_Success);

    NgxParameterImpl evalParams;
    evalParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    evalParams.Set(NVSDK_NGX_Parameter_Height, 1080u);

    ASSERT_EQ(interceptor.Intercept_EvaluateFeature(nullptr, virtualHandle, &evalParams, nullptr), NVSDK_NGX_Result_Success);

    ASSERT_EQ(s_evaluatedParams.size(), 1u);
    int evaluatedPreset = 0;
    float evaluatedSharpness = 0.0f;
    EXPECT_EQ(s_evaluatedParams[0]->Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, &evaluatedPreset), NVSDK_NGX_Result_Success);
    EXPECT_EQ(s_evaluatedParams[0]->Get(NVSDK_NGX_Parameter_Sharpness, &evaluatedSharpness), NVSDK_NGX_Result_Success);
    EXPECT_EQ(evaluatedPreset, static_cast<int>(NVSDK_NGX_DLSS_Hint_Render_Preset_K));
    EXPECT_FLOAT_EQ(evaluatedSharpness, 0.35f);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, PreservesExplicitCallerSharpnessAndPreset) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(MockCreateFeature, MockReleaseFeature, MockEvaluateFeature);
    interceptor.SetDlssPresetOverride(NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    interceptor.SetSharpnessOverride(0.35f, false);

    NgxParameterImpl createParams;
    createParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    createParams.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(interceptor.Intercept_CreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &createParams, &virtualHandle), NVSDK_NGX_Result_Success);

    NgxParameterImpl evalParams;
    evalParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    evalParams.Set(NVSDK_NGX_Parameter_Height, 1080u);
    evalParams.Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, 3);
    evalParams.Set(NVSDK_NGX_Parameter_Sharpness, 0.75f);

    ASSERT_EQ(interceptor.Intercept_EvaluateFeature(nullptr, virtualHandle, &evalParams, nullptr), NVSDK_NGX_Result_Success);

    ASSERT_EQ(s_evaluatedParams.size(), 1u);
    int evaluatedPreset = 0;
    float evaluatedSharpness = 0.0f;
    EXPECT_EQ(s_evaluatedParams[0]->Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, &evaluatedPreset), NVSDK_NGX_Result_Success);
    EXPECT_EQ(s_evaluatedParams[0]->Get(NVSDK_NGX_Parameter_Sharpness, &evaluatedSharpness), NVSDK_NGX_Result_Success);
    EXPECT_EQ(evaluatedPreset, 3);
    EXPECT_FLOAT_EQ(evaluatedSharpness, 0.75f);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, MotionVectorScaleMultiplierAppliedDuringEvaluate) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(MockCreateFeature, MockReleaseFeature, MockEvaluateFeature);
    interceptor.SetMvScaleMultiplier(0.5f, 1.0f);

    NgxParameterImpl createParams;
    createParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    createParams.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(interceptor.Intercept_CreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &createParams, &virtualHandle), NVSDK_NGX_Result_Success);

    NgxParameterImpl evalParams;
    evalParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    evalParams.Set(NVSDK_NGX_Parameter_Height, 1080u);
    evalParams.Set(NVSDK_NGX_Parameter_MV_Scale_X, 2.0f);
    evalParams.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 2.0f);

    ASSERT_EQ(interceptor.Intercept_EvaluateFeature(nullptr, virtualHandle, &evalParams, nullptr), NVSDK_NGX_Result_Success);

    ASSERT_EQ(s_evaluatedMvScaleX.size(), 1u);
    ASSERT_EQ(s_evaluatedMvScaleY.size(), 1u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[0], 1.0f);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[0], 2.0f);

    // Verify caller's params were restored after evaluation
    float restoredX = 0.0f, restoredY = 0.0f;
    EXPECT_EQ(evalParams.Get(NVSDK_NGX_Parameter_MV_Scale_X, &restoredX), NVSDK_NGX_Result_Success);
    EXPECT_EQ(evalParams.Get(NVSDK_NGX_Parameter_MV_Scale_Y, &restoredY), NVSDK_NGX_Result_Success);
    EXPECT_FLOAT_EQ(restoredX, 2.0f);
    EXPECT_FLOAT_EQ(restoredY, 2.0f);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, AfrDefaultsMotionVectorScaleTo2xWithoutOverride) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(MockCreateFeature, MockReleaseFeature, &MockEvaluateFeature);
    interceptor.SetVRBridge(&m_bridge);
    ASSERT_EQ(m_bridge.GetRenderingMode(), VRRenderingMode::AFR);

    NgxParameterImpl createParams;
    createParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    createParams.Set(NVSDK_NGX_Parameter_Height, 1080u);
    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(interceptor.Intercept_CreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &createParams, &virtualHandle), NVSDK_NGX_Result_Success);

    NgxParameterImpl evalParams;
    evalParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    evalParams.Set(NVSDK_NGX_Parameter_Height, 1080u);
    evalParams.Set(NVSDK_NGX_Parameter_MV_Scale_X, 0.5f);
    evalParams.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 0.25f);

    ASSERT_EQ(interceptor.Intercept_EvaluateFeature(nullptr, virtualHandle, &evalParams, nullptr), NVSDK_NGX_Result_Success);

    ASSERT_EQ(s_evaluatedMvScaleX.size(), 1u);
    ASSERT_EQ(s_evaluatedMvScaleY.size(), 1u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[0], 1.0f);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[0], 0.5f);

    float restoredX = 0.0f, restoredY = 0.0f;
    EXPECT_EQ(evalParams.Get(NVSDK_NGX_Parameter_MV_Scale_X, &restoredX), NVSDK_NGX_Result_Success);
    EXPECT_EQ(evalParams.Get(NVSDK_NGX_Parameter_MV_Scale_Y, &restoredY), NVSDK_NGX_Result_Success);
    EXPECT_FLOAT_EQ(restoredX, 0.5f);
    EXPECT_FLOAT_EQ(restoredY, 0.25f);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, AfrDefaultScaleDoesNotCompoundOnReusedParametersWithoutMvScale) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);
    auto& bridge = m_bridge;
    interceptor.SetVRBridge(&bridge);

    // The game never sets MV.Scale and reuses the same parameter block every frame
    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);

    for (uint32_t frame = 0; frame < 3; ++frame) {
        bridge.SetFrameParity(frame % 2);
        EXPECT_EQ(MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    }
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[i], NgxInterceptor::kDefaultAfrMvScale);
        EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[i], NgxInterceptor::kDefaultAfrMvScale);
    }

    // Switching to SDP on the same block must evaluate at 1.0x, not a leftover 2.0x
    bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    bridge.SetSubPassIndex(0);
    EXPECT_EQ(MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 4u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[3], 1.0f);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[3], 1.0f);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, SingleContextBypassEvaluatesAt1xUnderAfr) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);
    auto& bridge = m_bridge;
    interceptor.SetVRBridge(&bridge);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_X, 0.5f);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 0.5f);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);

    // 2D Floating Window and GUI menu frames are monoscopic and one frame apart
    bridge.Set2DViewActive(true);
    EXPECT_EQ(MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    bridge.Set2DViewActive(false);
    bridge.SetMenuVisible(true);
    EXPECT_EQ(MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);

    ASSERT_EQ(s_evaluatedMvScaleX.size(), 2u);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[i], 0.5f);
        EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[i], 0.5f);
    }

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, AfrEvaluatesAt1xBeforeCompanionHandshake) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);
    auto& bridge = m_bridge;
    interceptor.SetVRBridge(&bridge);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_X, 0.5f);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 0.5f);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);

    // Main menu before the handshake: presented as a quad layer, so parity never advances and
    // every evaluation of the context is one frame apart, even with AFR selected.
    bridge.SetCompanionActive(false);
    ASSERT_EQ(bridge.GetRenderingMode(), VRRenderingMode::AFR);
    for (int frame = 0; frame < 2; ++frame) {
        EXPECT_EQ(MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    }

    ASSERT_EQ(s_evaluatedMvScaleX.size(), 2u);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[i], 0.5f);
        EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[i], 0.5f);
    }

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, AfrEvaluatesAt1xForHandleNotSplitPerEye) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);
    interceptor.SetVRBridge(&m_bridge);
    ASSERT_EQ(m_bridge.GetRenderingMode(), VRRenderingMode::AFR);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_X, 0.5f);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 0.5f);

    // A handle the interceptor did not virtualize is a single context evaluated every frame.
    NVSDK_NGX_Handle plainHandle{};
    plainHandle.Id = 7;
    m_bridge.SetFrameParity(1);
    EXPECT_EQ(interceptor.Intercept_EvaluateFeature(nullptr, &plainHandle, &params, nullptr), NVSDK_NGX_Result_Success);

    ASSERT_EQ(s_evaluatedHandles.size(), 1u);
    EXPECT_EQ(s_evaluatedHandles[0], &plainHandle);
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 1u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[0], 0.5f);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[0], 0.5f);
}

TEST_F(DlssEvaluationTest, SequentialDualPassEvaluatesAt1xByDefault) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);
    auto& bridge = m_bridge;
    interceptor.SetVRBridge(&bridge);
    bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_X, 0.5f);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 0.5f);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);
    bridge.SetSubPassIndex(0);
    EXPECT_EQ(MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 1u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[0], 0.5f);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[0], 0.5f);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, SequentialDualPassSubPassRoutingRoutesLeftAndRightWithinSameFrame) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    auto& bridge = m_bridge;
    interceptor.SetVRBridge(&bridge);
    bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);

    // Frame N: Sub-Pass 0 -> Left Eye
    bridge.SetSubPassIndex(0);
    EXPECT_EQ(MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evaluatedHandles.size(), 1u);
    EXPECT_EQ(s_evaluatedHandles[0], vHandle->hLeft);

    // Frame N: Sub-Pass 1 -> Right Eye within the same frame tick
    bridge.SetSubPassIndex(1);
    EXPECT_EQ(MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evaluatedHandles.size(), 2u);
    EXPECT_EQ(s_evaluatedHandles[1], vHandle->hRight);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, SequentialDualPassLocksMotionVectorScaleTo1xRegardlessOfSetting) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    auto& bridge = m_bridge;
    interceptor.SetVRBridge(&bridge);
    bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    // Caller configures custom MV scale multiplier (e.g. 2.0f or 0.5f)
    interceptor.SetMvScaleMultiplier(2.5f, 2.5f);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_X, 0.5f);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 0.5f);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    // Sub-Pass 0: must lock multiplier to 1.0x (0.5f * 1.0f = 0.5f)
    bridge.SetSubPassIndex(0);
    EXPECT_EQ(MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 1u);
    ASSERT_EQ(s_evaluatedMvScaleY.size(), 1u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[0], 0.5f);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[0], 0.5f);

    // Sub-Pass 1: must lock multiplier to 1.0x (0.5f * 1.0f = 0.5f)
    bridge.SetSubPassIndex(1);
    EXPECT_EQ(MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 2u);
    ASSERT_EQ(s_evaluatedMvScaleY.size(), 2u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[1], 0.5f);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[1], 0.5f);

    // Caller's parameters must remain restored
    float restoredX = 0.0f, restoredY = 0.0f;
    EXPECT_EQ(params.Get(NVSDK_NGX_Parameter_MV_Scale_X, &restoredX), NVSDK_NGX_Result_Success);
    EXPECT_EQ(params.Get(NVSDK_NGX_Parameter_MV_Scale_Y, &restoredY), NVSDK_NGX_Result_Success);
    EXPECT_FLOAT_EQ(restoredX, 0.5f);
    EXPECT_FLOAT_EQ(restoredY, 0.5f);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, SequentialDualPassModeTransitionAssertsResetForOneFrameAcrossBothContexts) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);

    auto& bridge = m_bridge;
    interceptor.SetVRBridge(&bridge);

    // Start in AFR mode
    bridge.SetRenderingMode(VRRenderingMode::AFR);
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    // Steady-state AFR: no resets
    bridge.SetFrameParity(0);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    bridge.SetFrameParity(1);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evaluatedResetValues.size(), 2u);
    EXPECT_EQ(s_evaluatedResetValues[0], 0);
    EXPECT_EQ(s_evaluatedResetValues[1], 0);

    // Transition to Sequential Dual-Pass!
    bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    // Frame 1 of Dual-Pass: Sub-Pass 0 (hLeft) and Sub-Pass 1 (hRight) both latch Reset = 1
    bridge.SetSubPassIndex(0);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    bridge.SetSubPassIndex(1);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);

    ASSERT_EQ(s_evaluatedResetValues.size(), 4u);
    EXPECT_EQ(s_evaluatedResetValues[2], 1); // hLeft reset
    EXPECT_EQ(s_evaluatedResetValues[3], 1); // hRight reset

    // Frame 2 of Dual-Pass: both reset values are cleared back to 0
    bridge.SetSubPassIndex(0);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    bridge.SetSubPassIndex(1);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);

    ASSERT_EQ(s_evaluatedResetValues.size(), 6u);
    EXPECT_EQ(s_evaluatedResetValues[4], 0);
    EXPECT_EQ(s_evaluatedResetValues[5], 0);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, SequentialDualPassAppliesItsOwnMotionVectorScaleOverride) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    interceptor.SetSdpMvScaleMultiplier(0.0f, 0.5f);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_X, 2.0f);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 2.0f);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);

    for (uint32_t subPass = 0; subPass < 2; ++subPass) {
        m_bridge.SetSubPassIndex(subPass);
        MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    }
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 2u);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[i], 0.0f);
        EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[i], 1.0f);
    }

    // The engine's own scale is restored after each evaluation.
    float restoredX = 0.0f;
    params.Get(NVSDK_NGX_Parameter_MV_Scale_X, &restoredX);
    EXPECT_FLOAT_EQ(restoredX, 2.0f);

    // The menu's single-context bypass is not affected.
    m_bridge.SetMenuVisible(true);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX.back(), 2.0f);

    MockReleaseFeature(virtualHandle);
}

TEST_F(DlssEvaluationTest, DiagnosticsSummarizeJitterPerEyeContextUntilCountdownEnds) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);
    // Virtualizing a feature arms the default trace window.
    EXPECT_EQ(interceptor.GetDiagEvaluationsRemaining(), NgxInterceptor::kDefaultDiagEvaluations);

    interceptor.ArmDiagnostics(6);
    // An engine jitter sequence of period 4 split across alternating eyes: each eye sees 2 offsets.
    const float jitterX[] = {0.25f, -0.25f, -0.125f, 0.375f};
    for (int i = 0; i < 8; ++i) {
        m_bridge.SetSubPassIndex(static_cast<uint32_t>(i % 2));
        params.Set(NVSDK_NGX_Parameter_Jitter_Offset_X, jitterX[i % 4]);
        params.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, 0.0f);
        MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);
    }

    // The window closes after 6 evaluations; the last 2 are not recorded.
    EXPECT_EQ(interceptor.GetDiagEvaluationsRemaining(), 0u);
    NgxInterceptor::JitterSummary left = interceptor.GetDiagJitterSummary(0);
    NgxInterceptor::JitterSummary right = interceptor.GetDiagJitterSummary(1);
    EXPECT_EQ(left.evaluations, 3u);
    EXPECT_EQ(right.evaluations, 3u);
    EXPECT_EQ(left.distinctOffsets, 2u);
    EXPECT_EQ(right.distinctOffsets, 2u);
    EXPECT_FLOAT_EQ(left.minX, -0.125f);
    EXPECT_FLOAT_EQ(left.maxX, 0.25f);
    EXPECT_FLOAT_EQ(right.minX, -0.25f);
    EXPECT_FLOAT_EQ(right.maxX, 0.375f);

    MockReleaseFeature(virtualHandle);
}

// ---- Motion vector pass (vrDlssMvFix) ----

class FakeMvPass : public IMotionVectorPass {
public:
    int recordCount = 0;
    MvPassInputs lastInputs;
    ID3D12Resource* output = reinterpret_cast<ID3D12Resource*>(0xBEEF);
    bool fail = false;
    std::string failureReason = "unsupported motion vector format";
    std::vector<MvSelfCheckRequest> selfChecks; // one per Record that carried a request
    std::vector<MvCorrectionRequest> corrections; // one per Record that carried a correction
    std::vector<MvSelfCheckReport> pendingReports;
    std::vector<MvSelfCheckReport> PollSelfCheck() override {
        std::vector<MvSelfCheckReport> out;
        out.swap(pendingReports);
        return out;
    }
    MvPassResult Record(const MvPassInputs& inputs) override {
        ++recordCount;
        lastInputs = inputs;
        if (inputs.selfCheck) selfChecks.push_back(*inputs.selfCheck);
        if (inputs.correction) corrections.push_back(*inputs.correction);
        MvPassResult r;
        if (fail) { r.failureReason = failureReason; return r; }
        r.ok = true;
        r.output = output;
        return r;
    }
};

class MvPassEvaluationTest : public DlssEvaluationTest {
protected:
    FakeMvPass* m_pass = nullptr;
    int m_factoryCalls = 0;
    ID3D12GraphicsCommandList* m_cmdList = reinterpret_cast<ID3D12GraphicsCommandList*>(0x1000);
    ID3D12Resource* m_engineMv = reinterpret_cast<ID3D12Resource*>(0xAAA0);
    ID3D12Resource* m_engineDepth = reinterpret_cast<ID3D12Resource*>(0xDDD0);
    NgxParameterImpl m_params;
    NVSDK_NGX_Handle* m_handle = nullptr;

    void SetUpPass() {
        ASSERT_TRUE(m_ngx.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature));
        m_ngx.SetMotionVectorPassFactory([this](ID3D12GraphicsCommandList*) {
            ++m_factoryCalls;
            auto pass = std::make_unique<FakeMvPass>();
            m_pass = pass.get();
            return std::unique_ptr<IMotionVectorPass>(std::move(pass));
        });
        m_params.Set(NVSDK_NGX_Parameter_Width, 1920u);
        m_params.Set(NVSDK_NGX_Parameter_Height, 1080u);
        m_params.Set(NVSDK_NGX_Parameter_MotionVectors, m_engineMv);
        m_params.Set(NVSDK_NGX_Parameter_Depth, m_engineDepth);
        ASSERT_EQ(MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &m_params, &m_handle), NVSDK_NGX_Result_Success);
    }
    void TearDown() override {
        if (m_handle) MockReleaseFeature(m_handle);
        DlssEvaluationTest::TearDown();
    }
    ID3D12Resource* ParamMv() {
        ID3D12Resource* mv = nullptr;
        m_params.Get(NVSDK_NGX_Parameter_MotionVectors, &mv);
        return mv;
    }
};

TEST_F(MvPassEvaluationTest, DlssReceivesTheInjectorsTextureAndTheGameParametersAreRestored) {
    SetUpPass();
    m_ngx.SetMvFixEnabled(true);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);

    ASSERT_NE(m_pass, nullptr);
    EXPECT_EQ(m_pass->recordCount, 1);
    EXPECT_EQ(m_pass->lastInputs.cmdList, m_cmdList);
    EXPECT_EQ(m_pass->lastInputs.motionVectors, m_engineMv);
    EXPECT_EQ(m_pass->lastInputs.depth, m_engineDepth);
    ASSERT_EQ(s_evaluatedMotionVectors.size(), 1u);
    EXPECT_EQ(s_evaluatedMotionVectors[0], m_pass->output);
    // The engine's motion vectors are back in the parameter block afterwards.
    EXPECT_EQ(ParamMv(), m_engineMv);
}

TEST_F(MvPassEvaluationTest, SequentialDualPassEyeContextsBothGetTheInjectorsTexture) {
    SetUpPass();
    m_ngx.SetMvFixEnabled(true);
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    for (uint32_t subPass = 0; subPass < 2; ++subPass) {
        m_bridge.SetSubPassIndex(subPass);
        MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    }
    ASSERT_EQ(s_evaluatedMotionVectors.size(), 2u);
    EXPECT_EQ(s_evaluatedMotionVectors[0], m_pass->output);
    EXPECT_EQ(s_evaluatedMotionVectors[1], m_pass->output);
    EXPECT_EQ(ParamMv(), m_engineMv);
}

TEST_F(MvPassEvaluationTest, OnByDefaultAndSwitchableAtRuntime) {
    SetUpPass();
    EXPECT_TRUE(m_ngx.IsMvFixEnabled());
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_pass->output);
    EXPECT_EQ(m_factoryCalls, 1);

    m_ngx.SetMvFixEnabled(false);
    EXPECT_FALSE(m_ngx.IsMvFixEnabled());
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_engineMv);
    EXPECT_EQ(m_pass->recordCount, 1);

    m_ngx.SetMvFixEnabled(true);
    EXPECT_TRUE(m_ngx.IsMvFixEnabled());
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_pass->output);
    EXPECT_EQ(m_pass->recordCount, 2);
}

TEST_F(MvPassEvaluationTest, MenusAndTwoDimensionalViewKeepTheEnginesMotionVectors) {
    SetUpPass();
    m_ngx.SetMvFixEnabled(true);
    m_bridge.SetMenuVisible(true);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    m_bridge.SetMenuVisible(false);
    m_bridge.Set2DViewActive(true);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(s_evaluatedMotionVectors.size(), 2u);
    EXPECT_EQ(s_evaluatedMotionVectors[0], m_engineMv);
    EXPECT_EQ(s_evaluatedMotionVectors[1], m_engineMv);
    EXPECT_EQ(m_factoryCalls, 0);
}

TEST_F(MvPassEvaluationTest, EvaluationWithoutCommandListOrInputsPassesThroughWithoutDisabling) {
    SetUpPass();
    m_ngx.SetMvFixEnabled(true);
    MockEvaluateFeature(nullptr, m_handle, &m_params, nullptr);
    NgxParameterImpl noDepth;
    noDepth.Set(NVSDK_NGX_Parameter_MotionVectors, m_engineMv);
    MockEvaluateFeature(m_cmdList, m_handle, &noDepth, nullptr);
    EXPECT_EQ(s_evaluatedMotionVectors[0], m_engineMv);
    EXPECT_EQ(s_evaluatedMotionVectors[1], m_engineMv);
    EXPECT_FALSE(m_ngx.IsMvFixDisabledForSession());
}

TEST_F(MvPassEvaluationTest, FailureDisablesThePassForTheSessionAndDlssKeepsTheEnginesVectors) {
    SetUpPass();
    m_ngx.SetMvFixEnabled(true);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_NE(m_pass, nullptr);
    m_pass->fail = true;
    m_pass->failureReason = "unsupported motion vector format 99";

    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_engineMv);
    EXPECT_EQ(ParamMv(), m_engineMv);
    EXPECT_TRUE(m_ngx.IsMvFixDisabledForSession());
    EXPECT_FALSE(m_ngx.IsMvFixEnabled());
    EXPECT_EQ(m_ngx.GetMvFixDisableReason(), "unsupported motion vector format 99");

    // The pass is no longer asked, and "on" cannot bring it back.
    int calls = m_pass->recordCount;
    EXPECT_FALSE(m_ngx.SetMvFixEnabled(true));
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(m_pass->recordCount, calls);
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_engineMv);
}

TEST_F(MvPassEvaluationTest, MissingPassFromFactoryDisablesThePass) {
    SetUpPass();
    m_ngx.SetMotionVectorPassFactory([](ID3D12GraphicsCommandList*) { return std::unique_ptr<IMotionVectorPass>(); });
    m_ngx.SetMvFixEnabled(true);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_engineMv);
    EXPECT_TRUE(m_ngx.IsMvFixDisabledForSession());
    EXPECT_FALSE(m_ngx.GetMvFixDisableReason().empty());
}

TEST(MvPassShaderTest, PassThroughShaderCompilesWithTheSystemCompiler) {
    std::string bytecode, error;
    ASSERT_TRUE(MvPassCompileShader(bytecode, error)) << error;
    EXPECT_GT(bytecode.size(), 100u);
}

TEST(MvPassShaderTest, CorrectionShaderCompilesWithTheSystemCompiler) {
    std::string bytecode, error;
    ASSERT_TRUE(MvPassCompileShader(bytecode, error, "correct")) << error;
    EXPECT_GT(bytecode.size(), 100u);
}

TEST(MvPassShaderTest, SelfCheckShaderCompilesWithTheSystemCompiler) {
    std::string bytecode, error;
    ASSERT_TRUE(MvPassCompileShader(bytecode, error, "selfcheck")) << error;
    EXPECT_GT(bytecode.size(), 100u);
}

TEST(MvPassFormatTest, AcceptsTheEnginesFormatsAndRejectsOthers) {
    EXPECT_EQ(MvPassMotionVectorViewFormat(DXGI_FORMAT_R16G16_FLOAT), DXGI_FORMAT_R16G16_FLOAT);
    EXPECT_EQ(MvPassMotionVectorViewFormat(DXGI_FORMAT_R16G16_TYPELESS), DXGI_FORMAT_R16G16_FLOAT);
    EXPECT_EQ(MvPassMotionVectorViewFormat(DXGI_FORMAT_R32G32_FLOAT), DXGI_FORMAT_R32G32_FLOAT);
    EXPECT_EQ(MvPassMotionVectorViewFormat(DXGI_FORMAT_R8G8B8A8_UNORM), DXGI_FORMAT_UNKNOWN);
    EXPECT_EQ(MvPassMotionVectorViewFormat(DXGI_FORMAT_R16G16_SNORM), DXGI_FORMAT_UNKNOWN);

    EXPECT_EQ(MvPassDepthViewFormat(DXGI_FORMAT_R32_FLOAT), DXGI_FORMAT_R32_FLOAT);
    EXPECT_EQ(MvPassDepthViewFormat(DXGI_FORMAT_R32_TYPELESS), DXGI_FORMAT_R32_FLOAT);
    EXPECT_EQ(MvPassDepthViewFormat(DXGI_FORMAT_D32_FLOAT), DXGI_FORMAT_R32_FLOAT);
    EXPECT_EQ(MvPassDepthViewFormat(DXGI_FORMAT_R32G8X24_TYPELESS), DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
    EXPECT_EQ(MvPassDepthViewFormat(DXGI_FORMAT_R24G8_TYPELESS), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
    EXPECT_EQ(MvPassDepthViewFormat(DXGI_FORMAT_R8G8B8A8_UNORM), DXGI_FORMAT_UNKNOWN);
}

namespace {

std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

CameraRecord DiagCamera(uint32_t eye, double x) {
    CameraRecord r;
    r.eye = eye;
    r.position[0] = x;
    r.fovY = 1.0f;
    r.nearClip = 0.15f;
    r.farClip = 2000.0f;
    return r;
}

} // namespace

TEST_F(DlssEvaluationTest, DiagnosticTraceReportsTheResolvedCamerasAndWhichAreMissing) {
    const auto logPath = std::filesystem::temp_directory_path() / "fs25vr_camera_diag_test.log";
    Logger::Instance().SetLogFile(logPath);

    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature, &MockEvaluateFeature);
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);

    // No camera has been reported (loading screen): the trace names every record as missing.
    m_bridge.SetSubPassIndex(0);
    interceptor.ArmDiagnostics(1);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);

    // The left eye renders frame 0, the right eye frame 1, the left eye again in frame 2, an IPD of
    // 0.064 m apart (the head is still).
    CameraRecordStore& cameras = m_bridge.GetCameraRecords();
    ASSERT_TRUE(cameras.Report(DiagCamera(0, 10.0)));
    cameras.AdvanceFrame();
    ASSERT_TRUE(cameras.Report(DiagCamera(1, 10.064)));
    cameras.AdvanceFrame();
    ASSERT_TRUE(cameras.Report(DiagCamera(0, 10.0)));
    m_bridge.SetSubPassIndex(0);
    interceptor.ArmDiagnostics(1);
    MockEvaluateFeature(nullptr, virtualHandle, &params, nullptr);

    Logger::Instance().Flush();
    const std::string log = ReadWholeFile(logPath);
    Logger::Instance().SetLogFile(Logger::DefaultLogPath());
    std::filesystem::remove(logPath);

    EXPECT_NE(log.find("Cur=missing Prev=missing SameEye=missing"), std::string::npos) << log;
    EXPECT_NE(log.find("Cur=eye0@f2 Prev=eye1@f1 SameEye=eye0@f0"), std::string::npos) << log;
    EXPECT_NE(log.find("PrevRel=(dx=0.0640"), std::string::npos) << log;
    EXPECT_NE(log.find("SameEyeRel=(dx=0.0000"), std::string::npos) << log;

    MockReleaseFeature(virtualHandle);
}

// ---- Motion vector self-check (#34) ----

class MvSelfCheckTest : public MvPassEvaluationTest {
protected:
    std::filesystem::path m_logPath;
    void SetUp() override {
        MvPassEvaluationTest::SetUp();
        m_logPath = std::filesystem::temp_directory_path() / "fs25vr_mv_selfcheck_test.log";
        Logger::Instance().SetLogFile(m_logPath);
    }
    void TearDown() override {
        MvPassEvaluationTest::TearDown();
        Logger::Instance().SetLogFile(Logger::DefaultLogPath());
        std::filesystem::remove(m_logPath);
    }
    std::string Log() {
        Logger::Instance().Flush();
        return ReadWholeFile(m_logPath);
    }
    // Sequential Dual-Pass, MvFix on, the left eye rendering after the right eye 0.064 m away.
    void SetUpSdp(bool withRecords) {
        SetUpPass();
        m_ngx.SetMvFixEnabled(true);
        m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
        if (withRecords) {
            CameraRecordStore& cameras = m_bridge.GetCameraRecords();
            ASSERT_TRUE(cameras.Report(DiagCamera(1, 10.064)));
            cameras.AdvanceFrame();
            ASSERT_TRUE(cameras.Report(DiagCamera(0, 10.0)));
        }
        m_bridge.SetSubPassIndex(0);
    }
};

TEST_F(MvSelfCheckTest, RequestCarriesTheResolvedCamerasAndTheScaleDlssReceives) {
    SetUpSdp(true);
    m_params.Set(NVSDK_NGX_Parameter_MV_Scale_X, 2.0f);
    m_params.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 2.0f);
    m_ngx.ArmDiagnostics(4);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);

    ASSERT_NE(m_pass, nullptr);
    ASSERT_EQ(m_pass->selfChecks.size(), 1u);
    ASSERT_NE(m_pass->lastInputs.selfCheck, nullptr);
    const MvSelfCheckRequest& r = m_pass->selfChecks[0];
    EXPECT_EQ(r.eye, 0u);
    EXPECT_FLOAT_EQ(r.mvScaleX, 2.0f);
    EXPECT_FLOAT_EQ(r.mvScaleY, 2.0f);
    // fovY 1.0 rad: tan(0.5), the same for both cameras; near and far from the record.
    EXPECT_NEAR(r.current.tanHalfFovY, std::tan(0.5f), 1e-6f);
    EXPECT_NEAR(r.previous.tanHalfFovY, std::tan(0.5f), 1e-6f);
    EXPECT_FLOAT_EQ(r.current.nearClip, 0.15f);
    EXPECT_FLOAT_EQ(r.current.farClip, 2000.0f);
    // The previous rendered frame's camera is the right eye 0.064 m to the right: a point is 0.064 m
    // further left seen from it (p_prev = p - t).
    EXPECT_NEAR(r.toPrevious.c[0], -0.064f, 1e-5f);
    EXPECT_NEAR(r.toPrevious.m[0][0], 1.0f, 1e-6f);
    // The pass still hands DLSS its own texture and the game's vectors come back.
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_pass->output);
    EXPECT_EQ(ParamMv(), m_engineMv);
}

TEST_F(MvSelfCheckTest, NoRequestWhileDiagnosticsAreNotArmed) {
    SetUpSdp(true);
    m_ngx.ArmDiagnostics(0);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_NE(m_pass, nullptr);
    EXPECT_EQ(m_pass->recordCount, 1);
    EXPECT_TRUE(m_pass->selfChecks.empty());
    EXPECT_EQ(m_pass->lastInputs.selfCheck, nullptr);
}

TEST_F(MvSelfCheckTest, MissingCameraRecordSkipsThePredictionAndSaysSo) {
    SetUpSdp(false);
    m_ngx.ArmDiagnostics(4);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);

    ASSERT_NE(m_pass, nullptr);
    EXPECT_EQ(m_pass->recordCount, 1); // the pass itself still runs: the output stays a copy
    EXPECT_TRUE(m_pass->selfChecks.empty());
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_pass->output);
    const std::string log = Log();
    EXPECT_NE(log.find("MvCheck skipped"), std::string::npos) << log;
    EXPECT_NE(log.find("missing camera record"), std::string::npos) << log;
}

TEST_F(MvSelfCheckTest, ARecordForTheCurrentFrameButNotThePreviousOneAlsoSkips) {
    SetUpPass();
    m_ngx.SetMvFixEnabled(true);
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    ASSERT_TRUE(m_bridge.GetCameraRecords().Report(DiagCamera(0, 10.0)));
    m_bridge.SetSubPassIndex(0);
    m_ngx.ArmDiagnostics(4);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_TRUE(m_pass->selfChecks.empty());
    EXPECT_NE(Log().find("missing camera record"), std::string::npos);
}

TEST_F(MvSelfCheckTest, ReadBackResultsAreLoggedPerEyeWithBothConventionsAndTheSkippedFraction) {
    SetUpSdp(true);
    m_ngx.ArmDiagnostics(4);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);

    // The pass reads a result back a few evaluations later; the next evaluation logs it.
    MvSelfCheckReport report;
    report.eye = 1;
    report.evalIndex = 7;
    report.stats.total = 1024;
    report.stats.valid = 768;
    report.stats.skipped = 256;
    report.stats.skippedFraction = 0.25;
    report.stats.meanToPrevious = 0.8;
    report.stats.medianToPrevious = 0.25;
    report.stats.meanToCurrent = 12.5;
    report.stats.medianToCurrent = 11.0;
    report.stats.engineMean = 0.5;
    report.stats.predictedMean = 45.25;
    report.stats.k = 0.012;
    report.stats.rawMean = 0.25;
    report.stats.kRaw = -0.01;
    report.stats.rawMinX = -0.5;
    report.stats.rawMaxX = 0.75;
    report.stats.rawMinY = -1.5;
    report.stats.rawMaxY = 2.0;
    report.mvFormat = 34; // DXGI_FORMAT_R16G16_FLOAT
    m_pass->pendingReports.push_back(report);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);

    const std::string log = Log();
    EXPECT_NE(log.find("EngineMean=0.500 PredMean=45.250 k=0.012 RawMean=0.250 kRaw=-0.010"), std::string::npos) << log;
    EXPECT_NE(log.find("MvCheck RawMv format=34 Eye=1 Eval #7 rawX=[-0.500, 0.750] rawY=[-1.500, 2.000]"),
              std::string::npos) << log;
    EXPECT_NE(log.find("MvCheck Eye=1 Eval #7"), std::string::npos) << log;
    EXPECT_NE(log.find("previous-minus-current: mean=0.800 median=0.250"), std::string::npos) << log;
    EXPECT_NE(log.find("current-minus-previous: mean=12.500 median=11.000"), std::string::npos) << log;
    EXPECT_NE(log.find("skipped=25.0%"), std::string::npos) << log;
    EXPECT_NE(log.find("Best=previous-minus-current"), std::string::npos) << log;
}

TEST_F(MvSelfCheckTest, TheRawVectorRangeIsLoggedOncePerArming) {
    SetUpSdp(true);
    m_ngx.ArmDiagnostics(8);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    auto count = [&]() {
        const std::string log = Log();
        size_t n = 0;
        for (size_t at = log.find("MvCheck RawMv"); at != std::string::npos; at = log.find("MvCheck RawMv", at + 1)) ++n;
        return n;
    };
    MvSelfCheckReport report;
    report.eye = 0;
    report.evalIndex = 1;
    m_pass->pendingReports.push_back(report);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    report.evalIndex = 2;
    m_pass->pendingReports.push_back(report);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(count(), 1u);

    m_ngx.ArmDiagnostics(8);
    report.evalIndex = 3;
    m_pass->pendingReports.push_back(report);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(count(), 2u);
}

// ---- Per-eye motion vector correction (#35) ----

class MvCorrectionEvaluationTest : public MvSelfCheckTest {
protected:
    // Frame 0: left eye at x = 9.9; frame 1: right eye 0.064 m aside; the current frame is the left eye
    // again at x = 10, so the same eye's previous camera is 0.1 m to the left of the current one.
    void ReportLeftRightLeft() {
        CameraRecordStore& cameras = m_bridge.GetCameraRecords();
        ASSERT_TRUE(cameras.Report(DiagCamera(0, 9.9)));
        cameras.AdvanceFrame();
        ASSERT_TRUE(cameras.Report(DiagCamera(1, 10.064)));
        cameras.AdvanceFrame();
        ASSERT_TRUE(cameras.Report(DiagCamera(0, 10.0)));
    }
    void SetUpCorrection() {
        SetUpPass();
        m_ngx.SetMvFixEnabled(true);
        m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
        m_bridge.SetSubPassIndex(0);
    }
};

TEST_F(MvCorrectionEvaluationTest, TheRequestCarriesBothPreviousCamerasAndTheScaleDlssReceives) {
    SetUpCorrection();
    ReportLeftRightLeft();
    m_params.Set(NVSDK_NGX_Parameter_MV_Scale_X, 2.0f);
    m_params.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 2.0f);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);

    ASSERT_EQ(m_pass->corrections.size(), 1u);
    const MvCorrectionRequest& c = m_pass->corrections[0];
    EXPECT_FLOAT_EQ(c.mvScaleX, 2.0f);
    EXPECT_FLOAT_EQ(c.mvScaleY, 2.0f);
    EXPECT_NEAR(c.current.tanHalfFovY, std::tan(0.5f), 1e-6f);
    EXPECT_FLOAT_EQ(c.current.nearClip, 0.15f);
    EXPECT_FLOAT_EQ(c.current.farClip, 2000.0f);
    EXPECT_NEAR(c.toPreviousFrame.c[0], -0.064f, 1e-5f); // the right eye 0.064 m aside
    EXPECT_NEAR(c.toSameEye.c[0], 0.1f, 1e-5f);          // the same eye 0.1 m to the left
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_pass->output);
    EXPECT_EQ(ParamMv(), m_engineMv);
}

TEST_F(MvCorrectionEvaluationTest, FramesWithoutCameraRecordsPassTheEnginesVectorsThrough) {
    SetUpCorrection();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr); // nothing reported (loading screen)
    ASSERT_NE(m_pass, nullptr);
    EXPECT_TRUE(m_pass->corrections.empty());

    // Start of play: the other eye rendered before, this eye has no previous camera yet.
    CameraRecordStore& cameras = m_bridge.GetCameraRecords();
    ASSERT_TRUE(cameras.Report(DiagCamera(1, 10.064)));
    cameras.AdvanceFrame();
    ASSERT_TRUE(cameras.Report(DiagCamera(0, 10.0)));
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_TRUE(m_pass->corrections.empty());
    EXPECT_FALSE(m_ngx.IsMvFixDisabledForSession());
}

TEST_F(MvCorrectionEvaluationTest, ASelfCheckAlongsideTheCorrectionTargetsTheSameEyesPreviousCamera) {
    SetUpCorrection();
    ReportLeftRightLeft();
    m_ngx.ArmDiagnostics(4);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);

    ASSERT_EQ(m_pass->selfChecks.size(), 1u);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_NEAR(m_pass->selfChecks[0].toPrevious.c[0], 0.1f, 1e-5f); // not the right eye's -0.064
    EXPECT_NE(Log().find("MvCorrection Eval #"), std::string::npos);
}

TEST_F(MvCorrectionEvaluationTest, ZeroMvScaleKeepsTheEnginesVectors) {
    SetUpCorrection();
    ReportLeftRightLeft();
    m_ngx.SetSdpMvScaleMultiplier(0.0f, 0.0f); // the old diagnostic: no motion at all
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_TRUE(m_pass->corrections.empty());
}

TEST_F(MvCorrectionEvaluationTest, SequentialDualPassScaleMultiplierIsPartOfTheScaleTheCorrectionDivides) {
    SetUpCorrection();
    ReportLeftRightLeft();
    m_ngx.SetSdpMvScaleMultiplier(0.5f, 0.5f);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_FLOAT_EQ(m_pass->corrections[0].mvScaleX, 0.5f);
}

// ---- AFR uses the correction instead of eye-stride scaling (#36) ----

class MvCorrectionAfrEvaluationTest : public MvCorrectionEvaluationTest {
protected:
    // Alternate Frame Rendering, the left eye rendering the current frame after the right eye's.
    void SetUpAfr() {
        SetUpPass();
        m_bridge.SetRenderingMode(VRRenderingMode::AFR);
        m_bridge.SetFrameParity(0);
    }
};

TEST_F(MvCorrectionAfrEvaluationTest, TheCorrectionResolvesTheOtherEyeAsPreviousFrameAndTheSameEyeTwoFramesBack) {
    SetUpAfr();
    m_ngx.SetMvFixEnabled(true);
    ReportLeftRightLeft();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);

    ASSERT_EQ(m_pass->corrections.size(), 1u);
    const MvCorrectionRequest& c = m_pass->corrections[0];
    EXPECT_NEAR(c.toPreviousFrame.c[0], -0.064f, 1e-5f); // the right eye, the frame before
    EXPECT_NEAR(c.toSameEye.c[0], 0.1f, 1e-5f);          // the left eye two frames back
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_pass->output);
    EXPECT_EQ(ParamMv(), m_engineMv);
}

TEST_F(MvCorrectionAfrEvaluationTest, OnlyTheObjectMotionComponentCarriesTheEyeStrideFactor) {
    SetUpAfr();
    m_ngx.SetMvFixEnabled(true);
    ReportLeftRightLeft();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);

    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_FLOAT_EQ(m_pass->corrections[0].objectMotionFactor, 2.0f);
    // DLSS is not handed the 2.0x scale: the correction replaces it.
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 1u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[0], 1.0f);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[0], 1.0f);
    EXPECT_FLOAT_EQ(m_pass->corrections[0].mvScaleX, 1.0f);
}

TEST_F(MvCorrectionAfrEvaluationTest, TheStrideFactorIsAfrOnlySequentialDualPassKeepsObjectMotionAsIs) {
    SetUpCorrection();
    ReportLeftRightLeft();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_FLOAT_EQ(m_pass->corrections[0].objectMotionFactor, 1.0f);
}

TEST_F(MvCorrectionAfrEvaluationTest, TheNgxCorrectionGatesPixelsThatAreNotStaticConsistent) {
    SetUpCorrection();
    ReportLeftRightLeft();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_TRUE(m_pass->corrections[0].gateNonStatic);
}

TEST_F(MvCorrectionAfrEvaluationTest, VrDlssMvScaleIsIgnoredWhileTheCorrectionIsOn) {
    SetUpAfr();
    m_ngx.SetMvFixEnabled(true);
    m_ngx.SetMvScaleMultiplier(3.0f, 3.0f);
    ReportLeftRightLeft();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 1u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[0], 1.0f);
    EXPECT_FLOAT_EQ(m_pass->corrections[0].objectMotionFactor, 2.0f); // the stride is not user-tunable
}

TEST_F(MvCorrectionAfrEvaluationTest, WithTheCorrectionOffAfrKeepsEyeStrideScalingExactlyAsBefore) {
    SetUpAfr();
    m_ngx.SetMvFixEnabled(false);
    ReportLeftRightLeft(); // records or not, the pass is off
    m_ngx.SetMvScaleMultiplier(2.5f, 2.5f);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 1u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[0], 2.5f);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[0], 2.5f);
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_engineMv);
    EXPECT_EQ(m_factoryCalls, 0);
    float restored = 0.0f;
    EXPECT_EQ(m_params.Get(NVSDK_NGX_Parameter_MV_Scale_X, &restored), NVSDK_NGX_Result_Success);
    EXPECT_FLOAT_EQ(restored, 1.0f); // the multiplier is not left behind in the reused block
}

TEST_F(MvCorrectionAfrEvaluationTest, WhenThePassDisablesItselfAfrFallsBackToEyeStrideScaling) {
    SetUpAfr();
    m_ngx.SetMvFixEnabled(true);
    ReportLeftRightLeft();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr); // the pass is created and works
    ASSERT_NE(m_pass, nullptr);
    m_pass->fail = true;

    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr); // it fails on this evaluation
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 2u);
    EXPECT_TRUE(m_ngx.IsMvFixDisabledForSession());
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_engineMv);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[1], NgxInterceptor::kDefaultAfrMvScale);

    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr); // and from then on
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 3u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[2], NgxInterceptor::kDefaultAfrMvScale);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleY[2], NgxInterceptor::kDefaultAfrMvScale);
}

TEST_F(MvCorrectionAfrEvaluationTest, FallbackScalingAlsoCoversEvaluationsWithoutDepthOrMotionVectors) {
    SetUpAfr();
    m_ngx.SetMvFixEnabled(true);
    NgxParameterImpl noDepth;
    noDepth.Set(NVSDK_NGX_Parameter_MotionVectors, m_engineMv);
    MockEvaluateFeature(m_cmdList, m_handle, &noDepth, nullptr);
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 1u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[0], NgxInterceptor::kDefaultAfrMvScale);
    EXPECT_FALSE(m_ngx.IsMvFixDisabledForSession());
}

TEST_F(MvCorrectionAfrEvaluationTest, FramesWithoutCameraRecordsPassTheEnginesVectorsThroughAtOneTimes) {
    SetUpAfr();
    m_ngx.SetMvFixEnabled(true);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr); // nothing reported
    ASSERT_NE(m_pass, nullptr);
    EXPECT_TRUE(m_pass->corrections.empty());
    EXPECT_EQ(m_pass->recordCount, 1);
    ASSERT_EQ(s_evaluatedMvScaleX.size(), 1u);
    EXPECT_FLOAT_EQ(s_evaluatedMvScaleX[0], 1.0f);
}

TEST_F(MvCorrectionAfrEvaluationTest, ASelfCheckUnderAfrTargetsTheSameEyesCameraTwoFramesBack) {
    SetUpAfr();
    m_ngx.SetMvFixEnabled(true);
    ReportLeftRightLeft();
    m_ngx.ArmDiagnostics(4);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(m_pass->selfChecks.size(), 1u);
    EXPECT_NEAR(m_pass->selfChecks[0].toPrevious.c[0], 0.1f, 1e-5f);
    EXPECT_FLOAT_EQ(m_pass->selfChecks[0].mvScaleX, 1.0f);
}

TEST_F(MvCorrectionEvaluationTest, MvFixOffRestoresTheUncorrectedBehaviour) {
    SetUpCorrection();
    ReportLeftRightLeft();
    m_ngx.SetMvFixEnabled(false);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(s_evaluatedMotionVectors.back(), m_engineMv);
    EXPECT_EQ(m_factoryCalls, 0);
}

// ---- Depth model (vrDlssDepthModel) ----

TEST_F(MvCorrectionEvaluationTest, TheEngineDepthIsReadAsInfiniteReverseZByDefault) {
    SetUpCorrection();
    EXPECT_TRUE(m_ngx.IsDepthModelInfinite());
    ReportLeftRightLeft();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);

    ASSERT_EQ(m_pass->corrections.size(), 1u);
    const MvCorrectionRequest& c = m_pass->corrections[0];
    // Only the camera that rendered the depth buffer reads it.
    EXPECT_TRUE(c.current.infiniteDepth);
    EXPECT_FALSE(c.previousFrame.infiniteDepth);
    EXPECT_FALSE(c.sameEye.infiniteDepth);
    EXPECT_FLOAT_EQ(c.current.nearClip, 0.15f); // near still comes from the camera record
}

TEST_F(MvCorrectionEvaluationTest, TheFiniteDepthModelRestoresTheFiniteProjection) {
    SetUpCorrection();
    m_ngx.SetDepthModelInfinite(false);
    EXPECT_FALSE(m_ngx.IsDepthModelInfinite());
    ReportLeftRightLeft();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_FALSE(m_pass->corrections[0].current.infiniteDepth);
    EXPECT_FLOAT_EQ(m_pass->corrections[0].current.farClip, 2000.0f);
}

TEST_F(MvCorrectionEvaluationTest, TheSelfCheckUsesTheSameDepthModelAsTheCorrection) {
    SetUpCorrection();
    ReportLeftRightLeft();
    m_ngx.ArmDiagnostics(8);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(m_pass->selfChecks.size(), 1u);
    EXPECT_TRUE(m_pass->selfChecks[0].current.infiniteDepth);
    EXPECT_FALSE(m_pass->selfChecks[0].previous.infiniteDepth);

    m_ngx.SetDepthModelInfinite(false);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(m_pass->selfChecks.size(), 2u);
    EXPECT_FALSE(m_pass->selfChecks[1].current.infiniteDepth);
}

TEST_F(MvCorrectionEvaluationTest, TheProjectionTheCorrectionIsFedIsLoggedOncePerArmingWithItsSource) {
    SetUpCorrection();
    ReportLeftRightLeft();
    auto count = [&]() {
        const std::string log = Log();
        size_t n = 0;
        for (size_t at = log.find("MvProjection"); at != std::string::npos; at = log.find("MvProjection", at + 1)) ++n;
        return n;
    };
    m_ngx.ArmDiagnostics(0); // creating the feature armed the diagnostics
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(count(), 0u) << "not armed";

    m_ngx.ArmDiagnostics(8);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(count(), 1u);
    // fovY 1.0 rad: tan(0.5) = 0.546; a 1920x1080 render: aspect 1.778; the record carries no offset.
    EXPECT_NE(Log().find("NgxInterceptor: [Diag] MvProjection Eye=0 Eval #1: used=camera-record nearClip=0.150 "
                         "farClip=2000.000 fovY=1.000 tanHalfFovY=0.546 projOffset=(0.000, 0.000) aspect=1.778 "
                         "depthModel=infinite"),
              std::string::npos) << Log();

    m_ngx.ArmDiagnostics(8);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(count(), 2u);
}

TEST_F(MvCorrectionEvaluationTest, ANearClipOverrideReplacesTheRecordsNearOnEveryCamera) {
    SetUpCorrection();
    ReportLeftRightLeft();
    EXPECT_FLOAT_EQ(m_ngx.GetNearClipOverride(), 0.0f); // default: the record's near
    m_ngx.SetNearClipOverride(0.1f);
    m_ngx.ArmDiagnostics(8);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);

    ASSERT_EQ(m_pass->corrections.size(), 1u);
    const MvCorrectionRequest& c = m_pass->corrections[0];
    EXPECT_FLOAT_EQ(c.current.nearClip, 0.1f);
    EXPECT_FLOAT_EQ(c.previousFrame.nearClip, 0.1f);
    EXPECT_FLOAT_EQ(c.sameEye.nearClip, 0.1f);
    ASSERT_EQ(m_pass->selfChecks.size(), 1u);
    EXPECT_FLOAT_EQ(m_pass->selfChecks[0].current.nearClip, 0.1f);
    EXPECT_FLOAT_EQ(m_pass->selfChecks[0].previous.nearClip, 0.1f);
    EXPECT_FLOAT_EQ(c.current.farClip, 2000.0f); // only near is overridden

    // The record says 0.15: 0.15 - 0.1 = 0.05.
    EXPECT_NE(Log().find("used=override nearClip=0.100 farClip=2000.000"), std::string::npos) << Log();
    EXPECT_NE(Log().find("recordNearClip=0.150 differs from the override by 0.050"), std::string::npos) << Log();
}

TEST_F(MvCorrectionEvaluationTest, ANearClipOverrideEqualToTheRecordsNearGivesNoHintAndZeroRestoresTheRecord) {
    SetUpCorrection();
    ReportLeftRightLeft();
    m_ngx.SetNearClipOverride(0.15f);
    m_ngx.ArmDiagnostics(8);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_EQ(Log().find("differs from the override"), std::string::npos) << Log();

    m_ngx.SetNearClipOverride(0.0f);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_FLOAT_EQ(m_pass->corrections.back().current.nearClip, 0.15f);
    m_ngx.SetNearClipOverride(-3.0f); // not a distance: ignored like zero
    EXPECT_FLOAT_EQ(m_ngx.GetNearClipOverride(), 0.0f);
}

TEST_F(MvCorrectionEvaluationTest, TheFiniteDepthModelIsNamedInTheProjectionLine) {
    SetUpCorrection();
    ReportLeftRightLeft();
    m_ngx.SetDepthModelInfinite(false);
    m_ngx.ArmDiagnostics(8);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_NE(Log().find("aspect=1.778 depthModel=finite"), std::string::npos) << Log();
}

// ---- Automatic near plane (vrDlssNearClip auto) ----

class AutoNearTest : public MvCorrectionEvaluationTest {
protected:
    void SetUpAuto() {
        SetUpCorrection();
        ReportLeftRightLeft();
        m_ngx.ArmDiagnostics(0); // creating the feature armed the diagnostics; the estimate must not need them
    }
    static MvSelfCheckReport NearReport(uint64_t evalIndex, float nearClip) {
        MvSelfCheckReport r;
        r.evalIndex = evalIndex;
        r.nearEstimate = true;
        r.nearSolution.valid = true;
        r.nearSolution.nearClip = nearClip;
        r.nearSolution.scale = 0.15 / nearClip;
        r.nearSolution.used = 500;
        r.nearSolution.total = 600;
        r.nearSolution.informative = 560;
        r.nearSolution.retained = 500;
        r.nearSolution.inlierFraction = 0.9;
        r.stats.gatedFraction = 0.125;
        r.eye = 1;
        return r;
    }
    size_t NearRequests() const {
        size_t n = 0;
        if (m_pass) for (const MvSelfCheckRequest& r : m_pass->selfChecks) n += r.nearEstimate ? 1 : 0;
        return n;
    }
};

TEST_F(AutoNearTest, AutoIsTheDefaultAndTheRecordsNearIsUsedUntilTheFirstEstimate) {
    SetUpAuto();
    EXPECT_FLOAT_EQ(m_ngx.GetNearClipOverride(), 0.0f);
    EXPECT_FLOAT_EQ(m_ngx.GetAutoNearEstimate(), 0.0f);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_FLOAT_EQ(m_pass->corrections[0].current.nearClip, 0.15f);
}

TEST_F(AutoNearTest, EveryFourthCrossEyeEvaluationRecordsAnEstimateOutsideADiagnosticsWindow) {
    SetUpAuto();
    ASSERT_EQ(m_ngx.GetDiagEvaluationsRemaining(), 0u);
    for (int i = 0; i < 8; ++i) MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_EQ(m_pass->selfChecks.size(), 2u);
    for (const MvSelfCheckRequest& r : m_pass->selfChecks) {
        EXPECT_TRUE(r.nearEstimate);
        // It targets the previous rendered frame's camera (the right eye, 0.064 m aside), not the same eye's.
        EXPECT_NEAR(r.toPrevious.c[0], -0.064f, 1e-5f);
        EXPECT_TRUE(r.current.infiniteDepth);
        EXPECT_FLOAT_EQ(r.current.nearClip, 0.15f); // the assumed near n0
    }
    EXPECT_EQ(m_pass->selfChecks[1].evalIndex - m_pass->selfChecks[0].evalIndex, 4u);
}

TEST_F(AutoNearTest, AnEstimateMovesTheCorrectionsNearForBothEyesAndIsShownOnTheProjectionLine) {
    SetUpAuto();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_NE(m_pass, nullptr);
    for (size_t i = 0; i < NearEstimator::kMinSolutions; ++i) m_pass->pendingReports.push_back(NearReport(0, 0.1f));
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_NEAR(m_ngx.GetAutoNearEstimate(), 0.1f, 1e-6f);

    m_ngx.ArmDiagnostics(8);
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    const MvCorrectionRequest& c = m_pass->corrections.back();
    EXPECT_NEAR(c.current.nearClip, 0.1f, 1e-6f);
    EXPECT_NEAR(c.previousFrame.nearClip, 0.1f, 1e-6f);
    EXPECT_NEAR(c.sameEye.nearClip, 0.1f, 1e-6f);
    ASSERT_FALSE(m_pass->selfChecks.empty());
    EXPECT_NEAR(m_pass->selfChecks.back().current.nearClip, 0.1f, 1e-6f);
    EXPECT_NE(Log().find("used=auto estimate=0.100 record=0.150 nearClip=0.100"), std::string::npos) << Log();
}

TEST_F(AutoNearTest, ANonZeroNearClipAlwaysWinsAndStopsTheEstimating) {
    SetUpAuto();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    for (size_t i = 0; i < NearEstimator::kMinSolutions; ++i) m_pass->pendingReports.push_back(NearReport(0, 0.1f));
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_GT(m_ngx.GetAutoNearEstimate(), 0.0f);

    m_ngx.SetNearClipOverride(0.12f);
    const size_t requestsBefore = NearRequests();
    for (int i = 0; i < 8; ++i) MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_FLOAT_EQ(m_pass->corrections.back().current.nearClip, 0.12f);
    EXPECT_EQ(NearRequests(), requestsBefore) << "no estimate is recorded while overridden";

    m_ngx.SetNearClipOverride(0.0f); // back to auto: the learned estimate is still good
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_NEAR(m_pass->corrections.back().current.nearClip, 0.1f, 1e-6f);
}

TEST_F(AutoNearTest, MissingRecordsRecordNoEstimate) {
    SetUpCorrection();
    m_ngx.ArmDiagnostics(0);
    for (int i = 0; i < 6; ++i) MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr); // no records at all
    EXPECT_EQ(NearRequests(), 0u);
}

TEST_F(AutoNearTest, ADiscontinuityFlushForgetsTheEstimateAndReadbacksStillInFlight) {
    SetUpAuto();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    for (size_t i = 0; i < NearEstimator::kMinSolutions; ++i) m_pass->pendingReports.push_back(NearReport(0, 0.1f));
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    ASSERT_GT(m_ngx.GetAutoNearEstimate(), 0.0f);

    m_ngx.TriggerDiscontinuityFlush(); // a map change, a mode switch
    EXPECT_FLOAT_EQ(m_ngx.GetAutoNearEstimate(), 0.0f);
    // A solution recorded before the flush and read back after it describes the old scene.
    for (size_t i = 0; i < NearEstimator::kMinSolutions; ++i) m_pass->pendingReports.push_back(NearReport(1, 0.1f));
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_FLOAT_EQ(m_ngx.GetAutoNearEstimate(), 0.0f);
    EXPECT_FLOAT_EQ(m_pass->corrections.back().current.nearClip, 0.15f) << "back to the camera record";
}

TEST_F(AutoNearTest, TheEstimateIsLoggedOncePerTwoSecondsWhileItMoves) {
    SetUpAuto();
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    auto count = [&]() {
        const std::string log = Log();
        size_t n = 0;
        for (size_t at = log.find("auto near "); at != std::string::npos; at = log.find("auto near ", at + 1)) ++n;
        return n;
    };
    for (size_t i = 0; i < NearEstimator::kMinSolutions; ++i) m_pass->pendingReports.push_back(NearReport(0, 0.1f));
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_NE(Log().find("NgxInterceptor: auto near 0.100 m (record 0.150, Eye=1 inliers=90.0% (500 of 500) gated=12.5%)"), std::string::npos) << Log();
    EXPECT_EQ(count(), 1u);

    for (size_t i = 0; i < NearEstimator::kWindow; ++i) m_pass->pendingReports.push_back(NearReport(0, 0.15f)); // a change of more than 5%
    MockEvaluateFeature(m_cmdList, m_handle, &m_params, nullptr);
    EXPECT_NEAR(m_ngx.GetAutoNearEstimate(), 0.15f, 1e-6f);
    EXPECT_EQ(count(), 1u) << "throttled: less than 2 s since the last line";
}

} // namespace

} // namespace fs25vr
