// ROS 2 상대역. 이 환경에는 ros2 CLI도 rclpy도 없으므로 C++로 만든다.
// 플러그인이 이미 링크하는 것과 같은 라이브러리를 쓰므로 추가 의존성이 없고,
// 대기와 타임아웃을 테스트가 직접 통제해 CLI보다 결정적이다.
//
// 사용법:
//   maro_test_peer echo <robotName> <expectedJointCount> <timeoutSeconds>
//   maro_test_peer pub  <robotName> <jointName> <position>
//   maro_test_peer tf   <childFrameId> <timeoutSeconds>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <tf2_msgs/msg/tf_message.hpp>

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

int runTf(const std::string& childFrameId, double timeoutSec) {
    auto node = rclcpp::Node::make_shared("maro_test_peer_tf");

    bool satisfied = false;
    // /tf는 관례상 로봇 이름으로 네임스페이스되지 않는 전역 토픽이다
    // (MaroRosRuntime::start()가 "/" + robotName + "/joint_states"와 달리
    // "/tf"를 그대로 쓴다). 여기서도 같은 관례를 따라 절대 경로 "/tf"를
    // 그대로 구독한다 -- robotName을 끼워 넣으면 아무것도 받지 못한다.
    auto sub = node->create_subscription<tf2_msgs::msg::TFMessage>(
        "/tf", 10,
        [&](tf2_msgs::msg::TFMessage::SharedPtr msg) {
            for (const auto& t : msg->transforms) {
                if (t.child_frame_id != childFrameId) continue;
                // 테스트가 파싱할 수 있는 고정된 형태로 찍는다 --
                // runEcho()의 "joint <name> = <value>" 관례와 같은 이유:
                // "뭔가 받았다"가 아니라 실제 값을 단언하게 한다.
                std::cout << "tf " << t.child_frame_id << " translation = "
                          << t.transform.translation.x << " "
                          << t.transform.translation.y << " "
                          << t.transform.translation.z << std::endl;
                std::cout << "tf " << t.child_frame_id << " rotation = "
                          << t.transform.rotation.x << " "
                          << t.transform.rotation.y << " "
                          << t.transform.rotation.z << " "
                          << t.transform.rotation.w << std::endl;
                satisfied = true;
                break;
            }
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
        std::cerr << "timeout: no /tf transform with child_frame_id '"
                   << childFrameId << "'" << std::endl;
        return 1;
    }
    std::cout << "tf OK" << std::endl;
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
        std::cerr << "usage: maro_test_peer <echo|pub> <robotName> ...\n"
                     "       maro_test_peer tf <childFrameId> <timeoutSeconds>"
                  << std::endl;
        return 2;
    }

    rclcpp::init(argc, argv);

    const std::string mode = argv[1];
    int result = 2;

    if (mode == "echo" && argc == 5) {
        result = runEcho(argv[2], std::strtoul(argv[3], nullptr, 10),
                         std::strtod(argv[4], nullptr));
    } else if (mode == "pub" && argc == 5) {
        result = runPub(argv[2], argv[3], std::strtod(argv[4], nullptr));
    } else if (mode == "tf" && argc == 4) {
        // /tf는 로봇 이름으로 네임스페이스되지 않으므로(runTf() 주석 참고)
        // echo/pub과 달리 robotName 인자가 없다: <childFrameId> <timeoutSeconds>뿐.
        result = runTf(argv[2], std::strtod(argv[3], nullptr));
    } else {
        std::cerr << "bad arguments" << std::endl;
    }

    rclcpp::shutdown();
    return result;
}
