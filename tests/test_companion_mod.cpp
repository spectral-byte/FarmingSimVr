#include <gtest/gtest.h>
#include "luau/vr_bridge.h"
#include "luacode.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

static std::vector<std::string> s_capturedLogs;

int MockPrint(lua_State* L) {
    int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) line += "\t";
        const char* s = lua_tostring(L, i);
        line += (s ? s : "");
    }
    s_capturedLogs.push_back(line);
    return 0;
}

std::string ReadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        file.open("../" + path);
    }
#ifdef PROJECT_ROOT_DIR
    if (!file.is_open()) {
        file.open(std::string(PROJECT_ROOT_DIR) + "/" + path);
    }
#endif
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int RunLuauChunk(lua_State* L, const std::string& code, const char* chunkName) {
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(code.c_str(), code.length(), nullptr, &bytecodeSize);
    if (!bytecode) return -1;
    int loadResult = luau_load(L, chunkName, bytecode, bytecodeSize, 0);
    free(bytecode);
    if (loadResult != 0) return loadResult;
    return lua_pcall(L, 0, 0, 0);
}

void SetupCameraMockGlobals(lua_State* L) {
    RunLuauChunk(L, R"luau(
        _G.cameraFovY = {}
        _G.cameraProjectionOffsets = {}
        function setFovY(node, fov)
            _G.cameraFovY[node] = fov
        end
        function getFovY(node)
            return _G.cameraFovY[node] or 1.0472
        end
        function setProjectionOffset(node, x, y)
            _G.cameraProjectionOffsets[node] = { x = x, y = y }
        end
        function getProjectionOffset(node)
            local curr = _G.cameraProjectionOffsets[node]
            if curr then return curr.x, curr.y end
            return 0.0, 0.0
        end
    )luau", "=setupCameraMocks");
}

} // anonymous namespace

class CompanionModTest : public ::testing::Test {
protected:
    lua_State* L = nullptr;

    void SetUp() override {
        s_capturedLogs.clear();
        L = luaL_newstate();
        luaL_openlibs(L);

        // Override print with mock to capture logs
        lua_pushcclosure(L, &MockPrint, "print", 0);
        lua_setglobal(L, "print");
    }

    void TearDown() override {
        if (L) {
            lua_close(L);
            L = nullptr;
        }
    }
};

TEST_F(CompanionModTest, MetadataDescriptorIsValid) {
    std::string modDescContent = ReadFile("mods/FS25_VRCompanion/modDesc.xml");
    ASSERT_FALSE(modDescContent.empty());

    EXPECT_NE(modDescContent.find("<modDesc descVersion="), std::string::npos);
    EXPECT_NE(modDescContent.find("<title>"), std::string::npos);
    EXPECT_NE(modDescContent.find("<version>1.0.0.0</version>"), std::string::npos);
    EXPECT_NE(modDescContent.find("<extraSourceFiles>"), std::string::npos);
    EXPECT_NE(modDescContent.find("VRCompanion.lua"), std::string::npos);
}

TEST_F(CompanionModTest, PerformsHandshakeWhenVRBridgePresent) {
    fs25vr::VRBridge bridge;
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());

    int loadStatus = RunLuauChunk(L, scriptContent, "=VRCompanion.lua");
    ASSERT_EQ(loadStatus, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "");

    // Simulate engine triggering loadMap
    int mapStatus = RunLuauChunk(L, "VRCompanion:loadMap('mapUS')", "=loadMapCall");
    EXPECT_EQ(mapStatus, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "");

    // Verify mod table state
    RunLuauChunk(L, "assert(VRCompanion.handshakeComplete == true)", "=assertHandshake");

    // Verify captured logs
    bool foundHandshakeLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("[FS25_VR] Handshake successful! VRBridge detected") != std::string::npos) {
            foundHandshakeLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundHandshakeLog);
}

