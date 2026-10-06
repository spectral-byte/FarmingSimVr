#include <gtest/gtest.h>
#include "fidelityfx/ffx_defs.h"
#include "fidelityfx/fidelityfx_interceptor.h"
#include "luau/vr_bridge.h"
#include "ngx/ngx_interceptor.h"
#include <memory>
#include <vector>

namespace fs25vr {
namespace {

// A fake FidelityFX loader: it records every call it receives so tests observe what reaches the real API.
constexpr ffxStructType_t kFrameGenerationCreateType = 0x00020000u;
constexpr ffxStructType_t kFrameGenerationDispatchType = 0x00020001u;
constexpr ffxStructType_t kBackendCreateType = 0x00000002u;

struct FakeContext {
    uint32_t id;
};

int s_nextContextId = 0;
std::vector<ffxContext> s_createdContexts;
std::vector<ffxCreateContextDescHeader*> s_createDescs;
std::vector<uint32_t> s_createFlags;
std::vector<ffxContext> s_destroyed;
std::vector<ffxContext> s_configured;
std::vector<ffxContext> s_queried;
std::vector<ffxContext> s_dispatched;
std::vector<bool> s_dispatchReset;
std::vector<const ffxDispatchDescHeader*> s_dispatchDescs;
int s_failCreateNumber = 0; // 1-based create call that fails, 0 for none
int s_createCalls = 0;

__declspec(noinline) ffxReturnCode_t MockCreate(ffxContext* context, ffxCreateContextDescHeader* desc,
                                                const ffxAllocationCallbacks*) {
    ++s_createCalls;
    if (s_failCreateNumber != 0 && s_createCalls == s_failCreateNumber) return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    s_createDescs.push_back(desc);
    for (ffxApiHeader* h = desc; h; h = h->pNext) {
        if (h->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE) {
            s_createFlags.push_back(reinterpret_cast<ffxCreateContextDescUpscale*>(h)->flags);
        }
    }
    *context = new FakeContext{static_cast<uint32_t>(++s_nextContextId)};
    s_createdContexts.push_back(*context);
    return FFX_API_RETURN_OK;
}

__declspec(noinline) ffxReturnCode_t MockDestroy(ffxContext* context, const ffxAllocationCallbacks*) {
    s_destroyed.push_back(*context);
    delete static_cast<FakeContext*>(*context);
    *context = nullptr;
    return FFX_API_RETURN_OK;
}

__declspec(noinline) ffxReturnCode_t MockConfigure(ffxContext* context, const ffxConfigureDescHeader*) {
    s_configured.push_back(context ? *context : nullptr);
    return FFX_API_RETURN_OK;
}

__declspec(noinline) ffxReturnCode_t MockQuery(ffxContext* context, ffxQueryDescHeader*) {
    s_queried.push_back(context ? *context : nullptr);
    return FFX_API_RETURN_OK;
}

__declspec(noinline) ffxReturnCode_t MockDispatch(ffxContext* context, const ffxDispatchDescHeader* desc) {
    s_dispatched.push_back(*context);
    s_dispatchDescs.push_back(desc);
    bool reset = false;
    if (desc->type == FFX_API_DISPATCH_DESC_TYPE_UPSCALE) {
        reset = reinterpret_cast<const ffxDispatchDescUpscale*>(desc)->reset;
    }
    s_dispatchReset.push_back(reset);
    return FFX_API_RETURN_OK;
}

ffxCreateContextDescUpscale MakeUpscaleCreateDesc(uint32_t flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE |
                                                                   FFX_UPSCALE_ENABLE_DEPTH_INVERTED) {
    ffxCreateContextDescUpscale desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    desc.flags = flags;
    desc.maxRenderSize = {1280, 1440};
    desc.maxUpscaleSize = {2560, 2880};
    return desc;
}

ffxDispatchDescUpscale MakeUpscaleDispatchDesc(float jitterX = 0.0f, float jitterY = 0.0f) {
    ffxDispatchDescUpscale desc{};
    desc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    desc.jitterOffset = {jitterX, jitterY};
    desc.motionVectorScale = {1.0f, 1.0f};
    desc.renderSize = {1280, 1440};
    return desc;
}

class FidelityFxInterceptorTest : public ::testing::Test {
protected:
    std::shared_ptr<NgxInterceptor> m_ngxShared = std::make_shared<NgxInterceptor>();
    std::shared_ptr<VRBridge> m_bridgeShared = std::make_shared<VRBridge>(
        std::make_shared<OpenXrManager>(std::make_shared<NativeOpenXrDispatch>()), m_ngxShared);
    VRBridge& m_bridge = *m_bridgeShared;
    FidelityFxInterceptor m_ffx;

