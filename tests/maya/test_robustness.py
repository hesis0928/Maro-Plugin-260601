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
#
# 이 시나리오가 지금 통과하는 이유: aParentAxis는 message 타입 어트리뷰트라
# aOutValue/aOutTransform으로 가는 attributeAffects가 걸려 있지 않다 (확인:
# src/maro_plugin/MaroAxisNode.cpp, MaroAxisNode::initialize() -- attributeAffects를
# 거는 for 루프(aConventionAxis/aConventionInvert/aEnabled/aControlMode/
# aRosCommand)와 aCapabilityIn 두 줄이 전부이고, aParentAxis는 그 목록에
# 없다). 그래서 위에서 만든 순환 연결은 DG 그래프상으로는 존재해도 outValue를
# 계산하는 평가 경로에는 들어가지 않아 evaluate가 멈추지 않는다.
#
# 이 가정이 깨지는 시점: 나중에 축 체인 평가(예: 로봇 관절 체인 합성)를 위해
# attributeAffects(aParentAxis, aOutValue)를 추가하는 순간, 여기서 만든 순환은
# 진짜 평가 순환이 되어 이 시나리오는 더 이상 안전을 증명하지 못한다 --
# MaroAxisNode.cpp의 attributeAffects 목록에 aParentAxis를 추가하는 사람은
# 이 시나리오도 함께 재검토해야 한다.
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

# 4) 고아 능력 노드가 실제로 maroOrphanSet에 등록됐는지 먼저 확인한다 (axis가
# 시나리오 3에서 캐스케이드 삭제될 때 rot는 지워지지 않고 고아 세트에 담겨야
# 한다 -- MaroDeleteWatcher::onAxisAboutToDelete가 예약하는
# commandToExecute("sets -edit -addElement ...")가 조용히 실패해도 rot 자체는
# 평범한 DG 노드로 살아남으므로, 세트 멤버십을 직접 조회하지 않으면 이
# 시나리오는 이름과 달리 "고아 등록"이 아니라 "살아있는 노드 재연결"만
# 검증하게 된다). 그 다음 다른 축에 재연결해도 정상 동작하는지 검증한다.
assert cmds.objExists("maroOrphanSet"), "orphan set should exist"
orphan_members = cmds.sets("maroOrphanSet", query=True) or []
assert rot in orphan_members, f"orphan not registered in set: {orphan_members}"

axis2 = cmds.createNode("maroAxis", name="axR2")
cmds.connectAttr(rot + ".capabilityOut", axis2 + ".capabilityIn[0]")
cmds.setAttr(rot + ".angle", 0.3)
assert abs(cmds.getAttr(axis2 + ".outValue") - 0.3) < 1e-9
print("orphan reuse OK")

# 5) 능력 노드 없는 축을 평가 -> 0
#
# 근처의 순환/유한성 시나리오와 달리 이 시나리오는 원래 "일부러 깨뜨려서
# 실패시켜 봤는가"가 검증된 적이 없었다. compute()의 스택 루프는 0개 원소에
# 대해 그냥 아무것도 안 하고 넘어가므로 언뜻 자명해 보일 수 있다 -- 하지만
# 실제로 깨뜨려서 확인했다: MaroAxisNode.cpp의 compute()에서 결과값의 시드인
# `double value = 0.0;`을 `1.0`으로 바꾸고 재빌드했더니 바로 이 assert가
# 실패했다 (다른 시나리오들은 전부 그대로 통과 -- 자기 능력 노드가 실제
# 값을 덮어써서 시드가 무엇이든 상관없기 때문). 즉 이 시나리오는 빈 스택일
# 때 결과값의 기본 시드가 0이어야 한다는 것을 지키는, 자명하지 않은 검사다.
bare = cmds.createNode("maroAxis", name="axBare")
assert abs(cmds.getAttr(bare + ".outValue")) < 1e-9
print("empty stack OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("all robustness scenarios survived")
sys.exit(0)
