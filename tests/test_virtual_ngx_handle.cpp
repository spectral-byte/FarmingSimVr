#include <gtest/gtest.h>
#include "ngx/ngx_defs.h"
#include "ngx/virtual_ngx_handle.h"
#include "ngx/ngx_interceptor.h"
#include <vector>
#include <memory>
#include <algorithm>

namespace fs25vr {
namespace {

static int s_mockCreateCallCount = 0;
static int s_mockReleaseCallCount = 0;
static std::vector<NVSDK_NGX_Handle*> s_releasedHandles;
static std::vector<NVSDK_NGX_Handle*> s_createdHandles;
static std::vector<std::shared_ptr<NgxParameterImpl>> s_capturedParamsPerCreate;
static bool s_mockFailRightCreation = false;

__declspec(noinline) NVSDK_NGX_Result STDMETHODCALLTYPE MockCreateFeature(
    ID3D12GraphicsCommandList*,
    NVSDK_NGX_Feature,
    NVSDK_NGX_Parameter* InParameters,
    NVSDK_NGX_Handle** OutHandle
) {
    volatile int a = 1;
    volatile int b = 2;
    volatile int c = a + b;
    (void)c;

    s_mockCreateCallCount++;
    if (s_mockFailRightCreation && (s_mockCreateCallCount % 2 == 0)) {
        return NVSDK_NGX_Result_Fail;
    }

    auto captured = std::make_shared<NgxParameterImpl>();
    if (InParameters) {
        captured->CopyFrom(InParameters);
    }
    s_capturedParamsPerCreate.push_back(captured);

    auto handle = new NVSDK_NGX_Handle();
    handle->Id = static_cast<unsigned int>(100 + s_mockCreateCallCount);
    s_createdHandles.push_back(handle);
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

    s_mockReleaseCallCount++;
    s_releasedHandles.push_back(InHandle);
    delete InHandle;
    return NVSDK_NGX_Result_Success;
}

static int s_mockAllocCallCount = 0;
static int s_mockDestroyCallCount = 0;
static int s_mockGetCapsCallCount = 0;

__declspec(noinline) NVSDK_NGX_Result STDMETHODCALLTYPE MockAllocateParameters(
    NVSDK_NGX_Parameter** OutParameters
) {
    volatile int a = 5;
    volatile int b = 6;
    volatile int c = a + b;
    (void)c;

    s_mockAllocCallCount++;
    *OutParameters = new NgxParameterImpl();
    return NVSDK_NGX_Result_Success;
}

__declspec(noinline) NVSDK_NGX_Result STDMETHODCALLTYPE MockDestroyParameters(
    NVSDK_NGX_Parameter* InParameters
) {
    volatile int a = 7;
    volatile int b = 8;
    volatile int c = a + b;
    (void)c;

    s_mockDestroyCallCount++;
    delete static_cast<NgxParameterImpl*>(InParameters);
    return NVSDK_NGX_Result_Success;
}

__declspec(noinline) NVSDK_NGX_Result STDMETHODCALLTYPE MockGetCapabilityParameters(
    NVSDK_NGX_Parameter** OutParameters
) {
    volatile int a = 9;
    volatile int b = 10;
    volatile int c = a + b;
    (void)c;

    s_mockGetCapsCallCount++;
    *OutParameters = new NgxParameterImpl();
    return NVSDK_NGX_Result_Success;
}

class VirtualNgxHandleTest : public ::testing::Test {
protected:
    NgxInterceptor m_ngx;
protected:
    void SetUp() override {
        s_mockCreateCallCount = 0;
        s_mockReleaseCallCount = 0;
        s_mockAllocCallCount = 0;
        s_mockDestroyCallCount = 0;
        s_mockGetCapsCallCount = 0;
        s_releasedHandles.clear();
        s_createdHandles.clear();
        s_capturedParamsPerCreate.clear();
        s_mockFailRightCreation = false;

        m_ngx.DetachAll();
    }

