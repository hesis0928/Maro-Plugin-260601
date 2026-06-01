#pragma once
#include <filesystem>
#include <string>

namespace MaroPlugin {
    /**
     * @namespace OnfixMaro
     * @brief [Tier 1: 내부 관측] Maya In-Process 환경 정보 담당.
     */
    namespace OnfixMaro {
        void SaveFragment(const std::filesystem::path& path);
        std::string Enrich(const std::string& draft);
    };
}