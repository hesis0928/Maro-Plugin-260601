cmake_minimum_required(VERSION 3.20)
project(maro LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 외부 위치. 캐시 변수로 주입해 하드코딩을 피한다.
set(DEVKIT_LOCATION "C:/Users/ckd30/Projects/devkitBase" CACHE PATH "Maya devkit root")
set(ROS2_INSTALL    "C:/dev/ros2_jazzy/install"          CACHE PATH "ROS 2 install prefix")

# devkit의 pluginEntry.cmake는 환경 변수로 읽으므로 여기서 채워준다.
set(ENV{DEVKIT_LOCATION} "${DEVKIT_LOCATION}")

option(MARO_BUILD_PLUGIN "Build the Maya plugin (needs devkit + ROS 2)" ON)
option(MARO_BUILD_TESTS  "Build tests" ON)

add_subdirectory(src/maro_transform)

if(MARO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MARO_BUILD_PLUGIN)
    add_subdirectory(src/maro_plugin)
endif()
