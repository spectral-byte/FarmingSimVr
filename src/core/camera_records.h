#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace fs25vr {

// The camera the engine rendered one stereo sub-pass with, as the Companion Mod reports it after
// binding the eye camera. World transform in double precision (map coordinates reach millions of
// metres), orientation as a unit quaternion (x, y, z, w).
struct CameraRecord {
    uint32_t eye = 0;
    uint64_t frame = 0; // rendered-frame stamp, assigned by CameraRecordStore::Report
    double position[3] = {0.0, 0.0, 0.0};
    double orientation[4] = {0.0, 0.0, 0.0, 1.0};
    float fovY = 0.0f;
    float nearClip = 0.0f;
    float farClip = 0.0f;
    float projOffsetX = 0.0f;
    float projOffsetY = 0.0f;
};

// Another camera's transform relative to a reference ("current") camera, computed in double
// precision: translation is the other camera's position expressed in the reference camera's axes,
// rotation is conj(reference) * other.
struct RelativeCamera {
    double translation[3] = {0.0, 0.0, 0.0};
    double rotation[4] = {0.0, 0.0, 0.0, 1.0};
    double translationLength = 0.0;
    double rotationAngle = 0.0; // radians
};

RelativeCamera ComputeRelativeCamera(const CameraRecord& reference, const CameraRecord& other);

// The cameras a DLSS evaluation needs. A record that was not reported for the right frame is absent;
// relative transforms exist only when the current camera and the other record both do.
struct ResolvedCameras {
    uint32_t eye = 0;
    uint64_t frame = 0;
    std::optional<CameraRecord> current;         // this frame, this eye
    std::optional<CameraRecord> previousFrame;   // the frame rendered immediately before, either eye
    std::optional<CameraRecord> sameEyePrevious; // the last frame this eye rendered
    std::optional<RelativeCamera> previousFrameRelative;
    std::optional<RelativeCamera> sameEyeRelative;

    // True at an Eye Order Alternation swap: the eye rendered twice in a row, so the previous
    // frame already is the same eye's previous frame.
    bool PreviousFrameIsSameEye() const {
        return previousFrame && sameEyePrevious && previousFrame->frame == sameEyePrevious->frame;
    }
};

// One-line text for the vrDlssDiag trace: which records resolved, and the relative translation and
// rotation, or which are missing.
std::string DescribeResolvedCameras(const ResolvedCameras& resolved);

// A short ring of camera records stamped with a rendered-frame counter. The Companion Mod reports
// (Luau thread), the DXGI proxy advances the counter once per Present in stereo presentation, and
// the NGX hook resolves (render thread), so access is serialised.
class CameraRecordStore {
public:
    static constexpr uint32_t kEyeCount = 2;
    static constexpr size_t kCapacity = 8;
    // The oldest frame distance at which a record still counts as the same eye's previous camera: the
    // eye order swap leaves up to three frames between an eye's renders (R L | L R).
    static constexpr uint64_t kMaxSameEyeFrameGap = 3;

    // Stamps the record with the current rendered frame and keeps it. Returns false (and keeps
    // nothing) for an unknown eye or a non-finite or zero-length orientation. The same eye reported
    // twice in one frame keeps the latest.
    bool Report(CameraRecord record);

    uint64_t GetFrame() const;
    uint64_t AdvanceFrame();
    // Drops every record: frames outside stereo presentation report none, and the counter does not
    // advance there, so without this their records would read as the current frame's.
    void Clear();

    ResolvedCameras Resolve(uint32_t eye) const;

private:
    mutable std::mutex m_mutex;
    uint64_t m_frame = 0;
    std::array<CameraRecord, kCapacity> m_ring{};
    std::array<bool, kCapacity> m_used{};
    size_t m_next = 0;
};

} // namespace fs25vr
