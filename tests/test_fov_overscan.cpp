// FOV Overscan (spec #221, ticket #223): the eyes are rendered with the covering symmetric FOV widened by
// (1 + percent / 100) in tangents. The setting reaches the Injector through VRBridge and takes effect when the next
// OpenXR frame begins. These tests run on the Fake Tracked Headset and look only at what leaves the system: the FOV
// the engine camera is given, the FOV the compositor received at xrEndFrame, the VRBridge getter, the console
// replies and the log.
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

// An asymmetric headset, left eye as an OpenXR runtime reports it (outer = left); the right eye is its mirror.
const XrFovf kLeftEyeFov{ -0.95f, 0.70f, 0.80f, -0.60f };
const XrFovf kRightEyeFov{ -0.70f, 0.95f, 0.80f, -0.60f };

void StartHeadset(fs25vr::FakeTrackedHeadset& headset, const RenderResolution& resolution) {
    headset.SetEyeFov(0, kLeftEyeFov);
    headset.SetEyeFov(1, kRightEyeFov);
    ASSERT_TRUE(headset.manager->CreateEyeSwapchains(resolution.width, resolution.height));
}

// Copies both eyes and submits the pair, as the Injector does once per Stereo Presentation pair.
void SubmitPair(fs25vr::FakeTrackedHeadset& headset) {
    ASSERT_TRUE(headset.manager->CopyBackBufferToEye(0, nullptr));
    ASSERT_TRUE(headset.manager->CopyBackBufferToEye(1, nullptr));
    ASSERT_TRUE(headset.manager->SubmitFramePair());
}

// Redirects the game log to a temp file while a test runs and reads back the lines containing a needle.
class LogCapture {
public:
    LogCapture() : m_path(std::filesystem::temp_directory_path() / "fs25vr_fov_overscan_log_test.log") {
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

class FovOverscanTest : public CompanionModTest, public ::testing::WithParamInterface<RenderResolution> {};

TEST_P(FovOverscanTest, OverscanTenWidensTheSubmittedTangentsByExactlyTenPercentForBothEyes) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, GetParam());
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    SubmitPair(headset);
    ASSERT_EQ(headset.SubmittedViews().size(), 2u);
    const XrFovf base[2] = { headset.SubmittedViews()[0].fov, headset.SubmittedViews()[1].fov };

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setFovOverscan(10)", "=setOverscan"), 0);
    SubmitPair(headset);
    ASSERT_EQ(headset.SubmittedViews().size(), 2u);
    for (int eye = 0; eye < 2; ++eye) {
        const XrFovf& widened = headset.SubmittedViews()[eye].fov;
        EXPECT_NEAR(std::tan(widened.angleRight) / std::tan(base[eye].angleRight), 1.10, 1e-4) << "eye " << eye << " right";
        EXPECT_NEAR(std::tan(widened.angleLeft) / std::tan(base[eye].angleLeft), 1.10, 1e-4) << "eye " << eye << " left";
        EXPECT_NEAR(std::tan(widened.angleUp) / std::tan(base[eye].angleUp), 1.10, 1e-4) << "eye " << eye << " up";
        EXPECT_NEAR(std::tan(widened.angleDown) / std::tan(base[eye].angleDown), 1.10, 1e-4) << "eye " << eye << " down";
    }
}

