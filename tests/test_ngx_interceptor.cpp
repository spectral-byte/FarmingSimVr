#include <gtest/gtest.h>
#include "ngx/ngx_defs.h"
#include "ngx/virtual_ngx_handle.h"
#include "ngx/ngx_interceptor.h"
#include "luau/vr_bridge.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <vector>

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
static std::atomic<bool> s_evalInFlight{false};
static std::atomic<bool> s_releasedDuringInFlightEval{false};
static std::function<void()> s_onEvaluate;

__declspec(noinline) NVSDK_NGX_Result STDMETHODCALLTYPE MockCreate(
    ID3D12GraphicsCommandList*,
    NVSDK_NGX_Feature,
    NVSDK_NGX_Parameter*,
    NVSDK_NGX_Handle** OutHandle
) {
    s_createCount++;
    auto handle = new NVSDK_NGX_Handle();
    handle->Id = static_cast<unsigned int>(200 + s_createCount);
    *OutHandle = handle;
    return NVSDK_NGX_Result_Success;
}

__declspec(noinline) NVSDK_NGX_Result STDMETHODCALLTYPE MockRelease(
    NVSDK_NGX_Handle* InHandle
) {
    if (s_evalInFlight.load()) {
        s_releasedDuringInFlightEval.store(true);
    }
    s_releaseCount++;
    delete InHandle;
    return NVSDK_NGX_Result_Success;
}

__declspec(noinline) NVSDK_NGX_Result STDMETHODCALLTYPE MockEvaluate(
    ID3D12GraphicsCommandList*,
    const NVSDK_NGX_Handle* InFeatureHandle,
    const NVSDK_NGX_Parameter* InParameters,
    PFN_NVSDK_NGX_ProgressCallback
) {
    s_evalInFlight.store(true);
    if (s_onEvaluate) {
        s_onEvaluate();
    }
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

    s_evalInFlight.store(false);
    return NVSDK_NGX_Result_Success;
}

class NgxInterceptorTest : public ::testing::Test {
protected:
    std::shared_ptr<NgxInterceptor> m_ngxShared = std::make_shared<NgxInterceptor>();
    NgxInterceptor& m_ngx = *m_ngxShared;
    std::shared_ptr<VRBridge> m_bridgeShared = std::make_shared<VRBridge>(std::make_shared<OpenXrManager>(std::make_shared<NativeOpenXrDispatch>()), m_ngxShared);
    VRBridge& m_bridge = *m_bridgeShared;

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
        s_evalInFlight.store(false);
        s_releasedDuringInFlightEval.store(false);
        s_onEvaluate = nullptr;

        m_ngx.DetachAll();
        m_ngx.SetVRBridge(nullptr);
        m_bridge.SetRenderingMode(VRRenderingMode::AFR);
        m_bridge.SetSubPassIndex(0);
        m_bridge.SetFrameParity(0);
        m_bridge.Set2DViewActive(false);
        m_bridge.SetMenuVisible(false);
    }

    void TearDown() override {
        s_onEvaluate = nullptr;
        s_evalInFlight.store(false);
        m_ngx.DetachAll();
        m_ngx.SetVRBridge(nullptr);
        m_bridge.SetRenderingMode(VRRenderingMode::AFR);
        m_bridge.SetSubPassIndex(0);
        m_bridge.SetFrameParity(0);
        m_bridge.Set2DViewActive(false);
        m_bridge.SetMenuVisible(false);
    }
};

TEST_F(NgxInterceptorTest, SubPassRoutingInSequentialDualPassRoutesLeftAndRight) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate);

    auto& bridge = m_bridge;
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

    bridge.SetSubPassIndex(0);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalHandles.size(), 1u);
    EXPECT_EQ(s_evalHandles[0], vHandle->hLeft);

    bridge.SetSubPassIndex(1);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalHandles.size(), 2u);
    EXPECT_EQ(s_evalHandles[1], vHandle->hRight);

    bridge.SetSubPassIndex(0);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalHandles.size(), 3u);
    EXPECT_EQ(s_evalHandles[2], vHandle->hLeft);

    bridge.SetSubPassIndex(1);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalHandles.size(), 4u);
    EXPECT_EQ(s_evalHandles[3], vHandle->hRight);

    MockRelease(virtualHandle);
}

