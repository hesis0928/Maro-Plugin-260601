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

}  // namespace maro
