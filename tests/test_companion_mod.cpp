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
        MathUtil = {
            getValidLimit = function(angle)
                while angle > math.pi do angle = angle - 2 * math.pi end
                while angle < -math.pi do angle = angle + 2 * math.pi end
                return angle
            end
        }
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

    void RegisterTestSetFrameParity(fs25vr::VRBridge& bridge) {
        lua_getglobal(L, "VRBridge");
        lua_pushlightuserdata(L, &bridge);
        lua_pushcclosure(L, [](lua_State* state) -> int {
            auto* b = static_cast<fs25vr::VRBridge*>(lua_touserdata(state, lua_upvalueindex(1)));
            if (b) {
                b->SetFrameParity(static_cast<uint32_t>(luaL_checkinteger(state, 1)));
            }
            return 0;
        }, "setFrameParity", 1);
        lua_setfield(L, -2, "setFrameParity");
        lua_pop(L, 1);
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
    EXPECT_NE(modDescContent.find("<recenterVrTrackingSpace>true</recenterVrTrackingSpace>"), std::string::npos);
    EXPECT_NE(modDescContent.find("setting_recenterVrTrackingSpace"), std::string::npos);
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
        assert(math.abs(_G.playerCamTransforms.rotation.ry - (0.18 + math.pi)) < 0.001, "yaw mismatch")
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
        assert(math.abs(_G.cameraRotations[777].y - (0.10 + math.pi)) < 0.005, "yaw must be applied")
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
        assert(math.abs(_G.cameraRotations[888].y - (0.25 + math.pi)) < 0.001, "TrackIR yaw applied")
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

TEST_F(CompanionModTest, OnFootMovementDirectionAndCameraLookVectorAreCollinear) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        -- Mock full GIANTS Engine hierarchy:
        -- yawNode (10) -> pitchNode (20) -> cameraRootNode (30) -> firstPersonCamera (40)
        local nodes = {
            [10] = { name = "yawNode", parent = 0, trans = {0, 0, 0}, rot = {0, 0, 0} },
            [20] = { name = "pitchNode", parent = 10, trans = {0, 0, 0}, rot = {0, 0, 0} },
            [30] = { name = "cameraRootNode", parent = 20, trans = {0, 0, 0}, rot = {0, math.pi, 0} },
            [40] = { name = "firstPersonCamera", parent = 30, trans = {0, 0, 0}, rot = {0, 0, 0} },
        }
        local nextNodeId = 100

        function createTransformGroup(name)
            nextNodeId = nextNodeId + 1
            nodes[nextNodeId] = { name = name, parent = 0, trans = {0, 0, 0}, rot = {0, 0, 0} }
            return nextNodeId
        end

        function link(parent, child)
            if nodes[child] then
                nodes[child].parent = parent
            end
        end

        function getParent(node)
            return nodes[node] and nodes[node].parent or 0
        end

        function setTranslation(node, x, y, z)
            if nodes[node] then
                nodes[node].trans = { x, y, z }
            end
        end

        function getTranslation(node)
            local t = nodes[node] and nodes[node].trans or {0, 0, 0}
            return t[1], t[2], t[3]
        end

        function setRotation(node, rx, ry, rz)
            if nodes[node] then
                nodes[node].rot = { rx, ry, rz }
            end
        end

        function getRotation(node)
            local r = nodes[node] and nodes[node].rot or {0, 0, 0}
            return r[1], r[2], r[3]
        end

        -- Transform vector from local space of node to world space (using Y rotation only for 2D walk/yaw)
        function localDirectionToWorld(node, x, y, z)
            local cur = node
            local totalY = 0
            while cur ~= 0 and nodes[cur] do
                totalY = totalY + (nodes[cur].rot[2] or 0)
                cur = nodes[cur].parent
            end
            local cosY = math.cos(totalY)
            local sinY = math.sin(totalY)
            local wx = x * cosY + z * sinY
            local wz = -x * sinY + z * cosY
            return wx, y, wz
        end

        function localRotationToLocal(srcNode, dstNode, rx, ry, rz)
            local srcY = 0
            local cur = srcNode
            while cur ~= 0 and nodes[cur] do
                srcY = srcY + (nodes[cur].rot[2] or 0)
                cur = nodes[cur].parent
            end
            local dstY = 0
            cur = dstNode
            while cur ~= 0 and nodes[cur] do
                dstY = dstY + (nodes[cur].rot[2] or 0)
                cur = nodes[cur].parent
            end
            local diffY = srcY - dstY
            return rx, ry + diffY, rz
        end

        function localToLocal(srcNode, dstNode, x, y, z)
            return x, y, z
        end

        _G.mockPlayerCam = {
            isFirstPerson = true,
            firstPersonCamera = 40,
            cameraRootNode = 30,
            pitchNode = 20,
            yawNode = 10,
            setRotation = function(self, pitch, yaw, roll)
                setRotation(self.pitchNode, pitch or 0, 0, 0)
                setRotation(self.yawNode, 0, yaw or 0, 0)
                setRotation(self.cameraRootNode, 0, math.pi, roll or 0)
            end,
            getRotation = function(self)
                local cameraLookX, cameraLookY, cameraLookZ = localDirectionToWorld(self.cameraRootNode, 0, 0, -1)
                local yaw = math.atan2(cameraLookX, cameraLookZ)
                return 0, yaw, 0
            end
        }

        -- PlayerInputComponent movement logic from vanilla FS25
        _G.mockInput = {
            player = { camera = _G.mockPlayerCam },
            cameraRotationY = 0,
            calculateNormalisedMovementDirection = function(self, moveRight, moveForward)
                if moveRight ~= 0 or moveForward ~= 0 then
                    local len = math.sqrt(moveRight * moveRight + moveForward * moveForward)
                    return moveRight / len, moveForward / len
                end
                return 0, 0
            end,
            calculateNormalisedWorldMovementDirection = function(self, moveRight, moveForward)
                local localDirectionX, localDirectionZ = self:calculateNormalisedMovementDirection(moveRight, moveForward)
                if localDirectionX ~= 0 or localDirectionZ ~= 0 then
                    local wx, _, wz = localDirectionToWorld(self.player.camera.yawNode, -localDirectionX, 0, localDirectionZ)
                    return wx, wz
                end
                return 0, 0
            end
        }
        _G.mockPlayerCam.player = { inputComponent = _G.mockInput }
    )luau", "=setupPlayerHierachyMocks"), 0);

    // Initial pose: head forward (pitch=0, yaw=0, roll=0)
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onPlayerCameraUpdate(_G.mockPlayerCam, 16.6)", "=callPlayerCamUpdate"), 0);

    // Verify camera look vector points along world forward (+Z) and matches W walk vector
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        -- GIANTS Engine camera looks down its local -Z axis
        local camLookX, _, camLookZ = localDirectionToWorld(_G.mockPlayerCam.firstPersonCamera, 0, 0, -1)

        -- Pressing W: moveForward = 1, moveRight = 0
        local walkWX, walkWZ = _G.mockInput:calculateNormalisedWorldMovementDirection(0, 1)

        -- Pressing S: moveForward = -1, moveRight = 0
        local walkSX, walkSZ = _G.mockInput:calculateNormalisedWorldMovementDirection(0, -1)

        -- Pressing D: moveForward = 0, moveRight = 1
        local walkDX, walkDZ = _G.mockInput:calculateNormalisedWorldMovementDirection(1, 0)

        -- Pressing A: moveForward = 0, moveRight = -1
        local walkAX, walkAZ = _G.mockInput:calculateNormalisedWorldMovementDirection(-1, 0)

        -- Dot product of camera look and W walk direction must be +1.0 (collinear, forward)
        local dotW = camLookX * walkWX + camLookZ * walkWZ
        assert(math.abs(dotW - 1.0) < 0.001, "W must walk in the exact forward gaze direction! dotW=" .. tostring(dotW))

        -- S must walk backward (dot = -1.0)
        local dotS = camLookX * walkSX + camLookZ * walkSZ
        assert(math.abs(dotS - (-1.0)) < 0.001, "S must walk in the exact backward direction! dotS=" .. tostring(dotS))

        -- In GIANTS Engine, localDirectionToWorld maps -X to Right and +X to Left.
        -- Camera right vector is (+1, 0, 0) in camera local space.
        local camRightX, _, camRightZ = localDirectionToWorld(_G.mockPlayerCam.firstPersonCamera, 1, 0, 0)
        local dotD = camRightX * walkDX + camRightZ * walkDZ
        assert(math.abs(dotD - 1.0) < 0.001, "D must strafe in the exact camera right direction! dotD=" .. tostring(dotD))

        -- Camera left vector is (-1, 0, 0) in camera local space.
        local camLeftX, _, camLeftZ = localDirectionToWorld(_G.mockPlayerCam.firstPersonCamera, -1, 0, 0)
        local dotA = camLeftX * walkAX + camLeftZ * walkAZ
        assert(math.abs(dotA - 1.0) < 0.001, "A must strafe in the exact camera left direction! dotA=" .. tostring(dotA))
    )luau", "=verifyCollinearMovement"), 0);

    // Now test mouse turning via onPlayerCameraUpdateRotation
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.mockInput.cameraRotationY = 0.5 -- mouse turn right
        VRCompanion:onPlayerCameraUpdateRotation(_G.mockPlayerCam, 16.6)
        local _, yawNodeY, _ = getRotation(_G.mockPlayerCam.yawNode)
        assert(math.abs(yawNodeY - (-0.5)) < 0.001, "yawNode must turn right with mouse input")
    )luau", "=testMouseTurn"), 0);
}