TEST_P(FovOverscanTest, AnOverscanSetBetweenTheTwoEyesCopiesLeavesThatPairOnTheFovItBeganWithAndTheNextPairUsesTheNewOne) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, GetParam());
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    SubmitPair(headset);
    ASSERT_EQ(headset.SubmittedViews().size(), 2u);
    const XrFovf base[2] = { headset.SubmittedViews()[0].fov, headset.SubmittedViews()[1].fov };

    // The pair begins with the first eye's copy; the player types the command before the second eye's copy.
    ASSERT_TRUE(headset.manager->CopyBackBufferToEye(0, nullptr));
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setFovOverscan(10)", "=setOverscanMidPair"), 0);
    ASSERT_TRUE(headset.manager->CopyBackBufferToEye(1, nullptr));
    ASSERT_TRUE(headset.manager->SubmitFramePair());
    ASSERT_EQ(headset.SubmittedViews().size(), 2u);
    for (int eye = 0; eye < 2; ++eye) {
        EXPECT_FLOAT_EQ(headset.SubmittedViews()[eye].fov.angleUp, base[eye].angleUp) << "eye " << eye << " stays on the old FOV";
        EXPECT_FLOAT_EQ(headset.SubmittedViews()[eye].fov.angleRight, base[eye].angleRight) << "eye " << eye << " stays on the old FOV";
    }

    SubmitPair(headset);
    ASSERT_EQ(headset.SubmittedViews().size(), 2u);
    for (int eye = 0; eye < 2; ++eye) {
        EXPECT_NEAR(std::tan(headset.SubmittedViews()[eye].fov.angleUp) / std::tan(base[eye].angleUp), 1.10, 1e-4) << "eye " << eye;
    }
}

TEST_P(FovOverscanTest, VRBridgePublishesTheWidenedRenderedFovAndKeepsReportingTheHeadsetTangents) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, GetParam());
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local base = VRBridge.getEyeProjection(0)
        assert(base.isValid)
        _G.baseHalfTanY = math.tan(base.fovY / 2)
        _G.baseHalfTanX = math.tan(base.fovX / 2)
        VRBridge.setFovOverscan(10)
        local widened = VRBridge.getEyeProjection(0)
        assert(math.abs(math.tan(widened.fovY / 2) / _G.baseHalfTanY - 1.1) < 1e-4, "fovY widened by 10 percent in tangents: " .. widened.fovY)
        assert(math.abs(math.tan(widened.fovX / 2) / _G.baseHalfTanX - 1.1) < 1e-4, "fovX widened by the same factor: " .. widened.fovX)
        assert(math.abs(widened.tanLeft - math.tan(-0.95)) < 1e-4, "tanLeft is still the headset's")
        assert(math.abs(widened.tanRight - math.tan(0.70)) < 1e-4, "tanRight is still the headset's")
        assert(math.abs(widened.tanUp - math.tan(0.80)) < 1e-4, "tanUp is still the headset's")
        assert(math.abs(widened.tanDown - math.tan(-0.60)) < 1e-4, "tanDown is still the headset's")
        assert(math.abs(widened.xOffset - (math.tan(0.70) + math.tan(-0.95))) < 1e-4, "xOffset is still the headset's")
    )luau", "=publishedProjection"), 0);
}

INSTANTIATE_TEST_SUITE_P(RenderResolutions, FovOverscanTest, ::testing::Values(kSquarish, kWidescreen));

