#pragma once

#include <cmath>

namespace maro {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// (x, y, z, w) 순서. ROS geometry_msgs/Quaternion과 같은 순서다.
struct Quat {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

enum class LocalAxis { X, Y, Z };

// 사용자 기준 축 보정. 어느 로컬 축이 회전축인지와 부호를 담는다.
struct AxisConvention {
    LocalAxis axis = LocalAxis::Y;
    bool invert = false;
};

// Maya 씬 단위 1당 미터. Maya 기본값 cm이면 0.01이다.
struct SceneUnit {
    double metersPerMayaUnit = 0.01;
};

inline bool isFinite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline bool isFinite(const Quat& q) {
    return std::isfinite(q.x) && std::isfinite(q.y)
        && std::isfinite(q.z) && std::isfinite(q.w);
}

}  // namespace maro
