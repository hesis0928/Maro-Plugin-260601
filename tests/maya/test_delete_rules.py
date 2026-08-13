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

# 오브젝트를 지우면 축이 캐스케이드되고, 축에 물려 있던 능력 노드도
# 고아 세트에 담긴다.
cube3 = cmds.polyCube(name="seg3")[0]
axis3 = cmds.createNode("maroAxis", name="axisC")
cmds.maroBindAxis(axis3, cube3)
rot3 = cmds.createNode("maroRotation", name="rotC")
cmds.connectAttr(rot3 + ".capabilityOut", axis3 + ".capabilityIn[0]")

cmds.delete(cube3)
assert not cmds.objExists(axis3), "axis should cascade-delete with its object"
assert cmds.objExists(rot3), "capability node must survive the cascade"
members = cmds.sets("maroOrphanSet", query=True) or []
assert rot3 in members, f"cascaded axis's capability node not orphaned: {members}"
print("cascade plus orphan OK")

# undo 하면 능력 노드는 복원된 축 스택에만 있어야 하고, 고아 세트에는
# 더 이상 남아 있으면 안 된다. maroOrphanSet이 이미 존재하는 상태에서
# 검증해야 한다 (세트 생성 자체가 undo로 함께 사라지면 버그가 가려진다).
assert cmds.objExists("maroOrphanSet"), "orphan set must already exist for this check"
cmds.undo()
assert cmds.objExists(axis3), "axis should come back on undo"
assert cmds.objExists(cube3), "object should come back on undo"
stack_sources = cmds.listConnections(axis3 + ".capabilityIn[0]", source=True) or []
assert rot3 in stack_sources, "capability node should be back in the restored axis stack"
members = cmds.sets("maroOrphanSet", query=True) or []
assert rot3 not in members, (
    f"capability node still listed in maroOrphanSet after undo restored it "
    f"to the axis stack: {members}"
)
print("undo restores orphan state OK")

# 캐스케이드 삭제 후에는 빈 트랜스폼이 남지 않아야 한다.
cube4 = cmds.polyCube(name="seg4")[0]
axis4 = cmds.createNode("maroAxis", name="axisD")
axis4_parent = cmds.listRelatives(axis4, parent=True, fullPath=True)[0]
cmds.maroBindAxis(axis4, cube4)
cmds.delete(cube4)
assert not cmds.objExists(axis4), "axis should cascade-delete with its object"
assert not cmds.objExists(axis4_parent), (
    "empty parent transform should be deleted along with the axis shape"
)
print("no stray transform OK")

# 부모 트랜스폼에 축 셰이프 말고 다른 자식이 남아 있으면, 캐스케이드 삭제가
# 그 트랜스폼까지 지우면 안 된다 (다른 오브젝트가 함께 딸려 사라지면 안 됨).
cube5 = cmds.polyCube(name="seg5")[0]
axis5 = cmds.createNode("maroAxis", name="axisE")
axis5_parent = cmds.listRelatives(axis5, parent=True, fullPath=True)[0]
sibling = cmds.polyCube(name="seg5Sibling")[0]
sibling = cmds.parent(sibling, axis5_parent)[0]
cmds.maroBindAxis(axis5, cube5)
cmds.delete(cube5)
assert not cmds.objExists(axis5), "axis should still cascade-delete with its object"
assert cmds.objExists(axis5_parent), (
    "parent transform with another child must survive the cascade delete"
)
assert cmds.objExists(sibling), (
    "the other child under the parent transform must not be swept away"
)
print("guard keeps shared parent transform OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
