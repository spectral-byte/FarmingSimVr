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

    EXPECT_NE(modDescContent.find("<modDesc descVersion=\"105\">"), std::string::npos);
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

    // Verify setStereoRendering(true) was called and redundant setInvertStereoRendering was NOT called
    int checkStatus = RunLuauChunk(L, R"(
        assert(#_G.capturedStereoCalls == 1, "Expected exactly 1 stereo call")
        assert(_G.capturedStereoCalls[1].fn == "setStereoRendering" and _G.capturedStereoCalls[1].val == true)
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
    simPose.position = fs25vr::Vector3(0.15f, 0.05f, -0.10f);
    simPose.rotation = fs25vr::EulerAngles(0.20f, -0.30f, 0.02f);
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetIpd(0.0f);
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
    simPose.position = fs25vr::Vector3(0.04f, -0.03f, 0.02f);
    simPose.rotation = fs25vr::EulerAngles(0.12f, 0.18f, -0.01f);
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetIpd(0.0f);
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
    simPose.position = fs25vr::Vector3(0.5f, 0.2f, -0.3f);
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetIpd(0.0f);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Setup mock scene graph, active vehicle camera, and player camera
    ASSERT_EQ(RunLuauChunk(L, R"(
        _G.vehTrans = {}
        _G.playerTrans = {}
        function setTranslation(node, x, y, z)
            if node == 100 then
                _G.vehTrans = { x = x, y = y, z = z }
            elseif node == 200 then
                _G.playerTrans = { x = x, y = y, z = z }
            end
        end
        function setRotation(node, rx, ry, rz) end
        function getTranslation(node) return 0.0, 1.8, 0.0 end
        function getParent(node) return 0 end

        local mockInsideCam = {
            isInside = true,
            cameraNode = 100,
            origTransX = 0.0,
            origTransY = 1.5,
            origTransZ = 0.5
        }
        local mockPlayerCam = {
            isFirstPerson = true,
            firstPersonCamera = 200
        }
        _G.g_currentMission = {
            controlledVehicle = {
                getActiveCamera = function(self) return mockInsideCam end
            },
            player = {
                camera = mockPlayerCam
            }
        }

        -- Pre-update cameras with non-zero pose to simulate offset before recentering
        VRCompanion:onVehicleCameraUpdate(mockInsideCam, 16.6)
        VRCompanion:onPlayerCameraUpdate(mockPlayerCam, 16.6)
        assert(math.abs(_G.vehTrans.x - 0.5) < 0.001, "pre-recenter veh x must match simPose")
        assert(math.abs(_G.playerTrans.x - 0.5) < 0.001, "pre-recenter player x must match simPose")
    )", "=setupRecenterMocks"), 0);

    // Call recenter action
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onRecenterAction()", "=recenterCall"), 0);

    // Verify translational offsets are zeroed in bridge and camera node positions are actively realigned
    ASSERT_EQ(RunLuauChunk(L, R"(
        local pose = VRBridge.getHeadPose()
        assert(math.abs(pose.x) < 0.001, "recenter must zero x")
        assert(math.abs(pose.y) < 0.001, "recenter must zero y")
        assert(math.abs(pose.z) < 0.001, "recenter must zero z")

        -- Verify camera node positions are actively realigned to driver seat and player character roots
        assert(math.abs(_G.vehTrans.x - 0.0) < 0.001, "realigned vehicle x must be 0")
        assert(math.abs(_G.vehTrans.y - 1.5) < 0.001, "realigned vehicle y must be base seat height 1.5")
        assert(math.abs(_G.vehTrans.z - 0.5) < 0.001, "realigned vehicle z must be base seat depth 0.5")

        assert(math.abs(_G.playerTrans.x - 0.0) < 0.001, "realigned player x must be 0")
        assert(math.abs(_G.playerTrans.y - 1.8) < 0.001, "realigned player y must be base player height 1.8")
        assert(math.abs(_G.playerTrans.z - 0.0) < 0.001, "realigned player z must be 0")
    )", "=checkZeroedPoseAndNodes"), 0);

    // Verify recenter logged
    bool foundRecenterLog = false;
    bool foundVehicleRealignLog = false;
    bool foundPlayerRealignLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("Recenter Action") != std::string::npos) foundRecenterLog = true;
        if (msg.find("Cockpit head pose realigned") != std::string::npos) foundVehicleRealignLog = true;
        if (msg.find("On-foot head pose realigned") != std::string::npos) foundPlayerRealignLog = true;
    }
    EXPECT_TRUE(foundRecenterLog);
    EXPECT_TRUE(foundVehicleRealignLog);
    EXPECT_TRUE(foundPlayerRealignLog);
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
        VRCompanion.allowObliqueProjection = true
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
        VRCompanion.allowObliqueProjection = true
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

TEST_F(CompanionModTest, CentersCameraProjectionByDefaultToPreventCrossEyedDisparity) {
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

    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.mockVehicleCam = {
            isInside = true,
            cameraNode = 301,
            origTransX = 0, origTransY = 1.5, origTransZ = 0
        }
    )luau", "=setupVehicleCamMock"), 0);

    // Run vehicle camera update without allowObliqueProjection (default behavior)
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onVehicleCameraUpdate(_G.mockVehicleCam, 16.6)", "=updateVehicleCam"), 0);

    // Verify projection offset is explicitly 0, 0 to ensure zero disparity shear at infinity
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(_G.cameraProjectionOffsets[301] ~= nil, "setProjectionOffset must be called on vehicle cameraNode")
        assert(math.abs(_G.cameraProjectionOffsets[301].x) < 0.0001, "vehicle horizontal offset must be zero by default")
        assert(math.abs(_G.cameraProjectionOffsets[301].y) < 0.0001, "vehicle vertical offset must be zero by default")
    )luau", "=verifyCenteredProjection"), 0);
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
        VRCompanion.allowObliqueProjection = true
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

TEST_F(CompanionModTest, AlternatesCameraEyeTranslationAndObliqueFrustumsOnSequentialFramesInCab) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    XrFovf rightFov{ -0.80f, 0.90f, 0.85f, -0.85f };
    oxr->SetSimulatedProjection(0, fs25vr::OpenXrManager::ComputeProjectionFromFov(leftFov));
    oxr->SetSimulatedProjection(1, fs25vr::OpenXrManager::ComputeProjectionFromFov(rightFov));

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetIpd(0.064f); // 64mm IPD -> +/- 0.032m
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.cameraTranslations = {}
        function setTranslation(node, x, y, z)
            _G.cameraTranslations[node] = { x = x, y = y, z = z }
        end
        function setRotation(node, rx, ry, rz)
        end

        _G.mockVehicleCam = {
            isInside = true,
            cameraNode = 301,
            origTransX = 0, origTransY = 1.5, origTransZ = 0
        }
        VRCompanion.allowObliqueProjection = true
    )luau", "=setupAFRVehicleMocks"), 0);

    // Frame 0 (even, left eye, parity = 0)
    ASSERT_EQ(bridge.GetFrameParity(), 0u);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onVehicleCameraUpdate(_G.mockVehicleCam, 16.6)", "=updateVehicleCamFrame0"), 0);

    // Check Frame 0: Left Eye projection (negative offset) and negative IPD offset (-0.032m)
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(_G.cameraProjectionOffsets[301] ~= nil, "projection offset must be set on vehicle camera")
        assert(math.abs(_G.cameraProjectionOffsets[301].x - (-0.23052)) < 0.01, "frame 0 should have left eye projection offset")

        assert(_G.cameraTranslations[301] ~= nil, "translation must be set on vehicle camera")
        assert(math.abs(_G.cameraTranslations[301].x - (-0.032)) < 0.001, "frame 0 should have -IPD/2 translation")
        assert(math.abs(_G.cameraTranslations[301].y - 1.5) < 0.001, "y translation should match base height")
        assert(math.abs(_G.cameraTranslations[301].z) < 0.001, "z translation should be zero")
    )luau", "=verifyFrame0InCab"), 0);

    // Advance frame parity to 1 (odd, right eye)
    bridge.AdvanceFrame();
    ASSERT_EQ(bridge.GetFrameParity(), 1u);

    // Frame 1 (odd, right eye, parity = 1)
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onVehicleCameraUpdate(_G.mockVehicleCam, 16.6)", "=updateVehicleCamFrame1"), 0);

    // Check Frame 1: Right Eye projection (positive offset) and positive IPD offset (+0.032m)
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(math.abs(_G.cameraProjectionOffsets[301].x - (0.23052)) < 0.01, "frame 1 should have right eye projection offset")
        assert(math.abs(_G.cameraTranslations[301].x - (0.032)) < 0.001, "frame 1 should have +IPD/2 translation")
        assert(math.abs(_G.cameraTranslations[301].y - 1.5) < 0.001, "y translation should match base height")
        assert(math.abs(_G.cameraTranslations[301].z) < 0.001, "z translation should be zero")
    )luau", "=verifyFrame1InCab"), 0);
}

