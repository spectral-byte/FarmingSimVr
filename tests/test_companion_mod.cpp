#include <gtest/gtest.h>
#include "luau/vr_bridge.h"
#include "fake_openxr_runtime.h"
#include "luacode.h"
#include "luau_test_runner.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <set>
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

// Loads the shipped module files in modDesc order (exactly as the engine does).
int LoadCompanionModChecked(lua_State* L) {
    std::string err;
    int status = LoadCompanionMod(L, &err);
    if (status != 0) {
        fprintf(stderr, "\n[Companion Mod load error]: %s\n", err.c_str());
    }
    return status;
}
int RunLuauChunk(lua_State* L, const std::string& code, const char* chunkName) {
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(code.c_str(), code.length(), nullptr, &bytecodeSize);
    if (!bytecode) return -1;
    int loadResult = luau_load(L, chunkName, bytecode, bytecodeSize, 0);
    free(bytecode);
    if (loadResult != 0) return loadResult;
    int status = lua_pcall(L, 0, 0, 0);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        if (err) fprintf(stderr, "\n[Luau Error in %s]: %s\n", chunkName, err);
    }
    return status;
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
        if _G.setCamera == nil then
            _G.activeCamera = 0
            function setCamera(node)
                _G.activeCamera = node
            end
        end
        if _G.getCamera == nil then
            function getCamera()
                return _G.activeCamera or 0
            end
        end
        if _G.setAudioListener == nil then
            _G.activeAudioListener = 0
            function setAudioListener(node)
                _G.activeAudioListener = node
            end
        end
        if _G.getAudioListener == nil then
            function getAudioListener()
                return _G.activeAudioListener or 0
            end
        end
        if _G.createCamera == nil then
            _G.cameraCount = 0
            _G.createdCameras = {}
            function createCamera(name, fovY, nearClip, farClip)
                _G.cameraCount = (_G.cameraCount or 0) + 1
                local id = 700 + _G.cameraCount
                _G.createdCameras[id] = { name = name, fovY = fovY, nearClip = nearClip, farClip = farClip }
                _G.cameraFovY[id] = fovY
                if _G.nodes then
                    _G.nodes[id] = { name = name, parent = 0, trans = {0, 0, 0}, rot = {0, 0, 0} }
                end
                return id
            end
        end
        if _G.createTransformGroup == nil then
            _G.transformGroupCount = 0
            function createTransformGroup(name)
                _G.transformGroupCount = (_G.transformGroupCount or 0) + 1
                local id = 800 + _G.transformGroupCount
                if _G.nodes then
                    _G.nodes[id] = { name = name, parent = 0, trans = {0, 0, 0}, rot = {0, 0, 0} }
                end
                return id
            end
        end
        if _G.nodeTranslations == nil then _G.nodeTranslations = {} end
        if _G.setTranslation == nil then
            function setTranslation(node, x, y, z)
                _G.nodeTranslations[node] = { x = x, y = y, z = z, [1] = x, [2] = y, [3] = z }
                if _G.nodes and _G.nodes[node] then
                    _G.nodes[node].trans = { x, y, z }
                end
            end
        end
        if _G.getTranslation == nil then
            function getTranslation(node)
                if _G.nodes and _G.nodes[node] then
                    local t = _G.nodes[node].trans or {0, 0, 0}
                    return t[1], t[2], t[3]
                end
                local t = _G.nodeTranslations[node] or {0, 0, 0}
                return t[1] or t.x or 0, t[2] or t.y or 0, t[3] or t.z or 0
            end
        end
        if _G.nodeRotations == nil then _G.nodeRotations = {} end
        if _G.setRotation == nil then
            function setRotation(node, rx, ry, rz)
                _G.nodeRotations[node] = { rx = rx, ry = ry, rz = rz, [1] = rx, [2] = ry, [3] = rz }
                if _G.nodes and _G.nodes[node] then
                    _G.nodes[node].rot = { rx, ry, rz }
                end
            end
        end
        if _G.getRotation == nil then
            function getRotation(node)
                if _G.nodes and _G.nodes[node] then
                    local r = _G.nodes[node].rot or {0, 0, 0}
                    return r[1], r[2], r[3]
                end
                local r = _G.nodeRotations[node] or {0, 0, 0}
                return r[1] or r.rx or 0, r[2] or r.ry or 0, r[3] or r.rz or 0
            end
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

        // With an active OpenXR session a mode change is queued until the DXGI proxy latches it at a
        // completed frame boundary. These tests have no proxy, so Luau latches it explicitly.
        lua_pushlightuserdata(L, &bridge);
        lua_pushcclosure(L, [](lua_State* state) -> int {
            auto* b = static_cast<fs25vr::VRBridge*>(lua_touserdata(state, lua_upvalueindex(1)));
            if (b) {
                b->LatchPendingRenderingMode();
            }
            return 0;
        }, "latchPendingRenderingMode", 1);
        lua_setfield(L, -2, "latchPendingRenderingMode");
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

TEST_F(CompanionModTest, HarnessLoadOrderMatchesModDescExtraSourceFiles) {
    std::string modDescContent = ReadFile("mods/FS25_VRCompanion/modDesc.xml");
    ASSERT_FALSE(modDescContent.empty());

    std::vector<std::string> listed = ParseModDescSourceFiles(modDescContent);
    ASSERT_FALSE(listed.empty());
    EXPECT_EQ(listed, CompanionModLoadOrder())
        << "Test harness load order diverged from <extraSourceFiles> in modDesc.xml";
}

TEST_F(CompanionModTest, EveryModDescSourceFileExistsAndEntryScriptIsLast) {
    std::vector<std::string> listed = ParseModDescSourceFiles(ReadFile("mods/FS25_VRCompanion/modDesc.xml"));
    ASSERT_FALSE(listed.empty());
    for (const auto& file : listed) {
        EXPECT_FALSE(ReadFile(std::string("mods/FS25_VRCompanion/") + file).empty()) << file;
    }
    EXPECT_EQ(listed.back(), "src/VRCompanion.lua");
}

TEST_F(CompanionModTest, EntryScriptContainsOnlyModRegistration) {
    std::string entry = ReadFile("mods/FS25_VRCompanion/src/VRCompanion.lua");
    ASSERT_FALSE(entry.empty());
    EXPECT_EQ(entry.find("function"), std::string::npos) << "Entry script must not define functions";
    EXPECT_EQ(entry.find("VRCompanion."), std::string::npos) << "Entry script must not define members";
    EXPECT_NE(entry.find("addModEventListener(VRCompanion)"), std::string::npos);
}

TEST_F(CompanionModTest, EveryCompanionModFunctionIsDefinedInExactlyOneFile) {
    std::regex defRe(R"(^\s*(?:local\s+)?function\s+([A-Za-z_][\w\.]*[:\.]?[A-Za-z_]\w*)\s*\(|^\s*(VRCompanion[\w\.]*)\s*=\s*function\s*\()");
    std::map<std::string, std::string> definedIn;
    std::vector<std::string> duplicates;
    for (const auto& file : CompanionModLoadOrder()) {
        std::istringstream lines(ReadFile(std::string("mods/FS25_VRCompanion/") + file));
        std::string line;
        while (std::getline(lines, line)) {
            std::smatch m;
            if (!std::regex_search(line, m, defRe)) continue;
            std::string name = m[1].matched ? m[1].str() : m[2].str();
            std::replace(name.begin(), name.end(), ':', '.');
            if (name.rfind("VRCompanion", 0) != 0) continue;
            auto [it, inserted] = definedIn.emplace(name, file);
            if (!inserted) {
                duplicates.push_back(name + " (" + it->second + " and " + file + ")");
            }
        }
    }
    EXPECT_GT(definedIn.size(), 100u);
    for (const auto& d : duplicates) ADD_FAILURE() << "Defined in more than one file: " << d;
}

TEST_F(CompanionModTest, PackagedModZipContainsModDescIconAndEveryModuleFile) {
    std::string script = ReadFile("scripts/package_release.ps1");
    ASSERT_FALSE(script.empty());
    // Packaging globs src/*.lua; every modDesc-listed file must live directly in src/.
    EXPECT_NE(script.find("src/*.lua"), std::string::npos);
    EXPECT_NE(script.find("modDesc.xml"), std::string::npos);
    EXPECT_NE(script.find("icon_vr.png"), std::string::npos);
    for (const auto& file : ParseModDescSourceFiles(ReadFile("mods/FS25_VRCompanion/modDesc.xml"))) {
        EXPECT_EQ(file.rfind("src/", 0), 0u) << file;
        EXPECT_EQ(file.find('/', 4), std::string::npos) << file;
        EXPECT_EQ(file.substr(file.size() - 4), ".lua") << file;
    }
}

TEST_F(CompanionModTest, ReleaseBundleAndModZipShipTheGplLicense) {
    // GPL-3.0 requires conveying a copy of the license with every distributed copy;
    // the mod zip is also published on its own, so it carries LICENSE too.
    std::string script = ReadFile("scripts/package_release.ps1");
    ASSERT_FALSE(script.empty());
    std::istringstream lines(script);
    std::string line;
    bool modZipShipsLicense = false;
    bool bundleZipShipsLicense = false;
    while (std::getline(lines, line)) {
        if (line.find("tar.exe") == std::string::npos) continue;
        bool hasLicense = line.find("LICENSE") != std::string::npos;
        if (line.find("FS25_VRCompanion.zip\" modDesc.xml") != std::string::npos) modZipShipsLicense = hasLicense;
        if (line.find("$zipBundle") != std::string::npos) bundleZipShipsLicense = hasLicense;
    }
    EXPECT_TRUE(modZipShipsLicense);
    EXPECT_TRUE(bundleZipShipsLicense);
}

TEST_F(CompanionModTest, PerformsHandshakeWhenVRBridgePresent) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));


    int loadStatus = LoadCompanionModChecked(L);
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

    int loadStatus = LoadCompanionModChecked(L);
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


    ASSERT_EQ(LoadCompanionModChecked(L), 0);
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

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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

TEST_F(CompanionModTest, RequiresAfrFallbackFromTheFirstFrameWithoutPollingTelemetry) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('mapUS')", "=loadMapCall"), 0);

    // The fixed verdict (ADR-0017) holds before the first update, with no probe to wait for.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion:requiresAfrFallback() == true, "AFR fallback must be required before the first frame")
    )luau", "=assertBeforeUpdate"), 0);

    bool foundStereoLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("Stereo presentation: AFR fallback required: true") != std::string::npos) {
            foundStereoLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundStereoLog);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        for _ = 1, 5 do VRCompanion:update(16.6) end
        assert(VRCompanion:requiresAfrFallback() == true, "AFR fallback must stay required across updates")
    )luau", "=updateCalls"), 0);

    // No retired telemetry probe is reported, at load or while updating.
    for (const auto& msg : s_capturedLogs) {
        EXPECT_EQ(msg.find("Telemetry Probe"), std::string::npos) << msg;
    }
}

