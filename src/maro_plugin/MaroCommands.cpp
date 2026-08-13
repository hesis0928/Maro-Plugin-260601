#include "MaroCommands.h"

#include <chrono>
#include <memory>
#include <thread>

#include <maya/MAngle.h>
#include <maya/MArgList.h>
#include <maya/MDagPath.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MIntArray.h>
#include <maya/MObjectHandle.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MSelectionList.h>

#include "MaroAxisNode.h"
#include "MaroCommandDeviceNode.h"
#include "MaroPump.h"
#include "MaroRosRuntime.h"

namespace maro {

void* MaroBindAxisCommand::creator() {
    return new MaroBindAxisCommand();
}

MSyntax MaroBindAxisCommand::newSyntax() {
    MSyntax syntax;
    syntax.setObjectType(MSyntax::kSelectionList, 2, 2);
    return syntax;
}

MStatus MaroBindAxisCommand::doIt(const MArgList& args) {
    // 예외는 경계를 넘지 않는다. 커맨드에서 던지면 Maya가 죽는다.
    try {
        MStatus status;

        MSelectionList selection;
        for (unsigned int i = 0; i < args.length(); ++i) {
            MString name = args.asString(i, &status);
            if (!status) return status;
            if (!selection.add(name)) {
                MGlobal::displayError(
                    MString("Maro: cannot find node '") + name + "'.");
                return MS::kFailure;
            }
        }

        if (selection.length() != 2) {
            MGlobal::displayError(
                "Maro: maroBindAxis needs exactly two arguments: <axis> <transform>.");
            return MS::kFailure;
        }

        MObject axisObj;
        MObject targetObj;
        selection.getDependNode(0, axisObj);
        selection.getDependNode(1, targetObj);

        MFnDependencyNode axisFn(axisObj);
        if (axisFn.typeId() != MaroAxisNode::id) {
            MGlobal::displayError(
                MString("Maro: '") + axisFn.name() + "' is not a maroAxis node.");
            return MS::kFailure;
        }

        // 규칙: 회전 가능한 transform에만 바인딩한다.
        MDagPath targetPath;
        if (!MDagPath::getAPathTo(targetObj, targetPath) ||
            !targetPath.hasFn(MFn::kTransform)) {
            MFnDependencyNode targetFn(targetObj);
            MGlobal::displayError(
                MString("Maro: '") + targetFn.name() +
                "' is not a transform, so an axis cannot drive it. "
                "Select the transform node instead of its shape.");
            return MS::kFailure;
        }

        MFnDependencyNode targetFn(targetObj);
        MPlug axisTarget = axisFn.findPlug(MaroAxisNode::aTargetObject, false, &status);
        if (!status) return status;

        // 규칙: 축 하나는 오브젝트 하나에만 바인딩된다(양방향). 이미 다른
        // 오브젝트에 바인딩되어 있다면 거부한다. 같은 오브젝트로의 재바인딩은
        // 무해한 반복이므로 실패가 아니라 성공으로 취급한다. 이 검사를 대상
        // 쪽의 "오브젝트 하나에는 축 하나만" 검사보다 먼저 해야, 같은 축을
        // 같은 대상에 다시 바인딩할 때 그 검사에 걸리지 않는다.
        MPlugArray axisSources;
        axisTarget.connectedTo(axisSources, true, false);
        if (axisSources.length() > 0) {
            MObject boundObj = axisSources[0].node();
            if (boundObj == targetObj) {
                MGlobal::displayInfo(
                    MString("Maro: '") + axisFn.name() + "' is already bound to '" +
                    targetFn.name() + "'.");
                return redoIt();
            }

            MFnDependencyNode boundFn(boundObj);
            MGlobal::displayError(
                MString("Maro: '") + axisFn.name() + "' is already bound to '" +
                boundFn.name() + "'. Disconnect it first before binding it to '" +
                targetFn.name() + "'.");
            return MS::kFailure;
        }

        // 규칙: 오브젝트 하나에는 축 하나만.
        MPlug targetMessage = targetFn.findPlug("message", false, &status);
        if (status) {
            MPlugArray destinations;
            targetMessage.connectedTo(destinations, false, true);
            for (unsigned int i = 0; i < destinations.length(); ++i) {
                MFnDependencyNode otherFn(destinations[i].node());
                if (otherFn.typeId() == MaroAxisNode::id) {
                    MGlobal::displayError(
                        MString("Maro: '") + targetFn.name() +
                        "' is already bound to axis '" + otherFn.name() +
                        "'. One object carries exactly one axis.");
                    return MS::kFailure;
                }
            }
        }

        status = m_modifier.connect(targetMessage, axisTarget);
        if (!status) return status;

        m_stagedChange = true;
        return redoIt();
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: maroBindAxis failed: ") + e.what());
        return MS::kFailure;
    } catch (...) {
        MGlobal::displayError("Maro: maroBindAxis failed with unknown error.");
        return MS::kFailure;
    }
}

MStatus MaroBindAxisCommand::redoIt() {
    try {
        return m_modifier.doIt();
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: maroBindAxis redo failed: ") + e.what());
        return MS::kFailure;
    } catch (...) {
        MGlobal::displayError("Maro: maroBindAxis redo failed with unknown error.");
        return MS::kFailure;
    }
}

MStatus MaroBindAxisCommand::undoIt() {
    try {
        return m_modifier.undoIt();
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: maroBindAxis undo failed: ") + e.what());
        return MS::kFailure;
    } catch (...) {
        MGlobal::displayError("Maro: maroBindAxis undo failed with unknown error.");
        return MS::kFailure;
    }
}

namespace {

// child 를 parent 아래에 붙이면 순환이 생기는지 본다.
// parent 에서 조상 방향으로 거슬러 올라가다 child 를 만나면 순환이다.
bool wouldCreateCycle(const MObject& child, const MObject& parent) {
    MObject current = parent;

    // 축 개수만큼만 돌면 충분하다. 이미 순환이 있는 씬에서도 멈춘다.
    for (int guard = 0; guard < 10000; ++guard) {
        if (current == child) return true;

        MFnDependencyNode fn(current);
        MPlug parentPlug = fn.findPlug(MaroAxisNode::aParentAxis, false);

        MPlugArray sources;
        parentPlug.connectedTo(sources, true, false);
        if (sources.length() == 0) return false;

        current = sources[0].node();
    }
    return true;   // 상한에 걸렸다면 이미 순환이다
}

}  // namespace

void* MaroConnectAxisCommand::creator() {
    return new MaroConnectAxisCommand();
}

MSyntax MaroConnectAxisCommand::newSyntax() {
    MSyntax syntax;
    syntax.setObjectType(MSyntax::kSelectionList, 2, 2);
    return syntax;
}

MStatus MaroConnectAxisCommand::doIt(const MArgList& args) {
    // 예외는 경계를 넘지 않는다. 커맨드에서 던지면 Maya가 죽는다.
    try {
        MStatus status;

        MSelectionList selection;
        for (unsigned int i = 0; i < args.length(); ++i) {
            MString name = args.asString(i, &status);
            if (!status) return status;
            if (!selection.add(name)) {
                MGlobal::displayError(MString("Maro: cannot find node '") + name + "'.");
                return MS::kFailure;
            }
        }

        if (selection.length() != 2) {
            MGlobal::displayError(
                "Maro: maroConnectAxis needs exactly two arguments: <child> <parent>.");
            return MS::kFailure;
        }

        MObject childObj;
        MObject parentObj;
        selection.getDependNode(0, childObj);
        selection.getDependNode(1, parentObj);

        MFnDependencyNode childFn(childObj);
        MFnDependencyNode parentFn(parentObj);

        if (childFn.typeId() != MaroAxisNode::id ||
            parentFn.typeId() != MaroAxisNode::id) {
            MGlobal::displayError("Maro: maroConnectAxis expects two maroAxis nodes.");
            return MS::kFailure;
        }

        if (childObj == parentObj) {
            MGlobal::displayError(
                MString("Maro: '") + childFn.name() + "' cannot be its own parent.");
            return MS::kFailure;
        }

        if (wouldCreateCycle(childObj, parentObj)) {
            MGlobal::displayError(
                MString("Maro: connecting '") + childFn.name() + "' under '" +
                parentFn.name() + "' would create a cycle in the axis chain.");
            return MS::kFailure;
        }

        MPlug parentMessage = parentFn.findPlug("message", false, &status);
        if (!status) return status;
        MPlug childParent = childFn.findPlug(MaroAxisNode::aParentAxis, false, &status);
        if (!status) return status;

        // 부모는 하나뿐이다. 기존 연결이 있으면 끊고 새로 잇는다.
        MPlugArray existing;
        childParent.connectedTo(existing, true, false);
        for (unsigned int i = 0; i < existing.length(); ++i) {
            status = m_modifier.disconnect(existing[i], childParent);
            if (!status) return status;
        }

        status = m_modifier.connect(parentMessage, childParent);
        if (!status) return status;

        return redoIt();
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: maroConnectAxis failed: ") + e.what());
        return MS::kFailure;
    } catch (...) {
        MGlobal::displayError("Maro: maroConnectAxis failed with unknown error.");
        return MS::kFailure;
    }
}

MStatus MaroConnectAxisCommand::redoIt() {
    try {
        return m_modifier.doIt();
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: maroConnectAxis redo failed: ") + e.what());
        return MS::kFailure;
    } catch (...) {
        MGlobal::displayError("Maro: maroConnectAxis redo failed with unknown error.");
        return MS::kFailure;
    }
}

MStatus MaroConnectAxisCommand::undoIt() {
    try {
        return m_modifier.undoIt();
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: maroConnectAxis undo failed: ") + e.what());
        return MS::kFailure;
    } catch (...) {
        MGlobal::displayError("Maro: maroConnectAxis undo failed with unknown error.");
        return MS::kFailure;
    }
}

namespace {
std::unique_ptr<MaroRosRuntime> g_runtime;
MObjectHandle g_commandDeviceHandle;

// shutdownBridge()의 타임아웃 경로가 g_runtime을 release()해 넘겨 두는
// 자리. 절대 delete하지 않는다 -- 의도적 누수(아래 shutdownBridge() 주석
// 참고)이며, 이 포인터를 남겨 두는 이유는 오직 디버거/코드 리뷰에서 "이건
// 실수로 버려진 게 아니라 일부러 버려진 것"이라는 흔적을 남기기 위해서다.
MaroRosRuntime* g_abandonedRuntime = nullptr;
}  // namespace

void shutdownBridge() {
    MaroPump::stop();          // 아웃바운드 타이머 콜백을 먼저 뗀다.

    if (g_commandDeviceHandle.isValid()) {
        MDGModifier modifier;
        modifier.deleteNode(g_commandDeviceHandle.object());
        modifier.doIt();
    }
    g_commandDeviceHandle = MObjectHandle();

    // deleteNode()가(혹은 File -> New가) 커맨드 디바이스 노드의 백그라운드
    // 스레드 종료를 동기적으로 보장한다는 근거를 devkit 문서에서 찾지
    // 못했다 (MaroCommandDeviceNode.cpp의 설계 노트 참고). 그 스레드가
    // 아직 도는 채로 아래 g_runtime->stop()이 rclcpp::shutdown()으로 전역
    // 컨텍스트를 끊으면 크래시하거나 프로세스가 안 끝난다. 이 프로젝트는
    // "정리 호출이 전부 성공을 반환했는데도 프로세스가 끝나지 않은" 전례가
    // 있다 -- 가정하지 않고 직접 확인한다.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (MaroCommandDeviceNode::isThreadAlive() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (MaroCommandDeviceNode::isThreadAlive()) {
        MGlobal::displayError(
            "Maro: command device thread did not stop within 2s; "
            "ROS 2 shutdown may hang or crash.");

        // C2: 이 시점에 우리는 방금 "스레드가 아직 산다"는 걸 직접
        // 확인했다. 그런데도 아래로 흘러 g_runtime->stop()
        // (-> rclcpp::shutdown(), MaroRosRuntime.cpp 참고)을 부르면, 그
        // 스레드가 여전히 참조 중인 전역 rclcpp 컨텍스트를 발밑에서
        // 뽑아버리는 것이다 -- 크래시하거나 프로세스가 끝나지 않는다.
        // 이미 위에서 비정상 상태임을 사용자에게 알렸다 -- 여기서 더
        // 할 수 있는 안전한 정리는 없다.
        //
        // 재검토(Gap 1): g_runtime.reset()을 "안 부르는" 것만으로는 누수가
        // 되지 않는다. g_runtime은 std::unique_ptr이라 언젠가는 소멸자가
        // 돈다 -- 그리고 여기서 return하고 나면, 이 함수를 부른
        // uninitializePlugin()도 곧 return하고, Maya는 maro.mll에
        // FreeLibrary()를 부른다. DLL_PROCESS_DETACH가 이 번역 단위의
        // 네임스페이스 정적 변수(g_runtime)를 소멸시키는데, 그 소멸자가
        // ~MaroRosRuntime() -> stop() -> rclcpp::shutdown()을 부른다
        // (MaroRosRuntime.cpp 참고) -- 우리가 여기서 피하려던 바로 그
        // 호출이, 같은 언로드 시퀀스 안에서 몇 순간 뒤로 미뤄질 뿐이다.
        // 그러니 이건 진짜 누수가 아니라 "return으로 위장한 지연"이었다.
        //
        // 진짜로 버리려면 소유권 자체를 버려야 한다: release()는
        // unique_ptr에게서 포인터를 빼앗아 반환하고, unique_ptr 자신은
        // nullptr이 되어 스코프를 벗어나도 아무것도 delete하지 않는다.
        // 그 원시 포인터를 g_abandonedRuntime에 담아 두고 다시는 delete하지
        // 않는다 -- 그러면 ~MaroRosRuntime()이 이 인스턴스에 대해서는
        // 다시는(DLL 언로드 시점을 포함해) 불리지 않고, 안에 들어있던
        // std::thread(아직 조인 안 됨)도 소멸자를 맞지 않아 std::terminate
        // 위험도 없다. 컨텍스트 누수는 살아남지만(메모리/핸들만 낭비),
        // 살아있는 스레드 밑에서 컨텍스트를 뽑는 건 살아남지 못한다 -- 이게
        // 그 둘 중 덜 나쁜 쪽을 실제로 선택하는 코드다. 프로세스 종료 시
        // OS가 나머지를 정리한다.
        g_abandonedRuntime = g_runtime.release();
        return;
    }

