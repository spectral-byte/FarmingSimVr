// FOV coverage (ADR-0005): the eyes are rendered and submitted with a symmetric FOV that covers every edge of
// the headset's own FOV. These tests run on the Fake Tracked Headset and look only at what leaves the system:
// the FOV the engine camera is given, the FOV the compositor received at xrEndFrame, VRBridge's published
// eye projection and the log.
#include "companion_mod_fixture.h"
#include "core/logger.h"
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {

struct RenderResolution {
    uint32_t width;
    uint32_t height;
};

// Square-ish (Virtual Desktop's Godlike preset) and 16:9.
constexpr RenderResolution kSquarish{ 3072, 3216 };
constexpr RenderResolution kWidescreen{ 1920, 1080 };

// An asymmetric headset: wide outer side, narrow inner side, shallow bottom. Left eye as an OpenXR runtime
// reports it (outer = left); the right eye is its mirror.
const XrFovf kLeftEyeFov{ -0.95f, 0.70f, 0.80f, -0.60f };
const XrFovf kRightEyeFov{ -0.70f, 0.95f, 0.80f, -0.60f };

void SetHeadsetFov(fs25vr::FakeTrackedHeadset& headset) {
    headset.SetEyeFov(0, kLeftEyeFov);
    headset.SetEyeFov(1, kRightEyeFov);
}

// Starts a Fake Tracked Headset with the asymmetric FOV and the eye swapchains created at `resolution`.
void StartHeadset(fs25vr::FakeTrackedHeadset& headset, const RenderResolution& resolution) {
    SetHeadsetFov(headset);
    ASSERT_TRUE(headset.manager->CreateEyeSwapchains(resolution.width, resolution.height));
}

// Copies both eyes and submits the pair, as the Injector does once per Stereo Presentation pair.
void SubmitPair(fs25vr::FakeTrackedHeadset& headset) {
    ASSERT_TRUE(headset.manager->CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(headset.manager->CopyBackBufferToEye(1, nullptr));
    ASSERT_TRUE(headset.manager->SubmitFramePair());
}

// Redirects the game log to a temp file while a test runs and counts lines containing a needle.
class LogCapture {
public:
    LogCapture() : m_path(std::filesystem::temp_directory_path() / "fs25vr_fov_coverage_log_test.log") {
        std::filesystem::remove(m_path);
        fs25vr::Logger::Instance().SetLogFile(m_path);
    }
    ~LogCapture() {
        fs25vr::Logger::Instance().SetLogFile(fs25vr::Logger::DefaultLogPath());
        std::filesystem::remove(m_path);
    }
    size_t Count(const std::string& needle) const {
        fs25vr::Logger::Instance().Flush();
        std::ifstream file(m_path);
        std::string line;
        size_t count = 0;
        while (std::getline(file, line)) {
            if (line.find(needle) != std::string::npos) ++count;
        }
        return count;
    }
    std::string LastLineWith(const std::string& needle) const {
        fs25vr::Logger::Instance().Flush();
        std::ifstream file(m_path);
        std::string line, last;
        while (std::getline(file, line)) {
            if (line.find(needle) != std::string::npos) last = line;
        }
        return last;
    }
private:
    std::filesystem::path m_path;
};

} // namespace

class FovCoverageTest : public ::testing::TestWithParam<RenderResolution> {};

TEST_P(FovCoverageTest, EverySubmittedEdgeReachesTheMatchingHeadsetEdgeForBothEyes) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, GetParam());
    SubmitPair(headset);

    const auto& views = headset.SubmittedViews();
    ASSERT_EQ(views.size(), 2u);
    const XrFovf headsetFov[2] = { kLeftEyeFov, kRightEyeFov };
    for (int eye = 0; eye < 2; ++eye) {
        const XrFovf& submitted = views[eye].fov;
        const XrFovf& headsetEye = headsetFov[eye];
        EXPECT_LE(submitted.angleLeft, headsetEye.angleLeft) << "eye " << eye << " left edge";
        EXPECT_GE(submitted.angleRight, headsetEye.angleRight) << "eye " << eye << " right edge";
        EXPECT_GE(submitted.angleUp, headsetEye.angleUp) << "eye " << eye << " up edge";
        EXPECT_LE(submitted.angleDown, headsetEye.angleDown) << "eye " << eye << " down edge";
    }
}

