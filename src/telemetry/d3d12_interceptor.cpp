#include "d3d12_interceptor.h"
#include "core/logger.h"

namespace fs25vr {

namespace {
static D3D12Interceptor* s_activeInterceptor = nullptr;
} // anonymous namespace

D3D12Interceptor& D3D12Interceptor::Instance() {
    static D3D12Interceptor instance;
    return instance;
}

D3D12Interceptor::D3D12Interceptor(std::shared_ptr<GraphicsTelemetryProbe> probe)
    : m_probe(probe ? std::move(probe) : std::shared_ptr<GraphicsTelemetryProbe>(&GraphicsTelemetryProbe::Instance(), [](auto*) {})) {
}

D3D12Interceptor::~D3D12Interceptor() {
    DetachAll();
}

bool D3D12Interceptor::AttachCommandList(ID3D12GraphicsCommandList* pCmdList) {
    if (!pCmdList) {
        LogError("D3D12Interceptor::AttachCommandList: Command list pointer is null.");
        return false;
    }
    if (m_commandListAttached) return true;

    s_activeInterceptor = this;
    void** vtable = *reinterpret_cast<void***>(pCmdList);
    if (!vtable) {
        LogError("D3D12Interceptor::AttachCommandList: VTable pointer is null.");
        return false;
    }

    void* fnViewports = vtable[kRSSetViewportsVTableSlot];
    void* fnScissors = vtable[kRSSetScissorRectsVTableSlot];
    void* fnRenderTargets = vtable[kOMSetRenderTargetsVTableSlot];

    bool viewportsAttached = m_detourViewports.Install(fnViewports, reinterpret_cast<void*>(&D3D12Interceptor::Intercept_RSSetViewports));
    bool scissorsAttached = m_detourScissors.Install(fnScissors, reinterpret_cast<void*>(&D3D12Interceptor::Intercept_RSSetScissorRects));
    bool renderTargetsAttached = m_detourRenderTargets.Install(fnRenderTargets, reinterpret_cast<void*>(&D3D12Interceptor::Intercept_OMSetRenderTargets));

    m_commandListAttached = viewportsAttached && scissorsAttached && renderTargetsAttached;
    if (!m_commandListAttached) {
        LogError("D3D12Interceptor::AttachCommandList: Failed to install command list detours.");
        m_detourViewports.Uninstall();
        m_detourScissors.Uninstall();
        m_detourRenderTargets.Uninstall();
    } else {
        LogInfo("D3D12Interceptor: Successfully attached detours to command list methods.");
    }
    return m_commandListAttached;
}

bool D3D12Interceptor::AttachCommandQueue(ID3D12CommandQueue* pQueue) {
    if (!pQueue) {
        LogError("D3D12Interceptor::AttachCommandQueue: Command queue pointer is null.");
        return false;
    }
    if (m_commandQueueAttached) return true;

    s_activeInterceptor = this;
    void** vtable = *reinterpret_cast<void***>(pQueue);
    if (!vtable) {
        LogError("D3D12Interceptor::AttachCommandQueue: VTable pointer is null.");
        return false;
    }

    void* fnExecute = vtable[kExecuteCommandListsVTableSlot];
    m_commandQueueAttached = m_detourExecuteLists.Install(fnExecute, reinterpret_cast<void*>(&D3D12Interceptor::Intercept_ExecuteCommandLists));
    if (m_commandQueueAttached) {
        LogInfo("D3D12Interceptor: Successfully attached detour to command queue ExecuteCommandLists.");
    } else {
        LogError("D3D12Interceptor::AttachCommandQueue: Failed to install ExecuteCommandLists detour.");
    }
    return m_commandQueueAttached;
}

void D3D12Interceptor::DetachAll() {
    if (m_detourViewports.IsInstalled()) m_detourViewports.Uninstall();
    if (m_detourScissors.IsInstalled()) m_detourScissors.Uninstall();
    if (m_detourRenderTargets.IsInstalled()) m_detourRenderTargets.Uninstall();
    if (m_detourExecuteLists.IsInstalled()) m_detourExecuteLists.Uninstall();
    m_commandListAttached = false;
    m_commandQueueAttached = false;
    if (s_activeInterceptor == this) {
        s_activeInterceptor = nullptr;
    }
    LogInfo("D3D12Interceptor: Detached all detours.");
}

void STDMETHODCALLTYPE D3D12Interceptor::Intercept_RSSetViewports(ID3D12GraphicsCommandList* self,
                                                                  UINT NumViewports, const D3D12_VIEWPORT* pViewports) {
    if (s_activeInterceptor && s_activeInterceptor->m_probe && !s_activeInterceptor->m_probe->IsComplete()) {
        s_activeInterceptor->m_probe->RecordViewports(NumViewports, pViewports);
    }
    auto orig = reinterpret_cast<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*)>(
        s_activeInterceptor ? s_activeInterceptor->m_detourViewports.GetOriginal() : nullptr);
    if (orig) orig(self, NumViewports, pViewports);
}

