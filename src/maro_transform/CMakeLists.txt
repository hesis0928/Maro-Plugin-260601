add_library(maro_transform STATIC
    src/Convert.cpp
)

target_include_directories(maro_transform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(maro_transform PUBLIC cxx_std_20)
