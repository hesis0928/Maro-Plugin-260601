"""maroAxis 노드가 등록되고 기대한 어트리뷰트를 갖는지 확인한다."""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
cmds.loadPlugin(plugin)

cmds.file(new=True, force=True)
axis = cmds.createNode("maroAxis")
print("created:", axis)

expected = [
    "targetObject",
    "parentAxis",
    "jointName",
    "capabilityIn",
    "conventionAxis",
    "conventionInvert",
    "controlMode",
    "outValue",
    "outTransform",
    "enabled",
]
for attr in expected:
    assert cmds.attributeQuery(attr, node=axis, exists=True), f"missing attr: {attr}"
print("attributes OK")

# 기본값 확인: 축은 기본적으로 사용 가능하고 Manual 모드다.
assert cmds.getAttr(axis + ".enabled") is True
assert cmds.getAttr(axis + ".controlMode") == 0, "default controlMode must be Manual"
print("defaults OK")

# 씬에 남아있는 maroAxis 인스턴스가 있으면 unloadPlugin이 거부하므로
# 언로드 전에 씬을 비운다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