void STDMETHODCALLTYPE D3D12Interceptor::Intercept_RSSetScissorRects(ID3D12GraphicsCommandList* self,
                                                                    UINT NumRects, const D3D12_RECT* pRects) {
    if (s_activeInterceptor && s_activeInterceptor->m_probe && !s_activeInterceptor->m_probe->IsComplete()) {
        s_activeInterceptor->m_probe->RecordScissorRects(NumRects, pRects);
    }
    auto orig = reinterpret_cast<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*)>(
        s_activeInterceptor ? s_activeInterceptor->m_detourScissors.GetOriginal() : nullptr);
    if (orig) orig(self, NumRects, pRects);
}

void STDMETHODCALLTYPE D3D12Interceptor::Intercept_OMSetRenderTargets(ID3D12GraphicsCommandList* self,
                                                                     UINT NumRenderTargetDescriptors,
                                                                     const D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
                                                                     BOOL RTsSingleHandleToDescriptorRange,
                                                                     const D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor) {
    if (s_activeInterceptor && s_activeInterceptor->m_probe && !s_activeInterceptor->m_probe->IsComplete()) {
        s_activeInterceptor->m_probe->RecordRenderTargets(NumRenderTargetDescriptors, pRenderTargetDescriptors, RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
    }
    auto orig = reinterpret_cast<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*)>(
        s_activeInterceptor ? s_activeInterceptor->m_detourRenderTargets.GetOriginal() : nullptr);
    if (orig) orig(self, NumRenderTargetDescriptors, pRenderTargetDescriptors, RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
}

void STDMETHODCALLTYPE D3D12Interceptor::Intercept_ExecuteCommandLists(ID3D12CommandQueue* self,
                                                                      UINT NumCommandLists,
                                                                      ID3D12CommandList* const* ppCommandLists) {
    if (s_activeInterceptor && s_activeInterceptor->m_probe && !s_activeInterceptor->m_probe->IsComplete()) {
        s_activeInterceptor->m_probe->RecordExecuteCommandLists(NumCommandLists, ppCommandLists);
        // Automatically attach command list detours if any passed command list is a graphics command list
        if (ppCommandLists && NumCommandLists > 0 && !s_activeInterceptor->m_commandListAttached) {
            for (UINT i = 0; i < NumCommandLists; ++i) {
                if (ppCommandLists[i]) {
                    ID3D12GraphicsCommandList* graphicsList = nullptr;
                    if (SUCCEEDED(ppCommandLists[i]->QueryInterface(IID_PPV_ARGS(&graphicsList)))) {
                        s_activeInterceptor->AttachCommandList(graphicsList);
                        graphicsList->Release();
                        break;
                    }
                }
            }
        }
    }
    auto orig = reinterpret_cast<void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*)>(
        s_activeInterceptor ? s_activeInterceptor->m_detourExecuteLists.GetOriginal() : nullptr);
    if (orig) orig(self, NumCommandLists, ppCommandLists);
}

} // namespace fs25vr
