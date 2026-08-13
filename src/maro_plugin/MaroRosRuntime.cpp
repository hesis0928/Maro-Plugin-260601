#include "MaroRosRuntime.h"

#include <chrono>

#include <rclcpp/rclcpp.hpp>

namespace maro {

struct MaroRosRuntime::Impl {
    std::shared_ptr<rclcpp::Node> node;
};

MaroRosRuntime::MaroRosRuntime() : m_impl(std::make_unique<Impl>()) {}

MaroRosRuntime::~MaroRosRuntime() {
    stop();
}

bool MaroRosRuntime::start(const std::string& robotName) {
    if (m_running.load()) return true;

    try {
        if (!rclcpp::ok()) {
            rclcpp::init(0, nullptr);
        }
        m_impl->node = rclcpp::Node::make_shared(robotName);
    } catch (const std::exception&) {
        // 예외가 Maya 쪽으로 새지 않게 여기서 막는다.
        m_impl->node.reset();
        return false;
    } catch (...) {
        m_impl->node.reset();
        return false;
    }

    m_stopRequested.store(false);
    m_running.store(true);
    m_thread = std::thread(&MaroRosRuntime::spinLoop, this);
    return true;
}

void MaroRosRuntime::stop() {
    if (!m_running.load()) return;

    m_stopRequested.store(true);
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_running.store(false);

    // 순서가 중요하다. 노드 내부를 참조하는 것들을 먼저 놓아야
    // DDS 참가자가 살아남아 프로세스가 안 끝나는 일이 없다.
    m_impl->node.reset();

    // 이 rclcpp::shutdown()은 프로세스 전역 rclcpp 컨텍스트를 끝낸다.
    // MaroCommandDeviceNode도 같은 전역 컨텍스트로 자기 노드를 만들므로,
    // 그쪽 스레드가 완전히 멈춘 뒤에만 여기까지 와야 한다. 호출자
    // (MaroCommands.cpp의 shutdownBridge())가 그 순서를 보장한다 — 여기서
    // 순서를 어기면 살아있는 스레드 밑에서 컨텍스트가 끊겨 크래시하거나
    // 프로세스가 끝나지 않는다.
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
}

void MaroRosRuntime::spinLoop() {
    try {
        while (!m_stopRequested.load() && rclcpp::ok()) {
            // 이 노드는 퍼블리셔만 갖는다 — 구독은 MaroCommandDeviceNode
            // 쪽의 별도 노드가 처리한다 (Task 10 설계 노트). 퍼블리셔는
            // spin 없이도 publish()가 바로 나가므로 여기서
            // rclcpp::spin_some()을 부를 필요가 없다.
            //
            // 펌프가 넣은 샘플을 꺼낸다. 발행은 Task 11에서 붙는다.
            // 지금은 건너온 개수만 세어 흐름을 관측 가능하게 한다.
            const std::vector<AxisSample> samples = m_publishQueue.drain();
            m_drainedSamples.fetch_add(samples.size(), std::memory_order_relaxed);

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (...) {
        // 스레드에서 예외가 새면 조용히 죽어 진단이 어려워진다.
        m_stopRequested.store(true);
    }
}

}  // namespace maro
