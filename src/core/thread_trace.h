#pragma once

#include <atomic>
#include <cstdint>

namespace fs25vr {

class ThreadTrace {
public:
    static constexpr uint32_t kMaxChangeLogs = 8;

    explicit ThreadTrace(const char* site) : m_site(site) {}

    bool Record(uint32_t threadId);
    bool Record();

private:
    static constexpr uint32_t kNoThread = 0;

    const char* m_site;
    std::atomic<uint32_t> m_lastThread{kNoThread};
    std::atomic<uint32_t> m_changeLogs{0};
};

}

#define FS25VR_TRACE_THREAD(site)                                  \
    do {                                                           \
        static ::fs25vr::ThreadTrace fs25vrThreadTrace_(site);     \
        fs25vrThreadTrace_.Record();                               \
    } while (0)