TEST_F(CompanionModTest, DefinesAndCalculatesDefaultAndCustomVRHudInsets) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.DEFAULT_HUD_INSET_X == 0.20, "DEFAULT_HUD_INSET_X must be 0.20")
        assert(VRCompanion.DEFAULT_HUD_INSET_Y == 0.16, "DEFAULT_HUD_INSET_Y must be 0.16")
        assert(VRCompanion.DEFAULT_HUD_UI_SCALE == 0.88, "DEFAULT_HUD_UI_SCALE must be 0.88")

        local left, right, bottom, top, scale = VRCompanion:calculateHudAnchors()
        assert(math.abs(left - 0.20) < 0.0001, "left anchor default must be 0.20")
        assert(math.abs(right - 0.80) < 0.0001, "right anchor default must be 0.80")
        assert(math.abs(bottom - 0.16) < 0.0001, "bottom anchor default must be 0.16")
        assert(math.abs(top - 0.84) < 0.0001, "top anchor default must be 0.84")
        assert(math.abs(scale - 0.88) < 0.0001, "scale multiplier default must be 0.88")

        local cLeft, cRight, cBottom, cTop, cScale = VRCompanion:calculateHudAnchors(0.12, 0.08, 0.95)
        assert(math.abs(cLeft - 0.12) < 0.0001, "custom left anchor must be 0.12")
        assert(math.abs(cRight - 0.88) < 0.0001, "custom right anchor must be 0.88")
        assert(math.abs(cBottom - 0.08) < 0.0001, "custom bottom anchor must be 0.08")
        assert(math.abs(cTop - 0.92) < 0.0001, "custom top anchor must be 0.92")
        assert(math.abs(cScale - 0.95) < 0.0001, "custom scale must be 0.95")
    )luau", "=verifyCalculations"), 0);
}

TEST_F(CompanionModTest, ClampsVRHudInsetsAndScaleWithinSafeBoundaries) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local minL, minR, minB, minT, minS = VRCompanion:calculateHudAnchors(-0.5, -0.5, 0.1)
        assert(math.abs(minL - 0.0) < 0.0001, "negative insetX clamped to 0.0")
        assert(math.abs(minR - 1.0) < 0.0001, "right anchor clamped to 1.0")
        assert(math.abs(minB - 0.0) < 0.0001, "negative insetY clamped to 0.0")
        assert(math.abs(minT - 1.0) < 0.0001, "top anchor clamped to 1.0")
        assert(math.abs(minS - 0.50) < 0.0001, "scale clamped to min 0.50")

        local maxL, maxR, maxB, maxT, maxS = VRCompanion:calculateHudAnchors(0.9, 0.9, 3.0)
        assert(math.abs(maxL - 0.40) < 0.0001, "excessive insetX clamped to 0.40")
        assert(math.abs(maxR - 0.60) < 0.0001, "right anchor clamped to 0.60")
        assert(math.abs(maxB - 0.35) < 0.0001, "excessive insetY clamped to 0.35")
        assert(math.abs(maxT - 0.65) < 0.0001, "top anchor clamped to 0.65")
        assert(math.abs(maxS - 1.50) < 0.0001, "scale clamped to max 1.50")
    )luau", "=verifyClamping"), 0);
}

TEST_F(CompanionModTest, ExposesConfigurationWithGracefulFallbackToDefaults) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:configureHudInsets(0.22, 0.14, 0.92)
        local insets = VRCompanion:getHudInsets()
        assert(math.abs(insets.left - 0.22) < 0.0001, "insets.left must match")
        assert(math.abs(insets.right - 0.78) < 0.0001, "insets.right must match")
        assert(math.abs(insets.bottom - 0.14) < 0.0001, "insets.bottom must match")
        assert(math.abs(insets.top - 0.86) < 0.0001, "insets.top must match")
        assert(math.abs(insets.scaleMultiplier - 0.92) < 0.0001, "insets.scaleMultiplier must match")

        -- Graceful fallback on missing/unspecified XML config
        VRCompanion.hudInsetX = nil
        VRCompanion.hudInsetY = nil
        VRCompanion.hudUiScaleMultiplier = nil
        local fb = VRCompanion:loadConfiguration("missing_file.xml")
        assert(math.abs(fb.left - 0.20) < 0.0001, "fallback left must be default 0.20")
        assert(math.abs(fb.right - 0.80) < 0.0001, "fallback right must be default 0.80")
        assert(math.abs(fb.bottom - 0.16) < 0.0001, "fallback bottom must be default 0.16")
        assert(math.abs(fb.top - 0.84) < 0.0001, "fallback top must be default 0.84")
        assert(math.abs(fb.scaleMultiplier - 0.88) < 0.0001, "fallback scale must be default 0.88")
    )luau", "=verifyConfigApi"), 0);
}

TEST_F(CompanionModTest, AppliesVRHudInsetsAndScaleToStandardDisplaysUponMapLoad) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_hudAnchorLeft = 0.0
        _G.g_hudAnchorRight = 1.0
        _G.g_hudAnchorBottom = 0.0
        _G.g_hudAnchorTop = 1.0

        local displays = {
            speedMeter = { storeScaledValuesCalled = false, setScaleCalled = false, position = nil,
                storeScaledValues = function(self) self.storeScaledValuesCalled = true end,
                setScale = function(self, s) self.setScaleCalled = true self.scale = s end,
                setPosition = function(self, x, y) self.position = { x = x, y = y } end
            },
            inputHelp = { storeScaledValuesCalled = false, position = nil,
                storeScaledValues = function(self) self.storeScaledValuesCalled = true end,
                setPosition = function(self, x, y) self.position = { x = x, y = y } end
            },
            gameInfo = { storeScaledValuesCalled = false, position = nil,
                storeScaledValues = function(self) self.storeScaledValuesCalled = true end,
                setPosition = function(self, x, y) self.position = { x = x, y = y } end
            },
            contextAction = { storeScaledValuesCalled = false, position = nil,
                storeScaledValues = function(self) self.storeScaledValuesCalled = true end,
                setPosition = function(self, x, y) self.position = { x = x, y = y } end
            },
            ingameMap = { storeScaledValuesCalled = false, position = nil,
                storeScaledValues = function(self) self.storeScaledValuesCalled = true end,
                setPosition = function(self, x, y) self.position = { x = x, y = y } end
            }
        }
        _G.g_currentMission = { hud = displays }

        VRCompanion:configureHudInsets()
        VRCompanion:loadMap('testMap')

        -- Global anchors contracted
        assert(math.abs(_G.g_hudAnchorLeft - 0.20) < 0.0001, "g_hudAnchorLeft must be 0.20")
        assert(math.abs(_G.g_hudAnchorRight - 0.80) < 0.0001, "g_hudAnchorRight must be 0.80")
        assert(math.abs(_G.g_hudAnchorBottom - 0.16) < 0.0001, "g_hudAnchorBottom must be 0.16")
        assert(math.abs(_G.g_hudAnchorTop - 0.84) < 0.0001, "g_hudAnchorTop must be 0.84")

        -- UI scale multiplier applied (0.88x)
        assert(math.abs(VRCompanion.effectiveUiScale - 0.88) < 0.0001, "effectiveUiScale must be 0.88")
        assert(displays.speedMeter.setScaleCalled == true, "SpeedMeterDisplay.setScale must be called")
        assert(displays.speedMeter.scale == 0.88, "SpeedMeterDisplay scale must be 0.88")

        -- All standard displays repositioned
        assert(displays.speedMeter.storeScaledValuesCalled == true, "speedMeter storeScaledValues called")
        assert(displays.inputHelp.storeScaledValuesCalled == true, "inputHelp storeScaledValues called")
        assert(displays.gameInfo.storeScaledValuesCalled == true, "gameInfo storeScaledValues called")
        assert(displays.contextAction.storeScaledValuesCalled == true, "contextAction storeScaledValues called")
        assert(displays.ingameMap.storeScaledValuesCalled == true, "ingameMap storeScaledValues called")

        -- Display positioning coordinates
        assert(displays.speedMeter.position.x == 0.80 and displays.speedMeter.position.y == 0.16, "speedMeter bottom-right")
        assert(displays.inputHelp.position.x == 0.20 and displays.inputHelp.position.y == 0.84, "inputHelp top-left")
        assert(displays.gameInfo.position.x == 0.80 and displays.gameInfo.position.y == 0.84, "gameInfo top-right")
        assert(displays.contextAction.position.x == 0.20 and displays.contextAction.position.y == 0.16, "contextAction bottom-left")
        assert(displays.ingameMap.position.x == 0.20 and displays.ingameMap.position.y == 0.16, "ingameMap bottom-left")
    )luau", "=verifyDisplaysUponMapLoad"), 0);
}

TEST_F(CompanionModTest, RestoresStandard2DHudCornerAnchorsOnMapUnload) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_hudAnchorLeft = 0.0
        _G.g_hudAnchorRight = 1.0
        _G.g_hudAnchorBottom = 0.0
        _G.g_hudAnchorTop = 1.0
        _G.g_currentMission = { hud = {} }

        VRCompanion:loadMap('testMap')
        assert(math.abs(_G.g_hudAnchorLeft - 0.20) < 0.0001, "g_hudAnchorLeft must be 0.20 when loaded")

        VRCompanion:deleteMap()
        assert(math.abs(_G.g_hudAnchorLeft - 0.0) < 0.0001, "g_hudAnchorLeft restored to 0.0")
        assert(math.abs(_G.g_hudAnchorRight - 1.0) < 0.0001, "g_hudAnchorRight restored to 1.0")
        assert(math.abs(_G.g_hudAnchorBottom - 0.0) < 0.0001, "g_hudAnchorBottom restored to 0.0")
        assert(math.abs(_G.g_hudAnchorTop - 1.0) < 0.0001, "g_hudAnchorTop restored to 1.0")
    )luau", "=verifyRestoreOnDeleteMap"), 0);
}

