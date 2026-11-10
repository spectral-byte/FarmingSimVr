// The game's garbage collector around VRBridge calls (spec #245, ADR-0002).
//
// The Injector links its own copy of Luau and runs it on the game's heap, and a collection step started from that copy
// has crashed the game. Every function in VRBridge's function table is therefore marked per-frame or occasional:
// an occasional one holds the collector off for the call and leaves it as it found it, a per-frame one never touches it.
// These tests walk the whole table and watch the Luau state from outside: whether the collector runs, and whether the
// destructor of an unreachable userdata has run.
#include "vr_bridge_fixture.h"
#include <algorithm>
#include <string>
#include <vector>

namespace {

using fs25vr::VRBridge;
using Entry = VRBridge::LuaFunctionEntry;
using Kind = VRBridge::CallKind;

bool g_destructorRan = false;
void MarkDestroyed(void*) { g_destructorRan = true; }

// The two ways every function is called: with nothing (a function that needs a number raises a Lua error) and with a
// full set of numbers (it does its work).
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
        // Whatever was left on the stack above the function: results or an error message.
        lua_settop(L, 1);
        return status;
    }

    // Leaves a collection due: unreachable userdata with a destructor exists, and the next allocation on this state
    // would start a collection step, each step finishing a phase and the next allocation starting the next one, so that
    // a handful of allocations are enough to free the userdata. The function under test is on the stack.
    void MakeCollectionDue(bool collectorRunning) {
        lua_gc(L, LUA_GCCOLLECT, 0);
        lua_gc(L, LUA_GCSTOP, 0);
        lua_checkstack(L, 64);  // a full collection shrinks the stack; growing it again must not happen inside the call
        lua_gc(L, LUA_GCSETSTEPMUL, 1000000);
        lua_newuserdatadtor(L, 16, &MarkDestroyed);
        lua_pop(L, 1);
        g_destructorRan = false;
        if (collectorRunning) lua_gc(L, LUA_GCRESTART, 0);
    }

    // What the game does next: it allocates. Returns whether that was enough to free the userdata.
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

} // anonymous namespace

TEST_F(VrBridgeCollectorTest, EveryFunctionInTheFunctionTableIsMarkedPerFrameOrOccasional) {
    ASSERT_GT(std::size(VRBridge::kFunctionTable), 0u);
    for (const Entry& entry : VRBridge::kFunctionTable) {
        EXPECT_TRUE(entry.kind == Kind::PerFrame || entry.kind == Kind::Occasional)
            << "VRBridge." << entry.name << " is not marked per-frame or occasional in the function table";
    }
}

TEST_F(VrBridgeCollectorTest, TheGamesNextAllocationsCollectWhatIsDueOnceNoCallIsHolding) {
    // The arrangement the other tests rely on: with the collector running and a collection due, the game's own
    // allocations free the unreachable userdata. Without this, "it has not run" below would prove nothing.
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
            Call(args);  // once to settle the stack and string tables the call grows on first use
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
    // A function that checks its argument raises a Lua error when called with none. The table finds them, so a function
    // added later that raises is covered by default.
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
    // The occasional functions known to check a number. A function that starts to raise is covered by the loop above;
    // one of these that stops raising makes this fail, so that the list is kept honest.
    for (const char* name : {"setDlssMvScale", "setDlssSharpness", "setDlssPreset", "setDlssSdpMvScale", "setDlssNearClip"}) {
        EXPECT_NE(std::find(raisers.begin(), raisers.end(), name), raisers.end()) << name << " no longer raises without its argument";
    }
}

TEST_F(VrBridgeCollectorTest, APerFrameCallNeverTouchesTheCollector) {
    // Stopping and restarting the collector makes a step due at the next allocation. With headroom before the next
    // collection, a call that left the collector alone leaves the game's following allocations free of one.
    int perFrame = 0;
    for (const Entry& entry : VRBridge::kFunctionTable) {
        if (entry.kind != Kind::PerFrame) continue;
        ++perFrame;
        for (Args args : {Args::None, Args::Numbers}) {
            SCOPED_TRACE(Describe(entry, args, true));
            lua_checkstack(L, 64);
            PushBridgeFunction(entry.name);
            Call(args);
            lua_gc(L, LUA_GCCOLLECT, 0);  // the threshold is now twice the live heap
            lua_checkstack(L, 64);
            lua_gc(L, LUA_GCSETSTEPMUL, 1000000);
            lua_newuserdatadtor(L, 16, &MarkDestroyed);
            lua_pop(L, 1);
            g_destructorRan = false;

            Call(args);

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
