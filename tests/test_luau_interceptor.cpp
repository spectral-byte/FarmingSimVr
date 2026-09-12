#include <gtest/gtest.h>
#include "luau/luau_interceptor.h"
#include "luacode.h"
#include <string>

namespace {

static int s_queryCallCount = 0;

__declspec(noinline) int Engine_isHeadTrackingAvailable(lua_State* L) {
    s_queryCallCount++;
    lua_pushboolean(L, 1);
    return 1;
}

int RunLuauString(lua_State* L, const std::string& code) {
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(code.c_str(), code.length(), nullptr, &bytecodeSize);
    if (!bytecode) return -1;
    int loadResult = luau_load(L, "=test_interceptor", bytecode, bytecodeSize, 0);
    free(bytecode);
    if (loadResult != 0) return loadResult;
    return lua_pcall(L, 0, 0, 0);
}

} // anonymous namespace

class LuauInterceptorTest : public ::testing::Test {
protected:
    lua_State* L = nullptr;

    void SetUp() override {
        s_queryCallCount = 0;
        L = luaL_newstate();
        luaL_openlibs(L);

        // Register the simulated engine head tracking query
        lua_pushcclosure(L, &Engine_isHeadTrackingAvailable, "isHeadTrackingAvailable", 0);
        lua_setglobal(L, "isHeadTrackingAvailable");
    }

    void TearDown() override {
        fs25vr::LuauInterceptor::Instance().Detach();
        if (L) {
            lua_close(L);
            L = nullptr;
        }
    }
};

TEST_F(LuauInterceptorTest, InterceptsActiveLuaStateAndInjectsVRBridge) {
    auto& interceptor = fs25vr::LuauInterceptor::Instance();
    interceptor.Detach();

    EXPECT_FALSE(interceptor.IsIntercepted());
    EXPECT_EQ(interceptor.GetActiveState(), nullptr);

    // Before query, VRBridge does not exist
    EXPECT_EQ(RunLuauString(L, "assert(VRBridge == nil)"), 0) << "VRBridge should not exist yet";

    // Attach interceptor detour to the query function
    ASSERT_TRUE(interceptor.Attach(reinterpret_cast<void*>(&Engine_isHeadTrackingAvailable)));

    // Execute Luau script calling the query
    std::string script = R"(
        local available = isHeadTrackingAvailable()
        assert(available == true, "Query must return true")
        assert(VRBridge ~= nil, "VRBridge must now be injected and accessible")
        assert(VRBridge.version == "1.0.0", "VRBridge.version must be accessible")
    )";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau error: " << (err ? err : "unknown");
    }

    EXPECT_TRUE(interceptor.IsIntercepted());
    EXPECT_EQ(interceptor.GetActiveState(), L);
    EXPECT_EQ(s_queryCallCount, 1);

    // Subsequent call should pass through and retain VRBridge without issue
    int status2 = RunLuauString(L, "local a = isHeadTrackingAvailable(); assert(a == true)");
    EXPECT_EQ(status2, 0);
    EXPECT_EQ(s_queryCallCount, 2);

    interceptor.Detach();
}

TEST_F(LuauInterceptorTest, SupportsIsolatedInstanceWithoutSingletonCoupling) {
    auto isolatedBridge = std::make_shared<fs25vr::VRBridge>();
    fs25vr::LuauInterceptor isolatedInterceptor(isolatedBridge);

    ASSERT_TRUE(isolatedInterceptor.Attach(reinterpret_cast<void*>(&Engine_isHeadTrackingAvailable)));
    EXPECT_FALSE(isolatedInterceptor.IsIntercepted());

    int status = RunLuauString(L, "isHeadTrackingAvailable()");
    EXPECT_EQ(status, 0);

    EXPECT_TRUE(isolatedInterceptor.IsIntercepted());
    EXPECT_EQ(isolatedInterceptor.GetActiveState(), L);

    isolatedInterceptor.Detach();
}
