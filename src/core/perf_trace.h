#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace fs25vr {

// What one frame-timing sample measures. The Injector times its own work on the present path and the
// GPU work it records; the Companion Mod times its hooks through VRBridge (perfTraceBegin / perfTraceEnd).
enum class PerfSection : uint32_t {
    FrameInterval,      // one Present to the next: the engine's whole frame (one eye)
    InjectorPresent,    // DxgiProxy::OnPresent: eye copy recording, xrEndFrame, mirror handling
    SwapchainWait,      // xrAcquireSwapchainImage + xrWaitSwapchainImage for an eye copy
    CopyFenceWait,      // the CPU waiting for the GPU to finish an earlier eye copy before reusing its slot
    XrEndFrame,         // SubmitFramePair / SubmitMenuQuad's xrEndFrame
    DriverPresent,      // the game's real Present (the Desktop Mirror Window)
    XrWaitFrame,        // xrWaitFrame blocking: headroom left before the compositor's next frame
    FrameBegin,         // the rest of BeginFrame: xrBeginFrame, head and hand tracking
    GpuEyeCopy,         // GPU time of an eye copy
    GpuQueueLag,        // submitting an eye copy to the GPU starting it: how far behind the GPU runs
    GpuMvPass,          // GPU time of the motion vector pass ahead of an upscaler evaluation
    LuaGameUpdate,      // the game's mission update (includes the Companion Mod's update)
    LuaModUpdate,       // VRCompanion:update
    LuaModCamera,       // the Companion Mod's camera hooks
    LuaModDraw,         // VRCompanion:draw
    Count
};

const char* PerfSectionLabel(PerfSection section);
bool PerfSectionFromLuaName(const char* name, PerfSection& section);

int64_t QpcFrequency();
int64_t QpcTicksToNanoseconds(int64_t ticks, int64_t frequency);
int64_t GpuTicksToNanoseconds(uint64_t ticks, uint64_t frequency);
int64_t GpuTimestampToCpuNanoseconds(uint64_t timestamp, uint64_t frequency, uint64_t calibrationGpu,
                                     int64_t calibrationCpuNs);

struct PerfSectionStats {
    uint32_t count = 0;
    double avgMs = 0.0;
    double p95Ms = 0.0;
    double maxMs = 0.0;
};
PerfSectionStats SummarizePerfSamples(std::vector<int64_t> samplesNs);

class PerfTrace {
public:
    static constexpr uint32_t kDefaultSeconds = 10;
    static constexpr uint32_t kMaxSeconds = 60;
    static constexpr int64_t kWindowNs = 1000000000;

    using Clock = std::function<int64_t()>;  // monotonic nanoseconds
    using Sink = std::function<void(const std::string&)>;

    static PerfTrace& Instance();

    PerfTrace();
    void SetClock(Clock clock);
    void SetSink(Sink sink);

    void Start(uint32_t seconds);
    void Stop();
    bool IsActive() const { return m_active.load(std::memory_order_relaxed); }
    int64_t Now() const;

    void AddSample(PerfSection section, int64_t durationNs);
    void Begin(PerfSection section);
    void End(PerfSection section);
    void MarkFrame(bool stereo, bool sequentialDualPass);

    void SetGpuTimestampFrequency(uint64_t frequency) { m_gpuFrequency.store(frequency, std::memory_order_relaxed); }
    uint64_t GetGpuTimestampFrequency() const { return m_gpuFrequency.load(std::memory_order_relaxed); }

private:
    struct Samples {
        std::vector<int64_t> sections[static_cast<size_t>(PerfSection::Count)];
        uint32_t frames = 0;
        uint32_t stereoFrames = 0;
        uint32_t dualPassFrames = 0;
        void Clear();
    };

    struct Output {
        Sink sink;
        std::vector<std::string> lines;
        void Write() const;
    };

    static std::string BuildReport(const char* label, const Samples& samples, int64_t elapsedNs);
    void MarkFrameLocked(bool stereo, bool sequentialDualPass, Output& out);
    void FinishLocked(int64_t now, Output& out);

    std::atomic<bool> m_active{false};
    std::atomic<uint64_t> m_gpuFrequency{0};
    mutable std::mutex m_mutex;
    Clock m_clock;
    Sink m_sink;
    Samples m_window;
    Samples m_total;
    int64_t m_traceStartNs = 0;
    int64_t m_windowStartNs = 0;
    int64_t m_deadlineNs = 0;
    int64_t m_lastFrameNs = -1;
    uint32_t m_windowsReported = 0;  // per-second lines logged so far in this trace
    int64_t m_beginNs[static_cast<size_t>(PerfSection::Count)]{};
    bool m_begun[static_cast<size_t>(PerfSection::Count)]{};
};

class PerfScope {
public:
    explicit PerfScope(PerfSection section)
        : m_section(section), m_startNs(PerfTrace::Instance().IsActive() ? PerfTrace::Instance().Now() : -1) {}
    ~PerfScope() {
        if (m_startNs >= 0) PerfTrace::Instance().AddSample(m_section, PerfTrace::Instance().Now() - m_startNs);
    }
    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    PerfSection m_section;
    int64_t m_startNs;
};

}
