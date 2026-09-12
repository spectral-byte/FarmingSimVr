#pragma once

#include <d3d12.h>
#include <memory>
#include "core/detour.h"
#include "graphics_telemetry_probe.h"

namespace fs25vr {

// Canonical Direct3D 12 vtable slot indices
inline constexpr size_t kExecuteCommandListsVTableSlot = 10;
inline constexpr size_t kRSSetViewportsVTableSlot = 21;
inline constexpr size_t kRSSetScissorRectsVTableSlot = 22;
inline constexpr size_t kOMSetRenderTargetsVTableSlot = 46;

class D3D12Interceptor {
public:
    static D3D12Interceptor& Instance();

    explicit D3D12Interceptor(std::shared_ptr<GraphicsTelemetryProbe> probe = nullptr);
    ~D3D12Interceptor();

    bool AttachCommandList(ID3D12GraphicsCommandList* pCmdList);
    bool AttachCommandQueue(ID3D12CommandQueue* pQueue);
    void DetachAll();

    bool IsCommandListAttached() const { return m_commandListAttached; }
    bool IsCommandQueueAttached() const { return m_commandQueueAttached; }

    void SetTelemetryProbe(std::shared_ptr<GraphicsTelemetryProbe> probe) {
        m_probe = probe ? std::move(probe) : std::shared_ptr<GraphicsTelemetryProbe>(&GraphicsTelemetryProbe::Instance(), [](auto*) {});
    }
    GraphicsTelemetryProbe* GetTelemetryProbe() const { return m_probe.get(); }

    // Detour entry points
    static void STDMETHODCALLTYPE Intercept_RSSetViewports(ID3D12GraphicsCommandList* self,
                                                           UINT NumViewports, const D3D12_VIEWPORT* pViewports);
    static void STDMETHODCALLTYPE Intercept_RSSetScissorRects(ID3D12GraphicsCommandList* self,
                                                             UINT NumRects, const D3D12_RECT* pRects);
    static void STDMETHODCALLTYPE Intercept_OMSetRenderTargets(ID3D12GraphicsCommandList* self,
                                                              UINT NumRenderTargetDescriptors,
                                                              const D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
                                                              BOOL RTsSingleHandleToDescriptorRange,
                                                              const D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor);
    static void STDMETHODCALLTYPE Intercept_ExecuteCommandLists(ID3D12CommandQueue* self,
                                                               UINT NumCommandLists,
                                                               ID3D12CommandList* const* ppCommandLists);

private:
    std::shared_ptr<GraphicsTelemetryProbe> m_probe;
    FunctionDetour m_detourViewports;
    FunctionDetour m_detourScissors;
    FunctionDetour m_detourRenderTargets;
    FunctionDetour m_detourExecuteLists;

    bool m_commandListAttached = false;
    bool m_commandQueueAttached = false;
};

} // namespace fs25vr