TEST_F(CompanionModTest, HandlesMissingStereoRenderingTogglesGracefully) {
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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

    EXPECT_NE(modDescContent.find("<action name=\"VR_RECENTER_VIEW\""), std::string::npos);
    EXPECT_NE(modDescContent.find("<actionBinding action=\"VR_RECENTER_VIEW\""), std::string::npos);
    EXPECT_EQ(modDescContent.find("\"VR_RECENTER\""), std::string::npos) << "VR_RECENTER is superseded by VR_RECENTER_VIEW";
    EXPECT_NE(modDescContent.find("KEY_numPad5"), std::string::npos);
    EXPECT_EQ(modDescContent.find("<vrSettings>"), std::string::npos) << "Setting defaults live in the DEFAULT_* constants, not modDesc.xml";
    EXPECT_NE(modDescContent.find("setting_recenterVrTrackingSpace"), std::string::npos);
}

TEST_F(CompanionModTest, InCabVehicleCameraFollowsHeadPoseWithCabReferenceBinding) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.15f, 0.05f, -0.10f);
    simPose.rotation = fs25vr::EulerAngles(0.20f, -0.30f, 0.02f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetIpd(0.0f);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.04f, -0.03f, 0.02f);
    simPose.rotation = fs25vr::EulerAngles(0.12f, 0.18f, -0.01f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetIpd(0.0f);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.5f, 0.2f, -0.3f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetIpd(0.0f);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    headset.SetEyeFov(0, leftFov);

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    headset.SetEyeFov(0, leftFov);

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    headset.SetEyeFov(0, leftFov);

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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

TEST_F(CompanionModTest, ObliqueProjectionConsoleCommandTogglesPersistsAndAppliesOffsets) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    headset.SetEyeFov(0, leftFov);

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local registeredCommands = {}
        local removedCommands = {}
        _G.addConsoleCommand = function(cmd, desc, funcName, target)
            registeredCommands[cmd] = { desc = desc, funcName = funcName, target = target }
        end
        _G.removeConsoleCommand = function(cmd)
            removedCommands[cmd] = true
        end

        local savedBools = {}
        _G.createXMLFile = function() return 1 end
        _G.setXMLFloat = function() end
        _G.setXMLBool = function(xml, key, val) savedBools[key] = val end
        _G.setXMLString = function() end
        _G.saveXMLFile = function() return true end
        _G.delete = function() end
        _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end

        VRCompanion:loadMap("testMap")
        assert(registeredCommands["vrObliqueProjection"] ~= nil, "vrObliqueProjection console command must be registered on map load")
        assert(registeredCommands["vrObliqueProjection"].funcName == "consoleCommandSetObliqueProjection",
            "registered callback must be consoleCommandSetObliqueProjection")

        -- Defaults to symmetric projection
        assert(VRCompanion.allowObliqueProjection == false, "oblique projection must default to off")
        local statusRes = VRCompanion:consoleCommandSetObliqueProjection("status")
        assert(string.find(statusRes, "Disabled") ~= nil, "status must report disabled by default")

        local invalidRes = VRCompanion:consoleCommandSetObliqueProjection("maybe")
        assert(string.find(invalidRes, "Usage: vrObliqueProjection") ~= nil, "invalid input must return usage")
        assert(VRCompanion.allowObliqueProjection == false, "invalid input must not change the setting")

        local cam = { isInside = true, cameraNode = 301, origTransX = 0, origTransY = 1.5, origTransZ = 0 }

        -- Enabling applies per-eye asymmetric offsets and persists to settings.xml
        VRCompanion:consoleCommandSetObliqueProjection("true")
        assert(VRCompanion.allowObliqueProjection == true, "true must enable oblique projection")
        assert(savedBools["vrSettings.allowObliqueProjection"] == true, "enabled preference must persist to settings.xml")
        VRCompanion:onVehicleCameraUpdate(cam, 16.6)
        assert(math.abs(_G.cameraProjectionOffsets[301].x) > 0.001, "enabled oblique projection must apply a horizontal offset")

        -- Toggling back restores the centered projection and persists
        VRCompanion:consoleCommandSetObliqueProjection("toggle")
        assert(VRCompanion.allowObliqueProjection == false, "toggle must disable oblique projection")
        assert(savedBools["vrSettings.allowObliqueProjection"] == false, "disabled preference must persist to settings.xml")
        VRCompanion:onVehicleCameraUpdate(cam, 16.6)
        assert(math.abs(_G.cameraProjectionOffsets[301].x) < 0.0001, "disabled oblique projection must center the projection")

        VRCompanion:deleteMap()
        assert(removedCommands["vrObliqueProjection"] == true, "deleteMap must unregister vrObliqueProjection")
    )luau", "=testObliqueProjectionConsoleCommand"), 0);
}

TEST_F(CompanionModTest, RestoresOriginalFovAndProjectionOffsetsOnMapUnload) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    headset.SetEyeFov(0, leftFov);

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    XrFovf rightFov{ -0.80f, 0.90f, 0.85f, -0.85f };
    headset.SetEyeFov(0, leftFov);
    headset.SetEyeFov(1, rightFov);

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);
    bridge.SetIpd(0.064f); // 64mm IPD -> +/- 0.032m
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    XrFovf rightFov{ -0.80f, 0.90f, 0.85f, -0.85f };
    headset.SetEyeFov(0, leftFov);
    headset.SetEyeFov(1, rightFov);

    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);
    bridge.SetIpd(0.064f);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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

TEST_F(CompanionModTest, HooksPlayerCameraUpdatePositionAndDispatchesOnFoot6DOFTracking) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.1f, 0.2f, 0.3f);
    simPose.rotation = fs25vr::EulerAngles(0.05f, 0.10f, 0.02f);
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto openXrManager = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.2f, 0.1f, -0.05f);
    simPose.rotation = fs25vr::EulerAngles(0.05f, 0.15f, 0.0f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto openXrManager = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.5f, 0.2f, -0.1f);
    simPose.rotation = fs25vr::EulerAngles(0.1f, 0.2f, 0.0f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto openXrManager = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.1f, 0.0f, 0.0f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto openXrManager = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(1.0f, 1.5f, 0.0f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.DEFAULT_HUD_INSET_X == 0.30, "DEFAULT_HUD_INSET_X must be 0.30")
        assert(VRCompanion.DEFAULT_HUD_INSET_Y == 0.22, "DEFAULT_HUD_INSET_Y must be 0.22")
        assert(VRCompanion.DEFAULT_HUD_UI_SCALE == 0.82, "DEFAULT_HUD_UI_SCALE must be 0.82")

        local left, right, bottom, top, scale = VRCompanion:calculateHudAnchors()
        assert(math.abs(left - 0.30) < 0.0001, "left anchor default must be 0.30")
        assert(math.abs(right - 0.70) < 0.0001, "right anchor default must be 0.70")
        assert(math.abs(bottom - 0.22) < 0.0001, "bottom anchor default must be 0.22")
        assert(math.abs(top - 0.78) < 0.0001, "top anchor default must be 0.78")
        assert(math.abs(scale - 0.82) < 0.0001, "scale multiplier default must be 0.82")

        local cLeft, cRight, cBottom, cTop, cScale = VRCompanion:calculateHudAnchors(0.12, 0.08, 0.95)
        assert(math.abs(cLeft - 0.12) < 0.0001, "custom left anchor must be 0.12")
        assert(math.abs(cRight - 0.88) < 0.0001, "custom right anchor must be 0.88")
        assert(math.abs(cBottom - 0.08) < 0.0001, "custom bottom anchor must be 0.08")
        assert(math.abs(cTop - 0.92) < 0.0001, "custom top anchor must be 0.92")
        assert(math.abs(cScale - 0.95) < 0.0001, "custom scale must be 0.95")
    )luau", "=verifyCalculations"), 0);
}

TEST_F(CompanionModTest, ClampsVRHudInsetsAndScaleWithinSafeBoundaries) {
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
        assert(math.abs(fb.left - 0.30) < 0.0001, "fallback left must be default 0.30")
        assert(math.abs(fb.right - 0.70) < 0.0001, "fallback right must be default 0.70")
        assert(math.abs(fb.bottom - 0.22) < 0.0001, "fallback bottom must be default 0.22")
        assert(math.abs(fb.top - 0.78) < 0.0001, "fallback top must be default 0.78")
        assert(math.abs(fb.scaleMultiplier - 0.82) < 0.0001, "fallback scale must be default 0.82")
    )luau", "=verifyConfigApi"), 0);
}

TEST_F(CompanionModTest, AppliesVRHudInsetsAndScaleToStandardDisplaysUponMapLoad) {
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
        assert(math.abs(_G.g_hudAnchorLeft - 0.30) < 0.0001, "g_hudAnchorLeft must be 0.30")
        assert(math.abs(_G.g_hudAnchorRight - 0.70) < 0.0001, "g_hudAnchorRight must be 0.70")
        assert(math.abs(_G.g_hudAnchorBottom - 0.22) < 0.0001, "g_hudAnchorBottom must be 0.22")
        assert(math.abs(_G.g_hudAnchorTop - 0.78) < 0.0001, "g_hudAnchorTop must be 0.78")

        -- UI scale multiplier applied (0.82x)
        assert(math.abs(VRCompanion.effectiveUiScale - 0.82) < 0.0001, "effectiveUiScale must be 0.82")
        assert(displays.speedMeter.setScaleCalled == true, "SpeedMeterDisplay.setScale must be called")
        assert(displays.speedMeter.scale == 0.82, "SpeedMeterDisplay scale must be 0.82")

        -- All standard displays repositioned
        assert(displays.speedMeter.storeScaledValuesCalled == true, "speedMeter storeScaledValues called")
        assert(displays.inputHelp.storeScaledValuesCalled == true, "inputHelp storeScaledValues called")
        assert(displays.gameInfo.storeScaledValuesCalled == true, "gameInfo storeScaledValues called")
        assert(displays.contextAction.storeScaledValuesCalled == true, "contextAction storeScaledValues called")
        assert(displays.ingameMap.storeScaledValuesCalled == true, "ingameMap storeScaledValues called")

        -- Display positioning coordinates
        assert(displays.speedMeter.position.x == 0.70 and displays.speedMeter.position.y == 0.22, "speedMeter bottom-right")
        assert(displays.inputHelp.position.x == 0.30 and displays.inputHelp.position.y == 0.78, "inputHelp top-left")
        assert(displays.gameInfo.position.x == 0.70 and displays.gameInfo.position.y == 0.78, "gameInfo top-right")
        assert(displays.contextAction.position.x == 0.30 and displays.contextAction.position.y == 0.22, "contextAction bottom-left")
        assert(displays.ingameMap.position.x == 0.30 and displays.ingameMap.position.y == 0.22, "ingameMap bottom-left")
    )luau", "=verifyDisplaysUponMapLoad"), 0);
}

