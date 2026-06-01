#include "ghost_Maro.h"
#include "boad_Maro.h"
#include "onfix_Maro.h"
#include "offix_Maro.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace MaroPlugin {
    namespace { // Anonymous namespace for private helper functions
        std::filesystem::path GetFragmentPath(const std::string& name) {
            return std::filesystem::temp_directory_path() / (name + ".marofrag");
        }

        std::string ReadFragment(const std::string& name) {
            std::ifstream ifs(GetFragmentPath(name));
            if (!ifs.is_open()) return "[Fragment Missing]";
            std::stringstream ss;
            ss << ifs.rdbuf();
            return ss.str();
        }
    }

    void GhostMaro::InitiateEmergencyFragmentSave(const std::string& lastCommand) {
        BoadMaro::warn("[Ghost] Impending shutdown detected! Commanding all units to save fragments.");
        OnfixMaro::SaveFragment(GetFragmentPath("onfix"));
        OffixMaro::SaveFragment(GetFragmentPath("offix"), lastCommand);
    }

    void GhostMaro::AssembleFragmentsOnReboot() {
        BoadMaro::info("[Ghost] System rebooted. Assembling fragments...");

        std::string onfix_frag = ReadFragment("onfix");
        std::string offix_frag = ReadFragment("offix");
        std::string draft = "[Ghost Draft] Onfix(" + onfix_frag + ") / Offix(" + offix_frag + ")";
        BoadMaro::devInfo("T0: Ghost draft assembled.");

        std::string enriched = OnfixMaro::Enrich(draft);
        BoadMaro::devInfo("T1: Onfix enriched the draft.");

        std::string finalReport = OffixMaro::Finalize(enriched);
        BoadMaro::devInfo("T2: Offix finalized the report.");

        BoadMaro::info("--- Assembled Crash Report ---\n" + MString(finalReport.c_str()));
    }
}       