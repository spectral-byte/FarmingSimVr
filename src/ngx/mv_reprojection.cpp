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

float ReverseZToViewDepth(float depth, float nearClip, float farClip) {
    return nearClip * farClip / (nearClip + depth * (farClip - nearClip));
}

bool UnprojectPixel(float px, float py, float depth, float width, float height,
                    const Projection& proj, float out[3]) {
    if (!(depth > 0.0f)) return false; // reverse-Z: 0 is the far plane (sky)
    const float z = ReverseZToViewDepth(depth, proj.nearClip, proj.farClip);
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

    const float dx = px - cx; // predicted previous - current
    const float dy = py - cy;
    const float ex = mvX * scaleX;
    const float ey = mvY * scaleY;
    result.errToPrevious = std::sqrt((ex - dx) * (ex - dx) + (ey - dy) * (ey - dy));
    result.errToCurrent = std::sqrt((ex + dx) * (ex + dx) + (ey + dy) * (ey + dy));
    result.status = static_cast<uint32_t>(SampleStatus::Valid);
    return result;
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
    return stats;
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
