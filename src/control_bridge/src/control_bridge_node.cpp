#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/pose.hpp"
#include "maro_library/IpcData.h"

#include <boost/interprocess/windows_shared_memory.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

using std::placeholders::_1;

class ControlBridgeNode : public rclcpp::Node {
public:
    ControlBridgeNode() : Node("control_bridge_node") {
        try {
            // Maya가 먼저 켜져서 만들어둔 제어 메모리(Track C)에 연결합니다.
            m_mutex = std::make_unique<boost::interprocess::named_mutex>(boost::interprocess::open_only, MaroPlugin::CTRL_MUTEX_NAME);
            m_shm = std::make_unique<boost::interprocess::windows_shared_memory>(boost::interprocess::open_only, MaroPlugin::CTRL_SHM_NAME, boost::interprocess::read_write);
            m_region = std::make_unique<boost::interprocess::mapped_region>(*m_shm, boost::interprocess::read_write);

            m_ctrlData = static_cast<MaroPlugin::RobotControlData*>(m_region->get_address());
            RCLCPP_INFO(this->get_logger(), "Successfully connected to Maya Control SHM (Track C).");

        }
        catch (const boost::interprocess::interprocess_exception& e) {
            RCLCPP_FATAL(this->get_logger(), "Failed to connect to SHM: %s. Is Maya running with 'rosSim'?", e.what());
            // 에러 발생 시 main 함수의 catch 블록으로 던져서 안전하게 종료되도록 수정
            throw std::runtime_error("Maya 공유 메모리(SHM)에 연결할 수 없습니다. Maya가 먼저 실행되어 있는지 확인하세요.");
        }

        // ROS2 토픽 구독 설정 (/robot_pose 토픽을 받습니다)
        m_subscriber = this->create_subscription<geometry_msgs::msg::Pose>(
            "/robot_pose", 10, std::bind(&ControlBridgeNode::poseCallback, this, _1));

        RCLCPP_INFO(this->get_logger(), "Listening to /robot_pose topic...");
    }

private:
    void poseCallback(const geometry_msgs::msg::Pose::SharedPtr msg) {
        if (!m_ctrlData || !m_mutex) return;

        // 동시 접근을 막기 위한 뮤텍스 잠금
        boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(*m_mutex);

        m_ctrlData->has_transform = true;

        // 위치(Position) 값 복사
        m_ctrlData->position[0] = static_cast<float>(msg->position.x);
        m_ctrlData->position[1] = static_cast<float>(msg->position.y);
        m_ctrlData->position[2] = static_cast<float>(msg->position.z);

        // 회전(Orientation - Quaternion) 값 복사
        m_ctrlData->orientation[0] = static_cast<float>(msg->orientation.x);
        m_ctrlData->orientation[1] = static_cast<float>(msg->orientation.y);
        m_ctrlData->orientation[2] = static_cast<float>(msg->orientation.z);
        m_ctrlData->orientation[3] = static_cast<float>(msg->orientation.w);

        // Maya가 값을 읽어갈 수 있도록 시퀀스 인덱스 증가 (+1)
        m_ctrlData->command_index.fetch_add(1, std::memory_order_relaxed);
    }

    std::unique_ptr<boost::interprocess::named_mutex> m_mutex;
    std::unique_ptr<boost::interprocess::windows_shared_memory> m_shm;
    std::unique_ptr<boost::interprocess::mapped_region> m_region;
    MaroPlugin::RobotControlData* m_ctrlData = nullptr;
    rclcpp::Subscription<geometry_msgs::msg::Pose>::SharedPtr m_subscriber;
};

int main(int argc, char* argv[]) {
    try {
        // [수정됨] 단 한 번만 초기화합니다.
        rclcpp::init(argc, argv);

        // 노드를 생성하고 스핀(대기)합니다.
        rclcpp::spin(std::make_shared<ControlBridgeNode>());

        // 스핀이 종료되면(Ctrl+C 등) 안전하게 ROS 2를 종료합니다.
        rclcpp::shutdown();
    }
    catch (const std::exception& e) {
        std::cerr << "\n==================================" << std::endl;
        std::cerr << " 🚨 ROS 2 치명적 에러 발생: " << e.what() << std::endl;
        std::cerr << "==================================\n" << std::endl;

        // 에러로 인해 튕겼을 때도 ROS 2 엔진을 안전하게 꺼줍니다.
        if (rclcpp::ok()) {
            rclcpp::shutdown();
        }
    }
    catch (...) {
        std::cerr << "\n 🚨 알 수 없는 에러로 종료되었습니다.\n" << std::endl;
        if (rclcpp::ok()) {
            rclcpp::shutdown();
        }
    }

    return 0;
}