#pragma once

#include <maya/MPxLocatorNode.h>
#include <maya/MTypeId.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>

namespace maro {

// 축은 Maya 오브젝트 하나에 바인딩되는 앵커다.
// 능력 노드가 capabilityIn 배열에 쌓여 이 축의 성격을 결정한다.
class MaroAxisNode : public MPxLocatorNode {
public:
    static void* creator();
    static MStatus initialize();

    MStatus compute(const MPlug& plug, MDataBlock& data) override;

    static MTypeId id;

    // 바인딩과 계층
    static MObject aTargetObject;   // message
    static MObject aParentAxis;     // message

    // 능력 스택. 데이터 복합 배열이다 (message가 아니다 — 값을 날라야 하므로).
    static MObject aCapabilityIn;   // compound array
    static MObject aCapType;        //   short  0=rotation 1=limit 2=sensorDir 3=sensorRange
    static MObject aCapValue;       //   double rotation 각도
    static MObject aCapEnable;      //   short3 limit 활성 X/Y/Z
    static MObject aCapMin;         //   double3 limit 하한
    static MObject aCapMax;         //   double3 limit 상한

    // 설정
    static MObject aJointName;      // string
    static MObject aConventionAxis; // enum: 0=X 1=Y 2=Z
    static MObject aConventionInvert;  // bool
    static MObject aControlMode;    // enum: 0=Manual 1=ROS
    static MObject aRosCommand;     // double, ROS 모드에서의 기준값 (펌프가 씀)
    static MObject aEnabled;        // bool

    // 출력
    static MObject aOutValue;       // double
    static MObject aOutTransform;   // matrix
};

}  // namespace maro