TEST_F(CompanionModTest, MetadataDescriptorDeclaresHudToggleActionAndBindings) {
    std::string modDescContent = ReadFile("mods/FS25_VRCompanion/modDesc.xml");
    ASSERT_FALSE(modDescContent.empty());

    EXPECT_NE(modDescContent.find("<action name=\"VR_HUD_TOGGLE\""), std::string::npos);
    EXPECT_NE(modDescContent.find("<actionBinding action=\"VR_HUD_TOGGLE\""), std::string::npos);
    EXPECT_NE(modDescContent.find("<text name=\"input_VR_HUD_TOGGLE\">"), std::string::npos);
}

TEST_F(CompanionModTest, HudStateMachineDefinesThreeStageStatesAndCyclesCyclically) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.HUD_STATE_HIDDEN == 0, "HUD_STATE_HIDDEN must be 0")
        assert(VRCompanion.HUD_STATE_STREAMLINED == 1, "HUD_STATE_STREAMLINED must be 1")
        assert(VRCompanion.HUD_STATE_FULL == 2, "HUD_STATE_FULL must be 2")

        VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
        assert(VRCompanion:getHudState() == 0, "Initial state must be 0")

        -- 0 -> 1
        local s1 = VRCompanion:cycleHudState()
        assert(s1 == 1, "0 -> 1")
        assert(VRCompanion:getHudState() == 1, "State is 1")

        -- 1 -> 2
        local s2 = VRCompanion:cycleHudState()
        assert(s2 == 2, "1 -> 2")
        assert(VRCompanion:getHudState() == 2, "State is 2")

        -- 2 -> 0
        local s3 = VRCompanion:cycleHudState()
        assert(s3 == 0, "2 -> 0")
        assert(VRCompanion:getHudState() == 0, "State is 0")

        -- Clamp check
        VRCompanion:setHudState(-10)
        assert(VRCompanion:getHudState() == 0, "Negative clamped to 0")
        VRCompanion:setHudState(10)
        assert(VRCompanion:getHudState() == 2, "Overflow clamped to 2")

        -- onHudToggleAction
        VRCompanion:setHudState(0)
        assert(VRCompanion:onHudToggleAction("VR_HUD_TOGGLE", 1) == 1, "Action advances to 1")
    )luau", "=verifyHudStateMachine"), 0);
}

TEST_F(CompanionModTest, AppliesHudStatesCorrectlyAcrossDisplays) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local hud = {
            isVisible = true,
            setIsVisible = function(self, vis) self.isVisible = vis end,
            speedMeter = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            inputHelp = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            gameInfo = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            contextAction = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            ingameMap = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end }
        }
        _G.g_currentMission = { hud = hud }

        -- Hidden (0)
        VRCompanion:applyHudState(VRCompanion.HUD_STATE_HIDDEN)
        assert(hud.isVisible == false, "HUD must be hidden in state 0")
        assert(hud.speedMeter.isVisible == false, "speedMeter must be hidden")
        assert(hud.gameInfo.isVisible == false, "gameInfo must be hidden")

        -- Streamlined (1)
        VRCompanion:applyHudState(VRCompanion.HUD_STATE_STREAMLINED)
        assert(hud.isVisible == true, "HUD must be visible in state 1")
        assert(hud.speedMeter.isVisible == true, "speedMeter must be visible")
        assert(hud.contextAction.isVisible == true, "contextAction must be visible")
        assert(hud.inputHelp.isVisible == false, "inputHelp must be hidden in Streamlined")
        assert(hud.ingameMap.isVisible == false, "ingameMap must be hidden in Streamlined")
        assert(hud.gameInfo.isVisible == false, "gameInfo must be hidden in Streamlined")

        -- Full (2)
        VRCompanion:applyHudState(VRCompanion.HUD_STATE_FULL)
        assert(hud.isVisible == true, "HUD must be visible in state 2")
        assert(hud.speedMeter.isVisible == true, "speedMeter visible in Full")
        assert(hud.inputHelp.isVisible == true, "inputHelp visible in Full")
        assert(hud.gameInfo.isVisible == true, "gameInfo visible in Full")
        assert(hud.ingameMap.isVisible == true, "ingameMap visible in Full")
    )luau", "=verifyApplyHudStates"), 0);
}

TEST_F(CompanionModTest, IsDisplayVisibleFiltersComponentsAcrossAllThreeStates) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local displays = {
            "SpeedMeterDisplay",
            "ContextActionDisplay",
            "FillLevelsDisplay",
            "IngameMap",
            "InputHelpDisplay",
            "GameInfoDisplay"
        }

        -- 1. Hidden (0)
        for _, d in ipairs(displays) do
            assert(VRCompanion:isDisplayVisible(d, VRCompanion.HUD_STATE_HIDDEN) == false,
                d .. " must be false in Hidden")
        end

        -- 2. Streamlined (1)
        assert(VRCompanion:isDisplayVisible("SpeedMeterDisplay", VRCompanion.HUD_STATE_STREAMLINED) == true, "SpeedMeterDisplay visible")
        assert(VRCompanion:isDisplayVisible("ContextActionDisplay", VRCompanion.HUD_STATE_STREAMLINED) == true, "ContextActionDisplay visible")
        assert(VRCompanion:isDisplayVisible("FillLevelsDisplay", VRCompanion.HUD_STATE_STREAMLINED) == true, "FillLevelsDisplay visible")

        assert(VRCompanion:isDisplayVisible("IngameMap", VRCompanion.HUD_STATE_STREAMLINED) == false, "IngameMap suppressed")
        assert(VRCompanion:isDisplayVisible("InputHelpDisplay", VRCompanion.HUD_STATE_STREAMLINED) == false, "InputHelpDisplay suppressed")
        assert(VRCompanion:isDisplayVisible("GameInfoDisplay", VRCompanion.HUD_STATE_STREAMLINED) == false, "GameInfoDisplay suppressed")

        -- 3. Full (2)
        for _, d in ipairs(displays) do
            assert(VRCompanion:isDisplayVisible(d, VRCompanion.HUD_STATE_FULL) == true,
                d .. " must be true in Full")
        end

        -- 4. Omitted state defaults to active state
        VRCompanion:setHudState(VRCompanion.HUD_STATE_STREAMLINED)
        assert(VRCompanion:isDisplayVisible("SpeedMeterDisplay") == true)
        assert(VRCompanion:isDisplayVisible("IngameMap") == false)

        VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
        assert(VRCompanion:isDisplayVisible("SpeedMeterDisplay") == false)
    )luau", "=verifyIsDisplayVisible"), 0);
}

