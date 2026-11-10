// What VRBridge's per-frame functions allocate (spec #245, ADR-0002).
//
// The Injector links its own copy of Luau and runs it on the game's heap, and a collection step started from that copy
// has crashed the game. A function the Companion Mod calls on every update or render therefore allocates nothing once
// the caller's reusable tables hold their fields: it cannot start a collection, and it adds no garbage for the game to
// collect. These tests walk VRBridge's whole function table and watch the Luau state from outside: the bytes it holds.
#include <gtest/gtest.h>
#include "core/perf_trace.h"
#include "input/virtual_gamepad.h"
#include "luau/vr_bridge.h"
#include <cstddef>
#include <string>

namespace {

using fs25vr::VRBridge;
using Entry = VRBridge::LuaFunctionEntry;
using Kind = VRBridge::CallKind;

constexpr int kWarmUpCalls = 5;
constexpr int kMeasuredCalls = 100;
constexpr int kNumberArgs = 13;  // the most numbers any function takes (reportCameraRecord)

class VrBridgePerFrameAllocationTest : public ::testing::Test {
protected:
    lua_State* L = nullptr;
    VRBridge bridge{nullptr, nullptr};

    void SetUp() override {
        L = luaL_newstate();
        luaL_openlibs(L);
        ASSERT_TRUE(bridge.Inject(L));
    }

    void TearDown() override {
        fs25vr::PerfTrace::Instance().Stop();
        if (L) lua_close(L);
    }

    // Everything the Luau state holds on the heap, garbage included.
    size_t HeapBytes() const {
        return static_cast<size_t>(lua_gc(L, LUA_GCCOUNT, 0)) * 1024u + static_cast<size_t>(lua_gc(L, LUA_GCCOUNTB, 0));
    }

    // What a caller that keeps its tables does with a call: the arguments (numbers, then the table it reuses) are
    // copied from fixed stack slots, and a table the call returns replaces the one it passes next time.
    struct Caller {
        lua_State* L;
        int argc;       // arguments passed
        int tableSlot;  // stack slot of the reused table, 0 when none is passed
        int base;       // stack top between calls

        // Returns the pcall status; the results are dropped, a returned table is kept as the next call's table.
        int Call() {
            lua_pushvalue(L, 1);
            for (int i = 0; i < argc; ++i) lua_pushvalue(L, 2 + i);
            const int status = lua_pcall(L, argc, LUA_MULTRET, 0);
            if (status == LUA_OK && tableSlot != 0 && lua_gettop(L) > base && lua_istable(L, -1)) {
                lua_pushvalue(L, -1);
                lua_replace(L, tableSlot);
            }
            lua_settop(L, base);
            return status;
        }
    };

    // Pushes VRBridge.<name> into slot 1, then the arguments into the next slots.
    Caller SetUpCaller(const char* name, bool withArguments) {
        lua_settop(L, 0);
        lua_getglobal(L, "VRBridge");
        lua_getfield(L, -1, name);
        lua_remove(L, -2);
        EXPECT_EQ(lua_type(L, 1), LUA_TFUNCTION) << name;
        Caller caller{L, 0, 0, 1};
        if (withArguments) {
            for (int i = 0; i < kNumberArgs; ++i) lua_pushnumber(L, 1.0);
            lua_newtable(L);
            caller.argc = kNumberArgs + 1;
            caller.tableSlot = 2 + kNumberArgs;
            caller.base = 2 + kNumberArgs;
        }
        return caller;
    }

    // The bytes the state holds after the call has run kMeasuredCalls times with the collector stopped, less the bytes
    // before: what the calls allocated and kept. A full collection first, so that what the call needs and does not
    // keep alive (a string only it references) is made again, as it is in the game between two frames.
    size_t BytesAllocatedByRepeatedCalls(Caller& caller) {
        for (int i = 0; i < kWarmUpCalls; ++i) caller.Call();
        lua_gc(L, LUA_GCCOLLECT, 0);
        lua_gc(L, LUA_GCSTOP, 0);
        lua_checkstack(L, 128);  // a full collection shrinks the stack; growing it again is not the call's doing
        const size_t before = HeapBytes();
        for (int i = 0; i < kMeasuredCalls; ++i) caller.Call();
        const size_t after = HeapBytes();
        lua_gc(L, LUA_GCRESTART, 0);
        return after - before;
    }
};

} // anonymous namespace

TEST_F(VrBridgePerFrameAllocationTest, EveryPerFrameFunctionCalledWithReusedTablesAllocatesNothing) {
    int measured = 0;
    for (const Entry& entry : VRBridge::kFunctionTable) {
        if (entry.kind != Kind::PerFrame) continue;
        SCOPED_TRACE(std::string("VRBridge.") + entry.name + "(numbers, a reused table)");
        Caller caller = SetUpCaller(entry.name, true);
        ASSERT_EQ(caller.Call(), LUA_OK) << "a call with numbers and a table raises a Lua error";

        EXPECT_EQ(BytesAllocatedByRepeatedCalls(caller), 0u);
        ++measured;
    }
    EXPECT_GT(measured, 0);
}

