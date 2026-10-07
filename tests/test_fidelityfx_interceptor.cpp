#include <gtest/gtest.h>
#include "fidelityfx/ffx_defs.h"
#include "fidelityfx/ffx_mv_conventions.h"
#include "fidelityfx/fidelityfx_interceptor.h"
#include "luau/vr_bridge.h"
#include "ngx/mv_pass.h"
#include "ngx/ngx_interceptor.h"
#include "core/logger.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
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
std::vector<void*> s_dispatchMotionVectors;       // the motion vector resource each upscale dispatch received
std::vector<uint32_t> s_dispatchMotionVectorStates;
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
        const auto* upscale = reinterpret_cast<const ffxDispatchDescUpscale*>(desc);
        reset = upscale->reset;
        s_dispatchMotionVectors.push_back(upscale->motionVectors.resource);
        s_dispatchMotionVectorStates.push_back(upscale->motionVectors.state);
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

// ---- Reading the dispatch's conventions (#38) ----

constexpr uint32_t kSupportedCreateFlags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DEPTH_INVERTED;

TEST(FfxMvConventionsTest, ReadsTheMotionVectorScaleAndJitterFromTheDispatchDescription) {
    auto desc = MakeUpscaleDispatchDesc(0.25f, -0.125f);
    desc.motionVectorScale = {-2.0f, 3.0f};
    FfxMvConventions c = ReadFfxMvConventions(kSupportedCreateFlags, desc);
    EXPECT_EQ(c.unsupportedReason, nullptr);
    EXPECT_FLOAT_EQ(c.mvScaleX, -2.0f);
    EXPECT_FLOAT_EQ(c.mvScaleY, 3.0f);
    EXPECT_FLOAT_EQ(c.jitterX, 0.25f);
    EXPECT_FLOAT_EQ(c.jitterY, -0.125f);
    EXPECT_TRUE(c.depthInverted);
}

TEST(FfxMvConventionsTest, ReadsTheProjectionFromTheDispatchCameraFields) {
    auto desc = MakeUpscaleDispatchDesc();
    desc.cameraNear = 0.1f;
    desc.cameraFar = 1000.0f;
    desc.cameraFovAngleVertical = 1.0f;
    FfxMvConventions c = ReadFfxMvConventions(kSupportedCreateFlags, desc);
    ASSERT_TRUE(c.hasProjection);
    EXPECT_NEAR(c.tanHalfFovY, 0.5463025f, 1e-6f); // tan(0.5 rad)
    EXPECT_FLOAT_EQ(c.nearClip, 0.1f);
    EXPECT_FLOAT_EQ(c.farClip, 1000.0f);
}

TEST(FfxMvConventionsTest, ViewSpaceToMetersFactorScalesNearAndFar) {
    auto desc = MakeUpscaleDispatchDesc();
    desc.cameraNear = 0.1f;
    desc.cameraFar = 1000.0f;
    desc.cameraFovAngleVertical = 1.0f;
    desc.viewSpaceToMetersFactor = 2.0f;
    FfxMvConventions c = ReadFfxMvConventions(kSupportedCreateFlags, desc);
    ASSERT_TRUE(c.hasProjection);
    EXPECT_FLOAT_EQ(c.nearClip, 0.2f);
    EXPECT_FLOAT_EQ(c.farClip, 2000.0f);
}

TEST(FfxMvConventionsTest, NearAndFarGivenSwappedForReverseDepthAreNormalised) {
    auto desc = MakeUpscaleDispatchDesc();
    desc.cameraNear = 1000.0f;
    desc.cameraFar = 0.1f;
    desc.cameraFovAngleVertical = 1.0f;
    FfxMvConventions c = ReadFfxMvConventions(kSupportedCreateFlags, desc);
    ASSERT_TRUE(c.hasProjection);
    EXPECT_FLOAT_EQ(c.nearClip, 0.1f);
    EXPECT_FLOAT_EQ(c.farClip, 1000.0f);
}

