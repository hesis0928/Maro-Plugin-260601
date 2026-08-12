#pragma once

#include <maya/MPxNode.h>
#include <maya/MTypeId.h>

namespace maro {

// 능력 노드는 축에 쌓여 그 축의 성격을 만든다.
// 타입을 미리 고르는 게 아니라 무엇을 쌓았는지로 성격이 창발한다.

// 모든 능력 노드가 같은 모양의 복합 출력을 낸다. 축은 이걸 데이터블록으로
// 읽으므로 종류가 늘어도 축의 평가 루프는 그대로다.
struct CapabilityOutAttrs {
    MObject compound;   // capabilityOut
    MObject type;       // capType   short
    MObject value;      // capValue  double
    MObject enable;     // capEnable short3
    MObject minimum;    // capMin    double3
    MObject maximum;    // capMax    double3
};

// 능력 노드의 initialize()에서 공통 출력을 만들어 붙인다.
MStatus createCapabilityOut(CapabilityOutAttrs& attrs);

class MaroRotationNode : public MPxNode {
public:
    static void* creator();
    static MStatus initialize();
    MStatus compute(const MPlug& plug, MDataBlock& data) override;
    static MTypeId id;

    static MObject aAngle;           // double, 라디안
    static CapabilityOutAttrs out;
};

class MaroLimitNode : public MPxNode {
public:
    static void* creator();
    static MStatus initialize();
    MStatus compute(const MPlug& plug, MDataBlock& data) override;
    static MTypeId id;

    static MObject aEnableX;
    static MObject aEnableY;
    static MObject aEnableZ;
    static MObject aMinX;
    static MObject aMaxX;
    static MObject aMinY;
    static MObject aMaxY;
    static MObject aMinZ;
    static MObject aMaxZ;
    static CapabilityOutAttrs out;
};

class MaroSensorDirectionNode : public MPxNode {
public:
    static void* creator();
    static MStatus initialize();
    MStatus compute(const MPlug& plug, MDataBlock& data) override;
    static MTypeId id;

    static MObject aDirection;       // float3
    static CapabilityOutAttrs out;
};

class MaroSensorRangeNode : public MPxNode {
public:
    static void* creator();
    static MStatus initialize();
    MStatus compute(const MPlug& plug, MDataBlock& data) override;
    static MTypeId id;

    static MObject aRange;           // double
    static MObject aConeAngle;       // double, 라디안
    static CapabilityOutAttrs out;
};

}  // namespace maro