TEST_F(NgxInterceptorTest, MotionVectorScaleClampedTo1xInDualPassRegardlessOfSetting) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate);

    auto& bridge = m_bridge;
    interceptor.SetVRBridge(&bridge);
    bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    interceptor.SetMvScaleMultiplier(2.0f, 2.0f);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_X, 0.5f);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 0.5f);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreate(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    bridge.SetSubPassIndex(0);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalMvScaleX.size(), 1u);
    ASSERT_EQ(s_evalMvScaleY.size(), 1u);
    EXPECT_FLOAT_EQ(s_evalMvScaleX[0], 0.5f);
    EXPECT_FLOAT_EQ(s_evalMvScaleY[0], 0.5f);

    bridge.SetSubPassIndex(1);
    EXPECT_EQ(MockEvaluate(nullptr, virtualHandle, &params, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalMvScaleX.size(), 2u);
    ASSERT_EQ(s_evalMvScaleY.size(), 2u);
    EXPECT_FLOAT_EQ(s_evalMvScaleX[1], 0.5f);
    EXPECT_FLOAT_EQ(s_evalMvScaleY[1], 0.5f);

    float restoredX = 0.0f, restoredY = 0.0f;
    EXPECT_EQ(params.Get(NVSDK_NGX_Parameter_MV_Scale_X, &restoredX), NVSDK_NGX_Result_Success);
    EXPECT_EQ(params.Get(NVSDK_NGX_Parameter_MV_Scale_Y, &restoredY), NVSDK_NGX_Result_Success);
    EXPECT_FLOAT_EQ(restoredX, 0.5f);
    EXPECT_FLOAT_EQ(restoredY, 0.5f);

    MockRelease(virtualHandle);
}

TEST_F(NgxInterceptorTest, ModeTransitionLatchesResetForOneFrameAcrossBothHandles) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate);

    auto& bridge = m_bridge;
    interceptor.SetVRBridge(&bridge);

    bridge.SetRenderingMode(VRRenderingMode::AFR);
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreate(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle), NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    bridge.SetFrameParity(0);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    bridge.SetFrameParity(1);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    ASSERT_EQ(s_evalReset.size(), 2u);
    EXPECT_EQ(s_evalReset[0], 0);
    EXPECT_EQ(s_evalReset[1], 0);

    bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    bridge.SetSubPassIndex(0);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    bridge.SetSubPassIndex(1);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);

    ASSERT_EQ(s_evalReset.size(), 4u);
    EXPECT_EQ(s_evalReset[2], 1);
    EXPECT_EQ(s_evalReset[3], 1);

    bridge.SetSubPassIndex(0);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    bridge.SetSubPassIndex(1);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);

    ASSERT_EQ(s_evalReset.size(), 6u);
    EXPECT_EQ(s_evalReset[4], 0);
    EXPECT_EQ(s_evalReset[5], 0);

    bridge.SetRenderingMode(VRRenderingMode::AFR);

    bridge.SetFrameParity(0);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    bridge.SetFrameParity(1);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);

    ASSERT_EQ(s_evalReset.size(), 8u);
    EXPECT_EQ(s_evalReset[6], 1);
    EXPECT_EQ(s_evalReset[7], 1);

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
    auto& interceptor = m_ngx;
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
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate);

    auto& bridge = m_bridge;
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

    bridge.SetSubPassIndex(0);
    MockEvaluate(nullptr, virtualHandle, &params, nullptr);

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