    void SetUp() override {
        s_nextContextId = 0;
        s_createdContexts.clear();
        s_createDescs.clear();
        s_createFlags.clear();
        s_destroyed.clear();
        s_configured.clear();
        s_queried.clear();
        s_dispatched.clear();
        s_dispatchReset.clear();
        s_dispatchDescs.clear();
        s_failCreateNumber = 0;
        s_createCalls = 0;
        m_ffx.DetachAll();
        ResetBridge();
    }

    void TearDown() override {
        m_ffx.DetachAll();
        ResetBridge();
    }

    void ResetBridge() {
        m_bridge.SetRenderingMode(VRRenderingMode::AFR);
        m_bridge.SetSubPassIndex(0);
        m_bridge.SetFrameParity(0);
        m_bridge.Set2DViewActive(false);
        m_bridge.SetMenuVisible(false);
    }

    void AttachAll() {
        ASSERT_TRUE(m_ffx.AttachDirect(&MockCreate, &MockDestroy, &MockConfigure, &MockQuery, &MockDispatch));
        m_ffx.SetVRBridge(&m_bridge);
    }

    ffxContext CreateUpscaler() {
        ffxContext context = nullptr;
        auto desc = MakeUpscaleCreateDesc();
        EXPECT_EQ(MockCreate(&context, &desc.header, nullptr), FFX_API_RETURN_OK);
        return context;
    }

