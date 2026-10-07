#pragma once

#include "ngx/mv_reprojection.h"
#include <cstdint>
#include <mutex>
#include <vector>

namespace fs25vr {

// Smooths the per-evaluation near plane solutions (mvreproj::SolveNear) into the one value the correction
// uses: a median over the last kWindow confident solutions, published with hysteresis so it does not jitter,
// and nothing at all until a few solutions exist (the caller then falls back to the camera record's near).
// Holding the previous value beats publishing a spike: solutions with too few inliers are dropped, and the
// published value stays within kPlausibleFactor of the previous one unless kJumpSolutions consecutive
// solutions agree on a new value (a camera change: cab versus on foot).
// Thread-safe: solutions arrive from the render thread, a reset can come from the script thread.
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