TEST(FfxMvConventionsTest, UnusableCameraFieldsLeaveTheProjectionToTheCameraRecord) {
    auto desc = MakeUpscaleDispatchDesc(); // the camera fields are all zero
    EXPECT_FALSE(ReadFfxMvConventions(kSupportedCreateFlags, desc).hasProjection);
    desc.cameraNear = 0.1f;
    desc.cameraFar = std::numeric_limits<float>::max(); // an infinite far plane
    desc.cameraFovAngleVertical = 1.0f;
    EXPECT_FALSE(ReadFfxMvConventions(kSupportedCreateFlags, desc).hasProjection);
    desc.cameraFar = 1000.0f;
    desc.cameraFovAngleVertical = 0.0f;
    EXPECT_FALSE(ReadFfxMvConventions(kSupportedCreateFlags, desc).hasProjection);
}

TEST(FfxMvConventionsTest, InfiniteDepthTakesTheNearPlaneWhicheverFieldCarriesIt) {
    constexpr uint32_t kInfiniteFlags = kSupportedCreateFlags | FFX_UPSCALE_ENABLE_DEPTH_INFINITE;
    auto desc = MakeUpscaleDispatchDesc();
    desc.cameraFovAngleVertical = 1.0f;
    desc.viewSpaceToMetersFactor = 2.0f;
    // The far field is meaningless with an infinite far plane: FLT_MAX, zero, or the near plane swapped in.
    desc.cameraNear = 0.1f;
    desc.cameraFar = std::numeric_limits<float>::max();
    FfxMvConventions c = ReadFfxMvConventions(kInfiniteFlags, desc);
    EXPECT_TRUE(c.depthInfinite);
    ASSERT_TRUE(c.hasProjection);
    EXPECT_FLOAT_EQ(c.nearClip, 0.2f);
    desc.cameraFar = 0.0f;
    c = ReadFfxMvConventions(kInfiniteFlags, desc);
    ASSERT_TRUE(c.hasProjection);
    EXPECT_FLOAT_EQ(c.nearClip, 0.2f);
    desc.cameraNear = std::numeric_limits<float>::max();
    desc.cameraFar = 0.1f;
    c = ReadFfxMvConventions(kInfiniteFlags, desc);
    ASSERT_TRUE(c.hasProjection);
    EXPECT_FLOAT_EQ(c.nearClip, 0.2f);
    desc.cameraNear = desc.cameraFar = 0.0f;
    EXPECT_FALSE(ReadFfxMvConventions(kInfiniteFlags, desc).hasProjection);
}

TEST(FfxMvConventionsTest, ANegativeXMotionVectorScaleIsReadWithItsSign) {
    auto desc = MakeUpscaleDispatchDesc();
    desc.motionVectorScale = {-3004.0f, 1690.0f}; // -width/2, +height/2 of a 6008x3380 render, as seen in game
    FfxMvConventions c = ReadFfxMvConventions(kSupportedCreateFlags | FFX_UPSCALE_ENABLE_DEPTH_INFINITE, desc);
    EXPECT_EQ(c.unsupportedReason, nullptr);
    EXPECT_FLOAT_EQ(c.mvScaleX, -3004.0f);
    EXPECT_FLOAT_EQ(c.mvScaleY, 1690.0f);
}

TEST(FfxMvConventionsTest, ConventionsTheCorrectionCannotHonourAreReportedAsUnsupported) {
    auto desc = MakeUpscaleDispatchDesc();
    EXPECT_NE(ReadFfxMvConventions(FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE, desc).unsupportedReason, nullptr)
        << "forward-Z depth";
    EXPECT_EQ(ReadFfxMvConventions(kSupportedCreateFlags | FFX_UPSCALE_ENABLE_DEPTH_INFINITE, desc).unsupportedReason, nullptr)
        << "reverse-Z with an infinite far plane is supported";
    EXPECT_NE(ReadFfxMvConventions(FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DEPTH_INFINITE, desc)
                  .unsupportedReason, nullptr) << "forward-Z with an infinite far plane";
    EXPECT_NE(ReadFfxMvConventions(kSupportedCreateFlags | FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS, desc)
                  .unsupportedReason, nullptr);
    EXPECT_NE(ReadFfxMvConventions(kSupportedCreateFlags | FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION, desc)
                  .unsupportedReason, nullptr);
    EXPECT_EQ(ReadFfxMvConventions(kSupportedCreateFlags | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE, desc).unsupportedReason, nullptr);
}