// The Companion Mod hands the engine camera the vertical FOV VRBridge publishes, so with an overscan set the camera and
// the compositor must still agree, in either Stereo Presentation Mode.
class FovOverscanCameraTest : public CompanionModTest {
protected:
    // Loads the Companion Mod over `bridge` with a vehicle cab camera (node 301) as the engine's camera.
    void LoadMod(fs25vr::VRBridge& bridge, fs25vr::VRRenderingMode mode) {
        bridge.SetRenderingMode(mode);
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        SetupCameraMockGlobals(L);
        ASSERT_EQ(RunLuauChunk(L, R"luau(
            function setTranslation(node, x, y, z) end
            function setRotation(node, rx, ry, rz) end
            _G.mockVehicleCam = { isInside = true, cameraNode = 301, origTransX = 0, origTransY = 1.5, origTransZ = 0 }
        )luau", "=setupCamera"), 0);
    }

    // The FOV the engine camera is given for one eye's camera update.
    double CameraFovYForEye(fs25vr::VRBridge& bridge, uint32_t eye) {
        if (bridge.GetRenderingMode() == fs25vr::VRRenderingMode::SequentialDualPass) {
            bridge.SetSubPassIndex(eye);
        } else {
            EXPECT_EQ(bridge.GetFrameParity(), eye);
        }
        EXPECT_EQ(RunLuauChunk(L, "_G.cameraFovY[301] = nil; VRCompanion:onVehicleCameraUpdate(_G.mockVehicleCam, 16.6)", "=cameraUpdate"), 0);
        const std::string fov = EvalLua(L, "_G.cameraFovY[301]");
        return fov == "nil" ? 0.0 : std::stod(fov);
    }

    // Renders one pair the way the Injector drives it (frame begins, one camera update per eye, both copies, submit)
    // and returns each eye's camera FOV.
    void RenderPair(fs25vr::FakeTrackedHeadset& headset, fs25vr::VRBridge& bridge, double (&cameraFovY)[2]) {
        ASSERT_TRUE(headset.manager->BeginFrame());
        for (uint32_t eye = 0; eye < 2; ++eye) {
            cameraFovY[eye] = CameraFovYForEye(bridge, eye);
            if (bridge.GetRenderingMode() == fs25vr::VRRenderingMode::AFR) {
                bridge.AdvanceFrame();
            }
        }
        SubmitPair(headset);
    }
};

class FovOverscanCameraModeTest : public FovOverscanCameraTest, public ::testing::WithParamInterface<fs25vr::VRRenderingMode> {};

TEST_P(FovOverscanCameraModeTest, TheEngineCameraFovEqualsTheSubmittedVerticalFovForBothEyesAtOverscanZeroAndTen) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, kSquarish);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    LoadMod(bridge, GetParam());

    double atZero[2] = {}, atTen[2] = {};
    RenderPair(headset, bridge, atZero);
    ASSERT_EQ(headset.SubmittedViews().size(), 2u);
    for (int eye = 0; eye < 2; ++eye) {
        EXPECT_NEAR(atZero[eye], 2.0 * headset.SubmittedViews()[eye].fov.angleUp, 1e-4) << "overscan 0, eye " << eye;
    }

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setFovOverscan(10)", "=setOverscan"), 0);
    RenderPair(headset, bridge, atTen);
    ASSERT_EQ(headset.SubmittedViews().size(), 2u);
    for (int eye = 0; eye < 2; ++eye) {
        EXPECT_NEAR(atTen[eye], 2.0 * headset.SubmittedViews()[eye].fov.angleUp, 1e-4) << "overscan 10, eye " << eye;
        EXPECT_NEAR(atTen[eye], -2.0 * headset.SubmittedViews()[eye].fov.angleDown, 1e-4) << "overscan 10, eye " << eye;
        EXPECT_NEAR(std::tan(atTen[eye] / 2.0) / std::tan(atZero[eye] / 2.0), 1.10, 1e-4) << "the camera widens by 10 percent, eye " << eye;
    }
}

TEST_P(FovOverscanCameraModeTest, AnOverscanSetBetweenTheEyesCameraUpdatesLeavesThatPairOnItsFovAndTheNextPairUsesTheNewOne) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, kSquarish);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    LoadMod(bridge, GetParam());

    double base[2] = {};
    RenderPair(headset, bridge, base);

    // The first eye of the next pair is rendered, then the player types the command, then the second eye.
    ASSERT_TRUE(headset.manager->BeginFrame());
    double pair[2] = {};
    pair[0] = CameraFovYForEye(bridge, 0);
    if (GetParam() == fs25vr::VRRenderingMode::AFR) bridge.AdvanceFrame();
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setFovOverscan(10)", "=setOverscanMidPair"), 0);
    pair[1] = CameraFovYForEye(bridge, 1);
    if (GetParam() == fs25vr::VRRenderingMode::AFR) bridge.AdvanceFrame();
    SubmitPair(headset);
    ASSERT_EQ(headset.SubmittedViews().size(), 2u);
    for (int eye = 0; eye < 2; ++eye) {
        EXPECT_NEAR(pair[eye], base[eye], 1e-6) << "both eyes of the pair keep the FOV it began with, eye " << eye;
        EXPECT_NEAR(pair[eye], 2.0 * headset.SubmittedViews()[eye].fov.angleUp, 1e-4) << "eye " << eye;
    }

    double next[2] = {};
    RenderPair(headset, bridge, next);
    for (int eye = 0; eye < 2; ++eye) {
        EXPECT_NEAR(std::tan(next[eye] / 2.0) / std::tan(base[eye] / 2.0), 1.10, 1e-4) << "the next pair is widened, eye " << eye;
    }
}

