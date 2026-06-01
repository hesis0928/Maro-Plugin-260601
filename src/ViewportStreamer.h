#pragma once

#include "IpcData.h" // 경로를 프로젝트 구조에 맞게 수정했습니다.
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MMessage.h>
#include <string>
#include <memory>
#include <atomic>

// [필수] Track C 구현을 위해 Boost IPC 헤더가 선언부에 포함되어야 합니다.
#include <boost/interprocess/windows_shared_memory.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>

namespace MaroPlugin {

    class ViewportStreamer {
    public:
        ViewportStreamer();
        ~ViewportStreamer();

        ViewportStreamer(const ViewportStreamer&) = delete;
        ViewportStreamer& operator=(const ViewportStreamer&) = delete;

        MStatus start(double period);
        void stop();
        bool isRunning() const;

    private:
        void streamUpdate(); // 구현체와 이름을 맞췄습니다 (onTimer -> streamUpdate)
        static void timerCallback(float, float, void* clientData);

        std::atomic<bool> m_isRunning;
        MCallbackId m_callbackId;

        // Track B: 뷰포트 전송용 메모리
        std::unique_ptr<boost::interprocess::windows_shared_memory> m_shm;
        std::unique_ptr<boost::interprocess::mapped_region> m_region;
        std::unique_ptr<boost::interprocess::named_mutex> m_mutex;
        SharedImageHeader* m_pHeader;

        // [핵심] Track C 구현을 위한 멤버 변수 추가
        std::unique_ptr<boost::interprocess::windows_shared_memory> m_ctrl_shm;
        std::unique_ptr<boost::interprocess::mapped_region> m_ctrl_region;
        std::unique_ptr<boost::interprocess::named_mutex> m_ctrl_mutex;
        uint64_t m_lastCommandIndex;
    };

} // namespace MaroPlugin
```

-- -

### 검증 및 빌드 절차

이 헤더를 수정하면 `rosSimCmd.cpp`의 구현부와 구조적으로 완벽하게 일치하게 됩니다.

1. * *헤더 수정 : **위의 `ViewportStreamer.h`를 덮어씌우세요.
2. * *`control_bridge` 패키지 빌드 : **지난번 설정한 `CMakeLists.txt`가 이제 TIFF / OpenCV 의존성을 명시적으로 처리하므로, 아래 명령어를 실행하면 빌드가 성공할 것입니다.
```cmd
rmdir / s / q build\control_bridge
colcon build --packages - select control_bridge --cmake - args - DCMAKE_TOOLCHAIN_FILE = C: / src / vcpkg / scripts / buildsystems / vcpkg.cmake