#include "MaroManagement.h"
#include <maya/MGlobal.h>
#include <sstream>

namespace MaroPlugin {

    MaroManagement& MaroManagement::getAssistant() {
        static MaroManagement instance;
        return instance;
    }

    MaroManagement::MaroManagement() {
        initializeKnowledgeBase();
    }

    void MaroManagement::AnalyzeAndReport(const std::string& errorContextId, const MString& runtimeDetails) {
        auto it = embeddedBrain.find(errorContextId);

        std::stringstream report;
        report << "========================================================================\n"
            << " (Robot) [Maro 수행비서 Copilot - 지능형 내부 장애 진단 보고서] \n" // 수정
            << "========================================================================\n";

        if (it != embeddedBrain.end()) {
            const auto& ctx = it->second;
            report << "(Location) [발생 경로]\n   " << ctx.internalPath << "\n\n" // 수정
                << "(Document) [연관 문서]\n   " << ctx.relatedDoc << "\n\n" // 수정
                << "(Why) [원인 분석 (Technical Why)]\n   " << ctx.technicalWhy << "\n\n" // 수정
                << "(Symptom) [장애 현상 (Symptom)]\n   " << ctx.visualSymptom << "\n\n"; // 수정
            
            if (runtimeDetails.length() > 0) {
                report << "(Info)  [런타임 상세 정보]\n   " << runtimeDetails.asChar() << "\n\n"; // 수정
            }

            report << "(Solution) [상세 해결책]\n" << ctx.copilotSolution << "\n"; // 수정
        }
        else {
            report << "(Why) [원인 분석]\n"
                   << "   알 수 없는 내부 장애입니다. 등록된 해결책이 없습니다.\n\n"
                   << "(Guide) [학습 가이드]\n"
                   << "   이 문제의 해결책을 Maro에게 가르칠 수 있습니다.\n"
                   << "   MEL 스크립트 에디터에 아래와 같이 명령어를 입력하세요.\n\n"
                   << "   MaroLearn -id \"" << errorContextId << "\" -sol \"여기에 상세한 해결책을 작성하세요...\";\n";
        }

        report << "========================================================================";

        std::string line;
        std::string fullReport = report.str();
        std::stringstream ss(fullReport);

        MGlobal::displayError("[Maro Assistant] 내부 장애 조치 리포트가 발행되었습니다. 아래 가이드를 확인하세요.");

        while (std::getline(ss, line)) {
            MString mayaLine(line.c_str());
            MGlobal::displayInfo(mayaLine);
        }
    }

} // namespace MaroPlugin