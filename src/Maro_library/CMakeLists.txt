cmake_minimum_required(VERSION 3.8)
project(maro_library)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(OpenCV REQUIRED)

add_library(${PROJECT_NAME} SHARED
  src/maro_bridge.cpp
)

# Windows에서 .dll과 .lib를 올바르게 생성하고 export하기 위한 핵심 설정
set_target_properties(${PROJECT_NAME} PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# ament 패키지들의 의존성을 먼저 설정합니다.
ament_target_dependencies(${PROJECT_NAME}
  rclcpp
  sensor_msgs
)

# OpenCV와 같은 비-ament 패키지는 키워드 없이 '일반' 방식으로 링크하여
# ament_target_dependencies와의 문법 충돌을 피합니다.
target_link_libraries(${PROJECT_NAME} ${OpenCV_LIBRARIES})

# 다른 패키지가 이 라이브러리를 사용할 수 있도록 export 규칙을 설정합니다.
ament_export_include_directories(include)
ament_export_dependencies(rclcpp sensor_msgs OpenCV)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)

# 빌드된 결과물을 install 폴더로 복사하는 규칙입니다.
install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/
  DESTINATION include
)
install(EXPORT export_${PROJECT_NAME}
  DESTINATION share/${PROJECT_NAME}/cmake
)

ament_package()