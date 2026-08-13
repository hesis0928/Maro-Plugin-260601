#include "MaroRosRuntime.h"

#include <chrono>

#include <rclcpp/rclcpp.hpp>

#include <geometry_msgs/msg/transform_stamped.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <tf2_msgs/msg/tf_message.hpp>

#include "maro_transform/Convert.h"

namespace maro {

struct MaroRosRuntime::Impl {
    std::shared_ptr<rclcpp::Node> node;
    rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr jointPub;
    rclcpp::Publisher<tf2_msgs::msg::TFMessage>::SharedPtr tfPub;
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
        // 상대 토픽 이름("joint_states")은 노드의 네임스페이스(기본 "/")
        // 아래로 풀려 "/joint_states"가 된다 -- 노드 *이름*은 토픽 해석에
        // 관여하지 않는다. MaroCommandDeviceNode.cpp(Task 10, 수신 방향)가
        // 이미 "/" + robotName + "/joint_commands"로 완전한 절대 경로를
        // 쓰고 있으므로, 발행 방향도 같은 관례를 따라야 두 방향이 같은
        // "/<robotName>/..." 네임스페이스 아래 나란히 선다.
        m_impl->jointPub =
            m_impl->node->create_publisher<sensor_msgs::msg::JointState>(
                "/" + robotName + "/joint_states", 10);
        m_impl->tfPub =
            m_impl->node->create_publisher<tf2_msgs::msg::TFMessage>("/tf", 10);
    } catch (const std::exception&) {
        // 예외가 Maya 쪽으로 새지 않게 여기서 막는다.
        m_impl->tfPub.reset();
        m_impl->jointPub.reset();
        m_impl->node.reset();
        return false;
    } catch (...) {
        m_impl->tfPub.reset();
        m_impl->jointPub.reset();
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
    m_impl->tfPub.reset();
    m_impl->jointPub.reset();
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
            // Task 10에서는 개수만 셌다. 이제 실제로 발행한다.
            drainAndPublish();

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (...) {
        // 스레드에서 예외가 새면 조용히 죽어 진단이 어려워진다.
        m_stopRequested.store(true);
    }
}

void MaroRosRuntime::drainAndPublish() {
    const std::vector<AxisSample> samples = m_publishQueue.drain();
    if (samples.empty()) return;

    m_drainedSamples.fetch_add(samples.size(), std::memory_order_relaxed);

    if (!m_impl->jointPub || !m_impl->tfPub) return;

    sensor_msgs::msg::JointState joints;
    joints.header.stamp = m_impl->node->now();

    tf2_msgs::msg::TFMessage tf;

    for (const AxisSample& sample : samples) {
        joints.name.push_back(sample.jointName);
        joints.position.push_back(sample.value);

        const Vec3 p = mayaToRosPosition(sample.position, sample.unit);
        const Quat q = mayaToRosRotation(sample.rotation);

        geometry_msgs::msg::TransformStamped t;
        t.header.stamp = joints.header.stamp;
        t.header.frame_id = "world";
        t.child_frame_id = sample.jointName;
        t.transform.translation.x = p.x;
        t.transform.translation.y = p.y;
        t.transform.translation.z = p.z;
        t.transform.rotation.x = q.x;
        t.transform.rotation.y = q.y;
        t.transform.rotation.z = q.z;
        t.transform.rotation.w = q.w;
        tf.transforms.push_back(t);
    }

    m_impl->jointPub->publish(joints);
    m_impl->tfPub->publish(tf);
}

}  // namespace maro
