"""Maya 안의 축 상태가 ROS 2 토픽으로 실제로 나가는지 확인한다.

별도 프로세스의 C++ 피어(tests/peer/maro_test_peer.cpp)가 구독해 값을 받아야
통과한다. 이 환경에는 ros2 CLI도 rclpy도 없어(rclpy는 init()에서 세그폴트)
피어가 유일한 ROS 2 상대역이다.

브리프의 초안 대비 두 가지 의도적 편차 (모두 앞선 세션에서 확인됨):

1. 상단에서 cmds.currentUnit(angle="rad")를 고정한다. maroRotation.angle과
   maroAxis.outValue는 둘 다 MFnUnitAttribute::kAngle이라, cmds.setAttr/getAttr이
   Maya의 UI 각도 단위(기본 도)로 값을 여닫는다. 고정하지 않으면 이 스크립트가
   "도"로 값을 쓰고, MaroPump는 라디안으로 발행하므로 피어가 받은 값과 이 스크립트가
   기대하는 값이 어긋난다 (test_capability_stack.py의 "단위 계약" 절이 같은 함정을
   이미 증명했다).
2. cmds.refresh(force=True) 대신 Qt 이벤트 루프를 직접 돌린다
   (test_bridge_pump.py에서 이미 증명됨). mayapy는 QGuiApplication은 만들어 두지만
   아무도 그 이벤트 루프를 돌리지 않고, MTimerMessage(펌프)와
   MPxThreadedDeviceNode 둘 다 그 위에 얹혀 있다. cmds.refresh()는 mayapy 아래서
   관측상 완전한 no-op이라(콜백 0회) 그걸로 펌프를 기다리면 조용히 타임아웃만 난다.
"""
import os
import subprocess
import sys
import time

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

# 편차 2: Qt 이벤트 루프를 직접 돌려 MTimerMessage 펌프를 깨운다.
from PySide6.QtWidgets import QApplication  # noqa: E402

_qapp = QApplication.instance()