TEST_F(CompanionModTest, HooksNativeEngineHudDisplaysAndSuppressesFilteredComponentsOnDraw) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local draws = {
            HUD = 0,
            SpeedMeterDisplay = 0,
            ContextActionDisplay = 0,
            FillLevelsDisplay = 0,
            IngameMap = 0,
            InputHelpDisplay = 0,
            GameInfoDisplay = 0
        }

        _G.HUD = {
            draw = function(self) draws.HUD = draws.HUD + 1 end,
            setInputHelpVisible = function(self, vis) self.inputHelpVisible = vis end,
            setIngameMapVisible = function(self, vis) self.ingameMapVisible = vis end
        }
        _G.SpeedMeterDisplay = {
            draw = function(self) draws.SpeedMeterDisplay = draws.SpeedMeterDisplay + 1 end,
            getIsVisible = function(self) return true end
        }
        _G.ContextActionDisplay = {
            draw = function(self) draws.ContextActionDisplay = draws.ContextActionDisplay + 1 end,
            getIsVisible = function(self) return true end
        }
        _G.FillLevelsDisplay = {
            draw = function(self) draws.FillLevelsDisplay = draws.FillLevelsDisplay + 1 end,
            getIsVisible = function(self) return true end
        }
        _G.IngameMap = {
            draw = function(self) draws.IngameMap = draws.IngameMap + 1 end,
            getIsVisible = function(self) return true end
        }
        _G.InputHelpDisplay = {
            draw = function(self) draws.InputHelpDisplay = draws.InputHelpDisplay + 1 end,
            getIsVisible = function(self) return true end
        }
        _G.GameInfoDisplay = {
            draw = function(self) draws.GameInfoDisplay = draws.GameInfoDisplay + 1 end,
            getIsVisible = function(self) return true end
        }

        VRCompanion:hookHud()
        assert(VRCompanion.hudHooked == true, "hudHooked must be true")

        -- Streamlined (1)
        VRCompanion:setHudState(VRCompanion.HUD_STATE_STREAMLINED)
        draws = { HUD = 0, SpeedMeterDisplay = 0, ContextActionDisplay = 0, FillLevelsDisplay = 0, IngameMap = 0, InputHelpDisplay = 0, GameInfoDisplay = 0 }

        _G.SpeedMeterDisplay:draw()
        _G.ContextActionDisplay:draw()
        _G.FillLevelsDisplay:draw()
        _G.InputHelpDisplay:draw()
        _G.IngameMap:draw()
        _G.GameInfoDisplay:draw()
        _G.HUD:draw()

        assert(draws.SpeedMeterDisplay == 1, "SpeedMeterDisplay:draw must run")
        assert(draws.ContextActionDisplay == 1, "ContextActionDisplay:draw must run")
        assert(draws.FillLevelsDisplay == 1, "FillLevelsDisplay:draw must run")
        assert(draws.InputHelpDisplay == 0, "InputHelpDisplay:draw suppressed")
        assert(draws.IngameMap == 0, "IngameMap:draw suppressed")
        assert(draws.GameInfoDisplay == 0, "GameInfoDisplay:draw suppressed")
        assert(draws.HUD == 1, "HUD:draw runs")

        assert(_G.SpeedMeterDisplay:getIsVisible() == true)
        assert(_G.InputHelpDisplay:getIsVisible() == false)
        assert(_G.IngameMap:getIsVisible() == false)
        assert(_G.GameInfoDisplay:getIsVisible() == false)

        -- Hidden (0)
        VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
        draws = { HUD = 0, SpeedMeterDisplay = 0, ContextActionDisplay = 0, FillLevelsDisplay = 0, IngameMap = 0, InputHelpDisplay = 0, GameInfoDisplay = 0 }

        _G.SpeedMeterDisplay:draw()
        _G.ContextActionDisplay:draw()
        _G.FillLevelsDisplay:draw()
        _G.InputHelpDisplay:draw()
        _G.IngameMap:draw()
        _G.GameInfoDisplay:draw()
        _G.HUD:draw()

        assert(draws.SpeedMeterDisplay == 0, "SpeedMeter suppressed in Hidden")
        assert(draws.ContextActionDisplay == 0, "ContextAction suppressed in Hidden")
        assert(draws.InputHelpDisplay == 0, "InputHelp suppressed in Hidden")
        assert(draws.IngameMap == 0, "IngameMap suppressed in Hidden")
        assert(draws.GameInfoDisplay == 0, "GameInfo suppressed in Hidden")
        assert(draws.HUD == 0, "HUD suppressed in Hidden")

        -- Full (2)
        VRCompanion:setHudState(VRCompanion.HUD_STATE_FULL)
        draws = { HUD = 0, SpeedMeterDisplay = 0, ContextActionDisplay = 0, FillLevelsDisplay = 0, IngameMap = 0, InputHelpDisplay = 0, GameInfoDisplay = 0 }

        _G.SpeedMeterDisplay:draw()
        _G.ContextActionDisplay:draw()
        _G.FillLevelsDisplay:draw()
        _G.InputHelpDisplay:draw()
        _G.IngameMap:draw()
        _G.GameInfoDisplay:draw()
        _G.HUD:draw()

        assert(draws.SpeedMeterDisplay == 1, "SpeedMeter draws in Full")
        assert(draws.InputHelpDisplay == 1, "InputHelp draws in Full")
        assert(draws.IngameMap == 1, "IngameMap draws in Full")
        assert(draws.GameInfoDisplay == 1, "GameInfo draws in Full")
        assert(draws.HUD == 1, "HUD draws in Full")

        VRCompanion:unhookHud()
        assert(VRCompanion.hudHooked == false)
    )luau", "=verifyHookHudDisplays"), 0);
}

TEST_F(CompanionModTest, UnhookHudRestoresOriginalFunctionsCleanly) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local origCalled = false
        local orig = function(self) origCalled = true end
        _G.InputHelpDisplay = { draw = orig }

        VRCompanion:hookHud()
        assert(VRCompanion.hudHooked == true)
        assert(_G.InputHelpDisplay.draw ~= orig)

        VRCompanion:unhookHud()
        assert(VRCompanion.hudHooked == false)
        assert(_G.InputHelpDisplay.draw == orig)

        _G.InputHelpDisplay:draw()
        assert(origCalled == true)
    )luau", "=verifyUnhookHud"), 0);
}

TEST_F(CompanionModTest, LoadMapDefaultsToHiddenAndRegistersHudToggleAction) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local registeredActions = {}
        _G.g_inputBinding = {
            registerActionEvent = function(self, actionName, target, callback, triggerUp, triggerDown, triggerAlways, startActive)
                table.insert(registeredActions, {
                    actionName = actionName,
                    target = target,
                    callback = callback
                })
                return true, #registeredActions
            end,
            setActionEventTextPriority = function(self, eventId, priority) end
        }

        local mockHud = {
            isVisible = true,
            setIsVisible = function(self, vis) self.isVisible = vis end,
            speedMeter = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            inputHelp = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            gameInfo = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            contextAction = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            ingameMap = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end }
        }
        _G.g_currentMission = { hud = mockHud }

        VRCompanion:loadMap('mapUS')
        assert(VRCompanion:getHudState() == 0, "Defaults to Hidden (0) on loadMap")
        assert(mockHud.isVisible == false, "mockHud.isVisible must be false on loadMap")
        assert(VRCompanion.hudHooked == true, "hudHooked must be true on loadMap")

        local foundToggleAction = false
        for _, reg in ipairs(registeredActions) do
            if reg.actionName == "VR_HUD_TOGGLE" then
                foundToggleAction = true
            end
        end
        assert(foundToggleAction, "VR_HUD_TOGGLE action registered")

        VRCompanion:deleteMap()
        assert(mockHud.isVisible == true, "mockHud.isVisible restored on deleteMap")
        assert(VRCompanion.hudHooked == false, "hudHooked restored to false on deleteMap")
    )luau", "=verifyLoadMapAndLifecycle"), 0);
}

TEST_F(CompanionModTest, GuiMenuTransitionsPreserveUserSelectedHudState) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local mockHud = {
            isVisible = true,
            setIsVisible = function(self, vis) self.isVisible = vis end,
            speedMeter = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            inputHelp = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            gameInfo = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            contextAction = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
            ingameMap = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end }
        }
        _G.g_currentMission = { hud = mockHud }

        local guiVisible = false
        _G.g_gui = {
            getIsGuiVisible = function() return guiVisible end
        }

        VRCompanion:loadMap('mapUS')

        -- User chooses Streamlined (1)
        VRCompanion:setHudState(VRCompanion.HUD_STATE_STREAMLINED)
        assert(mockHud.inputHelp.isVisible == false, "inputHelp hidden")

        -- Enter menu
        guiVisible = true
        VRCompanion:update(16)
        assert(VRCompanion.isMenuVisible == true, "In menu")
        assert(VRCompanion:getHudState() == 1, "hudState preserved as 1")

        -- Engine GUI pollutes HUD
        mockHud.inputHelp.isVisible = true

        -- Exit menu
        guiVisible = false
        VRCompanion:update(16)
        assert(VRCompanion.isMenuVisible == false, "Exited menu")
        assert(VRCompanion:getHudState() == 1, "hudState still 1")
        assert(mockHud.inputHelp.isVisible == false, "inputHelp re-suppressed by applyHudState")

        VRCompanion:deleteMap()
    )luau", "=verifyGuiPreservation"), 0);
}

TEST_F(CompanionModTest, ComputesPhysicalIpdScaledDisparityAtTargetConvergenceDepth) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        -- 1. Default IPD (0.064m) at target depth (~1.8m) yields 0.012 disparity
        local dNominal = VRCompanion:computeConvergenceDisparity(0.064, 1.8)
        assert(math.abs(dNominal - 0.012) < 0.0001, "0.064 IPD at 1.8m must yield 0.012")

        -- 2. Safe fallback when IPD is nil, 0, or negative
        local dNil = VRCompanion:computeConvergenceDisparity(nil, 1.8)
        assert(math.abs(dNil - 0.012) < 0.0001, "Nil IPD must fall back to 0.012")

        local dZero = VRCompanion:computeConvergenceDisparity(0.0, 1.8)
        assert(math.abs(dZero - 0.012) < 0.0001, "Zero IPD must fall back to 0.012")

        -- 3. Linear scaling with IPD
        local d70 = VRCompanion:computeConvergenceDisparity(0.070, 1.8)
        assert(math.abs(d70 - (0.012 * (0.070 / 0.064))) < 0.0001, "70mm IPD scales linearly")

        local d58 = VRCompanion:computeConvergenceDisparity(0.058, 1.8)
        assert(math.abs(d58 - (0.012 * (0.058 / 0.064))) < 0.0001, "58mm IPD scales linearly")

        -- 4. Inverse scaling with virtual depth
        local dNear = VRCompanion:computeConvergenceDisparity(0.064, 0.9)
        assert(math.abs(dNear - 0.024) < 0.0001, "0.9m depth doubles disparity")

        local dFar = VRCompanion:computeConvergenceDisparity(0.064, 3.6)
        assert(math.abs(dFar - 0.006) < 0.0001, "3.6m depth halves disparity")

        -- 5. Safe boundary clamping
        local dClampedMin = VRCompanion:computeConvergenceDisparity(0.010, 1.8)
        assert(math.abs(dClampedMin - (0.012 * (0.040 / 0.064))) < 0.0001, "Clamped to MIN_IPD 0.040")

        local dClampedMax = VRCompanion:computeConvergenceDisparity(0.150, 1.8)
        assert(math.abs(dClampedMax - (0.012 * (0.085 / 0.064))) < 0.0001, "Clamped to MAX_IPD 0.085")

        local dClampedNear = VRCompanion:computeConvergenceDisparity(0.085, 0.1)
        assert(dClampedNear <= VRCompanion.MAX_CONVERGENCE_DISPARITY, "Clamped to MAX_CONVERGENCE_DISPARITY 0.040")
    )luau", "=verifyIpdConvergenceMath"), 0);
}

