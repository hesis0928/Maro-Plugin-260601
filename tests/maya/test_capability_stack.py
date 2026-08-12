"""스택 합성: rotation이 값을 만들고 limit들이 순차적으로 클램프한다."""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

axis = cmds.createNode("maroAxis", name="axis1")
rot = cmds.createNode("maroRotation", name="rot1")

cmds.connectAttr(rot + ".capabilityOut", axis + ".capabilityIn[0]")
cmds.setAttr(rot + ".angle", 1.0)
assert abs(cmds.getAttr(axis + ".outValue") - 1.0) < 1e-9, "rotation did not drive outValue"
print("rotation OK")

# limit을 얹으면 클램프된다. 축 보정 기본값은 Y이므로 Y 리밋을 건다.
lim = cmds.createNode("maroLimit", name="lim1")
cmds.setAttr(lim + ".enableY", True)
cmds.setAttr(lim + ".minY", -0.5)
cmds.setAttr(lim + ".maxY", 0.5)
cmds.connectAttr(lim + ".capabilityOut", axis + ".capabilityIn[1]")

assert abs(cmds.getAttr(axis + ".outValue") - 0.5) < 1e-9, "limit did not clamp"
print("limit OK")

# 두 번째 limit이 더 좁으면 그쪽이 이긴다 (순차 클램프).
lim2 = cmds.createNode("maroLimit", name="lim2")
cmds.setAttr(lim2 + ".enableY", True)
cmds.setAttr(lim2 + ".minY", -0.25)
cmds.setAttr(lim2 + ".maxY", 0.25)
cmds.connectAttr(lim2 + ".capabilityOut", axis + ".capabilityIn[2]")

assert abs(cmds.getAttr(axis + ".outValue") - 0.25) < 1e-9, "second limit did not clamp"
print("stacked limits OK")

# 비활성 축은 구동값을 내지 않는다.
cmds.setAttr(axis + ".enabled", False)
assert abs(cmds.getAttr(axis + ".outValue")) < 1e-9, "disabled axis must output zero"
print("disabled OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