    ffxReturnCode_t Dispatch(ffxContext context, const ffxDispatchDescUpscale& desc) {
        return MockDispatch(&context, &desc.header);
    }
};

TEST_F(FidelityFxInterceptorTest, CreatingAnUpscalerCreatesTwoContextsWithTheSameDescriptionBehindOneVirtualHandle) {
    AttachAll();
    ffxContext context = nullptr;
    auto desc = MakeUpscaleCreateDesc(FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE);
    ASSERT_EQ(MockCreate(&context, &desc.header, nullptr), FFX_API_RETURN_OK);

    ASSERT_EQ(s_createdContexts.size(), 2u);
    ASSERT_EQ(s_createFlags.size(), 2u);
    EXPECT_EQ(s_createFlags[0], s_createFlags[1]);
    EXPECT_EQ(s_createFlags[0], FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE);
    EXPECT_EQ(s_createDescs[0], s_createDescs[1]);

    ASSERT_NE(context, nullptr);
    EXPECT_TRUE(m_ffx.IsVirtualContext(context));
    EXPECT_EQ(m_ffx.GetActiveVirtualContextCount(), 1u);
    VirtualFfxContext* virtualContext = m_ffx.GetVirtualContext(context);
    ASSERT_NE(virtualContext, nullptr);
    EXPECT_EQ(virtualContext->left, s_createdContexts[0]);
    EXPECT_EQ(virtualContext->right, s_createdContexts[1]);
    EXPECT_NE(context, s_createdContexts[0]);
    EXPECT_NE(context, s_createdContexts[1]);
}

TEST_F(FidelityFxInterceptorTest, FailingToCreateTheSecondContextRollsBackTheFirst) {
    AttachAll();
    s_failCreateNumber = 2;
    ffxContext context = nullptr;
    auto desc = MakeUpscaleCreateDesc();
    EXPECT_EQ(MockCreate(&context, &desc.header, nullptr), FFX_API_RETURN_ERROR_RUNTIME_ERROR);

    EXPECT_EQ(m_ffx.GetActiveVirtualContextCount(), 0u);
    ASSERT_EQ(s_destroyed.size(), 1u);
    EXPECT_EQ(s_destroyed[0], s_createdContexts[0]);
    EXPECT_EQ(context, nullptr);
}

TEST_F(FidelityFxInterceptorTest, FailingToCreateTheFirstContextReturnsTheErrorWithoutAVirtualContext) {
    AttachAll();
    s_failCreateNumber = 1;
    ffxContext context = nullptr;
    auto desc = MakeUpscaleCreateDesc();
    EXPECT_EQ(MockCreate(&context, &desc.header, nullptr), FFX_API_RETURN_ERROR_RUNTIME_ERROR);
    EXPECT_EQ(m_ffx.GetActiveVirtualContextCount(), 0u);
    EXPECT_TRUE(s_destroyed.empty());
}

TEST_F(FidelityFxInterceptorTest, UnknownContextTypesPassThroughUntouched) {
    AttachAll();
    ffxApiHeader frameGeneration{kFrameGenerationCreateType, nullptr};
    ffxContext context = nullptr;
    ASSERT_EQ(MockCreate(&context, &frameGeneration, nullptr), FFX_API_RETURN_OK);

    ASSERT_EQ(s_createdContexts.size(), 1u);
    EXPECT_EQ(context, s_createdContexts[0]);
    EXPECT_FALSE(m_ffx.IsVirtualContext(context));
    EXPECT_EQ(m_ffx.GetActiveVirtualContextCount(), 0u);

    // Its dispatch, configure, query and destroy reach the loader with the context the game holds.
    ffxApiHeader dispatchDesc{kFrameGenerationDispatchType, nullptr};
    EXPECT_EQ(MockDispatch(&context, &dispatchDesc), FFX_API_RETURN_OK);
    ASSERT_EQ(s_dispatched.size(), 1u);
    EXPECT_EQ(s_dispatched[0], s_createdContexts[0]);
    EXPECT_EQ(s_dispatchDescs[0], &dispatchDesc);

    EXPECT_EQ(MockConfigure(&context, &dispatchDesc), FFX_API_RETURN_OK);
    EXPECT_EQ(s_configured, std::vector<ffxContext>{s_createdContexts[0]});
    EXPECT_EQ(MockQuery(&context, &dispatchDesc), FFX_API_RETURN_OK);
    EXPECT_EQ(s_queried, std::vector<ffxContext>{s_createdContexts[0]});

    ffxContext created = context;
    EXPECT_EQ(MockDestroy(&context, nullptr), FFX_API_RETURN_OK);
    EXPECT_EQ(s_destroyed, std::vector<ffxContext>{created});
}

TEST_F(FidelityFxInterceptorTest, AnUpscaleDescriptorFoundLaterInTheChainStillVirtualizes) {
    AttachAll();
    auto upscale = MakeUpscaleCreateDesc();
    ffxApiHeader backend{kBackendCreateType, &upscale.header};
    ffxContext context = nullptr;
    ASSERT_EQ(MockCreate(&context, &backend, nullptr), FFX_API_RETURN_OK);
    EXPECT_TRUE(m_ffx.IsVirtualContext(context));
    EXPECT_EQ(s_createdContexts.size(), 2u);
}

TEST_F(FidelityFxInterceptorTest, DispatchesRouteToTheEyeContextBySubPassUnderSequentialDualPass) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    auto desc = MakeUpscaleDispatchDesc();

    m_bridge.SetSubPassIndex(0);
    EXPECT_EQ(Dispatch(context, desc), FFX_API_RETURN_OK);
    m_bridge.SetSubPassIndex(1);
    EXPECT_EQ(Dispatch(context, desc), FFX_API_RETURN_OK);
    m_bridge.SetSubPassIndex(0);
    EXPECT_EQ(Dispatch(context, desc), FFX_API_RETURN_OK);