TEST_F(CompanionModTest, AlternatesPositiveDisparityOnLeftParityAndNegativeOnRightParity) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    RegisterTestSetFrameParity(bridge);

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    // Set simulated IPD on native OpenXR manager: 64mm
    oxr->SetIpd(0.064f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:configureHudInsets(0.20, 0.16, 0.88)
        VRCompanion.handshakeComplete = true
        VRCompanion.passThroughMode = false
        VRCompanion.headTrackingEnabled = true

        -- Parity 0: Left eye -> positive offset (+0.012)
        VRBridge.setFrameParity(0)
        local left0, right0, off0 = VRCompanion:updateHudConvergence()
        assert(math.abs(off0 - 0.012) < 0.0001, "Parity 0 offset must be +0.012")
        assert(math.abs(left0 - 0.212) < 0.0001, "Left anchor must be 0.20 + 0.012 = 0.212")
        assert(math.abs(right0 - 0.812) < 0.0001, "Right anchor must be 0.80 + 0.012 = 0.812")
        assert(math.abs(_G.g_hudAnchorLeft - 0.212) < 0.0001, "g_hudAnchorLeft must be 0.212")
        assert(math.abs(_G.g_hudAnchorRight - 0.812) < 0.0001, "g_hudAnchorRight must be 0.812")

        -- Parity 1: Right eye -> negative offset (-0.012)
        VRBridge.setFrameParity(1)
        local left1, right1, off1 = VRCompanion:updateHudConvergence()
        assert(math.abs(off1 - (-0.012)) < 0.0001, "Parity 1 offset must be -0.012")
        assert(math.abs(left1 - 0.188) < 0.0001, "Left anchor must be 0.20 - 0.012 = 0.188")
        assert(math.abs(right1 - 0.788) < 0.0001, "Right anchor must be 0.80 - 0.012 = 0.788")
        assert(math.abs(_G.g_hudAnchorLeft - 0.188) < 0.0001, "g_hudAnchorLeft must be 0.188")
        assert(math.abs(_G.g_hudAnchorRight - 0.788) < 0.0001, "g_hudAnchorRight must be 0.788")
    )luau", "=verifyParityDirectionality"), 0);
}

TEST_F(CompanionModTest, UpdatesConvergenceEveryFrameWithoutCoordinateDriftAcrossTenThousandCycles) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    RegisterTestSetFrameParity(bridge);

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    oxr->SetIpd(0.064f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:configureHudInsets(0.20, 0.16, 0.88)
        VRCompanion.handshakeComplete = true
        VRCompanion.passThroughMode = false
        VRCompanion.headTrackingEnabled = true

        -- Run 10,000 frames toggling parity
        for f = 1, 10000 do
            VRBridge.setFrameParity(f % 2)
            VRCompanion:update(16)
        end

        -- Verify base anchors have zero drift (< 1e-7)
        assert(math.abs(VRCompanion.hudAnchorLeftBase - 0.20) < 0.0000001, "Zero drift on left base anchor")
        assert(math.abs(VRCompanion.hudAnchorRightBase - 0.80) < 0.0000001, "Zero drift on right base anchor")

        -- Final frame parity was 10000 % 2 == 0 (parity 0)
        assert(math.abs(VRCompanion.hudAnchorLeft - 0.212) < 0.0000001, "Left anchor matches exact mathematical disparity")
        assert(math.abs(VRCompanion.hudAnchorRight - 0.812) < 0.0000001, "Right anchor matches exact mathematical disparity")
    )luau", "=verifyTenThousandCyclesZeroDrift"), 0);
}

TEST_F(CompanionModTest, RevertsConvergenceOffsetSmoothlyToZeroInPassThroughOrDisabledTracking) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    RegisterTestSetFrameParity(bridge);

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    oxr->SetIpd(0.064f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:configureHudInsets(0.20, 0.16, 0.88)
        VRCompanion.handshakeComplete = true
        VRCompanion.passThroughMode = false
        VRCompanion.headTrackingEnabled = true

        -- 1. Initialize active disparity
        VRBridge.setFrameParity(0)
        VRCompanion:updateHudConvergence()
        assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.012) < 0.0001)

        -- 2. Activate Pass-Through mode -> smooth reversion to zero
        VRCompanion:setPassThroughMode(true)
        assert(VRCompanion:isPassThroughMode() == true)

        -- Frame 1: decay starts smoothly
        VRBridge.setFrameParity(1)
        VRCompanion:update(16)
        local d1 = VRCompanion.currentConvergenceDisparity
        assert(d1 < 0.012 and d1 > 0.0, "Smooth decay on frame 1")

        -- Complete decay to zero
        for f = 1, 35 do
            VRBridge.setFrameParity(f % 2)
            VRCompanion:update(16)
        end
        assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.0) < 0.0001, "Disparity smoothly reaches 0 in Pass-Through")
        assert(math.abs(VRCompanion.currentConvergenceOffset - 0.0) < 0.0001, "Offset reaches 0 in Pass-Through")
        assert(math.abs(VRCompanion.hudAnchorLeft - 0.20) < 0.0001, "Left anchor at base 0.20 in Pass-Through")

        -- 3. Deactivate Pass-Through mode -> smooth recovery
        VRCompanion:setPassThroughMode(false)
        for f = 1, 35 do
            VRBridge.setFrameParity(f % 2)
            VRCompanion:update(16)
        end
        assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.012) < 0.0001, "Disparity smoothly recovers to 0.012")

        -- 4. Disable head tracking -> smooth reversion to zero
        VRCompanion:setHeadTrackingEnabled(false)
        assert(VRCompanion:isHeadTrackingActive() == false)
        for f = 1, 35 do
            VRBridge.setFrameParity(f % 2)
            VRCompanion:update(16)
        end
        assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.0) < 0.0001, "Disparity smoothly reaches 0 when tracking disabled")
        assert(math.abs(VRCompanion.currentConvergenceOffset - 0.0) < 0.0001, "Offset reaches 0 when tracking disabled")
    )luau", "=verifySmoothReversion"), 0);
}

TEST_F(CompanionModTest, RecenterModeDefaultsToBothWithVrTrackingSpaceEnabled) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE == true, "Default recenterVrTrackingSpace constant must be true")
        assert(VRCompanion.recenterVrTrackingSpace == true, "VRCompanion.recenterVrTrackingSpace must default to true")
        assert(VRCompanion:getRecenterMode() == "Both", "Default getRecenterMode must return Both")
    )luau", "=verifyRecenterModeDefaults"), 0);
}

TEST_F(CompanionModTest, RecenterActionInBothModeZeroesCameraYawAndCallsBridgeRecenter) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.4f, 0.1f, -0.2f);
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local mockInsideCam = {
            isInside = true,
            cameraNode = 100,
            origRotY = 0.0,
            rotY = 1.25,
            origRotX = 0.0,
            rotX = 0.1,
            origRotZ = 0.0,
            rotZ = 0.0
        }
        _G.g_currentMission = {
            controlledVehicle = {
                getActiveCamera = function(self) return mockInsideCam end
            }
        }
        _G.testMockCam = mockInsideCam

        VRCompanion.recenterVrTrackingSpace = true
        VRCompanion:onRecenterAction()

        -- Vehicle camera look yaw must be reset to origRotY
        assert(math.abs(_G.testMockCam.rotY - 0.0) < 0.0001, "rotY must be reset to origRotY in Both mode")

        -- VRBridge tracking space must be recentered (head pose zeroed)
        local pose = VRBridge.getHeadPose()
        assert(math.abs(pose.x) < 0.001, "tracking x must be zeroed by bridge.recenter() in Both mode")
    )luau", "=verifyBothRecenter"), 0);
}

TEST_F(CompanionModTest, RecenterActionInVehicleOnlyModeZeroesCameraYawAndBypassesBridgeRecenter) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.4f, 0.1f, -0.2f);
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local mockInsideCam = {
            isInside = true,
            cameraNode = 100,
            origRotY = 0.0,
            rotY = 1.25,
            origRotX = 0.0,
            rotX = 0.1,
            origRotZ = 0.0,
            rotZ = 0.0
        }
        _G.g_currentMission = {
            controlledVehicle = {
                getActiveCamera = function(self) return mockInsideCam end
            }
        }
        _G.testMockCam = mockInsideCam

        -- Set mode to VehicleOnly
        VRCompanion.recenterVrTrackingSpace = false
        VRCompanion:onRecenterAction()

        -- Vehicle camera look yaw must be reset to origRotY
        assert(math.abs(_G.testMockCam.rotY - 0.0) < 0.0001, "rotY must be reset to origRotY in VehicleOnly mode")

        -- VRBridge tracking space must NOT be recentered (pose.x remains at simPose 0.4)
        local pose = VRBridge.getHeadPose()
        assert(math.abs(pose.x - 0.4) < 0.001, "tracking x must remain untouched (0.4) when bridge.recenter() is bypassed")
    )luau", "=verifyVehicleOnlyRecenter"), 0);
}

TEST_F(CompanionModTest, LoadsRecenterVrTrackingSpaceFromXml) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local mockXmlStore = {
            ["vrSettings.recenterVrTrackingSpace"] = false
        }
        _G.loadXMLFile = function(schema, path)
            return 1234
        end
        _G.getXMLFloat = function(xml, key)
            return nil
        end
        _G.getXMLBool = function(xml, key)
            return mockXmlStore[key]
        end
        _G.getXMLString = function(xml, key)
            if mockXmlStore[key] ~= nil then
                return tostring(mockXmlStore[key])
            end
            return nil
        end
        _G.delete = function(xml) end

        -- Load XML with false
        VRCompanion:loadConfiguration("settings.xml")
        assert(VRCompanion.recenterVrTrackingSpace == false, "Must load recenterVrTrackingSpace = false")
        assert(VRCompanion:getRecenterMode() == "VehicleOnly", "Mode must be VehicleOnly")

        -- Load XML with true
        mockXmlStore["vrSettings.recenterVrTrackingSpace"] = true
        VRCompanion:loadConfiguration("settings.xml")
        assert(VRCompanion.recenterVrTrackingSpace == true, "Must load recenterVrTrackingSpace = true")
        assert(VRCompanion:getRecenterMode() == "Both", "Mode must be Both")
    )luau", "=verifyXmlLoad"), 0);
}