TEST_F(CompanionModTest, RestoresStandard2DHudCornerAnchorsOnMapUnload) {
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_hudAnchorLeft = 0.0
        _G.g_hudAnchorRight = 1.0
        _G.g_hudAnchorBottom = 0.0
        _G.g_hudAnchorTop = 1.0
        _G.g_currentMission = { hud = {} }

        VRCompanion:loadMap('testMap')
        assert(math.abs(_G.g_hudAnchorLeft - 0.30) < 0.0001, "g_hudAnchorLeft must be 0.30 when loaded")

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    auto oxr = std::make_shared<fs25vr::OpenXrManager>(std::make_shared<fs25vr::NativeOpenXrDispatch>());
    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);
    ASSERT_TRUE(bridge.Inject(L));
    RegisterTestSetFrameParity(bridge);

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    auto oxr = std::make_shared<fs25vr::OpenXrManager>(std::make_shared<fs25vr::NativeOpenXrDispatch>());
    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    RegisterTestSetFrameParity(bridge);

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    auto oxr = std::make_shared<fs25vr::OpenXrManager>(std::make_shared<fs25vr::NativeOpenXrDispatch>());
    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    RegisterTestSetFrameParity(bridge);

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE == true, "Default recenterVrTrackingSpace constant must be true")
        assert(VRCompanion.recenterVrTrackingSpace == true, "VRCompanion.recenterVrTrackingSpace must default to true")
        assert(VRCompanion:getRecenterMode() == "Both", "Default getRecenterMode must return Both")
    )luau", "=verifyRecenterModeDefaults"), 0);
}

TEST_F(CompanionModTest, RecenterActionInBothModeZeroesCameraYawAndCallsBridgeRecenter) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.4f, 0.1f, -0.2f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.4f, 0.1f, -0.2f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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

TEST_F(CompanionModTest, OnFootSnapTurnAdjustsPlayerCharacterYawNode) {
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local registeredEvents = {}
        _G.g_inputBinding = {
            events = {}, actionEvents = {}, -- FS25: events keyed by event id, actionEvents keyed by action name
            registerActionEvent = function(self, actionName, target, callback, triggerUp, triggerDown, triggerAlways, startActive)
                local id = actionName .. "|table: 0x0|" .. (self.nextEventNumber or 1); self.nextEventNumber = (self.nextEventNumber or 1) + 1
                local ev = { id = id, actionName = actionName, target = target, callback = callback }
                self.events[id] = ev; self.actionEvents[actionName] = { ev }
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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

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

TEST_F(CompanionModTest, Is2DViewActiveDefaultsToFalseAndSynchronizesWithVRBridge) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.is2DViewActive == false, "is2DViewActive must default to false")
        assert(VRCompanion:getIs2DViewActive() == false, "getIs2DViewActive must return false by default")
        assert(VRBridge.getIs2DViewActive() == false, "VRBridge.getIs2DViewActive must return false by default")

        -- Toggle to true
        VRCompanion:set2DViewActive(true)
        assert(VRCompanion.is2DViewActive == true, "is2DViewActive must be true after set2DViewActive(true)")
        assert(VRCompanion:getIs2DViewActive() == true, "getIs2DViewActive must return true")
        assert(VRBridge.getIs2DViewActive() == true, "VRBridge must reflect true state")

        -- Toggle to false
        VRCompanion:set2DViewActive(false)
        assert(VRCompanion.is2DViewActive == false, "is2DViewActive must be false after set2DViewActive(false)")
        assert(VRBridge.getIs2DViewActive() == false, "VRBridge must reflect false state")

        -- External bridge synchronization during update
        VRBridge.set2DViewActive(true)
        assert(VRBridge.getIs2DViewActive() == true)
        assert(VRCompanion.is2DViewActive == false) -- not yet updated
        VRCompanion:update(16.6)
        assert(VRCompanion.is2DViewActive == true, "VRCompanion must sync with external VRBridge state on update")
    )luau", "=test2DViewSync"), 0);
}

TEST_F(CompanionModTest, InCabVehicleCameraBypasses6DOFTrackingIn2DMode) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.15f, 0.05f, -0.10f);
    simPose.rotation = fs25vr::EulerAngles(0.12f, 0.25f, 0.0f);
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local mockVehCam = {
            isInside = true,
            cameraNode = 10,
            origRotY = 3.14159,
            rotY = 3.14159,
            rotX = 0.0,
            rotZ = 0.0,
            transX = 0.5,
            transY = 2.0,
            transZ = 1.0,
            origTransX = 0.5,
            origTransY = 2.0,
            origTransZ = 1.0,
        }
        _G.mockCamNodeTrans = { 0.5, 2.0, 1.0 }
        _G.mockCamNodeRot = { 0.0, 3.14159, 0.0 }

        function setTranslation(node, x, y, z)
            _G.mockCamNodeTrans = { x, y, z }
        end
        function getTranslation(node)
            return _G.mockCamNodeTrans[1], _G.mockCamNodeTrans[2], _G.mockCamNodeTrans[3]
        end
        function setRotation(node, rx, ry, rz)
            _G.mockCamNodeRot = { rx, ry, rz }
        end
        function getRotation(node)
            return _G.mockCamNodeRot[1], _G.mockCamNodeRot[2], _G.mockCamNodeRot[3]
        end

        -- 1. In 3D VR Mode (is2DViewActive == false), onVehicleCameraUpdate applies 6DOF head pose
        VRCompanion:set2DViewActive(false)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 16.6)
        assert(math.abs(_G.mockCamNodeRot[1] - 0.12) < 0.001, "3D VR mode must apply HMD pitch to camera")

        -- 2. Switch to 2D Floating Window Mode (is2DViewActive == true)
        VRCompanion:set2DViewActive(true)

        -- Simulate standard 2D mouse rotation and translation in-cab
        mockVehCam.rotX = 0.35
        mockVehCam.rotY = 2.50
        _G.mockCamNodeRot = { 0.35, 2.50, 0.0 }
        _G.mockCamNodeTrans = { 0.5, 2.0, 1.0 }

        -- Call onVehicleCameraUpdate in 2D mode
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 16.6)

        -- 6DOF head tracking updates must be bypassed! 2D mouse rotation is preserved untouched
        assert(math.abs(_G.mockCamNodeRot[1] - 0.35) < 0.001, "2D mode must preserve mouse pitch and bypass HMD pitch")
        assert(math.abs(_G.mockCamNodeRot[2] - 2.50) < 0.001, "2D mode must preserve mouse yaw and bypass HMD yaw")
        assert(math.abs(_G.mockCamNodeTrans[1] - 0.5) < 0.001, "2D mode must preserve vehicle camera translation")
    )luau", "=testVehicleCameraBypass"), 0);
}

TEST_F(CompanionModTest, OnFootPlayerCameraBypasses6DOFTrackingAndSupports2DMouseLook) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.1f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.2f, 0.0f, 0.0f);
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local mockInput = { cameraRotationX = 0.15, cameraRotationY = -0.25 }
        local curCamRot = { 0.0, 0.0, 0.0 }
        local curCamTrans = { 0.0, 1.8, 0.0 }

        local mockPlayerCam = {
            cameraNode = 20,
            firstPersonCamera = 20,
            player = { inputComponent = mockInput },
            setRotation = function(self, rx, ry, rz)
                curCamRot = { rx, ry, rz }
            end,
            getRotation = function(self)
                return curCamRot[1], curCamRot[2], curCamRot[3]
            end
        }
        function setTranslation(node, x, y, z) curCamTrans = { x, y, z } end
        function getTranslation(node) return curCamTrans[1], curCamTrans[2], curCamTrans[3] end
        function setRotation(node, rx, ry, rz) curCamRot = { rx, ry, rz } end
        function getRotation(node) return curCamRot[1], curCamRot[2], curCamRot[3] end

        -- 1. In 3D VR mode: mouse rotation keeps pitch = 0 (horizon locked level)
        VRCompanion:set2DViewActive(false)
        VRCompanion:onPlayerCameraUpdateRotation(mockPlayerCam, 16.6)
        assert(curCamRot[1] == 0.0, "3D VR mode must zero neck pitch")

        -- onPlayerCameraUpdate applies HMD pose
        VRCompanion:onPlayerCameraUpdate(mockPlayerCam, 16.6)
        assert(math.abs(curCamRot[1] - 0.2) < 0.001, "3D VR mode applies HMD pitch in onPlayerCameraUpdate")

        -- 2. In 2D Floating Window Mode: mouse drives both pitch and yaw
        VRCompanion:set2DViewActive(true)
        curCamRot = { 0.0, 0.0, 0.0 }
        curCamTrans = { 0.0, 1.8, 0.0 }

        VRCompanion:onPlayerCameraUpdateRotation(mockPlayerCam, 16.6)
        assert(math.abs(curCamRot[1] - 0.15) < 0.001, "2D mode must update pitch from mouse Y input")
        assert(math.abs(curCamRot[2] - 0.25) < 0.001, "2D mode must update yaw from mouse X input")

        -- onPlayerCameraUpdate bypasses 6DOF updates completely
        VRCompanion:onPlayerCameraUpdate(mockPlayerCam, 16.6)
        assert(math.abs(curCamRot[1] - 0.15) < 0.001, "2D mode must not overwrite pitch with HMD pose")
        assert(math.abs(curCamTrans[1] - 0.0) < 0.001, "2D mode must not apply HMD eye translation")
    )luau", "=testPlayerCameraBypass"), 0);
}

TEST_F(CompanionModTest, CameraProjectionRestoresOriginalFovAndZeroOffsetsIn2DMode) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    headset.SetEyeFov(0, leftFov);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    SetupCameraMockGlobals(L);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local camNode = 50
        setFovY(camNode, 1.0472) -- standard 60 deg FS25 camera FOV
        setProjectionOffset(camNode, 0.0, 0.0)

        -- 1. Apply VR projection in 3D mode (is2DViewActive == false)
        VRCompanion:set2DViewActive(false)
        VRCompanion:applyCameraProjection(camNode, 0)
        local vrFov = getFovY(camNode)
        assert(math.abs(vrFov - 1.70) < 0.02, "3D VR mode must apply VR headset FOV")

        -- 2. Switch to 2D mode (is2DViewActive == true)
        VRCompanion:set2DViewActive(true)
        VRCompanion:applyCameraProjection(camNode, 0)
        local restoredFov = getFovY(camNode)
        local offX, offY = getProjectionOffset(camNode)
        assert(math.abs(restoredFov - 1.0472) < 0.0001, "2D mode must restore original FOV")
        assert(offX == 0.0 and offY == 0.0, "2D mode must enforce zero projection offsets")

        -- 3. Switch back to 3D VR mode
        VRCompanion:set2DViewActive(false)
        VRCompanion:applyCameraProjection(camNode, 0)
        assert(math.abs(getFovY(camNode) - vrFov) < 0.0001, "Returning to 3D VR mode restores VR FOV")
    )luau", "=testCameraProjectionRestoration"), 0);
}