TEST_F(VrBridgePerFrameAllocationTest, EveryPerFrameFunctionThatTakesNoArgumentsAndReturnsNoTableAllocatesNothing) {
    // The Companion Mod calls a getter such as getRenderingMode or getVirtualGamepad with nothing. A function that
    // hands back a table builds one when it is given none; the test above passes it the table to fill instead.
    int measured = 0;
    for (const Entry& entry : VRBridge::kFunctionTable) {
        if (entry.kind != Kind::PerFrame) continue;
        SCOPED_TRACE(std::string("VRBridge.") + entry.name + "()");
        Caller caller = SetUpCaller(entry.name, false);
        lua_pushvalue(L, 1);
        const int status = lua_pcall(L, 0, LUA_MULTRET, 0);
        const bool returnedTable = status == LUA_OK && lua_gettop(L) > 1 && lua_istable(L, -1);
        lua_settop(L, 1);
        if (status != LUA_OK || returnedTable) continue;  // needs an argument, or fills a table
        EXPECT_EQ(BytesAllocatedByRepeatedCalls(caller), 0u);
        ++measured;
    }
    EXPECT_GT(measured, 0);
}

TEST_F(VrBridgePerFrameAllocationTest, TheVirtualGamepadStatusAllocatesNothingWhetherThePadIsWantedOrNot) {
    for (bool wanted : {false, true}) {
        SCOPED_TRACE(wanted ? "pad wanted" : "pad not wanted");
        lua_settop(L, 0);
        lua_getglobal(L, "VRBridge");
        lua_getfield(L, -1, "setVirtualGamepad");
        lua_pushboolean(L, wanted ? 1 : 0);
        lua_pushboolean(L, 1);
        ASSERT_EQ(lua_pcall(L, 2, 0, 0), LUA_OK);

        Caller caller = SetUpCaller("getVirtualGamepad", false);
        EXPECT_EQ(BytesAllocatedByRepeatedCalls(caller), 0u);
    }
}

TEST_F(VrBridgePerFrameAllocationTest, EveryReasonTheVirtualGamepadCanGiveReachesLuaWithoutAllocating) {
    // Some of the reasons are longer than a short string, which Luau does not intern: each push would make a new one.
    lua_getglobal(L, "VRBridge");
    lua_getfield(L, -1, "getVirtualGamepad");
    ASSERT_EQ(lua_type(L, -1), LUA_TFUNCTION);
    ASSERT_NE(lua_getupvalue(L, -1, 2), nullptr) << "the function carries the strings it hands out";
    ASSERT_EQ(lua_type(L, -1), LUA_TTABLE);
    const int anchors = lua_gettop(L);

    for (const char* reason : fs25vr::VirtualGamepad::Reasons()) {
        SCOPED_TRACE(reason);
        lua_gc(L, LUA_GCCOLLECT, 0);
        lua_gc(L, LUA_GCSTOP, 0);
        lua_checkstack(L, 64);
        const size_t before = HeapBytes();

        VRBridge::PushAnchoredString(L, anchors, reason);

        EXPECT_EQ(HeapBytes(), before);
        EXPECT_STREQ(lua_tostring(L, -1), reason);
        lua_pop(L, 1);
        lua_gc(L, LUA_GCRESTART, 0);
    }
}

TEST_F(VrBridgePerFrameAllocationTest, ReadingAnInjectorPropertyAllocatesNothing) {
    // VRBridge.__index answers the properties the table does not hold (the Companion Mod reads requiresAfrFallback,
    // frameParity and ipd through it), and any other name with nil. It is registered outside the function table.
    const char* const keys[] = {
        "requiresAfrFallback", "frameParity", "frameIndex", "renderingMode", "renderingModeIndex", "subPassIndex",
        "ipd", "isCompanionActive", "isMenuVisible", "is2DViewActive", "hasPendingDiscontinuity", "noSuchProperty",
    };
    for (const char* key : keys) {
        SCOPED_TRACE(key);
        lua_settop(L, 0);
        lua_getglobal(L, "VRBridge");  // slot 1
        lua_pushstring(L, key);        // slot 2, kept alive here as it is by the caller's own code
        auto read = [&] {
            lua_pushvalue(L, 2);
            lua_gettable(L, 1);
            lua_settop(L, 2);
        };
        for (int i = 0; i < kWarmUpCalls; ++i) read();
        lua_gc(L, LUA_GCCOLLECT, 0);
        lua_gc(L, LUA_GCSTOP, 0);
        lua_checkstack(L, 128);
        const size_t before = HeapBytes();
        for (int i = 0; i < kMeasuredCalls; ++i) read();
        EXPECT_EQ(HeapBytes(), before);
        lua_gc(L, LUA_GCRESTART, 0);
    }
}
