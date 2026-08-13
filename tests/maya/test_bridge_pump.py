"""브리지를 켜면 두 방향 모두 실제로 살아 도는지, 그리고 내린 뒤
프로세스가 깨끗이 끝나는지 확인한다.

- 발행 방향(DG -> 큐 -> MaroRosRuntime 스레드): collected/drained 카운터로
  증명한다 (Task 10 이전과 동일한 메커니즘).
- 수신 방향(ROS 2 -> MaroCommandDeviceNode 스레드): 아직 진짜 ROS 2 피어가
  없다 (피어는 Task 11에서 생긴다). 그래서 "명령이 적용됐는지"
  (appliedCommands)가 아니라 그 스레드가 살아서 스핀하는지
  (commandThreadTicks)를 증명한다. 실제 명령 왕복 증명은 Task 12의 계약
  테스트가 맡는다.

§12에서 퍼블리셔 누수로 프로세스가 종료되지 않는 결함이 실제로 있었다.
아래 "unload while running" 시나리오가 그 회귀를 막는다 — 이번엔 스레드가
두 개(발행 스레드 + Maya가 관리하는 커맨드 디바이스 스레드)라서 순서
실수의 여지도 늘었다.
"""
import os
import sys
import time

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

# mayapy는 뷰포트가 없는 헤드리스 세션이다(about(batch=True)==True,
# mayaState==kLibraryApp). MTimerMessage와 MPxThreadedDeviceNode의 내부
# 스레드/컴퓨트 갱신은 둘 다 Qt 이벤트 루프가 도는 것에 얹혀 있는데,
# mayapy는 QGuiApplication 인스턴스는 만들어 두지만 그 이벤트 루프를 아무도
# 돌리지 않는다. cmds.refresh(force=True)와 maya.utils.processIdleEvents()
# 둘 다 실측 결과 이 큐를 건드리지 못했다(둘 다 0회 콜백). 대화형 Maya는
# 자체 이벤트 루프가 항상 돌고 있어 이 처리가 필요 없다 -- 이건 순전히
# 헤드리스 테스트 하네스를 위한 우회다.
from PySide6.QtWidgets import QApplication  # noqa: E402

_qapp = QApplication.instance()


def _pump(seconds):
    """Qt 이벤트 루프를 짧게 돌려 타이머/유휴 콜백이 실제로 실행되게 한다."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        _qapp.processEvents()
        time.sleep(0.02)

plugin = os.environ["MARO_PLUGIN_PATH"]
name = os.path.splitext(os.path.basename(plugin))[0]

cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

# 이름 있는 축 하나를 세운다. 이름이 없으면 펌프가 건너뛴다.
cube = cmds.polyCube(name="seg")[0]
axis = cmds.createNode("maroAxis", name="axisP")
cmds.maroBindAxis(axis, cube)
cmds.setAttr(axis + ".jointName", "axisP", type="string")
rot = cmds.createNode("maroRotation")
cmds.connectAttr(rot + ".capabilityOut", axis + ".capabilityIn[0]")
cmds.setAttr(rot + ".angle", 0.4)

# 브리지를 켜기 전에는 아무것도 흐르지 않는다.
collected, drained, applied, ticks = cmds.maroBridgeStats()
assert collected == 0 and drained == 0 and ticks == 0, \
    f"nothing should flow before the bridge starts: stats={(collected, drained, applied, ticks)}"
print("idle stats OK")

cmds.maroStartBridge("maro")

# 펌프(MTimerMessage)와 커맨드 디바이스 스레드(MPxThreadedDeviceNode)는
# 둘 다 Qt 이벤트 루프 위에서 돈다. maroStartBridge()가 commandOut을 한 번
# 강제로 조회해 스레드 최초 기동은 이미 끌어냈지만(MaroCommands.cpp의
# MaroStartBridgeCommand::doIt() 참고), 이후의 타이머 틱과 반복 compute()
# 갱신은 계속 이벤트 루프가 돌아야 나온다. 시간이 아니라 관측 가능한
# 조건(카운터)을 기다리고, 데드라인은 마지막 안전망일 뿐이다.
deadline = time.time() + 20
collected = drained = applied = ticks = 0
while time.time() < deadline:
    _qapp.processEvents()
    time.sleep(0.05)
    collected, drained, applied, ticks = cmds.maroBridgeStats()
    if collected > 0 and drained > 0 and ticks > 0:
        break

stats = (collected, drained, applied, ticks)
assert collected > 0, f"pump never collected a sample from the DG (stats={stats})"
assert drained > 0, f"samples never reached the background thread (stats={stats})"
assert ticks > 0, \
    f"MaroCommandDeviceNode's thread never ticked -- native inbound thread " \
    f"did not come alive (stats={stats})"
print(f"pump + command thread flow OK (collected={collected}, drained={drained}, ticks={ticks})")

cmds.maroStopBridge()
collected_after, _, _, ticks_after = cmds.maroBridgeStats()
_pump(1.0)
collected_now, _, _, ticks_now = cmds.maroBridgeStats()
assert collected_now == collected_after, \
    f"pump kept running after maroStopBridge (before={collected_after}, after={collected_now})"
assert ticks_now == ticks_after, \
    f"command device thread kept ticking after maroStopBridge " \
    f"(before={ticks_after}, after={ticks_now})"
print("stop halts both threads OK")

# 브리지를 켠 채로 언로드해도 좀비 스레드가 남지 않아야 한다. 여기가 이
# 태스크에서 가장 중요한 시나리오다: File -> New가 커맨드 디바이스 노드를
# 지우는 경로(명시적 maroStopBridge를 거치지 않는 경로)로도 스레드가
# 안전하게 멈추는지까지 함께 검증한다.
cmds.maroStartBridge("maro")
# 노드 인스턴스가 남아 있으면 Maya가 언로드를 거부한다. 브리지는 켠 채로 둔다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(name)
print("unload while running OK")

maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
