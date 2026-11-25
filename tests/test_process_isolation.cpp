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

class NoRealOpenXrRuntimeEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        SetEnvironmentVariableA("XR_RUNTIME_JSON", "fs25vr-tests-no-such-openxr-runtime.json");
    }
};

[[maybe_unused]] ::testing::Environment* const g_noRealOpenXrRuntime =
    ::testing::AddGlobalTestEnvironment(new NoRealOpenXrRuntimeEnvironment);

std::string RuntimeOverride() {
    char buffer[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA("XR_RUNTIME_JSON", buffer, sizeof(buffer));
    return length > 0 && length < sizeof(buffer) ? std::string(buffer, length) : std::string();
}

}

TEST(TestProcessIsolationTest, OpenXrLoaderIsPointedAtARuntimeThatDoesNotExist) {
    const std::string manifest = RuntimeOverride();
    ASSERT_FALSE(manifest.empty());
    EXPECT_FALSE(std::filesystem::exists(manifest)) << manifest;
}

TEST(TestProcessIsolationTest, DxgiFactoryCallThroughProductionRuntimeEntersPassThrough) {
    fs25vr::InjectorRuntime runtime{fs25vr::InjectorRuntime::Dependencies{}};
    ComPtr<IDXGIFactory1> factory;
    ASSERT_EQ(runtime.GetDxgiProxy().CreateDXGIFactory1(IID_PPV_ARGS(&factory)), S_OK);
    EXPECT_TRUE(runtime.GetOpenXrManager().IsPassThrough());
    EXPECT_FALSE(runtime.GetDxgiProxy().IsActive());
}
