// 1. 매크로 충돌 방지 (무조건 최상단)
#define NOMINMAX
#define _HAS_STD_BYTE 0

#include <memory>
#include "src/ViewportStreamer.h"
#include "src/common/IpcData.h"

// Maya API
#include <maya/MPxCommand.h>
#include <maya/MFnPlugin.h>
#include <maya/MArgParser.h>
#include <maya/MSyntax.h>
#include <maya/MGlobal.h>
#include <maya/MObject.h>

// Maro 시스템
#include "Maro_DebugUtility/boad_Maro.h"
#include "Maro_DebugUtility/ghost_Maro.h"
#include "Maro_DebugUtility/book_Maro.h"
#include "Maro_Management/MaroManagement.h"

namespace MaroPlugin {

// === MaroCmd, MaroLearnCmd, 전역 변수 등 정의 ===
static std::unique_ptr<ViewportStreamer> g_streamer = nullptr;
class MaroCmd : public MPxCommand {
public:
    static const char* kRateFlag;
    static const char* kRateFlagLong;
    static const char* kShmNameFlag;
    static const char* kShmNameFlagLong;
    static const char* kTestShutdownFlag;
    static const char* kTestShutdownFlagLong;
    static const MString commandName;

    MStatus doIt(const MArgList& args) override {
        MArgParser parser(syntax(), args);

        if (parser.isFlagSet(kTestShutdownFlag)) {
            BoadMaro::warn("--- [DEV] SIMULATING SYSTEM SHUTDOWN SCENARIO ---");
            GhostMaro::InitiateEmergencyFragmentSave(commandName.asChar());
            BoadMaro::warn("...System Crashed...");
            BoadMaro::warn("--- [DEV] SIMULATING REBOOT AND FRAGMENT ASSEMBLY ---");
            GhostMaro::AssembleFragmentsOnReboot();
            return MS::kSuccess;
        }

        if (!g_streamer) {
            g_streamer = std::make_unique<ViewportStreamer>();
        }

        if (g_streamer->isRunning()) {
            g_streamer->stop();
            return MS::kSuccess;
        }

        double rate = 1.0 / 30.0;
        if (parser.isFlagSet(kRateFlag)) {
            parser.getFlagArgument(kRateFlag, 0, rate);
        }

        // [수정] start 호출이 단순해졌습니다.
        return g_streamer->start(rate);
    }

    static MSyntax newSyntax() {
        MSyntax syntax;
        syntax.addFlag(kRateFlag, kRateFlagLong, MSyntax::kDouble);
        syntax.addFlag(kShmNameFlag, kShmNameFlagLong, MSyntax::kString);
        syntax.addFlag(kTestShutdownFlag, kTestShutdownFlagLong);
        return syntax;
    }
    static void* creator() { return new MaroCmd; }
};
const MString MaroCmd::commandName = "maro"; // ROS 2 규칙에 맞게 소문자로 변경
const char* MaroCmd::kRateFlag = "-r";
const char* MaroCmd::kRateFlagLong = "-rate";
const char* MaroCmd::kShmNameFlag = "-shm";
const char* MaroCmd::kShmNameFlagLong = "-sharedMemoryName";
const char* MaroCmd::kTestShutdownFlag = "-tst";
const char* MaroCmd::kTestShutdownFlagLong = "-testShutdown";


class MaroLearnCmd : public MPxCommand {
public:
    static const char* kIdFlag;
    static const char* kIdFlagLong;
    static const char* kSolutionFlag;
    static const char* kSolutionFlagLong;
    static const MString commandName;

    MStatus doIt(const MArgList& args) override {
        MArgParser parser(syntax(), args);
        if (!parser.isFlagSet(kIdFlag) || !parser.isFlagSet(kSolutionFlag)) {
            displayError("Both -id and -solution flags must be provided.");
            return MS::kFailure;
        }
        MString id, solution;
        parser.getFlagArgument(kIdFlag, 0, id);
        parser.getFlagArgument(kSolutionFlag, 0, solution);

        BookMaro::getInstance().SaveLogPermanently(id.asChar(), solution);
        return MS::kSuccess;
    }

    static MSyntax newSyntax() {
        MSyntax syntax;
        syntax.addFlag(kIdFlag, kIdFlagLong, MSyntax::kString);
        syntax.addFlag(kSolutionFlag, kSolutionFlagLong, MSyntax::kString);
        return syntax;
    }
    static void* creator() { return new MaroLearnCmd; }
};
const MString MaroLearnCmd::commandName = "maroLearn"; // 소문자 시작으로 변경
const char* MaroLearnCmd::kIdFlag = "-id";
const char* MaroLearnCmd::kIdFlagLong = "-identifier";
const char* MaroLearnCmd::kSolutionFlag = "-sol";
const char* MaroLearnCmd::kSolutionFlagLong = "-solution";

} // namespace MaroPlugin


MStatus initializePlugin(MObject obj) {
    MFnPlugin plugin(obj, "MaroDeveloper", "1.0", "Any");

    MaroPlugin::BookMaro::getInstance(); 

    MStatus status = plugin.registerCommand(MaroPlugin::MaroCmd::commandName, MaroPlugin::MaroCmd::creator, MaroPlugin::MaroCmd::newSyntax);
    if (!status) {
        status.perror("Failed to register maro command");
        return status;
    }
    status = plugin.registerCommand(MaroPlugin::MaroLearnCmd::commandName, MaroPlugin::MaroLearnCmd::creator, MaroPlugin::MaroLearnCmd::newSyntax);
    if (!status) {
        status.perror("Failed to register maroLearn command");
        return status;
    }
    return status;
}

MStatus uninitializePlugin(MObject obj) {
    MFnPlugin plugin(obj);
    MaroPlugin::g_streamer.reset(); 
    
    MStatus status = plugin.deregisterCommand(MaroPlugin::MaroCmd::commandName);
    if (!status) {
        status.perror("Failed to deregister maro command");
        return status;
    }
    status = plugin.deregisterCommand(MaroPlugin::MaroLearnCmd::commandName);
    if (!status) {
        status.perror("Failed to deregister maroLearn command");
        return status;
    }
    return status;
}