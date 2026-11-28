#include "vr_bridge_fixture.h"
#include "luacode.h"
#include <algorithm>
#include <string>
#include <vector>

namespace {

using fs25vr::VRBridge;
using Entry = VRBridge::LuaFunctionEntry;
using Kind = VRBridge::CallKind;

bool g_destructorRan = false;
void MarkDestroyed(void*) { g_destructorRan = true; }

enum class Args { None, Numbers };

using fs25vr_test::kNumberArgs;

class VrBridgeCollectorTest : public fs25vr_test::VrBridgeInjectedTest {
protected:
    void SetUp() override {
        g_destructorRan = false;
        VrBridgeInjectedTest::SetUp();
    }

    bool CollectorRunning() const { return lua_gc(L, LUA_GCISRUNNING, 0) != 0; }

    int Call(Args args) {
        lua_pushvalue(L, -1);
        if (args == Args::Numbers) {
            for (int i = 0; i < kNumberArgs; ++i) lua_pushnumber(L, 1.0);
        }
        const int status = lua_pcall(L, args == Args::Numbers ? kNumberArgs : 0, LUA_MULTRET, 0);
        lua_settop(L, 1);
        return status;
    }

    void MakeCollectionDue(bool collectorRunning) {
        lua_gc(L, LUA_GCCOLLECT, 0);
        lua_gc(L, LUA_GCSTOP, 0);
        lua_checkstack(L, 64);
        lua_gc(L, LUA_GCSETSTEPMUL, 1000000);
        lua_newuserdatadtor(L, 16, &MarkDestroyed);
        lua_pop(L, 1);
        g_destructorRan = false;
        if (collectorRunning) lua_gc(L, LUA_GCRESTART, 0);
    }

    bool GameAllocatesUntilTheUserdataIsFreed() {
        for (int i = 0; i < 200 && !g_destructorRan; ++i) {
            lua_createtable(L, 0, 4);
            lua_pop(L, 1);
        }
        return g_destructorRan;
    }
};

std::string Describe(const Entry& entry, Args args, bool collectorRunning) {
    return std::string("VRBridge.") + entry.name + (args == Args::None ? "()" : "(numbers)") +
           (collectorRunning ? " with a running collector" : " with a stopped collector");
}

}

TEST_F(VrBridgeCollectorTest, EveryFunctionInTheFunctionTableIsMarkedPerFrameOrOccasional) {
    ASSERT_GT(std::size(VRBridge::kFunctionTable), 0u);
    for (const Entry& entry : VRBridge::kFunctionTable) {
        EXPECT_TRUE(entry.kind == Kind::PerFrame || entry.kind == Kind::Occasional)
            << "VRBridge." << entry.name << " is not marked per-frame or occasional in the function table";
    }
}

TEST_F(VrBridgeCollectorTest, TheGamesNextAllocationsCollectWhatIsDueOnceNoCallIsHolding) {
    lua_pushnil(L);
    MakeCollectionDue(true);

    EXPECT_TRUE(GameAllocatesUntilTheUserdataIsFreed());
}

TEST_F(VrBridgeCollectorTest, AnOccasionalCallLeavesARunningCollectorRunningAndStartsNoCollection) {
    int occasional = 0;
    for (const Entry& entry : VRBridge::kFunctionTable) {
        if (entry.kind == Kind::PerFrame) continue;
        ++occasional;
        for (Args args : {Args::None, Args::Numbers}) {
            SCOPED_TRACE(Describe(entry, args, true));
            lua_checkstack(L, 64);
            PushBridgeFunction(entry.name);
            Call(args);
            MakeCollectionDue(true);

            Call(args);

            EXPECT_TRUE(CollectorRunning()) << "the game's collector runs again after the call";
            EXPECT_FALSE(g_destructorRan) << "a collection ran during the call";
            EXPECT_TRUE(GameAllocatesUntilTheUserdataIsFreed()) << "the collection runs from the game's own next allocation";
            lua_settop(L, 0);
        }
    }
    EXPECT_GT(occasional, 0);
}

