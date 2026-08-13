#include "MaroCapabilityNodes.h"

#include <maya/MAngle.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>

namespace maro {

MStatus createCapabilityOut(CapabilityOutAttrs& attrs) {
    // capValue/capMin/capMax carry radians here on purpose, as plain doubles
    // rather than MFnUnitAttribute::kAngle. This compound is an internal
    // transport between our own nodes (maroRotation/maroLimit -> maroAxis);
    // it is marked non-storable/non-writable and never shown in the
    // Attribute Editor, so there is no UI-unit ambiguity to resolve. Making
    // it a unit type would also require child access through MFnUnitAttribute
    // on every read/write for no user-visible benefit. The angle/limit
    // attributes that a user actually types into keep real units; this
    // compound does not.
    MFnNumericAttribute numFn;
    MFnCompoundAttribute cmpFn;

    attrs.type = numFn.create("capType", "cpt", MFnNumericData::kShort, 0);
    attrs.value = numFn.create("capValue", "cpv", MFnNumericData::kDouble, 0.0);
    attrs.enable = numFn.create("capEnable", "cpe", MFnNumericData::k3Short, 0);
    attrs.minimum = numFn.create("capMin", "cpn", MFnNumericData::k3Double, 0.0);
    attrs.maximum = numFn.create("capMax", "cpx", MFnNumericData::k3Double, 0.0);

    attrs.compound = cmpFn.create("capabilityOut", "cpo");
    cmpFn.addChild(attrs.type);
    cmpFn.addChild(attrs.value);
    cmpFn.addChild(attrs.enable);
    cmpFn.addChild(attrs.minimum);
    cmpFn.addChild(attrs.maximum);
    cmpFn.setStorable(false);
    cmpFn.setWritable(false);

    return MS::kSuccess;
}

MTypeId MaroRotationNode::id(0x00135101);
MObject MaroRotationNode::aAngle;
CapabilityOutAttrs MaroRotationNode::out;

void* MaroRotationNode::creator() { return new MaroRotationNode(); }

MStatus MaroRotationNode::initialize() {
    MFnUnitAttribute angFn;

    // Real angular unit: the Attribute Editor now shows/accepts degrees
    // (Maya's default UI angle unit) while the data block still stores
    // radians internally, same as any native rotate* attribute.
    aAngle = angFn.create("angle", "ang", MFnUnitAttribute::kAngle, 0.0);
    angFn.setStorable(true);
    angFn.setKeyable(true);
    addAttribute(aAngle);

    createCapabilityOut(out);
    addAttribute(out.compound);

    attributeAffects(aAngle, out.compound);
    return MS::kSuccess;
}

MStatus MaroRotationNode::compute(const MPlug& plug, MDataBlock& data) {
    try {
        if (plug != out.compound && plug.parent() != out.compound) {
            return MS::kUnknownParameter;
        }

        MDataHandle handle = data.outputValue(out.compound);
        handle.child(out.type).setShort(0);   // 0 = rotation
        // .asAngle().asRadians() is explicit about the unit; the compound
        // child it feeds (capValue) is a plain double carrying radians.
        handle.child(out.value).setDouble(
            data.inputValue(aAngle).asAngle().asRadians());
        data.setClean(plug);
        return MS::kSuccess;
    } catch (...) {
        MGlobal::displayError("Maro: maroRotation compute failed.");
        return MS::kFailure;
    }
}

MTypeId MaroLimitNode::id(0x00135102);
MObject MaroLimitNode::aEnableX;
MObject MaroLimitNode::aEnableY;
MObject MaroLimitNode::aEnableZ;
MObject MaroLimitNode::aMinX;
MObject MaroLimitNode::aMaxX;
MObject MaroLimitNode::aMinY;
MObject MaroLimitNode::aMaxY;
MObject MaroLimitNode::aMinZ;
MObject MaroLimitNode::aMaxZ;
CapabilityOutAttrs MaroLimitNode::out;

void* MaroLimitNode::creator() { return new MaroLimitNode(); }

namespace {
MObject makeBool(MFnNumericAttribute& fn, const char* longName,
                 const char* shortName) {
    MObject attr = fn.create(longName, shortName, MFnNumericData::kBoolean, 0);
    fn.setStorable(true);
    fn.setKeyable(true);
    return attr;
}

// Angle unit attributes. The default is expressed via MAngle so the
// intent ("+/-180 degrees") reads directly in code instead of as a
// repeated-digit radian literal.
MObject makeAngle(MFnUnitAttribute& fn, const char* longName,
                  const char* shortName, const MAngle& value) {
    MObject attr = fn.create(longName, shortName, value);
    fn.setStorable(true);
    fn.setKeyable(true);
    return attr;
}
}  // namespace

MStatus MaroLimitNode::initialize() {
    MFnNumericAttribute numFn;
    MFnUnitAttribute angFn;

    aEnableX = makeBool(numFn, "enableX", "enx");
    addAttribute(aEnableX);
    aEnableY = makeBool(numFn, "enableY", "eny");
    addAttribute(aEnableY);
    aEnableZ = makeBool(numFn, "enableZ", "enz");
    addAttribute(aEnableZ);

    aMinX = makeAngle(angFn, "minX", "mnx", MAngle(-180.0, MAngle::kDegrees));
    addAttribute(aMinX);
    aMaxX = makeAngle(angFn, "maxX", "mxx", MAngle(180.0, MAngle::kDegrees));
    addAttribute(aMaxX);
    aMinY = makeAngle(angFn, "minY", "mny", MAngle(-180.0, MAngle::kDegrees));
    addAttribute(aMinY);
    aMaxY = makeAngle(angFn, "maxY", "mxy", MAngle(180.0, MAngle::kDegrees));
    addAttribute(aMaxY);
    aMinZ = makeAngle(angFn, "minZ", "mnz", MAngle(-180.0, MAngle::kDegrees));
    addAttribute(aMinZ);
    aMaxZ = makeAngle(angFn, "maxZ", "mxz", MAngle(180.0, MAngle::kDegrees));
    addAttribute(aMaxZ);

    createCapabilityOut(out);
    addAttribute(out.compound);

    for (const MObject& src : {aEnableX, aEnableY, aEnableZ, aMinX, aMaxX,
                               aMinY, aMaxY, aMinZ, aMaxZ}) {
        attributeAffects(src, out.compound);
    }
    return MS::kSuccess;
}

MStatus MaroLimitNode::compute(const MPlug& plug, MDataBlock& data) {
    try {
        if (plug != out.compound && plug.parent() != out.compound) {
            return MS::kUnknownParameter;
        }

        MDataHandle handle = data.outputValue(out.compound);
        handle.child(out.type).setShort(1);   // 1 = limit

        handle.child(out.enable).set3Short(
            static_cast<short>(data.inputValue(aEnableX).asBool()),
            static_cast<short>(data.inputValue(aEnableY).asBool()),
            static_cast<short>(data.inputValue(aEnableZ).asBool()));

        // .asAngle().asRadians() is explicit about the unit; the compound
        // children they feed (capMin/capMax) are plain doubles carrying
        // radians.
        handle.child(out.minimum).set3Double(
            data.inputValue(aMinX).asAngle().asRadians(),
            data.inputValue(aMinY).asAngle().asRadians(),
            data.inputValue(aMinZ).asAngle().asRadians());
        handle.child(out.maximum).set3Double(
            data.inputValue(aMaxX).asAngle().asRadians(),
            data.inputValue(aMaxY).asAngle().asRadians(),
            data.inputValue(aMaxZ).asAngle().asRadians());

        data.setClean(plug);
        return MS::kSuccess;
    } catch (...) {
        MGlobal::displayError("Maro: maroLimit compute failed.");
        return MS::kFailure;
    }
}

MTypeId MaroSensorDirectionNode::id(0x00135103);
MObject MaroSensorDirectionNode::aDirection;
CapabilityOutAttrs MaroSensorDirectionNode::out;

void* MaroSensorDirectionNode::creator() { return new MaroSensorDirectionNode(); }

MStatus MaroSensorDirectionNode::initialize() {
    MFnNumericAttribute numFn;

    aDirection = numFn.createPoint("direction", "dir");
    numFn.setStorable(true);
    numFn.setKeyable(true);
    numFn.setDefault(0.0f, 0.0f, 1.0f);
    addAttribute(aDirection);

    createCapabilityOut(out);
    addAttribute(out.compound);

    attributeAffects(aDirection, out.compound);
    return MS::kSuccess;
}

MStatus MaroSensorDirectionNode::compute(const MPlug& plug, MDataBlock& data) {
    try {
        if (plug != out.compound && plug.parent() != out.compound) {
            return MS::kUnknownParameter;
        }

        // 방향은 축의 구동값에 기여하지 않는다. S4가 소비할 수 있도록
        // capMin에 방향 벡터를 실어 두고 타입만 표시한다.
        const float3& dir = data.inputValue(aDirection).asFloat3();

        MDataHandle handle = data.outputValue(out.compound);
        handle.child(out.type).setShort(2);   // 2 = sensorDirection
        handle.child(out.minimum).set3Double(dir[0], dir[1], dir[2]);

        data.setClean(plug);
        return MS::kSuccess;
    } catch (...) {
        MGlobal::displayError("Maro: maroSensorDirection compute failed.");
        return MS::kFailure;
    }
}

MTypeId MaroSensorRangeNode::id(0x00135104);
MObject MaroSensorRangeNode::aRange;
MObject MaroSensorRangeNode::aConeAngle;
CapabilityOutAttrs MaroSensorRangeNode::out;

void* MaroSensorRangeNode::creator() { return new MaroSensorRangeNode(); }

MStatus MaroSensorRangeNode::initialize() {
    MFnNumericAttribute numFn;
    MFnUnitAttribute angFn;

    aRange = numFn.create("range", "rng", MFnNumericData::kDouble, 10.0);
    numFn.setStorable(true);
    numFn.setKeyable(true);
    addAttribute(aRange);

    // Real angular unit, same convention as maroRotation.angle and the
    // maroLimit min/max attributes: the Attribute Editor shows/accepts
    // degrees while the data block stores radians. Previously this was a
    // plain double carrying radians, so typing "30" here silently meant 30
    // radians while the adjacent capability nodes meant 30 degrees. Nothing
    // consumes this attribute yet, so there is no behavioral migration risk.
    aConeAngle = angFn.create("coneAngle", "cna", MFnUnitAttribute::kAngle,
                              0.5236);
    angFn.setStorable(true);
    angFn.setKeyable(true);
    addAttribute(aConeAngle);

    createCapabilityOut(out);
    addAttribute(out.compound);

    attributeAffects(aRange, out.compound);
    attributeAffects(aConeAngle, out.compound);
    return MS::kSuccess;
}

MStatus MaroSensorRangeNode::compute(const MPlug& plug, MDataBlock& data) {
    try {
        if (plug != out.compound && plug.parent() != out.compound) {
            return MS::kUnknownParameter;
        }

        MDataHandle handle = data.outputValue(out.compound);
        handle.child(out.type).setShort(3);   // 3 = sensorRange
        // .asAngle().asRadians() is explicit about the unit, same as
        // maroRotation/maroLimit; the compound child it feeds (capMin.y) is
        // a plain double carrying radians.
        handle.child(out.minimum).set3Double(
            data.inputValue(aRange).asDouble(),
            data.inputValue(aConeAngle).asAngle().asRadians(),
            0.0);

        data.setClean(plug);
        return MS::kSuccess;
    } catch (...) {
        MGlobal::displayError("Maro: maroSensorRange compute failed.");
        return MS::kFailure;
    }
}

}  // namespace maro