INSTANTIATE_TEST_SUITE_P(StereoPresentationModes, FovOverscanCameraModeTest,
    ::testing::Values(fs25vr::VRRenderingMode::AFR, fs25vr::VRRenderingMode::SequentialDualPass));

TEST_F(FovOverscanCameraTest, In2DFloatingWindowModeTheCameraKeepsTheEnginesOwnFovWhateverTheOverscan) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, kSquarish);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    LoadMod(bridge, fs25vr::VRRenderingMode::AFR);

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setFovOverscan(20); VRCompanion:set2DViewActive(true)", "=enter2D"), 0);
    ASSERT_TRUE(headset.manager->BeginFrame());
    // The mock engine camera's own FOV is getFovY's default, 1.0472.
    EXPECT_NEAR(CameraFovYForEye(bridge, 0), 1.0472, 1e-4);
}

TEST_F(FovOverscanCameraTest, WhileAMenuIsShownTheOverscanDoesNotReachTheCamera) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, kSquarish);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    LoadMod(bridge, fs25vr::VRRenderingMode::AFR);

    // As the Companion Mod's GUI poll shows a menu: its own flag and the bridge's.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.isMenuVisible = true; VRBridge.setMenuVisible(true)", "=showMenu"), 0);
    ASSERT_TRUE(headset.manager->BeginFrame());
    const double withoutOverscan = CameraFovYForEye(bridge, 0);
    ASSERT_GT(withoutOverscan, 0.0);
    SubmitPair(headset);

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setFovOverscan(20)", "=setOverscan"), 0);
    ASSERT_TRUE(headset.manager->BeginFrame());
    EXPECT_NEAR(CameraFovYForEye(bridge, 0), withoutOverscan, 1e-6) << "the menu background is not widened";
    SubmitPair(headset);

    // Closing the menu brings the stereoscopic view back with the overscan still set.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.isMenuVisible = false; VRBridge.setMenuVisible(false)", "=closeMenu"), 0);
    ASSERT_TRUE(headset.manager->BeginFrame());
    EXPECT_NEAR(std::tan(CameraFovYForEye(bridge, 0) / 2.0) / std::tan(withoutOverscan / 2.0), 1.20, 1e-4);
}

// The console command and its VRBridge hand-over, run through a map load and unload as the game does.
class FovOverscanCommandTest : public CompanionModTest {
protected:
    // Engine functions a map load needs, recording the console commands registered and removed.
    static constexpr const char* kMapLoadMocks = R"luau(
        _G.registeredCommands = {}
        _G.removedCommands = {}
        _G.addConsoleCommand = function(cmd, desc, funcName, target)
            _G.registeredCommands[cmd] = { desc = desc, funcName = funcName, target = target }
        end
        _G.removeConsoleCommand = function(cmd) _G.removedCommands[cmd] = true end
        _G.createFolder = function() return true end
        _G.createXMLFile = function() return 1 end
        _G.setXMLFloat = function() end
        _G.setXMLBool = function() end
        _G.setXMLString = function() end
        _G.saveXMLFile = function() return true end
        _G.delete = function() end
        _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    )luau";

    void LoadModAndMap(fs25vr::VRBridge& bridge) {
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kMapLoadMocks, "=mapLoadMocks"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('testMap')", "=loadMap"), 0);
    }
};

TEST_F(FovOverscanCommandTest, TheCommandIsRegisteredOnMapLoadAndRemovedOnMapUnload) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    LoadModAndMap(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local command = _G.registeredCommands["vrFovOverscan"]
        assert(command ~= nil, "vrFovOverscan must be registered on map load")
        assert(command.funcName == "consoleCommandSetFovOverscan", "registered callback: " .. tostring(command.funcName))
        assert(command.target == VRCompanion)
        assert(string.find(command.desc, "vrFovOverscan [0-20]", 1, true) ~= nil, "help names the usage: " .. command.desc)
        VRCompanion:deleteMap()
        assert(_G.removedCommands["vrFovOverscan"] == true, "deleteMap must unregister vrFovOverscan")
    )luau", "=registration"), 0);
}

