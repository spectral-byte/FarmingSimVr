#pragma once

#include <atomic>
#include <cstdint>

namespace fs25vr {

// Records which thread reaches a call site, to establish and then guard the Lua/Present
// thread model (ADR-0018). Logs the first thread seen and any later change, so
// a steady-state frame loop adds no log lines. A change means the single-thread model no
// longer holds, so it is logged as a warning; after kMaxChangeLogs changes the site goes
// quiet, so threads that alternate every frame can't flood the log from the present path.
class ThreadTrace {
public:
    static constexpr uint32_t kMaxChangeLogs = 8;

    explicit ThreadTrace(const char* site) : m_site(site) {}

    // Returns true when this call logged (first sighting, a change, or the suppression notice).
    bool Record(uint32_t threadId);
    bool Record();  // Records the calling thread.

private:
    static constexpr uint32_t kNoThread = 0;  // Windows never issues thread ID 0.

    const char* m_site;
    std::atomic<uint32_t> m_lastThread{kNoThread};
    std::atomic<uint32_t> m_changeLogs{0};
};

} // namespace fs25vr

// Declares a per-call-site tracer and records the calling thread.
#define FS25VR_TRACE_THREAD(site)                                  \
    do {                                                           \
        static ::fs25vr::ThreadTrace fs25vrThreadTrace_(site);     \
        fs25vrThreadTrace_.Record();                               \
    } while (0)
