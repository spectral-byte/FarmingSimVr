#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace fs25vr {

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

struct RelativeCamera {
    double translation[3] = {0.0, 0.0, 0.0};
    double rotation[4] = {0.0, 0.0, 0.0, 1.0};
    double translationLength = 0.0;
    double rotationAngle = 0.0; // radians
};

RelativeCamera ComputeRelativeCamera(const CameraRecord& reference, const CameraRecord& other);

struct ResolvedCameras {
    uint32_t eye = 0;
    uint64_t frame = 0;
    std::optional<CameraRecord> current;         // this frame, this eye
    std::optional<CameraRecord> previousFrame;   // the frame rendered immediately before, either eye
    std::optional<CameraRecord> sameEyePrevious; // the last frame this eye rendered
    std::optional<RelativeCamera> previousFrameRelative;
    std::optional<RelativeCamera> sameEyeRelative;
};

std::string DescribeResolvedCameras(const ResolvedCameras& resolved);

class CameraRecordStore {
public:
    static constexpr uint32_t kEyeCount = 2;
    static constexpr size_t kCapacity = 8;
    static constexpr uint64_t kMaxSameEyeFrameGap = 2;

    bool Report(CameraRecord record);

    uint64_t GetFrame() const;
    uint64_t AdvanceFrame();
    void Clear();

    ResolvedCameras Resolve(uint32_t eye) const;

private:
    mutable std::mutex m_mutex;
    uint64_t m_frame = 0;
    std::array<CameraRecord, kCapacity> m_ring{};
    std::array<bool, kCapacity> m_used{};
    size_t m_next = 0;
};

}