TEST_F(CompanionModTest, AlternatesCameraEyeTranslationAndObliqueFrustumsOnSequentialFramesOnFoot) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    XrFovf rightFov{ -0.80f, 0.90f, 0.85f, -0.85f };
    oxr->SetSimulatedProjection(0, fs25vr::OpenXrManager::ComputeProjectionFromFov(leftFov));
    oxr->SetSimulatedProjection(1, fs25vr::OpenXrManager::ComputeProjectionFromFov(rightFov));

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetIpd(0.064f);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.cameraTranslations = {}
        function getTranslation(node)
            return 0.0, 1.8, 0.0
        end
        function setTranslation(node, x, y, z)
            _G.cameraTranslations[node] = { x = x, y = y, z = z }
        end
        function setRotation(node, rx, ry, rz)
        end

        _G.mockPlayerCam = {
            isFirstPerson = true,
            firstPersonCamera = 401
        }
        VRCompanion.allowObliqueProjection = true
    )luau", "=setupAFRPlayerMocks"), 0);

    // Frame 0 (even, left eye, parity = 0)
    ASSERT_EQ(bridge.GetFrameParity(), 0u);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onPlayerCameraMakeCurrent(_G.mockPlayerCam)", "=makeCurrentPlayer"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onPlayerCameraUpdate(_G.mockPlayerCam, 16.6)", "=updatePlayerCamFrame0"), 0);

    // Check Frame 0: Left Eye projection and negative IPD offset (-0.032m)
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(_G.cameraProjectionOffsets[401] ~= nil, "projection offset must be set on player camera")
        assert(math.abs(_G.cameraProjectionOffsets[401].x - (-0.23052)) < 0.01, "frame 0 should have left eye projection offset")

        assert(_G.cameraTranslations[401] ~= nil, "translation must be set on player camera")
        assert(math.abs(_G.cameraTranslations[401].x - (-0.032)) < 0.001, "frame 0 should have -IPD/2 translation")
        assert(math.abs(_G.cameraTranslations[401].y - 1.8) < 0.001, "y translation should match base head height")
    )luau", "=verifyFrame0OnFoot"), 0);

    // Advance frame parity to 1 (odd, right eye)
    bridge.AdvanceFrame();
    ASSERT_EQ(bridge.GetFrameParity(), 1u);

    // Frame 1 (odd, right eye, parity = 1)
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onPlayerCameraUpdate(_G.mockPlayerCam, 16.6)", "=updatePlayerCamFrame1"), 0);

    // Check Frame 1: Right Eye projection and positive IPD offset (+0.032m)
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(math.abs(_G.cameraProjectionOffsets[401].x - (0.23052)) < 0.01, "frame 1 should have right eye projection offset")
        assert(math.abs(_G.cameraTranslations[401].x - (0.032)) < 0.001, "frame 1 should have +IPD/2 translation")
        assert(math.abs(_G.cameraTranslations[401].y - 1.8) < 0.001, "y translation should match base head height")
    )luau", "=verifyFrame1OnFoot"), 0);
}