    if (g_runtime) {
        g_runtime->stop();
        g_runtime.reset();
    }
}

void* MaroStartBridgeCommand::creator() { return new MaroStartBridgeCommand(); }

MSyntax MaroStartBridgeCommand::newSyntax() {
    MSyntax syntax;
    syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
    return syntax;
}

MStatus MaroStartBridgeCommand::doIt(const MArgList& args) {
    if (args.length() != 1) {
        MGlobal::displayError("Maro: maroStartBridge needs <robotName>.");
        return MS::kFailure;
    }

    MStatus status;
    const MString robotName = args.asString(0, &status);
    if (!status) return status;

    if (g_runtime && g_runtime->isRunning()) {
        MGlobal::displayWarning("Maro: bridge is already running.");
        return MS::kSuccess;
    }

    // I3.3: devkit 헤더(MPxThreadedDeviceNode.h, "NOTE" 근처)는 어트리뷰트
    // 갱신이 유휴 이벤트 큐에 얹혀 있고, 그 큐가 배치 모드에서 돌지 않는다고
    // 명시한다. 아래 강제 조회(있는 곳에 주석 참고)는 스레드를 한 번 킥할
    // 뿐 그 큐를 대신하지 못하므로, 배치/헤드리스에서는 인바운드(ROS 2 ->
    // Maya)가 절대 전달되지 않는다. 아웃바운드 발행은 MTimerMessage 기반이라
    // 이 제약과 무관하고 테스트도 헤드리스로 그걸 검증하므로, 시작을
    // 거부하지 않고 경고만 남긴다 -- 그래야 나중에 누가 처음부터 다시
    // 디버깅하지 않는다.
    if (MGlobal::mayaState() != MGlobal::kInteractive) {
        MGlobal::displayWarning(
            "Maro: Maya is not running interactively (batch/headless mode). "
            "MPxThreadedDeviceNode relies on Maya's idle event queue for "
            "attribute updates, and that queue does not run in this mode, "
            "so inbound ROS 2 commands will NOT be delivered. Outbound "
            "publishing is unaffected.");
    }

    g_runtime = std::make_unique<MaroRosRuntime>();
    if (!g_runtime->start(robotName.asChar())) {
        g_runtime.reset();
        MGlobal::displayError(
            "Maro: could not start the ROS 2 bridge. Check that the ROS 2 "
            "runtime DLLs sit next to the plugin.");
        return MS::kFailure;
    }

    status = MaroPump::start(*g_runtime);
    if (!status) {
        g_runtime->stop();
        g_runtime.reset();
        MGlobal::displayError("Maro: could not start the main-thread pump.");
        return status;
    }

    // 수신 노드. Maya가 스레드를 만들고 관리한다 -- 우리는 만들지 않는다.
    MDGModifier createModifier;
    MObject deviceObj = createModifier.createNode(MaroCommandDeviceNode::id, &status);
    if (!status) {
        MaroPump::stop();
        g_runtime->stop();
        g_runtime.reset();
        MGlobal::displayError("Maro: could not create the command device node.");
        return status;
    }
    status = createModifier.doIt();
    if (!status) {
        // 아래 devicePtr == nullptr 분기와 똑같은 경합이다: createNode()가
        // 이미 호출됐으므로(doIt() 실패 여부와 무관하게) 디바이스 스레드가
        // 이미 살아있을 수 있고, 여기서처럼 g_runtime->stop()을 직접 부르면
        // 그 스레드 밑에서 rclcpp::shutdown()이 도는 경합 -- shutdownBridge()
        // 가 폴링으로 막으려는 바로 그 상황 -- 을 재현한다. 만든 노드를
        // undoIt()으로 먼저 되돌리고(안 그러면 노드가 씬에 남아 플러그인
        // 언로드를 막는다), 나머지 정리는 shutdownBridge()에 맡긴다.
        // g_commandDeviceHandle은 이 지점에서 아직 세팅되지 않았으므로
        // shutdownBridge()의 "핸들로 노드 지우기" 단계는 아무 일도 하지
        // 않는다 -- 중복 삭제가 아니다.
        const MStatus undoStatus = createModifier.undoIt();
        if (!undoStatus) {
            MGlobal::displayError(
                "Maro: could not undo command device node creation; a "
                "stray node may remain and block plugin unload.");
        }
        shutdownBridge();
        MGlobal::displayError("Maro: could not add the command device node to the DG.");
        return status;
    }

    MFnDependencyNode deviceFn(deviceObj);

    // C1: 이 노드는 런타임 배관이지 사용자 데이터가 아니다. 표시해 두지
    // 않으면 브리지를 켠 채로 씬을 저장했을 때 이 노드가 파일에 그대로
    // 실린다. 다음에 그 씬을 열면 g_commandDeviceHandle은 빈 채로 시작하므로
    // shutdownBridge()가 그 노드를 절대 찾지 못해 못 지우고, setRobotName()도
    // 다시 불리지 않아 threadHandler()는 이름 없는 노드를 붙들고 영원히
    // 돈다 -- 그리고 인스턴스가 남아 있으니 deregisterNode도 실패해
    // 플러그인을 내릴 수 없다. 씬 저장 한 번으로 이 태스크가 고치려는
    // "프로세스가 안 끝나는" 전례를 재현하는 셈이라, 실패해도 조용히
    // 넘기지 않고 경고한다.
    const MStatus doNotWriteStatus = deviceFn.setDoNotWrite(true);
    if (!doNotWriteStatus) {
        MGlobal::displayWarning(
            "Maro: could not mark the command device node non-persistent; "
            "it may be saved into the scene file.");
    }

    auto* devicePtr = dynamic_cast<MaroCommandDeviceNode*>(deviceFn.userNode());
    if (devicePtr == nullptr) {
        // I6: createModifier.doIt()가 이미 이 노드를 DG에 넣었고,
        // g_commandDeviceHandle은 아직 세팅 전이다. 여기서 그냥 리턴하면
        // shutdownBridge()가 이 노드를 절대 찾지 못해 인스턴스가 씬에 남고,
        // C1과 같은 이유로 플러그인 언로드가 막힌다. 만든 걸 되돌린다.
        const MStatus undoStatus = createModifier.undoIt();
        if (!undoStatus) {
            MGlobal::displayError(
                "Maro: could not undo command device node creation; a "
                "stray node may remain and block plugin unload.");
        }

        // Gap 2 (재검토): 위 undoIt()가 DG에서 노드를 지워도, 463행 근처
        // 주석이 이미 짚었듯 threadHandler()는 노드 "생성" 시점에 이미
        // 시작됐을 수 있다 -- live나 C++ 인스턴스 획득 여부와 무관하게.
        // deleteNode() 계열 호출이 그 스레드의 종료를 동기적으로 보장한다는
        // 근거가 devkit 문서에 없다는 사실도 shutdownBridge() 맨 위 설계
        // 노트와 똑같이 적용된다. 그러므로 여기서 (예전 코드처럼)
        // g_runtime->stop()을 직접 부르면, shutdownBridge()가 존재하는
        // 이유였던 바로 그 경합 -- 살아있는 스레드 밑에서
        // rclcpp::shutdown() -- 을 다른 호출부에서 그대로 재현하게 된다.
        // shutdownBridge()를 부르는 게 안전한 이유: g_commandDeviceHandle은
        // 이 지점에서 아직 세팅되지 않았으므로(무효 핸들, 464행 근처에서야
        // 세팅된다) shutdownBridge()의 "핸들로 노드 지우기" 단계는 아무 일도
        // 안 한다 (이미 위 undoIt()로 지웠다) -- 중복 삭제가 아니다. g_runtime은
        // 이 지점에서 항상 non-null이다 (바로 위에서 make_unique와 start()가
        // 성공했다). 그 뒤로는 shutdownBridge()의 폴링 + 가드된 g_runtime
        // 정리(정상 정지 또는 진짜 누수)만 남는데, 그게 정확히 우리가 여기서
        // 원하는 동작이다. 로직을 복제하는 대신 재사용한다.
        shutdownBridge();
        MGlobal::displayError("Maro: command device node has no C++ instance.");
        return MS::kFailure;
    }

    MaroCommandDeviceNode::resetStats();
    // setRobotName()은 메인 스레드에서, live를 켜기 전에 부른다. 스레드
    // 자체는 노드 생성 시점에 이미 돌기 시작했을 수 있으므로 (문서상 live는
    // 스레드 존재가 아니라 데이터 처리만 게이트한다), 로봇 이름은 뮤텍스로
    // 안전하게 넘긴다 (MaroCommandDeviceNode::setRobotName 참고).
    devicePtr->setRobotName(robotName);
    g_commandDeviceHandle = MObjectHandle(deviceObj);

    MDGModifier liveModifier;
    liveModifier.newPlugValueBool(
        deviceFn.findPlug(MPxThreadedDeviceNode::live, false), true);
    status = liveModifier.doIt();
    if (!status) {
        MGlobal::displayError("Maro: could not set the command device live.");
        shutdownBridge();
        return status;
    }

    // I3.1: 이 조회가 실제로 하는 일은 딱 하나다 -- DG의 지연 평가 규칙을
    // 깨고 aCommandOut의 첫 compute()를 강제로 한 번 트리거해서, 그 안에서
    // Maya가 백그라운드 스레드를 존재하게 만들도록 킥하는 것. 그게 전부다.
    // 이후 반복 compute() 갱신 -- 즉 명령이 실제로 계속 흘러 들어오는 부분
    // -- 은 이 호출과 무관하게 유휴(idle) 이벤트 큐가 담당하고, devkit
    // 헤더가 명시하듯(MPxThreadedDeviceNode.h, "NOTE" 근처) 그 큐는 배치
    // 모드에서 돌지 않는다. 그러므로 이 한 줄은 배치/헤드리스에서 인바운드
    // 전달을 살리지 못한다 -- threadTicks가 0보다 커져서 위 배치 경고가
    // 없다면 "건강해 보이지만 사실 죽어 있는" 상태를 만들 뿐이다.
    // I3.2: 이 줄의 존재 이유 자체가 "안 그러면 조용히 실패한다"인데, 정작
    // 이 줄 자체가 조용히 실패하면 안 되므로 MStatus를 받아 확인한다.
    MStatus pullStatus;
    deviceFn.findPlug(MaroCommandDeviceNode::aCommandOut, false).asBool(&pullStatus);
    if (!pullStatus) {
        MGlobal::displayWarning(
            "Maro: could not force the command device's first compute(); "
            "inbound delivery may not start until something else "
            "re-evaluates the node.");
    }

    MGlobal::displayInfo(MString("Maro: bridge running as '") + robotName + "'.");
    return MS::kSuccess;
}

