#include "maro_transform/Convert.h"

#include <algorithm>
#include <cmath>

namespace maro {

Vec3 mayaToRosPosition(const Vec3& maya, const SceneUnit& unit) {
    const double s = unit.metersPerMayaUnit;
    return Vec3{maya.x * s, -maya.z * s, maya.y * s};
}

Vec3 rosToMayaPosition(const Vec3& ros, const SceneUnit& unit) {
    const double s = unit.metersPerMayaUnit;
    return Vec3{ros.x / s, ros.z / s, -ros.y / s};
}

Quat mayaToRosRotation(const Quat& maya) {
    return Quat{maya.x, -maya.z, maya.y, maya.w};
}

Quat rosToMayaRotation(const Quat& ros) {
    return Quat{ros.x, ros.z, -ros.y, ros.w};
}

Vec3 axisVectorOf(const AxisConvention& conv) {
    const double s = conv.invert ? -1.0 : 1.0;
    switch (conv.axis) {
        case LocalAxis::X: return Vec3{s, 0.0, 0.0};
        case LocalAxis::Y: return Vec3{0.0, s, 0.0};
        case LocalAxis::Z: return Vec3{0.0, 0.0, s};
    }
    return Vec3{0.0, s, 0.0};
}

Quat jointToMayaRotation(double angleRad, const AxisConvention& conv) {
    const Vec3 axis = axisVectorOf(conv);
    const double half = angleRad * 0.5;
    const double sn = std::sin(half);
    return Quat{axis.x * sn, axis.y * sn, axis.z * sn, std::cos(half)};
}

double mayaRotationToJoint(const Quat& maya, const AxisConvention& conv) {
    const Vec3 axis = axisVectorOf(conv);

    // 회전축 성분을 부호 있는 sin(theta/2)로 사영하고,
    // atan2로 (-pi, pi] 구간의 각을 복원한다.
    const double sinHalf = maya.x * axis.x + maya.y * axis.y + maya.z * axis.z;
    const double cosHalf = std::clamp(maya.w, -1.0, 1.0);

    return 2.0 * std::atan2(sinHalf, cosHalf);
}

}  // namespace maro
