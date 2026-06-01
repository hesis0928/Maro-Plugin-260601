#include "boad_Maro.h"
#include "book_Maro.h"
#include "offix_Maro.h"
#include "src/ViewportStreamer.h" // ViewportStreamer 전체 정의를 위해 추가
#include <maya/MGlobal.h>
#include <sstream>

namespace MaroPlugin {

    void BoadMaro::info(const MString& message) { MGlobal::displayInfo("[Boad-Info] " + message); }
    void BoadMaro::warn(const MString& message) { MGlobal::displayWarning("[Boad-Warn] " + message); }
    void BoadMaro::devInfo(const MString& message) {
#if _DEBUG
        MGlobal::displayInfo("[Boad-Dev] " + message);
#endif
    }

    void BoadMaro::error(const std::string& errorHash, const MString& detailedMessage) {
        MString finalMessage;
        if (BookMaro::getInstance().QueryLog(errorHash, finalMessage)) {
            finalMessage += "\n(INFO: Cached solution from BookMaro retrieved.)";
            MGlobal::displayError("[Boad-Error] " + finalMessage);
        } else {
            finalMessage = OffixMaro::DeepAnalyze(errorHash, detailedMessage);
            MGlobal::displayError("[Boad-Error] " + finalMessage);
            BookMaro::getInstance().SaveLogPermanently(errorHash, finalMessage);
        }
    }

    // BoadMaro::dumpState 구현은 ViewportStreamer.cpp 파일로 이전되었으므로
    // 이 파일에서는 해당 구현을 제거합니다.
    // MString BoadMaro::dumpState(const ViewportStreamer* streamer) { ... }
}