void* MaroStopBridgeCommand::creator() { return new MaroStopBridgeCommand(); }

MStatus MaroStopBridgeCommand::doIt(const MArgList&) {
    shutdownBridge();
    MGlobal::displayInfo("Maro: bridge stopped.");
    return MS::kSuccess;
}

void* MaroBridgeStatsCommand::creator() { return new MaroBridgeStatsCommand(); }

MStatus MaroBridgeStatsCommand::doIt(const MArgList&) {
    MIntArray stats;
    stats.append(static_cast<int>(MaroPump::collectedSampleCount()));
    stats.append(static_cast<int>(
        g_runtime ? g_runtime->drainedSampleCount() : 0));
    stats.append(static_cast<int>(MaroCommandDeviceNode::appliedCommandCount()));
    stats.append(static_cast<int>(MaroCommandDeviceNode::threadTickCount()));
    stats.append(static_cast<int>(
        g_runtime ? g_runtime->publishErrorCount() : 0));
    setResult(stats);
    return MS::kSuccess;
}

void* MaroSetControlModeCommand::creator() {
    return new MaroSetControlModeCommand();
}

MSyntax MaroSetControlModeCommand::newSyntax() {
    MSyntax syntax;
    syntax.setObjectType(MSyntax::kStringObjects, 2, 2);
    return syntax;
}

