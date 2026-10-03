#include <gtest/gtest.h>
#include "ngx/ngx_defs.h"
#include "ngx/virtual_ngx_handle.h"
#include "ngx/ngx_interceptor.h"
#include "luau/vr_bridge.h"
#include <vector>
#include <memory>

namespace fs25vr {
namespace {

static int s_createCount = 0;
static int s_releaseCount = 0;
static int s_evaluateCount = 0;
static std::vector<const NVSDK_NGX_Handle*> s_evalHandles;
static std::vector<int> s_evalReset;
static std::vector<float> s_evalMvScaleX;
static std::vector<float> s_evalMvScaleY;
static std::vector<int> s_evalPresets;
static std::vector<float> s_evalSharpness;

NVSDK_NGX_Result STDMETHODCALLTYPE MockCreate(
    ID3D12GraphicsCommandList* /*InCmdList*/,
    NVSDK_NGX_Feature /*InFeatureID*/,
    NVSDK_NGX_Parameter* /*InParameters*/,
    NVSDK_NGX_Handle** OutHandle
) {
    s_createCount++;
    auto handle = new NVSDK_NGX_Handle();
    handle->Id = static_cast<unsigned int>(200 + s_createCount);
    *OutHandle = handle;
    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result STDMETHODCALLTYPE MockRelease(
    NVSDK_NGX_Handle* InHandle
) {
    s_releaseCount++;
    delete InHandle;
    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result STDMETHODCALLTYPE MockEvaluate(
    ID3D12GraphicsCommandList* /*InCmdList*/,
    const NVSDK_NGX_Handle* InFeatureHandle,
    const NVSDK_NGX_Parameter* InParameters,
    PFN_NVSDK_NGX_ProgressCallback /*InCallback*/
) {
    s_evaluateCount++;
    s_evalHandles.push_back(InFeatureHandle);

    int reset = 0;
    if (InParameters && NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset))) {
        s_evalReset.push_back(reset);
    } else {
        s_evalReset.push_back(0);
    }

    float mvX = 1.0f, mvY = 1.0f;
    if (InParameters && NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvX))) {
        s_evalMvScaleX.push_back(mvX);
    } else {
        s_evalMvScaleX.push_back(1.0f);
    }

    if (InParameters && NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &mvY))) {
        s_evalMvScaleY.push_back(mvY);
    } else {
        s_evalMvScaleY.push_back(1.0f);
    }

    int preset = 0;
    if (InParameters && NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, &preset))) {
        s_evalPresets.push_back(preset);
    } else {
        s_evalPresets.push_back(0);
    }

    float sharp = 0.0f;
    if (InParameters && NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_Sharpness, &sharp))) {
        s_evalSharpness.push_back(sharp);
    } else {
        s_evalSharpness.push_back(0.0f);
    }

    return NVSDK_NGX_Result_Success;
}

class NgxInterceptorTest : public ::testing::Test {
protected:
    void SetUp() override {
        s_createCount = 0;
        s_releaseCount = 0;
        s_evaluateCount = 0;
        s_evalHandles.clear();
        s_evalReset.clear();
        s_evalMvScaleX.clear();
        s_evalMvScaleY.clear();
        s_evalPresets.clear();
        s_evalSharpness.clear();

        NgxInterceptor::Instance().DetachAll();
        NgxInterceptor::Instance().SetVRBridge(nullptr);
        VRBridge::Instance().SetRenderingMode(VRRenderingMode::AFR);
        VRBridge::Instance().SetSubPassIndex(0);
        VRBridge::Instance().SetFrameParity(0);
        VRBridge::Instance().Set2DViewActive(false);
        VRBridge::Instance().SetMenuVisible(false);
    }

    void TearDown() override {
        NgxInterceptor::Instance().DetachAll();
        NgxInterceptor::Instance().SetVRBridge(nullptr);
        VRBridge::Instance().SetRenderingMode(VRRenderingMode::AFR);
        VRBridge::Instance().SetSubPassIndex(0);
        VRBridge::Instance().SetFrameParity(0);
        VRBridge::Instance().Set2DViewActive(false);
        VRBridge::Instance().SetMenuVisible(false);
    }
};

