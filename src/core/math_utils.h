#pragma once

#include <cmath>
#include <algorithm>

namespace fs25vr {

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vector3() = default;
    constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    bool operator==(const Vector3& other) const = default;
};

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quaternion() = default;
    constexpr Quaternion(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    bool operator==(const Quaternion& other) const = default;

    constexpr Quaternion operator*(const Quaternion& o) const {
        return Quaternion(
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
            w * o.w - x * o.x - y * o.y - z * o.z
        );
    }
};

struct EulerAngles {
    float pitch = 0.0f; // radians (X-axis)
    float yaw = 0.0f;   // radians (Y-axis)
    float roll = 0.0f;  // radians (Z-axis)

    constexpr EulerAngles() = default;
    constexpr EulerAngles(float pitch_, float yaw_, float roll_) : pitch(pitch_), yaw(yaw_), roll(roll_) {}

    bool operator==(const EulerAngles& other) const = default;
};

inline EulerAngles QuaternionToEuler(const Quaternion& q) {
    EulerAngles angles;
    float sinPitch = 2.0f * (q.w * q.x - q.y * q.z);
    if (std::abs(sinPitch) >= 0.99999f) {
        angles.pitch = std::copysign(3.14159265f / 2.0f, sinPitch);
        angles.yaw = 2.0f * std::atan2(q.y, q.w);
        angles.roll = 0.0f;
    } else {
        angles.pitch = std::asin(std::clamp(sinPitch, -1.0f, 1.0f));
        angles.yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        angles.roll = std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.x * q.x + q.z * q.z));
    }
    return angles;
}

inline Vector3 RotateVectorByQuaternion(const Quaternion& q, const Vector3& v) {
    // t = 2 * cross(q.xyz, v)
    float tx = 2.0f * (q.y * v.z - q.z * v.y);
    float ty = 2.0f * (q.z * v.x - q.x * v.z);
    float tz = 2.0f * (q.x * v.y - q.y * v.x);

    // v' = v + qw * t + cross(q.xyz, t)
    return Vector3(
        v.x + q.w * tx + (q.y * tz - q.z * ty),
        v.y + q.w * ty + (q.z * tx - q.x * tz),
        v.z + q.w * tz + (q.x * ty - q.y * tx)
    );
}

} // namespace fs25vr