TEST_F(CompanionModTest, SavesRecenterVrTrackingSpaceToXml) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local savedBools = {}
        _G.createXMLFile = function(schema, path, root)
            return 5678
        end
        _G.setXMLFloat = function(xml, key, val) end
        _G.setXMLBool = function(xml, key, val)
            savedBools[key] = val
        end
        _G.saveXMLFile = function(xml) return true end
        _G.delete = function(xml) end

        -- Save VehicleOnly
        VRCompanion.recenterVrTrackingSpace = false
        VRCompanion:saveConfiguration("dummy.xml")
        assert(savedBools["vrSettings.recenterVrTrackingSpace"] == false, "Must save recenterVrTrackingSpace = false")

        -- Save Both
        VRCompanion.recenterVrTrackingSpace = true
        VRCompanion:saveConfiguration("dummy.xml")
        assert(savedBools["vrSettings.recenterVrTrackingSpace"] == true, "Must save recenterVrTrackingSpace = true")
    )luau", "=verifyXmlSave"), 0);
}

TEST_F(CompanionModTest, ConsoleCommandRecenterModeInspectsTogglesAndPersistsMode) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local savedXml = false
        VRCompanion.saveConfiguration = function(self)
            savedXml = true
            return true
        end

        -- 1. Inspect without args
        local msg = VRCompanion:consoleCommandSetRecenterMode()
        assert(string.find(msg, "Both") ~= nil, "Inspect should report Both by default")

        -- 2. Switch to VehicleOnly
        savedXml = false
        local msgVeh = VRCompanion:consoleCommandSetRecenterMode("vehicle")
        assert(string.find(msgVeh, "VehicleOnly") ~= nil, "Should report VehicleOnly")
        assert(VRCompanion.recenterVrTrackingSpace == false, "Property should be false")
        assert(VRCompanion:getRecenterMode() == "VehicleOnly", "Mode should be VehicleOnly")
        assert(savedXml == true, "Should trigger saveConfiguration")

        -- 3. Toggle back to Both
        savedXml = false
        local msgTog1 = VRCompanion:consoleCommandSetRecenterMode("toggle")
        assert(string.find(msgTog1, "Both") ~= nil, "Toggle should switch to Both")
        assert(VRCompanion.recenterVrTrackingSpace == true, "Property should be true")
        assert(VRCompanion:getRecenterMode() == "Both", "Mode should be Both")
        assert(savedXml == true, "Should trigger saveConfiguration")

        -- 4. Toggle back to VehicleOnly
        local msgTog2 = VRCompanion:consoleCommandSetRecenterMode("toggle")
        assert(string.find(msgTog2, "VehicleOnly") ~= nil, "Toggle should switch to VehicleOnly")
        assert(VRCompanion.recenterVrTrackingSpace == false, "Property should be false")

        -- 5. Explicitly set to Both
        local msgBoth = VRCompanion:consoleCommandSetRecenterMode("both")
        assert(string.find(msgBoth, "Both") ~= nil, "Should report Both")
        assert(VRCompanion.recenterVrTrackingSpace == true, "Property should be true")

        -- 6. Invalid arg displays usage
        local msgErr = VRCompanion:consoleCommandSetRecenterMode("invalidArg")
        assert(string.find(msgErr, "Usage") ~= nil, "Invalid arg should return Usage instructions")
    )luau", "=verifyConsoleCommands"), 0);
}

TEST_F(CompanionModTest, RegistersAndUnregistersRecenterModeConsoleCommands) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local registeredCommands = {}
        local unregisteredCommands = {}

        _G.addConsoleCommand = function(cmd, desc, fnName, target)
            registeredCommands[cmd] = { desc = desc, fnName = fnName, target = target }
        end
        _G.removeConsoleCommand = function(cmd)
            unregisteredCommands[cmd] = true
        end

        VRCompanion:loadMap("mapUS")

        assert(registeredCommands["vrRecenterMode"] ~= nil, "vrRecenterMode must be registered")
        assert(registeredCommands["vrRecenterMode"].fnName == "consoleCommandSetRecenterMode", "vrRecenterMode handler mismatch")
        assert(registeredCommands["vrRecenterTracking"] ~= nil, "vrRecenterTracking must be registered")
        assert(registeredCommands["vrRecenterTracking"].fnName == "consoleCommandSetRecenterMode", "vrRecenterTracking handler mismatch")

        VRCompanion:deleteMap()

        assert(unregisteredCommands["vrRecenterMode"] == true, "vrRecenterMode must be unregistered on deleteMap")
        assert(unregisteredCommands["vrRecenterTracking"] == true, "vrRecenterTracking must be unregistered on deleteMap")
    )luau", "=verifyCommandRegistration"), 0);
}

TEST_F(CompanionModTest, SnapTurnDefaultsAndConfigurableTurnAngle) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.DEFAULT_SNAP_TURN_ANGLE ~= nil, "DEFAULT_SNAP_TURN_ANGLE must be defined")
        assert(math.abs(VRCompanion.DEFAULT_SNAP_TURN_ANGLE - (math.pi * 0.5)) < 0.0001, "Default angle must be pi/2 rad")
        assert(math.abs(VRCompanion:getSnapTurnAngle() - (math.pi * 0.5)) < 0.0001, "getSnapTurnAngle must return pi/2")
        assert(VRCompanion.DEFAULT_SNAP_TURN_DEBOUNCE_MS >= 250, "Debounce cooldown must be at least 250ms")

        VRCompanion:setSnapTurnAngle(math.pi * 0.25)
        assert(math.abs(VRCompanion:getSnapTurnAngle() - (math.pi * 0.25)) < 0.0001, "Configured angle to pi/4")

        VRCompanion:setSnapTurnAngle(90)
        assert(math.abs(VRCompanion:getSnapTurnAngle() - (math.pi * 0.5)) < 0.0001, "Setting 90 degrees must convert to pi/2")
    )luau", "=verifySnapTurnDefaults"), 0);
}

TEST_F(CompanionModTest, SnapTurnDebounceCooldownEnforcesMinimum250ms) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:resetSnapTurnDebounce()

        local mockCam = {
            isInside = true,
            cameraNode = 101,
            origRotY = 0.0,
            rotY = 0.0
        }
        _G.g_currentMission = {
            controlledVehicle = {
                getActiveCamera = function() return mockCam end
            }
        }

        -- First snap turn succeeds
        local ok1 = VRCompanion:snapTurnRight(nil, 1000)
        assert(ok1 == true, "First snap turn at t=1000 must succeed")
        assert(math.abs(mockCam.rotY - (-math.pi * 0.5)) < 0.0001, "Camera rotY must be -pi/2")

        -- Snap turn 100ms later (< 250ms) is debounced
        local ok2, err2 = VRCompanion:snapTurnRight(nil, 1100)
        assert(ok2 == false, "Snap turn 100ms later must be debounced")
        assert(err2 == "debounced", "Error reason must be debounced")
        assert(math.abs(mockCam.rotY - (-math.pi * 0.5)) < 0.0001, "Camera angle must not change")

        -- Snap turn 250ms later (t=1250) succeeds
        local ok3 = VRCompanion:snapTurnRight(nil, 1250)
        assert(ok3 == true, "Snap turn at t=1250 must succeed")
        assert(math.abs(math.abs(mockCam.rotY) - math.pi) < 0.0001, "Camera rotY must advance to pi")
    )luau", "=verifySnapTurnDebounce"), 0);
}

TEST_F(CompanionModTest, InCabSnapTurnAdjustsCameraRotYAndUpdatesHeadTrackingNode) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:resetSnapTurnDebounce()

        local nodeRotations = {}
        _G.setRotation = function(node, rx, ry, rz)
            nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
        end
        _G.getRotation = function(node)
            local r = nodeRotations[node]
            if r then return r.rx, r.ry, r.rz end
            return 0, 0, 0
        end

        local testCam = {
            isInside = true,
            cameraNode = 301,
            headTrackingNode = 302,
            origHeadTrackingRotX = 0,
            origHeadTrackingRotY = math.pi,
            origHeadTrackingRotZ = 0,
            origRotY = math.pi,
            rotY = math.pi
        }
        nodeRotations[302] = { rx = 0, ry = math.pi, rz = 0 }

        local veh = {
            getActiveCamera = function() return testCam end
        }
        _G.g_currentMission = { controlledVehicle = veh }
        VRCompanion:onEnterVehicle(veh, true, nil)

        -- Snap turn right (-pi/2)
        local okR = VRCompanion:snapTurnRight(nil, 1000)
        assert(okR == true, "snapTurnRight must succeed")
        assert(math.abs(testCam.rotY - (math.pi * 0.5)) < 0.0001, "cam.rotY must be +pi/2")
        assert(math.abs(nodeRotations[302].ry - (math.pi * 0.5)) < 0.0001, "headTrackingNode yaw must be +pi/2")

        -- Snap turn left (+pi/2) after 300ms
        local okL = VRCompanion:snapTurnLeft(nil, 1300)
        assert(okL == true, "snapTurnLeft must succeed")
        assert(math.abs(math.abs(testCam.rotY) - math.pi) < 0.0001, "cam.rotY must be pi")
        assert(math.abs(math.abs(nodeRotations[302].ry) - math.pi) < 0.0001, "headTrackingNode yaw must be pi")
    )luau", "=verifyInCabSnapTurn"), 0);
}

