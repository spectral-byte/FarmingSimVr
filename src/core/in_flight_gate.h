#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace fs25vr {

struct InFlightCallGate {
    InFlightCallGate() = default;
    InFlightCallGate(const InFlightCallGate&) = delete;
    InFlightCallGate& operator=(const InFlightCallGate&) = delete;

    void Begin() {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_activeCalls;
    }

    void End() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_activeCalls > 0) {
            --m_activeCalls;
            if (m_activeCalls == 0) {
                m_cv.notify_all();
            }
        }
    }

    void WaitForZero() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_activeCalls == 0; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    uint32_t m_activeCalls = 0;
};

template <typename T>
class InFlightLease {
public:
    InFlightLease() = default;
    explicit InFlightLease(std::shared_ptr<T> ptr) : m_ptr(std::move(ptr)) {
        if (m_ptr) {
            m_ptr->callGate.Begin();
        }
    }
    ~InFlightLease() {
        Reset();
    }

    InFlightLease(const InFlightLease&) = delete;
    InFlightLease& operator=(const InFlightLease&) = delete;

    InFlightLease(InFlightLease&& other) noexcept : m_ptr(std::move(other.m_ptr)) {}
    InFlightLease& operator=(InFlightLease&& other) noexcept {
        if (this != &other) {
            Reset();
            m_ptr = std::move(other.m_ptr);
        }
        return *this;
    }

    void Reset() {
        if (m_ptr) {
            m_ptr->callGate.End();
            m_ptr.reset();
        }
    }

    T* get() const { return m_ptr.get(); }
    T* operator->() const { return m_ptr.get(); }
    explicit operator bool() const { return m_ptr != nullptr; }

private:
    std::shared_ptr<T> m_ptr;
};

}
