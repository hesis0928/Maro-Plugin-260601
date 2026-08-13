// ROS 2 상대역. 이 환경에는 ros2 CLI도 rclpy도 없으므로 C++로 만든다.
// 플러그인이 이미 링크하는 것과 같은 라이브러리를 쓰므로 추가 의존성이 없고,
// 대기와 타임아웃을 테스트가 직접 통제해 CLI보다 결정적이다.
//
// 사용법:
//   maro_test_peer echo <robotName> <expectedJointCount> <timeoutSeconds>
//   maro_test_peer pub  <robotName> <jointName> <position>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>

namespace {

int runEcho(const std::string& robot, std::size_t expectedJoints, double timeoutSec) {
    auto node = rclcpp::Node::make_shared("maro_test_peer_echo");

    bool satisfied = false;
    auto sub = node->create_subscription<sensor_msgs::msg::JointState>(
        "/" + robot + "/joint_states", 10,
        [&](sensor_msgs::msg::JointState::SharedPtr msg) {
            if (msg->name.size() < expectedJoints) return;
            for (std::size_t i = 0; i < msg->name.size(); ++i) {
                std::cout << "joint " << msg->name[i] << " = "
                          << msg->position[i] << std::endl;
            }
            satisfied = true;
        });

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(timeoutSec * 1000));

    while (rclcpp::ok() && !satisfied &&
           std::chrono::steady_clock::now() < deadline) {
        rclcpp::spin_some(node);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!satisfied) {
        std::cerr << "timeout: no joint_states with >= " << expectedJoints
                  << " joints" << std::endl;
        return 1;
    }
    std::cout << "echo OK" << std::endl;
    return 0;
}

int runPub(const std::string& robot, const std::string& joint, double position) {
    auto node = rclcpp::Node::make_shared("maro_test_peer_pub");
    auto pub = node->create_publisher<sensor_msgs::msg::JointState>(
        "/" + robot + "/joint_commands", 10);

    sensor_msgs::msg::JointState msg;
    msg.name.push_back(joint);
    msg.position.push_back(position);

    // 구독자가 붙을 시간을 준다.
    for (int i = 0; i < 50; ++i) {
        pub->publish(msg);
        rclcpp::spin_some(node);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::cout << "pub OK" << std::endl;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: maro_test_peer <echo|pub> <robotName> ..." << std::endl;
        return 2;
    }

    rclcpp::init(argc, argv);

    const std::string mode = argv[1];
    const std::string robot = argv[2];
    int result = 2;

    if (mode == "echo" && argc == 5) {
        result = runEcho(robot, std::strtoul(argv[3], nullptr, 10),
                         std::strtod(argv[4], nullptr));
    } else if (mode == "pub" && argc == 5) {
        result = runPub(robot, argv[3], std::strtod(argv[4], nullptr));
    } else {
        std::cerr << "bad arguments" << std::endl;
    }

    rclcpp::shutdown();
    return result;
}
