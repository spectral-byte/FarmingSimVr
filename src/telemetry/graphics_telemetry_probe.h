#pragma once

#include <d3d12.h>
#include <vector>
#include <string>
#include <mutex>
#include <cstdint>
#include <functional>
#include <atomic>

namespace fs25vr {

struct ViewportInfo {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct ScissorRectInfo {
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
};

struct RenderPassInfo {
    std::vector<ViewportInfo> viewports;
    std::vector<ScissorRectInfo> scissorRects;
    std::vector<uint64_t> rtvHandles;
    uint32_t rtvCount = 0;
    bool hasDsv = false;
};

struct FrameTelemetry {
    uint64_t frameIndex = 0;
    std::vector<RenderPassInfo> passes;
    uint32_t maxConcurrentViewports = 0;
    uint32_t executedCommandListCount = 0;
    bool hasMultiViewportStereo = false;
    bool hasDualPassStereo = false;
};

enum class StereoVerdict {
    Inconclusive,
    NativeStereoDetected,
    AfrFallbackRequired
};

struct ProbeSnapshot {
    uint64_t frameIndex = 0;
    uint32_t maxViewports = 0;
    size_t passCount = 0;
    bool nativeStereo = false;
    bool afrFallback = false;
    StereoVerdict verdict = StereoVerdict::Inconclusive;
    std::string summary;
};

class GraphicsTelemetryProbe {
public:
    static GraphicsTelemetryProbe& Instance();

    GraphicsTelemetryProbe();

    void RecordViewports(UINT count, const D3D12_VIEWPORT* pViewports);
    void RecordScissorRects(UINT count, const D3D12_RECT* pRects);
    void RecordRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRTVs,
                             bool singleHandleRange, const D3D12_CPU_DESCRIPTOR_HANDLE* pDSV);
    void RecordExecuteCommandLists(UINT numLists, ID3D12CommandList* const* ppLists);
    void OnFrameBoundary();

    bool IsComplete() const;
    void SetMaxProbeFrames(uint32_t count);
    uint32_t GetMaxProbeFrames() const;
    void SetCompletionCallback(std::function<void()> callback);

    uint64_t GetCurrentFrameIndex() const;
    const FrameTelemetry& GetLastFrameTelemetry() const;
    const std::vector<FrameTelemetry>& GetFrameHistory() const;
    StereoVerdict GetStereoVerdict() const;
    bool HasNativeStereoDetected() const;
    bool RequiresAfrFallback() const;
    uint32_t GetMaxConcurrentViewportsObserved() const;
    std::string GenerateDiagnosticReport() const;
    ProbeSnapshot GetSnapshot() const;
    void Reset();

private:
    void EvaluateVerdictLocked();
    std::string GenerateDiagnosticReportLocked() const;

    mutable std::recursive_mutex m_mutex;
    uint64_t m_currentFrameIndex = 0;
    RenderPassInfo m_currentPass;
    bool m_passDirty = false;
    std::vector<RenderPassInfo> m_pendingPasses;
    uint32_t m_pendingCommandListCount = 0;

    FrameTelemetry m_lastFrame;
    std::vector<FrameTelemetry> m_history;
    uint32_t m_maxConcurrentViewportsObserved = 0;
    StereoVerdict m_verdict = StereoVerdict::Inconclusive;
    std::atomic<bool> m_isComplete{false};
    uint32_t m_maxProbeFrames = 5;
    std::function<void()> m_completionCallback;
};

} // namespace fs25vr
