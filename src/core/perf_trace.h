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
// The section a Companion Mod name stands for ("gameUpdate", "modUpdate", "modCamera", "modDraw").
bool PerfSectionFromLuaName(const char* name, PerfSection& section);

// The QueryPerformanceCounter ticks in nanoseconds, without overflowing for any counter.
int64_t QpcTicksToNanoseconds(int64_t ticks, int64_t frequency);
// GPU timestamp ticks (ID3D12CommandQueue::GetTimestampFrequency) in nanoseconds.
int64_t GpuTicksToNanoseconds(uint64_t ticks, uint64_t frequency);
// Places a GPU timestamp on the CPU clock through one GetClockCalibration pair (gpu ticks, CPU nanoseconds).
int64_t GpuTimestampToCpuNanoseconds(uint64_t timestamp, uint64_t frequency, uint64_t calibrationGpu,
                                     int64_t calibrationCpuNs);

struct PerfSectionStats {
    uint32_t count = 0;
    double avgMs = 0.0;
    double p95Ms = 0.0;
    double maxMs = 0.0;
};
PerfSectionStats SummarizePerfSamples(std::vector<int64_t> samplesNs);

// Frame timing for a few seconds (`vrPerfTrace`), logged to fs25_vr.log as one line per second and a
// summary over the whole trace. Off, every call is one relaxed atomic load, so the hooks stay in place.
// Calls come from the Lua/Present thread (ADR-0018) and the render thread (the motion vector pass).
class PerfTrace {
public:
    static constexpr uint32_t kDefaultSeconds = 10;
    static constexpr uint32_t kMaxSeconds = 60;
    static constexpr int64_t kWindowNs = 1000000000;

    using Clock = std::function<int64_t()>;  // monotonic nanoseconds
    using Sink = std::function<void(const std::string&)>;

    static PerfTrace& Instance();

    PerfTrace();
    // Tests replace the QueryPerformanceCounter clock and the fs25_vr.log sink.
    void SetClock(Clock clock);
    void SetSink(Sink sink);

    // Starts (or restarts) a trace of `seconds`, clamped to 1..kMaxSeconds.
    void Start(uint32_t seconds);
    // Ends a running trace early, logging its summary.
    void Stop();
    bool IsActive() const { return m_active.load(std::memory_order_relaxed); }
    int64_t Now() const;

    void AddSample(PerfSection section, int64_t durationNs);
    // A Companion Mod section: End adds the time since its Begin. An End without a Begin adds nothing.
    void Begin(PerfSection section);
    void End(PerfSection section);
    // Ends one engine frame (a Present). Logs the second that just finished, and ends the trace when it is due.
    void MarkFrame(bool stereo, bool sequentialDualPass);

    // The presenting queue's timestamp frequency, which the motion vector pass reads its timestamps with
    // (it records on the game's command list and never sees the queue). 0 until the first eye copy is timed.
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

    void Report(const char* label, const Samples& samples, int64_t elapsedNs);
    void FinishLocked(int64_t now);

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

// Adds the time from construction to destruction to `section` while a trace runs.
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

} // namespace fs25vr
