#include <gtest/gtest.h>
#include "core/perf_trace.h"
#include "luau/vr_bridge.h"
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include <cstdlib>
#include <string>
#include <vector>

using fs25vr::PerfSection;
using fs25vr::PerfTrace;

namespace {

constexpr int64_t kMs = 1000000;

// A trace on a clock the test moves by hand, logging into a list.
class PerfTraceTest : public ::testing::Test {
protected:
    PerfTrace trace;
    int64_t nowNs = 5 * kMs;
    std::vector<std::string> lines;

    void SetUp() override {
        trace.SetClock([this] { return nowNs; });
        trace.SetSink([this](const std::string& line) { lines.push_back(line); });
    }

    // One engine frame every `frameMs`, for `frames` frames.
    void Frames(int frames, int64_t frameMs, bool stereo = true, bool dualPass = true) {
        for (int i = 0; i < frames; ++i) {
            nowNs += frameMs * kMs;
            trace.MarkFrame(stereo, dualPass);
        }
    }

    std::string LineStartingWith(const std::string& prefix) const {
        for (const auto& line : lines) {
            if (line.rfind(prefix, 0) == 0) return line;
        }
        return {};
    }
};

TEST(PerfTraceMathTest, PerformanceCounterTicksBecomeNanosecondsWithoutOverflow) {
    EXPECT_EQ(fs25vr::QpcTicksToNanoseconds(10000000, 10000000), 1000000000);
    EXPECT_EQ(fs25vr::QpcTicksToNanoseconds(15, 10000000), 1500);
    // Ten years of a 10 MHz counter: ticks * 1e9 alone would overflow int64.
    const int64_t tenYears = 10000000LL * 60 * 60 * 24 * 365 * 10;
    EXPECT_EQ(fs25vr::QpcTicksToNanoseconds(tenYears, 10000000), tenYears * 100);
    EXPECT_EQ(fs25vr::QpcTicksToNanoseconds(5, 0), 0);
}

TEST(PerfTraceMathTest, GpuTimestampsArePlacedOnTheCpuClockThroughTheCalibration) {
    const uint64_t frequency = 1000000;  // 1 tick = 1 us
    EXPECT_EQ(fs25vr::GpuTicksToNanoseconds(2500, frequency), 2500000);
    EXPECT_EQ(fs25vr::GpuTicksToNanoseconds(10, 0), 0);
    // Calibrated at GPU tick 1000 = CPU 50 ms
    EXPECT_EQ(fs25vr::GpuTimestampToCpuNanoseconds(4000, frequency, 1000, 50 * kMs), 53 * kMs);
    EXPECT_EQ(fs25vr::GpuTimestampToCpuNanoseconds(500, frequency, 1000, 50 * kMs), 50 * kMs - 500000);
}

TEST(PerfTraceMathTest, SummaryHasTheAverageNearestRank95thPercentileAndMaximum) {
    std::vector<int64_t> samples;
    for (int i = 1; i <= 20; ++i) samples.push_back(i * kMs);
    const fs25vr::PerfSectionStats stats = fs25vr::SummarizePerfSamples(samples);
    EXPECT_EQ(stats.count, 20u);
    EXPECT_DOUBLE_EQ(stats.avgMs, 10.5);
    EXPECT_DOUBLE_EQ(stats.p95Ms, 19.0);  // 19 of 20 samples are at or below it
    EXPECT_DOUBLE_EQ(stats.maxMs, 20.0);
    EXPECT_EQ(fs25vr::SummarizePerfSamples({}).count, 0u);
}

TEST(PerfTraceMathTest, OnlyTheCompanionModSectionsHaveLuaNames) {
    PerfSection section = PerfSection::Count;
    ASSERT_TRUE(fs25vr::PerfSectionFromLuaName("gameUpdate", section));
    EXPECT_EQ(section, PerfSection::LuaGameUpdate);
    ASSERT_TRUE(fs25vr::PerfSectionFromLuaName("modDraw", section));
    EXPECT_EQ(section, PerfSection::LuaModDraw);
    EXPECT_FALSE(fs25vr::PerfSectionFromLuaName("frame", section));
    EXPECT_FALSE(fs25vr::PerfSectionFromLuaName("nonsense", section));
    EXPECT_FALSE(fs25vr::PerfSectionFromLuaName(nullptr, section));
}

TEST_F(PerfTraceTest, IsOffUntilStartedAndLogsNothing) {
    EXPECT_FALSE(trace.IsActive());
    trace.AddSample(PerfSection::InjectorPresent, kMs);
    trace.Begin(PerfSection::LuaModUpdate);
    trace.End(PerfSection::LuaModUpdate);
    Frames(200, 10);
    trace.Stop();
    EXPECT_TRUE(lines.empty());
}

TEST_F(PerfTraceTest, LogsOneLinePerSecondAndASummaryThenEndsByItself) {
    trace.Start(2);
    ASSERT_TRUE(trace.IsActive());
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("Tracing frame timing for 2 s"), std::string::npos) << lines[0];

