#include "onfix_Maro.h"
#include "boad_Maro.h"
#include <fstream>
#include <maya/MGlobal.h>
#include <maya/MAnimControl.h> // 현재 프레임 정보를 얻기 위해 추가

namespace MaroPlugin {
    namespace OnfixMaro {
        void SaveFragment(const std::filesystem::path& path) {
            std::ofstream ofs(path);
            // 개선: 고정된 값 대신 실제 Maya의 상태(현재 시간)를 기록
            ofs << "Maya Time: " << MAnimControl::currentTime().as(MTime::kSeconds) << "s";
            BoadMaro::devInfo("[Onfix] Internal state fragment saved.");
        }

        std::string Enrich(const std::string& draft) {
            // 개선: 시뮬레이션에 현재 플러그인 버전 정보 추가
            return draft + " | [Onfix Enriched: PluginVersion=1.0]";
        }
    }
}