TEST_F(CompanionModTest, FallsBackGracefullyInPassThroughModeWhenVRBridgeAbsent) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());

    int loadStatus = RunLuauChunk(L, scriptContent, "=VRCompanion.lua");
    ASSERT_EQ(loadStatus, 0);

    // Call loadMap without VRBridge present
    int mapStatus = RunLuauChunk(L, "VRCompanion:loadMap('mapUS')", "=loadMapCall");
    EXPECT_EQ(mapStatus, 0);

    // Verify mod table state
    RunLuauChunk(L, "assert(VRCompanion.handshakeComplete == false)", "=assertNoHandshake");

    bool foundFallbackLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("Running in standard 2D Pass-Through Mode") != std::string::npos) {
            foundFallbackLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundFallbackLog);
}

TEST_F(CompanionModTest, HandlesFailedHandshakeGracefully) {
    // Inject mock VRBridge returning failure
    RunLuauChunk(L, R"(
        VRBridge = {
            version = "1.0.0",
            isReady = function() return false end,
            handshake = function() return { success = false, error = "Failed to start" } end
        }
    )", "=setupFailingVRBridge");

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());

    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('mapUS')", "=loadMapCall"), 0);

    RunLuauChunk(L, "assert(VRCompanion.handshakeComplete == false)", "=assertNoHandshake");

    bool foundFailLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("Handshake failed or rejected") != std::string::npos) {
            foundFailLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundFailLog);
}

TEST_F(CompanionModTest, InvokesEngineStereoRenderingTogglesDuringMapExecution) {
    // Inject mock engine stereo functions
    RunLuauChunk(L, R"(
        _G.capturedStereoCalls = {}
        function setStereoRendering(enable)
            table.insert(_G.capturedStereoCalls, { fn = "setStereoRendering", val = enable })
        end
        function setInvertStereoRendering(invert)
            table.insert(_G.capturedStereoCalls, { fn = "setInvertStereoRendering", val = invert })
        end
    )", "=setupStereoGlobals");

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Call loadMap
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('mapUS')", "=loadMapCall"), 0);

    // Verify setStereoRendering(true) and setInvertStereoRendering(false) were called
    int checkStatus = RunLuauChunk(L, R"(
        assert(#_G.capturedStereoCalls >= 2, "Expected at least 2 stereo calls")
        assert(_G.capturedStereoCalls[1].fn == "setStereoRendering" and _G.capturedStereoCalls[1].val == true)
        assert(_G.capturedStereoCalls[2].fn == "setInvertStereoRendering" and _G.capturedStereoCalls[2].val == false)
    )", "=checkStereoCalls");
    EXPECT_EQ(checkStatus, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "");

    // Verify log message
    bool foundStereoLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("Invoked engine stereo rendering toggle: setStereoRendering(true)") != std::string::npos) {
            foundStereoLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundStereoLog);

    // Call deleteMap and verify setStereoRendering(false)
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMapCall"), 0);
    int checkRestore = RunLuauChunk(L, R"(
        local last = _G.capturedStereoCalls[#_G.capturedStereoCalls]
        assert(last.fn == "setStereoRendering" and last.val == false)
    )", "=checkRestore");
    EXPECT_EQ(checkRestore, 0);

    bool foundRestoreLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("Restored engine stereo rendering toggle: setStereoRendering(false)") != std::string::npos) {
            foundRestoreLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundRestoreLog);
}

TEST_F(CompanionModTest, LogsStereoTelemetryProbeResolutionDuringMapUpdate) {
    fs25vr::VRBridge bridge;
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('mapUS')", "=loadMapCall"), 0);

    // Call update(16.6)
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCall"), 0);

    // Verify telemetry log or status
    bool foundTelemetryLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("Telemetry Probe") != std::string::npos || msg.find("telemetry") != std::string::npos) {
            foundTelemetryLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundTelemetryLog);
}

TEST_F(CompanionModTest, HandlesMissingStereoRenderingTogglesGracefully) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Without setting setStereoRendering, loadMap should not throw
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('mapUS')", "=loadMapCall"), 0);

    bool foundMissingLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("setStereoRendering") != std::string::npos && msg.find("not found") != std::string::npos) {
            foundMissingLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundMissingLog);
}