    for (int i = 0; i < 200; ++i) {
        trace.AddSample(PerfSection::InjectorPresent, kMs / 2);
        if (i % 2 == 1) trace.AddSample(PerfSection::XrWaitFrame, 3 * kMs);
        Frames(1, 10);
    }

    EXPECT_FALSE(trace.IsActive());
    const std::string first = LineStartingWith("[PerfTrace] second 1 ");
    ASSERT_FALSE(first.empty());
    EXPECT_NE(first.find("100.0 engine frames/s (Sequential Dual-Pass), 50.0 headset frames/s"), std::string::npos) << first;
    EXPECT_NE(first.find("| frame 10.00/10.00/10.00 ms (99.0/s)"), std::string::npos) << first;
    EXPECT_NE(first.find("| injector present 0.50/0.50/0.50 ms (100.0/s)"), std::string::npos) << first;
    EXPECT_NE(first.find("| xrWaitFrame 3.00/3.00/3.00 ms (50.0/s)"), std::string::npos) << first;
    EXPECT_EQ(first.find("GPU"), std::string::npos) << "sections without samples are left out: " << first;

    const std::string summary = LineStartingWith("[PerfTrace] Summary (2.00 s)");
    ASSERT_FALSE(summary.empty());
    EXPECT_NE(summary.find("100.0 engine frames/s"), std::string::npos) << summary;
    EXPECT_EQ(lines.back(), summary);

    // Off again: later frames add nothing
    const size_t count = lines.size();
    Frames(200, 10);
    EXPECT_EQ(lines.size(), count);
}

TEST_F(PerfTraceTest, TheLastSecondIsNumberedAfterThePreviousOneWhenTheTraceEndsLate) {
    // 7 ms frames cross the 2 s deadline at 2.002 s, so the trace ends a little after its last full second
    trace.Start(2);
    Frames(300, 7);
    EXPECT_FALSE(trace.IsActive());
    EXPECT_FALSE(LineStartingWith("[PerfTrace] second 1 ").empty());
    EXPECT_FALSE(LineStartingWith("[PerfTrace] second 2 ").empty());
    EXPECT_TRUE(LineStartingWith("[PerfTrace] second 3 ").empty()) << lines[lines.size() - 2];
}

TEST_F(PerfTraceTest, NamesThePresentationTheFramesUsed) {
    trace.Start(10);
    Frames(50, 10, /*stereo=*/true, /*dualPass=*/false);
    Frames(50, 10, /*stereo=*/true, /*dualPass=*/false);
    EXPECT_NE(LineStartingWith("[PerfTrace] second 1").find("(AFR)"), std::string::npos);
    Frames(100, 10, /*stereo=*/false, /*dualPass=*/false);
    EXPECT_NE(LineStartingWith("[PerfTrace] second 2").find("(no stereo frames: menu or 2D view)"), std::string::npos);
    trace.Stop();
    EXPECT_NE(LineStartingWith("[PerfTrace] Summary").find("(AFR)"), std::string::npos);
}

TEST_F(PerfTraceTest, StopEndsEarlyWithTheSummary) {
    trace.Start(30);
    Frames(30, 10);
    trace.Stop();
    EXPECT_FALSE(trace.IsActive());
    const std::string summary = LineStartingWith("[PerfTrace] Summary (0.30 s)");
    EXPECT_FALSE(summary.empty());
    EXPECT_FALSE(LineStartingWith("[PerfTrace] second 1 (0.30 s)").empty()) << "the unfinished second is logged too";
    lines.clear();
    trace.Stop();
    EXPECT_TRUE(lines.empty());
}