TEST_F(CompanionModTest, SkipsAfrAlternationWhenRequiresAfrFallbackIsFalse) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    XrFovf fov{ -0.90f, 0.90f, 0.85f, -0.85f };
    oxr->SetSimulatedProjection(0, fs25vr::OpenXrManager::ComputeProjectionFromFov(fov));

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    oxr->SetSimulatedPose(simPose);

    auto probe = std::make_shared<fs25vr::GraphicsTelemetryProbe>();
    // Configure probe with native stereo detected
    D3D12_VIEWPORT vps[2]{
        { 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f },
        { 1920.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f }
    };
    D3D12_RECT scissors[2]{
        { 0, 0, 1920, 1080 },
        { 1920, 0, 3840, 1080 }
    };
    probe->RecordViewports(2, vps);
    probe->RecordScissorRects(2, scissors);
    probe->OnFrameBoundary();

    ASSERT_TRUE(probe->HasNativeStereoDetected());
    ASSERT_FALSE(probe->RequiresAfrFallback());

    fs25vr::VRBridge bridge(oxr, probe);
    bridge.SetIpd(0.064f);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.cameraTranslations = {}
        function setTranslation(node, x, y, z)
            _G.cameraTranslations[node] = { x = x, y = y, z = z }
        end
        function setRotation(node, rx, ry, rz)
        end

        _G.mockVehicleCam = {
            isInside = true,
            cameraNode = 301,
            origTransX = 0, origTransY = 1.5, origTransZ = 0
        }
    )luau", "=setupNativeStereoVehicleMocks"), 0);

    // Frame 0: should NOT shift by IPD offset
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onVehicleCameraUpdate(_G.mockVehicleCam, 16.6)", "=updateNativeFrame0"), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(math.abs(_G.cameraTranslations[301].x) < 0.001, "translation x must remain centered when native stereo detected")
    )luau", "=verifyNativeFrame0"), 0);

    // Advance frame parity
    bridge.AdvanceFrame();
    // Frame 1: should still NOT shift by IPD offset
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onVehicleCameraUpdate(_G.mockVehicleCam, 16.6)", "=updateNativeFrame1"), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(math.abs(_G.cameraTranslations[301].x) < 0.001, "translation x must remain centered on next frame when native stereo detected")
    )luau", "=verifyNativeFrame1"), 0);
}