TEST_F(CompanionModTest, RecenterResetsSnappedCameraRotYToNeutralForwardWindshield) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:resetSnapTurnDebounce()

        local nodeRotations = {}
        _G.setRotation = function(node, rx, ry, rz)
            nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
        end
        _G.getRotation = function(node)
            local r = nodeRotations[node]
            if r then return r.rx, r.ry, r.rz end
            return 0, 0, 0
        end

        local testCam = {
            isInside = true,
            cameraNode = 401,
            headTrackingNode = 402,
            origHeadTrackingRotX = 0,
            origHeadTrackingRotY = math.pi,
            origHeadTrackingRotZ = 0,
            origRotY = math.pi,
            rotY = math.pi
        }
        nodeRotations[402] = { rx = 0, ry = math.pi, rz = 0 }

        _G.g_currentMission = {
            controlledVehicle = {
                getActiveCamera = function() return testCam end
            }
        }

        -- Snap turn right twice -> facing backward (rotY = 0)
        VRCompanion:snapTurnRight(nil, 1000)
        VRCompanion:snapTurnRight(nil, 1300)
        assert(math.abs(testCam.rotY - 0.0) < 0.0001, "cam.rotY is 0.0")

        -- Trigger Recenter
        VRCompanion:onRecenterAction()

        -- cam.rotY must be reset to origRotY (math.pi)
        assert(math.abs(testCam.rotY - math.pi) < 0.0001, "cam.rotY reset to origRotY")
        assert(math.abs(nodeRotations[402].ry - math.pi) < 0.001, "headTrackingNode reset to origHeadTrackingRotY")
    )luau", "=verifyRecenterReset"), 0);
}

TEST_F(CompanionModTest, KeyEventHardwareShortcutsTriggerSnapTurnsAndRejectsHeldKeyRepeat) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:resetSnapTurnDebounce()

        local mockCam = {
            isInside = true,
            cameraNode = 501,
            origRotY = 0.0,
            rotY = 0.0
        }
        _G.g_currentMission = {
            controlledVehicle = {
                getActiveCamera = function() return mockCam end
            }
        }
        _G.g_time = 1000

        -- Numpad 4 (Left, sym 260 / unicode 52)
        VRCompanion:keyEvent(52, 260, 0, true)
        assert(math.abs(mockCam.rotY - (math.pi * 0.5)) < 0.0001, "Numpad 4 snaps left")

        -- Held key repeat at t = 1350 is rejected
        _G.g_time = 1350
        VRCompanion:keyEvent(52, 260, 0, true)
        assert(math.abs(mockCam.rotY - (math.pi * 0.5)) < 0.0001, "Held repeat rejected")

        -- Release key
        VRCompanion:keyEvent(52, 260, 0, false)

        -- Press Numpad 6 (Right, sym 262 / unicode 54) at t = 1400
        _G.g_time = 1400
        VRCompanion:keyEvent(54, 262, 0, true)
        assert(math.abs(mockCam.rotY - 0.0) < 0.0001, "Numpad 6 snaps right back to 0")
        VRCompanion:keyEvent(54, 262, 0, false)

        -- Press Numpad 5 (Recenter, sym 261 / unicode 53)
        mockCam.rotY = 1.25
        _G.g_time = 1700
        VRCompanion:keyEvent(53, 261, 0, true)
        assert(math.abs(mockCam.rotY - mockCam.origRotY) < 0.0001, "Numpad 5 resets to origRotY")
    )luau", "=verifyKeyEventShortcuts"), 0);
}

TEST_F(CompanionModTest, OnFootSnapTurnAdjustsPlayerCharacterYawNode) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:resetSnapTurnDebounce()

        local nodeRotations = {}
        _G.setRotation = function(node, rx, ry, rz)
            nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
        end
        _G.getRotation = function(node)
            local r = nodeRotations[node]
            if r then return r.rx, r.ry, r.rz end
            return 0, 0, 0
        end

        local mockPlayerCam = {
            isFirstPerson = true,
            firstPersonCamera = 40,
            cameraRootNode = 30,
            pitchNode = 20,
            yawNode = 10,
            setRotation = function(self, pitch, yaw, roll)
                _G.setRotation(self.pitchNode, pitch or 0, 0, 0)
                _G.setRotation(self.yawNode, 0, yaw or 0, 0)
                _G.setRotation(self.cameraRootNode, 0, math.pi, roll or 0)
            end,
            getRotation = function(self)
                local r = nodeRotations[self.yawNode] or { rx = 0, ry = 0, rz = 0 }
                return 0, r.ry, 0
            end
        }
        nodeRotations[10] = { rx = 0, ry = 0, rz = 0 }

        _G.g_currentMission = {
            controlledVehicle = nil,
            player = {
                camera = mockPlayerCam
            }
        }

        -- 1. Trigger snap turn right (+90 deg -> -pi/2 yawNode)
        local okR, newYawR = VRCompanion:snapTurnRight(nil, 1000)
        assert(okR == true, "On-foot snapTurnRight must succeed")
        assert(math.abs(newYawR - (-math.pi * 0.5)) < 0.0001, "Expected newYawR = -pi/2")
        assert(math.abs(nodeRotations[10].ry - (-math.pi * 0.5)) < 0.0001, "yawNode rotation must equal -pi/2")

        -- 2. Trigger snap turn left (-90 deg -> back to 0.0 yawNode) after 300ms cooldown
        local okL, newYawL = VRCompanion:snapTurnLeft(nil, 1300)
        assert(okL == true, "On-foot snapTurnLeft must succeed after cooldown")
        assert(math.abs(newYawL - 0.0) < 0.0001, "Expected newYawL = 0.0")
        assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001, "yawNode rotation must equal 0.0")

        VRCompanion:resetSnapTurnDebounce()
    )luau", "=verifyOnFootSnapTurnYawNode"), 0);
}

