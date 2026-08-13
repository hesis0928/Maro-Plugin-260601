add_library(maro_transform STATIC
    src/Convert.cpp
)

target_include_directories(maro_transform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Maya 2026 devkit이 플러그인 타깃을 C++17로 강제한다(devkit.cmake). 이 라이브러리는
# 플러그인에 링크되므로 표준을 맞춰야 하며, C++20 기능을 쓰는 곳도 없다.
target_compile_features(maro_transform PUBLIC cxx_std_17)
