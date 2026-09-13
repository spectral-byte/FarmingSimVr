#include "graphics_telemetry_probe.h"
#include "core/logger.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace fs25vr {

GraphicsTelemetryProbe& GraphicsTelemetryProbe::Instance() {
    static GraphicsTelemetryProbe instance;
    return instance;
}

GraphicsTelemetryProbe::GraphicsTelemetryProbe() = default;

void GraphicsTelemetryProbe::RecordViewports(UINT count, const D3D12_VIEWPORT* pViewports) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_isComplete) return;

    if (m_passDirty && !m_currentPass.viewports.empty()) {
        m_pendingPasses.push_back(m_currentPass);
        m_currentPass = RenderPassInfo{};
    }

    m_currentPass.viewports.clear();
    if (pViewports && count > 0) {
        m_currentPass.viewports.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            ViewportInfo info;
            info.x = pViewports[i].TopLeftX;
            info.y = pViewports[i].TopLeftY;
            info.width = pViewports[i].Width;
            info.height = pViewports[i].Height;
            info.minDepth = pViewports[i].MinDepth;
            info.maxDepth = pViewports[i].MaxDepth;
            m_currentPass.viewports.push_back(info);
        }
    }
    m_passDirty = true;
}

void GraphicsTelemetryProbe::RecordScissorRects(UINT count, const D3D12_RECT* pRects) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_isComplete) return;

    m_currentPass.scissorRects.clear();
    if (pRects && count > 0) {
        m_currentPass.scissorRects.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            ScissorRectInfo info;
            info.left = pRects[i].left;
            info.top = pRects[i].top;
            info.right = pRects[i].right;
            info.bottom = pRects[i].bottom;
            m_currentPass.scissorRects.push_back(info);
        }
    }
    m_passDirty = true;
}

void GraphicsTelemetryProbe::RecordRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRTVs,
                                                 bool /*singleHandleRange*/, const D3D12_CPU_DESCRIPTOR_HANDLE* pDSV) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_isComplete) return;

    if (m_passDirty && !m_currentPass.rtvHandles.empty()) {
        m_pendingPasses.push_back(m_currentPass);
        m_currentPass = RenderPassInfo{};
    }
    m_currentPass.rtvCount = numRTVs;
    m_currentPass.rtvHandles.clear();
    if (pRTVs && numRTVs > 0) {
        m_currentPass.rtvHandles.reserve(numRTVs);
        for (UINT i = 0; i < numRTVs; ++i) {
            m_currentPass.rtvHandles.push_back(pRTVs[i].ptr);
        }
    }
    m_currentPass.hasDsv = (pDSV != nullptr && pDSV->ptr != 0);
    m_passDirty = true;
}

void GraphicsTelemetryProbe::RecordExecuteCommandLists(UINT numLists, ID3D12CommandList* const* /*ppLists*/) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_isComplete) return;
    m_pendingCommandListCount += numLists;
}

bool GraphicsTelemetryProbe::IsComplete() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_isComplete;
}

void GraphicsTelemetryProbe::SetMaxProbeFrames(uint32_t count) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_maxProbeFrames = (count > 0) ? count : 1;
}

uint32_t GraphicsTelemetryProbe::GetMaxProbeFrames() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_maxProbeFrames;
}

void GraphicsTelemetryProbe::SetCompletionCallback(std::function<void()> callback) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_completionCallback = std::move(callback);
}

