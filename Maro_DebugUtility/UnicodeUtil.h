#pragma once

#include <string>
#include <fstream>
#include <Windows.h> // Windows API 헤더

namespace MaroPlugin {
namespace UnicodeUtil {

    /**
     * @brief UTF-8 인코딩의 std::string을 Windows API가 사용하는 UTF-16(wchar_t) std::wstring으로 변환합니다.
     */
    inline std::wstring Utf8ToWide(const std::string& str_utf8) {
        if (str_utf8.empty()) {
            return std::wstring();
        }
        // 변환에 필요한 버퍼 크기 계산
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str_utf8[0], (int)str_utf8.size(), NULL, 0);
        std::wstring wstr_to(size_needed, 0);
        // 실제 변환 수행
        MultiByteToWideChar(CP_UTF8, 0, &str_utf8[0], (int)str_utf8.size(), &wstr_to[0], size_needed);
        return wstr_to;
    }

    /**
     * @brief UTF-8 문자열 내용을 지정된 경로에 UTF-8 인코딩 파일로 안전하게 저장합니다.
     *        경로에 유니코드(한글 등)가 포함되어 있어도 정상적으로 작동합니다.
     * @param path_utf8 파일 경로 (UTF-8)
     * @param content_utf8 파일 내용 (UTF-8)
     * @return 성공 시 true, 실패 시 false
     */
    inline bool WriteUtf8File(const std::string& path_utf8, const std::string& content_utf8) {
        // 1. 유니코드 경로 처리를 위해 UTF-8 경로를 UTF-16으로 변환
        std::wstring path_wide = Utf8ToWide(path_utf8);
        if (path_wide.empty()) {
            return false;
        }

        // 2. std::ofstream을 wide-character 경로로 열어 Windows API와의 호환성을 보장
        std::ofstream file_stream(path_wide, std::ios::binary);
        if (!file_stream.is_open()) {
            return false;
        }

        // 3. (권장) UTF-8 BOM(Byte Order Mark)을 파일 맨 앞에 추가하여
        //    메모장 같은 Windows 프로그램들이 파일을 UTF-8로 잘 인식하도록 함
        file_stream.write("\xEF\xBB\xBF", 3);

        // 4. UTF-8 내용을 파일에 쓴다
        file_stream.write(content_utf8.c_str(), content_utf8.length());

        file_stream.close();
        return file_stream.good();
    }

} // namespace UnicodeUtil
} // namespace MaroPlugin