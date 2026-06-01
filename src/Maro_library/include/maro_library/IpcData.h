#pragma once
#include <cstdint>
#include <atomic> // std::atomic을 위해 추가

namespace MaroPlugin {
    /**
     * @brief 플러그인, 외부 리더, 테스트 간에 공유되는 데이터의 구조와 상수를 정의합니다.
     */

     // 공유 메모리에 기록될 데이터 구조
    struct SharedImageHeader {
        uint32_t width;
        uint32_t height;
        uint32_t channels;
        std::atomic<uint64_t> frame_index; // atomic으로 변경
    };

    // 공유 메모리 관리 상수
    constexpr char SHM_NAME[] = "MayaViewportSHM";
    constexpr char MUTEX_NAME[] = "MayaViewportMutex";
    constexpr size_t MAX_RESOLUTION_BYTES = 4096 * 2160 * 4; // 4K RGBA
    constexpr size_t SHM_SIZE = sizeof(SharedImageHeader) + MAX_RESOLUTION_BYTES;

    // --- [Track C] 초고속 제어 수신용 (신규 추가) ---
    struct RobotControlData {
        std::atomic<uint64_t> command_index; // 명령 시퀀스
        bool has_transform;                  // 이동 명령 여부
        float position[3];                   // XYZ 절대 좌표
        float orientation[4];                // 회전 (Quaternion: X, Y, Z, W)

        // (추후 관절 제어용) bool has_joint; float joint_angles[64];
    };
    constexpr char CTRL_SHM_NAME[] = "MayaControlSHM";
    constexpr char CTRL_MUTEX_NAME[] = "MayaControlMutex";
    constexpr size_t CTRL_SHM_SIZE = 1024; // 1KB면 충분합니다.
};