TEST_F(FovOverscanCommandTest, WithoutAnArgumentItReportsThePercentTheRenderedFovAndTheUsage) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, kSquarish);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    LoadModAndMap(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local report = VRCompanion:consoleCommandSetFovOverscan()
        assert(string.find(report, "FOV Overscan is 0%", 1, true) ~= nil, report)
        -- Rendered at 3072x3216 over the asymmetric fixture: 111.3 degrees vertical, 108.9 horizontal
        assert(string.find(report, "111.3", 1, true) ~= nil, report)
        assert(string.find(report, "108.9", 1, true) ~= nil, report)
        assert(string.find(report, "vrFovOverscan [0-20] (0 by default)", 1, true) ~= nil, report)
        assert(VRBridge.getFovOverscan() == 0, "reporting changes nothing")

        VRCompanion:consoleCommandSetFovOverscan("10")
        report = VRCompanion:consoleCommandSetFovOverscan()
        assert(string.find(report, "FOV Overscan is 10%", 1, true) ~= nil, report)
        assert(string.find(report, "116.3", 1, true) ~= nil, "the report shows the widened FOV: " .. report)
        assert(string.find(report, "113.9", 1, true) ~= nil, report)
    )luau", "=report"), 0);
}

TEST_F(FovOverscanCommandTest, WithoutAHeadsetProjectionTheReportHasThePercentAndTheUsageOnly) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    LoadModAndMap(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local report = VRCompanion:consoleCommandSetFovOverscan()
        assert(string.find(report, "FOV Overscan is 0%", 1, true) ~= nil, report)
        assert(string.find(report, "degrees", 1, true) == nil, "there is no FOV to report: " .. report)
        assert(string.find(report, "vrFovOverscan [0-20] (0 by default)", 1, true) ~= nil, report)
    )luau", "=reportWithoutProjection"), 0);
}

TEST_F(FovOverscanCommandTest, ANumberFromZeroToTwentyIsSetAndAppliedAtOnceWithoutReloadingTheSavegame) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, kSquarish);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    LoadModAndMap(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local base = VRBridge.getEyeProjection(0)
        _G.baseHalfTanY = math.tan(base.fovY / 2)

        local reply = VRCompanion:consoleCommandSetFovOverscan("8")
        assert(string.find(reply, "FOV Overscan set to: 8%", 1, true) ~= nil, reply)
        assert(VRBridge.getFovOverscan() == 8, "the value reaches VRBridge")
        local widened = VRBridge.getEyeProjection(0)
        assert(math.abs(math.tan(widened.fovY / 2) / _G.baseHalfTanY - 1.08) < 1e-4, "applied at once: " .. widened.fovY)

        reply = VRCompanion:consoleCommandSetFovOverscan("7.5")
        assert(string.find(reply, "7.5%", 1, true) ~= nil, reply)
        assert(VRBridge.getFovOverscan() == 7.5, "decimals are allowed")

        VRCompanion:consoleCommandSetFovOverscan("20")
        assert(VRBridge.getFovOverscan() == 20, "20 is the largest value")
        VRCompanion:consoleCommandSetFovOverscan("0")
        assert(VRBridge.getFovOverscan() == 0, "0 is the smallest value")
    )luau", "=setValues"), 0);
}

TEST_F(FovOverscanCommandTest, AnythingElseReturnsTheUsageAndChangesNothing) {
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, kSquarish);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    LoadModAndMap(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:consoleCommandSetFovOverscan("8")
        for _, bad in ipairs({ "21", "-1", "20.5", "abc", "8x", "nan", "inf", "-inf", "1e3", "on", " " }) do
            local reply = VRCompanion:consoleCommandSetFovOverscan(bad)
            assert(string.find(reply, "Usage: vrFovOverscan [0-20] (0 by default)", 1, true) ~= nil,
                "'" .. bad .. "' returns the usage: " .. tostring(reply))
            assert(VRBridge.getFovOverscan() == 8, "'" .. bad .. "' must not change the overscan")
            assert(VRCompanion.fovOverscan == 8, "'" .. bad .. "' must not change the setting")
        end
    )luau", "=refusals"), 0);
}

