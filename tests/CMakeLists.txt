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