TEST_F(CompanionModTest, HooksPlayerCameraUpdatePositionAndDispatchesOnFoot6DOFTracking) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.1f, 0.2f, 0.3f);
    simPose.rotation = fs25vr::EulerAngles(0.05f, 0.10f, 0.02f);
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.cameraTranslations = {}
        _G.cameraRotations = {}
        function getTranslation(node)
            return 0.0, 1.8, 0.0
        end
        function setTranslation(node, x, y, z)
            _G.cameraTranslations[node] = { x = x, y = y, z = z }
        end
        function setRotation(node, rx, ry, rz)
            _G.cameraRotations[node] = { x = rx, y = ry, z = rz }
        end

        _G.PlayerCamera = {
            baseUpdatePositionCalled = false,
            updatePosition = function(cam, dt)
                cam.baseUpdatePositionCalled = true
            end,
            makeCurrent = function(cam)
                cam.baseMakeCurrentCalled = true
            end
        }

        _G.testPlayerCam = {
            isFirstPerson = true,
            firstPersonCamera = 777
        }
    )luau", "=setupPlayerCameraHookMocks"), 0);

    // Hook PlayerCamera
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:hookPlayerCamera()", "=hookPlayerCam"), 0);
    ASSERT_EQ(RunLuauChunk(L, "assert(VRCompanion.playerCameraHooked == true)", "=assertHooked"), 0);

    // Call makeCurrent then updatePosition via PlayerCamera class
    ASSERT_EQ(RunLuauChunk(L, "PlayerCamera.makeCurrent(_G.testPlayerCam)", "=callMakeCurrent"), 0);
    ASSERT_EQ(RunLuauChunk(L, "PlayerCamera.updatePosition(_G.testPlayerCam, 16.6)", "=callUpdatePosition"), 0);

    // Verify both original method and VR tracking were invoked
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(_G.testPlayerCam.baseMakeCurrentCalled == true, "original makeCurrent must be called")
        assert(_G.testPlayerCam.baseUpdatePositionCalled == true, "original updatePosition must be called")
        assert(_G.cameraTranslations[777] ~= nil, "setTranslation must be called on firstPersonCamera")
        assert(math.abs(_G.cameraTranslations[777].y - (1.8 + 0.2)) < 0.01, "height must include base + pose.y")
        assert(_G.cameraRotations[777] ~= nil, "setRotation must be called on firstPersonCamera")
        assert(math.abs(_G.cameraRotations[777].x - 0.05) < 0.005, "pitch must be applied")
        assert(math.abs(_G.cameraRotations[777].y - 0.10) < 0.005, "yaw must be applied")
    )luau", "=verifyPlayerCameraHookDispatched"), 0);

    // Test deleteMap restores original updatePosition
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMapTest"), 0);
    ASSERT_EQ(RunLuauChunk(L, "assert(VRCompanion.playerCameraHooked == false)", "=assertUnhooked"), 0);
}