TEST_P(FovCoverageTest, SubmittedFovIsSymmetricAndItsHorizontalSpanMatchesTheRenderResolutionAspect) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, GetParam());
    SubmitPair(headset);

    const auto& views = headset.SubmittedViews();
    ASSERT_EQ(views.size(), 2u);
    const double aspect = static_cast<double>(GetParam().width) / GetParam().height;
    for (int eye = 0; eye < 2; ++eye) {
        const XrFovf& fov = views[eye].fov;
        EXPECT_FLOAT_EQ(fov.angleLeft, -fov.angleRight) << "eye " << eye;
        EXPECT_FLOAT_EQ(fov.angleDown, -fov.angleUp) << "eye " << eye;
        EXPECT_NEAR(std::tan(fov.angleRight) / std::tan(fov.angleUp), aspect, 1e-4) << "eye " << eye;
    }
}

INSTANTIATE_TEST_SUITE_P(RenderResolutions, FovCoverageTest, ::testing::Values(kSquarish, kWidescreen));

// Worked values, computed by hand from the fixture (tan 0.95 = 1.3984, tan 0.80 = 1.0296, tan 0.60 = 0.6841):
// at 3072x3216 the outer side (1.3984 / 0.9552 = 1.4639) is the widest, so the vertical half-angle is atan(1.4639)
// = 0.9715 and the horizontal one is the outer 0.95 exactly; at 16:9 the upper side (0.80) is the widest and the
// horizontal half-angle is atan(16/9 * tan 0.80) = 1.0708.
TEST(FovCoverageValuesTest, SubmittedFovIsTheWidestSideAtTheRenderResolutionAspect) {
    {
        fs25vr::FakeTrackedHeadset headset;
        StartHeadset(headset, kSquarish);
        SubmitPair(headset);
        ASSERT_EQ(headset.SubmittedViews().size(), 2u);
        for (int eye = 0; eye < 2; ++eye) {
            EXPECT_NEAR(headset.SubmittedViews()[eye].fov.angleUp, 0.9715f, 0.0005f) << "eye " << eye;
            EXPECT_NEAR(headset.SubmittedViews()[eye].fov.angleRight, 0.95f, 0.0005f) << "eye " << eye;
        }
    }
    {
        fs25vr::FakeTrackedHeadset headset;
        StartHeadset(headset, kWidescreen);
        SubmitPair(headset);
        ASSERT_EQ(headset.SubmittedViews().size(), 2u);
        for (int eye = 0; eye < 2; ++eye) {
            EXPECT_NEAR(headset.SubmittedViews()[eye].fov.angleUp, 0.80f, 0.0005f) << "eye " << eye;
            EXPECT_NEAR(headset.SubmittedViews()[eye].fov.angleRight, 1.0708f, 0.0005f) << "eye " << eye;
        }
    }
}

TEST(FovCoverageValuesTest, ARuntimeFovChangeDuringTheSessionIsCoveredFromTheNextPair) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, kSquarish);
    SubmitPair(headset);
    ASSERT_EQ(headset.SubmittedViews().size(), 2u);
    EXPECT_NEAR(headset.SubmittedViews()[0].fov.angleUp, 0.9715f, 0.0005f);

    // The player lowers Virtual Desktop's FOV Tangent: every side shrinks.
    headset.SetEyeFov(0, { -0.70f, 0.50f, 0.60f, -0.40f });
    headset.SetEyeFov(1, { -0.50f, 0.70f, 0.60f, -0.40f });
    SubmitPair(headset);
    ASSERT_EQ(headset.SubmittedViews().size(), 2u);
    // Outer tan 0.70 = 0.8423; / 0.9552 = 0.8818 -> atan = 0.7228. The horizontal half-angle is the outer 0.70.
    EXPECT_NEAR(headset.SubmittedViews()[0].fov.angleUp, 0.7228f, 0.0005f);
    EXPECT_NEAR(headset.SubmittedViews()[0].fov.angleRight, 0.70f, 0.0005f);
}