void GraphicsTelemetryProbe::OnFrameBoundary() {
    std::function<void()> onCompleteCb;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);

        if (m_isComplete) {
            return;
        }

        if (m_passDirty) {
            m_pendingPasses.push_back(m_currentPass);
            m_currentPass = RenderPassInfo{};
            m_passDirty = false;
        }

        ++m_currentFrameIndex;

        FrameTelemetry frame;
        frame.frameIndex = m_currentFrameIndex;
        frame.passes = std::move(m_pendingPasses);
        frame.executedCommandListCount = m_pendingCommandListCount;

        for (const auto& pass : frame.passes) {
            uint32_t vpCount = static_cast<uint32_t>(pass.viewports.size());
            if (vpCount > frame.maxConcurrentViewports) {
                frame.maxConcurrentViewports = vpCount;
            }
        }

        if (frame.maxConcurrentViewports >= 2) {
            frame.hasMultiViewportStereo = true;
        }

        // Check dual-pass stereo heuristic: at least 2 consecutive passes with side-by-side split viewport layout
        // (e.g. Pass 1 at [0, 0, W, H] and Pass 2 at [W, 0, W, H] with active render targets and full-eye width)
        if (frame.passes.size() >= 2) {
            for (size_t i = 0; i < frame.passes.size() - 1; ++i) {
                const auto& p1 = frame.passes[i];
                const auto& p2 = frame.passes[i + 1];
                if (!p1.viewports.empty() && !p2.viewports.empty()) {
                    bool splitViewport = (p1.viewports[0].x == 0.0f &&
                                          p2.viewports[0].x == p1.viewports[0].width &&
                                          p1.viewports[0].y == 0.0f &&
                                          p2.viewports[0].y == 0.0f &&
                                          p1.viewports[0].width == p2.viewports[0].width &&
                                          p1.viewports[0].height == p2.viewports[0].height &&
                                          p1.viewports[0].width >= 640.0f &&
                                          !p1.rtvHandles.empty() &&
                                          !p2.rtvHandles.empty());
                    if (splitViewport) {
                        frame.hasDualPassStereo = true;
                        break;
                    }
                }
            }
        }

        m_maxConcurrentViewportsObserved = std::max(m_maxConcurrentViewportsObserved, frame.maxConcurrentViewports);
        m_lastFrame = frame;
        if (m_history.size() < m_maxProbeFrames) {
            m_history.push_back(frame);
        }

        EvaluateVerdictLocked();

        // Reset pending state
        m_pendingPasses.clear();
        m_pendingCommandListCount = 0;

        // Output detailed telemetry diagnostics only during the initial probe window
        if (m_currentFrameIndex <= m_maxProbeFrames) {
            std::ostringstream ss;
            ss << "[TelemetryProbe] Frame #" << frame.frameIndex << ": "
               << frame.passes.size() << " passes, "
               << frame.executedCommandListCount << " command lists, max viewports="
               << frame.maxConcurrentViewports;
            LogInfo(ss.str());

            for (size_t pIdx = 0; pIdx < frame.passes.size(); ++pIdx) {
                const auto& pass = frame.passes[pIdx];
                std::ostringstream passStream;
                passStream << "  Pass " << pIdx << ": Viewports=" << pass.viewports.size() << " (";
                for (size_t vIdx = 0; vIdx < pass.viewports.size(); ++vIdx) {
                    if (vIdx > 0) passStream << ", ";
                    passStream << "[" << pass.viewports[vIdx].x << "," << pass.viewports[vIdx].y
                               << " " << pass.viewports[vIdx].width << "x" << pass.viewports[vIdx].height << "]";
                }
                passStream << "), Scissors=" << pass.scissorRects.size() << " (";
                for (size_t sIdx = 0; sIdx < pass.scissorRects.size(); ++sIdx) {
                    if (sIdx > 0) passStream << ", ";
                    passStream << "[" << pass.scissorRects[sIdx].left << "," << pass.scissorRects[sIdx].top
                               << "," << pass.scissorRects[sIdx].right << "," << pass.scissorRects[sIdx].bottom << "]";
                }
                passStream << "), RTVs=" << pass.rtvCount << ", DSV=" << (pass.hasDsv ? "yes" : "no");
                LogInfo(passStream.str());
            }
        }

        // Check if probe has reached a definitive verdict or reached sampling limit
        if (m_verdict != StereoVerdict::Inconclusive &&
            (m_currentFrameIndex >= m_maxProbeFrames || m_verdict == StereoVerdict::NativeStereoDetected)) {
            m_isComplete = true;
            if (m_verdict == StereoVerdict::NativeStereoDetected) {
                LogInfo("[TelemetryProbe] Final Verdict: Native stereo detected in GIANTS Engine 10. Probe complete, disarming telemetry hooks.");
            } else if (m_verdict == StereoVerdict::AfrFallbackRequired) {
                LogInfo("[TelemetryProbe] Final Verdict: GIANTS Engine 10 Direct3D 12 pipeline requires Alternate Frame Rendering (AFR). Probe complete, disarming telemetry hooks.");
            }
            onCompleteCb = m_completionCallback;
        }
    }

    if (onCompleteCb) {
        onCompleteCb();
    }
}

