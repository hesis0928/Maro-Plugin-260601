#include "MaroManagement.h"

// 이 파일은 MaroManagement 클래스가 사용하는 정적 지식 베이스를 초기화하는 역할만 전담합니다.
// 이를 통해 MaroManagement.cpp는 순수 로직만 다루게 되어 코드가 깔끔해집니다.

namespace MaroPlugin {

void MaroManagement::initializeKnowledgeBase() {
    // [내장 지식 베이스 등록 구간]

    // 1. 공유메모리 생성 충돌
    embeddedBrain["IPC_CREATE_FAILURE"] = {
        "src/ViewportStreamer.cpp -> start()",
        "USD_IPC_plan.txt [Track B: 공유 메모리]",
        "Boost.Interprocess가 'MaroViewportSHM' 세그먼트를 생성(create_only)하려 했으나, 이전 세션의 좀비 프로세스나 다른 앱이 동일한 이름의 자원을 이미 점유하고 있어 커널 레벨에서 충돌했습니다.",
        "스트리밍 엔진이 시작되지 않고 즉시 'stopped' 로그를 출력합니다. 자동 실행되어야 할 CV창이 나타나지 않습니다.",
        "[해결 방법]\n"
        "  1. 이 오류는 시스템의 '좀비' 자원을 정리하는 과정에서 발생한 정상적인 자체 복구 과정일 수 있습니다. 'Maro' 명령어를 한 번 더 실행해 보십시오.\n"
        "  2. 지속적으로 실패한다면, 윈도우 작업 관리자에서 'shm_reader.exe' 프로세스를 찾아 강제 종료한 후 다시 시도하십시오."
    };

    // 2. 4K 해상도 오버플로우
    embeddedBrain["RESOLUTION_OVERFLOW"] = {
        "src/ViewportStreamer.cpp -> onTimer()",
        "USD_IPC_plan.txt [MAX_RESOLUTION_BYTES]",
        "현재 마야 Active 뷰포트의 해상도 버퍼 크기가 IpcData.h에 정의된 고정 버퍼 크기(4K RGBA)를 초과했습니다. 이는 버퍼 오버플로우를 유발할 수 있습니다.",
        "메모리 오염 방지를 위해 해당 프레임의 복사 작업을 강제로 스킵(Skip)합니다. 뷰포트 크기를 줄이지 않는 한 스트리밍이 일시 정지된 것처럼 보입니다.",
        "[해결 방법]\n"
        "  1. 현재 마야의 Viewport 작업 창의 크기를 마우스로 드래그하여 약간 줄여서 해상도를 낮추십시오.\n"
        "  2. 8K급 해상도 지원이 필수적이라면 'src/common/IpcData.h'의 MAX_RESOLUTION_BYTES 상수를 상향 조정하고 플러그인을 재빌드하십시오."
    };

    // (이하 다른 지식들도 모두 여기에 위치합니다...)
}

} // namespace MaroPlugin