    void TearDown() override {
        m_ngx.DetachAll();
    }
};

TEST_F(VirtualNgxHandleTest, ParameterStorageCloningAndConversions) {
    NgxParameterImpl params;

    params.Set(NVSDK_NGX_Parameter_Width, 3840u);
    params.Set(NVSDK_NGX_Parameter_Height, 2160u);
    params.Set(NVSDK_NGX_Parameter_PerfQualityValue, 1);
    params.Set(NVSDK_NGX_Parameter_Sharpness, 0.75f);
    params.Set("CustomULL", 9876543210ULL);

    unsigned int width = 0;
    EXPECT_EQ(params.Get(NVSDK_NGX_Parameter_Width, &width), NVSDK_NGX_Result_Success);
    EXPECT_EQ(width, 3840u);

    float sharpness = 0.0f;
    EXPECT_EQ(params.Get(NVSDK_NGX_Parameter_Sharpness, &sharpness), NVSDK_NGX_Result_Success);
    EXPECT_FLOAT_EQ(sharpness, 0.75f);

    int widthAsInt = 0;
    EXPECT_EQ(params.Get(NVSDK_NGX_Parameter_Width, &widthAsInt), NVSDK_NGX_Result_Success);
    EXPECT_EQ(widthAsInt, 3840);

    double sharpnessAsDouble = 0.0;
    EXPECT_EQ(params.Get(NVSDK_NGX_Parameter_Sharpness, &sharpnessAsDouble), NVSDK_NGX_Result_Success);
    EXPECT_NEAR(sharpnessAsDouble, 0.75, 1e-5);

    auto cloned = params.Clone();
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->GetParamCount(), params.GetParamCount());

    unsigned int clonedWidth = 0;
    EXPECT_EQ(cloned->Get(NVSDK_NGX_Parameter_Width, &clonedWidth), NVSDK_NGX_Result_Success);
    EXPECT_EQ(clonedWidth, 3840u);

    cloned->Set(NVSDK_NGX_Parameter_Width, 1920u);
    EXPECT_EQ(cloned->Get(NVSDK_NGX_Parameter_Width, &clonedWidth), NVSDK_NGX_Result_Success);
    EXPECT_EQ(clonedWidth, 1920u);
    EXPECT_EQ(params.Get(NVSDK_NGX_Parameter_Width, &width), NVSDK_NGX_Result_Success);
    EXPECT_EQ(width, 3840u);

    NgxParameterImpl dest;
    params.MirrorTo(&dest);
    EXPECT_EQ(dest.GetParamCount(), params.GetParamCount());
    unsigned int destHeight = 0;
    EXPECT_EQ(dest.Get(NVSDK_NGX_Parameter_Height, &destHeight), NVSDK_NGX_Result_Success);
    EXPECT_EQ(destHeight, 2160u);

    dest.Reset();
    EXPECT_EQ(dest.GetParamCount(), 0u);
    EXPECT_EQ(dest.Get(NVSDK_NGX_Parameter_Height, &destHeight), NVSDK_NGX_Result_FAIL_FeatureNotFound);
}

TEST_F(VirtualNgxHandleTest, VirtualHandleAllocationAndWrapping) {
    auto& interceptor = m_ngx;
    bool attached = interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature);
    ASSERT_TRUE(attached);
    EXPECT_TRUE(interceptor.IsAttached());

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 2560u);
    params.Set(NVSDK_NGX_Parameter_Height, 1440u);
    params.Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality));

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    NVSDK_NGX_Result res = MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);

    EXPECT_EQ(res, NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    EXPECT_TRUE(interceptor.IsVirtualHandle(virtualHandle));
    EXPECT_EQ(interceptor.GetActiveVirtualHandleCount(), 1u);

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);
    EXPECT_EQ(vHandle->magic, VIRTUAL_NGX_HANDLE_MAGIC);
    EXPECT_EQ(vHandle->featureId, NVSDK_NGX_Feature_SuperSampling);
    ASSERT_NE(vHandle->hLeft, nullptr);
    ASSERT_NE(vHandle->hRight, nullptr);
    EXPECT_NE(vHandle->hLeft, vHandle->hRight);
    EXPECT_TRUE(vHandle->IsValid());

    EXPECT_EQ(s_mockCreateCallCount, 2);

    NVSDK_NGX_Result relRes = MockReleaseFeature(virtualHandle);
    EXPECT_EQ(relRes, NVSDK_NGX_Result_Success);
}

