#include "MaroCommands.h"

#include <maya/MArgList.h>
#include <maya/MDagPath.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MSelectionList.h>

#include "MaroAxisNode.h"

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

    // 규칙: 오브젝트 하나에는 축 하나만.
    MFnDependencyNode targetFn(targetObj);
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

    MPlug axisTarget = axisFn.findPlug(MaroAxisNode::aTargetObject, false, &status);
    if (!status) return status;

    status = m_modifier.connect(targetMessage, axisTarget);
    if (!status) return status;

    return redoIt();
}

MStatus MaroBindAxisCommand::redoIt() {
    return m_modifier.doIt();
}

MStatus MaroBindAxisCommand::undoIt() {
    return m_modifier.undoIt();
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
}

MStatus MaroConnectAxisCommand::redoIt() {
    return m_modifier.doIt();
}

MStatus MaroConnectAxisCommand::undoIt() {
    return m_modifier.undoIt();
}

}  // namespace maro