TEST_F(PerfTraceTest, CompanionModSectionsAddTheTimeFromBeginToEnd) {
    trace.Start(10);
    trace.Begin(PerfSection::LuaModUpdate);
    nowNs += 2 * kMs;
    trace.End(PerfSection::LuaModUpdate);
    trace.End(PerfSection::LuaModUpdate);  // no Begin: nothing
    trace.End(PerfSection::LuaModDraw);
    trace.Stop();
    const std::string summary = LineStartingWith("[PerfTrace] Summary");
    EXPECT_NE(summary.find("| mod update 2.00/2.00/2.00 ms"), std::string::npos) << summary;
    EXPECT_EQ(summary.find("mod draw"), std::string::npos) << summary;
}

TEST_F(PerfTraceTest, StartClampsTheLengthAndRestartsAFreshTrace) {
    trace.Start(0);
    EXPECT_NE(lines.back().find("for 1 s"), std::string::npos) << lines.back();
    trace.Start(500);
    EXPECT_NE(lines.back().find("for 60 s"), std::string::npos) << lines.back();
    trace.AddSample(PerfSection::DriverPresent, kMs);
    trace.Start(5);
    trace.Stop();
    EXPECT_EQ(LineStartingWith("[PerfTrace] Summary").find("driver present"), std::string::npos)
        << "a restart drops the samples of the trace it replaces";
}

TEST_F(PerfTraceTest, NegativeDurationsCountAsZero) {
    // The GPU queue lag can come out a little negative from calibration jitter
    trace.Start(10);
    trace.AddSample(PerfSection::GpuQueueLag, -kMs);
    trace.Stop();
    EXPECT_NE(LineStartingWith("[PerfTrace] Summary").find("| GPU queue lag 0.00/0.00/0.00 ms"), std::string::npos);
}

// ---- VRBridge -------------------------------------------------------------------------------------

class PerfTraceBridgeTest : public ::testing::Test {
protected:
    lua_State* L = nullptr;
    fs25vr::VRBridge bridge{nullptr, nullptr};
    int64_t nowNs = 0;
    std::vector<std::string> lines;

    void SetUp() override {
        L = luaL_newstate();
        luaL_openlibs(L);
        ASSERT_TRUE(bridge.Inject(L));
        PerfTrace::Instance().SetClock([this] { return nowNs += kMs; });
        PerfTrace::Instance().SetSink([this](const std::string& line) { lines.push_back(line); });
    }

    void TearDown() override {
        PerfTrace::Instance().Stop();
        PerfTrace::Instance().SetClock(nullptr);
        PerfTrace::Instance().SetSink(nullptr);
        lua_close(L);
    }

    void Run(const std::string& code) {
        size_t size = 0;
        char* bytecode = luau_compile(code.c_str(), code.size(), nullptr, &size);
        ASSERT_NE(bytecode, nullptr);
        ASSERT_EQ(luau_load(L, "=test", bytecode, size, 0), 0);
        std::free(bytecode);
        ASSERT_EQ(lua_pcall(L, 0, 0, 0), 0) << lua_tostring(L, -1);
    }
};

TEST_F(PerfTraceBridgeTest, CompanionModStartsTimesAndStopsATraceThroughVRBridge) {
    Run("assert(VRBridge.isPerfTracing() == false)");
    Run("assert(VRBridge.startPerfTrace(3) == true)");
    EXPECT_TRUE(PerfTrace::Instance().IsActive());
    EXPECT_NE(lines.back().find("for 3 s"), std::string::npos);
    Run("assert(VRBridge.isPerfTracing() == true)");
    Run("VRBridge.perfTraceBegin('modCamera'); VRBridge.perfTraceEnd('modCamera')");
    Run("VRBridge.perfTraceBegin('frame'); VRBridge.perfTraceEnd('frame')");  // not a Companion Mod section
    Run("VRBridge.stopPerfTrace()");
    EXPECT_FALSE(PerfTrace::Instance().IsActive());
    EXPECT_NE(lines.back().find("| mod camera 1.00/1.00/1.00 ms"), std::string::npos) << lines.back();
    EXPECT_EQ(lines.back().find("| frame "), std::string::npos) << lines.back();
}

TEST_F(PerfTraceBridgeTest, StartWithoutALengthTracesTheDefaultSeconds) {
    Run("VRBridge.startPerfTrace()");
    EXPECT_NE(lines.back().find("for 10 s"), std::string::npos) << lines.back();
}

} // namespace
