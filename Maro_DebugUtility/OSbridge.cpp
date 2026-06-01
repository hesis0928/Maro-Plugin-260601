#include "OSbridge.h"
#include "boad_Maro.h" // BoadMaro의 로깅 기능을 사용

namespace MaroPlugin { // 네임스페이스 수정
    namespace OSbridge {
        void FlushToSystemLog(const std::string& ghostPrediction) {
            // GetInstance() 호출 제거
            BoadMaro::info(("[OSbridge] Flushing to System Log: " + ghostPrediction).c_str());
        }
    }
}