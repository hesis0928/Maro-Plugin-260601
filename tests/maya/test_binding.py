"""바인딩 규칙: transform만 허용, 오브젝트당 축 하나, 사유 출력."""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

cube = cmds.polyCube(name="armSegment")[0]
axis = cmds.createNode("maroAxis", name="axis1")

# 1) 정상 바인딩
cmds.maroBindAxis(axis, cube)
assert cmds.isConnected(cube + ".message", axis + ".targetObject"), \
    "binding did not create a message connection"
print("bind OK")

# 1.5) undo 하면 실제 바인딩이 사라진다. 이 단계에서는 undo 큐의 마지막
# undoable 항목이 진짜 bind이므로, undo가 정말로 연결을 되돌리는지 확인할
# 수 있다. no-op 재바인딩(아래 4번)은 undoable이 아니라서 undo 큐에 올라가지
# 않으므로, 이 검사를 그 뒤에 두면 무엇을 되돌렸는지 불분명해진다.
cmds.undo()
assert not cmds.isConnected(cube + ".message", axis + ".targetObject"), \
    "undo did not remove the binding"
print("undo OK")

# undo로 되돌린 바인딩을 다시 걸어서 이후 검사들이 기대하는 상태로 복원한다.
cmds.maroBindAxis(axis, cube)
assert cmds.isConnected(cube + ".message", axis + ".targetObject"), \
    "re-binding after undo did not create a message connection"
print("rebind after undo OK")

# 2) 같은 오브젝트에 두 번째 축 -> 거부 ("축 하나 = 오브젝트 하나")
axis2 = cmds.createNode("maroAxis", name="axis2")
try:
    cmds.maroBindAxis(axis2, cube)
    raise AssertionError("second bind to the same object should have been rejected")
except RuntimeError:
    print("duplicate bind rejected OK")

# 3) transform이 아닌 대상 -> 거부
light = cmds.createNode("pointLight")   # shape 노드라 transform이 아니다
axis3 = cmds.createNode("maroAxis", name="axis3")
try:
    cmds.maroBindAxis(axis3, light)
    raise AssertionError("binding to a non-transform should have been rejected")
except RuntimeError:
    print("non-transform bind rejected OK")

# 3.5) 이미 바인딩된 축을 다른 오브젝트에 다시 바인딩 -> 거부
cubeOther = cmds.polyCube(name="armSegmentOther")[0]
try:
    cmds.maroBindAxis(axis, cubeOther)
    raise AssertionError("rebinding a bound axis to another object should be rejected")
except RuntimeError:
    print("axis rebind rejected OK")

# 4) 같은 오브젝트에 다시 바인딩하는 것은 무해한 반복이므로 성공해야 한다
cmds.maroBindAxis(axis, cube)
print("idempotent rebind OK")

# 5) 축 체인 연결과 순환 거부
cube2 = cmds.polyCube(name="armSegment2")[0]
cmds.maroBindAxis(axis2, cube2)
cmds.maroConnectAxis(axis2, axis)          # axis2 의 부모가 axis
assert cmds.isConnected(axis + ".message", axis2 + ".parentAxis")
print("chain connect OK")

try:
    cmds.maroConnectAxis(axis, axis2)      # axis 의 부모를 axis2 로 -> 순환
    raise AssertionError("cycle should have been rejected at wiring time")
except RuntimeError:
    print("cycle rejected OK")

try:
    cmds.maroConnectAxis(axis, axis)       # 자기 자신
    raise AssertionError("self-parenting should have been rejected")
except RuntimeError:
    print("self-parent rejected OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