TEST_F(NgxInterceptorTest, EntryPointsFailSafeWhenNoInterceptorOwnsTheDetours) {
    ASSERT_FALSE(m_ngx.IsAttached());
    NgxParameterImpl params;
    NVSDK_NGX_Handle handle{};
    NVSDK_NGX_Handle* outHandle = &handle;
    NVSDK_NGX_Parameter* outParams = nullptr;

    EXPECT_EQ(NgxInterceptor::Intercept_CreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &outHandle),
              NVSDK_NGX_Result_Fail);
    EXPECT_EQ(outHandle, nullptr);
    EXPECT_EQ(NgxInterceptor::Intercept_ReleaseFeature(&handle), NVSDK_NGX_Result_Fail);
    EXPECT_EQ(NgxInterceptor::Intercept_EvaluateFeature(nullptr, &handle, &params, nullptr), NVSDK_NGX_Result_Fail);
    EXPECT_EQ(NgxInterceptor::Intercept_AllocateParameters(&outParams), NVSDK_NGX_Result_Fail);
    EXPECT_EQ(NgxInterceptor::Intercept_GetCapabilityParameters(&outParams), NVSDK_NGX_Result_Fail);
    EXPECT_EQ(outParams, nullptr);
    EXPECT_EQ(NgxInterceptor::Intercept_DestroyParameters(&params), NVSDK_NGX_Result_Fail);
    EXPECT_EQ(s_createCount + s_releaseCount + s_evaluateCount, 0);
}

TEST_F(NgxInterceptorTest, SecondInterceptorCannotTakeOverLiveModuleLoadHooks) {
    ASSERT_TRUE(m_ngx.InstallModuleLoadHooks());

    NgxInterceptor other;
    EXPECT_FALSE(other.InstallModuleLoadHooks());
    EXPECT_FALSE(other.AreModuleHooksInstalled());

    HMODULE kernel32 = LoadLibraryW(L"kernel32.dll");
    EXPECT_NE(kernel32, nullptr);
    if (kernel32) FreeLibrary(kernel32);

    m_ngx.UninstallModuleLoadHooks();
    EXPECT_TRUE(other.InstallModuleLoadHooks());
    other.DetachAll();
}

TEST_F(NgxInterceptorTest, DetachLiveHooksUnwindsWhicheverInterceptorOwnsThem) {
    ASSERT_TRUE(m_ngx.InstallModuleLoadHooks());
    ASSERT_TRUE(m_ngx.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate));

    NgxInterceptor::DetachLiveHooks();

    EXPECT_FALSE(m_ngx.AreModuleHooksInstalled());
    EXPECT_FALSE(m_ngx.IsAttached());
}

namespace {
FunctionDetour g_blockerLoadLibExW;

HMODULE WINAPI PassthroughLoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    auto orig = reinterpret_cast<decltype(&LoadLibraryExW)>(g_blockerLoadLibExW.GetOriginal());
    return orig ? orig(lpLibFileName, hFile, dwFlags) : nullptr;
}
}

TEST_F(NgxInterceptorTest, NonSuperSamplingFeaturesPassThroughUntouched) {
    auto& interceptor = m_ngx;
    ASSERT_TRUE(interceptor.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate));
    interceptor.SetVRBridge(&m_bridge);
    interceptor.SetDlssPresetOverride(NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    interceptor.SetSharpnessOverride(0.42f, true);

    NgxParameterImpl createParams;
    createParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    createParams.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* nonSrHandle = nullptr;
    ASSERT_EQ(MockCreate(nullptr, NVSDK_NGX_Feature_SlowMotion, &createParams, &nonSrHandle), NVSDK_NGX_Result_Success);
    ASSERT_NE(nonSrHandle, nullptr);
    EXPECT_EQ(s_createCount, 1);
    EXPECT_FALSE(interceptor.IsVirtualHandle(nonSrHandle));
    EXPECT_EQ(interceptor.GetActiveVirtualHandleCount(), 0u);
    EXPECT_FALSE(createParams.HasParam(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset));

    m_bridge.SignalDiscontinuity();
    ASSERT_TRUE(m_bridge.HasPendingDiscontinuity(0));

    NgxParameterImpl evalParams;
    evalParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    evalParams.Set(NVSDK_NGX_Parameter_Height, 1080u);
    ASSERT_EQ(MockEvaluate(nullptr, nonSrHandle, &evalParams, nullptr), NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_evalHandles.size(), 1u);
    EXPECT_EQ(s_evalHandles[0], nonSrHandle);
    EXPECT_EQ(s_evalPresets[0], 0);
    EXPECT_FLOAT_EQ(s_evalSharpness[0], 0.0f);
    EXPECT_EQ(s_evalReset[0], 0);
    EXPECT_TRUE(m_bridge.HasPendingDiscontinuity(0));

    ASSERT_EQ(MockRelease(nonSrHandle), NVSDK_NGX_Result_Success);
    EXPECT_EQ(s_releaseCount, 1);
}

