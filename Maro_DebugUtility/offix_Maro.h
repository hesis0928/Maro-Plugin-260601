#pragma once
#include <filesystem>
#include <string>
#include <maya/MString.h>

namespace MaroPlugin {
    /**
     * @namespace OffixMaro
     * @brief [Tier 2: 사후 분석] Out-of-Process 관점의 분석 시뮬레이션.
     */
    namespace OffixMaro {
        MString DeepAnalyze(const std::string& errorHash, const MString& originalError);
        // 개선: 마지막으로 실행된 명령어를 동적으로 받음
        void SaveFragment(const std::filesystem::path& path, const std::string& lastCommand);
        std::string Finalize(const std::string& enrichedDraft);
    };
}