TEST_F(CompanionModTest, HudConvergenceDisparityZeroedAndFullFrameAnchorsAppliedIn2DMode) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>(std::make_shared<fs25vr::NativeOpenXrDispatch>());
    fs25vr::VRBridge bridge(oxr, nullptr);
    bridge.SetIpd(0.064f);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion.handshakeComplete = true
        VRCompanion.passThroughMode = false
        VRCompanion.headTrackingEnabled = true

        _G.g_hudAnchorLeft = 0.20
        _G.g_hudAnchorRight = 0.80
        _G.g_hudAnchorBottom = 0.16
        _G.g_hudAnchorTop = 0.84

        -- 1. In 3D VR mode: convergence disparity is computed from IPD and non-zero
        VRCompanion:set2DViewActive(false)
        VRCompanion:updateHudConvergence(16.6)
        assert(VRCompanion.currentConvergenceDisparity > 0.0, "3D VR mode must have non-zero disparity")

        -- 2. In 2D Floating Window Mode: disparity and offset are zeroed out and full-frame anchors applied
        VRCompanion:set2DViewActive(true)
        VRCompanion:updateHudConvergence(16.6)
        assert(VRCompanion.currentConvergenceDisparity == 0.0, "2D mode must zero convergence disparity")
        assert(VRCompanion.currentConvergenceOffset == 0.0, "2D mode must zero convergence offset")
        assert(_G.g_hudAnchorLeft == 0.0, "2D mode must apply full-frame left anchor (0.0)")
        assert(_G.g_hudAnchorRight == 1.0, "2D mode must apply full-frame right anchor (1.0)")
        assert(_G.g_hudAnchorBottom == 0.0, "2D mode must apply full-frame bottom anchor (0.0)")
        assert(_G.g_hudAnchorTop == 1.0, "2D mode must apply full-frame top anchor (1.0)")

        -- Check across alternating frame parities (AFR left vs right)
        VRBridge.frameParity = 1
        VRCompanion:updateHudConvergence(16.6)
        assert(VRCompanion.currentConvergenceOffset == 0.0, "Parity 1 must also have 0.0 convergence offset in 2D mode")
        assert(_G.g_hudAnchorLeft == 0.0)
        assert(_G.g_hudAnchorRight == 1.0)

        -- 3. Returning to 3D VR mode restores VR insets and disparity
        VRCompanion:set2DViewActive(false)
        VRCompanion:updateHudConvergence(16.6)
        assert(VRCompanion.currentConvergenceDisparity > 0.0, "Returning to 3D VR mode restores disparity")
    )luau", "=testHudZeroingIn2DMode"), 0);
}

TEST_F(CompanionModTest, ModeIsStickyAcrossInGameGuiMenus) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local menuVisible = false
        _G.g_gui = {
            getIsGuiVisible = function()
                return menuVisible
            end
        }

        -- Start in 2D mode
        VRCompanion:set2DViewActive(true)
        assert(VRCompanion.is2DViewActive == true)

        -- 1. Open in-game GUI menu (e.g. ESC pause or shop)
        menuVisible = true
        VRCompanion:update(16.6)
        assert(VRCompanion.isMenuVisible == true, "Menu must be visible")
        assert(VRCompanion.is2DViewActive == true, "2D mode must remain active while in menu")
        assert(VRBridge.getIs2DViewActive() == true, "VRBridge must remain in 2D mode")

        -- 2. Close in-game GUI menu
        menuVisible = false
        VRCompanion:update(16.6)
        assert(VRCompanion.isMenuVisible == false, "Menu must be closed")
        assert(VRCompanion.is2DViewActive == true, "2D mode must be sticky and remain active after menu closes")
        assert(VRBridge.getIs2DViewActive() == true, "VRBridge must remain in 2D mode after menu closes")
    )luau", "=testMenuStickiness"), 0);
}

TEST_F(CompanionModTest, FreshMapLoadAlwaysInitializesIn3DVrMode) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        -- Force 2D mode before map load
        VRCompanion:set2DViewActive(true)
        assert(VRCompanion.is2DViewActive == true)
        assert(VRBridge.getIs2DViewActive() == true)

        -- Fresh map load
        VRCompanion:loadMap("mapUS")

        -- Must cleanly initialize in 3D VR mode (false)
        assert(VRCompanion.is2DViewActive == false, "Fresh map load must always reset is2DViewActive to false")
        assert(VRCompanion:getIs2DViewActive() == false, "getIs2DViewActive must return false")
        assert(VRBridge.getIs2DViewActive() == false, "VRBridge must be synchronized to false on map load")
    )luau", "=testFreshMapLoad"), 0);
}

TEST_F(CompanionModTest, SnapTurnSuppressedWhen2DViewActive) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local mockVehCam = {
            isInside = true,
            rotY = 0.0,
            origRotY = 0.0
        }
        local mockVeh = {
            getActiveCamera = function() return mockVehCam end
        }
        VRCompanion.getActiveVehicleCamera = function() return mockVehCam end
        VRCompanion.getControlledVehicle = function() return mockVeh end

        -- In 3D VR mode: snap turn succeeds
        VRCompanion:set2DViewActive(false)
        local ok3D = VRCompanion:snapTurn(1)
        assert(ok3D == true, "Snap turn must succeed in 3D VR mode")

        -- In 2D Floating Window Mode: snap turn is suppressed
        VRCompanion:set2DViewActive(true)
        VRCompanion.lastSnapTurnTime = 0
        local ok2D, reason = VRCompanion:snapTurn(1)
        assert(ok2D == false, "Snap turn must be suppressed in 2D Floating Window Mode")
        assert(reason == "2d_view_active", "Reason must indicate 2d_view_active")
    )luau", "=testSnapTurnSuppression"), 0);
}

TEST_F(CompanionModTest, ConsoleCommandsManage2DViewMode) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:set2DViewActive(false)

        -- Query status
        local status = VRCompanion:consoleCommandSet2DView("status")
        assert(string.find(status, "false") ~= nil, "Status must report false")

        -- Set true
        VRCompanion:consoleCommandSet2DView("true")
        assert(VRCompanion.is2DViewActive == true, "Command set2DView true must enable 2D mode")

        -- Toggle
        VRCompanion:consoleCommandToggleView()
        assert(VRCompanion.is2DViewActive == false, "Command toggleView must toggle to false")

        VRCompanion:consoleCommandToggleView()
        assert(VRCompanion.is2DViewActive == true, "Command toggleView must toggle to true")
    )luau", "=testConsoleCommands"), 0);
}

TEST_F(CompanionModTest, MetadataDescriptorDeclaresToggleViewActionAndBindings) {
    std::string modDescContent = ReadFile("mods/FS25_VRCompanion/modDesc.xml");
    ASSERT_FALSE(modDescContent.empty());

    EXPECT_NE(modDescContent.find("<action name=\"VR_TOGGLE_VIEW\" category=\"ONFOOT VEHICLE\"/>"), std::string::npos);
    EXPECT_NE(modDescContent.find("<actionBinding action=\"VR_TOGGLE_VIEW\">"), std::string::npos);
    EXPECT_NE(modDescContent.find("KEY_rctrl KEY_v"), std::string::npos);
    EXPECT_NE(modDescContent.find("KEY_numPad0"), std::string::npos);
    EXPECT_NE(modDescContent.find("<binding device=\"GAMEPAD_DEFAULT\""), std::string::npos);
    EXPECT_NE(modDescContent.find("BUTTON_9"), std::string::npos);
    EXPECT_NE(modDescContent.find("<text name=\"input_VR_TOGGLE_VIEW\">"), std::string::npos);
}

TEST_F(CompanionModTest, ToggleViewInEngineActionEventsRegistration) {
    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local registeredEvents = {}
        _G.g_inputBinding = {
            events = {}, actionEvents = {}, -- FS25: events keyed by event id, actionEvents keyed by action name
            registerActionEvent = function(self, actionName, target, callback, triggerUp, triggerDown, triggerAlways, startActive)
                local id = actionName .. "|table: 0x0|" .. (self.nextEventNumber or 1); self.nextEventNumber = (self.nextEventNumber or 1) + 1
                local ev = { id = id, actionName = actionName, target = target, callback = callback }
                self.events[id] = ev; self.actionEvents[actionName] = { ev }
                registeredEvents[actionName] = ev
                return true, id
            end,
            setActionEventTextPriority = function() end,
            setActionEventActive = function() end,
            setActionEventTextVisibility = function() end,
            setActionEventText = function() end
        }

        VRCompanion:registerActionEvents(true)
        assert(registeredEvents["VR_TOGGLE_VIEW"] ~= nil, "VR_TOGGLE_VIEW must be registered")
        assert(registeredEvents["VR_TOGGLE_VIEW"].callback == VRCompanion.onToggleVrViewAction, "Callback must be onToggleVrViewAction")
        assert(VRCompanion.toggleViewActionEventId ~= nil, "toggleViewActionEventId must be set")

        -- deleteMap cleans up action event ID
        VRCompanion:deleteMap()
        assert(VRCompanion.toggleViewActionEventId == nil, "toggleViewActionEventId must be nil after deleteMap")
    )luau", "=verifyToggleViewActionRegistration"), 0);
}

TEST_F(CompanionModTest, ToggleViewActionTogglesModeWithDebounceAndSilentExecution) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local warningCalls = 0
        _G.g_currentMission = {
            showBlinkingWarning = function(self, msg, duration)
                warningCalls = warningCalls + 1
            end
        }

        VRCompanion:set2DViewActive(false)
        VRCompanion:resetToggleViewDebounce()
        assert(VRCompanion.is2DViewActive == false)

        -- 1. Initial toggle action at t = 1000 toggles to 2D mode (silent)
        local ok, res = VRCompanion:onToggleVrViewAction("VR_TOGGLE_VIEW", 1, 1000)
        assert(ok == true, "First toggle action must succeed")
        assert(VRCompanion.is2DViewActive == true, "Must switch to 2D Floating Window Mode")
        assert(warningCalls == 0, "Toggle action must execute silently without blinking warnings")

        -- 2. Immediate second press at t = 1100 (within 250ms debounce) is rejected
        local ok2, reason = VRCompanion:onToggleVrViewAction("VR_TOGGLE_VIEW", 1, 1100)
        assert(ok2 == false, "Rapid consecutive press must be debounced")
        assert(reason == "debounced", "Failure reason must be 'debounced'")
        assert(VRCompanion.is2DViewActive == true, "View mode must remain 2D")
        assert(warningCalls == 0, "Debounced press must not trigger warnings")

        -- 3. Third press at t = 1300 (> 250ms debounce) succeeds, toggling back to 3D VR mode
        local ok3, res3 = VRCompanion:onToggleVrViewAction("VR_TOGGLE_VIEW", 1, 1300)
        assert(ok3 == false, "Toggling back returns new active state (false)")
        assert(VRCompanion.is2DViewActive == false, "Must return to 3D VR mode")
        assert(warningCalls == 0, "Exit from 2D mode must also execute silently without warnings")
    )luau", "=testToggleViewDebounceAndSilence"), 0);
}