// ---- Motion vector correction (#38) ----

class FfxFakeMvPass : public IMotionVectorPass {
public:
    int recordCount = 0;
    MvPassInputs lastInputs;
    ID3D12Resource* output = reinterpret_cast<ID3D12Resource*>(0xBEEF);
    bool fail = false;
    std::vector<MvCorrectionRequest> corrections;
    std::vector<MvSelfCheckRequest> selfChecks;
    MvPassResult Record(const MvPassInputs& inputs) override {
        ++recordCount;
        lastInputs = inputs;
        if (inputs.correction) corrections.push_back(*inputs.correction);
        if (inputs.selfCheck) selfChecks.push_back(*inputs.selfCheck);
        MvPassResult r;
        if (fail) { r.failureReason = "unsupported motion vector format"; return r; }
        r.ok = true;
        r.output = output;
        return r;
    }
};

CameraRecord FfxCamera(uint32_t eye, double x) {
    CameraRecord r;
    r.eye = eye;
    r.position[0] = x;
    r.fovY = 1.0f;
    r.nearClip = 0.15f;
    r.farClip = 2000.0f;
    return r;
}

class FfxMvCorrectionTest : public FidelityFxInterceptorTest {
protected:
    FfxFakeMvPass* m_pass = nullptr;
    int m_factoryCalls = 0;
    ffxContext m_context = nullptr;
    ID3D12GraphicsCommandList* m_cmdList = reinterpret_cast<ID3D12GraphicsCommandList*>(0x1000);
    ID3D12Resource* m_engineMv = reinterpret_cast<ID3D12Resource*>(0xAAA0);
    ID3D12Resource* m_engineDepth = reinterpret_cast<ID3D12Resource*>(0xDDD0);

    void SetUp() override {
        FidelityFxInterceptorTest::SetUp();
        s_dispatchMotionVectors.clear();
        s_dispatchMotionVectorStates.clear();
        m_ngxShared->SetMvFixEnabled(false);
        AttachAll();
        m_ffx.SetMotionVectorPassFactory([this](ID3D12GraphicsCommandList*) {
            ++m_factoryCalls;
            auto pass = std::make_unique<FfxFakeMvPass>();
            m_pass = pass.get();
            return std::unique_ptr<IMotionVectorPass>(std::move(pass));
        });
        m_context = CreateUpscaler();
        m_ffx.ArmDiagnostics(0);
    }
    void TearDown() override {
        m_ngxShared->SetMvFixEnabled(false);
        FidelityFxInterceptorTest::TearDown();
    }

    // Sequential Dual-Pass, the left eye rendering after the right eye 0.064 m away, one frame after its own
    // previous render: all three cameras resolve.
    void SetUpSdpWithRecords(VRRenderingMode mode = VRRenderingMode::SequentialDualPass) {
        m_bridge.SetRenderingMode(mode); // switching mode drops camera records, so it comes first
        CameraRecordStore& cameras = m_bridge.GetCameraRecords();
        ASSERT_TRUE(cameras.Report(FfxCamera(0, 10.0)));
        cameras.AdvanceFrame();
        ASSERT_TRUE(cameras.Report(FfxCamera(1, 10.064)));
        cameras.AdvanceFrame();
        ASSERT_TRUE(cameras.Report(FfxCamera(0, 10.0)));
        m_bridge.SetSubPassIndex(0);
    }

    ffxDispatchDescUpscale MakeDesc() {
        auto desc = MakeUpscaleDispatchDesc();
        desc.commandList = m_cmdList;
        desc.depth.resource = m_engineDepth;
        desc.motionVectors.resource = m_engineMv;
        desc.motionVectors.state = 2;
        desc.motionVectorScale = {2.0f, 2.0f};
        desc.cameraNear = 0.2f;
        desc.cameraFar = 3000.0f;
        desc.cameraFovAngleVertical = 0.8f;
        return desc;
    }
};

