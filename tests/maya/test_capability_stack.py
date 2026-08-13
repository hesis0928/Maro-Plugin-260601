"""스택 합성: rotation이 값을 만들고 limit들이 순차적으로 클램프한다."""
import math
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

# maroRotation.angle, maroLimit's min/max, and maroAxis.outValue are all
# MFnUnitAttribute::kAngle now, so cmds.setAttr/getAttr read and write them
# in Maya's *current UI angle unit* (degrees by default), not raw radians.
# Every literal below was written in radians (matching the compute()
# internals and the pre-conversion plain-double behavior), so pin the
# session's working angle unit to radians once, up front, instead of
# converting at every call site. cmds.currentUnit() is the only per-call
# unit-control surface cmds exposes for angle attributes in this Maya
# version -- setAttr's `type="doubleAngle"` is rejected outright ("not the
# name of a recognized type"). The unit contract test near the end of this
# file flips currentUnit to degrees deliberately, to prove the UI-facing
# conversion itself; everywhere else here stays in radians.
cmds.currentUnit(angle="rad")

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

# 스택은 노드 종류가 아니라 인덱스 순서로 평가된다.
# 리밋을 회전보다 낮은 인덱스에 두면, 리밋이 먼저 돌아 0을 클램프한 뒤
# 회전이 그 값을 덮어쓴다. 따라서 최종값은 클램프되지 않은 각도여야 한다.
# capType 별로 묶어 평가하는 구현이라면 리밋이 나중에 걸려 이 단언이 깨진다.
axisOrder = cmds.createNode("maroAxis", name="axisOrder")
limFirst = cmds.createNode("maroLimit", name="limFirst")
rotSecond = cmds.createNode("maroRotation", name="rotSecond")

cmds.setAttr(limFirst + ".enableY", True)
cmds.setAttr(limFirst + ".minY", -0.1)
cmds.setAttr(limFirst + ".maxY", 0.1)

cmds.connectAttr(limFirst + ".capabilityOut", axisOrder + ".capabilityIn[0]")
cmds.connectAttr(rotSecond + ".capabilityOut", axisOrder + ".capabilityIn[1]")
cmds.setAttr(rotSecond + ".angle", 1.0)

ordered = cmds.getAttr(axisOrder + ".outValue")
assert abs(ordered - 1.0) < 1e-9, \
    f"stack must evaluate in index order, not grouped by capType (got {ordered})"
print("index ordering OK")

# controlMode 가 ROS 면 기준값이 rotation 이 아니라 rosCommand 에서 온다.
# 리밋은 두 모드 모두에 걸린다.
axisRos = cmds.createNode("maroAxis", name="axisRos")
rotRos = cmds.createNode("maroRotation", name="rotRos")
cmds.connectAttr(rotRos + ".capabilityOut", axisRos + ".capabilityIn[0]")
cmds.setAttr(rotRos + ".angle", 0.3)
cmds.setAttr(axisRos + ".rosCommand", 0.9)

assert abs(cmds.getAttr(axisRos + ".outValue") - 0.3) < 1e-9, \
    "Manual mode must use the rotation node, not rosCommand"

cmds.setAttr(axisRos + ".controlMode", 1)
assert abs(cmds.getAttr(axisRos + ".outValue") - 0.9) < 1e-9, \
    "ROS mode must use rosCommand, not the rotation node"
print("control mode source OK")

limRos = cmds.createNode("maroLimit", name="limRos")
cmds.setAttr(limRos + ".enableY", True)
cmds.setAttr(limRos + ".minY", -0.5)
cmds.setAttr(limRos + ".maxY", 0.5)
cmds.connectAttr(limRos + ".capabilityOut", axisRos + ".capabilityIn[1]")

assert abs(cmds.getAttr(axisRos + ".outValue") - 0.5) < 1e-9, \
    "limits must clamp a ROS-driven value too"
print("limit clamps in ros mode OK")

# 센서 노드는 구동값에 기여하지 않지만, 등록되고 올바른 capType 을 실어야 한다.
axisSensor = cmds.createNode("maroAxis", name="axisSensor")
rotSensor = cmds.createNode("maroRotation", name="rotSensor")
sensorDir = cmds.createNode("maroSensorDirection", name="sensorDir")
sensorRange = cmds.createNode("maroSensorRange", name="sensorRange")

cmds.connectAttr(rotSensor + ".capabilityOut", axisSensor + ".capabilityIn[0]")
cmds.connectAttr(sensorDir + ".capabilityOut", axisSensor + ".capabilityIn[1]")
cmds.connectAttr(sensorRange + ".capabilityOut", axisSensor + ".capabilityIn[2]")
cmds.setAttr(rotSensor + ".angle", 0.6)

assert abs(cmds.getAttr(axisSensor + ".outValue") - 0.6) < 1e-9, \
    "sensor capabilities must not alter the driving value"
assert cmds.getAttr(sensorDir + ".capabilityOut.capType") == 2, "sensorDirection capType"
assert cmds.getAttr(sensorRange + ".capabilityOut.capType") == 3, "sensorRange capType"
print("sensor nodes OK")

# 비활성 축은 구동값을 내지 않는다.
cmds.setAttr(axis + ".enabled", False)
assert abs(cmds.getAttr(axis + ".outValue")) < 1e-9, "disabled axis must output zero"
print("disabled OK")

# 단위 계약: MFnUnitAttribute는 데이터블록(항상 라디안)과 cmds/Attribute
# Editor 표면(현재 UI 각도 단위, 기본 도) 사이를 변환한다. 그 변환이 실제로
# 걸려 있는지 끝까지 증명한다 -- rotation을 180 "도"로 설정하고 axis의
# outValue를 "라디안"으로 읽어 pi가 나오는지 확인한다. cmds.currentUnit()로
# 각 cmds.setAttr/getAttr 호출이 어느 단위로 말하는지 명시적으로 통제한다.
axisUnit = cmds.createNode("maroAxis", name="axisUnitContract")
rotUnit = cmds.createNode("maroRotation", name="rotUnitContract")
cmds.connectAttr(rotUnit + ".capabilityOut", axisUnit + ".capabilityIn[0]")

cmds.currentUnit(angle="deg")
cmds.setAttr(rotUnit + ".angle", 180.0)

cmds.currentUnit(angle="rad")
outRad = cmds.getAttr(axisUnit + ".outValue")
assert abs(outRad - math.pi) < 1e-9, \
    f"180 degrees in must read back as pi radians out (got {outRad})"
print("unit contract (180 deg in -> pi rad out) OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
