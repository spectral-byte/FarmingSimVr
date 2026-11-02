#include "core/perf_trace.h"

#include "core/logger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <windows.h>

namespace fs25vr {

namespace {

struct SectionInfo {
    const char* label;
    const char* luaName;  // nullptr: not a Companion Mod section
};

constexpr SectionInfo kSections[] = {
    {"frame", nullptr},
    {"injector present", nullptr},
    {"swapchain wait", nullptr},
    {"copy fence wait", nullptr},
    {"xrEndFrame", nullptr},
    {"driver present", nullptr},
    {"xrWaitFrame", nullptr},
    {"frame begin", nullptr},
    {"GPU eye copy", nullptr},
    {"GPU queue lag", nullptr},
    {"GPU MV pass", nullptr},
    {"game update (incl. mod)", "gameUpdate"},
    {"mod update", "modUpdate"},
    {"mod camera", "modCamera"},
    {"mod draw", "modDraw"},
};
static_assert(sizeof(kSections) / sizeof(kSections[0]) == static_cast<size_t>(PerfSection::Count),
              "one entry per PerfSection");

int64_t ReadQpcNanoseconds() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return QpcTicksToNanoseconds(counter.QuadPart, QpcFrequency());
}

std::string FormatMs(double ms) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", ms);
    return buffer;
}

std::string FormatRate(double count, double seconds) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", seconds > 0.0 ? count / seconds : 0.0);
    return buffer;
}

const char* DescribeFrames(uint32_t stereoFrames, uint32_t dualPassFrames) {
    if (stereoFrames == 0) return "no stereo frames: menu or 2D view";
    if (dualPassFrames == stereoFrames) return "Sequential Dual-Pass";
    if (dualPassFrames == 0) return "AFR";
    return "mode switched";
}

} // namespace

const char* PerfSectionLabel(PerfSection section) {
    const size_t index = static_cast<size_t>(section);
    return index < static_cast<size_t>(PerfSection::Count) ? kSections[index].label : "?";
}

bool PerfSectionFromLuaName(const char* name, PerfSection& section) {
    if (!name) return false;
    for (size_t i = 0; i < static_cast<size_t>(PerfSection::Count); ++i) {
        if (kSections[i].luaName && std::strcmp(kSections[i].luaName, name) == 0) {
            section = static_cast<PerfSection>(i);
            return true;
        }
    }
    return false;
}

int64_t QpcFrequency() {
    static const int64_t frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<int64_t>(f.QuadPart);
    }();
    return frequency;
}

int64_t QpcTicksToNanoseconds(int64_t ticks, int64_t frequency) {
    if (frequency <= 0) return 0;
    return (ticks / frequency) * 1000000000LL + (ticks % frequency) * 1000000000LL / frequency;
}

int64_t GpuTicksToNanoseconds(uint64_t ticks, uint64_t frequency) {
    if (frequency == 0) return 0;
    return static_cast<int64_t>((ticks / frequency) * 1000000000ULL + (ticks % frequency) * 1000000000ULL / frequency);
}

int64_t GpuTimestampToCpuNanoseconds(uint64_t timestamp, uint64_t frequency, uint64_t calibrationGpu,
                                     int64_t calibrationCpuNs) {
    if (timestamp >= calibrationGpu) {
        return calibrationCpuNs + GpuTicksToNanoseconds(timestamp - calibrationGpu, frequency);
    }
    return calibrationCpuNs - GpuTicksToNanoseconds(calibrationGpu - timestamp, frequency);
}

PerfSectionStats SummarizePerfSamples(std::vector<int64_t> samplesNs) {
    PerfSectionStats stats;
    if (samplesNs.empty()) return stats;
    std::sort(samplesNs.begin(), samplesNs.end());
    int64_t sum = 0;
    for (int64_t s : samplesNs) sum += s;
    const size_t n = samplesNs.size();
    // Nearest rank: the smallest sample with at least 95% of the samples at or below it.
    const size_t p95Index = static_cast<size_t>(std::ceil(0.95 * static_cast<double>(n))) - 1;
    stats.count = static_cast<uint32_t>(n);
    stats.avgMs = static_cast<double>(sum) / static_cast<double>(n) / 1e6;
    stats.p95Ms = static_cast<double>(samplesNs[p95Index]) / 1e6;
    stats.maxMs = static_cast<double>(samplesNs.back()) / 1e6;
    return stats;
}

PerfTrace& PerfTrace::Instance() {
    static PerfTrace instance;
    return instance;
}

// Info lines are buffered: each trace line is flushed, so the log can be read while the game runs.
static void LogAndFlush(const std::string& line) {
    LogInfo(line);
    Logger::Instance().Flush();
}

PerfTrace::PerfTrace() : m_clock(&ReadQpcNanoseconds), m_sink(&LogAndFlush) {}

void PerfTrace::SetClock(Clock clock) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clock = clock ? std::move(clock) : Clock(&ReadQpcNanoseconds);
}

void PerfTrace::SetSink(Sink sink) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sink = sink ? std::move(sink) : Sink(&LogAndFlush);
}

int64_t PerfTrace::Now() const {
    return m_clock();
}

void PerfTrace::Samples::Clear() {
    for (auto& s : sections) s.clear();
    frames = 0;
    stereoFrames = 0;
    dualPassFrames = 0;
}