// The Companion Mod hands the engine camera the vertical FOV VRBridge publishes, so the camera and the compositor
// must agree.
class FovCoverageCameraTest : public CompanionModTest, public ::testing::WithParamInterface<RenderResolution> {};

TEST_P(FovCoverageCameraTest, TheEngineCameraFovEqualsTheSubmittedVerticalFovForBothEyes) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, GetParam());

    fs25vr::VRBridge bridge(headset.manager, nullptr);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        function setTranslation(node, x, y, z) end
        function setRotation(node, rx, ry, rz) end
        _G.mockVehicleCam = { isInside = true, cameraNode = 301, origTransX = 0, origTransY = 1.5, origTransZ = 0 }
    )luau", "=setupCamera"), 0);

    // AFR: even engine frames render the left eye, odd frames the right.
    double cameraFovY[2] = {};
    for (uint32_t eye = 0; eye < 2; ++eye) {
        ASSERT_EQ(bridge.GetFrameParity(), eye);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onVehicleCameraUpdate(_G.mockVehicleCam, 16.6)", "=cameraUpdate"), 0);
        cameraFovY[eye] = std::stod(EvalLua(L, "_G.cameraFovY[301]"));
        bridge.AdvanceFrame();
    }

    SubmitPair(headset);
    const auto& views = headset.SubmittedViews();
    ASSERT_EQ(views.size(), 2u);
    for (int eye = 0; eye < 2; ++eye) {
        EXPECT_NEAR(cameraFovY[eye], 2.0 * views[eye].fov.angleUp, 1e-4) << "eye " << eye;
        EXPECT_NEAR(cameraFovY[eye], -2.0 * views[eye].fov.angleDown, 1e-4) << "eye " << eye;
    }
}

INSTANTIATE_TEST_SUITE_P(RenderResolutions, FovCoverageCameraTest, ::testing::Values(kSquarish, kWidescreen));

// VRBridge's eye projection: fovY / fovX are the rendered FOV; the tangents and offsets still report the headset's
// own FOV. Before the eye swapchains exist the coverage uses the headset FOV's own aspect (tan 0.95 + tan 0.70 =
// 2.2407 over tan 0.80 + tan 0.60 = 1.7137, aspect 1.3075): the outer side 1.3984 / 1.3075 = 1.0696 is the widest,
// so the vertical FOV is 2 * atan(1.0696) = 1.638 and the horizontal one is the outer span, 1.9. Once the swapchains
// exist it uses the swapchain's aspect.
TEST_F(CompanionModTest, EyeProjectionPublishesTheRenderedFovAndTheHeadsetTangents) {
    fs25vr::FakeTrackedHeadset headset;
    SetHeadsetFov(headset);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local left = VRBridge.getEyeProjection(0)
        assert(left.isValid)
        assert(math.abs(left.fovY - 1.638) < 0.002, "pre-swapchain fovY " .. left.fovY)
        assert(math.abs(left.fovX - 1.9) < 0.002, "pre-swapchain fovX " .. left.fovX)
        assert(math.abs(left.tanLeft - math.tan(-0.95)) < 1e-4, "tanLeft is the headset's")
        assert(math.abs(left.tanRight - math.tan(0.70)) < 1e-4, "tanRight is the headset's")
        assert(math.abs(left.tanUp - math.tan(0.80)) < 1e-4, "tanUp is the headset's")
        assert(math.abs(left.tanDown - math.tan(-0.60)) < 1e-4, "tanDown is the headset's")
        assert(math.abs(left.xOffset - (math.tan(0.70) + math.tan(-0.95))) < 1e-4, "xOffset is the headset's")
        assert(math.abs(left.yOffset - (math.tan(0.80) + math.tan(-0.60))) < 1e-4, "yOffset is the headset's")
    )luau", "=beforeSwapchains"), 0);

    ASSERT_TRUE(headset.manager->CreateEyeSwapchains(kSquarish.width, kSquarish.height));

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local left = VRBridge.getEyeProjection(0)
        assert(math.abs(left.fovY - 2 * 0.9715) < 0.002, "swapchain fovY " .. left.fovY)
        assert(math.abs(left.fovX - 2 * 0.95) < 0.002, "swapchain fovX " .. left.fovX)
        assert(math.abs(left.tanLeft - math.tan(-0.95)) < 1e-4, "tanLeft is still the headset's")
        assert(math.abs(left.tanUp - math.tan(0.80)) < 1e-4, "tanUp is still the headset's")
        assert(math.abs(left.xOffset - (math.tan(0.70) + math.tan(-0.95))) < 1e-4, "xOffset is still the headset's")
    )luau", "=afterSwapchains"), 0);
}