TEST_F(CompanionModTest, FallsBackToEngineTrackIRWhenVRBridgePoseUnavailable) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.cameraTranslations = {}
        _G.cameraRotations = {}
        function getTranslation(node)
            return 0.0, 1.75, 0.0
        end
        function setTranslation(node, x, y, z)
            _G.cameraTranslations[node] = { x = x, y = y, z = z }
        end
        function setRotation(node, rx, ry, rz)
            _G.cameraRotations[node] = { x = rx, y = ry, z = rz }
        end

        -- Mock native TrackIR engine functions
        function isHeadTrackingAvailable()
            return true
        end
        function getHeadTrackingTranslation()
            return 0.05, 0.08, -0.02
        end
        function getHeadTrackingRotation()
            return 0.12, 0.25, 0.01
        end

        _G.trackIrPlayerCam = {
            isFirstPerson = true,
            firstPersonCamera = 888
        }
    )luau", "=setupTrackIrMocks"), 0);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onPlayerCameraUpdate(_G.trackIrPlayerCam, 16.6)", "=trackIrUpdate"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(_G.cameraTranslations[888] ~= nil, "translation must be set via TrackIR fallback")
        assert(math.abs(_G.cameraTranslations[888].y - (1.75 + 0.08)) < 0.001, "TrackIR y translation applied")
        assert(_G.cameraRotations[888] ~= nil, "rotation must be set via TrackIR fallback")
        assert(math.abs(_G.cameraRotations[888].x - 0.12) < 0.001, "TrackIR pitch applied")
        assert(math.abs(_G.cameraRotations[888].y - 0.25) < 0.001, "TrackIR yaw applied")
    )luau", "=verifyTrackIrApplied"), 0);
}

TEST_F(CompanionModTest, UnloadMapNotifiesVRBridgeAndRevertsCompanionActiveState) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    EXPECT_FALSE(bridge.IsCompanionActive());

    // Load map: triggers handshake and marks active
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('testMap')", "=loadMapCall"), 0);
    EXPECT_TRUE(bridge.IsCompanionActive());

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.handshakeComplete == true, "companion handshakeComplete must be true")
        assert(VRBridge.isCompanionActive() == true, "VRBridge.isCompanionActive() must be true")
    )luau", "=verifyActive"), 0);

    // Unload map: triggers deleteMap and notifies VRBridge
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMapCall"), 0);
    EXPECT_FALSE(bridge.IsCompanionActive());

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.handshakeComplete == false, "companion handshakeComplete must be false after deleteMap")
        assert(VRBridge.isCompanionActive() == false, "VRBridge.isCompanionActive() must be false after deleteMap")
    )luau", "=verifyInactive"), 0);
}

