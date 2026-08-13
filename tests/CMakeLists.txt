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
    set(MAYAPY "C:/Program Files/Autodesk/Maya2026/bin/mayapy.exe")

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
                    # /tf 모드가 구독하는 tf2_msgs/msg/TFMessage는
                    # geometry_msgs/msg/TransformStamped를 담는다. 플러그인
                    # 쪽(src/maro_plugin/CMakeLists.txt)이 이미 이 둘을 같은
                    # 이유로 링크하고 있다.
                    geometry_msgs__rosidl_typesupport_cpp
                    tf2_msgs__rosidl_typesupport_cpp
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

    # Maya의 플러그인 로더는 .mll을 LOAD_WITH_ALTERED_SEARCH_PATH로 열지
    # 않는다 (src/maro_plugin/CMakeLists.txt의 I8 주석, task-10-report.md
    # 참고) -- 그래서 옆에 스테이징해 둔 ROS 2 런타임 DLL이 로드되려면 그
    # 디렉터리가 mayapy.exe를 띄우는 프로세스의 PATH에 있어야 한다. ctest가
    # 띄우는 테스트 프로세스는 ctest 자신의 PATH를 물려받을 뿐 이 디렉터리를
    # 자동으로 넣어주지 않으므로, ENVIRONMENT_MODIFICATION의
    # path_list_prepend로 앞에 붙인다 -- 일반 ENVIRONMENT 프로퍼티로
    # "PATH=...;$ENV{PATH}"를 직접 쓰면 안 된다: ENVIRONMENT는 전체를 세미콜론
    # 구분 리스트로 저장하는데 $ENV{PATH} 자체가 세미콜론투성이라 리스트가
    # 조각나 PATH의 앞부분만 남고 나머지는 깨진 "VAR=value" 항목으로
    # 흩어진다. path_list_prepend는 값 하나(경로 하나)만 다루므로 이 문제가
    # 없고, 기존 PATH를 지우지 않고 앞에 이어 붙인다.
    #
    # ROS 2 피어가 필요한 테스트는 피어 실행 파일 자신의 디렉터리도 이미
    # 안고 있어(자기 자신의 POST_BUILD로 같은 DLL들을 복사) 별도로 PATH에
    # 넣을 필요는 없다 -- Windows는 실행 파일 자신의 디렉터리를 그 파일의
    # 의존 DLL 검색 경로에 항상 포함하므로, LoadLibrary가 아니라 직접 실행
    # (subprocess)되는 피어는 이 문제 자체를 겪지 않는다.

    # 플러그인만 있으면 되는 테스트. robustness는 순수 DG 조작/평가만 하고
    # 브리지(rclcpp)를 켜지 않으므로 이 그룹에 속한다.
    foreach(maya_test load axis_node binding capability_stack delete_rules
                      robustness)
        add_test(NAME maya_${maya_test}
                 COMMAND "${MAYAPY}"
                         "${CMAKE_CURRENT_SOURCE_DIR}/maya/test_${maya_test}.py")
        set_tests_properties(maya_${maya_test} PROPERTIES
            ENVIRONMENT "MARO_PLUGIN_PATH=$<TARGET_FILE:maro>"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:maro>"
            TIMEOUT 240)
    endforeach()

    # bridge_pump는 피어를 쓰지 않지만 maroStartBridge()로 실제 rclcpp
    # 컨텍스트(DDS 도메인)를 띄운다. publish/contract와 같은 도메인을
    # 공유하므로 그것들과 동시에 돌면 서로 간섭한다 -- RUN_SERIAL이 필요하다.
    add_test(NAME maya_bridge_pump
             COMMAND "${MAYAPY}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/maya/test_bridge_pump.py")
    set_tests_properties(maya_bridge_pump PROPERTIES
        ENVIRONMENT "MARO_PLUGIN_PATH=$<TARGET_FILE:maro>"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:maro>"
        TIMEOUT 240
        RUN_SERIAL TRUE)

    # ROS 2 상대역이 필요한 테스트는 피어 경로도 받는다.
    foreach(maya_test publish contract)
        add_test(NAME maya_${maya_test}
                 COMMAND "${MAYAPY}"
                         "${CMAKE_CURRENT_SOURCE_DIR}/maya/test_${maya_test}.py")
        set_tests_properties(maya_${maya_test} PROPERTIES
            ENVIRONMENT "MARO_PLUGIN_PATH=$<TARGET_FILE:maro>;MARO_PEER_PATH=$<TARGET_FILE:maro_test_peer>"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:maro>"
            TIMEOUT 360)
        # 같은 DDS 도메인을 쓰므로 병렬 실행 시 서로 간섭한다.
        set_tests_properties(maya_${maya_test} PROPERTIES RUN_SERIAL TRUE)
    endforeach()
endif()
