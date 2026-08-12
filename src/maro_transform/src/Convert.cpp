#include "maro_transform/Convert.h"

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

}  // namespace maro