TEST(FovCoverageLogTest, TheHeadsetFovIsLoggedOnceAndAgainOnlyWhenItChanges) {
    LogCapture log;
    fs25vr::FakeTrackedHeadset headset;
    SetHeadsetFov(headset);

    ASSERT_TRUE(headset.manager->LocateActiveViews(1000000));
    EXPECT_EQ(log.Count("OpenXrManager: Headset FOV"), 1u);
    // -0.95 rad = -54.43 deg, 0.70 = 40.11, 0.80 = 45.84, -0.60 = -34.38 (left eye).
    const std::string line = log.LastLineWith("OpenXrManager: Headset FOV");
    EXPECT_NE(line.find("-54.4"), std::string::npos) << line;
    EXPECT_NE(line.find("40.1"), std::string::npos) << line;
    EXPECT_NE(line.find("45.8"), std::string::npos) << line;
    EXPECT_NE(line.find("-34.4"), std::string::npos) << line;

    ASSERT_TRUE(headset.manager->LocateActiveViews(2000000));
    ASSERT_TRUE(headset.manager->LocateActiveViews(3000000));
    EXPECT_EQ(log.Count("OpenXrManager: Headset FOV"), 1u) << "an unchanged headset FOV is not logged again";

    headset.SetEyeFov(0, { -0.70f, 0.50f, 0.60f, -0.40f });
    ASSERT_TRUE(headset.manager->LocateActiveViews(4000000));
    EXPECT_EQ(log.Count("OpenXrManager: Headset FOV"), 2u);
}

TEST(FovCoverageLogTest, TheRenderedFovIsLoggedWheneverItChanges) {
    LogCapture log;
    fs25vr::FakeTrackedHeadset headset;
    SetHeadsetFov(headset);

    ASSERT_TRUE(headset.manager->LocateActiveViews(1000000));
    EXPECT_EQ(log.Count("OpenXrManager: Rendered FOV"), 1u);

    // The swapchains give the coverage a new aspect: the rendered FOV changes, the headset's does not.
    ASSERT_TRUE(headset.manager->CreateEyeSwapchains(kSquarish.width, kSquarish.height));
    EXPECT_EQ(log.Count("OpenXrManager: Rendered FOV"), 2u);
    EXPECT_EQ(log.Count("OpenXrManager: Headset FOV"), 1u);
    // vertical 2 * 0.9715 = 111.3 degrees, horizontal 2 * 0.95 = 108.9 degrees.
    const std::string line = log.LastLineWith("OpenXrManager: Rendered FOV");
    EXPECT_NE(line.find("111.3"), std::string::npos) << line;
    EXPECT_NE(line.find("108.9"), std::string::npos) << line;

    ASSERT_TRUE(headset.manager->LocateActiveViews(2000000));
    EXPECT_EQ(log.Count("OpenXrManager: Rendered FOV"), 2u) << "an unchanged rendered FOV is not logged again";

    headset.SetEyeFov(0, { -0.70f, 0.50f, 0.60f, -0.40f });
    headset.SetEyeFov(1, { -0.50f, 0.70f, 0.60f, -0.40f });
    ASSERT_TRUE(headset.manager->LocateActiveViews(3000000));
    EXPECT_EQ(log.Count("OpenXrManager: Rendered FOV"), 3u);
}