TEST_F(CompanionModTest, PollsGuiVisibilityDuringUpdateAndNotifiesVRBridge) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Mock g_gui with getIsGuiVisible method
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_gui = {
            _visible = false,
            getIsGuiVisible = function(self)
                return self._visible
            end
        }
    )luau", "=setupGuiMock"), 0);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('testMap')", "=loadMapCall"), 0);
    EXPECT_FALSE(bridge.IsMenuVisible());

    // Update with menu closed
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCall1"), 0);
    EXPECT_FALSE(bridge.IsMenuVisible());

    // Open menu
    ASSERT_EQ(RunLuauChunk(L, "_G.g_gui._visible = true", "=openMenu"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCall2"), 0);

    EXPECT_TRUE(bridge.IsMenuVisible());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.isMenuVisible == true, "VRCompanion.isMenuVisible must be true")
        assert(VRBridge.getIsMenuVisible() == true, "VRBridge.getIsMenuVisible() must be true")
    )luau", "=verifyMenuOpen"), 0);

    bool foundOpenLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("In-game GUI menu opened") != std::string::npos) {
            foundOpenLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundOpenLog);

    // Close menu
    ASSERT_EQ(RunLuauChunk(L, "_G.g_gui._visible = false", "=closeMenu"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCall3"), 0);

    EXPECT_FALSE(bridge.IsMenuVisible());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.isMenuVisible == false, "VRCompanion.isMenuVisible must be false")
        assert(VRBridge.getIsMenuVisible() == false, "VRBridge.getIsMenuVisible() must be false")
    )luau", "=verifyMenuClosed"), 0);

    bool foundCloseLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("In-game GUI menu closed") != std::string::npos) {
            foundCloseLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundCloseLog);
}

TEST_F(CompanionModTest, SuppressesAfrEyeOffsetAndAlternationWhenMenuVisible) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_gui = {
            _visible = true,
            getIsGuiVisible = function(self) return self._visible end
        }
    )luau", "=setupGuiMockOpen"), 0);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('testMap')", "=loadMapCall"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCall"), 0);

    EXPECT_TRUE(bridge.IsMenuVisible());

    // When menu is visible, computeEyeOffset should return 0, 0, 0 regardless of eye index
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local ox, oy, oz = VRCompanion:computeEyeOffset({ qx = 0, qy = 0, qz = 0, qw = 1 }, 1, true)
        assert(ox == 0 and oy == 0 and oz == 0, "computeEyeOffset must return 0, 0, 0 when menu is visible")
    )luau", "=verifyZeroEyeOffset"), 0);
}

TEST_F(CompanionModTest, HandlesMissingOrFailingGuiGracefullyWithoutError) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // 1. g_gui is nil
    ASSERT_EQ(RunLuauChunk(L, "_G.g_gui = nil", "=nilGui"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('testMap')", "=loadMapCall"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCallNil"), 0);
    EXPECT_FALSE(bridge.IsMenuVisible());

    // 2. g_gui:getIsGuiVisible throws an error
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_gui = {
            getIsGuiVisible = function() error("Simulated GUI exception") end
        }
    )luau", "=failingGui"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCallFailing"), 0);
    EXPECT_FALSE(bridge.IsMenuVisible());
}

TEST_F(CompanionModTest, UnloadMapResetsMenuVisibilityState) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_gui = {
            _visible = true,
            getIsGuiVisible = function(self) return self._visible end
        }
    )luau", "=setupGuiMockOpen"), 0);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('testMap')", "=loadMapCall"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCall"), 0);
    EXPECT_TRUE(bridge.IsMenuVisible());

    // Unload map
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMapCall"), 0);
    EXPECT_FALSE(bridge.IsMenuVisible());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.isMenuVisible == false, "VRCompanion.isMenuVisible must be false after deleteMap")
        assert(VRBridge.getIsMenuVisible() == false, "VRBridge.getIsMenuVisible() must be false after deleteMap")
    )luau", "=verifyResetAfterUnload"), 0);
}