TEST_F(FfxMvCorrectionTest, FsrReceivesTheInjectorsTextureAndTheGamesDescriptionIsRestored) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);

    ASSERT_NE(m_pass, nullptr);
    EXPECT_EQ(m_pass->recordCount, 1);
    EXPECT_EQ(m_pass->lastInputs.cmdList, m_cmdList);
    EXPECT_EQ(m_pass->lastInputs.depth, m_engineDepth);
    EXPECT_EQ(m_pass->lastInputs.motionVectors, m_engineMv);
    ASSERT_EQ(s_dispatchMotionVectors.size(), 1u);
    EXPECT_EQ(s_dispatchMotionVectors[0], m_pass->output);
    EXPECT_EQ(s_dispatchMotionVectorStates[0], FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    // The game's description is back as it was.
    EXPECT_EQ(desc.motionVectors.resource, m_engineMv);
    EXPECT_EQ(desc.motionVectors.state, 2u);
}

TEST_F(FfxMvCorrectionTest, TheCorrectionUsesTheDispatchsScaleAndCameraFieldsNotTheRecords) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    desc.cameraFovAngleVertical = 0.8f; // the record says 1.0 rad
    Dispatch(m_context, desc);

    ASSERT_NE(m_pass, nullptr);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    const MvCorrectionRequest& r = m_pass->corrections[0];
    EXPECT_FLOAT_EQ(r.mvScaleX, 2.0f);
    EXPECT_FLOAT_EQ(r.mvScaleY, 2.0f);
    EXPECT_NEAR(r.current.tanHalfFovY, std::tan(0.4f), 1e-6f);
    EXPECT_FLOAT_EQ(r.current.nearClip, 0.2f);
    EXPECT_FLOAT_EQ(r.current.farClip, 3000.0f);
    // The previous cameras keep the records' projection; the rendered-previous camera is the right eye
    // 0.064 m away, the same eye's previous camera is where the eye was.
    EXPECT_NEAR(r.previousFrame.tanHalfFovY, std::tan(0.5f), 1e-6f);
    EXPECT_NEAR(r.toPreviousFrame.c[0], -0.064f, 1e-5f);
    EXPECT_NEAR(r.toSameEye.c[0], 0.0f, 1e-5f);
}

TEST_F(FfxMvCorrectionTest, WithoutDispatchCameraFieldsTheRecordsProjectionIsUsed) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    desc.cameraNear = desc.cameraFar = desc.cameraFovAngleVertical = 0.0f;
    Dispatch(m_context, desc);
    ASSERT_NE(m_pass, nullptr);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_NEAR(m_pass->corrections[0].current.tanHalfFovY, std::tan(0.5f), 1e-6f);
    EXPECT_FLOAT_EQ(m_pass->corrections[0].current.nearClip, 0.15f);
}

TEST_F(FfxMvCorrectionTest, SwitchableAtRuntimeWithTheDlssSwitch) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(false);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    ASSERT_EQ(s_dispatchMotionVectors.size(), 1u);
    EXPECT_EQ(s_dispatchMotionVectors[0], m_engineMv);
    EXPECT_EQ(m_factoryCalls, 0);

    m_ngxShared->SetMvFixEnabled(true);
    Dispatch(m_context, desc);
    EXPECT_EQ(s_dispatchMotionVectors.back(), m_pass->output);

    m_ngxShared->SetMvFixEnabled(false);
    Dispatch(m_context, desc);
    EXPECT_EQ(s_dispatchMotionVectors.back(), m_engineMv);
    EXPECT_EQ(m_pass->recordCount, 1);
}

TEST_F(FfxMvCorrectionTest, MenusAndTwoDimensionalModeKeepTheEnginesMotionVectors) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    m_bridge.SetMenuVisible(true);
    Dispatch(m_context, desc);
    m_bridge.SetMenuVisible(false);
    m_bridge.Set2DViewActive(true);
    Dispatch(m_context, desc);
    ASSERT_EQ(s_dispatchMotionVectors.size(), 2u);
    EXPECT_EQ(s_dispatchMotionVectors[0], m_engineMv);
    EXPECT_EQ(s_dispatchMotionVectors[1], m_engineMv);
    EXPECT_EQ(m_factoryCalls, 0);
}