TEST_F(CompanionModTest, RecenterIn2DModeRealignsFloatingQuadWithoutResettingVehicleOrientation) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local alignMenuQuadCalls = 0
        local recenterCalls = 0
        _G.VRBridge.alignMenuQuad = function()
            alignMenuQuadCalls = alignMenuQuadCalls + 1
        end
        _G.VRBridge.recenter = function()
            recenterCalls = recenterCalls + 1
        end

        local mockVehCam = {
            isInside = true,
            rotY = 1.25,
            origRotY = 0.0,
            cameraNode = 101
        }
        VRCompanion.getActiveVehicleCamera = function() return mockVehCam end
        VRCompanion.getControlledVehicle = function() return {} end

        -- Set 2D view mode active
        VRCompanion:set2DViewActive(true)

        -- Trigger Recenter Action while in 2D mode
        VRCompanion:onRecenterAction()

        -- Verify alignMenuQuad was called to realign 6DOF floating quad
        assert(alignMenuQuadCalls == 1, "alignMenuQuad must be called when recentering in 2D mode")
        -- Verify OpenXR tracking space recenter was NOT called
        assert(recenterCalls == 0, "VRBridge.recenter must NOT be called when in 2D mode")
        -- Verify vehicle camera orientation (rotY) was NOT reset
        assert(mockVehCam.rotY == 1.25, "Vehicle camera rotY must remain untouched in 2D mode recenter")

        -- Now switch back to 3D VR mode
        VRCompanion:set2DViewActive(false)
        VRCompanion:onRecenterAction()

        -- In 3D mode, standard recenter should be invoked and camera reset
        assert(recenterCalls == 1, "VRBridge.recenter must be called when in 3D VR mode")
        assert(alignMenuQuadCalls == 1, "alignMenuQuad must NOT be called in 3D mode")
    )luau", "=testRecenterIn2DMode"), 0);
}

TEST_F(CompanionModTest, ImplementsGetAndSetRenderingMode) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.RENDERING_MODE_AFR == "AFR", "RENDERING_MODE_AFR must be 'AFR'")
        assert(VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS == "SequentialDualPass", "RENDERING_MODE_SEQUENTIAL_DUAL_PASS must be 'SequentialDualPass'")
        assert(VRCompanion.RENDERING_MODE_DUAL_PASS == "SequentialDualPass", "RENDERING_MODE_DUAL_PASS must be alias")
        assert(type(VRCompanion.getRenderingMode) == "function", "getRenderingMode must be a function")
        assert(type(VRCompanion.setRenderingMode) == "function", "setRenderingMode must be a function")

        -- Initially SequentialDualPass
        assert(VRCompanion:getRenderingMode() == "SequentialDualPass", "initial getRenderingMode must be 'SequentialDualPass'")

        -- Switch to AFR
        local res = VRCompanion:setRenderingMode("AFR")
        assert(res == "AFR", "setRenderingMode('AFR') must return 'AFR'")
        assert(VRCompanion:getRenderingMode() == "AFR", "getRenderingMode must return 'AFR'")
        assert(VRBridge.getRenderingMode() == "AFR", "VRBridge native mode must synchronize to 'AFR'")

        -- Toggle back to SequentialDualPass
        local toggled = VRCompanion:setRenderingMode("toggle")
        assert(toggled == "SequentialDualPass", "setRenderingMode('toggle') must toggle to 'SequentialDualPass'")
        assert(VRCompanion:getRenderingMode() == "SequentialDualPass", "getRenderingMode must return 'SequentialDualPass'")
        assert(VRBridge.getRenderingMode() == "SequentialDualPass", "VRBridge native mode must synchronize to 'SequentialDualPass'")

        -- Set via alias
        VRCompanion:setRenderingMode("dual")
        assert(VRCompanion:getRenderingMode() == "SequentialDualPass", "setRenderingMode('dual') must set SequentialDualPass")
        assert(VRBridge.getRenderingMode() == "SequentialDualPass", "VRBridge must reflect dual")
    )luau", "=testRenderingMode"), 0);
}

TEST_F(CompanionModTest, QueriesNativeRenderingModeOnInitializationAndHandshake) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    // Pre-configure native state to Sequential Dual-Pass
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::SequentialDualPass);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    // Initial map load should handshake and query native rendering mode
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('mapUS')", "=loadMap"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.handshakeComplete == true, "handshake must be completed")
        assert(VRCompanion:getRenderingMode() == "SequentialDualPass", "VRCompanion must query native state and be 'SequentialDualPass'")
        assert(VRCompanion.renderingMode == "SequentialDualPass", "VRCompanion.renderingMode property must be 'SequentialDualPass'")
    )luau", "=assertNativeSync"), 0);

    // Verify log contains the active rendering mode
    bool foundModeLog = false;
    for (const auto& msg : s_capturedLogs) {
        if (msg.find("renderingMode: SequentialDualPass") != std::string::npos ||
            msg.find("Active VR Rendering Mode: SequentialDualPass") != std::string::npos) {
            foundModeLog = true;
            break;
        }
    }
    EXPECT_TRUE(foundModeLog);
}

TEST_F(CompanionModTest, RenderingModePersistenceAcrossMapUnload) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    // First map load
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('mapUS')", "=loadMap1"), 0);
    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::SequentialDualPass);

    // User switches to AFR in game
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:setRenderingMode('AFR')", "=setAFR"), 0);
    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::AFR);

    // Map unload
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    // Native state must persist AFR
    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::AFR);
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);

    // Second map load
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('mapAlpine')", "=loadMap2"), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.handshakeComplete == true, "handshake must succeed on second map")
        assert(VRCompanion:getRenderingMode() == "AFR", "mode must persist across map reloads")
    )luau", "=assertPersistence"), 0);
}

TEST_F(CompanionModTest, DualPassMissionUpdatePacingAndCameraSelection) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.currentCamera = 0
        function _G.setCamera(cam)
            _G.currentCamera = cam
        end
        function _G.getCamera()
            return _G.currentCamera
        end

        local capturedDt = nil
        _G.FSBaseMission = {
            update = function(self, dt)
                capturedDt = dt
            end
        }

        VRCompanion:loadMap("mapUS")
        VRCompanion.handshakeComplete = true
        VRCompanion.passThroughMode = false
        VRCompanion.headTrackingEnabled = true
        VRCompanion:setRenderingMode("DualPass")
        VRCompanion.is2DViewActive = false
        VRCompanion.isMenuVisible = false

        VRCompanion.cameraNodeLeft = 5001
        VRCompanion.cameraNodeRight = 5002

        -- 1. Sub-Pass 0 (Left Eye): real dt and cameraNodeLeft
        VRBridge.setSubPassIndex(0)
        _G.FSBaseMission:update(16.666)
        assert(math.abs(capturedDt - 16.666) < 0.001, "Sub-Pass 0 must receive real dt")
        assert(_G.currentCamera == 5001, "Sub-Pass 0 must bind cameraNodeLeft (5001)")
        assert(VRCompanion.activeRenderingCamera == 5001, "activeRenderingCamera must be 5001")

        -- 2. Sub-Pass 1 (Right Eye): simulation frozen (no update called) and cameraNodeRight bound
        capturedDt = nil
        VRBridge.setSubPassIndex(1)
        _G.FSBaseMission:update(16.666)
        assert(capturedDt == nil, "Sub-Pass 1 must freeze simulation (FSBaseMission.update not invoked)")
        assert(_G.currentCamera == 5002, "Sub-Pass 1 must bind cameraNodeRight (5002)")
        assert(VRCompanion.activeRenderingCamera == 5002, "activeRenderingCamera must be 5002")

        -- 3. Cleanup on deleteMap
        VRCompanion:deleteMap()
        assert(VRCompanion.missionUpdateHooked == false, "missionUpdateHooked must be false after deleteMap")

        _G.FSBaseMission = nil
    )luau", "=testDualPassPacing"), 0);
}

TEST_F(CompanionModTest, DualPassHudConvergenceLockedToSubPassEyeIndex) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>(std::make_shared<fs25vr::NativeOpenXrDispatch>());
    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    oxr->SetIpd(0.064f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap("mapUS")
        VRCompanion.handshakeComplete = true
        VRCompanion.passThroughMode = false
        VRCompanion.headTrackingEnabled = true
        VRCompanion:setRenderingMode("DualPass")
        VRCompanion.is2DViewActive = false
        VRCompanion.isMenuVisible = false
        VRCompanion.currentConvergenceDisparity = 0.012

        -- SubPass 0 (Left Eye) -> +0.012
        VRBridge.setSubPassIndex(0)
        local off0 = VRCompanion:getConvergenceOffset(nil, 0.012)
        assert(math.abs(off0 - 0.012) < 0.0001, "SubPass 0 must yield +0.012")
        VRCompanion:updateHudConvergence(0.016)
        assert(math.abs(VRCompanion.currentConvergenceOffset - 0.012) < 0.0001, "updateHudConvergence must set +0.012 on SubPass 0")

        -- SubPass 1 (Right Eye) -> -0.012
        VRBridge.setSubPassIndex(1)
        local off1 = VRCompanion:getConvergenceOffset(nil, 0.012)
        assert(math.abs(off1 - (-0.012)) < 0.0001, "SubPass 1 must yield -0.012")
        VRCompanion:updateHudConvergence(0.016)
        assert(math.abs(VRCompanion.currentConvergenceOffset - (-0.012)) < 0.0001, "updateHudConvergence must set -0.012 on SubPass 1")

        -- Mismatched frame parity does NOT affect Dual-Pass eye locking
        VRBridge.setSubPassIndex(0)
        local origParity = VRCompanion.getFrameParity
        VRCompanion.getFrameParity = function() return 1 end
        VRCompanion:updateHudConvergence(0.016)
        assert(math.abs(VRCompanion.currentConvergenceOffset - 0.012) < 0.0001, "subPassIndex must lock to Left eye (+0.012)")

        VRCompanion.getFrameParity = origParity
        VRCompanion:deleteMap()
    )luau", "=testDualPassHudConvergence"), 0);
}

TEST_F(CompanionModTest, DualEyeCameraNodesAndCyclopeanAudioListenerInitializedOnMapLoad) {
    SetupCameraMockGlobals(L);
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap("testMap")

        -- 1. Dual eye camera nodes must be initialized and non-nil
        assert(VRCompanion.cameraNodeLeft ~= nil and VRCompanion.cameraNodeLeft ~= 0,
            "VRCompanion.cameraNodeLeft must be initialized on map load")
        assert(VRCompanion.cameraNodeRight ~= nil and VRCompanion.cameraNodeRight ~= 0,
            "VRCompanion.cameraNodeRight must be initialized on map load")

        -- 2. Left and Right camera nodes must be distinct
        assert(VRCompanion.cameraNodeLeft ~= VRCompanion.cameraNodeRight,
            "cameraNodeLeft and cameraNodeRight must be distinct scene-graph camera nodes")

        -- 3. Dedicated Cyclopean Audio Listener node must be initialized
        assert(VRCompanion.cyclopeanAudioListenerNode ~= nil and VRCompanion.cyclopeanAudioListenerNode ~= 0,
            "VRCompanion.cyclopeanAudioListenerNode must be initialized on map load")

        -- 4. Engine audio listener must be bound to the cyclopean audio listener node
        assert(getAudioListener() == VRCompanion.cyclopeanAudioListenerNode,
            "Engine audio listener must be bound to cyclopeanAudioListenerNode via setAudioListener")
    )luau", "=testDualEyeInit"), 0);
}

