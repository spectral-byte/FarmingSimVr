#include <gtest/gtest.h>
#include "proxy/dxgi_proxy.h"
#include "proxy/mirror_window_controller.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <optional>

using Microsoft::WRL::ComPtr;

namespace fs25vr {
namespace {

// A D3D12 device or queue wrapper as ReShade hands them out: another object, with its methods in another module (this
// test executable rather than D3D12Core.dll), forwarding to the real object. Only the calls the swap makes are
// implemented; any other slot records that it was called.
class FakeD3D12Wrapper {
    void* const* m_vtable;  // first, as in any COM object

public:
    enum class Kind { Device, Queue };

    FakeD3D12Wrapper(Kind kind, IUnknown* inner) : m_vtable(kind == Kind::Device ? DeviceVtable() : QueueVtable()),
                                                  m_kind(kind), m_inner(inner) {}

    IUnknown* AsUnknown() { return reinterpret_cast<IUnknown*>(this); }
    // A device wrapper on another adapter reports this LUID instead of the real one.
    std::optional<LUID> adapterLuid;
    static bool s_unexpectedCall;

private:
    // ABI-fixed slots: IUnknown 0-2, ID3D12Object 3-6, then ID3D12Device::GetAdapterLuid at 43;
    // ID3D12DeviceChild::GetDevice at 7 and ID3D12CommandQueue::GetDesc at 18.
    static constexpr size_t kDeviceSlots = 44;
    static constexpr size_t kQueueSlots = 19;
    static constexpr size_t kGetAdapterLuidSlot = 43;
    static constexpr size_t kGetDeviceSlot = 7;
    static constexpr size_t kGetDescSlot = 18;

    static FakeD3D12Wrapper* Self(void* self) { return static_cast<FakeD3D12Wrapper*>(self); }

    static HRESULT STDMETHODCALLTYPE QueryInterface(void* self, REFIID riid, void** out) {
        const bool wrapped = Self(self)->m_kind == Kind::Device ? riid == __uuidof(ID3D12Device)
                                                                : riid == __uuidof(ID3D12CommandQueue);
        if (riid != __uuidof(IUnknown) && !wrapped) {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        *out = self;
        return S_OK;
    }
    // The wrapper lives on the test's stack, so references are not counted.
    static ULONG STDMETHODCALLTYPE AddRef(void*) { return 2; }
    static ULONG STDMETHODCALLTYPE Release(void*) { return 1; }
    // Members returning a struct take a hidden result pointer after `this` and return it.
    static LUID* STDMETHODCALLTYPE GetAdapterLuid(void* self, LUID* result) {
        FakeD3D12Wrapper* wrapper = Self(self);
        *result = wrapper->adapterLuid ? *wrapper->adapterLuid
                                       : static_cast<ID3D12Device*>(wrapper->m_inner)->GetAdapterLuid();
        return result;
    }
    static HRESULT STDMETHODCALLTYPE GetDevice(void* self, REFIID riid, void** device) {
        return static_cast<ID3D12CommandQueue*>(Self(self)->m_inner)->GetDevice(riid, device);
    }
    static D3D12_COMMAND_QUEUE_DESC* STDMETHODCALLTYPE GetDesc(void* self, D3D12_COMMAND_QUEUE_DESC* result) {
        *result = static_cast<ID3D12CommandQueue*>(Self(self)->m_inner)->GetDesc();
        return result;
    }
    static HRESULT STDMETHODCALLTYPE Unexpected() {
        s_unexpectedCall = true;
        return E_NOTIMPL;
    }

    template <size_t N>
    static std::array<void*, N> BaseVtable() {
        std::array<void*, N> slots{};
        slots.fill(reinterpret_cast<void*>(&Unexpected));
        slots[0] = reinterpret_cast<void*>(&QueryInterface);
        slots[1] = reinterpret_cast<void*>(&AddRef);
        slots[2] = reinterpret_cast<void*>(&Release);
        return slots;
    }
    static void* const* DeviceVtable() {
        static const std::array<void*, kDeviceSlots> slots = [] {
            auto s = BaseVtable<kDeviceSlots>();
            s[kGetAdapterLuidSlot] = reinterpret_cast<void*>(&GetAdapterLuid);
            return s;
        }();
        return slots.data();
    }
    static void* const* QueueVtable() {
        static const std::array<void*, kQueueSlots> slots = [] {
            auto s = BaseVtable<kQueueSlots>();
            s[kGetDeviceSlot] = reinterpret_cast<void*>(&GetDevice);
            s[kGetDescSlot] = reinterpret_cast<void*>(&GetDesc);
            return s;
        }();
        return slots.data();
    }

    Kind m_kind;
    IUnknown* m_inner;
};

bool FakeD3D12Wrapper::s_unexpectedCall = false;

class D3D11On12UnwrapTest : public ::testing::Test {
protected:
    void SetUp() override {
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> warp;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
            FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)))) {
            GTEST_SKIP() << "no WARP D3D12 device";
        }
        D3D12_COMMAND_QUEUE_DESC direct{D3D12_COMMAND_LIST_TYPE_DIRECT};
        D3D12_COMMAND_QUEUE_DESC copy{D3D12_COMMAND_LIST_TYPE_COPY};
        ASSERT_TRUE(SUCCEEDED(m_device->CreateCommandQueue(&direct, IID_PPV_ARGS(&m_queue))));
        ASSERT_TRUE(SUCCEEDED(m_device->CreateCommandQueue(&copy, IID_PPV_ARGS(&m_copyQueue))));
        m_proxy = std::make_unique<DxgiProxy>(nullptr, nullptr, nullptr, nullptr,
                                              std::make_shared<MirrorWindowController>(), nullptr);
        m_proxy->SetD3D12DeviceAndQueue(m_device.Get(), m_queue.Get());
        FakeD3D12Wrapper::s_unexpectedCall = false;
    }
    void TearDown() override { EXPECT_FALSE(FakeD3D12Wrapper::s_unexpectedCall); }

    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<ID3D12CommandQueue> m_copyQueue;
    std::unique_ptr<DxgiProxy> m_proxy;
};

