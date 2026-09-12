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