    ASSERT_EQ(s_dispatched.size(), 3u);
    EXPECT_EQ(s_dispatched[0], s_createdContexts[0]);
    EXPECT_EQ(s_dispatched[1], s_createdContexts[1]);
    EXPECT_EQ(s_dispatched[2], s_createdContexts[0]);
}

TEST_F(FidelityFxInterceptorTest, DispatchesFollowTheSwappedEyeOrder) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    for (uint64_t i = 0; i < VRBridge::kEyeOrderSwapPairs; ++i) m_bridge.AdvanceFrame();
    auto desc = MakeUpscaleDispatchDesc();

    m_bridge.SetSubPassIndex(0);
    Dispatch(context, desc);
    m_bridge.SetSubPassIndex(1);
    Dispatch(context, desc);

    ASSERT_EQ(s_dispatched.size(), 2u);
    EXPECT_EQ(s_dispatched[0], s_createdContexts[1]);
    EXPECT_EQ(s_dispatched[1], s_createdContexts[0]);
}

TEST_F(FidelityFxInterceptorTest, DispatchesRouteByFrameParityUnderAlternateFrameRendering) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    auto desc = MakeUpscaleDispatchDesc();

    m_bridge.SetFrameParity(0);
    Dispatch(context, desc);
    m_bridge.SetFrameParity(1);
    Dispatch(context, desc);

    ASSERT_EQ(s_dispatched.size(), 2u);
    EXPECT_EQ(s_dispatched[0], s_createdContexts[0]);
    EXPECT_EQ(s_dispatched[1], s_createdContexts[1]);
}

TEST_F(FidelityFxInterceptorTest, MenusAndTwoDimensionalModeUseOneContext) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    auto desc = MakeUpscaleDispatchDesc();

    m_bridge.SetMenuVisible(true);
    m_bridge.SetSubPassIndex(1);
    Dispatch(context, desc);
    m_bridge.SetMenuVisible(false);
    m_bridge.Set2DViewActive(true);
    Dispatch(context, desc);

    ASSERT_EQ(s_dispatched.size(), 2u);
    EXPECT_EQ(s_dispatched[0], s_createdContexts[0]);
    EXPECT_EQ(s_dispatched[1], s_createdContexts[0]);
}

TEST_F(FidelityFxInterceptorTest, WithoutABridgeDispatchesGoToTheLeftContext) {
    ASSERT_TRUE(m_ffx.AttachDirect(&MockCreate, &MockDestroy, &MockConfigure, &MockQuery, &MockDispatch));
    ffxContext context = CreateUpscaler();
    auto desc = MakeUpscaleDispatchDesc();
    EXPECT_EQ(Dispatch(context, desc), FFX_API_RETURN_OK);
    ASSERT_EQ(s_dispatched.size(), 1u);
    EXPECT_EQ(s_dispatched[0], s_createdContexts[0]);
}

TEST_F(FidelityFxInterceptorTest, ADiscontinuityFlushSetsTheResetFlagForBothContextsOnceAndRestoresTheDescriptor) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    auto desc = MakeUpscaleDispatchDesc();
    // First dispatches latch the current Stereo Presentation Mode and drain any earlier flush.
    m_bridge.SetSubPassIndex(0);
    Dispatch(context, desc);
    m_bridge.SetSubPassIndex(1);
    Dispatch(context, desc);
    s_dispatchReset.clear();
    s_dispatched.clear();

    m_ffx.TriggerDiscontinuityFlush();

    m_bridge.SetSubPassIndex(0);
    Dispatch(context, desc);
    m_bridge.SetSubPassIndex(1);
    Dispatch(context, desc);
    m_bridge.SetSubPassIndex(0);
    Dispatch(context, desc);
    m_bridge.SetSubPassIndex(1);
    Dispatch(context, desc);

    ASSERT_EQ(s_dispatchReset.size(), 4u);
    EXPECT_TRUE(s_dispatchReset[0]);
    EXPECT_TRUE(s_dispatchReset[1]);
    EXPECT_FALSE(s_dispatchReset[2]);
    EXPECT_FALSE(s_dispatchReset[3]);
    EXPECT_FALSE(desc.reset) << "the game's descriptor must not keep our reset";
}

