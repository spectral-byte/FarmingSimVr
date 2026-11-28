#include "upscaler_mv_helper.h"
#include "core/logger.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace fs25vr {

UpscalerDiagnostics::UpscalerDiagnostics(const char* logPrefix, const char* unitPlural, const char* summaryCountLabel)
    : m_logPrefix(logPrefix), m_unitPlural(unitPlural), m_summaryCountLabel(summaryCountLabel) {}

void UpscalerDiagnostics::Arm(uint32_t count) {
    count = (std::min)(count, kMaxWindow);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_samples[0].clear();
        m_samples[1].clear();
        m_lastContext = -1;
        m_contextRun = 0;
        m_longestContextRun = 0;
    }
    m_rawRangeLogged.store(false);
    m_projectionLogged.store(false);
    m_remaining.store(count);
    LogInfo(std::string(m_logPrefix) + ": [Diag] Armed for " + std::to_string(count) + " " + m_unitPlural + ".");
}

void UpscalerDiagnostics::Disarm() {
    m_remaining.store(0);
}

UpscalerDiagnostics::SlotClaim UpscalerDiagnostics::TryConsumeSlot() {
    uint32_t remaining = m_remaining.load();
    while (remaining > 0 && !m_remaining.compare_exchange_weak(remaining, remaining - 1)) {}
    if (remaining == 0) return {};
    return {true, remaining == 1};
}

void UpscalerDiagnostics::RecordSample(uint32_t contextIndex, float jitterX, float jitterY, bool reset) {
    const uint32_t ctx = (contextIndex == 1) ? 1 : 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_samples[ctx].push_back({jitterX, jitterY, reset});
    m_contextRun = (m_lastContext == static_cast<int>(ctx)) ? m_contextRun + 1 : 1;
    m_longestContextRun = (std::max)(m_longestContextRun, m_contextRun);
    m_lastContext = static_cast<int>(ctx);
}

UpscalerJitterSummary UpscalerDiagnostics::Summarize(const std::vector<UpscalerDiagSample>& samples) {
    UpscalerJitterSummary summary;
    std::vector<std::pair<float, float>> distinct;
    for (const auto& s : samples) {
        if (summary.count == 0) {
            summary.minX = summary.maxX = s.jitterX;
            summary.minY = summary.maxY = s.jitterY;
        }
        summary.count++;
        if (s.reset) summary.resets++;
        summary.minX = (std::min)(summary.minX, s.jitterX);
        summary.maxX = (std::max)(summary.maxX, s.jitterX);
        summary.minY = (std::min)(summary.minY, s.jitterY);
        summary.maxY = (std::max)(summary.maxY, s.jitterY);
        bool seen = std::any_of(distinct.begin(), distinct.end(), [&](const auto& d) {
            return std::fabs(d.first - s.jitterX) < kJitterMatchTolerance &&
                   std::fabs(d.second - s.jitterY) < kJitterMatchTolerance;
        });
        if (!seen) distinct.emplace_back(s.jitterX, s.jitterY);
    }
    summary.distinctOffsets = static_cast<uint32_t>(distinct.size());
    return summary;
}

UpscalerJitterSummary UpscalerDiagnostics::GetJitterSummary(uint32_t contextIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return Summarize(m_samples[contextIndex == 1 ? 1 : 0]);
}

uint32_t UpscalerDiagnostics::GetLongestContextRun() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_longestContextRun;
}

void UpscalerDiagnostics::LogSummary() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (uint32_t ctx = 0; ctx < 2; ++ctx) {
        const auto& samples = m_samples[ctx];
        UpscalerJitterSummary summary = Summarize(samples);

        std::stringstream ss;
        ss << std::fixed << std::setprecision(4)
           << m_logPrefix << ": [Diag] Summary Ctx=" << ctx << " " << m_summaryCountLabel << "=" << summary.count
           << " Resets=" << summary.resets << " DistinctJitter=" << summary.distinctOffsets
           << " JitterX=[" << summary.minX << ", " << summary.maxX << "]"
           << " JitterY=[" << summary.minY << ", " << summary.maxY << "] Offsets:";
        std::vector<std::pair<float, float>> listed;
        for (const auto& s : samples) {
            if (listed.size() >= kMaxListedJitterOffsets) break;
            bool seen = std::any_of(listed.begin(), listed.end(), [&](const auto& d) {
                return std::fabs(d.first - s.jitterX) < kJitterMatchTolerance &&
                       std::fabs(d.second - s.jitterY) < kJitterMatchTolerance;
            });
            if (!seen) {
                listed.emplace_back(s.jitterX, s.jitterY);
                ss << " (" << s.jitterX << ", " << s.jitterY << ")";
            }
        }
        LogInfo(ss.str());
    }
    LogInfo(std::string(m_logPrefix) + ": [Diag] Longest same-context run=" + std::to_string(m_longestContextRun) +
            " (1 means the contexts strictly alternated).");
}

