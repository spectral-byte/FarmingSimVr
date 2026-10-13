#pragma once

// Shared fixture for tests that run the shipped Companion Mod (mods/FS25_VRCompanion/src) inside a
// Luau state: log capture, chunk runner, mod loader, camera mock globals, the CompanionModTest
// fixture and the time/frame helpers. Per-feature test files include this header and add their own
// TEST_F(CompanionModTest, ...) cases. Everything is inline so any number of translation units can
// include it.

#include <gtest/gtest.h>
#include "luau/vr_bridge.h"
#include "fake_openxr_runtime.h"
#include "luacode.h"
#include "luau_test_runner.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
inline std::vector<std::string> s_capturedLogs;

inline int MockPrint(lua_State* L) {
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

inline std::string ReadFile(const std::string& path) {
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
inline int LoadCompanionModChecked(lua_State* L) {
    std::string err;
    int status = LoadCompanionMod(L, &err);
    if (status != 0) {
        fprintf(stderr, "\n[Companion Mod load error]: %s\n", err.c_str());
    }
    return status;
}
inline int RunLuauChunk(lua_State* L, const std::string& code, const char* chunkName) {
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

// Evaluates a Lua expression and returns tostring() of its value, e.g. EvalLua(L, "VRCompanion:getPointerHand()")
// is "1" or "nil". Lets tests read mod state through its public surface without writing a chunk per check.
inline std::string EvalLua(lua_State* L, const std::string& expression) {
    const std::string chunk = "_G.__evalResult = tostring(" + expression + ")";
    if (RunLuauChunk(L, chunk, "=eval") != 0) {
        return "<error>";
    }
    lua_getglobal(L, "__evalResult");
    const char* value = lua_tostring(L, -1);
    std::string result = value ? value : "<none>";
    lua_pop(L, 1);
    return result;
}

inline void SetupCameraMockGlobals(lua_State* L) {
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

class CompanionModTest : public ::testing::Test {
protected:
    lua_State* L = nullptr;
    double gameTimeMs_ = 0.0;

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

    // ---- Driving time and frames --------------------------------------------------------------
    // The mod reads the game clock through VRCompanion:getCurrentTimeMs(), which is the global
    // g_time (milliseconds) and falls back to the real clock when g_time is nil. g_time is not
    // defined until a test calls SetGameTime/AdvanceGameTime, so tests that never touch it behave
    // exactly as before. Tests that depend on time (the 3 s in-use window, debounces) set it and
    // advance it by hand, so they never depend on wall-clock time.
    void SetGameTime(double ms) {
        gameTimeMs_ = ms;
        lua_pushnumber(L, ms);
        lua_setglobal(L, "g_time");
    }
    void AdvanceGameTime(double ms) { SetGameTime(gameTimeMs_ + ms); }
    double GameTime() const { return gameTimeMs_; }

    // One game frame: the game clock advances by dtMs, the headset (when given) crosses a frame
    // boundary so the Injector syncs the Motion Controller actions and locates the hands, and then the
    // Companion Mod's VRCompanion:update(dt) runs. Fails the test if the update raises an error.
    void NextFrame(fs25vr::FakeTrackedHeadset* headset = nullptr, double dtMs = 16.667) {
        AdvanceGameTime(dtMs);
        if (headset) headset->NextFrame();
        lua_getglobal(L, "VRCompanion");
        lua_getfield(L, -1, "update");
        lua_pushvalue(L, -2);
        lua_pushnumber(L, dtMs);
        if (lua_pcall(L, 2, 0, 0) != 0) {
            const char* err = lua_tostring(L, -1);
            ADD_FAILURE() << "VRCompanion:update raised: " << (err ? err : "unknown error");
            lua_pop(L, 1);
        }
        lua_pop(L, 1);  // VRCompanion
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