TEST_F(NgxInterceptorTest, OverriddenSharpnessAndPresetAreRestoredAfterEvaluation) {
    auto& interceptor = m_ngx;
    ASSERT_TRUE(interceptor.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate));
    interceptor.SetVRBridge(&m_bridge);
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);

    NgxParameterImpl createParams;
    createParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    createParams.Set(NVSDK_NGX_Parameter_Height, 1080u);
    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreate(nullptr, NVSDK_NGX_Feature_SuperSampling, &createParams, &virtualHandle), NVSDK_NGX_Result_Success);
    int createPreset = 0;
    createParams.Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, &createPreset);
    EXPECT_EQ(createPreset, 0);

    interceptor.SetDlssPresetOverride(NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    interceptor.SetSharpnessOverride(0.42f, true);

    NgxParameterImpl evalParams;
    evalParams.Set(NVSDK_NGX_Parameter_Width, 1920u);
    evalParams.Set(NVSDK_NGX_Parameter_Height, 1080u);

    ASSERT_EQ(MockEvaluate(nullptr, virtualHandle, &evalParams, nullptr), NVSDK_NGX_Result_Success);
    EXPECT_EQ(s_evalPresets[0], NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    EXPECT_FLOAT_EQ(s_evalSharpness[0], 0.42f);

    int restoredPreset = -1;
    int restoredQualityPreset = -1;
    float restoredSharpness = -1.0f;
    evalParams.Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, &restoredPreset);
    evalParams.Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, &restoredQualityPreset);
    evalParams.Get(NVSDK_NGX_Parameter_Sharpness, &restoredSharpness);
    EXPECT_EQ(restoredPreset, 0);
    EXPECT_EQ(restoredQualityPreset, 0);
    EXPECT_FLOAT_EQ(restoredSharpness, 0.0f);

    interceptor.SetDlssPresetOverride(0);
    interceptor.SetSharpnessOverride(-1.0f, false);
    ASSERT_EQ(MockEvaluate(nullptr, virtualHandle, &evalParams, nullptr), NVSDK_NGX_Result_Success);
    EXPECT_EQ(s_evalPresets[1], 0);
    EXPECT_FLOAT_EQ(s_evalSharpness[1], 0.0f);

    NgxParameterImpl callerParams;
    callerParams.Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, 3);
    callerParams.Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, 4);
    callerParams.Set(NVSDK_NGX_Parameter_Sharpness, 0.75f);
    interceptor.SetDlssPresetOverride(NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    interceptor.SetSharpnessOverride(0.42f, true);

    ASSERT_EQ(MockEvaluate(nullptr, virtualHandle, &callerParams, nullptr), NVSDK_NGX_Result_Success);
    EXPECT_EQ(s_evalPresets[2], 3);
    EXPECT_FLOAT_EQ(s_evalSharpness[2], 0.42f);

    int callerPreset = 0;
    int callerQualityPreset = 0;
    float callerSharpness = 0.0f;
    EXPECT_EQ(callerParams.Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, &callerPreset), NVSDK_NGX_Result_Success);
    EXPECT_EQ(callerParams.Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, &callerQualityPreset), NVSDK_NGX_Result_Success);
    EXPECT_EQ(callerParams.Get(NVSDK_NGX_Parameter_Sharpness, &callerSharpness), NVSDK_NGX_Result_Success);
    EXPECT_EQ(callerPreset, 3);
    EXPECT_EQ(callerQualityPreset, 4);
    EXPECT_FLOAT_EQ(callerSharpness, 0.75f);

    MockRelease(virtualHandle);
}

