# devkit 공식 진입점. .mll 접미사, NT_PLUGIN 정의, devkit include/lib를 처리한다.
include($ENV{DEVKIT_LOCATION}/cmake/pluginEntry.cmake)

set(PROJECT_NAME maro)

set(SOURCE_FILES
    MaroPluginMain.cpp
    MaroAxisNode.cpp
    MaroCapabilityNodes.cpp
    MaroCommands.cpp
    MaroDeleteWatcher.cpp
    MaroRosRuntime.cpp
    MaroPump.cpp
    MaroCommandDeviceNode.cpp
)

set(LIBRARIES
    OpenMaya
    OpenMayaUI
    OpenMayaRender
    OpenMayaAnim
    Foundation
)

# ROS 2 헤더. install/include 아래 패키지별 하위 디렉터리를 모두 넣는다.
# idl/idlc는 string.h 등 표준 헤더와 이름이 충돌하므로 제외한다.
file(GLOB ROS2_PACKAGE_INCLUDES LIST_DIRECTORIES true "${ROS2_INSTALL}/include/*")
set(INCLUDE_DIRS ${ROS2_INSTALL}/include)
foreach(dir ${ROS2_PACKAGE_INCLUDES})
    if(IS_DIRECTORY ${dir})
        get_filename_component(name ${dir} NAME)
        if(NOT name STREQUAL "idl" AND NOT name STREQUAL "idlc")
            list(APPEND INCLUDE_DIRS ${dir})
        endif()
    endif()
endforeach()

set(LIBRARY_DIRS ${ROS2_INSTALL}/Lib)

build_plugin()

# build_plugin()이 만든 타깃에 나머지를 얹는다. devkit의 pluginEntry.cmake가
# build_plugin() 안에서 이미 target_link_libraries(${PROJECT_NAME} ${MAYA_LIBRARIES})를
# 키워드 없는(plain) 형태로 호출해 놓았다. CMake는 같은 타깃에 plain과
# keyword(PRIVATE/PUBLIC) 시그니처를 섞어 쓰는 것을 금지하므로, 여기서도
# PRIVATE 없이 plain 형태로 이어 붙인다.
target_link_libraries(${PROJECT_NAME} maro_transform)

foreach(ros_lib rclcpp rcl rcutils rcpputils rmw rosidl_runtime_c
                rosidl_typesupport_cpp
                sensor_msgs__rosidl_typesupport_cpp
                geometry_msgs__rosidl_typesupport_cpp
                tf2_msgs__rosidl_typesupport_cpp
                builtin_interfaces__rosidl_typesupport_cpp
                # rclcpp의 토픽 통계 기능(SubscriptionTopicStatistics)이
                # 이 둘을 끌어온다. 브리프의 링크 목록에는 없었지만 링크
                # 단계에서 libstatistics_collector 미해결 심볼로 드러났다.
                libstatistics_collector
                statistics_msgs__rosidl_typesupport_cpp)
    target_link_libraries(${PROJECT_NAME} "${ROS2_INSTALL}/Lib/${ros_lib}.lib")
endforeach()

# windows.h의 min/max 매크로가 rclcpp의 numeric_limits 사용과 충돌한다.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    NOMINMAX
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
)

# 런타임 DLL을 플러그인 옆에 둔다. 벤더 DLL은 install/bin이 아니라
# install/opt/<vendor>/bin에 있으므로 반드시 함께 복사해야 로드된다.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ROS2_INSTALL}/bin" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ROS2_INSTALL}/opt/libyaml_vendor/bin" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ROS2_INSTALL}/opt/spdlog_vendor/bin" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ROS2_INSTALL}/opt/console_bridge_vendor/bin" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Maro: staging ROS 2 runtime DLLs next to the plugin"
)