TEST_F(CompanionModTest, InCabVehicleCameraAlternatesActiveCameraBasedOnAfrFrameParity) {
    SetupCameraMockGlobals(L);
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    headset.SetHeadPose(simPose);
    oxr->SetIpd(0.064f);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    RegisterTestSetFrameParity(bridge);

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap("testMap")
        VRCompanion:setRenderingMode("AFR")
        VRBridge.latchPendingRenderingMode() -- test stand-in for the proxy's frame-boundary latch
        VRCompanion:set2DViewActive(false)
        VRCompanion.handshakeComplete = true

        local mockVehCam = {
            isInside = true,
            cameraNode = 100,
            origRotY = 0,
            origRotX = 0,
            origRotZ = 0,
            rotY = 0,
            rotX = 0,
            rotZ = 0,
            transX = 0,
            transY = 1.8,
            transZ = 0.5
        }

        local leftCam = VRCompanion.cameraNodeLeft
        local rightCam = VRCompanion.cameraNodeRight
        assert(leftCam ~= nil and rightCam ~= nil and leftCam ~= rightCam)

        -- 1. AFR Parity 0 (Left Eye)
        VRBridge.setFrameParity(0)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        assert(getCamera() == leftCam, "Active camera on Parity 0 must be cameraNodeLeft")

        -- 2. AFR Parity 1 (Right Eye)
        VRBridge.setFrameParity(1)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        assert(getCamera() == rightCam, "Active camera on Parity 1 must be cameraNodeRight")

        -- 3. Alternating back to AFR Parity 0 (Left Eye)
        VRBridge.setFrameParity(0)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        assert(getCamera() == leftCam, "Active camera on next Parity 0 must return to cameraNodeLeft")
    )luau", "=testVehicleCamParitySwitch"), 0);
}

TEST_F(CompanionModTest, DualCameraNodesRetainIndependentTransformHistoryAcrossAfrCycles) {
    SetupCameraMockGlobals(L);
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    headset.SetHeadPose(simPose);
    oxr->SetIpd(0.064f);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    RegisterTestSetFrameParity(bridge);

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap("testMap")
        VRCompanion:setRenderingMode("AFR")
        VRCompanion:set2DViewActive(false)
        VRCompanion.handshakeComplete = true

        local mockVehCam = {
            isInside = true,
            cameraNode = 100,
            transX = 0, transY = 1.8, transZ = 0
        }

        local leftCam = VRCompanion.cameraNodeLeft
        local rightCam = VRCompanion.cameraNodeRight

        -- Frame 0: Left Eye (Parity 0)
        VRBridge.setFrameParity(0)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        local l0_x, l0_y, l0_z = getTranslation(leftCam)

        -- Frame 1: Right Eye (Parity 1)
        VRBridge.setFrameParity(1)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        local r1_x, r1_y, r1_z = getTranslation(rightCam)
        local l1_x, l1_y, l1_z = getTranslation(leftCam)

        -- cameraNodeLeft must retain its previous transform state during right eye frame!
        assert(math.abs(l1_x - l0_x) < 0.0001, "cameraNodeLeft must retain transform history on parity 1")
        assert(math.abs(l1_y - l0_y) < 0.0001, "cameraNodeLeft Y must remain unchanged on parity 1")
        assert(math.abs(l1_z - l0_z) < 0.0001, "cameraNodeLeft Z must remain unchanged on parity 1")

        -- Right eye must have stereoscopic offset relative to left eye
        assert(math.abs(r1_x - l0_x) > 0.01, "Right eye and left eye must have non-zero stereoscopic offset")

        -- Frame 2: Left Eye (Parity 0) with forward motion
        VRBridge.setFrameParity(0)
        mockVehCam.transZ = 0.1
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        local l2_x, l2_y, l2_z = getTranslation(leftCam)
        local r2_x, r2_y, r2_z = getTranslation(rightCam)

        -- cameraNodeRight must retain its transform history during left eye frame!
        assert(math.abs(r2_x - r1_x) < 0.0001, "cameraNodeRight must retain transform history on parity 0")

        -- Single-eye motion vector (l2 - l0): pure translation along movement direction without horizontal jump
        local horizontalDelta = math.abs(l2_x - l0_x)
        assert(horizontalDelta < 0.001, "Eye-stride motion vector must have zero horizontal jump contamination")
    )luau", "=testTransformHistoryRetention"), 0);
}

TEST_F(CompanionModTest, CyclopeanAudioListenerBoundAtNeutralHeadMidpointWithoutInterEyeJitter) {
    SetupCameraMockGlobals(L);
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    headset.SetHeadPose(simPose);
    oxr->SetIpd(0.064f);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    RegisterTestSetFrameParity(bridge);

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap("testMap")
        VRCompanion:setRenderingMode("AFR")
        VRBridge.latchPendingRenderingMode() -- test stand-in for the proxy's frame-boundary latch
        VRCompanion:set2DViewActive(false)
        VRCompanion.handshakeComplete = true

        local mockVehCam = {
            isInside = true,
            cameraNode = 100,
            transX = 0, transY = 1.8, transZ = 0.5
        }

        local audioNode = VRCompanion.cyclopeanAudioListenerNode
        assert(audioNode ~= nil and audioNode ~= 0)

        -- Parity 0 (Left Eye)
        VRBridge.setFrameParity(0)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        local a0_x, a0_y, a0_z = getTranslation(audioNode)
        local leftCam_x, _, _ = getTranslation(VRCompanion.cameraNodeLeft)

        -- Parity 1 (Right Eye)
        VRBridge.setFrameParity(1)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        local a1_x, a1_y, a1_z = getTranslation(audioNode)
        local rightCam_x, _, _ = getTranslation(VRCompanion.cameraNodeRight)

        -- Cyclopean audio listener must be stationary at neutral midpoint between eyes across both parities
        assert(math.abs(a1_x - a0_x) < 0.0001, "Cyclopean audio listener must not hop between eyes across frames")
        assert(math.abs(a1_y - a0_y) < 0.0001, "Audio listener Y must remain stationary")
        assert(math.abs(a1_z - a0_z) < 0.0001, "Audio listener Z must remain stationary")

        -- Cyclopean midpoint X must be at head midpoint (0 in local space) rather than eye offsets
        assert(math.abs(a0_x) < 0.005, "Cyclopean audio listener X must be centered at head pose midpoint")
        assert(math.abs(leftCam_x - a0_x) > 0.01, "Left eye must be offset from cyclopean midpoint")
        assert(math.abs(rightCam_x - a0_x) > 0.01, "Right eye must be offset from cyclopean midpoint")
    )luau", "=testCyclopeanAudioStationary"), 0);
}

TEST_F(CompanionModTest, MonoscopicAnd2DModeBypassesDualCameraSwitching) {
    SetupCameraMockGlobals(L);
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    headset.SetHeadPose(simPose);
    oxr->SetIpd(0.064f);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    RegisterTestSetFrameParity(bridge);

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap("testMap")
        VRCompanion:set2DViewActive(true) -- Enter 2D Floating Window Mode

        local mockVehCam = {
            isInside = true,
            cameraNode = 100,
            transX = 0, transY = 1.8, transZ = 0.5
        }

        -- On parity 0
        VRBridge.setFrameParity(0)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        local cam0 = getCamera()

        -- On parity 1
        VRBridge.setFrameParity(1)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        local cam1 = getCamera()

        -- In 2D mode, camera must NOT alternate to right eye
        assert(cam0 == cam1, "Camera must not alternate across frames in 2D mode (Single-Context Bypass)")
    )luau", "=test2DModeBypassesAlternation"), 0);
}

TEST_F(CompanionModTest, DualPassSimulationFrozenWithIdenticalVehicleAndWorldCoordinates) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.currentCamera = 0
        function _G.setCamera(cam)
            _G.currentCamera = cam
        end

        local mockVehicle = {
            posX = 200.0,
            posZ = 50.0,
            speedMs = 15.0,
            update = function(self, dt)
                local dtSec = (dt > 1.0) and (dt * 0.001) or dt
                self.posX = self.posX + self.speedMs * dtSec
            end
        }

        local mockObject = {
            animTime = 0.0,
            update = function(self, dt)
                local dtSec = (dt > 1.0) and (dt * 0.001) or dt
                self.animTime = self.animTime + dtSec
            end
        }

        _G.FSBaseMission = {
            controlledVehicle = mockVehicle,
            update = function(self, dt)
                mockVehicle:update(dt)
                mockObject:update(dt)
            end
        }

        VRCompanion:loadMap("mapUS")
        VRCompanion.handshakeComplete = true
        VRCompanion.passThroughMode = false
        VRCompanion.headTrackingEnabled = true
        VRCompanion:setRenderingMode("DualPass")
        VRCompanion.cameraNodeLeft = 6001
        VRCompanion.cameraNodeRight = 6002

        -- Pass 0: real dt advances simulation
        VRBridge.setSubPassIndex(0)
        _G.FSBaseMission:update(33.333)

        local p0VehX = mockVehicle.posX
        local p0Anim = mockObject.animTime
        assert(_G.currentCamera == 6001, "Pass 0 camera must be cameraNodeLeft")
        assert(p0VehX > 200.0, "Vehicle must advance on Pass 0")
        assert(p0Anim > 0.0, "Object must advance on Pass 0")

        -- Pass 1: clamped dt = 0 freezes simulation at exact same coordinates
        VRBridge.setSubPassIndex(1)
        _G.FSBaseMission:update(33.333)

        local p1VehX = mockVehicle.posX
        local p1Anim = mockObject.animTime
        assert(_G.currentCamera == 6002, "Pass 1 camera must be cameraNodeRight")

        assert(p0VehX == p1VehX, string.format("Vehicle X must be identical across Pass 0 and 1: P0=%.6f, P1=%.6f", p0VehX, p1VehX))
        assert(p0Anim == p1Anim, string.format("Object animTime must be identical across Pass 0 and 1: P0=%.6f, P1=%.6f", p0Anim, p1Anim))

        VRCompanion:deleteMap()
        _G.FSBaseMission = nil
    )luau", "=testIdenticalCoordinates"), 0);
}