TEST_F(NgxInterceptorTest, DatafileAndImageResourceLoadsAreIgnoredByModuleHooks) {
    ASSERT_TRUE(m_ngx.InstallModuleLoadHooks());
    const std::wstring nvngxPath = std::filesystem::path(FAKE_NVNGX_PATH).make_preferred().wstring();

    HMODULE hDatafile = LoadLibraryExW(nvngxPath.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    ASSERT_NE(hDatafile, nullptr);
    EXPECT_FALSE(m_ngx.IsAttached());
    FreeLibrary(hDatafile);

    HMODULE hImageRes = LoadLibraryExW(nvngxPath.c_str(), nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    ASSERT_NE(hImageRes, nullptr);
    EXPECT_FALSE(m_ngx.IsAttached());
    FreeLibrary(hImageRes);

    HMODULE hReal = LoadLibraryW(nvngxPath.c_str());
    ASSERT_NE(hReal, nullptr);
    EXPECT_TRUE(m_ngx.IsAttached());
    m_ngx.DetachAll();
    FreeLibrary(hReal);
}

TEST_F(NgxInterceptorTest, InstallModuleLoadHooksFailsAndRollsBackWhenDetourFails) {
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    ASSERT_NE(kernel32, nullptr);
    void* pfnLoadLibExW = reinterpret_cast<void*>(GetProcAddress(kernel32, "LoadLibraryExW"));
    ASSERT_NE(pfnLoadLibExW, nullptr);

    ASSERT_TRUE(g_blockerLoadLibExW.Install(pfnLoadLibExW, reinterpret_cast<void*>(&PassthroughLoadLibraryExW)));
    EXPECT_FALSE(m_ngx.InstallModuleLoadHooks());
    EXPECT_FALSE(m_ngx.AreModuleHooksInstalled());

    ASSERT_TRUE(g_blockerLoadLibExW.Uninstall());
    EXPECT_TRUE(m_ngx.InstallModuleLoadHooks());
    EXPECT_TRUE(m_ngx.AreModuleHooksInstalled());
    m_ngx.DetachAll();
}

TEST_F(NgxInterceptorTest, ConcurrentReleaseWaitsForInFlightEvaluation) {
    ASSERT_TRUE(m_ngx.AttachDirect(&MockCreate, &MockRelease, &MockEvaluate));
    m_ngx.SetVRBridge(&m_bridge);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    ASSERT_EQ(MockCreate(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle),
              NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    std::promise<void> evalEntered;
    std::promise<void> allowEvalExit;
    auto evalEnteredFuture = evalEntered.get_future();
    auto allowEvalExitFuture = allowEvalExit.get_future().share();
    s_onEvaluate = [&]() {
        evalEntered.set_value();
        allowEvalExitFuture.wait();
    };

    std::thread evalThread([&]() {
        MockEvaluate(nullptr, virtualHandle, &params, nullptr);
    });

    evalEnteredFuture.wait();

    std::atomic<bool> releaseFinished{false};
    std::thread releaseThread([&]() {
        EXPECT_EQ(MockRelease(virtualHandle), NVSDK_NGX_Result_Success);
        releaseFinished.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(s_releasedDuringInFlightEval.load());
    EXPECT_FALSE(releaseFinished.load());

    allowEvalExit.set_value();
    evalThread.join();
    releaseThread.join();

    EXPECT_FALSE(s_releasedDuringInFlightEval.load());
    EXPECT_EQ(s_releaseCount, 2);
    EXPECT_EQ(m_ngx.GetActiveVirtualHandleCount(), 0u);
}

}
}