TEST_F(VirtualNgxHandleTest, CreationParametersMirroredAcrossBothHandles) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 3840u);
    params.Set(NVSDK_NGX_Parameter_Height, 2160u);
    params.Set(NVSDK_NGX_Parameter_OutWidth, 3840u);
    params.Set(NVSDK_NGX_Parameter_OutHeight, 2160u);
    params.Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced));
    params.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, 0x1u);
    params.Set(NVSDK_NGX_Parameter_Sharpness, 0.65f);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    NVSDK_NGX_Result res = MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);

    EXPECT_EQ(res, NVSDK_NGX_Result_Success);
    ASSERT_EQ(s_capturedParamsPerCreate.size(), 2u);

    const auto& leftParams = s_capturedParamsPerCreate[0];
    unsigned int leftWidth = 0, leftHeight = 0, leftFlags = 0;
    int leftPerf = 0;
    float leftSharpness = 0.0f;
    EXPECT_EQ(leftParams->Get(NVSDK_NGX_Parameter_Width, &leftWidth), NVSDK_NGX_Result_Success);
    EXPECT_EQ(leftParams->Get(NVSDK_NGX_Parameter_Height, &leftHeight), NVSDK_NGX_Result_Success);
    EXPECT_EQ(leftParams->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &leftFlags), NVSDK_NGX_Result_Success);
    EXPECT_EQ(leftParams->Get(NVSDK_NGX_Parameter_PerfQualityValue, &leftPerf), NVSDK_NGX_Result_Success);
    EXPECT_EQ(leftParams->Get(NVSDK_NGX_Parameter_Sharpness, &leftSharpness), NVSDK_NGX_Result_Success);

    EXPECT_EQ(leftWidth, 3840u);
    EXPECT_EQ(leftHeight, 2160u);
    EXPECT_EQ(leftFlags, 0x1u);
    EXPECT_EQ(leftPerf, static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced));
    EXPECT_FLOAT_EQ(leftSharpness, 0.65f);

    const auto& rightParams = s_capturedParamsPerCreate[1];
    unsigned int rightWidth = 0, rightHeight = 0, rightFlags = 0;
    int rightPerf = 0;
    float rightSharpness = 0.0f;
    EXPECT_EQ(rightParams->Get(NVSDK_NGX_Parameter_Width, &rightWidth), NVSDK_NGX_Result_Success);
    EXPECT_EQ(rightParams->Get(NVSDK_NGX_Parameter_Height, &rightHeight), NVSDK_NGX_Result_Success);
    EXPECT_EQ(rightParams->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &rightFlags), NVSDK_NGX_Result_Success);
    EXPECT_EQ(rightParams->Get(NVSDK_NGX_Parameter_PerfQualityValue, &rightPerf), NVSDK_NGX_Result_Success);
    EXPECT_EQ(rightParams->Get(NVSDK_NGX_Parameter_Sharpness, &rightSharpness), NVSDK_NGX_Result_Success);

    EXPECT_EQ(rightWidth, 3840u);
    EXPECT_EQ(rightHeight, 2160u);
    EXPECT_EQ(rightFlags, 0x1u);
    EXPECT_EQ(rightPerf, static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced));
    EXPECT_FLOAT_EQ(rightSharpness, 0.65f);

    MockReleaseFeature(virtualHandle);
}

