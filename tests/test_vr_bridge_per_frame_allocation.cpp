#include "vr_bridge_fixture.h"
#include "input/virtual_gamepad.h"
#include <cstddef>
#include <string>

namespace {

using fs25vr::VRBridge;
using Entry = VRBridge::LuaFunctionEntry;
using Kind = VRBridge::CallKind;

constexpr int kWarmUpCalls = 5;
constexpr int kMeasuredCalls = 100;
using fs25vr_test::kNumberArgs;

class VrBridgePerFrameAllocationTest : public fs25vr_test::VrBridgeInjectedTest {
protected:
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

    Caller SetUpCaller(const char* name, bool withArguments) {
        lua_settop(L, 0);
        PushBridgeFunction(name);  // slot 1
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

    size_t BytesAllocatedByRepeatedCalls(Caller& caller) {
        for (int i = 0; i < kWarmUpCalls; ++i) caller.Call();
        lua_gc(L, LUA_GCCOLLECT, 0);
        lua_gc(L, LUA_GCSTOP, 0);
        lua_checkstack(L, 128);
        const size_t before = HeapBytes();
        for (int i = 0; i < kMeasuredCalls; ++i) caller.Call();
        const size_t after = HeapBytes();
        lua_gc(L, LUA_GCRESTART, 0);
        return after - before;
    }

    template <typename PushKey>
    size_t BytesAllocatedByRepeatedReads(PushKey pushKey, bool* readNil = nullptr) {
        lua_settop(L, 0);
        lua_getglobal(L, "VRBridge");  // slot 1
        pushKey();                     // slot 2
        auto read = [&] {
            lua_pushvalue(L, 2);
            lua_gettable(L, 1);
            if (readNil) *readNil = lua_isnil(L, -1);
            lua_settop(L, 2);
        };
        for (int i = 0; i < kWarmUpCalls; ++i) read();
        lua_gc(L, LUA_GCCOLLECT, 0);
        lua_gc(L, LUA_GCSTOP, 0);
        lua_checkstack(L, 128);
        const size_t before = HeapBytes();
        for (int i = 0; i < kMeasuredCalls; ++i) read();
        const size_t after = HeapBytes();
        lua_gc(L, LUA_GCRESTART, 0);
        return after - before;
    }
};

}

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
    int measured = 0;
    for (const Entry& entry : VRBridge::kFunctionTable) {
        if (entry.kind != Kind::PerFrame) continue;
        SCOPED_TRACE(std::string("VRBridge.") + entry.name + "()");
        Caller caller = SetUpCaller(entry.name, false);
        lua_pushvalue(L, 1);
        const int status = lua_pcall(L, 0, LUA_MULTRET, 0);
        const bool returnedTable = status == LUA_OK && lua_gettop(L) > 1 && lua_istable(L, -1);
        lua_settop(L, 1);
        if (status != LUA_OK || returnedTable) continue;
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
    lua_getglobal(L, "VRBridge");
    lua_getfield(L, -1, "getVirtualGamepad");
    ASSERT_EQ(lua_type(L, -1), LUA_TFUNCTION);
    ASSERT_NE(lua_getupvalue(L, -1, VRBridge::kAnchorsUpvalue), nullptr) << "the function carries the strings it hands out";
    ASSERT_EQ(lua_type(L, -1), LUA_TTABLE);
    const int anchors = lua_gettop(L);

    ASSERT_GT(fs25vr::VirtualGamepad::Reasons().size(), 0u);
    for (const char* reason : fs25vr::VirtualGamepad::Reasons()) {
        ASSERT_NE(reason, nullptr) << "a reason is missing from the list of Virtual Gamepad reasons";
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

TEST_F(VrBridgePerFrameAllocationTest, TheAnchoredStringsTableHasNoArrayPart) {
    lua_getglobal(L, "VRBridge");
    lua_getfield(L, -1, "getRenderingMode");
    ASSERT_EQ(lua_type(L, -1), LUA_TFUNCTION);
    ASSERT_NE(lua_getupvalue(L, -1, VRBridge::kAnchorsUpvalue), nullptr);
    ASSERT_EQ(lua_type(L, -1), LUA_TTABLE);
    EXPECT_EQ(lua_objlen(L, -1), 0) << "the table holds no integer keys, so it has no array part";
    lua_rawgeti(L, -1, 1);
    EXPECT_EQ(lua_type(L, -1), LUA_TNIL);
}

TEST_F(VrBridgePerFrameAllocationTest, ReadingTheRenderingModeAllocatesNothingInEitherMode) {
    for (fs25vr::VRRenderingMode mode : {fs25vr::VRRenderingMode::AFR, fs25vr::VRRenderingMode::SequentialDualPass}) {
        SCOPED_TRACE(mode == fs25vr::VRRenderingMode::AFR ? "AFR" : "SequentialDualPass");
        bridge.SetRenderingMode(mode);

        Caller caller = SetUpCaller("getRenderingMode", false);
        EXPECT_EQ(BytesAllocatedByRepeatedCalls(caller), 0u) << "VRBridge.getRenderingMode()";

        EXPECT_EQ(BytesAllocatedByRepeatedReads([&] { lua_pushstring(L, "renderingMode"); }), 0u) << "VRBridge.renderingMode";
    }
}

TEST_F(VrBridgePerFrameAllocationTest, ReadingANumberKeyAllocatesNothing) {
    for (double key : {1.0, 2.5, -7.0}) {
        SCOPED_TRACE(key);
        bool readNil = false;
        EXPECT_EQ(BytesAllocatedByRepeatedReads([&] { lua_pushnumber(L, key); }, &readNil), 0u);
        EXPECT_TRUE(readNil) << "an unknown key reads nil";
    }
}

TEST_F(VrBridgePerFrameAllocationTest, ReadingAnInjectorPropertyAllocatesNothing) {
    const char* const keys[] = {
        "requiresAfrFallback", "frameParity", "frameIndex", "renderingMode", "renderingModeIndex", "subPassIndex",
        "ipd", "isCompanionActive", "isMenuVisible", "is2DViewActive", "hasPendingDiscontinuity", "noSuchProperty",
    };
    for (const char* key : keys) {
        SCOPED_TRACE(key);
        EXPECT_EQ(BytesAllocatedByRepeatedReads([&] { lua_pushstring(L, key); }), 0u);
    }
}
