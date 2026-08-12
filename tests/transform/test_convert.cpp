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
