#include "MaroCommands.h"

#include <chrono>
#include <memory>
#include <thread>

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
        MaroPump::stop();
        g_runtime->stop();
        g_runtime.reset();
        MGlobal::displayError("Maro: could not add the command device node to the DG.");
        return status;
    }

    MFnDependencyNode deviceFn(deviceObj);
    auto* devicePtr = dynamic_cast<MaroCommandDeviceNode*>(deviceFn.userNode());
    if (devicePtr == nullptr) {
        MaroPump::stop();
        g_runtime->stop();
        g_runtime.reset();
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

    // devkit 문서(MPxThreadedDeviceNode.h)는 postConstructor()에서 등록한
    // "refresh output attributes"가 유휴 큐를 통해 갱신된다고만 말하고,
    // 그 첫 갱신이 어떻게 트리거되는지는 말하지 않는다. 실측 결과, DG는
    // 지연 평가라 아무도 aCommandOut을 조회하지 않으면 (뷰포트가 도는
    // 대화형 세션에서는 뷰포트 리프레시가 매 프레임 이걸 대신 해 준다)
    // 백그라운드 스레드 자체가 시작되지 않는다 -- 뷰포트가 없는 배치/헤드리스
    // 실행(자동화 파이프라인 등)에서는 아무도 그 역할을 대신하지 않는다.
    // 대화형 여부에 기대지 않도록 여기서 한 번 직접 조회해 강제로 첫
    // compute()를 끌어낸다. 이미 대화형 세션이라면 군더더기 호출 하나일 뿐
    // 해가 없다.
    deviceFn.findPlug(MaroCommandDeviceNode::aCommandOut, false).asBool();

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
    setResult(stats);
    return MS::kSuccess;
}

}  // namespace maro
