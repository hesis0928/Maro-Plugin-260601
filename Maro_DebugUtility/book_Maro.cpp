#include "book_Maro.h"
#include "boad_Maro.h"
#include <fstream>
#include <string> // std::to_string을 위해 추가
#include <maya/MGlobal.h>
#include <nlohmann/json.hpp>

namespace MaroPlugin {

    BookMaro::BookMaro() {
        MString userAppDir = MGlobal::executeCommandStringResult("internalVar -userAppDir");
        if (userAppDir.length() > 0) {
            dbPath = std::filesystem::path(userAppDir.asChar()) / "maro_knowledge.json";
        } else {
            dbPath = std::filesystem::temp_directory_path() / "maro_knowledge.json";
        }
        LoadKnowledgeFromFile();
    }

    BookMaro& BookMaro::getInstance() {
        static BookMaro instance;
        return instance;
    }

    void BookMaro::LoadKnowledgeFromFile() {
        if (dbPath.empty()) {
            BoadMaro::warn("Knowledge base path is invalid. Learning is disabled.");
            return;
        }
        if (!std::filesystem::exists(dbPath)) {
            BoadMaro::devInfo("Knowledge base file not found. Starting with a fresh brain.");
            return;
        }
        std::ifstream ifs(dbPath);
        nlohmann::json j;
        try {
            ifs >> j;
            for (auto& [key, val] : j.items()) {
                knowledgeDB[key] = MString(val.get<std::string>().c_str());
            }
            // [수정] std::to_string을 사용하여 안전하게 형 변환합니다.
            BoadMaro::devInfo("Successfully loaded " + MString(std::to_string(knowledgeDB.size()).c_str()) + " memories from the knowledge base.");
        } catch (const nlohmann::json::exception& e) {
            BoadMaro::warn(MString("Failed to parse knowledge base file: ") + e.what());
        }
    }

    void BookMaro::PersistKnowledgeToFile() {
        // [수정] 파일 경로가 비어있는 경우를 방지합니다.
        if (dbPath.empty()) return;

        nlohmann::json j;
        for (const auto& [key, val] : knowledgeDB) {
            j[key] = val.asChar();
        }
        
        // [수정] 파일 쓰기 작업 전체를 try-catch로 감싸 예외를 처리합니다.
        try {
            std::ofstream ofs(dbPath);
            ofs << j.dump(4);
        } catch (const std::exception& e) {
            MString errorMsg = "Failed to write to knowledge base file: ";
            errorMsg += dbPath.string().c_str();
            errorMsg += " | Error: ";
            errorMsg += e.what();
            MGlobal::displayWarning(errorMsg); // BoadMaro를 사용하면 무한 루프에 빠질 수 있으므로 MGlobal 직접 사용
        }
    }

    bool BookMaro::QueryLog(const std::string& errorHash, MString& outLog) {
        auto it = knowledgeDB.find(errorHash);
        if (it != knowledgeDB.end()) {
            outLog = it->second;
            return true;
        }
        return false;
    }

    void BookMaro::SaveLogPermanently(const std::string& errorHash, const MString& logData) {
        knowledgeDB[errorHash] = logData;
        PersistKnowledgeToFile();
        BoadMaro::info("[BookMaro] A new solution has been learned and saved permanently.");
    }
}