#pragma once

#include "openxr/openxr_dispatch.h"
#include <cstring>
#include <deque>

namespace fs25vr {

// FIFO event queue for fake OpenXR dispatches. Fakes forward PollEvent() to Poll().
class FakeOpenXrEventQueue {
public:
    void PushSessionState(XrSessionState state) {
        XrEventDataSessionStateChanged ev{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
        ev.state = state;
        m_events.push_back(Pack(ev));
    }

    void PushInstanceLossPending() {
        XrEventDataInstanceLossPending ev{XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING};
        m_events.push_back(Pack(ev));
    }

    XrResult Poll(XrEventDataBuffer* out) {
        if (m_events.empty()) {
            return XR_EVENT_UNAVAILABLE;
        }
        *out = m_events.front();
        m_events.pop_front();
        return XR_SUCCESS;
    }

    size_t Pending() const { return m_events.size(); }

private:
    template <typename T>
    static XrEventDataBuffer Pack(const T& ev) {
        static_assert(sizeof(T) <= sizeof(XrEventDataBuffer), "event too large");
        XrEventDataBuffer buf{XR_TYPE_EVENT_DATA_BUFFER};
        memcpy(&buf, &ev, sizeof(T));
        return buf;
    }

    std::deque<XrEventDataBuffer> m_events;
};

} // namespace fs25vr
