#include "MaroPump.h"

#include <cmath>

#include <maya/MAngle.h>
#include <maya/MDagPath.h>
#include <maya/MDistance.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MMatrix.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MQuaternion.h>
#include <maya/MTimerMessage.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MVector.h>

#include "MaroAxisNode.h"
#include "MaroRosRuntime.h"

namespace maro {

MCallbackId MaroPump::s_timerId = 0;
MaroRosRuntime* MaroPump::s_runtime = nullptr;
std::atomic<std::uint64_t> MaroPump::s_collected{0};

namespace {

constexpr float kPumpIntervalSeconds = 1.0f / 30.0f;

// Maya의 현재 선형 단위를 미터 배율로 바꾼다.
// 하드코딩하면 사용자가 단위를 바꿨을 때 로봇이 100배로 나온다.
SceneUnit currentSceneUnit() {
    SceneUnit unit;
    unit.metersPerMayaUnit = MDistance(1.0, MDistance::internalUnit())
                                 .asMeters();
    return unit;
}

AxisConvention conventionOf(const MFnDependencyNode& axisFn) {
    AxisConvention conv;
    const short axisIndex =
        axisFn.findPlug(MaroAxisNode::aConventionAxis, false).asShort();
    conv.axis = (axisIndex == 0) ? LocalAxis::X
                                 : ((axisIndex == 2) ? LocalAxis::Z : LocalAxis::Y);
    conv.invert = axisFn.findPlug(MaroAxisNode::aConventionInvert, false).asBool();
    return conv;
}

}  // namespace

MStatus MaroPump::start(MaroRosRuntime& runtime) {
    if (s_timerId != 0) return MS::kSuccess;

    s_runtime = &runtime;
    s_collected.store(0);

    MStatus status;
    s_timerId = MTimerMessage::addTimerCallback(kPumpIntervalSeconds, onTimer,
                                                nullptr, &status);
    if (!status) {
        s_runtime = nullptr;
        s_timerId = 0;
    }
    return status;
}

MStatus MaroPump::stop() {
    if (s_timerId != 0) {
        MMessage::removeCallback(s_timerId);
        s_timerId = 0;
    }
    s_runtime = nullptr;
    return MS::kSuccess;
}

bool MaroPump::isRunning() { return s_timerId != 0; }

std::uint64_t MaroPump::collectedSampleCount() { return s_collected.load(); }

void MaroPump::onTimer(float, float, void*) {
    // Maya 콜백이다. 예외가 새면 Maya가 죽는다.
    try {
        if (s_runtime == nullptr) return;
        collectSamples(*s_runtime);
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: pump tick failed: ") + e.what());
    } catch (...) {
        MGlobal::displayError("Maro: pump tick failed with unknown error.");
    }
}

void MaroPump::collectSamples(MaroRosRuntime& runtime) {
    const SceneUnit unit = currentSceneUnit();

    for (MItDependencyNodes it(MFn::kPluginLocatorNode); !it.isDone(); it.next()) {
        MFnDependencyNode axisFn(it.thisNode());
        if (axisFn.typeId() != MaroAxisNode::id) continue;
        if (!axisFn.findPlug(MaroAxisNode::aEnabled, false).asBool()) continue;

        const MString joint =
            axisFn.findPlug(MaroAxisNode::aJointName, false).asString();
        if (joint.length() == 0) continue;   // 이름 없는 축은 발행하지 않는다

        AxisSample sample;
        sample.jointName = joint.asChar();
        // aOutValue는 MFnUnitAttribute::kAngle이다. asDouble()로 읽으면
        // Maya가 UI 각도 단위(기본 도)로 변환한 값을 돌려줄 수 있어
        // 라디안이 필요한 이 파이프라인에서 값이 어긋난다. asAngle()로
        // 받아 asRadians()로 명시해야 항상 라디안이다.
        sample.value =
            axisFn.findPlug(MaroAxisNode::aOutValue, false).asMAngle().asRadians();
        sample.convention = conventionOf(axisFn);
        sample.unit = unit;

        if (!std::isfinite(sample.value)) continue;

        // /tf는 이 축이 구동하는 실제 Maya 오브젝트의 월드 변환이 있어야
        // 의미가 있다. targetObject는 message 연결이라(데이터를 나르지
        // 않는다) MaroBindAxisCommand::doIt과 같은 방식으로만 오브젝트를
        // 얻을 수 있다 -- connectedTo(asDst=true)로 이 축에 연결된 소스
        // 쪽(바인딩된 트랜스폼)을 본다. 바인딩이 없으면 이 축은 씬 안의
        // 어떤 위치도 대표하지 않으므로, 이름 없는 축과 같은 이유로
        // 건너뛴다 -- 원점(identity)을 발행하는 것보다는 아예 발행하지
        // 않는 쪽이 낫다 (이 태스크가 고치는 바로 그 문제).
        MPlugArray targetSources;
        axisFn.findPlug(MaroAxisNode::aTargetObject, false)
            .connectedTo(targetSources, true, false);
        if (targetSources.length() == 0) continue;

        // MFnDagNode를 MObject로 바로 생성하면 경로 컨텍스트를 잃어
        // parentCount()/월드 행렬 조회가 조용히 틀어진다 -- 이미
        // MaroBindAxisCommand::doIt(MaroCommands.cpp)과
        // MaroDeleteWatcher.cpp에서 같은 함정에 걸렸다. MDagPath::getAPathTo로
        // 실제 경로를 얻어야 inclusiveMatrix()가 조상 체인을 포함한 진짜
        // 월드 행렬을 돌려준다.
        MDagPath targetPath;
        if (MDagPath::getAPathTo(targetSources[0].node(), targetPath) !=
            MS::kSuccess) {
            continue;
        }

        // /tf 프레임은 전부 공통 루트("world") 기준으로 발행되므로(
        // MaroRosRuntime::drainAndPublish 참고) 로컬이 아니라 월드 행렬을
        // 쓴다. 스케일/기울임은 이 파이프라인이 다루지 않으므로 kTransform
        // 공간으로 평행이동을, rotation()으로 회전만 뽑아낸다 -- 둘 다
        // Maya가 순수 행렬에서 피벗 없이 성분을 복원하는 표준 경로다.
        const MMatrix worldMatrix = targetPath.inclusiveMatrix();
        MTransformationMatrix xform(worldMatrix);

        MStatus translationStatus;
        const MVector t =
            xform.getTranslation(MSpace::kTransform, &translationStatus);
        const MQuaternion q = xform.rotation();

        // 이 값들은 백그라운드 스레드를 거쳐 그대로 ROS 2 와이어로 나간다.
        // NaN/inf가 거기까지 새지 않도록 여기서 막는다 (sample.value에 이미
        // 적용한 것과 같은 가드).
        const Vec3 position{t.x, t.y, t.z};
        const Quat rotation{q.x, q.y, q.z, q.w};
        if (!translationStatus || !isFinite(position) || !isFinite(rotation)) {
            continue;
        }

        sample.position = position;
        sample.rotation = rotation;

        runtime.publishQueue().push(std::move(sample));
        s_collected.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace maro