TEST_F(FfxMvCorrectionTest, AlternateFrameRenderingCorrectsWithTheObjectMotionEyeStride) {
    SetUpSdpWithRecords(VRRenderingMode::AFR);
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    ASSERT_NE(m_pass, nullptr);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_FLOAT_EQ(m_pass->corrections[0].objectMotionFactor, 2.0f);
    EXPECT_EQ(s_dispatchMotionVectors.back(), m_pass->output);
}

TEST_F(FfxMvCorrectionTest, TheFidelityFxCorrectionIsLeftUngated) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_FALSE(m_pass->corrections[0].gateNonStatic);
}

TEST_F(FfxMvCorrectionTest, SequentialDualPassCorrectsWithoutTheEyeStride) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_FLOAT_EQ(m_pass->corrections[0].objectMotionFactor, 1.0f);
}

TEST_F(FfxMvCorrectionTest, AlternateFrameRenderingSelfChecksTheCorrectedVectors) {
    SetUpSdpWithRecords(VRRenderingMode::AFR);
    m_ngxShared->SetMvFixEnabled(true);
    m_ffx.ArmDiagnostics(4);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    ASSERT_EQ(m_pass->selfChecks.size(), 1u);
    EXPECT_NEAR(m_pass->selfChecks[0].toPrevious.c[0], 0.0f, 1e-5f);
}

TEST_F(FfxMvCorrectionTest, AlternateFrameRenderingWithoutRecordsKeepsTheEnginesVectorsThroughACopy) {
    m_bridge.SetRenderingMode(VRRenderingMode::AFR);
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    ASSERT_NE(m_pass, nullptr);
    EXPECT_TRUE(m_pass->corrections.empty());
}

TEST_F(FfxMvCorrectionTest, TheSelfCheckAtAnEyeOrderSwapIsBuiltFromTheRawRecords) {
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    CameraRecordStore& cameras = m_bridge.GetCameraRecords();
    ASSERT_TRUE(cameras.Report(FfxCamera(1, 10.064)));
    cameras.AdvanceFrame();
    ASSERT_TRUE(cameras.Report(FfxCamera(1, 10.1))); // the right eye twice in a row, head moved
    m_bridge.SetSubPassIndex(1);
    m_ngxShared->SetMvFixEnabled(true);
    m_ffx.ArmDiagnostics(4);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);

    ASSERT_NE(m_pass, nullptr);
    EXPECT_TRUE(m_pass->corrections.empty());
    ASSERT_EQ(m_pass->selfChecks.size(), 1u);
    const MvSelfCheckRequest& r = m_pass->selfChecks[0];
    EXPECT_NEAR(r.previous.tanHalfFovY, std::tan(0.5f), 1e-6f);
    EXPECT_NEAR(r.toPrevious.m[0][0], 1.0f, 1e-5f);
    EXPECT_NEAR(r.toPrevious.c[0], 0.036f, 1e-5f);
}
TEST_F(FfxMvCorrectionTest, MissingCameraRecordsPassTheEnginesVectorsThroughACopyWithoutACorrection) {
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    m_bridge.GetCameraRecords().Clear();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    ASSERT_NE(m_pass, nullptr);
    EXPECT_EQ(m_pass->recordCount, 1);
    EXPECT_TRUE(m_pass->corrections.empty());
    EXPECT_EQ(m_pass->lastInputs.correction, nullptr);
}

TEST_F(FfxMvCorrectionTest, AZeroMotionVectorScalePassesTheEnginesVectorsThrough) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    desc.motionVectorScale = {0.0f, 0.0f};
    Dispatch(m_context, desc);
    ASSERT_NE(m_pass, nullptr);
    EXPECT_TRUE(m_pass->corrections.empty());
}