TEST_F(FovOverscanCommandTest, AMapUnloadPutsVRBridgeBackToZeroAndTheNextMapLoadSendsTheSessionValueAgain) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    LoadModAndMap(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:consoleCommandSetFovOverscan("8")
        assert(VRBridge.getFovOverscan() == 8)
        VRCompanion:deleteMap()
        assert(VRBridge.getFovOverscan() == 0, "a map unload resets VRBridge to 0")

        -- Record the order in which the Companion Mod talks to VRBridge on the next map load
        local calls = {}
        local realHandshake = VRBridge.handshake
        local realSet = VRBridge.setFovOverscan
        VRBridge.handshake = function(...) table.insert(calls, "handshake"); return realHandshake(...) end
        VRBridge.setFovOverscan = function(percent) table.insert(calls, "overscan:" .. tostring(percent)); return realSet(percent) end
        VRCompanion:loadMap("testMap")
        assert(VRBridge.getFovOverscan() == 8, "the next map load sends the value of this session again")
        local handshakeAt, overscanAt = nil, nil
        for i, c in ipairs(calls) do
            if c == "handshake" and handshakeAt == nil then handshakeAt = i end
            if c == "overscan:8" then overscanAt = i end
        end
        assert(handshakeAt ~= nil and overscanAt ~= nil and overscanAt > handshakeAt,
            "the value is sent after the handshake: " .. table.concat(calls, ","))
    )luau", "=mapReload"), 0);
}

TEST_F(FovOverscanCommandTest, TheCommandIsHarmlessWithoutTheNativeBridgeFunctions) {
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRBridge = { version = "1.0.0", handshake = function() return true end }
        local reply = VRCompanion:consoleCommandSetFovOverscan("5")
        assert(type(reply) == "string")
        assert(type(VRCompanion:consoleCommandSetFovOverscan()) == "string")
    )luau", "=withoutBridgeFunctions"), 0);
}

// 3072x3216: the rendered FOV at overscan 0 is 2 * 0.9715 vertical and 2 * 0.95 horizontal (see test_fov_coverage.cpp).
// At 10 percent: tan 0.9715 * 1.1 = 1.46396 * 1.1 = 1.61036, atan = 1.01511, so 116.3 degrees vertical; tan 0.95 * 1.1 =
// 1.39838 * 1.1 = 1.53822, atan = 0.99435, so 113.9 degrees horizontal.
TEST_F(CompanionModTest, TheRenderedFovLineNamesTheOverscanAndIsWrittenWheneverItChanges) {
    LogCapture log;
    fs25vr::FakeTrackedHeadset headset;
    StartHeadset(headset, kSquarish);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    SubmitPair(headset);
    const size_t linesAtZero = log.Count("OpenXrManager: Rendered FOV");
    EXPECT_GE(linesAtZero, 1u);
    EXPECT_NE(log.LastLineWith("OpenXrManager: Rendered FOV").find("Overscan 0.0%"), std::string::npos)
        << log.LastLineWith("OpenXrManager: Rendered FOV");

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setFovOverscan(10)", "=setOverscan"), 0);
    SubmitPair(headset);
    EXPECT_EQ(log.Count("OpenXrManager: Rendered FOV"), linesAtZero + 1) << "the overscan changed the rendered FOV";
    const std::string line = log.LastLineWith("OpenXrManager: Rendered FOV");
    EXPECT_NE(line.find("Overscan 10.0%"), std::string::npos) << line;
    EXPECT_NE(line.find("116.3 x 113.9"), std::string::npos) << line;

    SubmitPair(headset);
    SubmitPair(headset);
    EXPECT_EQ(log.Count("OpenXrManager: Rendered FOV"), linesAtZero + 1) << "an unchanged rendered FOV is not logged again";
}
