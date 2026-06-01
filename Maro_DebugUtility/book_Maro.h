#pragma once
#include <unordered_map>
#include <string>
#include <filesystem> // 추가
#include <maya/MString.h>

namespace MaroPlugin {
    class BookMaro {
    private:
        std::unordered_map<std::string, MString> knowledgeDB;
        std::filesystem::path dbPath; // 추가: DB 파일 경로

        BookMaro(); // private으로 변경
        void PersistKnowledgeToFile(); // 추가: 파일로 저장
        void LoadKnowledgeFromFile(); // 추가: 파일에서 로드

    public:
        static BookMaro& getInstance();
        BookMaro(const BookMaro&) = delete;
        void operator=(const BookMaro&) = delete;

        bool QueryLog(const std::string& errorHash, MString& outLog);
        void SaveLogPermanently(const std::string& errorHash, const MString& logData);
    };
}