#include <gtest/gtest.h>
#include "core/injector_runtime.h"
#include "luau/luau_interceptor.h"
#include "luau/vr_bridge.h"
#include "luacode.h"
#include <windows.h>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

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
    std::shared_ptr<fs25vr::VRBridge> bridge;
    std::unique_ptr<fs25vr::InjectorRuntime> runtime;

    void SetUp() override {
        s_queryCallCount = 0;
        // Only a runtime-owned interceptor installs the detour, and the entry point resolves it through the runtime.
        bridge = std::make_shared<fs25vr::VRBridge>(nullptr, nullptr);
        fs25vr::InjectorRuntime::Dependencies deps;
        deps.vrBridge = bridge;
        runtime = std::make_unique<fs25vr::InjectorRuntime>(std::move(deps));
        L = luaL_newstate();
        luaL_openlibs(L);

        // Register the simulated engine head tracking query
        lua_pushcclosure(L, &Engine_isHeadTrackingAvailable, "isHeadTrackingAvailable", 0);
        lua_setglobal(L, "isHeadTrackingAvailable");
    }

    void TearDown() override {
        runtime->GetLuauInterceptor().Detach();
        if (L) {
            lua_close(L);
            L = nullptr;
        }
        runtime.reset();
    }
};

TEST_F(LuauInterceptorTest, InterceptsActiveLuaStateAndInjectsVRBridge) {
    auto& interceptor = runtime->GetLuauInterceptor();

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

TEST_F(LuauInterceptorTest, DetourResolvesTheInterceptorThroughTheCurrentRuntime) {
    auto isolatedBridge = std::make_shared<fs25vr::VRBridge>(nullptr, nullptr);
    fs25vr::InjectorRuntime::Dependencies deps;
    deps.vrBridge = isolatedBridge;
    fs25vr::InjectorRuntime isolatedRuntime(std::move(deps));
    ASSERT_EQ(fs25vr::InjectorRuntime::Current(), &isolatedRuntime);

    auto& isolatedInterceptor = isolatedRuntime.GetLuauInterceptor();
    ASSERT_TRUE(isolatedInterceptor.Attach(reinterpret_cast<void*>(&Engine_isHeadTrackingAvailable)));
    EXPECT_FALSE(isolatedInterceptor.IsIntercepted());

    int status = RunLuauString(L, "isHeadTrackingAvailable()");
    EXPECT_EQ(status, 0);

    EXPECT_TRUE(isolatedInterceptor.IsIntercepted());
    EXPECT_EQ(isolatedInterceptor.GetActiveState(), L);
    EXPECT_FALSE(runtime->GetLuauInterceptor().IsIntercepted());

    isolatedInterceptor.Detach();
}

TEST_F(LuauInterceptorTest, DetourResolvesTheInterceptorThatInstalledItNotTheNewestRuntime) {
    auto& installer = runtime->GetLuauInterceptor();
    ASSERT_TRUE(installer.Attach(reinterpret_cast<void*>(&Engine_isHeadTrackingAvailable)));

    fs25vr::InjectorRuntime::Dependencies deps;
    deps.vrBridge = std::make_shared<fs25vr::VRBridge>(nullptr, nullptr);
    fs25vr::InjectorRuntime newerRuntime(std::move(deps));
    ASSERT_EQ(fs25vr::InjectorRuntime::Current(), &newerRuntime);

    EXPECT_EQ(RunLuauString(L, "assert(isHeadTrackingAvailable() == true)"), 0);

    // The query reached the real function through the installer's original, not a null one.
    EXPECT_EQ(s_queryCallCount, 1);
    EXPECT_TRUE(installer.IsIntercepted());
    EXPECT_FALSE(newerRuntime.GetLuauInterceptor().IsIntercepted());
}

TEST_F(LuauInterceptorTest, StandaloneInterceptorInstallsNoDetour) {
    // No runtime owns it, so the hook could not find its original: Attach must refuse.
    fs25vr::LuauInterceptor standalone(bridge);
    EXPECT_FALSE(standalone.Attach(reinterpret_cast<void*>(&Engine_isHeadTrackingAvailable)));
    EXPECT_FALSE(standalone.GetDetour().IsInstalled());

    EXPECT_EQ(RunLuauString(L, "assert(isHeadTrackingAvailable() == true)"), 0);
    EXPECT_EQ(s_queryCallCount, 1);
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


namespace {

// Lays the NUL-terminated markers out back to back, as they sit in a host .rdata section.
std::vector<uint8_t> MakeRdata(const std::vector<std::string>& markers) {
    std::vector<uint8_t> rdata(64, 0);
    for (const std::string& marker : markers) {
        rdata.insert(rdata.end(), marker.begin(), marker.end());
        rdata.push_back(0);
        rdata.insert(rdata.end(), 8, 0);
    }
    return rdata;
}

std::vector<std::string> AllFingerprintMarkers() {
    return std::vector<std::string>(std::begin(fs25vr::kHostLuauFingerprint), std::end(fs25vr::kHostLuauFingerprint));
}

// An executable stand-in for the host module: .text holds a query function plus the registration code
// that pairs it with its name, and .rdata holds that name followed by the given strings. Executable so
// the detour can really be installed when the guard lets AttachToHostSections through.
class MockHost {
public:
    static constexpr size_t kTextSize = 0x100;
    static constexpr size_t kRdataOffset = 0x200;
    static constexpr size_t kSize = 0x1000;
    static constexpr size_t kFnOffset = 0x20;

    explicit MockHost(const std::vector<std::string>& rdataStrings) {
        m_base = static_cast<uint8_t*>(VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!m_base) return;
        std::memset(m_base, 0x90, kTextSize);
        std::memset(m_base + kTextSize, 0, kSize - kTextSize);

        const uint8_t fnEntry[] = {0x48, 0x83, 0xEC, 0x28};  // sub rsp, 0x28
        std::memcpy(m_base + kFnOffset, fnEntry, sizeof(fnEntry));

        size_t cursor = kRdataOffset;
        const char queryName[] = "isHeadTrackingAvailable";
        std::memcpy(m_base + cursor, queryName, sizeof(queryName));
        const uint8_t* queryString = m_base + cursor;
        cursor += sizeof(queryName);
        for (const std::string& s : rdataStrings) {
            std::memcpy(m_base + cursor, s.c_str(), s.size() + 1);
            cursor += s.size() + 1;
        }

        EmitRipLea(0x80, 0x4C, 0x05, queryString);       // lea r8, [rip + queryName]
        EmitRipLea(0x87, 0x48, 0x15, m_base + kFnOffset); // lea rdx, [rip + fn]
    }
    ~MockHost() {
        if (m_base) VirtualFree(m_base, 0, MEM_RELEASE);
    }
    MockHost(const MockHost&) = delete;
    MockHost& operator=(const MockHost&) = delete;

    bool Valid() const { return m_base != nullptr; }
    const uint8_t* Text() const { return m_base; }
    const uint8_t* Rdata() const { return m_base + kRdataOffset; }
    size_t RdataSize() const { return kSize - kRdataOffset; }
    void* QueryFunction() const { return m_base + kFnOffset; }

private:
    void EmitRipLea(size_t off, uint8_t rex, uint8_t modrm, const uint8_t* target) {
        m_base[off] = rex;
        m_base[off + 1] = 0x8D;
        m_base[off + 2] = modrm;
        const int32_t disp = static_cast<int32_t>(target - (m_base + off + 7));
        std::memcpy(m_base + off + 3, &disp, sizeof(disp));
    }

    uint8_t* m_base = nullptr;
};

} // anonymous namespace

TEST_F(LuauInterceptorTest, AttachToHostSectionsRefusesHostWithoutTheLuauFingerprint) {
    MockHost host({});
    ASSERT_TRUE(host.Valid());
    auto& interceptor = runtime->GetLuauInterceptor();

    EXPECT_FALSE(interceptor.AttachToHostSections(host.Rdata(), host.RdataSize(), host.Text(), MockHost::kTextSize));
    EXPECT_FALSE(interceptor.GetDetour().IsInstalled());
}

TEST_F(LuauInterceptorTest, AttachToHostSectionsRefusesHostWhoseVmMarkersAreMissing) {
    // Front end matches 0.662 but the VM half of the fingerprint is absent: still a mismatch.
    std::vector<std::string> frontEndOnly = AllFingerprintMarkers();
    frontEndOnly.resize(5);
    ASSERT_EQ(frontEndOnly.back(), "LuauAllowFragmentParsing");
    MockHost host(frontEndOnly);
    ASSERT_TRUE(host.Valid());
    auto& interceptor = runtime->GetLuauInterceptor();

    EXPECT_FALSE(interceptor.AttachToHostSections(host.Rdata(), host.RdataSize(), host.Text(), MockHost::kTextSize));
    EXPECT_FALSE(interceptor.GetDetour().IsInstalled());
}

TEST_F(LuauInterceptorTest, AttachToHostSectionsAttachesToMatchingHost) {
    // Control for the refusal tests: the same mock host, plus the fingerprint, does get the detour.
    MockHost host(AllFingerprintMarkers());
    ASSERT_TRUE(host.Valid());
    auto& interceptor = runtime->GetLuauInterceptor();

    EXPECT_TRUE(interceptor.AttachToHostSections(host.Rdata(), host.RdataSize(), host.Text(), MockHost::kTextSize));
    EXPECT_TRUE(interceptor.GetDetour().IsInstalled());
    interceptor.Detach();
}

TEST(LuauVersionGuardTest, HostCarryingEveryFingerprintMarkerMatches) {
    std::vector<std::string> markers;
    for (const char* marker : fs25vr::kHostLuauFingerprint) markers.emplace_back(marker);
    std::vector<uint8_t> rdata = MakeRdata(markers);

    EXPECT_TRUE(fs25vr::HostEmbedsMatchingLuau(rdata.data(), rdata.size()));
}

TEST(LuauVersionGuardTest, HostMissingAnyFingerprintMarkerIsAMismatch) {
    constexpr size_t markerCount = sizeof(fs25vr::kHostLuauFingerprint) / sizeof(fs25vr::kHostLuauFingerprint[0]);
    for (size_t skipped = 0; skipped < markerCount; ++skipped) {
        std::vector<std::string> markers;
        for (size_t i = 0; i < markerCount; ++i) {
            if (i != skipped) markers.emplace_back(fs25vr::kHostLuauFingerprint[i]);
        }
        std::vector<uint8_t> rdata = MakeRdata(markers);

        EXPECT_FALSE(fs25vr::HostEmbedsMatchingLuau(rdata.data(), rdata.size()))
            << "missing marker: " << fs25vr::kHostLuauFingerprint[skipped];
    }
}

TEST(LuauVersionGuardTest, MarkerWithLeadingCharactersDoesNotCount) {
    std::vector<std::string> markers = AllFingerprintMarkers();
    markers[0] = "x" + markers[0];
    std::vector<uint8_t> rdata = MakeRdata(markers);

    EXPECT_FALSE(fs25vr::HostEmbedsMatchingLuau(rdata.data(), rdata.size()));
}

TEST(LuauVersionGuardTest, MarkerWithTrailingCharactersDoesNotCount) {
    std::vector<std::string> markers = AllFingerprintMarkers();
    markers[0] = markers[0] + "Extra";
    std::vector<uint8_t> rdata = MakeRdata(markers);

    EXPECT_FALSE(fs25vr::HostEmbedsMatchingLuau(rdata.data(), rdata.size()));
}

TEST(LuauVersionGuardTest, MarkerAtTheStartOfTheSectionCounts) {
    std::vector<uint8_t> rdata = MakeRdata(AllFingerprintMarkers());
    rdata.erase(rdata.begin(), rdata.begin() + 64);  // drop the leading padding so a marker sits at offset 0
    ASSERT_NE(rdata[0], 0);

    EXPECT_TRUE(fs25vr::HostEmbedsMatchingLuau(rdata.data(), rdata.size()));
}

TEST(LuauVersionGuardTest, MarkerCutOffByTheSectionEndDoesNotCount) {
    // The last marker's terminating NUL falls just outside the section.
    std::vector<uint8_t> rdata = MakeRdata(AllFingerprintMarkers());
    const size_t sectionSize = rdata.size() - 8 - 1;

    EXPECT_FALSE(fs25vr::HostEmbedsMatchingLuau(rdata.data(), sectionSize));
    EXPECT_TRUE(fs25vr::HostEmbedsMatchingLuau(rdata.data(), sectionSize + 1));
}

TEST(LuauVersionGuardTest, EmptySectionIsAMismatch) {
    const uint8_t byte = 0;
    EXPECT_FALSE(fs25vr::HostEmbedsMatchingLuau(nullptr, 0));
    EXPECT_FALSE(fs25vr::HostEmbedsMatchingLuau(&byte, 0));
}

TEST(LuauVersionGuardTest, GuardVersionMatchesTheLinkedLuauTag) {
    // FS25VR_LUAU_TAG is CMakeLists.txt's GIT_TAG: bumping it without updating the guard fails here.
    EXPECT_STREQ(fs25vr::kExpectedLuauVersion, FS25VR_LUAU_TAG);
}