TEST_F(VirtualNgxHandleTest, VirtualHandleReleaseCleansUpBothHandlesWithoutLeaks) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature);

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);

    VirtualNgxHandle* vHandle = interceptor.GetVirtualHandle(virtualHandle);
    ASSERT_NE(vHandle, nullptr);
    NVSDK_NGX_Handle* expectedLeft = vHandle->hLeft;
    NVSDK_NGX_Handle* expectedRight = vHandle->hRight;

    EXPECT_EQ(interceptor.GetActiveVirtualHandleCount(), 1u);
    EXPECT_EQ(s_mockReleaseCallCount, 0);

    NVSDK_NGX_Result res = MockReleaseFeature(virtualHandle);
    EXPECT_EQ(res, NVSDK_NGX_Result_Success);

    EXPECT_EQ(s_mockReleaseCallCount, 2);
    ASSERT_EQ(s_releasedHandles.size(), 2u);
    EXPECT_EQ(s_releasedHandles[0], expectedLeft);
    EXPECT_EQ(s_releasedHandles[1], expectedRight);

    EXPECT_EQ(interceptor.GetActiveVirtualHandleCount(), 0u);
    EXPECT_FALSE(interceptor.IsVirtualHandle(virtualHandle));
}

TEST_F(VirtualNgxHandleTest, NullAndInvalidHandleHandling) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature);

    NVSDK_NGX_Result res = MockReleaseFeature(nullptr);
    EXPECT_EQ(res, NVSDK_NGX_Result_FAIL_InvalidParameter);

    NgxParameterImpl params;
    res = MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, nullptr);
    EXPECT_EQ(res, NVSDK_NGX_Result_FAIL_InvalidParameter);

    NVSDK_NGX_Handle* handle = nullptr;
    res = MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, nullptr, &handle);
    EXPECT_EQ(res, NVSDK_NGX_Result_FAIL_InvalidParameter);
    EXPECT_EQ(handle, nullptr);

    auto rawHandle = new NVSDK_NGX_Handle();
    rawHandle->Id = 999;
    EXPECT_FALSE(interceptor.IsVirtualHandle(rawHandle));

    res = MockReleaseFeature(rawHandle);
    EXPECT_EQ(res, NVSDK_NGX_Result_Success);
    EXPECT_EQ(s_mockReleaseCallCount, 1);
}

TEST_F(VirtualNgxHandleTest, RightHandleCreationFailureRollsBackLeft) {
    auto& interceptor = m_ngx;
    interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature);

    s_mockFailRightCreation = true;

    NgxParameterImpl params;
    params.Set(NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    NVSDK_NGX_Result res = MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, &params, &virtualHandle);

    EXPECT_TRUE(NVSDK_NGX_FAILED(res));
    EXPECT_EQ(virtualHandle, nullptr);

    EXPECT_EQ(s_mockCreateCallCount, 2);
    EXPECT_EQ(s_mockReleaseCallCount, 1);
    ASSERT_EQ(s_createdHandles.size(), 1u);
    ASSERT_EQ(s_releasedHandles.size(), 1u);
    EXPECT_EQ(s_releasedHandles[0], s_createdHandles[0]);

    EXPECT_EQ(interceptor.GetActiveVirtualHandleCount(), 0u);
}

TEST_F(VirtualNgxHandleTest, ModuleLoadDetectionAndNameMatching) {
    auto& interceptor = m_ngx;

    EXPECT_FALSE(interceptor.AreModuleHooksInstalled());
    bool installed = interceptor.InstallModuleLoadHooks();
    EXPECT_TRUE(installed);
    EXPECT_TRUE(interceptor.AreModuleHooksInstalled());

    EXPECT_TRUE(interceptor.InstallModuleLoadHooks());

    interceptor.UninstallModuleLoadHooks();
    EXPECT_FALSE(interceptor.AreModuleHooksInstalled());
}

