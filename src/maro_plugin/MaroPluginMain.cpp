#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MStatus.h>

#include "MaroAxisNode.h"
#include "MaroCapabilityNodes.h"
#include "MaroCommandDeviceNode.h"
#include "MaroCommands.h"
#include "MaroDeleteWatcher.h"

namespace {
constexpr char kVendor[] = "Maro";
constexpr char kVersion[] = "0.1.0";
}  // namespace

MStatus initializePlugin(MObject obj) {
    MFnPlugin plugin(obj, kVendor, kVersion, "Any");

    MStatus status = plugin.registerNode(
        "maroAxis",
        maro::MaroAxisNode::id,
        maro::MaroAxisNode::creator,
        maro::MaroAxisNode::initialize,
        MPxNode::kLocatorNode);
    if (!status) {
        status.perror("Maro: failed to register maroAxis");
        return status;
    }

    struct CapabilityRegistration {
        const char* name;
        MTypeId id;
        MCreatorFunction creator;
        MInitializeFunction initialize;
    };

    const CapabilityRegistration kCapabilities[] = {
        {"maroRotation", maro::MaroRotationNode::id,
         maro::MaroRotationNode::creator, maro::MaroRotationNode::initialize},
        {"maroLimit", maro::MaroLimitNode::id,
         maro::MaroLimitNode::creator, maro::MaroLimitNode::initialize},
        {"maroSensorDirection", maro::MaroSensorDirectionNode::id,
         maro::MaroSensorDirectionNode::creator,
         maro::MaroSensorDirectionNode::initialize},
        {"maroSensorRange", maro::MaroSensorRangeNode::id,
         maro::MaroSensorRangeNode::creator, maro::MaroSensorRangeNode::initialize},
    };

    for (const auto& cap : kCapabilities) {
        status = plugin.registerNode(cap.name, cap.id, cap.creator, cap.initialize);
        if (!status) {
            status.perror(MString("Maro: failed to register ") + cap.name);
            return status;
        }
    }

    status = plugin.registerCommand(
        "maroBindAxis",
        maro::MaroBindAxisCommand::creator,
        maro::MaroBindAxisCommand::newSyntax);
    if (!status) {
        status.perror("Maro: failed to register maroBindAxis");
        return status;
    }

    status = plugin.registerCommand(
        "maroSetControlMode",
        maro::MaroSetControlModeCommand::creator,
        maro::MaroSetControlModeCommand::newSyntax);
    if (!status) {
        status.perror("Maro: failed to register maroSetControlMode");
        return status;
    }

    status = plugin.registerCommand(
        "maroConnectAxis",
        maro::MaroConnectAxisCommand::creator,
        maro::MaroConnectAxisCommand::newSyntax);
    if (!status) {
        status.perror("Maro: failed to register maroConnectAxis");
        return status;
    }

    status = plugin.registerNode(
        "maroCommandDevice",
        maro::MaroCommandDeviceNode::id,
        maro::MaroCommandDeviceNode::creator,
        maro::MaroCommandDeviceNode::initialize,
        MPxNode::kThreadedDeviceNode);
    if (!status) {
        status.perror("Maro: failed to register maroCommandDevice");
        return status;
    }

    status = plugin.registerCommand("maroStartBridge",
                                    maro::MaroStartBridgeCommand::creator,
                                    maro::MaroStartBridgeCommand::newSyntax);
    if (!status) {
        status.perror("Maro: failed to register maroStartBridge");
        return status;
    }

    status = plugin.registerCommand("maroStopBridge",
                                    maro::MaroStopBridgeCommand::creator);
    if (!status) {
        status.perror("Maro: failed to register maroStopBridge");
        return status;
    }

    status = plugin.registerCommand("maroBridgeStats",
                                    maro::MaroBridgeStatsCommand::creator);
    if (!status) {
        status.perror("Maro: failed to register maroBridgeStats");
        return status;
    }

    status = maro::MaroDeleteWatcher::install();
    if (!status) {
        status.perror("Maro: failed to install delete watcher");
        return status;
    }

    MGlobal::displayInfo("Maro: plugin loaded.");
    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj) {
    MFnPlugin plugin(obj);

    maro::shutdownBridge();
    maro::MaroDeleteWatcher::uninstall();

    plugin.deregisterCommand("maroBridgeStats");
    plugin.deregisterCommand("maroStopBridge");
    plugin.deregisterCommand("maroStartBridge");
    plugin.deregisterCommand("maroConnectAxis");
    plugin.deregisterCommand("maroSetControlMode");
    plugin.deregisterCommand("maroBindAxis");

    plugin.deregisterNode(maro::MaroCommandDeviceNode::id);
    plugin.deregisterNode(maro::MaroSensorRangeNode::id);
    plugin.deregisterNode(maro::MaroSensorDirectionNode::id);
    plugin.deregisterNode(maro::MaroLimitNode::id);
    plugin.deregisterNode(maro::MaroRotationNode::id);

    MStatus status = plugin.deregisterNode(maro::MaroAxisNode::id);
    if (!status) {
        status.perror("Maro: failed to deregister maroAxis");
    }

    MGlobal::displayInfo("Maro: plugin unloaded.");
    return status;
}
