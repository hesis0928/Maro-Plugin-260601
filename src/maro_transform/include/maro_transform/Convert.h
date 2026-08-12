#pragma once

#include "maro_transform/Types.h"

namespace maro {

// Maya(Y-up 우수) <-> ROS REP-103(Z-up 우수) 기저 변환.
// X축 기준 -90도 회전이며 행렬식이 +1이라 우수좌표계가 보존된다.
//   mayaToRos: (x, y, z) -> ( x, -z,  y)
//   rosToMaya: (x, y, z) -> ( x,  z, -y)
// 스케일은 위치에만 적용한다.
//
// 전제: unit.metersPerMayaUnit != 0. 씬 단위는 Maya가 주는 값이라 0이 될 수 없고,
// 이 라이브러리는 순수 함수로 유지하기 위해 런타임 검사를 두지 않는다.
// 호출측이 값을 지어내는 경우에만 문제가 되며, 결과는 maro::isFinite로 걸린다.

Vec3 mayaToRosPosition(const Vec3& maya, const SceneUnit& unit);
Vec3 rosToMayaPosition(const Vec3& ros, const SceneUnit& unit);

}  // namespace maro
