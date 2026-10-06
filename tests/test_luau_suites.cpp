#include <gtest/gtest.h>
#include "luau_test_runner.h"

TEST(LuauTestRunnerTest, ExecutesTrivialChunkSuccessfully) {
    LuauTestRunner runner;
    auto result = runner.ExecuteCode("local x = 1 + 2; assert(x == 3)");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_TRUE(result.errorMessage.empty());
}

TEST(LuauTestRunnerTest, CapturesPrintOutput) {
    LuauTestRunner runner;
    auto result = runner.ExecuteCode("print('Hello', 'Luau')");
    EXPECT_TRUE(result.success);
    ASSERT_EQ(result.capturedLogs.size(), 1u);
    EXPECT_EQ(result.capturedLogs[0], "Hello\tLuau");
}

TEST(LuauTestRunnerTest, ProvidesLoadstring) {
    LuauTestRunner runner;
    auto result = runner.ExecuteCode("local f = loadstring('return 42'); assert(f() == 42)");
    EXPECT_TRUE(result.success);
}

TEST(LuauTestRunnerTest, ProvidesLuneFsModuleAndReadsFiles) {
    LuauTestRunner runner;
    auto result = runner.ExecuteCode(
        "local fs = require('@lune/fs')\n"
        "local content = fs.readFile('mods/FS25_VRCompanion/modDesc.xml')\n"
        "assert(type(content) == 'string' and #content > 0)\n"
    );
    EXPECT_TRUE(result.success);
}

TEST(LuauTestRunnerTest, FailsOnAssertionWithDescriptiveTraceback) {
    LuauTestRunner runner;
    auto result = runner.ExecuteCode("assert(false, 'custom_assertion_error')", "=my_chunk.luau");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.errorMessage.find("custom_assertion_error"), std::string::npos);
    EXPECT_NE(result.traceback.find("my_chunk.luau"), std::string::npos);
}

TEST(LuauTestRunnerTest, FailsOnSyntaxError) {
    LuauTestRunner runner;
    auto result = runner.ExecuteCode("local bad syntax !!!", "=syntax_err.luau");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST(LuauTestRunnerTest, DetectsFailedLogsAsFailure) {
    LuauTestRunner runner;
    auto result = runner.ExecuteCode("print('  [FAIL] TestExample: Expected true got false')");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.errorMessage.find("assertions failed"), std::string::npos);
}

class LuauTestSuiteTest : public ::testing::Test {
protected:
    LuauTestRunner runner;

    void RunSuite(const std::string& suitePath) {
        auto result = runner.ExecuteFile(suitePath);
        if (!result.success) {
            std::string diagnostics = "Luau test suite execution failed for: " + suitePath + "\n";
            diagnostics += "Error: " + result.errorMessage + "\n";
            if (!result.traceback.empty()) {
                diagnostics += "Traceback:\n" + result.traceback + "\n";
            }
            diagnostics += "Captured output (" + std::to_string(result.capturedLogs.size()) + " lines):\n";
            for (const auto& line : result.capturedLogs) {
                diagnostics += "  " + line + "\n";
            }
            FAIL() << diagnostics;
        }
        EXPECT_TRUE(result.success);
    }
};

TEST_F(LuauTestSuiteTest, Test2dViewMode) {
    RunSuite("tests/test_2d_view_mode.luau");
}

TEST_F(LuauTestSuiteTest, TestDualPassPacing) {
    RunSuite("tests/test_dual_pass_pacing.luau");
}

TEST_F(LuauTestSuiteTest, TestHudConvergence) {
    RunSuite("tests/test_hud_convergence.luau");
}

TEST_F(LuauTestSuiteTest, TestHudInsets) {
    RunSuite("tests/test_hud_insets.luau");
}

TEST_F(LuauTestSuiteTest, TestHudStateMachine) {
    RunSuite("tests/test_hud_state_machine.luau");
}

TEST_F(LuauTestSuiteTest, TestReusableTables) {
    RunSuite("tests/test_reusable_tables.luau");
}

TEST_F(LuauTestSuiteTest, TestSnapTurn) {
    RunSuite("tests/test_snap_turn.luau");
}

TEST_F(LuauTestSuiteTest, TestVehicleCameraYaw) {
    RunSuite("tests/test_vehicle_camera_yaw.luau");
}