TEST_F(CompanionModTest, ActiveGuiAppliesHeadPoseTrackingToActiveCameraNode) {
    auto openXrManager = std::make_shared<fs25vr::OpenXrManager>(nullptr);
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.2f, 0.1f, -0.05f);
    simPose.rotation = fs25vr::EulerAngles(0.05f, 0.15f, 0.0f);
    simPose.isValid = true;
    openXrManager->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Setup showroom / background menu camera mocks
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.activeCamNode = 999
        _G.camTrans = { x = 0.0, y = 2.0, z = -5.0 }
        _G.camRot = { rx = -0.1, ry = 0.0, rz = 0.0 }
        _G.setTransCalls = 0
        _G.setRotCalls = 0

        function getCamera()
            return _G.activeCamNode
        end
        function getTranslation(node)
            return _G.camTrans.x, _G.camTrans.y, _G.camTrans.z
        end
        function setTranslation(node, x, y, z)
            _G.camTrans.x = x
            _G.camTrans.y = y
            _G.camTrans.z = z
            _G.setTransCalls = _G.setTransCalls + 1
        end
        function getRotation(node)
            return _G.camRot.rx, _G.camRot.ry, _G.camRot.rz
        end
        function setRotation(node, rx, ry, rz)
            _G.camRot.rx = rx
            _G.camRot.ry = ry
            _G.camRot.rz = rz
            _G.setRotCalls = _G.setRotCalls + 1
        end

        _G.g_gui = {
            _visible = true,
            getIsGuiVisible = function(self) return self._visible end
        }
    )luau", "=setupShowroomMocks"), 0);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('testMap')", "=loadMapCall"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCall"), 0);

    // Verify camera translation and rotation received head pose offsets
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(_G.setTransCalls >= 1, "setTranslation must be called for active camera node")
        assert(_G.setRotCalls >= 1, "setRotation must be called for active camera node")
        -- Base (0, 2, -5) + Pose (0.2, 0.1, -0.05)
        assert(math.abs(_G.camTrans.x - 0.2) < 0.001, "camTrans.x must include head pose x offset")
        assert(math.abs(_G.camTrans.y - 2.1) < 0.001, "camTrans.y must include head pose y offset")
        assert(math.abs(_G.camTrans.z - (-5.05)) < 0.001, "camTrans.z must include head pose z offset")
        -- BaseRot (-0.1, 0, 0) + PoseRot (0.05, 0.15, 0)
        assert(math.abs(_G.camRot.rx - (-0.05)) < 0.001, "camRot.rx must include head pitch")
        assert(math.abs(_G.camRot.ry - 0.15) < 0.001, "camRot.ry must include head yaw")
        assert(math.abs(_G.camRot.rz - 0.0) < 0.001, "camRot.rz must include head roll")
    )luau", "=verifyCameraTrackingInGui"), 0);
}

