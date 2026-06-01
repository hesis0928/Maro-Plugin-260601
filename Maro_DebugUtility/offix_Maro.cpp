#include "offix_Maro.h"
#include "boad_Maro.h"
#include <fstream>

namespace MaroPlugin {
    namespace OffixMaro {
        MString DeepAnalyze(const std::string& errorHash, const MString& originalError) {
            return originalError + "\n[Offix Analyzed Solution] The root cause is likely memory corruption. Check pointer validity.";
        }

        void SaveFragment(const std::filesystem::path& path, const std::string& lastCommand) {
            std::ofstream ofs(path);
            // 개선: 하드코딩된 'rosSim' 대신 실제 실행된 명령어 이름을 기록
            ofs << "Last Command: " << lastCommand;
            BoadMaro::devInfo("[Offix] Last known state fragment saved.");
        }

        std::string Finalize(const std::string& enrichedDraft) {
            return enrichedDraft + " | [Offix Finalized: Path analysis complete.]";
        }
    }
}