TEST_F(FidelityFxInterceptorTest, AnEngineRequestedResetSurvivesTheFlushRestore) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    auto desc = MakeUpscaleDispatchDesc();
    desc.reset = true;
    Dispatch(context, desc);
    ASSERT_EQ(s_dispatchReset.size(), 1u);
    EXPECT_TRUE(s_dispatchReset[0]);
    EXPECT_TRUE(desc.reset);
}

TEST_F(FidelityFxInterceptorTest, SwitchingTheStereoPresentationModeResetsBothContexts) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    auto desc = MakeUpscaleDispatchDesc();

    m_bridge.SetRenderingMode(VRRenderingMode::AFR);
    m_bridge.SetFrameParity(0);
    Dispatch(context, desc);
    m_bridge.SetFrameParity(1);
    Dispatch(context, desc);
    s_dispatchReset.clear();

    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    m_bridge.SetSubPassIndex(0);
    Dispatch(context, desc);
    m_bridge.SetSubPassIndex(1);
    Dispatch(context, desc);
    m_bridge.SetSubPassIndex(0);
    Dispatch(context, desc);

    ASSERT_EQ(s_dispatchReset.size(), 3u);
    EXPECT_TRUE(s_dispatchReset[0]);
    EXPECT_TRUE(s_dispatchReset[1]);
    EXPECT_FALSE(s_dispatchReset[2]);
}

TEST_F(FidelityFxInterceptorTest, DestroyingAVirtualContextDestroysBothAndClearsTheHandle) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    ASSERT_EQ(m_ffx.GetActiveVirtualContextCount(), 1u);

    EXPECT_EQ(MockDestroy(&context, nullptr), FFX_API_RETURN_OK);

    EXPECT_EQ(context, nullptr);
    EXPECT_EQ(m_ffx.GetActiveVirtualContextCount(), 0u);
    ASSERT_EQ(s_destroyed.size(), 2u);
    EXPECT_NE(s_destroyed[0], s_destroyed[1]);
}

TEST_F(FidelityFxInterceptorTest, ConfigureReachesBothContextsAndQueryTheLeftOne) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    ffxApiHeader desc{0x00010006u, nullptr};

    EXPECT_EQ(MockConfigure(&context, &desc), FFX_API_RETURN_OK);
    ASSERT_EQ(s_configured.size(), 2u);
    EXPECT_EQ(s_configured[0], s_createdContexts[0]);
    EXPECT_EQ(s_configured[1], s_createdContexts[1]);

    EXPECT_EQ(MockQuery(&context, &desc), FFX_API_RETURN_OK);
    ASSERT_EQ(s_queried.size(), 1u);
    EXPECT_EQ(s_queried[0], s_createdContexts[0]);
}

TEST_F(FidelityFxInterceptorTest, ConfigureAndQueryWithoutAContextPassThrough) {
    AttachAll();
    ffxApiHeader desc{0x00010002u, nullptr};
    EXPECT_EQ(MockQuery(nullptr, &desc), FFX_API_RETURN_OK);
    EXPECT_EQ(MockConfigure(nullptr, &desc), FFX_API_RETURN_OK);
    EXPECT_EQ(s_queried, std::vector<ffxContext>{nullptr});
    EXPECT_EQ(s_configured, std::vector<ffxContext>{nullptr});
}

TEST_F(FidelityFxInterceptorTest, DiagnosticsShowStrictAlternationAndPerEyeJitter) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    m_ffx.ArmDiagnostics(8);

    for (int frame = 0; frame < 4; ++frame) {
        auto left = MakeUpscaleDispatchDesc(0.25f, -0.5f);
        auto right = MakeUpscaleDispatchDesc(-0.25f, 0.5f);
        m_bridge.SetSubPassIndex(0);
        Dispatch(context, left);
        m_bridge.SetSubPassIndex(1);
        Dispatch(context, right);
    }

    EXPECT_EQ(m_ffx.GetDiagDispatchesRemaining(), 0u);
    EXPECT_EQ(m_ffx.GetDiagLongestContextRun(), 1u);
    auto leftSummary = m_ffx.GetDiagJitterSummary(0);
    auto rightSummary = m_ffx.GetDiagJitterSummary(1);
    EXPECT_EQ(leftSummary.dispatches, 4u);
    EXPECT_EQ(rightSummary.dispatches, 4u);
    EXPECT_EQ(leftSummary.distinctOffsets, 1u);
    EXPECT_FLOAT_EQ(leftSummary.minX, 0.25f);
    EXPECT_FLOAT_EQ(rightSummary.maxY, 0.5f);
}

