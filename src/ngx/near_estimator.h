#pragma once

#include "ngx/mv_reprojection.h"
#include <cstdint>
#include <mutex>
#include <vector>

namespace fs25vr {

// Smooths the per-evaluation near plane solutions (mvreproj::SolveNear) into the one value the correction
// uses: a median over the last few valid solutions, published with hysteresis so it does not jitter, and
// nothing at all until a few solutions exist (the caller then falls back to the camera record's near).
// Thread-safe: solutions arrive from the render thread, a reset can come from the script thread.
class NearEstimator {
public:
    static constexpr float kMinNear = 0.02f; // sanity range in metres; solutions outside it are ignored
    static constexpr float kMaxNear = 1.0f;
    static constexpr size_t kWindow = 5;
    static constexpr size_t kMinSolutions = 3;
    static constexpr float kHysteresis = 0.03f; // relative change needed to move the published value

    // evalIndex is the evaluation the self-check behind the solution was recorded on.
    void Submit(uint64_t evalIndex, const mvreproj::NearSolution& solution);
    // The published near in metres, or 0 when there is none yet.
    float Estimate() const;
    // Forgets everything, and ignores solutions recorded on evaluations before fromEvalIndex (their
    // readbacks may still be in flight when the scene changes).
    void Reset(uint64_t fromEvalIndex);

private:
    mutable std::mutex m_mutex;
    std::vector<float> m_window;
    float m_published = 0.0f;
    uint64_t m_ignoreBefore = 0;
};

} // namespace fs25vr