TEST_F(FfxMvCorrectionTest, AnEyeOrderSwapNeedsNoCorrection) {
    m_bridge.SetRenderingMode(VRRenderingMode::SequentialDualPass);
    CameraRecordStore& cameras = m_bridge.GetCameraRecords();
    ASSERT_TRUE(cameras.Report(FfxCamera(1, 10.064)));
    cameras.AdvanceFrame();
    ASSERT_TRUE(cameras.Report(FfxCamera(1, 10.064))); // the right eye twice in a row
    m_bridge.SetSubPassIndex(1);
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    ASSERT_NE(m_pass, nullptr);
    EXPECT_TRUE(m_pass->corrections.empty());
}

TEST_F(FfxMvCorrectionTest, ConventionsThePassCannotHonourKeepTheEnginesVectorsAndSkipThePass) {
    // A context created with display-resolution motion vectors.
    ffxContext context = nullptr;
    auto createDesc = MakeUpscaleCreateDesc(kSupportedCreateFlags | FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS);
    ASSERT_EQ(MockCreate(&context, &createDesc.header, nullptr), FFX_API_RETURN_OK);
    m_ffx.ArmDiagnostics(0);
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(context, desc);
    EXPECT_EQ(s_dispatchMotionVectors.back(), m_engineMv);
    EXPECT_EQ(m_factoryCalls, 0);
}

TEST_F(FfxMvCorrectionTest, InfiniteDepthContextsAreCorrectedAndTheRequestSaysSo) {
    // The game's FSR3 flags: HDR | DepthInverted | DepthInfinite (0x19), with a negative x scale.
    ffxContext context = nullptr;
    auto createDesc = MakeUpscaleCreateDesc(kSupportedCreateFlags | FFX_UPSCALE_ENABLE_DEPTH_INFINITE);
    ASSERT_EQ(MockCreate(&context, &createDesc.header, nullptr), FFX_API_RETURN_OK);
    m_ffx.ArmDiagnostics(0);
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    desc.motionVectorScale = {-3004.0f, 1690.0f};
    desc.cameraNear = 0.2f;
    desc.cameraFar = std::numeric_limits<float>::max();
    Dispatch(context, desc);

    ASSERT_NE(m_pass, nullptr);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    const MvCorrectionRequest& r = m_pass->corrections[0];
    EXPECT_TRUE(r.current.infiniteDepth);
    EXPECT_FALSE(r.previousFrame.infiniteDepth);
    EXPECT_FLOAT_EQ(r.mvScaleX, -3004.0f);
    EXPECT_FLOAT_EQ(r.mvScaleY, 1690.0f);
    EXPECT_FLOAT_EQ(r.current.nearClip, 0.2f);
    EXPECT_EQ(s_dispatchMotionVectors.back(), m_pass->output);
}

TEST_F(FfxMvCorrectionTest, FiniteDepthContextsDoNotRequestInfiniteDepth) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    ASSERT_NE(m_pass, nullptr);
    ASSERT_EQ(m_pass->corrections.size(), 1u);
    EXPECT_FALSE(m_pass->corrections[0].current.infiniteDepth);
}

TEST(MvProjectionLineTest, ShowsTheDispatchAndTheRecordSideBySideWithTheirDifference) {
    MvProjectionDiag d;
    d.source = "dispatch-desc";
    d.used = {0.2f, 0.0f, 0.8f};
    d.projOffsetX = 0.01f;
    d.projOffsetY = -0.02f;
    d.aspect = 1920.0f / 1080.0f;
    d.infinite = true;
    d.depthSource = "create-flags";
    d.hasComparison = true;
    d.dispatchRaw = {0.2f, std::numeric_limits<float>::max(), 0.8f};
    d.viewSpaceToMeters = 1.0f;
    d.record = {0.15f, 2000.0f, 1.0f};
    // tan(0.4) = 0.423, tan(0.5) = 0.546, 1920 / 1080 = 1.778, 0.15 - 0.2 = -0.05, 1.0 - 0.8 = 0.2
    EXPECT_EQ(FormatMvProjectionLine("FidelityFxInterceptor", "Dispatch", 0, 3, d),
              "FidelityFxInterceptor: [Diag] MvProjection Eye=0 Dispatch #3: used=dispatch-desc nearClip=0.200 "
              "farClip=0.000 fovY=0.800 tanHalfFovY=0.423 projOffset=(0.010, -0.020) aspect=1.778 "
              "depthModel=infinite depthSource=create-flags dispatch{nearClip=0.200 farClip=max fovY=0.800 "
              "viewSpaceToMeters=1.000} record{nearClip=0.150 farClip=2000.000 fovY=1.000 tanHalfFovY=0.546} "
              "recordMinusDispatch{nearClip=-0.050 fovY=0.200}");
}