TEST_F(CompanionModTest, MetadataDescriptorDeclaresRecenterActionAndInputBinding) {
    std::string modDescContent = ReadFile("mods/FS25_VRCompanion/modDesc.xml");
    ASSERT_FALSE(modDescContent.empty());

    EXPECT_NE(modDescContent.find("<action name=\"VR_RECENTER\""), std::string::npos);
    EXPECT_NE(modDescContent.find("<actionBinding action=\"VR_RECENTER\""), std::string::npos);
    EXPECT_NE(modDescContent.find("KEY_numPad5"), std::string::npos);
}

TEST_F(CompanionModTest, InCabVehicleCameraFollowsHeadPoseWithCabReferenceBinding) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::HeadPose simPose;
    simPose.x = 0.15f;
    simPose.y = 0.05f;
    simPose.z = -0.10f;
    simPose.pitch = 0.20f;
    simPose.yaw = -0.30f;
    simPose.roll = 0.02f;
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Setup mock engine scene graph functions
    ASSERT_EQ(RunLuauChunk(L, R"(
        _G.cameraTransforms = {}
        function getParent(node) return 300 end
        function localToLocal(fromNode, toNode, x, y, z)
            return x + 1.0, y + 2.0, z + 3.0
        end
        function localRotationToLocal(fromNode, toNode, rx, ry, rz)
            return rx, ry, rz
        end
        function setTranslation(node, x, y, z)
            _G.cameraTransforms.translation = { node = node, x = x, y = y, z = z }
        end
        function setRotation(node, rx, ry, rz)
            _G.cameraTransforms.rotation = { node = node, rx = rx, ry = ry, rz = rz }
        end

        _G.mockInsideCam = {
            isInside = true,
            cameraNode = 100,
            headTrackingNode = 200,
            cameraPositionNode = 50
        }
        _G.mockOutsideCam = {
            isInside = false,
            cameraNode = 101,
            headTrackingNode = nil,
            cameraPositionNode = 51
        }
    )", "=setupCameraMocks"), 0);

    // Update inside camera
    int updateInside = RunLuauChunk(L, "VRCompanion:onVehicleCameraUpdate(_G.mockInsideCam, 16.6)", "=updateInside");
    EXPECT_EQ(updateInside, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "");

    // Verify inside camera applied 1:1 motion and localToLocal cab binding
    ASSERT_EQ(RunLuauChunk(L, R"(
        assert(_G.cameraTransforms.translation ~= nil, "translation must be set")
        assert(_G.cameraTransforms.translation.node == 100, "must target inside cameraNode")
        -- localToLocal added (1.0, 2.0, 3.0) to (0.15, 0.05, -0.10)
        assert(math.abs(_G.cameraTransforms.translation.x - 1.15) < 0.001, "x transform mismatch")
        assert(math.abs(_G.cameraTransforms.translation.y - 2.05) < 0.001, "y transform mismatch")
        assert(math.abs(_G.cameraTransforms.translation.z - 2.90) < 0.001, "z transform mismatch")

        assert(_G.cameraTransforms.rotation ~= nil, "rotation must be set")
        assert(math.abs(_G.cameraTransforms.rotation.rx - 0.20) < 0.001, "pitch mismatch")
        assert(math.abs(_G.cameraTransforms.rotation.ry - (-0.30)) < 0.001, "yaw mismatch")
        assert(math.abs(_G.cameraTransforms.rotation.rz - 0.02) < 0.001, "roll mismatch")
    )", "=verifyInsideCam"), 0);

    // Clear and test outside camera
    RunLuauChunk(L, "_G.cameraTransforms = {}", "=clearTransforms");
    int updateOutside = RunLuauChunk(L, "VRCompanion:onVehicleCameraUpdate(_G.mockOutsideCam, 16.6)", "=updateOutside");
    EXPECT_EQ(updateOutside, 0);
    // Exterior camera must not have translation or rotation modified
    ASSERT_EQ(RunLuauChunk(L, "assert(_G.cameraTransforms.translation == nil, 'exterior camera must not be modified')", "=verifyOutsideCam"), 0);
}

TEST_F(CompanionModTest, OnFootPlayerCameraAppliesSmooth6DOFTrackingWhileWalking) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::HeadPose simPose;
    simPose.x = 0.04f;
    simPose.y = -0.03f;
    simPose.z = 0.02f;
    simPose.pitch = 0.12f;
    simPose.yaw = 0.18f;
    simPose.roll = -0.01f;
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Setup player camera mock
    ASSERT_EQ(RunLuauChunk(L, R"(
        _G.playerCamTransforms = {}
        function getTranslation(node)
            return 0.0, 1.8, 0.0 -- Base head height in meters
        end
        function setTranslation(node, x, y, z)
            _G.playerCamTransforms.translation = { node = node, x = x, y = y, z = z }
        end
        function setRotation(node, rx, ry, rz)
            _G.playerCamTransforms.rotation = { node = node, rx = rx, ry = ry, rz = rz }
        end

        _G.mockPlayerCam = {
            isFirstPerson = true,
            firstPersonCamera = 500
        }
    )", "=setupPlayerCamMocks"), 0);

    // Initialize base translation
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onPlayerCameraMakeCurrent(_G.mockPlayerCam)", "=makeCurrentCall"), 0);

    // Update player camera (walking / crouching)
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onPlayerCameraUpdate(_G.mockPlayerCam, 16.6)", "=updatePlayerCam"), 0);

    // Verify 6DOF tracking offset applied relative to base height (0, 1.8, 0)
    ASSERT_EQ(RunLuauChunk(L, R"(
        assert(_G.playerCamTransforms.translation ~= nil, "player translation must be set")
        assert(_G.playerCamTransforms.translation.node == 500, "must target firstPersonCamera")
        assert(math.abs(_G.playerCamTransforms.translation.x - 0.04) < 0.001, "x offset mismatch")
        assert(math.abs(_G.playerCamTransforms.translation.y - 1.77) < 0.001, "y (1.8 - 0.03) mismatch")
        assert(math.abs(_G.playerCamTransforms.translation.z - 0.02) < 0.001, "z offset mismatch")

        assert(_G.playerCamTransforms.rotation ~= nil, "player rotation must be set")
        assert(math.abs(_G.playerCamTransforms.rotation.rx - 0.12) < 0.001, "pitch mismatch")
        assert(math.abs(_G.playerCamTransforms.rotation.ry - 0.18) < 0.001, "yaw mismatch")
        assert(math.abs(_G.playerCamTransforms.rotation.rz - (-0.01)) < 0.001, "roll mismatch")
    )", "=verifyPlayerCam"), 0);
}

TEST_F(CompanionModTest, RecenterActionZeroesTranslationalOffsetsAndLogsConfirmation) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::HeadPose simPose;
    simPose.x = 0.5f;
    simPose.y = 0.2f;
    simPose.z = -0.3f;
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Call recenter action
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onRecenterAction()", "=recenterCall"), 0);

    // Verify translational offsets are zeroed
    ASSERT_EQ(RunLuauChunk(L, R"(
        local pose = VRBridge.getHeadPose()
        assert(math.abs(pose.x) < 0.001, "recenter must zero x")
        assert(math.abs(pose.y) < 0.001, "recenter must zero y")
        assert(math.abs(pose.z) < 0.001, "recenter must zero z")
    )", "=checkZeroedPose"), 0);

    // Verify recenter logged
    bool foundRecenterLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("Recenter Action") != std::string::npos) {
            foundRecenterLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundRecenterLog);
}

TEST_F(CompanionModTest, AppliesVerticalFovAndObliqueFrustumOffsetsToVehicleCamera) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    oxr->SetSimulatedProjection(0, fs25vr::OpenXrManager::ComputeProjectionFromFov(leftFov));

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Setup engine camera mocks via shared helper
    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.mockVehicleCam = {
            isInside = true,
            cameraNode = 301,
            origTransX = 0, origTransY = 1.5, origTransZ = 0
        }
    )luau", "=setupVehicleCamMock"), 0);

    // Run vehicle camera update
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onVehicleCameraUpdate(_G.mockVehicleCam, 16.6)", "=updateVehicleCam"), 0);

    // Verify FOV and oblique frustum offsets applied
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(_G.cameraFovY[301] ~= nil, "setFovY must be called on vehicle cameraNode")
        assert(math.abs(_G.cameraFovY[301] - 1.70) < 0.02, "vehicle vertical fov must match VR headset FOV")

        assert(_G.cameraProjectionOffsets[301] ~= nil, "setProjectionOffset must be called on vehicle cameraNode")
        assert(math.abs(_G.cameraProjectionOffsets[301].x - (-0.23052)) < 0.01, "vehicle horizontal offset mismatch")
        assert(math.abs(_G.cameraProjectionOffsets[301].y) < 0.01, "vehicle vertical offset mismatch")
    )luau", "=verifyVehicleCamProjection"), 0);
}

