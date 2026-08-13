"""모드 전환 계약: Manual은 명령을 무시하고, ROS는 반영한다.

환경 제약 (반드시 읽을 것) -- mayapy에서는 인바운드 배선의 마지막 단계가
전혀 돌지 않는다:

devkit 헤더(MPxThreadedDeviceNode.h, "NOTE")는 인바운드 어트리뷰트 갱신이
Maya의 유휴(idle) 이벤트 큐에 얹혀 있고, "it will not work in Maya batch
mode"라고 명시한다. mayapy는 배치 모드다(about(batch=True) == True).

직접 확인했다(스크립트는 이 리포에 남기지 않음, 재현 방법은
task-12-report.md 참고): 브리지를 켜고 실제로 피어가 명령을 발행한 뒤,
아래 세 가지 펌핑 방식을 각각 5~8초 이상 시도해도 maroBridgeStats()의
applied(인덱스 2)는 단 한 번도 0에서 움직이지 않았다 --
  1) cmds.refresh(force=True) 반복
  2) maroCommandDevice.commandOut을 강제로 getAttr() (DG pull evaluation)
     반복 -- MaroStartBridgeCommand::doIt()가 최초 1회 하는 것과 같은 트릭.
  3) test_bridge_pump.py가 outbound(collected/drained)에는 실제로 효과가
     있음을 이미 증명한 QApplication.processEvents() 펌핑.
방식 3)은 대조군으로도 의미가 있다: 같은 실행에서 collected/drained(발행
쪽, MTimerMessage 기반)는 실제로 올라갔다 -- 그러니 이건 "펌핑이 부족해서"
가 아니라 인바운드 경로(MPxThreadedDeviceNode의 compute() 트리거)가 이
환경에서 원천적으로 막혀 있다는 뜻이다. applyToMatchingAxis()(Task 10이
이미 작성한, 구독 스레드가 받은 값을 controlMode==1인 축에만 적용하는
실제 게이트)는 compute() 안에서만 불리므로, 이 환경에서는 절대 실행되지
않는다 -- 그 게이트가 맞든 틀리든 mayapy에서는 관측할 수 없다.

그래서 이 스크립트는 다음과 같이 나눈다:
  1) 명령이 실제로 배선을 탔다는 증거로 commandThreadTicks를 쓴다 --
     "스레드가 살아서 계속 spin_some()을 돌았다"는 것만 증명하고, 이
     메시지 하나가 링버퍼까지 들어갔다는 보장은 아니다(그 증거는 이
     환경에서 얻을 수 없다). 그래도 "아무 것도 안 도는 죽은 브리지"는
     배제하므로 뒤따르는 Manual 무시 검증이 최소한 무의미하지는 않다.
  2) "ROS 모드에서 실제 전송된 명령이 반영된다"(=applied 카운터 증가)는
     이 환경에서 검증 불가능하므로 시도는 하되(예상과 다르면 실제로
     검증한다), 예상대로 안 되면 이유를 stats와 함께 출력하고 건너뛴다 --
     거짓 성공으로 위장하지 않는다.
  3) 이 태스크가 실제로 추가하는 코드(maroSetControlMode)와, 헤드리스에서
     완전히 신뢰할 수 있는 평가 경로(MaroAxisNode::compute()의 rosDriven
     분기 -- 평범한 DG pull evaluation이라 유휴 큐와 무관하다)는 전송
     구간을 건너뛰고(rosCommand를 직접 기록) 완전히 검증한다. 리밋도
     마찬가지로 ROS 소스 값에 걸리는지 확인한다.

파이프라인의 "절반"(전송 -> 스레드 수신 -> compute() -> applyToMatchingAxis)
은 이 스크립트로 증명하지 못한다 -- 대화형 Maya에서만 검증 가능하다.
"""
import os
import subprocess
import sys
import time

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
peer = os.environ["MARO_PEER_PATH"]
name = os.path.splitext(os.path.basename(plugin))[0]

cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)
# 새 파일은 단위를 리셋한다. 각도 어트리뷰트는 MFnUnitAttribute::kAngle이므로
# file(new=True) 뒤, 다른 무엇보다 먼저 라디안으로 고정한다.
cmds.currentUnit(angle="rad")