TEST(MvProjectionLineTest, SaysSoWhenTheDispatchFieldsWereUnusable) {
    MvProjectionDiag d;
    d.source = "camera-record";
    d.used = {0.15f, 2000.0f, 1.0f};
    d.aspect = 2.0f;
    d.infinite = false;
    d.depthSource = "create-flags";
    d.hasComparison = true;
    d.dispatchRaw = {0.0f, 0.0f, 0.0f};
    d.viewSpaceToMeters = 0.0f;
    d.record = {0.15f, 2000.0f, 1.0f};
    const std::string line = FormatMvProjectionLine("FidelityFxInterceptor", "Dispatch", 1, 9, d);
    EXPECT_NE(line.find("used=camera-record nearClip=0.150 farClip=2000.000 fovY=1.000 tanHalfFovY=0.546 "
                        "projOffset=(0.000, 0.000) aspect=2.000 depthModel=finite depthSource=create-flags"),
              std::string::npos) << line;
    EXPECT_NE(line.find("dispatch{nearClip=0.000 farClip=0.000 fovY=0.000 viewSpaceToMeters=0.000}"), std::string::npos) << line;
    EXPECT_NE(line.find("recordMinusDispatch=n/a"), std::string::npos) << line;
}

class FfxMvProjectionDiagTest : public FfxMvCorrectionTest {
protected:
    std::filesystem::path m_logPath;
    void SetUp() override {
        FfxMvCorrectionTest::SetUp();
        m_logPath = std::filesystem::temp_directory_path() / "fs25vr_ffx_mvprojection_test.log";
        Logger::Instance().SetLogFile(m_logPath);
    }
    void TearDown() override {
        FfxMvCorrectionTest::TearDown();
        Logger::Instance().SetLogFile(Logger::DefaultLogPath());
        std::filesystem::remove(m_logPath);
    }
    std::string Log() {
        Logger::Instance().Flush();
        std::ifstream in(m_logPath, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
};

TEST_F(FfxMvProjectionDiagTest, TheDispatchAndRecordProjectionsAreLoggedOncePerArming) {
    ffxContext context = nullptr;
    auto createDesc = MakeUpscaleCreateDesc(kSupportedCreateFlags | FFX_UPSCALE_ENABLE_DEPTH_INFINITE);
    ASSERT_EQ(MockCreate(&context, &createDesc.header, nullptr), FFX_API_RETURN_OK);
    m_ffx.ArmDiagnostics(0); // creating a context arms the diagnostics
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    desc.cameraNear = 0.2f;
    desc.cameraFar = std::numeric_limits<float>::max();
    Dispatch(context, desc);
    EXPECT_EQ(Log().find("MvProjection"), std::string::npos) << "not armed";

    m_ffx.ArmDiagnostics(8);
    Dispatch(context, desc);
    Dispatch(context, desc);
    const std::string log = Log();
    const size_t first = log.find("[Diag] MvProjection");
    ASSERT_NE(first, std::string::npos) << log;
    EXPECT_EQ(log.find("[Diag] MvProjection", first + 1), std::string::npos) << "once per arming";
    // The dispatch says near 0.2, fovY 0.8; the record says near 0.15, fovY 1.0 rad.
    EXPECT_NE(log.find("used=dispatch-desc nearClip=0.200"), std::string::npos) << log;
    EXPECT_NE(log.find("depthModel=infinite depthSource=create-flags"), std::string::npos) << log;
    EXPECT_NE(log.find("record{nearClip=0.150 farClip=2000.000 fovY=1.000"), std::string::npos) << log;
    EXPECT_NE(log.find("recordMinusDispatch{nearClip=-0.050 fovY=0.200}"), std::string::npos) << log;
}

TEST_F(FfxMvCorrectionTest, DispatchWithoutCommandListOrInputsPassesThroughWithoutDisabling) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto noList = MakeDesc();
    noList.commandList = nullptr;
    Dispatch(m_context, noList);
    auto noDepth = MakeDesc();
    noDepth.depth.resource = nullptr;
    Dispatch(m_context, noDepth);
    EXPECT_EQ(s_dispatchMotionVectors[0], m_engineMv);
    EXPECT_EQ(s_dispatchMotionVectors[1], m_engineMv);
    EXPECT_FALSE(m_ffx.IsMvFixDisabledForSession());
}

TEST_F(FfxMvCorrectionTest, FailureDisablesThePassForTheSessionAndFsrKeepsTheEnginesVectors) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    ASSERT_NE(m_pass, nullptr);
    m_pass->fail = true;