TEST_F(D3D11On12UnwrapTest, SwapsAWrappedDeviceForTheGamesRealDevice) {
    FakeD3D12Wrapper wrapper(FakeD3D12Wrapper::Kind::Device, m_device.Get());
    IUnknown* device = wrapper.AsUnknown();
    IUnknown* queues[] = {m_queue.Get()};

    m_proxy->UnwrapForD3D11On12(device, queues, 1);

    EXPECT_EQ(static_cast<void*>(device), static_cast<void*>(m_device.Get()));
    EXPECT_EQ(static_cast<void*>(queues[0]), static_cast<void*>(m_queue.Get()));
}

TEST_F(D3D11On12UnwrapTest, SwapsAWrappedQueueOfTheSwapChainQueuesType) {
    FakeD3D12Wrapper wrapper(FakeD3D12Wrapper::Kind::Queue, m_queue.Get());
    IUnknown* device = m_device.Get();
    IUnknown* queues[] = {wrapper.AsUnknown()};

    m_proxy->UnwrapForD3D11On12(device, queues, 1);

    EXPECT_EQ(static_cast<void*>(queues[0]), static_cast<void*>(m_queue.Get()));
}

TEST_F(D3D11On12UnwrapTest, LeavesTheRealDeviceAndQueueAsTheyAre) {
    IUnknown* device = m_device.Get();
    IUnknown* queues[] = {m_queue.Get()};

    m_proxy->UnwrapForD3D11On12(device, queues, 1);

    EXPECT_EQ(static_cast<void*>(device), static_cast<void*>(m_device.Get()));
    EXPECT_EQ(static_cast<void*>(queues[0]), static_cast<void*>(m_queue.Get()));
}

TEST_F(D3D11On12UnwrapTest, LeavesADeviceWrapperOnAnotherAdapterAlone) {
    FakeD3D12Wrapper wrapper(FakeD3D12Wrapper::Kind::Device, m_device.Get());
    LUID other = m_device->GetAdapterLuid();
    other.LowPart += 1;
    wrapper.adapterLuid = other;
    IUnknown* device = wrapper.AsUnknown();

    m_proxy->UnwrapForD3D11On12(device, nullptr, 0);

    EXPECT_EQ(static_cast<void*>(device), static_cast<void*>(wrapper.AsUnknown()));
}

TEST_F(D3D11On12UnwrapTest, LeavesAWrappedQueueOfAnotherTypeAlone) {
    FakeD3D12Wrapper wrapper(FakeD3D12Wrapper::Kind::Queue, m_copyQueue.Get());
    IUnknown* device = m_device.Get();
    IUnknown* queues[] = {wrapper.AsUnknown()};

    m_proxy->UnwrapForD3D11On12(device, queues, 1);

    EXPECT_EQ(static_cast<void*>(queues[0]), static_cast<void*>(wrapper.AsUnknown()));
}

TEST_F(D3D11On12UnwrapTest, SwapsNothingBeforeTheGameHasADevice) {
    DxgiProxy fresh(nullptr, nullptr, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);
    FakeD3D12Wrapper wrapper(FakeD3D12Wrapper::Kind::Device, m_device.Get());
    IUnknown* device = wrapper.AsUnknown();

    fresh.UnwrapForD3D11On12(device, nullptr, 0);

    EXPECT_EQ(static_cast<void*>(device), static_cast<void*>(wrapper.AsUnknown()));
}

} // namespace
} // namespace fs25vr