void GraphicsTelemetryProbe::EvaluateVerdictLocked() {
    bool detectedStereo = false;
    for (const auto& frame : m_history) {
        if (frame.hasMultiViewportStereo || frame.hasDualPassStereo) {
            detectedStereo = true;
            break;
        }
    }

    if (detectedStereo) {
        m_verdict = StereoVerdict::NativeStereoDetected;
    } else if (m_history.size() >= 3 && m_maxConcurrentViewportsObserved <= 1) {
        m_verdict = StereoVerdict::AfrFallbackRequired;
    } else {
        m_verdict = StereoVerdict::Inconclusive;
    }
}

uint64_t GraphicsTelemetryProbe::GetCurrentFrameIndex() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_currentFrameIndex;
}

const FrameTelemetry& GraphicsTelemetryProbe::GetLastFrameTelemetry() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_lastFrame;
}

const std::vector<FrameTelemetry>& GraphicsTelemetryProbe::GetFrameHistory() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_history;
}

StereoVerdict GraphicsTelemetryProbe::GetStereoVerdict() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_verdict;
}

bool GraphicsTelemetryProbe::HasNativeStereoDetected() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_verdict == StereoVerdict::NativeStereoDetected;
}

bool GraphicsTelemetryProbe::RequiresAfrFallback() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_verdict == StereoVerdict::AfrFallbackRequired;
}

uint32_t GraphicsTelemetryProbe::GetMaxConcurrentViewportsObserved() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_maxConcurrentViewportsObserved;
}

std::string GraphicsTelemetryProbe::GenerateDiagnosticReportLocked() const {
    std::ostringstream ss;
    ss << "=== FS25 Direct3D 12 Stereo Telemetry Report ===\n";
    ss << "Total Frames Sampled: " << m_history.size() << "\n";
    ss << "Max Concurrent Viewports: " << m_maxConcurrentViewportsObserved << "\n";

    for (const auto& frame : m_history) {
        ss << "Frame #" << frame.frameIndex << ": " << frame.passes.size() << " passes\n";
        for (size_t i = 0; i < frame.passes.size(); ++i) {
            const auto& pass = frame.passes[i];
            ss << "  Pass " << i << ": ";
            if (!pass.viewports.empty()) {
                ss << pass.viewports[0].width << "x" << pass.viewports[0].height << " (count: " << pass.viewports.size() << ")";
            }
            ss << " RTVs: " << pass.rtvCount << "\n";
        }
    }

    ss << "Resolution Verdict: ";
    if (m_verdict == StereoVerdict::NativeStereoDetected) {
        ss << "Native Stereo Passes Detected\n";
    } else if (m_verdict == StereoVerdict::AfrFallbackRequired) {
        ss << "Alternate Frame Rendering (AFR) Fallback Required\n";
    } else {
        ss << "Inconclusive (insufficient frames)\n";
    }
    return ss.str();
}

std::string GraphicsTelemetryProbe::GenerateDiagnosticReport() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return GenerateDiagnosticReportLocked();
}

ProbeSnapshot GraphicsTelemetryProbe::GetSnapshot() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    ProbeSnapshot snap;
    snap.frameIndex = m_lastFrame.frameIndex;
    snap.maxViewports = m_lastFrame.maxConcurrentViewports;
    snap.passCount = m_lastFrame.passes.size();
    snap.nativeStereo = (m_verdict == StereoVerdict::NativeStereoDetected);
    snap.afrFallback = (m_verdict == StereoVerdict::AfrFallbackRequired);
    snap.verdict = m_verdict;
    const char* verdictName = (m_verdict == StereoVerdict::NativeStereoDetected) ? "NativeStereoDetected"
        : (m_verdict == StereoVerdict::AfrFallbackRequired ? "AfrFallbackRequired" : "Inconclusive");
    snap.summary = std::string("Verdict: ") + verdictName + " (" + std::to_string(m_history.size()) + " frames sampled)";
    return snap;
}

void GraphicsTelemetryProbe::Reset() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_currentFrameIndex = 0;
    m_currentPass = RenderPassInfo{};
    m_passDirty = false;
    m_pendingPasses.clear();
    m_pendingCommandListCount = 0;
    m_lastFrame = FrameTelemetry{};
    m_history.clear();
    m_maxConcurrentViewportsObserved = 0;
    m_verdict = StereoVerdict::Inconclusive;
    m_isComplete = false;
}

} // namespace fs25vr
