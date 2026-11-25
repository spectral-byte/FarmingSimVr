#pragma once

#include "ngx/mv_reprojection.h"
#include <cstdint>
#include <mutex>
#include <vector>

namespace fs25vr {

class NearEstimator {
public:
    static constexpr float kMinNear = 0.02f; // sanity range in metres; solutions outside it are ignored
    static constexpr float kMaxNear = 1.0f;
    static constexpr size_t kWindow = 21;
    static constexpr size_t kMinSolutions = 5;
    static constexpr float kHysteresis = 0.03f; // relative change needed to move the published value
    static constexpr float kPlausibleFactor = 2.0f; // published value stays within [previous / f, previous * f] ...
    static constexpr size_t kJumpSolutions = 5;     // ... unless this many consecutive solutions agree
    static constexpr float kJumpAgreement = 0.10f;  // relative spread within which they agree

    void Submit(uint64_t evalIndex, const mvreproj::NearSolution& solution);
    float Estimate() const;
    void Reset(uint64_t fromEvalIndex);

private:
    mutable std::mutex m_mutex;
    std::vector<float> m_window;
    float m_published = 0.0f;
    uint64_t m_ignoreBefore = 0;
};

}