cube = cmds.polyCube(name="seg")[0]
axis = cmds.createNode("maroAxis", name="axisA")
cmds.maroBindAxis(axis, cube)
cmds.setAttr(axis + ".jointName", "axisA", type="string")

rot = cmds.createNode("maroRotation")
cmds.connectAttr(rot + ".capabilityOut", axis + ".capabilityIn[0]")
cmds.setAttr(rot + ".angle", 0.2)


def wait_until(condition, timeout):
    """조건이 참이 될 때까지 기다린다. 데드라인은 마지막 안전망일 뿐,
    시간이 아니라 조건이 통과 기준이다."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        cmds.refresh(force=True)
        if condition():
            return True
        time.sleep(0.05)
    return False


def publish_command(value):
    subprocess.run([peer, "pub", "maro", "axisA", str(value)],
                    check=True, timeout=120)


def pump_idle(seconds):
    """조건 없이 정해진 시간만 돈다. "아무 일도 안 일어남"은 조건으로
    기다릴 수 없으므로, 그 경우에만 쓴다."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        cmds.refresh(force=True)
        time.sleep(0.05)


# M18/이 프로젝트의 반복된 전례: 이 지점부터 teardown까지가 "라이브" 구간이다
# -- 브리지가 켜져 있으면 백그라운드 스레드(들)가 실제로 rclcpp 컨텍스트를
# 붙들고 돈다. try 없이 assert가 여기서 터지면 teardown(maroStopBridge/
# unloadPlugin)을 건너뛰어 taskkill /F에도 안 죽는 mayapy.exe 좀비를 만든다
# (이번 작업 중 실제로 재현했다 -- task-12-report.md 참고). 반드시 감싼다.
try:
    cmds.maroStartBridge("maro")

    # ---- 1) Manual 축은 명령이 실제로 도착해도 움직이지 않는다. ----
    assert cmds.getAttr(axis + ".controlMode") == 0, "default must be Manual"

    ticks_before = cmds.maroBridgeStats()[3]
    publish_command(1.2)
    assert wait_until(lambda: cmds.maroBridgeStats()[3] > ticks_before, timeout=5), (
        "command device thread never ticked after publish -- the receiving "
        "thread appears dead, so the Manual check below would be vacuous; "
        f"maroBridgeStats={cmds.maroBridgeStats()}"
    )
    pump_idle(1.0)  # 처리할 기회를 더 준다.

    manual_out = cmds.getAttr(axis + ".outValue")
    assert abs(manual_out - 0.2) < 1e-6, (
        f"Manual axis moved despite ignoring commands; outValue={manual_out}, "
        f"maroBridgeStats={cmds.maroBridgeStats()}"
    )
    print("manual ignores command OK")

    # ---- 2) 실제 전송을 통한 반영: 이 환경에서 원천적으로 검증 불가능할
    # 수 있다. 시도는 하되, 거짓 성공으로 위장하지 않는다. ----
    applied_before = cmds.maroBridgeStats()[2]
    ticks_before_2 = cmds.maroBridgeStats()[3]

    cmds.maroSetControlMode(axis, 1)
    assert cmds.getAttr(axis + ".controlMode") == 1, "maroSetControlMode did not flip controlMode"

    publish_command(1.2)
    delivered = wait_until(lambda: cmds.maroBridgeStats()[2] > applied_before, timeout=8)
    stats_after_attempt = cmds.maroBridgeStats()

    if delivered:
        # 이 환경에서 안 될 거라 예상했는데 실제로 됐다면 억지로 SKIP 처리
        # 하지 않고 진짜로 검증한다.
        assert wait_until(
            lambda: abs(cmds.getAttr(axis + ".outValue") - 1.2) < 1e-6, timeout=5), (
            "applied count rose but outValue never reflected the delivered "
            f"command; outValue={cmds.getAttr(axis + '.outValue')}, "
            f"maroBridgeStats={cmds.maroBridgeStats()}"
        )
        print("ros mode applies REAL delivered command OK (verified end-to-end)")
    else:
        # SKIP의 전제조건: 적어도 스레드는 이 시도 동안에도 살아 있었어야
        # 한다. 안 그러면 "환경 제약"이 아니라 그냥 브리지가 죽은 것이다.
        assert stats_after_attempt[3] > ticks_before_2, (
            "SKIP precondition failed: command device thread did not even "
            f"tick during this attempt; maroBridgeStats={stats_after_attempt}"
        )
        print(
            "SKIP: real end-to-end ROS delivery (applied counter rising) "
            "cannot be verified under mayapy -- MPxThreadedDeviceNode "
            "requires Maya's idle event queue for inbound attribute "
            "updates, and batch mode does not run it (devkit NOTE). "
            f"Confirmed live: command device thread kept ticking "
            f"(maroBridgeStats={stats_after_attempt}) but applied stayed at "
            f"{applied_before}. This half of the pipeline must be verified "
            "in interactive Maya."
        )

    # ---- 3) 게이트 뒤 평가 로직(MaroAxisNode::compute()의 rosDriven 분기)은
    # 전송 구간을 건너뛰고(rosCommand 직접 기록) 완전히 헤드리스로 검증한다.
    # 이건 평범한 DG pull evaluation이라 유휴 큐와 무관하게 배치 모드에서도
    # 완전히 신뢰할 수 있다. ----
    cmds.setAttr(axis + ".rosCommand", 1.2)
    ros_out = cmds.getAttr(axis + ".outValue")
    assert abs(ros_out - 1.2) < 1e-6, (
        f"ROS mode did not source outValue from rosCommand; outValue={ros_out}"
    )
    print("ros mode sources outValue from rosCommand OK")

    # 다시 Manual로: 명령을 통해 되돌린다. rosCommand(1.2)를 무시하고
    # rotation 노드 값(0.2)으로 돌아가야 한다.
    cmds.maroSetControlMode(axis, 0)
    assert cmds.getAttr(axis + ".controlMode") == 0
    back_out = cmds.getAttr(axis + ".outValue")
    assert abs(back_out - 0.2) < 1e-6, (
        "switching back to Manual via maroSetControlMode did not restore "
        f"the rotation-driven value (rosCommand leaked through); outValue={back_out}"
    )
    print("maroSetControlMode toggles both directions OK")

    # undo: 커맨드는 undoable로 선언되어 있다 (isUndoable() == true).
    cmds.maroSetControlMode(axis, 1)
    assert cmds.getAttr(axis + ".controlMode") == 1
    cmds.undo()
    assert cmds.getAttr(axis + ".controlMode") == 0, "undo did not revert controlMode"
    print("maroSetControlMode undo OK")

    # 이후 리밋 검증을 위해 ROS 모드로 되돌린다.
    cmds.maroSetControlMode(axis, 1)
    assert cmds.getAttr(axis + ".controlMode") == 1

    # ---- 4) 리밋은 모드와 무관하게, ROS 소스 값에도 적용된다. ----
    cmds.setAttr(axis + ".rosCommand", 1.2)
    lim = cmds.createNode("maroLimit")
    cmds.setAttr(lim + ".enableY", True)
    cmds.setAttr(lim + ".minY", -0.5)
    cmds.setAttr(lim + ".maxY", 0.5)
    cmds.connectAttr(lim + ".capabilityOut", axis + ".capabilityIn[1]")

    limited_out = cmds.getAttr(axis + ".outValue")
    assert abs(limited_out - 0.5) < 1e-6, (
        f"limit did not clamp a ROS-sourced value; outValue={limited_out}"
    )
    print("limit clamps ros value OK")

    cmds.maroStopBridge()
    print("bridge stop OK")
finally:
    # 안전망: 위에서 이미 성공적으로 maroStopBridge까지 끝났다면 멱등하게
    # 아무 일도 하지 않는다(재호출해도 안전). 실패했다면 여기서 브리지를
    # 내리고, 씬을 비운 뒤(남은 노드 인스턴스가 있으면 unloadPlugin이
    # 거부된다) 플러그인을 내린다.
    if cmds.pluginInfo(name, query=True, loaded=True):
        cmds.maroStopBridge()
        cmds.file(new=True, force=True)
        cmds.unloadPlugin(name)

maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
