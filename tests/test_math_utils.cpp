#include <gtest/gtest.h>
#include "core/math_utils.h"

using namespace fs25vr;

TEST(MathUtilsTest, Vector3Operations) {
    Vector3 defaultVec;
    EXPECT_FLOAT_EQ(defaultVec.x, 0.0f);
    EXPECT_FLOAT_EQ(defaultVec.y, 0.0f);
    EXPECT_FLOAT_EQ(defaultVec.z, 0.0f);

    Vector3 v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);

    Vector3 vEqual(1.0f, 2.0f, 3.0f);
    EXPECT_EQ(v, vEqual);

    Vector3 vDifferent(1.0f, 2.0f, 4.0f);
    EXPECT_NE(v, vDifferent);
}

TEST(MathUtilsTest, QuaternionOperations) {
    Quaternion defaultQuat;
    EXPECT_FLOAT_EQ(defaultQuat.x, 0.0f);
    EXPECT_FLOAT_EQ(defaultQuat.y, 0.0f);
    EXPECT_FLOAT_EQ(defaultQuat.z, 0.0f);
    EXPECT_FLOAT_EQ(defaultQuat.w, 1.0f);

    Quaternion q1(0.0f, 0.0f, 0.0f, 1.0f);
    Quaternion q2(0.0f, 0.70710678f, 0.0f, 0.70710678f);
    Quaternion qProduct = q1 * q2;

    EXPECT_NEAR(qProduct.x, q2.x, 1e-5f);
    EXPECT_NEAR(qProduct.y, q2.y, 1e-5f);
    EXPECT_NEAR(qProduct.z, q2.z, 1e-5f);
    EXPECT_NEAR(qProduct.w, q2.w, 1e-5f);
}

TEST(MathUtilsTest, EulerAnglesOperations) {
    EulerAngles defaultEuler;
    EXPECT_FLOAT_EQ(defaultEuler.pitch, 0.0f);
    EXPECT_FLOAT_EQ(defaultEuler.yaw, 0.0f);
    EXPECT_FLOAT_EQ(defaultEuler.roll, 0.0f);

    EulerAngles e(0.1f, 0.2f, 0.3f);
    EXPECT_FLOAT_EQ(e.pitch, 0.1f);
    EXPECT_FLOAT_EQ(e.yaw, 0.2f);
    EXPECT_FLOAT_EQ(e.roll, 0.3f);

    EulerAngles eEqual(0.1f, 0.2f, 0.3f);
    EXPECT_EQ(e, eEqual);
}

TEST(MathUtilsTest, QuaternionToEulerAndBack) {
    // 90 degrees around Y (yaw)
    float halfAngle = 0.25f * 3.14159265f;
    Quaternion qYaw(0.0f, std::sin(halfAngle), 0.0f, std::cos(halfAngle));
    EulerAngles angles = QuaternionToEuler(qYaw);

    EXPECT_NEAR(angles.yaw, 3.14159265f * 0.5f, 1e-4f);
    EXPECT_NEAR(angles.pitch, 0.0f, 1e-4f);
    EXPECT_NEAR(angles.roll, 0.0f, 1e-4f);
}

TEST(MathUtilsTest, RotateVectorByQuaternion) {
    // 90 degrees around Y (yaw): (1, 0, 0) should rotate to (0, 0, -1)
    float halfAngle = 0.25f * 3.14159265f;
    Quaternion qYaw(0.0f, std::sin(halfAngle), 0.0f, std::cos(halfAngle));
    Vector3 v(1.0f, 0.0f, 0.0f);
    Vector3 rotated = RotateVectorByQuaternion(qYaw, v);

    EXPECT_NEAR(rotated.x, 0.0f, 1e-5f);
    EXPECT_NEAR(rotated.y, 0.0f, 1e-5f);
    EXPECT_NEAR(rotated.z, -1.0f, 1e-5f);
}
