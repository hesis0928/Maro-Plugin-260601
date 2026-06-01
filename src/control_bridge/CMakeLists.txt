cmake_minimum_required(VERSION 3.10)
project(control_bridge)

if(POLICY CMP0111)
  cmake_policy(SET CMP0111 NEW)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)

# OpenCV 및 TIFF 설정 (vcpkg 툴체인을 사용하므로 find_package만 하면 됩니다)
find_package(TIFF REQUIRED)
find_package(OpenCV CONFIG REQUIRED)

# maro_library 찾기
find_package(maro_library REQUIRED)
find_package(Boost REQUIRED COMPONENTS interprocess)

add_executable(control_bridge_node src/control_bridge_node.cpp)

# 모든 의존성 패키지(ROS 및 Custom 라이브러리)를 여기에 나열
ament_target_dependencies(control_bridge_node
  rclcpp
  geometry_msgs
  sensor_msgs
  maro_library
)

# target_link_libraries 쪽에 Boost::interprocess 추가
target_link_libraries(control_bridge_node
    "C:/Users/ckd30/Projects/Maya_Ros_Sim/install/maro_library/lib/maro_library.lib"
    ${OpenCV_LIBS}
    TIFF::TIFF
    Boost::interprocess
)
# 파일 맨 마지막에 추가
install(TARGETS control_bridge_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()