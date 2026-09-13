#include <gtest/gtest.h>
#include "telemetry/d3d12_interceptor.h"
#include "telemetry/graphics_telemetry_probe.h"
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace fs25vr {

namespace {

static int s_mockVpCalls = 0;
static int s_mockScissorCalls = 0;
static int s_mockRtCalls = 0;
static int s_mockExecuteCalls = 0;

__declspec(noinline) void STDMETHODCALLTYPE MockRSSetViewports(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*) {
    volatile int a = 1;
    volatile int b = 2;
    volatile int c = a + b;
    s_mockVpCalls += c;
}

__declspec(noinline) void STDMETHODCALLTYPE MockRSSetScissorRects(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*) {
    volatile int a = 3;
    volatile int b = 4;
    volatile int c = a + b;
    s_mockScissorCalls += c;
}

__declspec(noinline) void STDMETHODCALLTYPE MockOMSetRenderTargets(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*) {
    volatile int a = 5;
    volatile int b = 6;
    volatile int c = a + b;
    s_mockRtCalls += c;
}

__declspec(noinline) void STDMETHODCALLTYPE MockExecuteCommandLists(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) {
    volatile int a = 7;
    volatile int b = 8;
    volatile int c = a + b;
    s_mockExecuteCalls += c;
}

__declspec(noinline) HRESULT STDMETHODCALLTYPE MockQueryInterface(ID3D12CommandList* self, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(ID3D12GraphicsCommandList) || riid == __uuidof(ID3D12CommandList) || riid == __uuidof(IUnknown)) {
        *ppv = self;
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

__declspec(noinline) ULONG STDMETHODCALLTYPE MockAddRef(IUnknown*) {
    return 2;
}

__declspec(noinline) ULONG STDMETHODCALLTYPE MockRelease(IUnknown*) {
    return 1;
}

struct MockComObject {
    void** vtable;
};

} // anonymous namespace

class D3D12InstrumentationTest : public ::testing::Test {
protected:
    std::shared_ptr<GraphicsTelemetryProbe> probe;
    std::unique_ptr<D3D12Interceptor> interceptor;

    void SetUp() override {
        s_mockVpCalls = 0;
        s_mockScissorCalls = 0;
        s_mockRtCalls = 0;
        s_mockExecuteCalls = 0;

        probe = std::make_shared<GraphicsTelemetryProbe>();
        probe->Reset();
        interceptor = std::make_unique<D3D12Interceptor>(probe);
    }

    void TearDown() override {
        interceptor->DetachAll();
    }
};

TEST_F(D3D12InstrumentationTest, RejectsNullPointersGracefully) {
    EXPECT_FALSE(interceptor->AttachCommandList(nullptr));
    EXPECT_FALSE(interceptor->AttachCommandQueue(nullptr));
    EXPECT_FALSE(interceptor->IsCommandListAttached());
    EXPECT_FALSE(interceptor->IsCommandQueueAttached());
}

TEST_F(D3D12InstrumentationTest, HooksAndInterceptsMockCommandList) {
    std::vector<void*> vtable(60, nullptr);
    vtable[0] = reinterpret_cast<void*>(&MockQueryInterface);
    vtable[1] = reinterpret_cast<void*>(&MockAddRef);
    vtable[2] = reinterpret_cast<void*>(&MockRelease);
    vtable[kRSSetViewportsVTableSlot] = reinterpret_cast<void*>(&MockRSSetViewports);
    vtable[kRSSetScissorRectsVTableSlot] = reinterpret_cast<void*>(&MockRSSetScissorRects);
    vtable[kOMSetRenderTargetsVTableSlot] = reinterpret_cast<void*>(&MockOMSetRenderTargets);

    MockComObject mockObj{ vtable.data() };
    auto* pCmdList = reinterpret_cast<ID3D12GraphicsCommandList*>(&mockObj);

    ASSERT_TRUE(interceptor->AttachCommandList(pCmdList));
    EXPECT_TRUE(interceptor->IsCommandListAttached());

    D3D12_VIEWPORT vp{ 0.0f, 0.0f, 2560.0f, 1440.0f, 0.0f, 1.0f };
    D3D12_RECT scissor{ 0, 0, 2560, 1440 };
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{ 0x8000 };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{ 0x9000 };

    pCmdList->RSSetViewports(1, &vp);
    EXPECT_GT(s_mockVpCalls, 0);

    pCmdList->RSSetScissorRects(1, &scissor);
    EXPECT_GT(s_mockScissorCalls, 0);

    pCmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    EXPECT_GT(s_mockRtCalls, 0);

    probe->OnFrameBoundary();

    const auto& last = probe->GetLastFrameTelemetry();
    ASSERT_EQ(last.passes.size(), 1u);
    ASSERT_EQ(last.passes[0].viewports.size(), 1u);
    EXPECT_FLOAT_EQ(last.passes[0].viewports[0].width, 2560.0f);
    EXPECT_FLOAT_EQ(last.passes[0].viewports[0].height, 1440.0f);
    ASSERT_EQ(last.passes[0].scissorRects.size(), 1u);
    EXPECT_EQ(last.passes[0].scissorRects[0].right, 2560);
    EXPECT_EQ(last.passes[0].rtvCount, 1u);
    ASSERT_EQ(last.passes[0].rtvHandles.size(), 1u);
    EXPECT_EQ(last.passes[0].rtvHandles[0], 0x8000ULL);
    EXPECT_TRUE(last.passes[0].hasDsv);

    interceptor->DetachAll();
    EXPECT_FALSE(interceptor->IsCommandListAttached());
}

TEST_F(D3D12InstrumentationTest, HooksAndInterceptsMockCommandQueue) {
    std::vector<void*> vtable(20, nullptr);
    vtable[0] = reinterpret_cast<void*>(&MockQueryInterface);
    vtable[1] = reinterpret_cast<void*>(&MockAddRef);
    vtable[2] = reinterpret_cast<void*>(&MockRelease);
    vtable[kExecuteCommandListsVTableSlot] = reinterpret_cast<void*>(&MockExecuteCommandLists);

    MockComObject mockObj{ vtable.data() };
    auto* pQueue = reinterpret_cast<ID3D12CommandQueue*>(&mockObj);

    ASSERT_TRUE(interceptor->AttachCommandQueue(pQueue));
    EXPECT_TRUE(interceptor->IsCommandQueueAttached());

    ID3D12CommandList* dummyLists[] = { nullptr };
    pQueue->ExecuteCommandLists(1, dummyLists);
    EXPECT_GT(s_mockExecuteCalls, 0);

    probe->OnFrameBoundary();
    const auto& last = probe->GetLastFrameTelemetry();
    EXPECT_EQ(last.executedCommandListCount, 1u);

    interceptor->DetachAll();
    EXPECT_FALSE(interceptor->IsCommandQueueAttached());
}

TEST_F(D3D12InstrumentationTest, CommandQueueAutoAttachesCommandListOnExecute) {
    std::vector<void*> queueVTable(20, nullptr);
    queueVTable[0] = reinterpret_cast<void*>(&MockQueryInterface);
    queueVTable[1] = reinterpret_cast<void*>(&MockAddRef);
    queueVTable[2] = reinterpret_cast<void*>(&MockRelease);
    queueVTable[kExecuteCommandListsVTableSlot] = reinterpret_cast<void*>(&MockExecuteCommandLists);
    MockComObject mockQueueObj{ queueVTable.data() };
    auto* pQueue = reinterpret_cast<ID3D12CommandQueue*>(&mockQueueObj);

    std::vector<void*> cmdListVTable(60, nullptr);
    cmdListVTable[0] = reinterpret_cast<void*>(&MockQueryInterface);
    cmdListVTable[1] = reinterpret_cast<void*>(&MockAddRef);
    cmdListVTable[2] = reinterpret_cast<void*>(&MockRelease);
    cmdListVTable[kRSSetViewportsVTableSlot] = reinterpret_cast<void*>(&MockRSSetViewports);
    cmdListVTable[kRSSetScissorRectsVTableSlot] = reinterpret_cast<void*>(&MockRSSetScissorRects);
    cmdListVTable[kOMSetRenderTargetsVTableSlot] = reinterpret_cast<void*>(&MockOMSetRenderTargets);
    MockComObject mockCmdListObj{ cmdListVTable.data() };
    auto* pCmdList = reinterpret_cast<ID3D12CommandList*>(&mockCmdListObj);

    ASSERT_TRUE(interceptor->AttachCommandQueue(pQueue));
    EXPECT_FALSE(interceptor->IsCommandListAttached());

    ID3D12CommandList* lists[] = { pCmdList };
    pQueue->ExecuteCommandLists(1, lists);

    // Auto-attachment must succeed
    EXPECT_TRUE(interceptor->IsCommandListAttached());

    interceptor->DetachAll();
}

TEST_F(D3D12InstrumentationTest, RealD3D12DeviceInstrumentationIfAvailable) {
    ComPtr<ID3D12Device> device;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        ComPtr<IDXGIFactory4> dxgiFactory;
        if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)))) {
            ComPtr<IDXGIAdapter> warpAdapter;
            if (SUCCEEDED(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)))) {
                hr = D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
            }
        }
    }

    if (SUCCEEDED(hr) && device) {
        ComPtr<ID3D12CommandAllocator> allocator;
        ASSERT_EQ(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), S_OK);

        ComPtr<ID3D12GraphicsCommandList> cmdList;
        ASSERT_EQ(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmdList)), S_OK);

        D3D12_COMMAND_QUEUE_DESC qDesc{};
        qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue;
        ASSERT_EQ(device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&queue)), S_OK);

        EXPECT_TRUE(interceptor->AttachCommandList(cmdList.Get()));
        EXPECT_TRUE(interceptor->AttachCommandQueue(queue.Get()));

        D3D12_VIEWPORT vp{ 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f };
        D3D12_RECT scissor{ 0, 0, 1920, 1080 };
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &scissor);
        cmdList->Close();

        ID3D12CommandList* lists[] = { cmdList.Get() };
        queue->ExecuteCommandLists(1, lists);

        probe->OnFrameBoundary();

        const auto& last = probe->GetLastFrameTelemetry();
        ASSERT_EQ(last.passes.size(), 1u);
        ASSERT_EQ(last.passes[0].viewports.size(), 1u);
        EXPECT_FLOAT_EQ(last.passes[0].viewports[0].width, 1920.0f);
        EXPECT_FLOAT_EQ(last.passes[0].viewports[0].height, 1080.0f);
        // Synchronize GPU queue execution before destroying D3D12 objects
        ComPtr<ID3D12Fence> fence;
        ASSERT_EQ(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), S_OK);
        HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        queue->Signal(fence.Get(), 1);
        fence->SetEventOnCompletion(1, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
        CloseHandle(fenceEvent);

        interceptor->DetachAll();
        EXPECT_FALSE(interceptor->IsCommandListAttached());
        EXPECT_FALSE(interceptor->IsCommandQueueAttached());
    }
}

} // namespace fs25vr
