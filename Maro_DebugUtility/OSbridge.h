#pragma once
#include <string>

namespace MaroPlugin { // 네임스페이스 수정
    /**
     * @namespace OSbridge
     * @brief [Tier 4: 특수 유닛] 시스템 로그 브릿지.
     */
    namespace OSbridge {
        void FlushToSystemLog(const std::string& ghostPrediction);
    }
}