TEST_F(CompanionModTest, AppliesVerticalFovAndObliqueFrustumOffsetsToPlayerCamera) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    oxr->SetSimulatedProjection(0, fs25vr::OpenXrManager::ComputeProjectionFromFov(leftFov));

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Setup player camera mocks via shared helper
    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.mockPlayerCam = {
            isFirstPerson = true,
            firstPersonCamera = 401
        }
    )luau", "=setupPlayerCamMock"), 0);

    // Run player camera update
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onPlayerCameraUpdate(_G.mockPlayerCam, 16.6)", "=updatePlayerCam"), 0);

    // Verify FOV and oblique frustum offsets applied
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(_G.cameraFovY[401] ~= nil, "setFovY must be called on player firstPersonCamera")
        assert(math.abs(_G.cameraFovY[401] - 1.70) < 0.02, "player vertical fov must match VR headset FOV")

        assert(_G.cameraProjectionOffsets[401] ~= nil, "setProjectionOffset must be called on player firstPersonCamera")
        assert(math.abs(_G.cameraProjectionOffsets[401].x - (-0.23052)) < 0.01, "player horizontal offset mismatch")
        assert(math.abs(_G.cameraProjectionOffsets[401].y) < 0.01, "player vertical offset mismatch")
    )luau", "=verifyPlayerCamProjection"), 0);
}

