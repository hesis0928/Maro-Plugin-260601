#include "MaroDeleteWatcher.h"

#include <maya/MDGMessage.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnSet.h>
#include <maya/MGlobal.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MNodeMessage.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MSelectionList.h>

#include "MaroAxisNode.h"
#include "MaroCapabilityNodes.h"

namespace maro {

MCallbackIdArray MaroDeleteWatcher::s_callbacks;

namespace {

constexpr char kOrphanSetName[] = "maroOrphanSet";

bool isCapabilityNode(const MFnDependencyNode& fn) {
    const MTypeId id = fn.typeId();
    return id == MaroRotationNode::id || id == MaroLimitNode::id ||
           id == MaroSensorDirectionNode::id || id == MaroSensorRangeNode::id;
}

// 고아 능력 노드를 담는 세트를 얻거나 만든다.
// 세트에 속하면 연결이 생겨 File > Optimize Scene Size가 지우지 못한다.
MObject orphanSet(MDGModifier& modifier) {
    MSelectionList existing;
    if (existing.add(kOrphanSetName)) {
        MObject setObj;
        if (existing.getDependNode(0, setObj)) {
            return setObj;
        }
    }

    MObject setObj = modifier.createNode("objectSet");
    modifier.renameNode(setObj, kOrphanSetName);
    return setObj;
}

}  // namespace

MStatus MaroDeleteWatcher::install() {
    MStatus status;

    // 이미 씬에 있는 노드에도 콜백을 건다.
    for (MItDependencyNodes it(MFn::kInvalid); !it.isDone(); it.next()) {
        MObject node = it.thisNode();
        onNodeAdded(node, nullptr);
    }

    s_callbacks.append(
        MDGMessage::addNodeAddedCallback(onNodeAdded, "dependNode", nullptr, &status));
    return status;
}

MStatus MaroDeleteWatcher::uninstall() {
    MMessage::removeCallbacks(s_callbacks);
    s_callbacks.clear();
    return MS::kSuccess;
}

void MaroDeleteWatcher::onNodeAdded(MObject& node, void* /*clientData*/) {
    try {
        MFnDependencyNode fn(node);
        MStatus status;

        if (fn.typeId() == MaroAxisNode::id) {
            s_callbacks.append(MNodeMessage::addNodeAboutToDeleteCallback(
                node, onAxisAboutToDelete, nullptr, &status));
        } else if (node.hasFn(MFn::kTransform)) {
            s_callbacks.append(MNodeMessage::addNodeAboutToDeleteCallback(
                node, onObjectAboutToDelete, nullptr, &status));
        }
    } catch (...) {
        // 콜백에서 예외가 새면 Maya가 죽는다.
        MGlobal::displayError("Maro: failed to attach delete callback.");
    }
}

void MaroDeleteWatcher::onObjectAboutToDelete(MObject& node,
                                              MDGModifier& modifier,
                                              void* /*clientData*/) {
    try {
        MFnDependencyNode fn(node);
        MPlug message = fn.findPlug("message", false);

        MPlugArray destinations;
        message.connectedTo(destinations, false, true);

        for (unsigned int i = 0; i < destinations.length(); ++i) {
            MObject other = destinations[i].node();
            MFnDependencyNode otherFn(other);
            if (otherFn.typeId() != MaroAxisNode::id) continue;

            // 이 modifier에 실으면 삭제와 undo/redo가 함께 묶인다.
            modifier.deleteNode(other);
            MGlobal::displayInfo(
                MString("Maro: deleting axis '") + otherFn.name() +
                "' because its bound object was deleted.");
        }
    } catch (...) {
        MGlobal::displayError("Maro: cascade delete failed.");
    }
}

void MaroDeleteWatcher::onAxisAboutToDelete(MObject& node, MDGModifier& modifier,
                                            void* /*clientData*/) {
    try {
        MFnDependencyNode axisFn(node);
        MPlug stack = axisFn.findPlug(MaroAxisNode::aCapabilityIn, false);

        MObject setObj;
        bool haveSet = false;

        for (unsigned int i = 0; i < stack.numElements(); ++i) {
            MPlug element = stack.elementByPhysicalIndex(i);

            // 스택은 복합 데이터 어트리뷰트다. 연결은 자식 플러그 단위로
            // 맺힐 수 있으므로 복합 자체와 자식을 모두 확인한다.
            MPlugArray sources;
            element.connectedTo(sources, true, false);
            for (unsigned int c = 0; sources.length() == 0 &&
                                     c < element.numChildren(); ++c) {
                element.child(c).connectedTo(sources, true, false);
            }
            if (sources.length() == 0) continue;

            MObject capNode = sources[0].node();
            MFnDependencyNode capFn(capNode);
            if (!isCapabilityNode(capFn)) continue;

            if (!haveSet) {
                setObj = orphanSet(modifier);
                haveSet = true;
            }

            // 능력 노드는 삭제하지 않는다. 고아 세트에 담아 재사용 가능하게 둔다.
            MFnSet setFn(setObj);
            setFn.addMember(capNode);
        }

        if (haveSet) {
            MGlobal::displayInfo(
                "Maro: capability nodes moved to maroOrphanSet for reuse.");
        }
    } catch (...) {
        MGlobal::displayError("Maro: orphan handling failed.");
    }
}

}  // namespace maro
