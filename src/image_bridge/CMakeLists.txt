cmake_minimum_required(VERSION 3.10)
project(image_bridge)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(image_transport REQUIRED)
find_package(maro_library REQUIRED)
find_package(Boost REQUIRED COMPONENTS interprocess)
find_package(OpenCV REQUIRED)

add_executable(image_bridge_node src/image_bridge_node.cpp)

# ament_target_dependencies를 사용하여 모든 ROS2 의존성을 한 번에 연결
# maro_library가 이미 여기에 포함되어 있으므로, 컴파일러는 자동으로
# maro_library/include 경로를 탐색하게 됩니다.
ament_target_dependencies(image_bridge_node
  rclcpp
  sensor_msgs
  image_transport
  maro_library
)

# 비-ROS2 라이브러리들을 키워드 없이 '일반' 방식으로 링크합니다.
target_link_libraries(image_bridge_node
    Boost::interprocess
    ${OpenCV_LIBRARIES}
)

install(TARGETS
    image_bridge_node
    DESTINATION lib/${PROJECT_NAME}
)

ament_package()