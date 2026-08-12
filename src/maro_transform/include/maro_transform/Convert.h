#pragma once

#include "maro_transform/Types.h"

namespace maro {

// Maya(Y-up 우수) <-> ROS REP-103(Z-up 우수) 기저 변환.
// X축 기준 -90도 회전이며 행렬식이 +1이라 우수좌표계가 보존된다.
//   mayaToRos: (x, y, z) -> ( x, -z,  y)
//   rosToMaya: (x, y, z) -> ( x,  z, -y)
// 스케일은 위치에만 적용한다.

Vec3 mayaToRosPosition(const Vec3& maya, const SceneUnit& unit);
Vec3 rosToMayaPosition(const Vec3& ros, const SceneUnit& unit);

}  // namespace maro
