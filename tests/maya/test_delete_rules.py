"""삭제 비대칭성과 고아 능력 노드 규칙."""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

# 오브젝트를 지우면 축도 사라진다.
cube = cmds.polyCube(name="seg")[0]
axis = cmds.createNode("maroAxis", name="axisA")
cmds.maroBindAxis(axis, cube)
cmds.delete(cube)
assert not cmds.objExists(axis), "axis should be deleted along with its object"
print("cascade delete OK")

# undo 하면 둘 다 돌아온다 (같은 undo 청크).
cmds.undo()
assert cmds.objExists(cube), "object should come back on undo"
assert cmds.objExists(axis), "axis should come back on undo, in the same chunk"
print("cascade undo OK")

# 축을 지워도 오브젝트는 남는다.
cmds.delete(axis)
assert cmds.objExists(cube), "deleting an axis must not delete its object"
print("asymmetry OK")

# 축을 지우면 능력 노드는 남고 고아 세트에 담긴다.
cube2 = cmds.polyCube(name="seg2")[0]
axis2 = cmds.createNode("maroAxis", name="axisB")
cmds.maroBindAxis(axis2, cube2)
rot = cmds.createNode("maroRotation", name="rotB")
cmds.connectAttr(rot + ".capabilityOut", axis2 + ".capabilityIn[0]")

cmds.delete(axis2)
assert cmds.objExists(rot), "capability node must survive axis deletion"
assert cmds.objExists("maroOrphanSet"), "orphan set should exist"
members = cmds.sets("maroOrphanSet", query=True) or []
assert rot in members, f"orphan not registered in set: {members}"
print("orphan OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