TEST_F(VrBridgeCollectorTest, AnOccasionalCallLeavesAStoppedCollectorStopped) {
    for (const Entry& entry : VRBridge::kFunctionTable) {
        if (entry.kind == Kind::PerFrame) continue;
        for (Args args : {Args::None, Args::Numbers}) {
            SCOPED_TRACE(Describe(entry, args, false));
            lua_checkstack(L, 64);
            PushBridgeFunction(entry.name);
            Call(args);
            MakeCollectionDue(false);

            Call(args);

            EXPECT_FALSE(CollectorRunning()) << "a collector the game had stopped is not started by the call";
            EXPECT_FALSE(g_destructorRan) << "a collection ran during the call";
            lua_settop(L, 0);
        }
    }
}

TEST_F(VrBridgeCollectorTest, AnOccasionalCallThatEndsWithALuaErrorLeavesTheCollectorAsItFoundIt) {
    std::vector<std::string> raisers;
    for (const Entry& entry : VRBridge::kFunctionTable) {
        if (entry.kind != Kind::Occasional) continue;
        lua_checkstack(L, 64);
        PushBridgeFunction(entry.name);
        const bool raises = Call(Args::None) != LUA_OK;
        lua_settop(L, 0);
        if (!raises) continue;
        raisers.push_back(entry.name);

        for (bool collectorRunning : {true, false}) {
            SCOPED_TRACE(Describe(entry, Args::None, collectorRunning));
            lua_checkstack(L, 64);
            PushBridgeFunction(entry.name);
            Call(Args::None);
            MakeCollectionDue(collectorRunning);

            ASSERT_NE(Call(Args::None), LUA_OK) << "the call without its argument raises a Lua error";

            EXPECT_EQ(CollectorRunning(), collectorRunning);
            EXPECT_FALSE(g_destructorRan) << "a collection ran during the call";
            lua_settop(L, 0);
        }
    }
    for (const char* name : {"setDlssMvScale", "setDlssSharpness", "setDlssPreset", "setDlssSdpMvScale", "setDlssNearClip"}) {
        EXPECT_NE(std::find(raisers.begin(), raisers.end(), name), raisers.end()) << name << " no longer raises without its argument";
    }
}

TEST_F(VrBridgeCollectorTest, APerFrameCallNeverTouchesTheCollector) {
    int perFrame = 0;
    for (const Entry& entry : VRBridge::kFunctionTable) {
        if (entry.kind != Kind::PerFrame) continue;
        ++perFrame;
        for (Args args : {Args::None, Args::Numbers}) {
            if (args == Args::None && std::string(entry.name) == "reportCameraRecord") continue;
            SCOPED_TRACE(Describe(entry, args, true));
            lua_checkstack(L, 64);
            PushBridgeFunction(entry.name);
            int tableSlot = 0;
            lua_pushvalue(L, 1);
            int warmupArgc = 0;
            if (args == Args::Numbers) {
                for (int i = 0; i < kNumberArgs; ++i) lua_pushnumber(L, 1.0);
                warmupArgc = kNumberArgs;
            }
            const int warmupStatus = lua_pcall(L, warmupArgc, LUA_MULTRET, 0);
            if (warmupStatus == LUA_OK && lua_gettop(L) > 1 && lua_istable(L, -1)) {
                if (args == Args::None) {
                    lua_settop(L, 0);
                    continue;
                }
                lua_pushvalue(L, -1);
                lua_replace(L, 2);
                lua_settop(L, 2);
                tableSlot = 2;
            } else {
                lua_settop(L, 1);
            }

            lua_gc(L, LUA_GCCOLLECT, 0);  // the threshold is now twice the live heap
            lua_checkstack(L, 64);
            lua_gc(L, LUA_GCSETSTEPMUL, 1000000);
            lua_newuserdatadtor(L, 16, &MarkDestroyed);
            lua_pop(L, 1);
            g_destructorRan = false;

            lua_pushvalue(L, 1);
            int callArgc = 0;
            if (args == Args::Numbers) {
                for (int i = 0; i < kNumberArgs; ++i) lua_pushnumber(L, 1.0);
                callArgc = kNumberArgs;
                if (tableSlot != 0) {
                    lua_pushvalue(L, tableSlot);
                    ++callArgc;
                }
            }
            ASSERT_EQ(lua_pcall(L, callArgc, LUA_MULTRET, 0), LUA_OK);
            lua_settop(L, tableSlot ? tableSlot : 1);

            EXPECT_TRUE(CollectorRunning());
            for (int i = 0; i < 40; ++i) {
                lua_createtable(L, 0, 4);
                lua_pop(L, 1);
            }
            EXPECT_FALSE(g_destructorRan) << "a per-frame call made a collection due";
            lua_settop(L, 0);
            lua_gc(L, LUA_GCCOLLECT, 0);
        }
    }
    EXPECT_GT(perFrame, 0);
}