def _pump(seconds):
    """Qt 이벤트 루프를 짧게 돌려 타이머/유휴 콜백이 실제로 실행되게 한다."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        _qapp.processEvents()
        time.sleep(0.02)


plugin = os.environ["MARO_PLUGIN_PATH"]
peer = os.environ["MARO_PEER_PATH"]
name = os.path.splitext(os.path.basename(plugin))[0]

cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

# 편차 1: 이 스크립트의 모든 setAttr/getAttr 각도 값은 라디안으로 읽고 쓴다.
# MaroPump가 라디안으로 발행하므로(aOutValue.asMAngle().asRadians()), 이 스크립트가
# 쓰는 값과 피어가 받는 값을 같은 단위로 비교하려면 여기서 고정해야 한다.
#
# 반드시 file(new=True) *뒤에* 고정해야 한다 -- 실측 결과 File>New가
# currentUnit(angle=...)을 씬 기본값(도)으로 조용히 되돌린다(loadPlugin 전에
# 고정해도 그 뒤의 file(new=True)가 도로 덮어쓴다). test_capability_stack.py가
# 이미 이 순서(파일 초기화 -> 단위 고정)로 그 함정을 피해 갔다. 순서를
# 뒤집으면 이후의 모든 setAttr(rad로 쓴다고 믿는 값)이 실제로는 "도"로
# 해석되어, cmds.getAttr로는 자기 자신과 앞뒤가 맞아 보이지만(둘 다 도라서)
# MPlug 기반의 네이티브 읽기(MaroPump가 쓰는 것과 같은 경로, 캐노니컬
# 라디안을 정확히 돌려준다)와는 pi/180배 어긋난다 -- 실제로 이 스크립트를
# 작성하며 그 어긋남을 직접 재현하고서 순서를 고쳤다.
cmds.currentUnit(angle="rad")

cube = cmds.polyCube(name="seg")[0]
axis = cmds.createNode("maroAxis", name="axisPub")
cmds.maroBindAxis(axis, cube)
cmds.setAttr(axis + ".jointName", "axisPub", type="string")
rot = cmds.createNode("maroRotation")
cmds.connectAttr(rot + ".capabilityOut", axis + ".capabilityIn[0]")
cmds.setAttr(rot + ".angle", 0.75)  # currentUnit이 rad이므로 이 0.75는 라디안이다.

# 피어가 받아야 할 값을 미리 로컬로도 확인해 둔다 -- outValue가 0.75가 아니면
# 발행 파이프라인이 아니라 캡ability 스택 자체가 문제라는 뜻이라 바로 갈린다.
outVal = cmds.getAttr(axis + ".outValue")
assert abs(outVal - 0.75) < 1e-9, \
    f"capability stack did not drive outValue to 0.75 rad before the bridge even starts (got {outVal})"

# 피어가 받아야 할 정확한 문자열. maro_test_peer의 runEcho()가
# "joint <name> = <position>"로 std::cout에 찍는다 (tests/peer/maro_test_peer.cpp).
EXPECTED_JOINT = "axisPub"
EXPECTED_VALUE = 0.75
PEER_TIMEOUT_SEC = 20  # 피어 자체 타임아웃 (echo 모드 4번째 인자)
BACKSTOP_SEC = PEER_TIMEOUT_SEC + 10  # 파이썬 쪽 안전망. 피어 타임아웃보다 넉넉히 크다.

# M18(이전 태스크들에서 세 번 겪은 사고)과 동일한 이유로 라이브 구간을
# try/finally로 감싼다: 여기서 assert가 터지면 브리지(백그라운드 rclcpp 스레드)와
# 구독 중인 피어 서브프로세스가 둘 다 살아남아, taskkill /F에도 안 죽고 빌드
# DLL을 잠그는 mayapy.exe 좀비를 만든다. finally에서 브리지 정지 + 플러그인
# 언로드 + 피어 프로세스 종료를 전부 멱등하게 보장한다.
listener = None
try:
    cmds.maroStartBridge("maro")

    listener = subprocess.Popen(
        [peer, "echo", "maro", "1", str(PEER_TIMEOUT_SEC)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    # 고정 시간 sleep으로 "됐겠지" 하지 않는다. maroBridgeStats()로 실제 흐름을
    # 관측하면서 Qt 이벤트 루프를 계속 돌리고, 피어가 스스로 끝내거나(성공/자체
    # 타임아웃) 우리 쪽 안전망 데드라인이 오면 멈춘다.
    deadline = time.time() + BACKSTOP_SEC
    collected = drained = applied = ticks = 0
    while time.time() < deadline and listener.poll() is None:
        _qapp.processEvents()
        time.sleep(0.05)
        collected, drained, applied, ticks = cmds.maroBridgeStats()

    stats = (collected, drained, applied, ticks)

    if listener.poll() is None:
        # 안전망 데드라인에 걸렸는데도 피어가 아직 살아있다 -- 정상 경로라면
        # 피어 자신의 타임아웃(PEER_TIMEOUT_SEC)이 먼저 왔어야 한다. 강제 종료하고
        # 실패로 취급한다.
        listener.terminate()

    out, _ = listener.communicate(timeout=10)
    print(out)

    # 실패 시 어느 단계가 문제였는지 바로 보이도록 카운터를 함께 남긴다.
    # collected가 0이면 펌프가 축을 못 찾은 것, drained가 0이면 스레드 경계
    # 문제, 둘 다 0이 아닌데 피어가 못 받으면 토픽 이름이나 DDS 문제다.
    assert listener.returncode == 0, \
        f"peer never received joint_states (timed out); maroBridgeStats={stats}\n" \
        f"peer output:\n{out}"
    assert f"joint {EXPECTED_JOINT} = " in out, \
        f"joint name '{EXPECTED_JOINT}' missing from published message:\n{out}\n" \
        f"maroBridgeStats={stats}"
    # 값 자체를 검증한다 -- 이름만 맞고 값이 틀리면(예: 발행 경로가 항상 0을
    # 내보내거나 각도를 라디안이 아니라 도로 실어 보내면) "피어가 뭔가는 받았다"만
    # 확인하는 테스트는 통과해 버린다. 발행은 라디안으로 나가므로 여기서 기대하는
    # 값도 라디안(0.75)이어야 한다.
    published = None
    for line in out.splitlines():
        prefix = f"joint {EXPECTED_JOINT} = "
        if line.startswith(prefix):
            published = float(line[len(prefix):].strip())
            break
    assert published is not None, \
        f"could not parse published value for '{EXPECTED_JOINT}' from peer output:\n{out}"
    assert abs(published - EXPECTED_VALUE) < 1e-6, \
        f"joint value published as {published}, expected {EXPECTED_VALUE} rad " \
        f"(0.75 rad in -> {EXPECTED_VALUE} rad out, no unit conversion in between):\n{out}\n" \
        f"maroBridgeStats={stats}"
    print(f"publish round trip OK (joint={EXPECTED_JOINT}, value={published})")

    cmds.maroStopBridge()
    cmds.file(new=True, force=True)
    cmds.unloadPlugin(name)
    print("unload OK")
finally:
    # 안전망: 피어가 아직 떠 있으면 죽인다. 남겨두면 DDS 리소스를 붙든 채
    # 다음 테스트/빌드를 방해할 수 있다.
    if listener is not None and listener.poll() is None:
        listener.terminate()
        try:
            listener.wait(timeout=5)
        except subprocess.TimeoutExpired:
            listener.kill()
            listener.wait(timeout=5)

    # 안전망: 위에서 이미 성공적으로 unloadPlugin까지 끝났다면 플러그인은
    # 이미 내려가 있으니 아무 것도 하지 않는다. 어딘가에서 assert가 터졌다면
    # 브리지를 내리고(멱등: 이미 꺼져 있어도 안전하다) 씬을 비운 뒤(남은
    # 노드 인스턴스가 있으면 unloadPlugin이 거부된다) 플러그인을 내린다.
    if cmds.pluginInfo(name, query=True, loaded=True):
        cmds.maroStopBridge()
        cmds.file(new=True, force=True)
        cmds.unloadPlugin(name)

maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