MStatus MaroSetControlModeCommand::doIt(const MArgList& args) {
    MStatus status;

    if (args.length() != 2) {
        MGlobal::displayError(
            "Maro: maroSetControlMode needs <axis> <0=Manual|1=ROS>.");
        return MS::kFailure;
    }

    const MString axisName = args.asString(0, &status);
    if (!status) return status;
    const int mode = args.asInt(1, &status);
    if (!status) return status;

    if (mode != 0 && mode != 1) {
        MGlobal::displayError("Maro: control mode must be 0 (Manual) or 1 (ROS).");
        return MS::kFailure;
    }

    MSelectionList selection;
    if (!selection.add(axisName)) {
        MGlobal::displayError(MString("Maro: cannot find node '") + axisName + "'.");
        return MS::kFailure;
    }

    MObject axisObj;
    selection.getDependNode(0, axisObj);

    MFnDependencyNode axisFn(axisObj);
    if (axisFn.typeId() != MaroAxisNode::id) {
        MGlobal::displayError(
            MString("Maro: '") + axisName + "' is not a maroAxis node.");
        return MS::kFailure;
    }

    MPlug modePlug = axisFn.findPlug(MaroAxisNode::aControlMode, false, &status);
    if (!status) return status;

    // Manual -> ROS 전환 시, 실제 명령이 오기 전까지의 목표를 현재 값으로
    // 시딩해 로봇이 마지막 명령값으로 튀는 것을 막는다. 순서가 중요하다:
    // outValue를 먼저 읽고 aRosCommand를 시딩한 다음에 모드를 바꿔야 한다.
    // 반대로 하면 compute()가 이미 소스를 ROS로 바꾼 뒤라 0에서 시딩하게
    // 된다.
    if (mode == 1 && modePlug.asShort() == 0) {
        MPlug outValue = axisFn.findPlug(MaroAxisNode::aOutValue, false);
        // outValue는 MFnUnitAttribute::kAngle이다. asDouble()로 읽으면 Maya가
        // UI 각도 단위(기본 도)로 변환한 값을 돌려줄 수 있어 로그가 실제
        // 라디안 값과 어긋난다. MaroPump.cpp가 같은 이유로 이미 asMAngle().
        // asRadians()를 쓰고 있다 -- 여기는 로그 전용이라 동작은 안 바뀐다.
        const double seedValue = outValue.asMAngle().asRadians();

        // 런타임 데이터 흐름이므로 직접 쓴다 (applyToMatchingAxis와 동일한
        // 관례). undo 스택(m_modifier)에는 올리지 않는다 -- 이건 사용자
        // 설정이 아니라 프레임 단위 런타임 시딩이다.
        axisFn.findPlug(MaroAxisNode::aRosCommand, false).setDouble(seedValue);

        MGlobal::displayInfo(
            MString("Maro: seeding ROS target for '") + axisName + "' with " +
            seedValue + " to avoid a jump on mode switch.");
    }

    status = m_modifier.newPlugValueShort(modePlug, static_cast<short>(mode));
    if (!status) return status;

    return redoIt();
}

MStatus MaroSetControlModeCommand::redoIt() {
    return m_modifier.doIt();
}

MStatus MaroSetControlModeCommand::undoIt() {
    return m_modifier.undoIt();
}

}  // namespace maro
