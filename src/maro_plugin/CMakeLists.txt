# devkit 공식 진입점. .mll 접미사, NT_PLUGIN 정의, devkit include/lib를 처리한다.
include($ENV{DEVKIT_LOCATION}/cmake/pluginEntry.cmake)

set(PROJECT_NAME maro)

set(SOURCE_FILES
    MaroPluginMain.cpp
)

set(LIBRARIES
    OpenMaya
    OpenMayaUI
    OpenMayaRender
    OpenMayaAnim
    Foundation
)

build_plugin()