TEST_F(CompanionModTest, ActiveGuiRestoresCameraTransformWhenMenuClosed) {
    auto openXrManager = std::make_shared<fs25vr::OpenXrManager>(nullptr);
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.5f, 0.2f, -0.1f);
    simPose.rotation = fs25vr::EulerAngles(0.1f, 0.2f, 0.0f);
    simPose.isValid = true;
    openXrManager->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.activeCamNode = 888
        _G.camTrans = { x = 1.0, y = 3.0, z = -10.0 }
        _G.camRot = { rx = 0.0, ry = 0.5, rz = 0.0 }

        function getCamera() return _G.activeCamNode end
        function getTranslation(node) return _G.camTrans.x, _G.camTrans.y, _G.camTrans.z end
        function setTranslation(node, x, y, z) _G.camTrans.x, _G.camTrans.y, _G.camTrans.z = x, y, z end
        function getRotation(node) return _G.camRot.rx, _G.camRot.ry, _G.camRot.rz end
        function setRotation(node, rx, ry, rz) _G.camRot.rx, _G.camRot.ry, _G.camRot.rz = rx, ry, rz end

        _G.g_gui = {
            _visible = true,
            getIsGuiVisible = function(self) return self._visible end
        }
    )luau", "=setupMocks"), 0);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('testMap')", "=loadMapCall"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCall1"), 0);

    // Verify offset applied
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(math.abs(_G.camTrans.x - 1.5) < 0.001, "offset must be applied during menu")
    )luau", "=verifyOffsetApplied"), 0);

    // Now close the menu
    ASSERT_EQ(RunLuauChunk(L, "_G.g_gui._visible = false", "=closeMenu"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCall2"), 0);

    // Verify original base transform restored
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(math.abs(_G.camTrans.x - 1.0) < 0.001, "camTrans.x must be restored to base 1.0")
        assert(math.abs(_G.camTrans.y - 3.0) < 0.001, "camTrans.y must be restored to base 3.0")
        assert(math.abs(_G.camTrans.z - (-10.0)) < 0.001, "camTrans.z must be restored to base -10.0")
        assert(math.abs(_G.camRot.rx - 0.0) < 0.001, "camRot.rx must be restored to base 0.0")
        assert(math.abs(_G.camRot.ry - 0.5) < 0.001, "camRot.ry must be restored to base 0.5")
        assert(math.abs(_G.camRot.rz - 0.0) < 0.001, "camRot.rz must be restored to base 0.0")
    )luau", "=verifyBaseRestored"), 0);
}

TEST_F(CompanionModTest, ActiveGuiPreserves2DMouseAndControllerInteractions) {
    auto openXrManager = std::make_shared<fs25vr::OpenXrManager>(nullptr);
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.1f, 0.0f, 0.0f);
    simPose.isValid = true;
    openXrManager->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.activeCamNode = 101
        function getCamera() return _G.activeCamNode end
        function getTranslation(node) return 0, 0, 0 end
        function setTranslation(node, x, y, z) end
        function getRotation(node) return 0, 0, 0 end
        function setRotation(node, rx, ry, rz) end

        _G.mouseEventsReceived = 0
        _G.keyEventsReceived = 0
        _G.g_gui = {
            _visible = true,
            getIsGuiVisible = function(self) return self._visible end,
            mouseEvent = function(self, posX, posY, isDown, isUp, button)
                _G.mouseEventsReceived = _G.mouseEventsReceived + 1
            end,
            keyEvent = function(self, unicode, sym, modifier, isDown)
                _G.keyEventsReceived = _G.keyEventsReceived + 1
            end
        }
    )luau", "=setupGuiInputMocks"), 0);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('testMap')", "=loadMapCall"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCall"), 0);

    // Simulate 2D mouse and controller events being dispatched to GUI
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_gui:mouseEvent(0.5, 0.5, true, false, 1)
        _G.g_gui:keyEvent(65, 97, 0, true)
        assert(_G.mouseEventsReceived == 1, "mouse event must be delivered to GUI")
        assert(_G.keyEventsReceived == 1, "key event must be delivered to GUI")
    )luau", "=simulateGuiInput"), 0);
}

TEST_F(CompanionModTest, RecenterActionRealignsMenuQuadWhenInMenu) {
    auto openXrManager = std::make_shared<fs25vr::OpenXrManager>(nullptr);
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(1.0f, 1.5f, 0.0f);
    simPose.isValid = true;
    openXrManager->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_gui = {
            _visible = true,
            getIsGuiVisible = function(self) return self._visible end
        }
    )luau", "=setupGuiMocks"), 0);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('testMap')", "=loadMapCall"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.6)", "=updateCall"), 0);

    // Recenter invoked while menu is displayed
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onRecenterAction()", "=recenterCall"), 0);

    EXPECT_TRUE(openXrManager->HasMenuQuadPose());
    bool foundQuadAlignLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("Menu Quad layer realigned") != std::string::npos ||
            msg.find("Recenter Action complete") != std::string::npos) {
            foundQuadAlignLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundQuadAlignLog);
}




