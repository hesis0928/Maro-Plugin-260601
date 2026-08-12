#include <cmath>
#include <random>
#include <gtest/gtest.h>

#include "maro_transform/Types.h"
#include "maro_transform/Convert.h"

TEST(Harness, TypesAreUsable) {
    maro::Vec3 v{1.0, 2.0, 3.0};
    EXPECT_TRUE(maro::isFinite(v));

    maro::Quat q;
    EXPECT_DOUBLE_EQ(q.w, 1.0);

    maro::AxisConvention c;
    EXPECT_EQ(c.axis, maro::LocalAxis::Y);
    EXPECT_FALSE(c.invert);
}

TEST(Harness, NonFiniteIsDetected) {
    maro::Vec3 bad{1.0, std::nan(""), 3.0};
    EXPECT_FALSE(maro::isFinite(bad));
}

namespace {
constexpr double kEps = 1e-12;
}

TEST(Position, MayaUpBecomesRosUp) {
    // Maya의 위 방향은 +Y, ROS의 위 방향은 +Z다.
    const maro::SceneUnit unit{1.0};  // 스케일 영향을 배제
    const maro::Vec3 mayaUp{0.0, 1.0, 0.0};

    const maro::Vec3 ros = maro::mayaToRosPosition(mayaUp, unit);

    EXPECT_NEAR(ros.x, 0.0, kEps);
    EXPECT_NEAR(ros.y, 0.0, kEps);
    EXPECT_NEAR(ros.z, 1.0, kEps);
}

TEST(Position, ScaleConvertsCentimetresToMetres) {
    const maro::SceneUnit cm{0.01};
    const maro::Vec3 maya{100.0, 0.0, 0.0};

    const maro::Vec3 ros = maro::mayaToRosPosition(maya, cm);

    EXPECT_NEAR(ros.x, 1.0, kEps);
    EXPECT_NEAR(ros.y, 0.0, kEps);
    EXPECT_NEAR(ros.z, 0.0, kEps);
}

TEST(Position, RoundTripIsIdentity) {
    const maro::SceneUnit cm{0.01};
    const maro::Vec3 original{12.5, -3.25, 88.0};

    const maro::Vec3 back =
        maro::mayaToRosPosition(maro::rosToMayaPosition(original, cm), cm);

    EXPECT_NEAR(back.x, original.x, 1e-9);
    EXPECT_NEAR(back.y, original.y, 1e-9);
    EXPECT_NEAR(back.z, original.z, 1e-9);
}

TEST(Position, MayaForwardFlipsSignIntoRosY) {
    // Maya +Z must land on ROS -Y. This is the case that distinguishes a
    // proper rotation from a reflection; earlier tests all used z == 0,
    // where -0 and 0 are indistinguishable.
    const maro::SceneUnit unit{1.0};
    const maro::Vec3 maya{0.0, 0.0, 1.0};

    const maro::Vec3 ros = maro::mayaToRosPosition(maya, unit);

    EXPECT_NEAR(ros.x, 0.0, kEps);
    EXPECT_NEAR(ros.y, -1.0, kEps);
    EXPECT_NEAR(ros.z, 0.0, kEps);
}

TEST(Position, RosLeftFlipsSignIntoMayaZ) {
    // The inverse direction, likewise checked on the axis that changes sign.
    const maro::SceneUnit unit{1.0};
    const maro::Vec3 ros{0.0, 1.0, 0.0};

    const maro::Vec3 maya = maro::rosToMayaPosition(ros, unit);

    EXPECT_NEAR(maya.x, 0.0, kEps);
    EXPECT_NEAR(maya.y, 0.0, kEps);
    EXPECT_NEAR(maya.z, -1.0, kEps);
}

TEST(Rotation, MayaYawBecomesRosYaw) {
    // Maya에서 위(+Y) 축 기준 90도 회전.
    const double h = std::sqrt(2.0) / 2.0;
    const maro::Quat mayaYaw{0.0, h, 0.0, h};

    const maro::Quat ros = maro::mayaToRosRotation(mayaYaw);

    // ROS에서는 위 축이 +Z이므로 회전축이 Z로 옮겨간다.
    EXPECT_NEAR(ros.x, 0.0, 1e-12);
    EXPECT_NEAR(ros.y, 0.0, 1e-12);
    EXPECT_NEAR(ros.z, h, 1e-12);
    EXPECT_NEAR(ros.w, h, 1e-12);
}

TEST(Rotation, IdentityStaysIdentity) {
    const maro::Quat id;
    const maro::Quat ros = maro::mayaToRosRotation(id);

    EXPECT_NEAR(ros.x, 0.0, 1e-12);
    EXPECT_NEAR(ros.y, 0.0, 1e-12);
    EXPECT_NEAR(ros.z, 0.0, 1e-12);
    EXPECT_NEAR(ros.w, 1.0, 1e-12);
}

TEST(Rotation, MayaRollFlipsSignIntoRosY) {
    // 부호가 실제로 뒤집히는 축을 검사한다. 위 두 테스트는 qz가 0이라
    // -qz와 qz를 구분하지 못하고, 왕복 테스트는 반사(reflection)도 통과시킨다.
    // Task 2에서 위치 변환이 같은 함정에 빠졌던 것과 같은 구조다.
    const double h = std::sqrt(2.0) / 2.0;
    const maro::Quat mayaRoll{0.0, 0.0, h, h};   // Maya +Z 축 회전

    const maro::Quat ros = maro::mayaToRosRotation(mayaRoll);

    EXPECT_NEAR(ros.x, 0.0, 1e-12);
    EXPECT_NEAR(ros.y, -h, 1e-12);   // Maya +Z -> ROS -Y
    EXPECT_NEAR(ros.z, 0.0, 1e-12);
    EXPECT_NEAR(ros.w, h, 1e-12);
}