TEST_F(CompanionModTest, DeleteMapCleansUpDualCameraNodesAndCyclopeanAudioListener) {
    SetupCameraMockGlobals(L);
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap("testMap")
        assert(VRCompanion.cameraNodeLeft ~= nil)
        assert(VRCompanion.cameraNodeRight ~= nil)
        assert(VRCompanion.cyclopeanAudioListenerNode ~= nil)

        VRCompanion:deleteMap()
        assert(VRCompanion.cameraNodeLeft == nil, "cameraNodeLeft must be nil after deleteMap")
        assert(VRCompanion.cameraNodeRight == nil, "cameraNodeRight must be nil after deleteMap")
        assert(VRCompanion.cyclopeanAudioListenerNode == nil, "cyclopeanAudioListenerNode must be nil after deleteMap")
    )luau", "=testDeleteMapCleanup"), 0);
}

TEST_F(CompanionModTest, CompanionModFlagsDiscontinuityOnSnapTurnRecenterAnd2DViewToggle) {
    SetupCameraMockGlobals(L);
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    // Initial state: clear any initialization discontinuities
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);
    EXPECT_FALSE(bridge.HasPendingDiscontinuity(0));
    EXPECT_FALSE(bridge.HasPendingDiscontinuity(1));

    // 1. Recenter Action flags discontinuity across VRBridge
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:onRecenterAction()
    )luau", "=testRecenterDiscontinuity"), 0);
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    // 2. 2D/3D Mode toggle flags discontinuity across VRBridge
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:set2DViewActive(true)
    )luau", "=test2DViewToggleDiscontinuity"), 0);
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    // Toggling back to 3D mode also flags discontinuity
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:set2DViewActive(false)
    )luau", "=testRestore3DDiscontinuity"), 0);
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    // 3. Snap Turn locomotion flags discontinuity across VRBridge
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local mockVehCam = {
            isInside = true,
            rotY = 0,
            origRotY = 0
        }
        VRCompanion.getActiveVehicleCamera = function() return mockVehCam end
        local ok, res = VRCompanion:snapTurnLeft()
        assert(ok == true, "Snap turn must succeed")
    )luau", "=testSnapTurnDiscontinuity"), 0);
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
}

TEST_F(CompanionModTest, RenderingModeXmlPersistenceRoundtripAndLoad) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local savedStrings = {}
        _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
        _G.createFolder = function() return true end
        _G.createXMLFile = function() return 1 end
        _G.setXMLFloat = function() end
        _G.setXMLBool = function() end
        _G.setXMLString = function(xml, key, val)
            savedStrings[key] = val
        end
        _G.saveXMLFile = function() return true end
        _G.delete = function() end

        -- 1. Verify saving SequentialDualPass writes "DualPass" to vrSettings.renderingMode
        VRCompanion:setRenderingMode("DualPass")
        VRCompanion:saveConfiguration("dummy.xml")
        assert(savedStrings["vrSettings.renderingMode"] == "DualPass", 
            string.format("Expected 'DualPass' in vrSettings.renderingMode, got '%s'", tostring(savedStrings["vrSettings.renderingMode"])))

        -- 2. Verify saving AFR writes "AFR" to vrSettings.renderingMode
        VRCompanion:setRenderingMode("AFR")
        VRCompanion:saveConfiguration("dummy.xml")
        assert(savedStrings["vrSettings.renderingMode"] == "AFR", 
            string.format("Expected 'AFR' in vrSettings.renderingMode, got '%s'", tostring(savedStrings["vrSettings.renderingMode"])))

        -- 3. Verify loading XML with "DualPass" restores SequentialDualPass
        local mockXmlStore = {
            ["vrSettings.renderingMode"] = "DualPass"
        }
        _G.loadXMLFile = function() return 2 end
        _G.getXMLFloat = function() return nil end
        _G.getXMLBool = function() return nil end
        _G.getXMLString = function(xml, key)
            return mockXmlStore[key]
        end

        VRCompanion:loadConfiguration("settings.xml")
        assert(VRCompanion:getRenderingMode() == "SequentialDualPass", "Must restore SequentialDualPass from XML")
        assert(VRBridge.getRenderingMode() == "SequentialDualPass", "VRBridge must reflect SequentialDualPass")

        -- 4. Verify loading XML with "AFR" restores AFR
        mockXmlStore["vrSettings.renderingMode"] = "AFR"
        VRCompanion:loadConfiguration("settings.xml")
        assert(VRCompanion:getRenderingMode() == "AFR", "Must restore AFR from XML")
        assert(VRBridge.getRenderingMode() == "AFR", "VRBridge must reflect AFR")

        -- 5. Verify map load restores user settings.xml preference
        mockXmlStore["vrSettings.renderingMode"] = "DualPass"
        VRCompanion:loadMap("testMap")
        assert(VRCompanion:getRenderingMode() == "SequentialDualPass", "Map load must restore SequentialDualPass from settings.xml")
        assert(VRBridge.getRenderingMode() == "SequentialDualPass", "VRBridge must reflect SequentialDualPass on map load")
    )luau", "=testRenderingModeXmlRoundtrip"), 0);
}

TEST_F(CompanionModTest, RenderingModeConsoleCommandAndNotificationAlert) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local registeredCommands = {}
        local removedCommands = {}
        _G.addConsoleCommand = function(cmd, desc, funcName, target)
            registeredCommands[cmd] = { desc = desc, funcName = funcName, target = target }
        end
        _G.removeConsoleCommand = function(cmd)
            removedCommands[cmd] = true
        end

        local warningAlerts = {}
        local ingameNotifications = {}
        _G.g_currentMission = {
            showBlinkingWarning = function(self, msg, duration)
                table.insert(warningAlerts, { msg = msg, duration = duration })
            end,
            addIngameNotification = function(self, notifType, msg)
                table.insert(ingameNotifications, { notifType = notifType, msg = msg })
            end
        }

        local savedStrings = {}
        _G.createXMLFile = function() return 1 end
        _G.setXMLFloat = function() end
        _G.setXMLBool = function() end
        _G.setXMLString = function(xml, key, val) savedStrings[key] = val end
        _G.saveXMLFile = function() return true end
        _G.delete = function() end
        _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end

        -- 1. Test loadMap registers vrRenderingMode console command
        VRCompanion:loadMap("testMap")
        assert(registeredCommands["vrRenderingMode"] ~= nil, "vrRenderingMode console command must be registered on map load")
        assert(registeredCommands["vrRenderingMode"].funcName == "consoleCommandSetRenderingMode", 
            "registered callback must be consoleCommandSetRenderingMode")

        -- 2. Query status with empty or 'status' argument
        assert(type(VRCompanion.consoleCommandSetRenderingMode) == "function", "consoleCommandSetRenderingMode must be a function")
        local statusRes = VRCompanion:consoleCommandSetRenderingMode("status")
        assert(string.find(statusRes, "Current Stereo Presentation Mode") ~= nil, "Status response must describe current mode")

        -- 3. Invalid input returns usage
        local invalidRes = VRCompanion:consoleCommandSetRenderingMode("invalid_mode")
        assert(string.find(invalidRes, "Usage: vrRenderingMode") ~= nil, "Invalid mode must return usage instructions")

        -- 4. Switch to DualPass via console command
        warningAlerts = {}
        ingameNotifications = {}
        local dualRes = VRCompanion:consoleCommandSetRenderingMode("dualpass")
        assert(VRCompanion:getRenderingMode() == "SequentialDualPass", "Mode must be SequentialDualPass")
        assert(VRBridge.getRenderingMode() == "SequentialDualPass", "VRBridge must reflect SequentialDualPass")
        assert(savedStrings["vrSettings.renderingMode"] == "DualPass", "User preference must persist to settings.xml")
        assert(string.find(dualRes, "SequentialDualPass") ~= nil or string.find(dualRes, "Dual%-Pass") ~= nil, 
            "Console response must report updated status")
        assert(#warningAlerts > 0 or #ingameNotifications > 0, "In-game notification alert must be displayed on mode switch")

        -- 5. Toggle mode via console command
        warningAlerts = {}
        ingameNotifications = {}
        local toggleRes = VRCompanion:consoleCommandSetRenderingMode("toggle")
        assert(VRCompanion:getRenderingMode() == "AFR", "Mode must toggle to AFR")
        assert(VRBridge.getRenderingMode() == "AFR", "VRBridge must reflect AFR")
        assert(savedStrings["vrSettings.renderingMode"] == "AFR", "Toggled preference must persist to settings.xml")
        assert(#warningAlerts > 0 or #ingameNotifications > 0, "In-game notification alert must be displayed on toggle")

        -- 6. Switch back to afr explicitly
        local afrRes = VRCompanion:consoleCommandSetRenderingMode("afr")
        assert(VRCompanion:getRenderingMode() == "AFR", "Explicit afr command must set AFR")

        -- 7. Test deleteMap removes console command
        VRCompanion:deleteMap()
        assert(removedCommands["vrRenderingMode"] == true, "deleteMap must unregister vrRenderingMode")
    )luau", "=testRenderingModeConsoleCommand"), 0);
}