void PerfTrace::Start(uint32_t seconds) {
    seconds = std::clamp<uint32_t>(seconds, 1, kMaxSeconds);
    Output out;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const int64_t now = m_clock();
        m_window.Clear();
        m_total.Clear();
        m_traceStartNs = now;
        m_windowStartNs = now;
        m_deadlineNs = now + static_cast<int64_t>(seconds) * kWindowNs;
        m_lastFrameNs = -1;
        m_windowsReported = 0;
        std::fill(std::begin(m_begun), std::end(m_begun), false);
        m_active.store(true, std::memory_order_relaxed);
        out.sink = m_sink;
        out.lines.push_back("[PerfTrace] Tracing frame timing for " + std::to_string(seconds) +
                            " s: a line per second, then a summary. Each section is avg/p95/max ms (calls per second).");
    }
    out.Write();
}

void PerfTrace::Stop() {
    Output out;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_active.load(std::memory_order_relaxed)) return;
        FinishLocked(m_clock(), out);
    }
    out.Write();
}

// The sink runs after the lock is released: it writes to the log file, and the motion vector pass's
// render thread would otherwise wait on that I/O to add a GPU sample.
void PerfTrace::Output::Write() const {
    if (!sink) return;
    for (const std::string& line : lines) sink(line);
}

void PerfTrace::AddSample(PerfSection section, int64_t durationNs) {
    if (!IsActive() || section >= PerfSection::Count) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active.load(std::memory_order_relaxed)) return;
    if (durationNs < 0) durationNs = 0;
    m_window.sections[static_cast<size_t>(section)].push_back(durationNs);
    m_total.sections[static_cast<size_t>(section)].push_back(durationNs);
}

void PerfTrace::Begin(PerfSection section) {
    if (!IsActive() || section >= PerfSection::Count) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_beginNs[static_cast<size_t>(section)] = m_clock();
    m_begun[static_cast<size_t>(section)] = true;
}

void PerfTrace::End(PerfSection section) {
    if (!IsActive() || section >= PerfSection::Count) return;
    int64_t duration = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const size_t index = static_cast<size_t>(section);
        if (!m_begun[index]) return;
        m_begun[index] = false;
        duration = m_clock() - m_beginNs[index];
    }
    AddSample(section, duration);
}

void PerfTrace::MarkFrame(bool stereo, bool sequentialDualPass) {
    if (!IsActive()) return;
    Output out;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_active.load(std::memory_order_relaxed)) return;
        MarkFrameLocked(stereo, sequentialDualPass, out);
    }
    out.Write();
}

void PerfTrace::MarkFrameLocked(bool stereo, bool sequentialDualPass, Output& out) {
    const int64_t now = m_clock();
    if (m_lastFrameNs >= 0) {
        const int64_t interval = now - m_lastFrameNs;
        m_window.sections[static_cast<size_t>(PerfSection::FrameInterval)].push_back(interval);
        m_total.sections[static_cast<size_t>(PerfSection::FrameInterval)].push_back(interval);
    }
    m_lastFrameNs = now;
    for (Samples* s : {&m_window, &m_total}) {
        ++s->frames;
        if (stereo) {
            ++s->stereoFrames;
            if (sequentialDualPass) ++s->dualPassFrames;
        }
    }

    if (now >= m_deadlineNs) {
        FinishLocked(now, out);
        return;
    }
    if (now - m_windowStartNs >= kWindowNs) {
        out.sink = m_sink;
        out.lines.push_back(
            BuildReport(("second " + std::to_string(++m_windowsReported)).c_str(), m_window, now - m_windowStartNs));
        m_window.Clear();
        m_windowStartNs = now;
    }
}

void PerfTrace::FinishLocked(int64_t now, Output& out) {
    out.sink = m_sink;
    if (m_window.frames > 0 && now > m_windowStartNs) {
        out.lines.push_back(
            BuildReport(("second " + std::to_string(++m_windowsReported)).c_str(), m_window, now - m_windowStartNs));
    }
    out.lines.push_back(BuildReport("Summary", m_total, now - m_traceStartNs));
    m_window.Clear();
    m_total.Clear();
    m_active.store(false, std::memory_order_relaxed);
}

std::string PerfTrace::BuildReport(const char* label, const Samples& samples, int64_t elapsedNs) {
    const double seconds = static_cast<double>(elapsedNs) / 1e9;
    const size_t waits = samples.sections[static_cast<size_t>(PerfSection::XrWaitFrame)].size();
    std::string line = std::string("[PerfTrace] ") + label + " (" + FormatMs(seconds) + " s): " +
                       FormatRate(samples.frames, seconds) + " engine frames/s (" +
                       DescribeFrames(samples.stereoFrames, samples.dualPassFrames) + "), " +
                       FormatRate(static_cast<double>(waits), seconds) + " headset frames/s";
    for (size_t i = 0; i < static_cast<size_t>(PerfSection::Count); ++i) {
        if (samples.sections[i].empty()) continue;
        const PerfSectionStats stats = SummarizePerfSamples(samples.sections[i]);
        line += std::string(" | ") + kSections[i].label + " " + FormatMs(stats.avgMs) + "/" + FormatMs(stats.p95Ms) +
                "/" + FormatMs(stats.maxMs) + " ms (" + FormatRate(stats.count, seconds) + "/s)";
    }
    return line;
}

} // namespace fs25vr
