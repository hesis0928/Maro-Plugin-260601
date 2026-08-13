#include "MaroAxisNode.h"

#include <algorithm>
#include <cmath>

#include <maya/MAngle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MPlug.h>

namespace maro {

MTypeId MaroAxisNode::id(0x00135100);

MObject MaroAxisNode::aTargetObject;
MObject MaroAxisNode::aParentAxis;
MObject MaroAxisNode::aCapabilityIn;
MObject MaroAxisNode::aCapType;
MObject MaroAxisNode::aCapValue;
MObject MaroAxisNode::aCapEnable;
MObject MaroAxisNode::aCapMin;
MObject MaroAxisNode::aCapMax;
MObject MaroAxisNode::aJointName;
MObject MaroAxisNode::aConventionAxis;
MObject MaroAxisNode::aConventionInvert;
MObject MaroAxisNode::aControlMode;
MObject MaroAxisNode::aRosCommand;
MObject MaroAxisNode::aEnabled;
MObject MaroAxisNode::aOutValue;
MObject MaroAxisNode::aOutTransform;

void* MaroAxisNode::creator() {
    return new MaroAxisNode();
}

MStatus MaroAxisNode::initialize() {
    MFnMessageAttribute msgFn;
    MFnNumericAttribute numFn;
    MFnTypedAttribute typFn;
    MFnEnumAttribute enumFn;
    MFnMatrixAttribute matFn;
    MFnUnitAttribute angFn;

    // 바인딩은 이름 문자열이 아니라 message 연결로 맺는다.
    // Maya가 rename/delete/undo를 자동 추적하므로 동기화가 깨지지 않는다.
    aTargetObject = msgFn.create("targetObject", "tgo");
    msgFn.setStorable(true);
    addAttribute(aTargetObject);

    aParentAxis = msgFn.create("parentAxis", "pax");
    msgFn.setStorable(true);
    addAttribute(aParentAxis);

    // 스택 입력은 데이터 복합 어트리뷰트다. message는 데이터를 나르지 않아
    // compute가 플러그를 직접 조회해야 하고, 그러면 DG 더티 전파를 우회해
    // 병렬 평가에서 값이 어긋난다. 능력 노드의 기여를 실제 데이터로 받는다.
    aCapType = numFn.create("capType", "cpt", MFnNumericData::kShort, 0);
    aCapValue = numFn.create("capValue", "cpv", MFnNumericData::kDouble, 0.0);
    aCapEnable = numFn.create("capEnable", "cpe", MFnNumericData::k3Short, 0);
    aCapMin = numFn.create("capMin", "cpn", MFnNumericData::k3Double, 0.0);
    aCapMax = numFn.create("capMax", "cpx", MFnNumericData::k3Double, 0.0);

    MFnCompoundAttribute cmpFn;
    aCapabilityIn = cmpFn.create("capabilityIn", "cpi");
    cmpFn.addChild(aCapType);
    cmpFn.addChild(aCapValue);
    cmpFn.addChild(aCapEnable);
    cmpFn.addChild(aCapMin);
    cmpFn.addChild(aCapMax);
    cmpFn.setStorable(true);
    cmpFn.setArray(true);
    cmpFn.setIndexMatters(true);   // 스택은 인덱스 순서대로 평가된다
    addAttribute(aCapabilityIn);

    aJointName = typFn.create("jointName", "jnm", MFnData::kString);
    typFn.setStorable(true);
    addAttribute(aJointName);

    aConventionAxis = enumFn.create("conventionAxis", "cva", 1);  // 기본 Y
    enumFn.addField("X", 0);
    enumFn.addField("Y", 1);
    enumFn.addField("Z", 2);
    enumFn.setStorable(true);
    enumFn.setKeyable(true);
    addAttribute(aConventionAxis);

    aConventionInvert = numFn.create("conventionInvert", "cvi",
                                     MFnNumericData::kBoolean, 0);
    numFn.setStorable(true);
    numFn.setKeyable(true);
    addAttribute(aConventionInvert);

    aControlMode = enumFn.create("controlMode", "cmd", 0);  // 기본 Manual
    enumFn.addField("Manual", 0);
    enumFn.addField("ROS", 1);
    enumFn.setStorable(true);
    enumFn.setKeyable(true);
    addAttribute(aControlMode);

    // 펌프가 매 프레임 직접 쓰는 값이다. 사용자 구성이 아니므로 씬에 저장하지 않는다.
    aRosCommand = numFn.create("rosCommand", "rcm", MFnNumericData::kDouble, 0.0);
    numFn.setStorable(false);
    numFn.setKeyable(false);
    addAttribute(aRosCommand);

    aEnabled = numFn.create("enabled", "enb", MFnNumericData::kBoolean, 1);
    numFn.setStorable(true);
    numFn.setKeyable(true);
    addAttribute(aEnabled);

    // 네이티브 rotateX류 어트리뷰트로 바로 연결될 수 있는 값이므로 실제
    // 각도 단위로 선언한다. 평범한 double로 두면 그 연결에서 Maya가
    // 라디안 값을 UI 단위(도)로 오인해 잘못된 unitConversion 배율을
    // 끼워 넣는다 (실측: plainVal(1.0 rad 의도) -> rotateX 연결 시
    // conversionFactor 0.0174533로 1도로 둔갑).
    aOutValue = angFn.create("outValue", "otv", MFnUnitAttribute::kAngle, 0.0);
    angFn.setStorable(false);
    angFn.setWritable(false);
    addAttribute(aOutValue);

    aOutTransform = matFn.create("outTransform", "ott",
                                 MFnMatrixAttribute::kDouble);
    matFn.setStorable(false);
    matFn.setWritable(false);
    addAttribute(aOutTransform);

    // aConventionInvert는 여기서 attributeAffects에 넣지 않는다: compute()가
    // 실제로 읽지 않기 때문이다 (v1에는 반전 보정이 구현되어 있지 않다,
    // MaroAxisNode.h의 선언부 주석 참고). 안 읽는 소스를 영향권에 넣으면
    // 실제로는 아무 것도 안 바뀌는데 더티 전파만 유발해 재계산을 낭비한다.
    for (const MObject& src : {aConventionAxis, aEnabled,
                               aControlMode, aRosCommand}) {
        attributeAffects(src, aOutValue);
        attributeAffects(src, aOutTransform);
    }

    attributeAffects(aCapabilityIn, aOutValue);
    attributeAffects(aCapabilityIn, aOutTransform);

    return MS::kSuccess;
}

MStatus MaroAxisNode::compute(const MPlug& plug, MDataBlock& data) {
    try {
        if (plug != aOutValue && plug != aOutTransform) {
            return MS::kUnknownParameter;
        }

        const bool enabled = data.inputValue(aEnabled).asBool();

        double value = 0.0;

        if (enabled) {
            // 스택은 capabilityIn 인덱스 순서대로 평가한다.
            // rotation이 값을 만들고, 뒤따르는 limit들이 순차적으로 클램프한다.
            // 값은 전부 데이터블록에서 읽는다. 플러그를 직접 조회하면 DG 더티
            // 전파를 우회해 병렬 평가에서 값이 어긋난다.
            const short axisIndex = data.inputValue(aConventionAxis).asShort();
            const unsigned int component =
                (axisIndex == 0) ? 0u : ((axisIndex == 2) ? 2u : 1u);

            // 모드가 기준값의 출처를 정한다. 리밋은 어느 쪽이든 똑같이 적용된다.
            const bool rosDriven = data.inputValue(aControlMode).asShort() == 1;

            MArrayDataHandle stack = data.inputArrayValue(aCapabilityIn);

            for (unsigned int i = 0; i < stack.elementCount(); ++i) {
                stack.jumpToArrayElement(i);
                MDataHandle element = stack.inputValue();

                const short capType = element.child(aCapType).asShort();

                if (capType == 0) {           // rotation
                    value = rosDriven ? data.inputValue(aRosCommand).asDouble()
                                      : element.child(aCapValue).asDouble();
                } else if (capType == 1) {    // limit
                    const short3& enable = element.child(aCapEnable).asShort3();
                    if (enable[component] != 0) {
                        const double3& lo = element.child(aCapMin).asDouble3();
                        const double3& hi = element.child(aCapMax).asDouble3();
                        value = std::clamp(value,
                                           std::min(lo[component], hi[component]),
                                           std::max(lo[component], hi[component]));
                    }
                }
                // 센서 노드(capType 2, 3)는 구동값에 기여하지 않는다. S4에서 소비한다.
            }
        }

        // NaN/inf를 Maya에 흘리지 않는다.
        if (!std::isfinite(value)) {
            MGlobal::displayWarning(
                "Maro: axis produced a non-finite value; holding zero.");
            value = 0.0;
        }

        // aOutValue is MFnUnitAttribute::kAngle now; write via setMAngle so
        // Maya stores it correctly instead of reinterpreting the raw double.
        MDataHandle outVal = data.outputValue(aOutValue);
        outVal.setMAngle(MAngle(value, MAngle::kRadians));
        outVal.setClean();

        MDataHandle outXf = data.outputValue(aOutTransform);
        outXf.setMMatrix(MMatrix::identity);
        outXf.setClean();

        return MS::kSuccess;
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: maroAxis compute failed: ") + e.what());
        return MS::kFailure;
    } catch (...) {
        MGlobal::displayError("Maro: maroAxis compute failed with unknown error.");
        return MS::kFailure;
    }
}

}  // namespace maro
