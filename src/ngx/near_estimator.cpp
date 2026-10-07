#include "ngx/near_estimator.h"
#include <algorithm>
#include <cmath>

namespace fs25vr {

void NearEstimator::Submit(uint64_t evalIndex, const mvreproj::NearSolution& solution) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (evalIndex < m_ignoreBefore || !solution.valid) return;
    if (!(solution.nearClip >= kMinNear && solution.nearClip <= kMaxNear)) return;
    m_window.push_back(solution.nearClip);
    if (m_window.size() > kWindow) m_window.erase(m_window.begin());
    if (m_window.size() < kMinSolutions) return;

    std::vector<float> sorted = m_window;
    std::sort(sorted.begin(), sorted.end());
    const size_t n = sorted.size();
    const float median = (n % 2 == 1) ? sorted[n / 2] : 0.5f * (sorted[n / 2 - 1] + sorted[n / 2]);
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