TEST_F(CompanionModTest, OnFootMovementDirectionAndCameraLookRemainCollinearAfterSnapTurnWithoutDrift) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:resetSnapTurnDebounce()

        local nodes = {
            [10] = { name = "yawNode", parent = 0, trans = {0, 0, 0}, rot = {0, 0, 0} },
            [20] = { name = "pitchNode", parent = 10, trans = {0, 0, 0}, rot = {0, 0, 0} },
            [30] = { name = "cameraRootNode", parent = 20, trans = {0, 0, 0}, rot = {0, math.pi, 0} },
            [40] = { name = "firstPersonCamera", parent = 30, trans = {0, 0, 0}, rot = {0, 0, 0} },
        }

        _G.setRotation = function(node, rx, ry, rz)
            if nodes[node] then
                nodes[node].rot = { rx, ry, rz }
            end
        end

        _G.getRotation = function(node)
            local r = nodes[node] and nodes[node].rot or {0, 0, 0}
            return r[1], r[2], r[3]
        end

        _G.localDirectionToWorld = function(node, x, y, z)
            local cur = node
            local totalY = 0
            while cur ~= 0 and nodes[cur] do
                totalY = totalY + (nodes[cur].rot[2] or 0)
                cur = nodes[cur].parent
            end
            local cosY = math.cos(totalY)
            local sinY = math.sin(totalY)
            local wx = x * cosY + z * sinY
            local wz = -x * sinY + z * cosY
            return wx, y, wz
        end

        local mockPlayerCam = {
            isFirstPerson = true,
            firstPersonCamera = 40,
            cameraRootNode = 30,
            pitchNode = 20,
            yawNode = 10,
            setRotation = function(self, pitch, yaw, roll)
                _G.setRotation(self.pitchNode, pitch or 0, 0, 0)
                _G.setRotation(self.yawNode, 0, yaw or 0, 0)
                _G.setRotation(self.cameraRootNode, 0, math.pi, roll or 0)
            end,
            getRotation = function(self)
                local r = nodes[self.yawNode] and nodes[self.yawNode].rot or {0, 0, 0}
                return 0, r[2] or 0, 0
            end
        }

        local mockInput = {
            player = { camera = mockPlayerCam },
            cameraRotationY = 0,
            calculateNormalisedMovementDirection = function(self, moveRight, moveForward)
                if moveRight ~= 0 or moveForward ~= 0 then
                    local len = math.sqrt(moveRight * moveRight + moveForward * moveForward)
                    return moveRight / len, moveForward / len
                end
                return 0, 0
            end,
            calculateNormalisedWorldMovementDirection = function(self, moveRight, moveForward)
                local localDirectionX, localDirectionZ = self:calculateNormalisedMovementDirection(moveRight, moveForward)
                if localDirectionX ~= 0 or localDirectionZ ~= 0 then
                    local wx, _, wz = _G.localDirectionToWorld(self.player.camera.yawNode, -localDirectionX, 0, localDirectionZ)
                    return wx, wz
                end
                return 0, 0
            end
        }
        mockPlayerCam.player = { inputComponent = mockInput }

        _G.g_currentMission = {
            controlledVehicle = nil,
            player = {
                camera = mockPlayerCam
            }
        }

        -- Step 1: Initial state -> facing +Z forward
        local camLookX, _, camLookZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, 0, 0, -1)
        local walkWX, walkWZ = mockInput:calculateNormalisedWorldMovementDirection(0, 1)
        local dotInitial = camLookX * walkWX + camLookZ * walkWZ
        assert(math.abs(dotInitial - 1.0) < 0.001, "Initial gaze and walk collinear forward")

        -- Step 2: Trigger snap turn right (+90 deg -> yawNode = -pi/2)
        local okR, newYawR = VRCompanion:snapTurnRight(nil, 1000)
        assert(okR == true, "snapTurnRight must succeed")
        assert(math.abs(newYawR - (-math.pi * 0.5)) < 0.0001, "newYawR must be -pi/2")
        assert(math.abs(nodes[10].rot[2] - (-math.pi * 0.5)) < 0.0001, "yawNode rot must be -pi/2")

        -- Step 3: Verify walking forward (W) immediately aligns with snapped heading
        local snappedLookX, _, snappedLookZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, 0, 0, -1)
        local snappedWalkWX, snappedWalkWZ = mockInput:calculateNormalisedWorldMovementDirection(0, 1)
        local dotW = snappedLookX * snappedWalkWX + snappedLookZ * snappedWalkWZ
        assert(math.abs(dotW - 1.0) < 0.001, "Snapped gaze and W walk must be collinear")
        assert(math.abs(snappedLookX - (-1.0)) < 0.001, "Snapped gaze must face -X (right)")
        assert(math.abs(snappedWalkWX - (-1.0)) < 0.001, "Snapped W walk must face -X (right)")

        -- Step 4: Verify strafing (A/D) aligns with snapped orientation
        local snappedWalkDX, snappedWalkDZ = mockInput:calculateNormalisedWorldMovementDirection(1, 0)
        local snappedWalkAX, snappedWalkAZ = mockInput:calculateNormalisedWorldMovementDirection(-1, 0)
        local camRightX, _, camRightZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, 1, 0, 0)
        local camLeftX, _, camLeftZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, -1, 0, 0)

        local dotD = camRightX * snappedWalkDX + camRightZ * snappedWalkDZ
        assert(math.abs(dotD - 1.0) < 0.001, "D strafe must match camera right after snap turn")

        local dotA = camLeftX * snappedWalkAX + camLeftZ * snappedWalkAZ
        assert(math.abs(dotA - 1.0) < 0.001, "A strafe must match camera left after snap turn")

        -- Step 5: Verify zero rotational drift across subsequent camera rotation update
        mockInput.cameraRotationY = 0
        VRCompanion:onPlayerCameraUpdateRotation(mockPlayerCam, 16.6)
        local _, updatedYaw, _ = _G.getRotation(mockPlayerCam.yawNode)
        assert(math.abs(updatedYaw - (-math.pi * 0.5)) < 0.001, "yawNode must maintain -pi/2 without drift")

        -- Step 6: Subsequent snap turn left (-90 deg) restores neutral forward heading
        local okL, newYawL = VRCompanion:snapTurnLeft(nil, 1300)
        assert(okL == true, "snapTurnLeft must succeed")
        assert(math.abs(newYawL - 0.0) < 0.0001, "newYawL must be 0.0")

        local restoredLookX, _, restoredLookZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, 0, 0, -1)
        local restoredWalkWX, restoredWalkWZ = mockInput:calculateNormalisedWorldMovementDirection(0, 1)
        local dotRestored = restoredLookX * restoredWalkWX + restoredLookZ * restoredWalkWZ
        assert(math.abs(dotRestored - 1.0) < 0.001, "Restored forward gaze and walk must be collinear")
        assert(math.abs(restoredLookZ - 1.0) < 0.001, "Restored gaze must face +Z forward")

        VRCompanion:resetSnapTurnDebounce()
    )luau", "=verifyCollinearSnapTurn"), 0);
}

TEST_F(CompanionModTest, OnFootSnapTurnSuppressedWhenGuiMenuVisible) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:resetSnapTurnDebounce()

        local nodeRotations = {}
        _G.setRotation = function(node, rx, ry, rz)
            nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
        end
        _G.getRotation = function(node)
            local r = nodeRotations[node]
            if r then return r.rx, r.ry, r.rz end
            return 0, 0, 0
        end

        local mockPlayerCam = {
            isFirstPerson = true,
            firstPersonCamera = 40,
            cameraRootNode = 30,
            pitchNode = 20,
            yawNode = 10,
            setRotation = function(self, pitch, yaw, roll)
                _G.setRotation(self.pitchNode, pitch or 0, 0, 0)
                _G.setRotation(self.yawNode, 0, yaw or 0, 0)
                _G.setRotation(self.cameraRootNode, 0, math.pi, roll or 0)
            end,
            getRotation = function(self)
                local r = nodeRotations[self.yawNode] or { rx = 0, ry = 0, rz = 0 }
                return 0, r.ry, 0
            end
        }
        nodeRotations[10] = { rx = 0, ry = 0, rz = 0 }

        _G.g_currentMission = {
            controlledVehicle = nil,
            player = {
                camera = mockPlayerCam
            }
        }

        -- Set menu visible
        VRCompanion.isMenuVisible = true

        -- Snap turn right must be rejected
        local okR, errR = VRCompanion:snapTurnRight(nil, 1000)
        assert(okR == false, "On-foot snap turn right must be rejected when menu is visible")
        assert(errR == "menu_visible", "Error reason must be menu_visible")
        assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001, "yawNode must not change while menu is open")

        -- Snap turn left must be rejected
        local okL, errL = VRCompanion:snapTurnLeft(nil, 1100)
        assert(okL == false, "On-foot snap turn left must be rejected when menu is visible")
        assert(errL == "menu_visible", "Error reason must be menu_visible")
        assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001, "yawNode must not change while menu is open")

        -- Close menu -> snap turn succeeds
        VRCompanion.isMenuVisible = false
        local okAfter, newYawAfter = VRCompanion:snapTurnRight(nil, 1300)
        assert(okAfter == true, "On-foot snap turn must succeed after menu is closed")
        assert(math.abs(newYawAfter - (-math.pi * 0.5)) < 0.0001, "yawNode must step to -pi/2")
        assert(math.abs(nodeRotations[10].ry - (-math.pi * 0.5)) < 0.0001, "yawNode rotation must equal -pi/2")

        VRCompanion:resetSnapTurnDebounce()
    )luau", "=verifyOnFootMenuGuard"), 0);
}

TEST_F(CompanionModTest, SnapTurnInEngineActionEventsRegistration) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local registeredEvents = {}
        _G.g_inputBinding = {
            actionEvents = {},
            registerActionEvent = function(self, actionName, target, callback, triggerUp, triggerDown, triggerAlways, startActive)
                local id = #self.actionEvents + 1
                local ev = { id = id, actionName = actionName, target = target, callback = callback }
                self.actionEvents[id] = ev
                registeredEvents[actionName] = ev
                return true, id
            end,
            setActionEventTextPriority = function() end,
            setActionEventActive = function() end,
            setActionEventTextVisibility = function() end,
            setActionEventText = function() end
        }

        VRCompanion:registerActionEvents(true)
        assert(registeredEvents["VR_SNAP_TURN_LEFT"] ~= nil, "VR_SNAP_TURN_LEFT must be registered")
        assert(registeredEvents["VR_SNAP_TURN_RIGHT"] ~= nil, "VR_SNAP_TURN_RIGHT must be registered")
        assert(VRCompanion.snapLeftActionEventId ~= nil, "snapLeftActionEventId must be set")
        assert(VRCompanion.snapRightActionEventId ~= nil, "snapRightActionEventId must be set")
    )luau", "=verifySnapTurnActionRegistration"), 0);
}

TEST_F(CompanionModTest, SnapTurnXmlConfigurationLoadingAndSaving) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local savedValues = {}
        _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
        _G.createFolder = function() return true end
        _G.createXMLFile = function() return 1 end
        _G.setXMLFloat = function(xml, key, val) savedValues[key] = val end
        _G.setXMLBool = function() end
        _G.saveXMLFile = function() return true end
        _G.delete = function() end

        -- Save
        VRCompanion:setSnapTurnAngle(math.rad(45.0))
        VRCompanion:saveConfiguration()
        assert(savedValues["vrSettings.snapTurnAngle"] == 45.0, "Must save 45.0 degrees to XML")

        -- Load
        _G.loadXMLFile = function() return 2 end
        _G.getXMLFloat = function(xml, key)
            if key == "vrSettings.snapTurnAngle" then return 90.0 end
            return nil
        end
        _G.getXMLBool = function() return nil end

        VRCompanion:loadConfiguration("mockSettings.xml")
        assert(math.abs(VRCompanion:getSnapTurnAngle() - (math.pi * 0.5)) < 0.0001, "Must load 90.0 degrees (pi/2 rad)")
    )luau", "=verifySnapTurnXmlConfig"), 0);
}

TEST_F(CompanionModTest, SnapTurnConsoleCommand) {
    std::string scriptContent = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(scriptContent.empty());
    ASSERT_EQ(RunLuauChunk(L, scriptContent, "=VRCompanion.lua"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion.snapTurnAngle = math.pi * 0.5

        -- Query
        local query = VRCompanion:consoleCommandSetSnapTurnAngle()
        assert(string.find(query, "90") ~= nil, "Query must return 90 deg")

        -- Set
        local res = VRCompanion:consoleCommandSetSnapTurnAngle("30")
        assert(string.find(res, "30") ~= nil, "Must confirm 30 deg")
        assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(30.0)) < 0.0001, "Angle must be 30 deg in radians")
    )luau", "=verifySnapTurnConsoleCommand"), 0);
}












