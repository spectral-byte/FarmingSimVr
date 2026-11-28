#pragma once

#include "mv_pass.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fs25vr {

struct UpscalerDiagSample {
    float jitterX = 0.0f;
    float jitterY = 0.0f;
    bool reset = false;
};

struct UpscalerJitterSummary {
    uint32_t count = 0;
    uint32_t resets = 0;
    uint32_t distinctOffsets = 0;
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
};

class UpscalerDiagnostics {
public:
    static constexpr uint32_t kDefaultWindow = 128;
    static constexpr uint32_t kMaxWindow = 4096;
    static constexpr float kJitterMatchTolerance = 1e-4f;
    static constexpr size_t kMaxListedJitterOffsets = 16;

    UpscalerDiagnostics(const char* logPrefix, const char* unitPlural, const char* summaryCountLabel);

    void Arm(uint32_t count = kDefaultWindow);
    void Disarm();
    uint32_t Remaining() const { return m_remaining.load(); }
    bool IsArmed() const { return m_remaining.load() > 0; }

    struct SlotClaim {
        bool claimed = false;
        bool isLast = false;
    };
    SlotClaim TryConsumeSlot();
    void RecordSample(uint32_t contextIndex, float jitterX, float jitterY, bool reset);
    void LogSummary();

    bool ConsumeRawRangeLogFlag() { return !m_rawRangeLogged.exchange(true); }
    bool ConsumeProjectionLogFlag() { return !m_projectionLogged.exchange(true); }

    UpscalerJitterSummary GetJitterSummary(uint32_t contextIndex) const;
    uint32_t GetLongestContextRun() const;

    static UpscalerJitterSummary Summarize(const std::vector<UpscalerDiagSample>& samples);

private:
    const char* m_logPrefix;
    const char* m_unitPlural;
    const char* m_summaryCountLabel;

    std::atomic<uint32_t> m_remaining{0};
    std::atomic<bool> m_rawRangeLogged{false};
    std::atomic<bool> m_projectionLogged{false};
    mutable std::mutex m_mutex;
    std::vector<UpscalerDiagSample> m_samples[2];
    int m_lastContext = -1;
    uint32_t m_contextRun = 0;
    uint32_t m_longestContextRun = 0;
};

struct MvPassRunResult {
    bool handled = false;
    bool swapped = false;
    ID3D12Resource* output = nullptr;
};

class UpscalerMvPassRunner {
public:
    static constexpr bool kDefaultEnabled = true;

    UpscalerMvPassRunner(const char* logPrefix, const char* upscalerName);

    void ReleaseForProcessTermination();
    void Reset();

    void SetFactory(MotionVectorPassFactory factory);
    bool SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled.load(); }
    bool IsDisabledForSession() const { return m_disabled.load(); }
    std::string GetDisableReason() const;
    const char* DescribeState() const;

    MvPassRunResult Run(const MvPassInputs& inputs, std::vector<MvSelfCheckReport>& reports);
    void Disable(const std::string& reason);

private:
    const char* m_logPrefix;
    const char* m_upscalerName;

    mutable std::mutex m_mutex;
    MotionVectorPassFactory m_factory;
    std::unique_ptr<IMotionVectorPass> m_pass;
    std::string m_disableReason;
    std::atomic<bool> m_enabled{kDefaultEnabled};
    std::atomic<bool> m_disabled{false};
};

bool BuildMvCorrectionRequest(const mvreproj::CorrectionPlan& plan, float mvScaleX, float mvScaleY,
                              float objectMotionFactor, bool gateNonStatic,
                              MvCorrectionRequest& outRequest, const char*& outState);

void LogSelfCheckReport(const char* logPrefix, const char* unit, UpscalerDiagnostics& diagnostics,
                        const MvSelfCheckReport& report);

}
