find_package(GTest CONFIG REQUIRED)

add_executable(maro_transform_tests
    transform/test_convert.cpp
)

target_link_libraries(maro_transform_tests PRIVATE
    maro_transform
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(maro_transform_tests)

if(MARO_BUILD_PLUGIN)
    file(GLOB ROS2_PACKAGE_INCLUDES LIST_DIRECTORIES true "${ROS2_INSTALL}/include/*")
    set(PEER_INCLUDES ${ROS2_INSTALL}/include)
    foreach(dir ${ROS2_PACKAGE_INCLUDES})
        if(IS_DIRECTORY ${dir})
            get_filename_component(name ${dir} NAME)
            if(NOT name STREQUAL "idl" AND NOT name STREQUAL "idlc")
                list(APPEND PEER_INCLUDES ${dir})
            endif()
        endif()
    endforeach()

    add_executable(maro_test_peer peer/maro_test_peer.cpp)
    target_include_directories(maro_test_peer PRIVATE ${PEER_INCLUDES})
    target_compile_definitions(maro_test_peer PRIVATE
        NOMINMAX _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)

    foreach(ros_lib rclcpp rcl rcutils rcpputils rmw rosidl_runtime_c
                    rosidl_typesupport_cpp
                    sensor_msgs__rosidl_typesupport_cpp
                    builtin_interfaces__rosidl_typesupport_cpp
                    # rclcpp의 토픽 통계 기능(SubscriptionTopicStatistics)이
                    # 이 둘을 끌어온다. 브리프의 링크 목록에는 없었지만 링크
                    # 단계에서 libstatistics_collector 미해결 심볼로 드러났다
                    # (src/maro_plugin/CMakeLists.txt에서 이미 겪은 것과 같은 문제).
                    libstatistics_collector
                    statistics_msgs__rosidl_typesupport_cpp)
        target_link_libraries(maro_test_peer PRIVATE "${ROS2_INSTALL}/Lib/${ros_lib}.lib")
    endforeach()

    add_custom_command(TARGET maro_test_peer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${ROS2_INSTALL}/bin" "$<TARGET_FILE_DIR:maro_test_peer>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${ROS2_INSTALL}/opt/libyaml_vendor/bin" "$<TARGET_FILE_DIR:maro_test_peer>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${ROS2_INSTALL}/opt/spdlog_vendor/bin" "$<TARGET_FILE_DIR:maro_test_peer>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${ROS2_INSTALL}/opt/console_bridge_vendor/bin" "$<TARGET_FILE_DIR:maro_test_peer>"
    )
endif()
