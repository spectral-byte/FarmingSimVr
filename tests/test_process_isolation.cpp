#include <gtest/gtest.h>
#include "core/injector_runtime.h"
#include "openxr/openxr_manager.h"
#include "proxy/dxgi_proxy.h"
#include <wrl/client.h>
#include <windows.h>
#include <filesystem>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

// The test process loads the freshly built Injector as its own dxgi.dll, and the tests build the
// production Injector runtime too, so a DXGI factory call reaches the real OpenXR loader. On a machine
// with a VR runtime installed that starts the runtime (SteamVR launches vrserver) and the test run hangs.
// Pointing the loader's runtime override at a manifest that does not exist makes every such call enter
// Pass-Through Mode instead. Tests that need an Active VR Mode inject the fake OpenXR runtime.
class NoRealOpenXrRuntimeEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        SetEnvironmentVariableA("XR_RUNTIME_JSON", "fs25vr-tests-no-such-openxr-runtime.json");
    }
};

// Registered during static initialization so it runs in every test process, whether ctest starts one
// per test or one process runs the whole suite, and before the first test body.
[[maybe_unused]] ::testing::Environment* const g_noRealOpenXrRuntime =
    ::testing::AddGlobalTestEnvironment(new NoRealOpenXrRuntimeEnvironment);

std::string RuntimeOverride() {
    char buffer[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA("XR_RUNTIME_JSON", buffer, sizeof(buffer));
    return length > 0 && length < sizeof(buffer) ? std::string(buffer, length) : std::string();
}

}  // namespace

TEST(TestProcessIsolationTest, OpenXrLoaderIsPointedAtARuntimeThatDoesNotExist) {
    const std::string manifest = RuntimeOverride();
    ASSERT_FALSE(manifest.empty());
    EXPECT_FALSE(std::filesystem::exists(manifest)) << manifest;
}

TEST(TestProcessIsolationTest, DxgiFactoryCallThroughProductionRuntimeEntersPassThrough) {
    // A runtime of its own, built from production parts, so the hooks the factory call installs are
    // unwound when it goes out of scope instead of lingering for the tests that run after this one.
    fs25vr::InjectorRuntime runtime{fs25vr::InjectorRuntime::Dependencies{}};
    ComPtr<IDXGIFactory1> factory;
    ASSERT_EQ(runtime.GetDxgiProxy().CreateDXGIFactory1(IID_PPV_ARGS(&factory)), S_OK);
    EXPECT_TRUE(runtime.GetOpenXrManager().IsPassThrough());
    EXPECT_FALSE(runtime.GetDxgiProxy().IsActive());
}
