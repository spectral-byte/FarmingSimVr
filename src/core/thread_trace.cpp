#include "core/thread_trace.h"

#include "core/logger.h"

#include <string>
#include <windows.h>

namespace fs25vr {

bool ThreadTrace::Record(uint32_t threadId) {
    // Steady state is a plain load; only a different thread pays for the exchange.
    if (m_lastThread.load(std::memory_order_relaxed) == threadId) return false;
    uint32_t previous = m_lastThread.exchange(threadId);
    if (previous == threadId) return false;

    std::string message = std::string("[ThreadTrace] ") + m_site + " on thread " +
                          std::to_string(threadId);
    if (previous == kNoThread) {
        LogInfo(message);
        // Info lines are buffered; flush so a crash during the session can't lose the sighting.
        Logger::Instance().Flush();
        return true;
    }

    if (m_changeLogs.load(std::memory_order_relaxed) > kMaxChangeLogs) return false;
    uint32_t changes = m_changeLogs.fetch_add(1);
    if (changes > kMaxChangeLogs) return false;
    if (changes == kMaxChangeLogs) {
        LogWarn(std::string("[ThreadTrace] ") + m_site +
                " keeps changing threads; further changes are not logged");
        return true;
    }
    LogWarn(message + " (was " + std::to_string(previous) + ")");
    return true;
}

bool ThreadTrace::Record() {
    return Record(static_cast<uint32_t>(GetCurrentThreadId()));
}

} // namespace fs25vr
