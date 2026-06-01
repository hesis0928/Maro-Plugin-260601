#pragma once
#include <maya/MString.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace MaroPlugin {

    // 문제 지점을 인공지능처럼 정밀 추적하기 위한 컨텍스트 구조체
    struct MaroKnowledge {
        std::string internalPath;    // 문제가 발생한 소스 코드 내부 실 경로
        std::string relatedDoc;      // 관련 설계 문서 및 장 번호
        std::string technicalWhy;    // 왜 이 문제가 발생했는지에 대한 심층적 원인
        std::string visualSymptom;   // 이로 인해 사용자가 겪는 현상 (어떻게 안 되는지)
        std::string copilotSolution; // 상세한 단계별 해결 방안 및 추천 코드/명령어
    };

    /**
     * @class MaroManagement
     * @brief [Maro 전용 비서] 내장된 소스/설계 지식을 기반으로 문제 지점과 해결책을 가이드하는 유틸리티.
     */
    class MaroManagement {
    private:
        std::unordered_map<std::string, MaroKnowledge> embeddedBrain;
        MaroManagement();
        void initializeKnowledgeBase(); // 지식 베이스 초기화 함수 선언 추가

    public:
        static MaroManagement& getAssistant();

        MaroManagement(const MaroManagement&) = delete;
        void operator=(const MaroManagement&) = delete;

        // 마야 스크립트나 명령어 도중 발생한 에러 코드를 분석하여 Copilot 리포트 출력
        void AnalyzeAndReport(const std::string& errorContextId, const MString& runtimeDetails = "");
    };

} // namespace MaroPlugin