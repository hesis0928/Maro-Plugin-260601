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
    // outValue는 이 축이 ROS 2로 내보낼 관절값이자, 네이티브 rotateX류
    // 어트리뷰트에 직접 연결될 수 있는 값이다. MFnUnitAttribute::kAngle로
    // 선언해 두어야 그 연결에서 Maya가 라디안을 UI 단위(도)로 오인해
    // 엉뚱한 unitConversion 배율을 끼워 넣는 사고를 피한다. C++에서는
    // asAngle().asRadians()로 읽으면 UI 단위와 무관하게 항상 라디안이다.
    static MObject aOutValue;       // MFnUnitAttribute::kAngle (내부: 라디안)
    static MObject aOutTransform;   // matrix
};

}  // namespace maro
