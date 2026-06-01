#pragma once

#include <string> // std::string 사용을 위해 추가

namespace MaroPlugin {
    /**
     * @class GhostMaro
     * @brief [Tier 3: 예측 및 조립] 재난 지휘관.
     */
    class GhostMaro {
    public:
        static void InitiateEmergencyFragmentSave(const std::string& lastCommand);
        static void AssembleFragmentsOnReboot();
    };
}