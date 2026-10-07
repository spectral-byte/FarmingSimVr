#include "ngx/near_estimator.h"
#include <algorithm>
#include <cmath>

namespace fs25vr {

namespace {

float Median(std::vector<float> values) {
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    return (n % 2 == 1) ? values[n / 2] : 0.5f * (values[n / 2 - 1] + values[n / 2]);
}

} // namespace

void NearEstimator::Submit(uint64_t evalIndex, const mvreproj::NearSolution& solution) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (evalIndex < m_ignoreBefore || !solution.valid) return;
    if (!(solution.nearClip >= kMinNear && solution.nearClip <= kMaxNear)) return;
    // Low confidence: hold the previous value rather than let a poorly supported solution into the window.
    if (solution.used < mvreproj::kMinNearSamples || !(solution.inlierFraction >= mvreproj::kMinNearInlierFraction)) return;
    m_window.push_back(solution.nearClip);
    if (m_window.size() > kWindow) m_window.erase(m_window.begin());

    // A real jump (a different camera): the latest kJumpSolutions agree with each other, and the published
    // value is not within the plausible factor of them. The window restarts from those solutions.
    if (m_published > 0.0f && m_window.size() >= kJumpSolutions) {
        const std::vector<float> recent(m_window.end() - kJumpSolutions, m_window.end());
        const float centre = Median(recent);
        bool agree = true;
        for (float v : recent) agree = agree && std::fabs(v - centre) <= kJumpAgreement * centre;
        if (agree && (centre > m_published * kPlausibleFactor || centre < m_published / kPlausibleFactor)) {
            m_window = recent;
            m_published = centre;
            return;
        }
    }
    if (m_window.size() < kMinSolutions) return;

    const float median = Median(m_window);
    if (m_published > 0.0f && (median > m_published * kPlausibleFactor || median < m_published / kPlausibleFactor)) {
        return; // implausible without agreement: hold
    }
    if (!(m_published > 0.0f) || std::fabs(median - m_published) > kHysteresis * m_published) m_published = median;
}
float NearEstimator::Estimate() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_published;
}

void NearEstimator::Reset(uint64_t fromEvalIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_window.clear();
    m_published = 0.0f;
    m_ignoreBefore = fromEvalIndex;
}

} // namespace fs25vr