TEST_F(VirtualNgxHandleTest, MatchesNgxNameFiltering) {
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName(L"nvngx.dll"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName(L"_nvngx.dll"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName(L"nvngx"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName(L"_nvngx"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName("nvngx.dll"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName("_nvngx.dll"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName("nvngx"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName("_nvngx"));

    EXPECT_TRUE(NgxInterceptor::MatchesNgxName(L"C:\\Windows\\System32\\nvngx.dll"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName(L"C:\\Windows\\System32\\DriverStore\\FileRepository\\nv_dispi.inf\\_nvngx.dll"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName("C:/Windows/System32/nvngx.dll"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName("C:/DriverStore/_nvngx.dll"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName(L"C:\\Games\\dlss_mods\\nvngx.dll"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName(L"D:\\update\\cache\\_nvngx.dll"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName("C:/Users/dlss/nvngx.dll"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName("/opt/update/_nvngx.dll"));

    EXPECT_TRUE(NgxInterceptor::MatchesNgxName(L"NVNGX.DLL"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName(L"_NVNGX.DLL"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName("NVNGX.DLL"));
    EXPECT_TRUE(NgxInterceptor::MatchesNgxName("_NVNGX.DLL"));

    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"nvngx_dlss.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"nvngx_dlss"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"nvngx_dlssg.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"nvngx_dlss_g.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"nvngx_dlssd.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"nvngx_dlss_d.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"nvngx_update.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName("nvngx_dlss.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName("nvngx_dlss"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName("nvngx_dlssg.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName("nvngx_dlss_g.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName("nvngx_dlssd.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName("nvngx_dlss_d.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName("nvngx_update.dll"));

    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"C:\\Games\\FS25\\x64\\nvngx_dlss.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"C:\\Games\\FS25\\x64\\nvngx_dlssg.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName("C:/Games/FS25/x64/nvngx_dlss.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName("C:/Games/FS25/x64/nvngx_dlssg.dll"));

    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"dxgi.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"kernel32.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName("dxgi.dll"));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(static_cast<LPCWSTR>(nullptr)));
    EXPECT_FALSE(NgxInterceptor::MatchesNgxName(static_cast<LPCSTR>(nullptr)));
}

TEST_F(VirtualNgxHandleTest, ParameterDetoursAndNativeMirroringOnFeatureCreation) {
    auto& interceptor = m_ngx;
    bool attached = interceptor.AttachDirect(
        &MockCreateFeature,
        &MockReleaseFeature,
        nullptr,
        &MockAllocateParameters,
        &MockDestroyParameters,
        &MockGetCapabilityParameters
    );
    ASSERT_TRUE(attached);

    NVSDK_NGX_Parameter* params = nullptr;
    NVSDK_NGX_Result allocRes = MockAllocateParameters(&params);
    EXPECT_EQ(allocRes, NVSDK_NGX_Result_Success);
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(s_mockAllocCallCount, 1);

    params->Set(NVSDK_NGX_Parameter_Width, 2560u);
    params->Set(NVSDK_NGX_Parameter_Height, 1440u);
    params->Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality));

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    NVSDK_NGX_Result createRes = MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, params, &virtualHandle);
    EXPECT_EQ(createRes, NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);

    EXPECT_EQ(s_mockCreateCallCount, 2);
    EXPECT_EQ(s_mockAllocCallCount, 2);
    EXPECT_EQ(s_mockDestroyCallCount, 1);

    ASSERT_EQ(s_capturedParamsPerCreate.size(), 2u);
    unsigned int rWidth = 0, rHeight = 0;
    EXPECT_EQ(s_capturedParamsPerCreate[1]->Get(NVSDK_NGX_Parameter_Width, &rWidth), NVSDK_NGX_Result_Success);
    EXPECT_EQ(s_capturedParamsPerCreate[1]->Get(NVSDK_NGX_Parameter_Height, &rHeight), NVSDK_NGX_Result_Success);
    EXPECT_EQ(rWidth, 2560u);
    EXPECT_EQ(rHeight, 1440u);

    MockReleaseFeature(virtualHandle);
    MockDestroyParameters(params);
    EXPECT_EQ(s_mockDestroyCallCount, 2);
}

TEST_F(VirtualNgxHandleTest, ParameterDetoursFallbackWhenOriginalsUnavailable) {
    auto& interceptor = m_ngx;
    bool attached = interceptor.AttachDirect(&MockCreateFeature, &MockReleaseFeature);
    ASSERT_TRUE(attached);

    NVSDK_NGX_Parameter* params = nullptr;
    NVSDK_NGX_Result allocRes = NgxInterceptor::Intercept_AllocateParameters(&params);
    EXPECT_EQ(allocRes, NVSDK_NGX_Result_Success);
    ASSERT_NE(params, nullptr);

    params->Set(NVSDK_NGX_Parameter_Width, 1920u);
    params->Set(NVSDK_NGX_Parameter_Height, 1080u);

    NVSDK_NGX_Handle* virtualHandle = nullptr;
    NVSDK_NGX_Result createRes = MockCreateFeature(nullptr, NVSDK_NGX_Feature_SuperSampling, params, &virtualHandle);
    EXPECT_EQ(createRes, NVSDK_NGX_Result_Success);
    ASSERT_NE(virtualHandle, nullptr);
    EXPECT_EQ(s_mockCreateCallCount, 2);

    NVSDK_NGX_Result destroyRes = NgxInterceptor::Intercept_DestroyParameters(params);
    EXPECT_EQ(destroyRes, NVSDK_NGX_Result_Success);

    MockReleaseFeature(virtualHandle);
}

class CoercingForeignNgxParameter final : public NVSDK_NGX_Parameter {
public:
    void Set(const char*, unsigned long long) override {}
    void Set(const char*, float) override {}
    void Set(const char*, double) override {}
    void Set(const char*, unsigned int) override {}
    void Set(const char*, int) override {}
    void Set(const char*, ID3D11Resource*) override {}
    void Set(const char*, ID3D12Resource*) override {}
    void Set(const char*, void*) override {}
    void Reset() override {}

    void PutUll(const char* name, unsigned long long val) { m_ull[name] = val; }
    void PutFloat(const char* name, float val) { m_float[name] = val; }
    void PutUint(const char* name, unsigned int val) { m_uint[name] = val; }
    void PutInt(const char* name, int val) { m_int[name] = val; }
    void PutD3D12(const char* name, ID3D12Resource* val) { m_d3d12[name] = val; }

    NVSDK_NGX_Result Get(const char* InName, unsigned long long* OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        if (auto it = m_ull.find(InName); it != m_ull.end()) {
            *OutValue = it->second;
            return NVSDK_NGX_Result_Success;
        }
        if (auto it = m_uint.find(InName); it != m_uint.end()) {
            *OutValue = it->second;
            return NVSDK_NGX_Result_Success;
        }
        if (auto it = m_int.find(InName); it != m_int.end()) {
            *OutValue = static_cast<unsigned long long>(it->second);
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    NVSDK_NGX_Result Get(const char* InName, float* OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        if (auto it = m_float.find(InName); it != m_float.end()) {
            *OutValue = it->second;
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    NVSDK_NGX_Result Get(const char*, double*) const override {
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    NVSDK_NGX_Result Get(const char* InName, unsigned int* OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        if (auto it = m_uint.find(InName); it != m_uint.end()) {
            *OutValue = it->second;
            return NVSDK_NGX_Result_Success;
        }
        if (auto it = m_int.find(InName); it != m_int.end()) {
            *OutValue = static_cast<unsigned int>(it->second);
            return NVSDK_NGX_Result_Success;
        }
        if (auto it = m_ull.find(InName); it != m_ull.end()) {
            *OutValue = static_cast<unsigned int>(it->second);
            return NVSDK_NGX_Result_Success;
        }
        if (auto it = m_float.find(InName); it != m_float.end()) {
            *OutValue = static_cast<unsigned int>(it->second);
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    NVSDK_NGX_Result Get(const char* InName, int* OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        if (auto it = m_int.find(InName); it != m_int.end()) {
            *OutValue = it->second;
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    NVSDK_NGX_Result Get(const char*, ID3D11Resource**) const override {
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    NVSDK_NGX_Result Get(const char* InName, ID3D12Resource** OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        if (auto it = m_d3d12.find(InName); it != m_d3d12.end()) {
            *OutValue = it->second;
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    NVSDK_NGX_Result Get(const char*, void**) const override {
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

private:
    std::unordered_map<std::string, unsigned long long> m_ull;
    std::unordered_map<std::string, float> m_float;
    std::unordered_map<std::string, unsigned int> m_uint;
    std::unordered_map<std::string, int> m_int;
    std::unordered_map<std::string, ID3D12Resource*> m_d3d12;
};

class StrictUintForeignNgxParameter final : public NVSDK_NGX_Parameter {
public:
    void Set(const char*, unsigned long long) override {}
    void Set(const char*, float) override {}
    void Set(const char*, double) override {}
    void Set(const char*, unsigned int) override {}
    void Set(const char*, int) override {}
    void Set(const char*, ID3D11Resource*) override {}
    void Set(const char*, ID3D12Resource*) override {}
    void Set(const char*, void*) override {}
    void Reset() override {}

    void PutUint(const char* name, unsigned int val) { m_uint[name] = val; }

    NVSDK_NGX_Result Get(const char*, unsigned long long*) const override {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    NVSDK_NGX_Result Get(const char*, float*) const override { return NVSDK_NGX_Result_FAIL_FeatureNotFound; }
    NVSDK_NGX_Result Get(const char*, double*) const override { return NVSDK_NGX_Result_FAIL_FeatureNotFound; }
    NVSDK_NGX_Result Get(const char* InName, unsigned int* OutValue) const override {
        if (!InName || !OutValue) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        if (auto it = m_uint.find(InName); it != m_uint.end()) {
            *OutValue = it->second;
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }
    NVSDK_NGX_Result Get(const char*, int*) const override { return NVSDK_NGX_Result_FAIL_FeatureNotFound; }
    NVSDK_NGX_Result Get(const char*, ID3D11Resource**) const override { return NVSDK_NGX_Result_FAIL_FeatureNotFound; }
    NVSDK_NGX_Result Get(const char*, ID3D12Resource**) const override { return NVSDK_NGX_Result_FAIL_FeatureNotFound; }
    NVSDK_NGX_Result Get(const char*, void**) const override { return NVSDK_NGX_Result_FAIL_FeatureNotFound; }

private:
    std::unordered_map<std::string, unsigned int> m_uint;
};

TEST_F(VirtualNgxHandleTest, CopyFromForeignParameterPreservesFloats64BitIntsAndSubrectsWithoutUintCoercion) {
    CoercingForeignNgxParameter foreign;
    auto* fakeColor = reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0xCAFE1234));
    foreign.PutUint(NVSDK_NGX_Parameter_Width, 1920u);
    foreign.PutUint(NVSDK_NGX_Parameter_Height, 1080u);
    foreign.PutFloat(NVSDK_NGX_Parameter_Sharpness, 0.75f);
    foreign.PutFloat(NVSDK_NGX_Parameter_Jitter_Offset_X, -0.25f);
    foreign.PutFloat(NVSDK_NGX_Parameter_MV_Scale_X, 1.5f);
    foreign.PutFloat(NVSDK_NGX_Parameter_PreExposure, 2.5f);
    foreign.PutUll(NVSDK_NGX_Parameter_CreationNodeMask, 0x100000042ULL);
    foreign.PutInt(NVSDK_NGX_Parameter_Reset, 1);
    foreign.PutInt(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, 11);
    foreign.PutUint(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, 64u);
    foreign.PutUint(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, 32u);
    foreign.PutUint(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, 128u);
    foreign.PutUint(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, 96u);
    foreign.PutD3D12(NVSDK_NGX_Parameter_Color, fakeColor);

    NgxParameterImpl copied;
    copied.CopyFrom(&foreign);

    float sharpness = 0.0f;
    float jitterX = 0.0f;
    float mvScaleX = 0.0f;
    float preExposure = 0.0f;
    EXPECT_EQ(copied.Get(NVSDK_NGX_Parameter_Sharpness, &sharpness), NVSDK_NGX_Result_Success);
    EXPECT_EQ(copied.Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &jitterX), NVSDK_NGX_Result_Success);
    EXPECT_EQ(copied.Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvScaleX), NVSDK_NGX_Result_Success);
    EXPECT_EQ(copied.Get(NVSDK_NGX_Parameter_PreExposure, &preExposure), NVSDK_NGX_Result_Success);
    EXPECT_FLOAT_EQ(sharpness, 0.75f);
    EXPECT_FLOAT_EQ(jitterX, -0.25f);
    EXPECT_FLOAT_EQ(mvScaleX, 1.5f);
    EXPECT_FLOAT_EQ(preExposure, 2.5f);

    unsigned long long nodeMask = 0;
    EXPECT_EQ(copied.Get(NVSDK_NGX_Parameter_CreationNodeMask, &nodeMask), NVSDK_NGX_Result_Success);
    EXPECT_EQ(nodeMask, 0x100000042ULL);

    unsigned int depthBaseX = 0, depthBaseY = 0, outBaseX = 0, outBaseY = 0;
    EXPECT_EQ(copied.Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, &depthBaseX), NVSDK_NGX_Result_Success);
    EXPECT_EQ(copied.Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, &depthBaseY), NVSDK_NGX_Result_Success);
    EXPECT_EQ(copied.Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, &outBaseX), NVSDK_NGX_Result_Success);
    EXPECT_EQ(copied.Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, &outBaseY), NVSDK_NGX_Result_Success);
    EXPECT_EQ(depthBaseX, 64u);
    EXPECT_EQ(depthBaseY, 32u);
    EXPECT_EQ(outBaseX, 128u);
    EXPECT_EQ(outBaseY, 96u);

    ID3D12Resource* color = nullptr;
    EXPECT_EQ(copied.Get(NVSDK_NGX_Parameter_Color, &color), NVSDK_NGX_Result_Success);
    EXPECT_EQ(color, fakeColor);

    StrictUintForeignNgxParameter strictUint;
    strictUint.PutUint(NVSDK_NGX_Parameter_Width, 2560u);
    strictUint.PutUint(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, 48u);
    NgxParameterImpl copiedStrict;
    copiedStrict.CopyFrom(&strictUint);
    unsigned int strictWidth = 0, strictSubrectX = 0;
    EXPECT_EQ(copiedStrict.Get(NVSDK_NGX_Parameter_Width, &strictWidth), NVSDK_NGX_Result_Success);
    EXPECT_EQ(copiedStrict.Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, &strictSubrectX),
              NVSDK_NGX_Result_Success);
    EXPECT_EQ(strictWidth, 2560u);
    EXPECT_EQ(strictSubrectX, 48u);
}

TEST_F(VirtualNgxHandleTest, CopyFromTrackedInstancePreservesCustomKeys) {
    NgxParameterImpl source;
    source.Set("CustomULL", 9876543210ULL);
    source.Set("CustomFloat", 0.375f);
    source.Set(NVSDK_NGX_Parameter_Width, 3840u);

    NgxParameterImpl clone;
    clone.CopyFrom(&source);

    unsigned long long ullVal = 0;
    float fVal = 0.0f;
    unsigned int width = 0;
    EXPECT_EQ(clone.Get("CustomULL", &ullVal), NVSDK_NGX_Result_Success);
    EXPECT_EQ(clone.Get("CustomFloat", &fVal), NVSDK_NGX_Result_Success);
    EXPECT_EQ(clone.Get(NVSDK_NGX_Parameter_Width, &width), NVSDK_NGX_Result_Success);
    EXPECT_EQ(ullVal, 9876543210ULL);
    EXPECT_FLOAT_EQ(fVal, 0.375f);
    EXPECT_EQ(width, 3840u);
}

}
}