TEST_F(NgxInterceptorTest, GetActiveDualPassEyeIndexReflectsSubPassIndex) {
    auto& interceptor = NgxInterceptor::Instance();
    auto& bridge = VRBridge::Instance();
    interceptor.SetVRBridge(&bridge);

    bridge.SetSubPassIndex(0);
    EXPECT_EQ(interceptor.GetActiveDualPassEyeIndex(), 0u);

    bridge.SetSubPassIndex(1);
    EXPECT_EQ(interceptor.GetActiveDualPassEyeIndex(), 1u);

    bridge.SetSubPassIndex(0);
    EXPECT_EQ(interceptor.GetActiveDualPassEyeIndex(), 0u);
}

TEST_F(NgxInterceptorTest, SubPassRoutingInSequentialDualPassRoutesLeftAndRight) {
    auto& interceptor = NgxInterceptor::Instance();
    interceptor.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate);

    auto& bridge = VRBridge::Instance();
    interceptor.SetVRBridge(&bridge);
    bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreate(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);

    // Frame 1 - Sub-Pass 0 -> evaluates hLeft
    bridge.SetSubPassIndex(0);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalHandles.size(), 1u);
    EXPECT_EQ(s_evalHandles[0], vHandle->hLeft);

    // Frame 1 - Sub-Pass 1 -> evaluates hRight
    bridge.SetSubPassIndex(1);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalHandles.size(), 2u);
    EXPECT_EQ(s_evalHandles[1], vHandle->hRight);

    // Frame 2 - Sub-Pass 0 -> evaluates hLeft
    bridge.SetSubPassIndex(0);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalHandles.size(), 3u);
    EXPECT_EQ(s_evalHandles[2], vHandle->hLeft);

    // Frame 2 - Sub-Pass 1 -> evaluates hRight
    bridge.SetSubPassIndex(1);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalHandles.size(), 4u);
    EXPECT_EQ(s_evalHandles[3], vHandle->hRight);

    MockRelease(virtualHandle);
}

TEST_F(NgxInterceptorTest, MotionVectorScaleClampedTo1xInDualPassRegardlessOfSetting) {
    auto& interceptor = NgxInterceptor::Instance();
    interceptor.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate);

    auto& bridge = VRBridge::Instance();
    interceptor.SetVRBridge(&bridge);
    bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    // Set custom MV scale multiplier e.g. 2.0x (typical for AFR)
    interceptor.SetMvScaleMultiplier(2.0f, 2.0f);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_X, 0.5f);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 0.5f);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreate(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    // Sub-Pass 0 (Left Eye) -> MV scale multiplier must be locked to 1.0x (0.5f * 1.0f = 0.5f)
    bridge.SetSubPassIndex(0);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalMvScaleX.size(), 1u);
    ASSERT_EQ(s_evalMvScaleY.size(), 1u);
    EXPECT_FLOAT_EQ(s_evalMvScaleX[0], 0.5f);
    EXPECT_FLOAT_EQ(s_evalMvScaleY[0], 0.5f);

    // Sub-Pass 1 (Right Eye) -> MV scale multiplier must be locked to 1.0x (0.5f * 1.0f = 0.5f)
    bridge.SetSubPassIndex(1);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalMvScaleX.size(), 2u);
    ASSERT_EQ(s_evalMvScaleY.size(), 2u);
    EXPECT_FLOAT_EQ(s_evalMvScaleX[1], 0.5f);
    EXPECT_FLOAT_EQ(s_evalMvScaleY[1], 0.5f);

    // Caller's parameters must remain intact
    float restoredX = 0.0f, restoredY = 0.0f;
    EXPECT_EQ(params.Get(NVSDK_NGX_Parameter_MV_Scale_X, &restoredX), NVSDK_NGX_Result_Success);
    EXPECT_EQ(params.Get(NVSDK_NGX_Parameter_MV_Scale_Y, &restoredY), NVSDK_NGX_Result_Success);
    EXPECT_FLOAT_EQ(restoredX, 0.5f);
    EXPECT_FLOAT_EQ(restoredY, 0.5f);

    MockRelease(virtualHandle);
}