TEST_F(CompanionModTest, LiveModeSwitchingMaintainsCleanCameraAlignmentDuringDrivingAndOnFoot) {
    SetupCameraMockGlobals(L);
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    headset.SetHeadPose(simPose);
    oxr->SetIpd(0.064f);

    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    XrFovf rightFov{ -0.80f, 0.90f, 0.85f, -0.85f };
    headset.SetEyeFov(0, leftFov);
    headset.SetEyeFov(1, rightFov);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    RegisterTestSetFrameParity(bridge);

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap("testMap")
        VRCompanion:set2DViewActive(false)
        VRCompanion.handshakeComplete = true

        local mockVehCam = {
            isInside = true,
            cameraNode = 301,
            origRotY = 0, origRotX = 0, origRotZ = 0,
            rotY = 0, rotX = 0, rotZ = 0,
            transX = 0, transY = 1.8, transZ = 0.5
        }

        local mockPlayerCam = {
            cameraNode = 401,
            firstPersonCamera = 401,
            yawNode = 402,
            vrHeadTrackingNode = nil,
            transX = 0, transY = 1.7, transZ = 0,
            rotY = 0, rotX = 0, rotZ = 0
        }
        VRCompanion.activePlayerCamera = mockPlayerCam

        local leftCam = VRCompanion.cameraNodeLeft
        local rightCam = VRCompanion.cameraNodeRight
        local audioNode = VRCompanion.cyclopeanAudioListenerNode
        assert(leftCam ~= nil and rightCam ~= nil and audioNode ~= nil)

        VRCompanion:setRenderingMode("AFR")
        VRBridge.latchPendingRenderingMode() -- test stand-in for the proxy's frame-boundary latch

        -- PHASE 1: Vehicle driving in AFR mode
        VRBridge.setFrameParity(0)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        assert(getCamera() == leftCam, "In AFR parity 0, vehicle camera must be Left")
        local l_x, l_y, l_z = getTranslation(leftCam)

        VRBridge.setFrameParity(1)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        assert(getCamera() == rightCam, "In AFR parity 1, vehicle camera must be Right")
        local r_x, r_y, r_z = getTranslation(rightCam)
        assert(math.abs(r_x - l_x) > 0.01, "Left and right vehicle cameras must have stereoscopic offset")

        local afrAudioX, _, _ = getTranslation(audioNode)
        assert(math.abs(afrAudioX) < 0.005, "Cyclopean audio listener must be at head center in AFR")

        -- PHASE 2: Live switch to Sequential Dual-Pass via console command while driving
        VRCompanion:consoleCommandSetRenderingMode("dualpass")
        VRBridge.latchPendingRenderingMode() -- test stand-in for the proxy's frame-boundary latch
        assert(VRCompanion:getRenderingMode() == "SequentialDualPass", "Mode must be SequentialDualPass")
        assert(VRBridge.getRenderingMode() == "SequentialDualPass")
        VRBridge.setFrameParity(0)

        -- SubPass 0 (Left Eye)
        VRBridge.setSubPassIndex(0)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        assert(getCamera() == leftCam, "In DualPass SubPass 0, vehicle camera must be Left")
        local dp_lx, _, _ = getTranslation(leftCam)
        -- In DualPass, projection offset must remain centered on optical axis to match OpenXR symmetric FOV
        local offX0, offY0 = getProjectionOffset(leftCam)
        assert(math.abs(offX0) < 0.0001, "Camera projection X offset must remain centered in DualPass")
        assert(math.abs(offY0) < 0.0001, "Camera projection Y offset must remain centered in DualPass")

        -- When allowObliqueProjection is explicitly enabled in DualPass, asymmetric offsets are applied
        VRCompanion.allowObliqueProjection = true
        VRCompanion:applyCameraProjection(leftCam, 0)
        local oblX, _ = getProjectionOffset(leftCam)
        assert(math.abs(oblX) > 0.001, "Oblique frustum offset must be applied when allowObliqueProjection is true")
        VRCompanion.allowObliqueProjection = false

        -- SubPass 1 (Right Eye)
        VRBridge.setSubPassIndex(1)
        VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
        assert(getCamera() == rightCam, "In DualPass SubPass 1, vehicle camera must be Right")
        local dp_rx, _, _ = getTranslation(rightCam)
        assert(math.abs(dp_rx - dp_lx) > 0.01, "Left and right vehicle cameras must have stereoscopic offset in DualPass")

        -- Audio listener must remain centered during DualPass driving
        local dpAudioX, _, _ = getTranslation(audioNode)
        assert(math.abs(dpAudioX - afrAudioX) < 0.0001, "Audio listener must not shift when switching to DualPass")

        -- PHASE 3: On-foot locomotion in Sequential Dual-Pass
        VRCompanion.getControlledVehicle = function() return nil end

        -- SubPass 0 (Left Eye)
        VRBridge.setSubPassIndex(0)
        VRCompanion:onPlayerCameraUpdate(mockPlayerCam, 0.016)
        assert(getCamera() == leftCam, "In DualPass SubPass 0, player on-foot camera must be Left")
        local foot_lx, _, _ = getTranslation(leftCam)
        local footOffX, footOffY = getProjectionOffset(leftCam)
        assert(math.abs(footOffX) < 0.0001, "On-foot camera projection X offset must remain centered in DualPass")
        assert(math.abs(footOffY) < 0.0001, "On-foot camera projection Y offset must remain centered in DualPass")

        -- SubPass 1 (Right Eye)
        VRBridge.setSubPassIndex(1)
        VRCompanion:onPlayerCameraUpdate(mockPlayerCam, 0.016)
        assert(getCamera() == rightCam, "In DualPass SubPass 1, player on-foot camera must be Right")
        local foot_rx, _, _ = getTranslation(rightCam)
        assert(math.abs(foot_rx - foot_lx) > 0.01, "Left and right on-foot cameras must have stereoscopic offset in DualPass")

        -- Audio listener on foot remains centered
        local footAudioX, _, _ = getTranslation(audioNode)
        assert(math.abs(footAudioX - dpAudioX) < 0.005, "Audio listener must remain centered during on-foot locomotion")

        -- PHASE 4: Live toggle back to AFR while on-foot
        VRCompanion:consoleCommandSetRenderingMode("toggle")
        VRBridge.latchPendingRenderingMode() -- test stand-in for the proxy's frame-boundary latch
        assert(VRCompanion:getRenderingMode() == "AFR", "Mode must toggle back to AFR")
        assert(VRBridge.getRenderingMode() == "AFR")

        VRBridge.setFrameParity(0)
        VRCompanion:onPlayerCameraUpdate(mockPlayerCam, 0.016)
        assert(getCamera() == leftCam, "In AFR parity 0 on-foot, camera must be Left")

        VRBridge.setFrameParity(1)
        VRCompanion:onPlayerCameraUpdate(mockPlayerCam, 0.016)
        assert(getCamera() == rightCam, "In AFR parity 1 on-foot, camera must be Right")

        VRCompanion:deleteMap()
    )luau", "=testLiveModeSwitchingLocomotion"), 0);
}

TEST_F(CompanionModTest, VehicleSpeedAndFerryDtZeroGuardsPreventDivideByZeroException) {
    SetupCameraMockGlobals(L);
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local vehSpeedCallCount = 0
        local ferryCallCount = 0

        Vehicle = {
            updateVehicleSpeed = function(self, dt)
                vehSpeedCallCount = vehSpeedCallCount + 1
                if dt == 0 or dt == nil then
                    error("Divide by zero error")
                end
                return 100 / dt
            end
        }

        Ferry = {
            updatePosition = function(self, dt)
                ferryCallCount = ferryCallCount + 1
                if dt == 0 or dt == nil then
                    error("Divide by zero error")
                end
                return 50 / dt
            end
        }

        VRCompanion:loadMap("testMap")
        assert(VRCompanion.vehicleSpeedHooked == true, "vehicleSpeedHooked must be true after loadMap")

        -- 1. dt = 0 must be safely intercepted
        local ok0, _ = pcall(function() Vehicle:updateVehicleSpeed(0.0) end)
        assert(ok0, "Vehicle:updateVehicleSpeed(0.0) must not throw")
        assert(vehSpeedCallCount == 0, "Original updateVehicleSpeed must not be called when dt is 0")

        -- 2. dt <= 0.00001 must also be intercepted
        local okEps, _ = pcall(function() Vehicle:updateVehicleSpeed(0.000005) end)
        assert(okEps, "Vehicle:updateVehicleSpeed with epsilon dt must not throw")
        assert(vehSpeedCallCount == 0, "Original updateVehicleSpeed must not be called for epsilon dt")

        -- 3. Normal dt passes through
        local spd = Vehicle:updateVehicleSpeed(16.0)
        assert(vehSpeedCallCount == 1, "Original updateVehicleSpeed must be called for valid dt")
        assert(math.abs(spd - (100 / 16.0)) < 0.001)

        -- 4. Ferry dt = 0 must be intercepted
        local okFerry0, _ = pcall(function() Ferry:updatePosition(0.0) end)
        assert(okFerry0, "Ferry:updatePosition(0.0) must not throw")
        assert(ferryCallCount == 0, "Original Ferry:updatePosition must not be called when dt is 0")

        -- 5. Ferry normal dt passes through
        Ferry:updatePosition(10.0)
        assert(ferryCallCount == 1, "Original Ferry:updatePosition must be called for valid dt")

        -- 6. deleteMap cleanly unhooks
        VRCompanion:deleteMap()
        assert(VRCompanion.vehicleSpeedHooked == false, "vehicleSpeedHooked must be false after deleteMap")

        Vehicle = nil
        Ferry = nil
    )luau", "=testVehicleSpeedDtZeroGuards"), 0);
}

TEST_F(CompanionModTest, CameraManagerActiveCameraBindingEliminatesEngineWarnings) {
    SetupCameraMockGlobals(L);
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local rawSetCameraCalled = 0
        local managerBoundCam = nil

        local origSetCamera = _G.setCamera
        _G.setCamera = function(node)
            rawSetCameraCalled = rawSetCameraCalled + 1
            origSetCamera(node)
        end

        _G.g_cameraManager = {
            setActiveCamera = function(self, cam)
                managerBoundCam = cam
            end
        }

        VRCompanion:bindRenderingCamera(9001)
        assert(managerBoundCam == 9001, "g_cameraManager:setActiveCamera must be called with targetCam")
        assert(rawSetCameraCalled == 0, "Raw setCamera must NOT be called when g_cameraManager is available")
        assert(VRCompanion.activeRenderingCamera == 9001, "activeRenderingCamera must be 9001")
        assert(_G.currentCamera == 9001, "_G.currentCamera must be 9001")
        assert(_G.activeCamera == 9001, "_G.activeCamera must be 9001")

        -- Cleanup
        _G.g_cameraManager = nil
        _G.setCamera = origSetCamera
    )luau", "=testCameraManagerActiveCameraBinding"), 0);
}

TEST_F(CompanionModTest, SequentialDualPassSubPass1ExplicitlySynchronizesRightEyeCamera) {
    SetupCameraMockGlobals(L);
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.isValid = true;
    simPose.position = fs25vr::Vector3(0.0f, 0.0f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    headset.SetHeadPose(simPose);
    oxr->SetIpd(0.064f);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(LoadCompanionModChecked(L), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap("testMap")
        VRCompanion:consoleCommandSetRenderingMode("dualpass")
        assert(VRCompanion:getRenderingMode() == "SequentialDualPass")

        local mockVehCam = {
            isInside = true,
            cameraNode = 501,
            origRotY = 0, origRotX = 0, origRotZ = 0,
            rotY = 0, rotX = 0, rotZ = 0,
            transX = 0, transY = 1.8, transZ = 0.5
        }

        local mockVehicle = {
            getActiveCamera = function(self)
                return mockVehCam
            end
        }

        g_currentMission = {
            controlledVehicle = mockVehicle,
            update = function(self, dt)
                -- Even if vehicle update does NOT call VehicleCamera.update on dt=0:
            end
        }

        local leftCam = VRCompanion.cameraNodeLeft
        local rightCam = VRCompanion.cameraNodeRight

        -- Sub-Pass 0: Left Eye
        VRBridge.setSubPassIndex(0)
        VRCompanion:syncActiveEyeCamera(0)
        assert(getCamera() == leftCam, "Sub-Pass 0 must bind Left Camera")
        local l_x, l_y, l_z = getTranslation(leftCam)

        -- Sub-Pass 1: Right Eye (simulation paused / dt=0)
        VRBridge.setSubPassIndex(1)
        VRCompanion:syncActiveEyeCamera(1)
        assert(getCamera() == rightCam, "Sub-Pass 1 must bind Right Camera")
        local r_x, r_y, r_z = getTranslation(rightCam)

        -- Right Eye must have +IPD offset relative to Left Eye
        assert(math.abs((r_x - l_x) - 0.064) < 0.005, string.format("Right eye must have +0.064m IPD offset relative to Left: L=%.4f, R=%.4f", l_x, r_x))

        VRCompanion:deleteMap()
        g_currentMission = nil
    )luau", "=testDualPassSubPass1SyncActiveEyeCamera"), 0);
}















