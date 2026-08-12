#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MStatus.h>

#include "MaroAxisNode.h"
#include "MaroCommands.h"

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

    status = plugin.registerCommand(
        "maroBindAxis",
        maro::MaroBindAxisCommand::creator,
        maro::MaroBindAxisCommand::newSyntax);
    if (!status) {
        status.perror("Maro: failed to register maroBindAxis");
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

    MGlobal::displayInfo("Maro: plugin loaded.");
    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj) {
    MFnPlugin plugin(obj);

    plugin.deregisterCommand("maroConnectAxis");
    plugin.deregisterCommand("maroBindAxis");

    MStatus status = plugin.deregisterNode(maro::MaroAxisNode::id);
    if (!status) {
        status.perror("Maro: failed to deregister maroAxis");
    }

    MGlobal::displayInfo("Maro: plugin unloaded.");
    return status;
}
