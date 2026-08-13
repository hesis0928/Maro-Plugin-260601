"""Maya 안의 축 상태가 ROS 2 토픽으로 실제로 나가는지 확인한다.

별도 프로세스의 C++ 피어(tests/peer/maro_test_peer.cpp)가 구독해 값을 받아야
통과한다. 이 환경에는 ros2 CLI도 rclpy도 없어(rclpy는 init()에서 세그폴트)
피어가 유일한 ROS 2 상대역이다.

/joint_states뿐 아니라 /tf도 검증한다: 축이 바인딩된 오브젝트를 원점이 아닌
실제 위치/회전으로 옮겨 두고, 피어가 그 값을 그대로(mayaToRos 변환을 거쳐)
받는지까지 확인한다. AxisSample.position/rotation이 채워지지 않으면(이
태스크가 고치는 결함) /tf는 항상 원점/항등을 실어 보내므로, "피어가 뭔가는
받았다"만 보는 테스트는 그 결함을 통과시켜 버린다 -- 그래서 값 자체를
단언한다.

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
import math
import os
import subprocess
import sys
import time
import traceback

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


def _wait_for_peer(proc, backstop_sec):
    """피어가 스스로 끝내거나(성공/자체 타임아웃) 우리 쪽 안전망 데드라인이 올
    때까지 Qt 이벤트 루프를 계속 돌리며 maroBridgeStats()로 실제 흐름을
    관측한다. 고정 시간 sleep으로 "됐겠지" 하지 않는다 -- 관측 가능한 조건
    (피어의 종료)을 기다리고, 데드라인은 마지막 안전망일 뿐이다."""
    deadline = time.time() + backstop_sec
    collected = drained = applied = ticks = pub_errors = 0
    while time.time() < deadline and proc.poll() is None:
        _qapp.processEvents()
        time.sleep(0.05)
        collected, drained, applied, ticks, pub_errors = cmds.maroBridgeStats()

    if proc.poll() is None:
        # 안전망 데드라인에 걸렸는데도 피어가 아직 살아있다 -- 정상 경로라면
        # 피어 자신의 타임아웃(PEER_TIMEOUT_SEC)이 먼저 왔어야 한다. 강제
        # 종료하고 호출측이 returncode != 0으로 실패 처리하게 한다.
        proc.terminate()

    out, _ = proc.communicate(timeout=10)
    return out, (collected, drained, applied, ticks, pub_errors)


def main():
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
    # 같은 이유로 선형 단위도 고정한다: 아래에서 cube.translate에 쓰는 리터럴을
    # 이 Maya 설치본의 기본 UI 단위(인치일 수도, cm일 수도 있다)와 무관하게
    # "센티미터"로 명확히 해석시킨다. Maya의 *내부* 표현(MDagPath::inclusiveMatrix(),
    # MPlug::asDouble() -- 즉 MaroPump.cpp가 실제로 읽는 값)은 이 UI 핀과 무관하게
    # 항상 센티미터다(MDistance::internalUnit(), deprecated된 setInternalUnit()을
    # 아무도 부르지 않는 한 고정값) -- 그래서 기대값 계산에 쓰는
    # METERS_PER_MAYA_UNIT은 이 핀의 영향을 받지 않지만, cmds.setAttr이 우리
    # 리터럴을 애매하지 않게 해석하려면 이 핀이 필요하다.
    cmds.currentUnit(linear="cm")

    cube = cmds.polyCube(name="seg")[0]
    axis = cmds.createNode("maroAxis", name="axisPub")
    cmds.maroBindAxis(axis, cube)
    cmds.setAttr(axis + ".jointName", "axisPub", type="string")
    rot = cmds.createNode("maroRotation")
    cmds.connectAttr(rot + ".capabilityOut", axis + ".capabilityIn[0]")
    cmds.setAttr(rot + ".angle", 0.75)  # currentUnit이 rad이므로 이 0.75는 라디안이다.

    # --- /tf 커버리지: 바인딩된 큐브를 알려진, 자명하지 않은 월드 자세로
    # 옮긴다. 세 이동 성분을 모두 다르고 0이 아니게 고른 것은 의도적이다 --
    # 축 치환이 틀리거나(예: Maya Z -> ROS Y가 아니라 실수로 X) 부호가
    # 하나 빠져도(예: -Z가 아니라 +Z), 성분 중 하나라도 0이면 그 실수가
    # "우연히" 걸리지 않고 통과해 버릴 수 있다. 회전은 단일 축(Z)만 돌려
    # 기대 쿼터니언을 Maya의 오일러->쿼터니언 변환에 기대지 않고 손으로
    # 바로 유도할 수 있게 한다.
    MAYA_POSITION_CM = (30.0, 20.0, 10.0)  # (x, y, z), Maya 내부 단위(cm)
    MAYA_ROTATE_Z_DEG = 50.0

    cmds.setAttr(cube + ".translateX", MAYA_POSITION_CM[0])
    cmds.setAttr(cube + ".translateY", MAYA_POSITION_CM[1])
    cmds.setAttr(cube + ".translateZ", MAYA_POSITION_CM[2])
    cmds.setAttr(cube + ".rotateZ", math.radians(MAYA_ROTATE_Z_DEG))  # currentUnit=rad

    # 순수 변환 라이브러리(maro_transform/Convert.cpp)가 하는 일을 손으로 그대로
    # 재현해 기대값을 구한다: mayaToRos는 (x, y, z) -> (x, -z, y)이고 위치는
    # metersPerMayaUnit로 스케일된다. MaroPump.cpp의 currentSceneUnit()은
    # MDistance::internalUnit()(항상 센티미터)을 미터로 바꾼 값을 쓰므로 0.01이다.
    # 회전의 벡터부는 같은 축 치환을 받고 스칼라부(w)는 불변이다
    # (Convert.cpp의 mayaToRosRotation 주석 참고, det=+1인 진짜 회전이라 성립).
    METERS_PER_MAYA_UNIT = 0.01
    mx, my, mz = MAYA_POSITION_CM
    EXPECTED_TF_TRANSLATION = (
        mx * METERS_PER_MAYA_UNIT,
        -mz * METERS_PER_MAYA_UNIT,
        my * METERS_PER_MAYA_UNIT,
    )

    _half = math.radians(MAYA_ROTATE_Z_DEG) / 2.0
    # Z축 단일 회전이므로 오일러 회전 순서와 무관하게 쿼터니언은
    # (0, 0, sin(half), cos(half))이다 -- 다른 두 축 회전이 0이라 어느
    # 순서로 합성하든 결과가 Rz(theta) 하나뿐이다.
    _maya_quat = (0.0, 0.0, math.sin(_half), math.cos(_half))  # (x, y, z, w)
    EXPECTED_TF_ROTATION = (
        _maya_quat[0],
        -_maya_quat[2],
        _maya_quat[1],
        _maya_quat[3],
    )

    # 피어가 받아야 할 값을 미리 로컬로도 확인해 둔다 -- outValue가 0.75가 아니면
    # 발행 파이프라인이 아니라 캡ability 스택 자체가 문제라는 뜻이라 바로 갈린다.
    outVal = cmds.getAttr(axis + ".outValue")
    assert abs(outVal - 0.75) < 1e-9, \
        f"capability stack did not drive outValue to 0.75 rad before the bridge even starts (got {outVal})"

    # 피어가 받아야 할 정확한 문자열. maro_test_peer의 runEcho()가
    # "joint <name> = <position>"로, runTf()가 "tf <frame> translation/rotation = ..."로
    # std::cout에 찍는다 (tests/peer/maro_test_peer.cpp). /tf의 child_frame_id는
    # sample.jointName 그대로다(MaroRosRuntime::drainAndPublish), 그래서 같은
    # 이름을 둘 다에 쓴다.
    EXPECTED_JOINT = "axisPub"
    EXPECTED_VALUE = 0.75
    PEER_TIMEOUT_SEC = 20  # 피어 자체 타임아웃
    BACKSTOP_SEC = PEER_TIMEOUT_SEC + 10  # 파이썬 쪽 안전망. 피어 타임아웃보다 넉넉히 크다.

    # M18(이전 태스크들에서 세 번 겪은 사고)과 동일한 이유로 라이브 구간을
    # try/finally로 감싼다: 여기서 assert가 터지면 브리지(백그라운드 rclcpp 스레드)와
    # 구독 중인 피어 서브프로세스가 살아남아, taskkill /F에도 안 죽고 빌드
    # DLL을 잠그는 mayapy.exe 좀비를 만든다. finally에서 브리지 정지 + 플러그인
    # 언로드 + 피어 프로세스 종료를 전부 멱등하게 보장한다.
    peers = []
    try:
        cmds.maroStartBridge("maro")

        listener = subprocess.Popen(
            [peer, "echo", "maro", "1", str(PEER_TIMEOUT_SEC)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        peers.append(listener)

        out, stats = _wait_for_peer(listener, BACKSTOP_SEC)
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

        # --- /tf: 같은 라이브 브리지, 두 번째 피어. "/<robot>/joint_states"가
        # 아니라 전역 "/tf" 토픽을 구독한다(runTf()의 주석 참고 -- robotName을
        # 넣으면 아무것도 못 받는다). 큐브 자세는 브리지를 켜기 전에 이미
        # 설정해 뒀으므로, 펌프가 수집한 첫 틱부터 실제 변환값이 실려 있다.
        tf_listener = subprocess.Popen(
            [peer, "tf", EXPECTED_JOINT, str(PEER_TIMEOUT_SEC)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        peers.append(tf_listener)

        tf_out, tf_stats = _wait_for_peer(tf_listener, BACKSTOP_SEC)
        print(tf_out)

        assert tf_listener.returncode == 0, \
            f"peer never received a /tf transform for '{EXPECTED_JOINT}' (timed out); " \
            f"maroBridgeStats={tf_stats}\npeer output:\n{tf_out}"

        translation = rotation = None
        t_prefix = f"tf {EXPECTED_JOINT} translation = "
        r_prefix = f"tf {EXPECTED_JOINT} rotation = "
        for line in tf_out.splitlines():
            if line.startswith(t_prefix):
                translation = tuple(float(v) for v in line[len(t_prefix):].split())
            elif line.startswith(r_prefix):
                rotation = tuple(float(v) for v in line[len(r_prefix):].split())
        assert translation is not None and rotation is not None, \
            f"could not parse /tf translation/rotation for '{EXPECTED_JOINT}' from peer output:\n" \
            f"{tf_out}\nmaroBridgeStats={tf_stats}"

        # 값 자체를 검증한다 -- child_frame_id만 맞고 값이 원점/항등이면(즉
        # AxisSample.position/rotation이 채워지지 않은 채 기본값이 새어나가면)
        # "피어가 뭔가는 받았다"만 확인하는 테스트는 통과해 버린다. 이게 바로
        # 이 태스크가 고치는 결함(/tf가 항상 identity를 발행하던 것)이다.
        for axis_label, got, expected in zip("xyz", translation, EXPECTED_TF_TRANSLATION):
            assert abs(got - expected) < 1e-6, \
                f"/tf translation.{axis_label} published as {got}, expected {expected} " \
                f"(Maya {MAYA_POSITION_CM} cm -> mayaToRosPosition -> {EXPECTED_TF_TRANSLATION} m):\n" \
                f"{tf_out}\nmaroBridgeStats={tf_stats}"
        for comp_label, got, expected in zip("xyzw", rotation, EXPECTED_TF_ROTATION):
            assert abs(got - expected) < 1e-6, \
                f"/tf rotation.{comp_label} published as {got}, expected {expected} " \
                f"(Maya rotateZ={MAYA_ROTATE_Z_DEG} deg -> mayaToRosRotation -> {EXPECTED_TF_ROTATION}):\n" \
                f"{tf_out}\nmaroBridgeStats={tf_stats}"
        print(f"tf round trip OK (child_frame_id={EXPECTED_JOINT}, "
              f"translation={translation}, rotation={rotation})")

        cmds.maroStopBridge()
        cmds.file(new=True, force=True)
        cmds.unloadPlugin(name)
        print("unload OK")
    finally:
        # 안전망: 아직 떠 있는 피어가 있으면 죽인다. 남겨두면 DDS 리소스를 붙든 채
        # 다음 테스트/빌드를 방해할 수 있다.
        for proc in peers:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)

        # 안전망: 위에서 이미 성공적으로 unloadPlugin까지 끝났다면 플러그인은
        # 이미 내려가 있으니 아무 것도 하지 않는다. 어딘가에서 assert가 터졌다면
        # 브리지를 내리고(멱등: 이미 꺼져 있어도 안전하다) 씬을 비운 뒤(남은
        # 노드 인스턴스가 있으면 unloadPlugin이 거부된다) 플러그인을 내린다.
        if cmds.pluginInfo(name, query=True, loaded=True):
            cmds.maroStopBridge()
            cmds.file(new=True, force=True)
            cmds.unloadPlugin(name)


# main() 안에서 assert가 터지면(어느 단계든) 예외가 여기까지 올라온다.
# 예전 버전은 uninitialize()/teardown 출력/sys.exit(0)을 try/finally *뒤에* 두어,
# 실패한 실행에서는 셋 다 건너뛰고 처리되지 않은 예외의 기본 트레이스백과
# 종료 코드에만 의존했다. mayapy 프로세스 정리(uninitialize)를 성공/실패
# 양쪽에서 똑같이 보장하도록, 나머지 라이브 구간과 같은 정리 규율 아래
# 두었다: 예외를 여기서 잡아 트레이스백은 그대로 보존해 찍고, 항상
# uninitialize() + "teardown OK"를 실행한 뒤, 성공/실패에 맞는 종료 코드로
# 딱 한 번 sys.exit()한다.
_exit_code = 0
try:
    main()
except Exception:
    _exit_code = 1
    traceback.print_exc()
finally:
    maya.standalone.uninitialize()
    print("teardown OK")

sys.exit(_exit_code)
