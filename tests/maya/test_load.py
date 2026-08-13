"""GUI 없는 Maya에서 플러그인이 로드되고 언로드되는지 확인한다.

이 스크립트는 mayapy로 실행한다. 성공하면 종료 코드 0으로 끝난다.
프로세스가 끝나지 않으면 정리(teardown) 결함이다.
"""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402  (standalone 초기화 후에만 import 가능)

plugin = os.environ["MARO_PLUGIN_PATH"]

cmds.loadPlugin(plugin)
assert cmds.pluginInfo(plugin, query=True, loaded=True), "plugin did not load"
print("load OK")

cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
print("unload OK")

maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