UpscalerMvPassRunner::UpscalerMvPassRunner(const char* logPrefix, const char* upscalerName)
    : m_logPrefix(logPrefix),
      m_upscalerName(upscalerName),
      m_factory([](ID3D12GraphicsCommandList*) { return CreateD3D12MotionVectorPass(); }) {}

void UpscalerMvPassRunner::ReleaseForProcessTermination() {
    (void)m_pass.release();
}

void UpscalerMvPassRunner::Reset() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pass.reset();
        m_disableReason.clear();
        m_factory = [](ID3D12GraphicsCommandList*) { return CreateD3D12MotionVectorPass(); };
    }
    m_enabled.store(kDefaultEnabled);
    m_disabled.store(false);
}

void UpscalerMvPassRunner::SetFactory(MotionVectorPassFactory factory) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pass.reset();
    m_factory = std::move(factory);
}

bool UpscalerMvPassRunner::SetEnabled(bool enabled) {
    if (enabled && m_disabled.load()) {
        LogWarn(std::string(m_logPrefix) + ": vrDlssMvFix on ignored; the pass is disabled for this session (" +
                GetDisableReason() + ")");
        return false;
    }
    bool previous = m_enabled.exchange(enabled);
    if (previous != enabled) {
        LogInfo(std::string(m_logPrefix) + ": Motion vector pass (vrDlssMvFix) " + (enabled ? "on" : "off"));
    }
    return enabled;
}

std::string UpscalerMvPassRunner::GetDisableReason() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_disableReason;
}

const char* UpscalerMvPassRunner::DescribeState() const {
    if (m_disabled.load()) return "failed";
    return m_enabled.load() ? "on" : "off";
}

void UpscalerMvPassRunner::Disable(const std::string& reason) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_disabled.load()) return;
        m_disableReason = reason;
    }
    m_disabled.store(true);
    m_enabled.store(false);
    LogError(std::string(m_logPrefix) + ": Motion vector pass disabled for this session: " + reason + ". " +
             m_upscalerName + " continues on the engine's motion vectors.");
}

MvPassRunResult UpscalerMvPassRunner::Run(const MvPassInputs& inputs, std::vector<MvSelfCheckReport>& reports) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_disabled.load()) return {};
    if (!inputs.correction && !inputs.selfCheck) {
        if (m_pass) {
            reports = m_pass->PollSelfCheck();
        }
        MvPassRunResult res;
        res.handled = true;
        res.swapped = false;
        res.output = inputs.motionVectors;
        return res;
    }
    if (!m_pass) {
        if (m_factory) m_pass = m_factory(inputs.cmdList);
        if (!m_pass) {
            lock.unlock();
            Disable("the motion vector pass could not be created");
            return {};
        }
    }
    MvPassResult result = m_pass->Record(inputs);
    if (!result.ok || !result.output) {
        lock.unlock();
        Disable(result.failureReason.empty() ? std::string("the pass reported a failure") : result.failureReason);
        return {};
    }
    reports = m_pass->PollSelfCheck();
    MvPassRunResult res;
    res.handled = true;
    res.swapped = (result.output != inputs.motionVectors);
    res.output = result.output;
    return res;
}

bool BuildMvCorrectionRequest(const mvreproj::CorrectionPlan& plan, float mvScaleX, float mvScaleY,
                              float objectMotionFactor, bool gateNonStatic,
                              MvCorrectionRequest& outRequest, const char*& outState) {
    outState = mvreproj::CorrectionDecisionName(plan.decision);
    if (plan.decision != mvreproj::CorrectionDecision::Correct) {
        return false;
    }
    if (!std::isfinite(mvScaleX) || !std::isfinite(mvScaleY) ||
        std::fabs(mvScaleX) <= 1e-6f || std::fabs(mvScaleY) <= 1e-6f) {
        outState = "zero-mv-scale";
        return false;
    }
    outRequest.mvScaleX = mvScaleX;
    outRequest.mvScaleY = mvScaleY;
    outRequest.current = plan.current;
    outRequest.previousFrame = plan.previousFrame;
    outRequest.sameEye = plan.sameEye;
    outRequest.toPreviousFrame = plan.toPreviousFrame;
    outRequest.toSameEye = plan.toSameEye;
    outRequest.objectMotionFactor = objectMotionFactor;
    outRequest.gateNonStatic = gateNonStatic;
    outState = "correcting";
    return true;
}

void LogSelfCheckReport(const char* logPrefix, const char* unit, UpscalerDiagnostics& diagnostics,
                        const MvSelfCheckReport& report) {
    if (diagnostics.ConsumeRawRangeLogFlag()) {
        LogInfo(FormatMvRawRangeReport(logPrefix, unit, report));
    }
    LogInfo(FormatMvSelfCheckReport(logPrefix, unit, report));
}

}