TEST_F(NgxInterceptorTest, ModeTransitionLatchesResetForOneFrameAcrossBothHandles) {
    auto& interceptor = NgxInterceptor::Instance();
    interceptor.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate);

    auto& bridge = VRBridge::Instance();
    interceptor.SetVRBridge(&bridge);

    // Start in AFR mode
    bridge.SetRenderingMode(VRRenderingMode::AFR);
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreate(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    // Frame 0 in AFR: no reset
    bridge.SetFrameParity(0);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    bridge.SetFrameParity(1);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evalReset.size(), 2u);
    EXPECT_EQ(s_evalReset[0], 0);
    EXPECT_EQ(s_evalReset[1], 0);

    // Switch to Sequential Dual-Pass!
    bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    // Frame 1 in Dual-Pass: Sub-Pass 0 and Sub-Pass 1 must BOTH receive Reset = 1
    bridge.SetSubPassIndex(0);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    bridge.SetSubPassIndex(1);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);

    ASSERT_EQ(s_evalReset.size(), 4u);
    EXPECT_EQ(s_evalReset[2], 1); // hLeft reset
    EXPECT_EQ(s_evalReset[3], 1); // hRight reset

    // Frame 2 in Dual-Pass: subsequent frame must NOT have reset (latched for exactly one frame)
    bridge.SetSubPassIndex(0);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    bridge.SetSubPassIndex(1);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);

    ASSERT_EQ(s_evalReset.size(), 6u);
    EXPECT_EQ(s_evalReset[4], 0);
    EXPECT_EQ(s_evalReset[5], 0);

    // Switch back to AFR!
    bridge.SetRenderingMode(VRRenderingMode::AFR);

    // Frame 3 in AFR: Parity 0 (hLeft) and Parity 1 (hRight) must BOTH receive Reset = 1
    bridge.SetFrameParity(0);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    bridge.SetFrameParity(1);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);

    ASSERT_EQ(s_evalReset.size(), 8u);
    EXPECT_EQ(s_evalReset[6], 1); // hLeft reset
    EXPECT_EQ(s_evalReset[7], 1); // hRight reset

    // Frame 4 in AFR: subsequent frame has Reset = 0
    bridge.SetFrameParity(0);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    bridge.SetFrameParity(1);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);

    ASSERT_EQ(s_evalReset.size(), 10u);
    EXPECT_EQ(s_evalReset[8], 0);
    EXPECT_EQ(s_evalReset[9], 0);

    MockRelease(virtualHandle);
}

TEST_F(NgxInterceptorTest, DetachAllResetsDlssTuningOverridesToDefaults) {
    auto& interceptor = NgxInterceptor::Instance();
    interceptor.SetDlssPresetOverride(NVSDK_NGX_DLSS_Hint_Render_Preset_K + 1);
    interceptor.SetSharpnessOverride(0.65f, false);
    interceptor.SetMvScaleMultiplier(0.5f, 0.75f);

    interceptor.DetachAll();

    EXPECT_EQ(interceptor.GetDlssPresetOverride(), NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    EXPECT_FLOAT_EQ(interceptor.GetSharpnessOverride(), 0.50f);
    EXPECT_TRUE(interceptor.IsForceSharpnessOverride());
    EXPECT_FLOAT_EQ(interceptor.GetMvScaleMultiplierX(), NgxInterceptor::kDefaultAfrMvScale);
    EXPECT_FLOAT_EQ(interceptor.GetMvScaleMultiplierY(), NgxInterceptor::kDefaultAfrMvScale);
}

TEST_F(NgxInterceptorTest, SharpnessAndPresetOverridesAppliedToBothContextsInDualPass) {
    auto& interceptor = NgxInterceptor::Instance();
    interceptor.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate);

    auto& bridge = VRBridge::Instance();
    interceptor.SetVRBridge(&bridge);
    bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    interceptor.SetDlssPresetOverride(NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    interceptor.SetSharpnessOverride(0.42f, true);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreate(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    // Sub-Pass 0
    bridge.SetSubPassIndex(0);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);

    // Sub-Pass 1
    bridge.SetSubPassIndex(1);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);

    ASSERT_EQ(s_evalPresets.size(), 2u);
    ASSERT_EQ(s_evalSharpness.size(), 2u);

    EXPECT_EQ(s_evalPresets[0], NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    EXPECT_EQ(s_evalPresets[1], NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    EXPECT_FLOAT_EQ(s_evalSharpness[0], 0.42f);
    EXPECT_FLOAT_EQ(s_evalSharpness[1], 0.42f);

    MockRelease(virtualHandle);
}

} // namespace
} // namespace fs25vr
