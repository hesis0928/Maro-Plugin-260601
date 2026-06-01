#pragma once
#include <maya/MString.h>
#include "MaroTypes.h" // ViewportStreamer 전방 선언을 위해 포함
#include <cassert>

// boad_Maro.h 파일 내부

#include <cassert>
// (BoadMaro 클래스 선언이 있는 곳 아래에 작성)

#ifndef MARO_ASSERT
#define MARO_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            /* Maya 스크립트 에디터에 에러 메시지 출력 */ \
            BoadMaro::error("ASSERT_FAILED", msg); \
            /* 프로그램 실행 중단 */ \
            assert(false && (msg)); \
        } \
    } while (0)
#endif

namespace MaroPlugin { // 수정
    /**
     * @class BoadMaro
     * @brief [Tier 4: 시각화] Maro 시스템의 중앙 로깅 인터페이스.
     */
    class BoadMaro {
    public:
        static void info(const MString& message);
        static void warn(const MString& message);
        static void devInfo(const MString& message);
        static void error(const std::string& errorHash, const MString& detailedMessage);

        // 선언만 남겨두고 구현은 ViewportStreamer를 아는 곳으로 이동
        static MString dumpState(const ViewportStreamer* streamer);
    };
}