// The fixture the VRBridge collector and allocation tests (spec #245) share: a Luau state with VRBridge injected, closed
// again after the test, and the way those tests reach a function in VRBridge's table.
#pragma once

#include <gtest/gtest.h>
#include "core/perf_trace.h"
#include "luau/vr_bridge.h"

namespace fs25vr_test {

// The most numbers any VRBridge function takes (reportCameraRecord), so a call with this many reaches every function's
// work and not just its argument check.
constexpr int kNumberArgs = 13;

class VrBridgeInjectedTest : public ::testing::Test {
protected:
    lua_State* L = nullptr;
    fs25vr::VRBridge bridge{nullptr, nullptr};

    void SetUp() override {
        L = luaL_newstate();
        luaL_openlibs(L);
        ASSERT_TRUE(bridge.Inject(L));
    }

    void TearDown() override {
        fs25vr::PerfTrace::Instance().Stop();
        if (L) lua_close(L);
    }

    // Pushes VRBridge.<name>.
    void PushBridgeFunction(const char* name) {
        lua_getglobal(L, "VRBridge");
        lua_getfield(L, -1, name);
        lua_remove(L, -2);
        ASSERT_EQ(lua_type(L, -1), LUA_TFUNCTION) << name;
    }
};

} // namespace fs25vr_test
