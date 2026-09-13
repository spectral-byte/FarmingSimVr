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

TEST_F(LuauInterceptorTest, FindQueryFunctionInTextResolvesPairedFunctionAcrossRegisters) {
    // Construct mock buffer (text + rdata)
    std::vector<uint8_t> mockBuffer(1024, 0x90);
    const size_t strOffset = 0x200;
    const char strData[] = "isHeadTrackingAvailable";
    std::memcpy(&mockBuffer[strOffset], strData, sizeof(strData));
    const uint8_t* mockString = &mockBuffer[strOffset];

    // 1. Mock function entry at offset 0x20
    const size_t fnOffset = 0x20;
    mockBuffer[fnOffset] = 0x48;     // sub rsp, 0x28
    mockBuffer[fnOffset + 1] = 0x83;
    mockBuffer[fnOffset + 2] = 0xEC;
    mockBuffer[fnOffset + 3] = 0x28;

    // 2. Mock registration call at offset 0x80:
    // String loaded into R8: lea r8, [rip + dispString] (4C 8D 05 <disp>)
    const size_t strInsnOff = 0x80;
    mockBuffer[strInsnOff] = 0x4C;
    mockBuffer[strInsnOff + 1] = 0x8D;
    mockBuffer[strInsnOff + 2] = 0x05;
    int32_t strDisp = static_cast<int32_t>(mockString - (&mockBuffer[strInsnOff] + 7));
    std::memcpy(&mockBuffer[strInsnOff + 3], &strDisp, sizeof(int32_t));

    // Function loaded into RDX: lea rdx, [rip + dispFn] (48 8D 15 <disp>)
    const size_t fnInsnOff = 0x87;
    mockBuffer[fnInsnOff] = 0x48;
    mockBuffer[fnInsnOff + 1] = 0x8D;
    mockBuffer[fnInsnOff + 2] = 0x15;
    int32_t fnDisp = static_cast<int32_t>((&mockBuffer[fnOffset]) - (&mockBuffer[fnInsnOff] + 7));
    std::memcpy(&mockBuffer[fnInsnOff + 3], &fnDisp, sizeof(int32_t));

    void* resolved = fs25vr::LuauInterceptor::FindQueryFunctionInText(
        mockBuffer.data(), 0x100, mockString
    );
    EXPECT_EQ(resolved, &mockBuffer[fnOffset]);
}

TEST_F(LuauInterceptorTest, FindQueryFunctionInTextResolvesFunctionWhenStringInRdxAndFuncInR8) {
    // Construct mock buffer where string is in RDX and function is in R8
    std::vector<uint8_t> mockBuffer(1024, 0x90);
    const size_t strOffset = 0x200;
    const char strData[] = "isHeadTrackingAvailable";
    std::memcpy(&mockBuffer[strOffset], strData, sizeof(strData));
    const uint8_t* mockString = &mockBuffer[strOffset];

    const size_t fnOffset = 0x30;
    mockBuffer[fnOffset] = 0x48;     // sub rsp, 0x28
    mockBuffer[fnOffset + 1] = 0x83;
    mockBuffer[fnOffset + 2] = 0xEC;
    mockBuffer[fnOffset + 3] = 0x28;

    // String loaded into RDX: lea rdx, [rip + dispString] (48 8D 15 <disp>)
    const size_t strInsnOff = 0x90;
    mockBuffer[strInsnOff] = 0x48;
    mockBuffer[strInsnOff + 1] = 0x8D;
    mockBuffer[strInsnOff + 2] = 0x15;
    int32_t strDisp = static_cast<int32_t>(mockString - (&mockBuffer[strInsnOff] + 7));
    std::memcpy(&mockBuffer[strInsnOff + 3], &strDisp, sizeof(int32_t));

    // Function loaded into R8: lea r8, [rip + dispFn] (4C 8D 05 <disp>)
    const size_t fnInsnOff = 0x97;
    mockBuffer[fnInsnOff] = 0x4C;
    mockBuffer[fnInsnOff + 1] = 0x8D;
    mockBuffer[fnInsnOff + 2] = 0x05;
    int32_t fnDisp = static_cast<int32_t>((&mockBuffer[fnOffset]) - (&mockBuffer[fnInsnOff] + 7));
    std::memcpy(&mockBuffer[fnInsnOff + 3], &fnDisp, sizeof(int32_t));

    void* resolved = fs25vr::LuauInterceptor::FindQueryFunctionInText(
        mockBuffer.data(), 0x100, mockString
    );
    EXPECT_EQ(resolved, &mockBuffer[fnOffset]);
}