TEST_F(CompanionModTest, RestoresOriginalFovAndProjectionOffsetsOnMapUnload) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    oxr->SetSimulatedProjection(0, fs25vr::OpenXrManager::ComputeProjectionFromFov(leftFov));

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Setup camera mocks via shared helper
    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.mockCam = {
            isInside = true,
            cameraNode = 505,
            origTransX = 0, origTransY = 0, origTransZ = 0
        }
    )luau", "=setupRestoreMocks"), 0);

    // Apply projection via update
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onVehicleCameraUpdate(_G.mockCam, 16.6)", "=updateCam"), 0);

    // Ensure modified
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(math.abs(_G.cameraFovY[505] - 1.70) < 0.02, "must be set to VR fov")
        assert(math.abs(_G.cameraProjectionOffsets[505].x) > 0.05, "must be non-zero offset")
    )luau", "=checkModified"), 0);

    // Unload map
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMapCall"), 0);

    // Verify restored
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(math.abs(_G.cameraFovY[505] - 1.0472) < 0.001, "original FOV must be restored")
        assert(math.abs(_G.cameraProjectionOffsets[505].x) < 0.001, "offset x must be reset to 0")
        assert(math.abs(_G.cameraProjectionOffsets[505].y) < 0.001, "offset y must be reset to 0")
    )luau", "=checkRestored"), 0);
}

