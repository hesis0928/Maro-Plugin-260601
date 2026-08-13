#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "MaroBridgeQueues.h"

namespace rclcpp {
class Node;
}

namespace maro {

// 발행 방향(Maya -> ROS 2) 전담. rclcpp는 이 클래스가 만든 백그라운드
// 스레드에서만 돈다. 이 클래스의 어떤 코드도 Maya API를 호출하지 않는다.
//
// 수신 방향(ROS 2 -> Maya)은 이 클래스가 아니라 MaroCommandDeviceNode가
// Maya가 관리하는 별도 스레드에서 처리한다 (Task 10 설계 노트 참고).
// 그래서 여기엔 commandQueue가 없다 — 두 방향이 서로 다른 스레드 소유권을
// 갖는다는 뜻이다.
class MaroRosRuntime {
public:
    MaroRosRuntime();
    ~MaroRosRuntime();

    MaroRosRuntime(const MaroRosRuntime&) = delete;
    MaroRosRuntime& operator=(const MaroRosRuntime&) = delete;

    bool start(const std::string& robotName);
    void stop();
    bool isRunning() const { return m_running.load(); }

    BoundedQueue<AxisSample>& publishQueue() { return m_publishQueue; }

    // 펌프가 넣은 샘플이 백그라운드까지 실제로 건너왔는지 보기 위한 계수기.
    // 발행이 붙기 전에도 스레드 경계를 넘는 흐름을 관측할 수 있다.
    std::uint64_t drainedSampleCount() const { return m_drainedSamples.load(); }

private:
    void spinLoop();

    // 메인 스레드가 채운 샘플을 백그라운드에서 변환·발행한다.
    void drainAndPublish();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<std::uint64_t> m_drainedSamples{0};

    BoundedQueue<AxisSample> m_publishQueue;
};

}  // namespace maro
