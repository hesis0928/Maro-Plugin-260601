"""크래시를 유발할 수 있는 조작들이 규칙대로 거부·비활성화되는지 확인한다.

이 스크립트가 끝까지 도달하고 종료 코드 0으로 끝나면 크래시가 없었다는 뜻이다.

브리지(maroStartBridge)는 켜지 않는다 -- 이 시나리오들은 전부 순수 DG
조작/평가라서 필요 없고, 켜지 않으므로 백그라운드 스레드가 생기지 않아
다른 라이브 테스트들과 달리 try/finally로 감쌀 대상도 없다(비교:
test_bridge_pump.py, test_contract.py, test_publish.py).
"""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)
# 새 파일은 단위를 도(degree)로 되돌린다. 어트리뷰트는 라디안으로 저장되고
# 이 스크립트의 리터럴도 라디안 기준이므로, 다른 무엇보다 먼저 고정한다.
cmds.currentUnit(angle="rad")

# 1) 순환 연결은 반드시 거부되어야 한다 (스펙 §9 원칙 2).
a = cmds.createNode("maroAxis", name="axC1")
b = cmds.createNode("maroAxis", name="axC2")
c = cmds.createNode("maroAxis", name="axC3")

cmds.maroConnectAxis(b, a)      # b 의 부모 = a
cmds.maroConnectAxis(c, b)      # c 의 부모 = b

try:
    cmds.maroConnectAxis(a, c)  # a 의 부모 = c -> 3단계 순환
    raise AssertionError("multi-hop cycle must be rejected at wiring time")
except RuntimeError:
    print("multi-hop cycle rejected OK")

# 커맨드를 거치지 않고 직접 이어 순환을 만든 경우에도 평가가 멈추면 안 된다.
cmds.connectAttr(c + ".message", a + ".parentAxis", force=True)
cmds.getAttr(a + ".outValue")
cmds.getAttr(b + ".outValue")
cmds.getAttr(c + ".outValue")
print("raw cycle evaluation survived OK")

# 2) NaN/inf 주입 -> 축이 유한값을 유지한다.
cube = cmds.polyCube(name="segR")[0]
axis = cmds.createNode("maroAxis", name="axR")
cmds.maroBindAxis(axis, cube)
rot = cmds.createNode("maroRotation")
cmds.connectAttr(rot + ".capabilityOut", axis + ".capabilityIn[0]")

cmds.setAttr(rot + ".angle", float("inf"))
value = cmds.getAttr(axis + ".outValue")
assert value == value, "outValue became NaN"
assert abs(value) < 1e308, "outValue became infinite"
print("non-finite input contained OK")

# 3) 바인딩 대상이 사라진 뒤 평가 -> 축도 사라졌으므로 접근이 안전해야 한다.
cmds.delete(cube)
assert not cmds.objExists(axis)
print("delete during live graph OK")

# 4) 고아 능력 노드를 다른 축에 재연결 -> 정상 동작
axis2 = cmds.createNode("maroAxis", name="axR2")
cmds.connectAttr(rot + ".capabilityOut", axis2 + ".capabilityIn[0]")
cmds.setAttr(rot + ".angle", 0.3)
assert abs(cmds.getAttr(axis2 + ".outValue") - 0.3) < 1e-9
print("orphan reuse OK")

# 5) 능력 노드 없는 축을 평가 -> 0
bare = cmds.createNode("maroAxis", name="axBare")
assert abs(cmds.getAttr(bare + ".outValue")) < 1e-9
print("empty stack OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("all robustness scenarios survived")
sys.exit(0)