TEST(Rotation, RosPitchFlipsSignIntoMayaZ) {
    // 역방향도 부호가 걸린 축에서 검사한다.
    const double h = std::sqrt(2.0) / 2.0;
    const maro::Quat rosPitch{0.0, h, 0.0, h};   // ROS +Y 축 회전

    const maro::Quat maya = maro::rosToMayaRotation(rosPitch);

    EXPECT_NEAR(maya.x, 0.0, 1e-12);
    EXPECT_NEAR(maya.y, 0.0, 1e-12);
    EXPECT_NEAR(maya.z, -h, 1e-12);  // ROS +Y -> Maya -Z
    EXPECT_NEAR(maya.w, h, 1e-12);
}

TEST(Rotation, RoundTripIsIdentity) {
    const maro::Quat original{0.18257418583505536, 0.3651483716701107,
                              0.5477225575051661, 0.7302967433402214};

    const maro::Quat back =
        maro::mayaToRosRotation(maro::rosToMayaRotation(original));

    EXPECT_NEAR(back.x, original.x, 1e-12);
    EXPECT_NEAR(back.y, original.y, 1e-12);
    EXPECT_NEAR(back.z, original.z, 1e-12);
    EXPECT_NEAR(back.w, original.w, 1e-12);
}

TEST(AxisConventionTest, VectorSelectsAndSignsLocalAxis) {
    const maro::Vec3 y = maro::axisVectorOf({maro::LocalAxis::Y, false});
    EXPECT_NEAR(y.x, 0.0, 1e-12);
    EXPECT_NEAR(y.y, 1.0, 1e-12);
    EXPECT_NEAR(y.z, 0.0, 1e-12);

    const maro::Vec3 negZ = maro::axisVectorOf({maro::LocalAxis::Z, true});
    EXPECT_NEAR(negZ.z, -1.0, 1e-12);
}

TEST(AxisConventionTest, InvertFlipsJointSign) {
    const maro::AxisConvention plain{maro::LocalAxis::Y, false};
    const maro::AxisConvention flipped{maro::LocalAxis::Y, true};

    const double angle = 0.7;
    const maro::Quat q = maro::jointToMayaRotation(angle, plain);

    EXPECT_NEAR(maro::mayaRotationToJoint(q, plain), angle, 1e-9);
    EXPECT_NEAR(maro::mayaRotationToJoint(q, flipped), -angle, 1e-9);
}

TEST(AxisConventionTest, JointRoundTripSurvivesRandomAngles) {
    std::mt19937 rng(20260813);
    // 왕복이 유일하게 정의되는 구간 (-pi, pi].
    std::uniform_real_distribution<double> angleDist(-3.14159, 3.14159);

    const maro::LocalAxis axes[] = {maro::LocalAxis::X, maro::LocalAxis::Y,
                                    maro::LocalAxis::Z};

    for (int i = 0; i < 2000; ++i) {
        const maro::AxisConvention conv{axes[i % 3], (i % 2) == 0};
        const double angle = angleDist(rng);

        const double back =
            maro::mayaRotationToJoint(maro::jointToMayaRotation(angle, conv), conv);

        ASSERT_NEAR(back, angle, 1e-9)
            << "axis=" << static_cast<int>(conv.axis)
            << " invert=" << conv.invert << " angle=" << angle;
    }
}

TEST(AxisConventionTest, FullPoseRoundTripSurvivesRandomValues) {
    std::mt19937 rng(99);
    std::uniform_real_distribution<double> posDist(-1000.0, 1000.0);
    std::uniform_real_distribution<double> qDist(-1.0, 1.0);

    // 극단 스케일도 함께 흘린다.
    const double scales[] = {1.0, 0.01, 0.001, 100.0};

    for (int i = 0; i < 2000; ++i) {
        const maro::SceneUnit unit{scales[i % 4]};
        const maro::Vec3 p{posDist(rng), posDist(rng), posDist(rng)};

        const maro::Vec3 pBack =
            maro::mayaToRosPosition(maro::rosToMayaPosition(p, unit), unit);
        ASSERT_NEAR(pBack.x, p.x, 1e-6);
        ASSERT_NEAR(pBack.y, p.y, 1e-6);
        ASSERT_NEAR(pBack.z, p.z, 1e-6);

        maro::Quat q{qDist(rng), qDist(rng), qDist(rng), qDist(rng)};
        const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (n < 1e-6) continue;
        q = maro::Quat{q.x / n, q.y / n, q.z / n, q.w / n};

        const maro::Quat qBack =
            maro::mayaToRosRotation(maro::rosToMayaRotation(q));
        ASSERT_NEAR(qBack.x, q.x, 1e-12);
        ASSERT_NEAR(qBack.y, q.y, 1e-12);
        ASSERT_NEAR(qBack.z, q.z, 1e-12);
        ASSERT_NEAR(qBack.w, q.w, 1e-12);
    }
}