TEST_F(VrBridgeCollectorTest, APerFrameCallLeavesAStoppedCollectorStopped) {
    lua_gc(L, LUA_GCSTOP, 0);
    for (const Entry& entry : VRBridge::kFunctionTable) {
        if (entry.kind != Kind::PerFrame) continue;
        SCOPED_TRACE(std::string("VRBridge.") + entry.name);
        lua_checkstack(L, 64);
        PushBridgeFunction(entry.name);

        Call(Args::None);
        Call(Args::Numbers);

        EXPECT_FALSE(CollectorRunning());
        lua_settop(L, 0);
    }
}

TEST_F(VrBridgeCollectorTest, ColdPathPerFrameTableGettersAndCameraRecordValidationHoldTheCollectorWhenCollectionIsDue) {
    struct ColdCase {
        const char* name;
        std::vector<double> numbers;
        bool passIncompleteTable;
        bool expectError;
    };
    const ColdCase cases[] = {
        {"getHeadPose", {}, false, false},
        {"getHeadPose", {}, true, false},
        {"getHandState", {0.0}, false, false},
        {"getHandState", {0.0}, true, false},
        {"getEyeProjection", {0.0}, false, false},
        {"getEyeProjection", {0.0}, true, false},
        {"getEyePose", {0.0}, false, false},
        {"getEyePose", {0.0}, true, false},
        {"getPanelHit", {0.0}, false, false},
        {"getPanelHit", {0.0}, true, false},
        {"getPanelHitAlongRay", {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0}, false, false},
        {"getPanelHitAlongRay", {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0}, true, false},
        {"reportCameraRecord", {0.0, 1.0, 2.0}, false, true},
    };

    const char* loopCode = "return function(fn, ...) for i = 1, 8 do fn(...) end end";
    size_t loopBytecodeSize = 0;
    char* loopBytecode = luau_compile(loopCode, std::strlen(loopCode), nullptr, &loopBytecodeSize);
    ASSERT_NE(loopBytecode, nullptr);

    for (const ColdCase& c : cases) {
        for (bool collectorRunning : {true, false}) {
            SCOPED_TRACE(std::string("VRBridge.") + c.name + (c.passIncompleteTable ? "(incompleteTable)" : "(cold)") +
                         (collectorRunning ? " running" : " stopped"));
            lua_checkstack(L, 64);
            if (c.expectError) {
                PushBridgeFunction(c.name);
                MakeCollectionDue(collectorRunning);
                for (int call = 0; call < 8; ++call) {
                    lua_pushvalue(L, 1);
                    int argc = 0;
                    for (double n : c.numbers) {
                        lua_pushnumber(L, n);
                        ++argc;
                    }
                    EXPECT_NE(lua_pcall(L, argc, LUA_MULTRET, 0), LUA_OK);
                    lua_settop(L, 1);
                }
            } else {
                ASSERT_EQ(luau_load(L, "=cold_loop", loopBytecode, loopBytecodeSize, 0), 0);
                ASSERT_EQ(lua_pcall(L, 0, 1, 0), LUA_OK);
                PushBridgeFunction(c.name);
                for (double n : c.numbers) lua_pushnumber(L, n);
                if (c.passIncompleteTable) lua_createtable(L, 0, 1);
                const int argCount = 1 + static_cast<int>(c.numbers.size()) + (c.passIncompleteTable ? 1 : 0);
                MakeCollectionDue(collectorRunning);
                EXPECT_EQ(lua_pcall(L, argCount, 0, 0), LUA_OK);
            }

            EXPECT_EQ(CollectorRunning(), collectorRunning);
            EXPECT_FALSE(g_destructorRan) << "a collection ran during the cold-path call";
            if (collectorRunning) {
                EXPECT_TRUE(GameAllocatesUntilTheUserdataIsFreed());
            }
            lua_settop(L, 0);
        }
    }
    free(loopBytecode);
}