    Dispatch(m_context, desc);
    EXPECT_EQ(s_dispatchMotionVectors.back(), m_engineMv);
    EXPECT_EQ(desc.motionVectors.resource, m_engineMv);
    EXPECT_TRUE(m_ffx.IsMvFixDisabledForSession());
    EXPECT_EQ(m_ffx.GetMvFixDisableReason(), "unsupported motion vector format");

    int calls = m_pass->recordCount;
    Dispatch(m_context, desc);
    EXPECT_EQ(m_pass->recordCount, calls);
    EXPECT_EQ(s_dispatchMotionVectors.back(), m_engineMv);
}

TEST_F(FfxMvCorrectionTest, MissingPassFromTheFactoryDisablesThePass) {
    SetUpSdpWithRecords();
    m_ffx.SetMotionVectorPassFactory([](ID3D12GraphicsCommandList*) { return std::unique_ptr<IMotionVectorPass>(); });
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    EXPECT_EQ(s_dispatchMotionVectors.back(), m_engineMv);
    EXPECT_TRUE(m_ffx.IsMvFixDisabledForSession());
}

TEST_F(FfxMvCorrectionTest, ACorrectionComposesWithADiscontinuityResetAndBothAreRestored) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc); // latches the mode
    m_ffx.TriggerDiscontinuityFlush();
    Dispatch(m_context, desc);
    EXPECT_TRUE(s_dispatchReset.back());
    EXPECT_EQ(s_dispatchMotionVectors.back(), m_pass->output);
    EXPECT_FALSE(desc.reset);
    EXPECT_EQ(desc.motionVectors.resource, m_engineMv);
}

TEST_F(FfxMvCorrectionTest, ArmedDiagnosticsAskForASelfCheckOfTheCorrectedVectors) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    m_ffx.ArmDiagnostics(4);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);

    ASSERT_NE(m_pass, nullptr);
    ASSERT_EQ(m_pass->selfChecks.size(), 1u);
    const MvSelfCheckRequest& r = m_pass->selfChecks[0];
    EXPECT_EQ(r.eye, 0u);
    EXPECT_FLOAT_EQ(r.mvScaleX, 2.0f);
    EXPECT_NEAR(r.current.tanHalfFovY, std::tan(0.4f), 1e-6f);
    EXPECT_FLOAT_EQ(r.current.farClip, 3000.0f);
    // The vectors scored are the corrected ones: they should land on the same eye's previous camera.
    EXPECT_NEAR(r.toPrevious.c[0], 0.0f, 1e-5f);
}

TEST_F(FfxMvCorrectionTest, NoSelfCheckWhileDiagnosticsAreNotArmed) {
    SetUpSdpWithRecords();
    m_ngxShared->SetMvFixEnabled(true);
    auto desc = MakeDesc();
    Dispatch(m_context, desc);
    ASSERT_NE(m_pass, nullptr);
    EXPECT_TRUE(m_pass->selfChecks.empty());
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
