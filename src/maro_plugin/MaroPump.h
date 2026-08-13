#pragma once

#include <atomic>
#include <cstdint>

#include <maya/MCallbackIdArray.h>
#include <maya/MStatus.h>

namespace maro {

class MaroRosRuntime;

// Maya 메인 스레드에서만 도는 발행 펌프. DG에서 축 상태를 읽어 큐에
// 적재하는, 발행 방향 전담이다.
// 변환에 필요한 컨텍스트(씬 단위, 축 보정)는 Maya 조회가 필요하므로
// 여기서 읽어 샘플에 함께 실어 보낸다. 백그라운드는 Maya를 조회하지 않는다.
//
// 수신 방향(명령 적용)은 더 이상 여기 없다. MaroCommandDeviceNode::compute()가
// 대신한다 — 이 펌프가 만든 타이머가 아니라 devkit이 관리하는 dirty 전파로
// 불린다 (Task 10 설계 노트 참고).
class MaroPump {
public:
    static MStatus start(MaroRosRuntime& runtime);
    static MStatus stop();
    static bool isRunning();

    static std::uint64_t collectedSampleCount();

private:
    static void onTimer(float elapsed, float last, void* clientData);
    static void collectSamples(MaroRosRuntime& runtime);

    static MCallbackId s_timerId;
    static MaroRosRuntime* s_runtime;
    static std::atomic<std::uint64_t> s_collected;
};

}  // namespace maro
