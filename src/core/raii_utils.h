#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <utility>

namespace fs25vr {

/**
 * @brief RAII wrapper for Win32 event handles to prevent resource leaks during device resets or failures.
 */
class ScopedEventHandle {
public:
    ScopedEventHandle() noexcept : m_handle(nullptr) {}
    explicit ScopedEventHandle(HANDLE handle) noexcept : m_handle(handle) {}

    ~ScopedEventHandle() noexcept {
        Close();
    }

    ScopedEventHandle(const ScopedEventHandle&) = delete;
    ScopedEventHandle& operator=(const ScopedEventHandle&) = delete;

    ScopedEventHandle(ScopedEventHandle&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }

    ScopedEventHandle& operator=(ScopedEventHandle&& other) noexcept {
        if (this != &other) {
            Close();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    HANDLE Get() const noexcept { return m_handle; }
    operator HANDLE() const noexcept { return m_handle; }
    explicit operator bool() const noexcept { return IsValid(); }

    bool IsValid() const noexcept {
        return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
    }

    HANDLE Release() noexcept {
        HANDLE h = m_handle;
        m_handle = nullptr;
        return h;
    }

    void Reset(HANDLE handle = nullptr) noexcept {
        if (m_handle != handle) {
            Close();
            m_handle = handle;
        }
    }

    void Close() noexcept {
        if (IsValid()) {
            CloseHandle(m_handle);
            m_handle = nullptr;
        }
    }

    static ScopedEventHandle Create(LPSECURITY_ATTRIBUTES lpEventAttributes = nullptr,
                                    BOOL bManualReset = FALSE,
                                    BOOL bInitialState = FALSE,
                                    LPCSTR lpName = nullptr) {
        return ScopedEventHandle(CreateEventA(lpEventAttributes, bManualReset, bInitialState, lpName));
    }

private:
    HANDLE m_handle = nullptr;
};

/**
 * @brief RAII guard for mapping and automatically unmapping ID3D12Resource memory.
 */
template <typename T = uint8_t>
class ScopedResourceMap {
public:
    ScopedResourceMap(ID3D12Resource* resource, UINT subresource = 0, const D3D12_RANGE* readRange = nullptr)
        : m_resource(resource), m_subresource(subresource) {
        if (m_resource) {
            void* ptr = nullptr;
            m_hr = m_resource->Map(m_subresource, readRange, &ptr);
            if (SUCCEEDED(m_hr)) {
                m_data = static_cast<T*>(ptr);
            }
        }
    }

    ~ScopedResourceMap() noexcept {
        Unmap();
    }

    ScopedResourceMap(const ScopedResourceMap&) = delete;
    ScopedResourceMap& operator=(const ScopedResourceMap&) = delete;

    ScopedResourceMap(ScopedResourceMap&& other) noexcept
        : m_resource(other.m_resource), m_subresource(other.m_subresource),
          m_data(other.m_data), m_hr(other.m_hr) {
        other.m_resource = nullptr;
        other.m_data = nullptr;
        other.m_hr = E_FAIL;
    }

    ScopedResourceMap& operator=(ScopedResourceMap&& other) noexcept {
        if (this != &other) {
            Unmap();
            m_resource = other.m_resource;
            m_subresource = other.m_subresource;
            m_data = other.m_data;
            m_hr = other.m_hr;
            other.m_resource = nullptr;
            other.m_data = nullptr;
            other.m_hr = E_FAIL;
        }
        return *this;
    }

    bool Succeeded() const noexcept { return SUCCEEDED(m_hr) && m_data != nullptr; }
    explicit operator bool() const noexcept { return Succeeded(); }
    T* Get() const noexcept { return m_data; }
    T* operator->() const noexcept { return m_data; }
    T& operator[](size_t index) const noexcept { return m_data[index]; }

    void Unmap(const D3D12_RANGE* writtenRange = nullptr) noexcept {
        if (m_resource && SUCCEEDED(m_hr)) {
            m_resource->Unmap(m_subresource, writtenRange);
            m_resource = nullptr;
            m_data = nullptr;
            m_hr = E_FAIL;
        }
    }

private:
    ID3D12Resource* m_resource = nullptr;
    UINT m_subresource = 0;
    T* m_data = nullptr;
    HRESULT m_hr = E_FAIL;
};

/**
 * @brief RAII guard ensuring ID3D12GraphicsCommandList is properly closed.
 */
class ScopedCommandListRecording {
public:
    explicit ScopedCommandListRecording(ID3D12GraphicsCommandList* cmdList) noexcept
        : m_cmdList(cmdList) {}

    ~ScopedCommandListRecording() noexcept {
        Close();
    }

    ScopedCommandListRecording(const ScopedCommandListRecording&) = delete;
    ScopedCommandListRecording& operator=(const ScopedCommandListRecording&) = delete;

    HRESULT Close() noexcept {
        if (m_cmdList && !m_closed) {
            m_closed = true;
            return m_cmdList->Close();
        }
        return S_OK;
    }

private:
    ID3D12GraphicsCommandList* m_cmdList = nullptr;
    bool m_closed = false;
};

} // namespace fs25vr