TEST_F(FidelityFxInterceptorTest, DiagnosticsSeeTheEyeOrderSwapAsADoubleOnOneContext) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    auto desc = MakeUpscaleDispatchDesc();

    // The last pair before the swap renders left, right; the first pair after renders right, left.
    for (uint64_t i = 0; i + 1 < VRBridge::kEyeOrderSwapPairs; ++i) m_bridge.AdvanceFrame();
    m_ffx.ArmDiagnostics(4);
    m_bridge.SetSubPassIndex(0);
    Dispatch(context, desc);
    m_bridge.SetSubPassIndex(1);
    Dispatch(context, desc);
    m_bridge.AdvanceFrame();
    m_bridge.SetSubPassIndex(0);
    Dispatch(context, desc);
    m_bridge.SetSubPassIndex(1);
    Dispatch(context, desc);

    EXPECT_EQ(m_ffx.GetDiagLongestContextRun(), 2u);
}

TEST_F(FidelityFxInterceptorTest, VirtualizingAContextArmsDiagnostics) {
    AttachAll();
    EXPECT_EQ(m_ffx.GetDiagDispatchesRemaining(), 0u);
    CreateUpscaler();
    EXPECT_EQ(m_ffx.GetDiagDispatchesRemaining(), FidelityFxInterceptor::kDefaultDiagDispatches);
}

TEST_F(FidelityFxInterceptorTest, DispatchesOfOtherTypesOnAVirtualContextGoToTheLeftContextUntouched) {
    AttachAll();
    ffxContext context = CreateUpscaler();
    m_bridge.SetFrameParity(1);
    ffxApiHeader other{kFrameGenerationDispatchType, nullptr};
    EXPECT_EQ(MockDispatch(&context, &other), FFX_API_RETURN_OK);
    ASSERT_EQ(s_dispatched.size(), 1u);
    EXPECT_EQ(s_dispatched[0], s_createdContexts[0]);
    EXPECT_EQ(s_dispatchDescs[0], &other);
}

TEST_F(FidelityFxInterceptorTest, MatchesTheFidelityFxLoaderByFileName) {
    EXPECT_TRUE(FidelityFxInterceptor::MatchesLoaderName(L"amd_fidelityfx_loader_dx12.dll"));
    EXPECT_TRUE(FidelityFxInterceptor::MatchesLoaderName(L"C:\\Games\\FS25\\x64\\AMD_FidelityFX_Loader_DX12.DLL"));
    EXPECT_TRUE(FidelityFxInterceptor::MatchesLoaderName("x64/amd_fidelityfx_loader_dx12.dll"));
    EXPECT_FALSE(FidelityFxInterceptor::MatchesLoaderName(L"amd_fidelityfx_upscaler_dx12.dll"));
    EXPECT_FALSE(FidelityFxInterceptor::MatchesLoaderName(L"amd_fidelityfx_dx12.dll"));
    EXPECT_FALSE(FidelityFxInterceptor::MatchesLoaderName("nvngx.dll"));
    EXPECT_FALSE(FidelityFxInterceptor::MatchesLoaderName(static_cast<LPCWSTR>(nullptr)));
}

TEST_F(FidelityFxInterceptorTest, AttachDirectRejectsMissingEntryPoints) {
    EXPECT_FALSE(m_ffx.AttachDirect(nullptr, &MockDestroy));
    EXPECT_FALSE(m_ffx.IsAttached());
}

} // namespace
} // namespace fs25vr
