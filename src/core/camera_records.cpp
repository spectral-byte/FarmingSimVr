#include "core/camera_records.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace fs25vr {

namespace {

struct Quat {
    double x, y, z, w;
};

Quat Conjugate(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

Quat Multiply(const Quat& a, const Quat& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

// Rotates v by the unit quaternion q: v + 2w(u x v) + 2 u x (u x v), u = q.xyz.
void Rotate(const Quat& q, const double v[3], double out[3]) {
    const double ux = q.x, uy = q.y, uz = q.z;
    const double cx = uy * v[2] - uz * v[1];
    const double cy = uz * v[0] - ux * v[2];
    const double cz = ux * v[1] - uy * v[0];
    out[0] = v[0] + 2.0 * (q.w * cx + (uy * cz - uz * cy));
    out[1] = v[1] + 2.0 * (q.w * cy + (uz * cx - ux * cz));
    out[2] = v[2] + 2.0 * (q.w * cz + (ux * cy - uy * cx));
}

Quat QuatOf(const CameraRecord& r) {
    return {r.orientation[0], r.orientation[1], r.orientation[2], r.orientation[3]};
}

void AppendRecord(std::ostringstream& ss, const char* label, const std::optional<CameraRecord>& r) {
    ss << label << "=";
    if (r) ss << "eye" << r->eye << "@f" << r->frame;
    else ss << "missing";
}

void AppendRelative(std::ostringstream& ss, const char* label, const std::optional<RelativeCamera>& rel) {
    if (!rel) return;
    ss << " " << label << "=(dx=" << rel->translation[0] << " dy=" << rel->translation[1]
       << " dz=" << rel->translation[2] << " dist=" << rel->translationLength
       << " rot=" << rel->rotationAngle * 180.0 / 3.14159265358979323846 << "deg)";
}

} // namespace

RelativeCamera ComputeRelativeCamera(const CameraRecord& reference, const CameraRecord& other) {
    RelativeCamera rel;
    const Quat inv = Conjugate(QuatOf(reference));
    const double delta[3] = {
        other.position[0] - reference.position[0],
        other.position[1] - reference.position[1],
        other.position[2] - reference.position[2],
    };
    Rotate(inv, delta, rel.translation);
    rel.translationLength = std::sqrt(rel.translation[0] * rel.translation[0] +
                                      rel.translation[1] * rel.translation[1] +
                                      rel.translation[2] * rel.translation[2]);
    const Quat q = Multiply(inv, QuatOf(other));
    rel.rotation[0] = q.x;
    rel.rotation[1] = q.y;
    rel.rotation[2] = q.z;
    rel.rotation[3] = q.w;
    rel.rotationAngle = 2.0 * std::acos((std::min)(1.0, std::fabs(q.w)));
    return rel;
}

std::string DescribeResolvedCameras(const ResolvedCameras& resolved) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);
    AppendRecord(ss, "Cur", resolved.current);
    ss << " ";
    AppendRecord(ss, "Prev", resolved.previousFrame);
    ss << " ";
    AppendRecord(ss, "SameEye", resolved.sameEyePrevious);
    AppendRelative(ss, "PrevRel", resolved.previousFrameRelative);
    AppendRelative(ss, "SameEyeRel", resolved.sameEyeRelative);
    return ss.str();
}

bool CameraRecordStore::Report(CameraRecord record) {
    if (record.eye >= kEyeCount) return false;
    double norm2 = 0.0;
    for (double c : record.orientation) {
        if (!std::isfinite(c)) return false;
        norm2 += c * c;
    }
    for (double c : record.position) {
        if (!std::isfinite(c)) return false;
    }
    if (norm2 < 1e-12) return false;
    const double invNorm = 1.0 / std::sqrt(norm2);
    for (double& c : record.orientation) c *= invNorm;

    std::lock_guard<std::mutex> lock(m_mutex);
    record.frame = m_frame;
    size_t slot = m_next;
    for (size_t i = 0; i < kCapacity; ++i) {
        if (m_used[i] && m_ring[i].frame == record.frame && m_ring[i].eye == record.eye) {
            slot = i;
            break;
        }
    }
    if (slot == m_next) m_next = (m_next + 1) % kCapacity;
    m_ring[slot] = record;
    m_used[slot] = true;
    return true;
}

uint64_t CameraRecordStore::GetFrame() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_frame;
}

uint64_t CameraRecordStore::AdvanceFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return ++m_frame;
}

void CameraRecordStore::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_used.fill(false);
    m_next = 0;
}

ResolvedCameras CameraRecordStore::Resolve(uint32_t eye) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    ResolvedCameras out;
    out.eye = eye;
    out.frame = m_frame;

    for (size_t i = 0; i < kCapacity; ++i) {
        if (!m_used[i]) continue;
        const CameraRecord& r = m_ring[i];
        if (r.frame == m_frame && r.eye == eye) {
            out.current = r;
        } else if (m_frame > 0 && r.frame == m_frame - 1) {
            out.previousFrame = r;
        }
        if (r.eye == eye && r.frame < m_frame && m_frame - r.frame <= kMaxSameEyeFrameGap &&
            (!out.sameEyePrevious || r.frame > out.sameEyePrevious->frame)) {
            out.sameEyePrevious = r;
        }
    }

    if (out.current) {
        if (out.previousFrame) out.previousFrameRelative = ComputeRelativeCamera(*out.current, *out.previousFrame);
        if (out.sameEyePrevious) out.sameEyeRelative = ComputeRelativeCamera(*out.current, *out.sameEyePrevious);
    }
    return out;
}

} // namespace fs25vr
