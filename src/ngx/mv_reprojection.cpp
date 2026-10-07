#include "ngx/mv_reprojection.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace fs25vr::mvreproj {

RelativeTransform MakeRelativeTransform(const RelativeCamera& relative) {
    // p_other = R(conj(q)) * (p - t) with q = relative.rotation, t = relative.translation.
    const double x = -relative.rotation[0];
    const double y = -relative.rotation[1];
    const double z = -relative.rotation[2];
    const double w = relative.rotation[3];
    double r[3][3] = {
        {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
        {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
        {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)},
    };
    RelativeTransform t;
    for (int i = 0; i < 3; ++i) {
        double c = 0.0;
        for (int j = 0; j < 3; ++j) {
            t.m[i][j] = static_cast<float>(r[i][j]);
            c -= r[i][j] * relative.translation[j];
        }
        t.c[i] = static_cast<float>(c);
    }
    return t;
}

void ApplyRelativeTransform(const RelativeTransform& t, const float p[3], float out[3]) {
    for (int i = 0; i < 3; ++i) {
        out[i] = t.m[i][0] * p[0] + t.m[i][1] * p[1] + t.m[i][2] * p[2] + t.c[i];
    }
}

float ReverseZToViewDepth(float depth, float nearClip, float farClip, bool infiniteDepth) {
    if (infiniteDepth) {
        // depth <= near / cap would exceed the cap (and 0 divides by zero): that is the sky.
        if (!(depth > nearClip / kInfiniteSkyViewDepth)) return kInfiniteSkyViewDepth;
        return nearClip / depth;
    }
    return nearClip * farClip / (nearClip + depth * (farClip - nearClip));
}

bool UnprojectPixel(float px, float py, float depth, float width, float height,
                    const Projection& proj, float out[3]) {
    if (!(depth > 0.0f)) return false; // reverse-Z: 0 is the far plane (sky)
    const float z = ReverseZToViewDepth(depth, proj.nearClip, proj.farClip, proj.infiniteDepth);
    const float ndcX = 2.0f * px / width - 1.0f;
    const float ndcY = 1.0f - 2.0f * py / height;
    out[0] = ndcX * proj.tanHalfFovY * proj.aspect * z;
    out[1] = ndcY * proj.tanHalfFovY * z;
    out[2] = -z;
    return true;
}

bool ProjectToPixel(const float p[3], float width, float height, const Projection& proj,
                    float& px, float& py) {
    const float z = -p[2];
    if (!(z > 1e-6f)) return false;
    const float ndcX = p[0] / (z * proj.tanHalfFovY * proj.aspect);
    const float ndcY = p[1] / (z * proj.tanHalfFovY);
    px = (ndcX * 0.5f + 0.5f) * width;
    py = (0.5f - ndcY * 0.5f) * height;
    return true;
}

SampleResult EvaluateSample(uint32_t x, uint32_t y, float depth, float mvX, float mvY,
                            float scaleX, float scaleY, uint32_t width, uint32_t height,
                            const Projection& current, const Projection& previous,
                            const RelativeTransform& toPrevious) {
    SampleResult result;
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float cx = static_cast<float>(x) + 0.5f;
    const float cy = static_cast<float>(y) + 0.5f;

    float p[3];
    if (!UnprojectPixel(cx, cy, depth, w, h, current, p)) {
        result.status = static_cast<uint32_t>(SampleStatus::Sky);
        return result;
    }
    float q[3];
    ApplyRelativeTransform(toPrevious, p, q);
    float px = 0.0f, py = 0.0f;
    if (!ProjectToPixel(q, w, h, previous, px, py) || px < 0.0f || px >= w || py < 0.0f || py >= h) {
        result.status = static_cast<uint32_t>(SampleStatus::OffScreen);
        return result;
    }

    // The same point as if infinitely far: only the relative rotation moves it. Depth cancels in the
    // projection, so the unit direction stands in for the point.
    float rotP[3] = {p[0], p[1], p[2]};
    float rotQ[3];
    const float rotLen = std::sqrt(rotP[0] * rotP[0] + rotP[1] * rotP[1] + rotP[2] * rotP[2]);
    for (float& v : rotP) v /= rotLen;
    for (int i = 0; i < 3; ++i) {
        rotQ[i] = toPrevious.m[i][0] * rotP[0] + toPrevious.m[i][1] * rotP[1] + toPrevious.m[i][2] * rotP[2];
    }
    float rotPx = 0.0f, rotPy = 0.0f;
    if (!ProjectToPixel(rotQ, w, h, previous, rotPx, rotPy)) {
        result.status = static_cast<uint32_t>(SampleStatus::OffScreen);
        return result;
    }

    const float dx = px - cx; // predicted previous - current
    const float dy = py - cy;
    const float ex = mvX * scaleX;
    const float ey = mvY * scaleY;
    result.errToPrevious = std::sqrt((ex - dx) * (ex - dx) + (ey - dy) * (ey - dy));
    result.errToCurrent = std::sqrt((ex + dx) * (ex + dx) + (ey + dy) * (ey + dy));
    result.engineX = ex;
    result.engineY = ey;
    result.predictedX = dx;
    result.predictedY = dy;
    result.rawX = mvX;
    result.rawY = mvY;
    result.rotationX = rotPx - cx;
    result.rotationY = rotPy - cy;
    result.viewDepth = -p[2];
    result.status = static_cast<uint32_t>(SampleStatus::Valid);
    return result;
}

void UnprojectAtViewDepth(float px, float py, float viewDepth, float width, float height, const Projection& proj,
                          float out[3]) {
    const float ndcX = 2.0f * px / width - 1.0f;
    const float ndcY = 1.0f - 2.0f * py / height;
    out[0] = ndcX * proj.tanHalfFovY * proj.aspect * viewDepth;
    out[1] = ndcY * proj.tanHalfFovY * viewDepth;
    out[2] = -viewDepth;
}

bool CorrectMotionVector(uint32_t x, uint32_t y, float depth, float mvX, float mvY, float scaleX, float scaleY,
                         uint32_t width, uint32_t height, const Projection& current, const Projection& previousFrame,
                         const Projection& sameEye, const RelativeTransform& toPreviousFrame,
                         const RelativeTransform& toSameEye, float& outX, float& outY, float objectMotionFactor,
                         bool gateNonStatic) {
    outX = mvX;
    outY = mvY;
    if (!(std::fabs(scaleX) > 1e-6f) || !(std::fabs(scaleY) > 1e-6f)) return false;
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float cx = static_cast<float>(x) + 0.5f;
    const float cy = static_cast<float>(y) + 0.5f;

    float p[3];
    UnprojectAtViewDepth(cx, cy, ReverseZToViewDepth((std::max)(depth, 0.0f), current.nearClip, current.farClip,
                                             current.infiniteDepth),
                         w, h,
                         current, p);
    float q[3];
    float prevX = 0.0f, prevY = 0.0f, sameX = 0.0f, sameY = 0.0f;
    ApplyRelativeTransform(toPreviousFrame, p, q);
    if (!ProjectToPixel(q, w, h, previousFrame, prevX, prevY)) return false;
    ApplyRelativeTransform(toSameEye, p, q);
    if (!ProjectToPixel(q, w, h, sameEye, sameX, sameY)) return false;

    if (gateNonStatic &&
        !IsStaticConsistent(mvX * scaleX, mvY * scaleY, prevX - cx, prevY - cy)) {
        return false; // camera-attached or moving: the engine's vector already holds its own motion
    }
    outX = mvX + (sameX - prevX) / scaleX;
    outY = mvY + (sameY - prevY) / scaleY;
    if (objectMotionFactor != 1.0f) {
        // Object motion = the engine's vector less the static prediction toward the previous frame's camera.
        const float objX = mvX - (prevX - cx) / scaleX;
        const float objY = mvY - (prevY - cy) / scaleY;
        outX += (objectMotionFactor - 1.0f) * objX;
        outY += (objectMotionFactor - 1.0f) * objY;
    }
    return true;
}

bool IsStaticConsistent(float ex, float ey, float dx, float dy) {
    const float limit = (std::max)(kStaticGateAbsPx, kStaticGateRelative * std::hypot(dx, dy));
    return std::hypot(ex - dx, ey - dy) <= limit;
}

namespace {

bool UsableProjection(const CameraRecord& r) {
    return r.fovY > 0.0f && r.nearClip > 0.0f && r.farClip > r.nearClip;
}

Projection ProjectionOf(const CameraRecord& r) {
    Projection p;
    p.tanHalfFovY = std::tan(r.fovY * 0.5f);
    p.nearClip = r.nearClip;
    p.farClip = r.farClip;
    return p; // aspect is the render's, filled in by the pass
}

} // namespace

CorrectionPlan PlanCorrection(const ResolvedCameras& resolved) {
    CorrectionPlan plan;
    if (!resolved.current) return plan;
    if (!resolved.previousFrame || !resolved.sameEyePrevious || !resolved.previousFrameRelative ||
        !resolved.sameEyeRelative) {
        return plan;
    }
    if (!UsableProjection(*resolved.current) || !UsableProjection(*resolved.previousFrame) ||
        !UsableProjection(*resolved.sameEyePrevious)) {
        plan.decision = CorrectionDecision::UnusableProjection;
        return plan;
    }
    plan.decision = CorrectionDecision::Correct;
    plan.current = ProjectionOf(*resolved.current);
    plan.previousFrame = ProjectionOf(*resolved.previousFrame);
    plan.sameEye = ProjectionOf(*resolved.sameEyePrevious);
    plan.toPreviousFrame = MakeRelativeTransform(*resolved.previousFrameRelative);
    plan.toSameEye = MakeRelativeTransform(*resolved.sameEyeRelative);
    return plan;
}

const char* CorrectionDecisionName(CorrectionDecision d) {
    switch (d) {
        case CorrectionDecision::Correct: return "correct";
        case CorrectionDecision::UnusableProjection: return "unusable-projection";
        default: return "missing-records";
    }
}
uint32_t GridSampleCoordinate(uint32_t i, uint32_t n, uint32_t extent) {
    if (n == 0 || extent == 0) return 0;
    const double cell = (static_cast<double>(i) + 0.5) * static_cast<double>(extent) / static_cast<double>(n);
    return (std::min)(extent - 1, static_cast<uint32_t>(cell));
}

namespace {

double Median(std::vector<float>& values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    return (n % 2 == 1) ? values[n / 2] : 0.5 * (static_cast<double>(values[n / 2 - 1]) + values[n / 2]);
}

double Mean(const std::vector<float>& values) {
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (float v : values) sum += v;
    return sum / static_cast<double>(values.size());
}

} // namespace

SelfCheckStats Summarize(const SampleResult* samples, size_t count) {
    SelfCheckStats stats;
    stats.total = static_cast<uint32_t>(count);
    std::vector<float> toPrevious, toCurrent;
    for (size_t i = 0; i < count; ++i) {
        if (samples[i].status == static_cast<uint32_t>(SampleStatus::Valid)) {
            toPrevious.push_back(samples[i].errToPrevious);
            toCurrent.push_back(samples[i].errToCurrent);
        }
    }
    stats.valid = static_cast<uint32_t>(toPrevious.size());
    stats.skipped = stats.total - stats.valid;
    stats.skippedFraction = stats.total ? static_cast<double>(stats.skipped) / stats.total : 0.0;
    stats.meanToPrevious = Mean(toPrevious);
    stats.meanToCurrent = Mean(toCurrent);
    stats.medianToPrevious = Median(toPrevious);
    stats.medianToCurrent = Median(toCurrent);

    double sumEngine = 0.0, sumPredicted = 0.0, sumRaw = 0.0, sumEP = 0.0, sumRP = 0.0, sumPP = 0.0;
    bool first = true;
    uint32_t gated = 0;
    for (size_t i = 0; i < count; ++i) {
        const SampleResult& s = samples[i];
        if (s.status != static_cast<uint32_t>(SampleStatus::Valid)) continue;
        const double ex = s.engineX, ey = s.engineY, px = s.predictedX, py = s.predictedY;
        const double rx = s.rawX, ry = s.rawY;
        sumEngine += std::sqrt(ex * ex + ey * ey);
        sumPredicted += std::sqrt(px * px + py * py);
        sumRaw += std::sqrt(rx * rx + ry * ry);
        sumEP += ex * px + ey * py;
        sumRP += rx * px + ry * py;
        sumPP += px * px + py * py;
        if (!IsStaticConsistent(static_cast<float>(ex), static_cast<float>(ey), static_cast<float>(px),
                                static_cast<float>(py))) {
            ++gated;
        }
        if (first) {
            stats.rawMinX = stats.rawMaxX = rx;
            stats.rawMinY = stats.rawMaxY = ry;
            first = false;
        } else {
            stats.rawMinX = (std::min)(stats.rawMinX, rx);
            stats.rawMaxX = (std::max)(stats.rawMaxX, rx);
            stats.rawMinY = (std::min)(stats.rawMinY, ry);
            stats.rawMaxY = (std::max)(stats.rawMaxY, ry);
        }
    }
    if (stats.valid > 0) {
        stats.engineMean = sumEngine / stats.valid;
        stats.predictedMean = sumPredicted / stats.valid;
        stats.rawMean = sumRaw / stats.valid;
        stats.gatedFraction = static_cast<double>(gated) / stats.valid;
    }
    if (sumPP > 0.0) {
        stats.k = sumEP / sumPP;
        stats.kRaw = sumRP / sumPP;
    }
    return stats;
}

NearSolution SolveNear(const SampleResult* samples, size_t count, float assumedNear) {
    NearSolution out;
    if (!samples || !(assumedNear > 0.0f) || !std::isfinite(assumedNear)) return out;
    // e - R and T (the translation part of the prediction), R itself (for the size of the prediction in the
    // consistency test), and the view depth at the assumed near.
    struct Term { double ex, ey, tx, ty, rx, ry, z0; };
    std::vector<Term> terms;
    for (size_t i = 0; i < count; ++i) {
        const SampleResult& s = samples[i];
        if (s.status != static_cast<uint32_t>(SampleStatus::Valid)) continue;
        ++out.total;
        const double tx = static_cast<double>(s.predictedX) - s.rotationX;
        const double ty = static_cast<double>(s.predictedY) - s.rotationY;
        if (!(tx * tx + ty * ty >= kMinInformativeTranslationSq)) continue;
        terms.push_back({static_cast<double>(s.engineX) - s.rotationX, static_cast<double>(s.engineY) - s.rotationY,
                         tx, ty, s.rotationX, s.rotationY, s.viewDepth});
    }
    out.informative = static_cast<uint32_t>(terms.size());
    if (terms.size() < kMinNearSamples) return out;
    double sumTT = 0.0;
    for (const Term& t : terms) sumTT += t.tx * t.tx + t.ty * t.ty;
    if (!(sumTT / static_cast<double>(terms.size()) >= kMinNearTranslationSq)) return out;

    // At candidate scale s a term is outside the near field when its view depth z0 / s reaches the cutoff (a
    // term without a depth always is), and an inlier when its vector matches the static prediction R + s T.
    auto retained = [&](const Term& t, double s) { return !(t.z0 > 0.0) || t.z0 / s >= kNearFieldViewDepth; };
    auto inlier = [&](const Term& t, double s) {
        return IsStaticConsistent(static_cast<float>(t.rx + t.ex), static_cast<float>(t.ry + t.ey),
                                  static_cast<float>(t.rx + s * t.tx), static_cast<float>(t.ry + s * t.ty));
    };
    struct Tally { uint32_t retained = 0, inliers = 0; };
    auto tally = [&](double s) {
        Tally r;
        for (const Term& t : terms) {
            if (!retained(t, s)) continue;
            ++r.retained;
            if (inlier(t, s)) ++r.inliers;
        }
        return r;
    };

    // Candidate scales: the per-sample ratios (quantiles when there are many). The best candidate is the one
    // most samples agree with; a camera-attached or moving minority cannot pull it the way it pulls a mean.
    std::vector<double> ratios;
    for (const Term& t : terms) {
        const double r = (t.ex * t.tx + t.ey * t.ty) / (t.tx * t.tx + t.ty * t.ty);
        if (r > 0.0 && std::isfinite(r)) ratios.push_back(r);
    }
    if (ratios.empty()) return out;
    std::sort(ratios.begin(), ratios.end());
    std::vector<double> candidates;
    if (ratios.size() <= kMaxNearCandidates) {
        candidates = ratios;
    } else {
        for (size_t i = 0; i < kMaxNearCandidates; ++i) candidates.push_back(ratios[i * ratios.size() / kMaxNearCandidates]);
    }
    double s = 0.0;
    uint32_t bestInliers = 0;
    for (double c : candidates) {
        const Tally t = tally(c);
        if (t.inliers > bestInliers) {
            bestInliers = t.inliers;
            s = c;
        }
    }
    if (!(s > 0.0)) return out;

    // Refine: least squares over the inliers at the current scale, then re-classify.
    for (int pass = 0; pass < 4; ++pass) {
        double num = 0.0, den = 0.0;
        for (const Term& t : terms) {
            if (!retained(t, s) || !inlier(t, s)) continue;
            num += t.ex * t.tx + t.ey * t.ty;
            den += t.tx * t.tx + t.ty * t.ty;
        }
        if (!(den > 0.0) || !(num > 0.0)) break;
        s = num / den;
    }
    const Tally fin = tally(s);
    out.used = fin.inliers;
    out.retained = fin.retained;
    out.inlierFraction = fin.retained ? static_cast<double>(fin.inliers) / fin.retained : 0.0;
    if (fin.inliers < kMinNearSamples || fin.retained < kMinNearSamples ||
        fin.retained < kMinNearRetainedFraction * static_cast<double>(terms.size()) ||
        out.inlierFraction < kMinNearInlierFraction) {
        return out;
    }
    if (!(s > 0.0) || !std::isfinite(s)) return out;
    out.scale = s;
    out.nearClip = static_cast<float>(assumedNear / s);
    out.valid = true;
    return out;
}
Convention BetterConvention(const SelfCheckStats& stats) {
    if (stats.valid == 0) return Convention::None;
    return stats.medianToCurrent < stats.medianToPrevious ? Convention::ToCurrent : Convention::ToPrevious;
}

const char* ConventionName(Convention c) {
    switch (c) {
        case Convention::ToPrevious: return "previous-minus-current";
        case Convention::ToCurrent: return "current-minus-previous";
        default: return "none";
    }
}

} // namespace fs25vr::mvreproj
