#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "image_transport/image_transport.hpp"
// maro_library 헤더를 포함합니다. cv_bridge는 더 이상 필요 없습니다.
#include "maro_library/maro_bridge.hpp"
#include "maro_library/IpcData.h"

#include <boost/interprocess/windows_shared_memory.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include <opencv2/opencv.hpp>
#include <memory>
#include <chrono>

using namespace std::chrono_literals;

class ImageBridgeNode : public rclcpp::Node {
public:
    ImageBridgeNode() : Node("image_bridge_node"), m_lastReadFrame(0) {
        RCLCPP_INFO(this->get_logger(), "Initializing Image Bridge Node...");

        try {
            m_mutex = std::make_unique<boost::interprocess::named_mutex>(boost::interprocess::open_or_create, MaroPlugin::MUTEX_NAME);
            m_shm = std::make_unique<boost::interprocess::windows_shared_memory>(
                boost::interprocess::open_or_create,
                MaroPlugin::SHM_NAME,
                boost::interprocess::read_write,
                MaroPlugin::SHM_SIZE
            );
            m_region = std::make_unique<boost::interprocess::mapped_region>(*m_shm, boost::interprocess::read_write);
            
            m_header = static_cast<MaroPlugin::SharedImageHeader*>(m_region->get_address());
            m_buffer = static_cast<unsigned char*>(m_region->get_address()) + sizeof(MaroPlugin::SharedImageHeader);

        } catch (const boost::interprocess::interprocess_exception& e) {
            RCLCPP_FATAL(this->get_logger(), "Failed to create/open shared memory: %s", e.what());
            rclcpp::shutdown();
            return;
        }

        m_publisher = std::make_shared<image_transport::Publisher>(
            image_transport::create_publisher(this, "/maya/viewport_image"));

        m_timer = this->create_wall_timer(16ms, std::bind(&ImageBridgeNode::timerCallback, this));
        
        RCLCPP_INFO(this->get_logger(), "Image Bridge Node started. Publishing to /maya/viewport_image");
    }

    ~ImageBridgeNode() {
        boost::interprocess::shared_memory_object::remove(MaroPlugin::SHM_NAME);
        boost::interprocess::named_mutex::remove(MaroPlugin::MUTEX_NAME);
        RCLCPP_INFO(this->get_logger(), "Shared memory objects removed.");
    }

private:
    void timerCallback() {
        uint64_t currentFrame = 0;
        cv::Mat image;

        {
            boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(*m_mutex);
            currentFrame = m_header->frame_index.load(std::memory_order_relaxed);

            if (currentFrame > m_lastReadFrame && m_header->width > 0 && m_header->height > 0) {
                image = cv::Mat(m_header->height, m_header->width, CV_8UC4, m_buffer).clone();
                m_lastReadFrame = currentFrame;
            }
        }

        if (!image.empty()) {
            // OpenCV 이미지는 BGRA 형식이므로, ROS 메시지 인코딩도 "bgra8"로 설정합니다.
            // cvtColor는 더 이상 필요하지 않습니다.
            // cv::cvtColor(image, image, cv::COLOR_RGBA2BGRA); 
            
            // ================================================================
            // <<< 핵심 수정 사항 >>>
            
            // 1. maro_bridge::CvImage 구조체를 생성합니다.
            maro_bridge::CvImage maro_image;

            // 2. 헤더, 인코딩, 이미지 데이터를 채웁니다.
            maro_image.header.stamp = this->get_clock()->now();
            maro_image.header.frame_id = "maya_viewport";
            maro_image.encoding = "bgra8"; // OpenCV Mat이 BGRA 형식이므로 일치시킵니다.
            maro_image.image = image;

            // 3. maro_bridge::toImageMsg 함수를 호출하여 ROS2 메시지를 생성합니다.
            sensor_msgs::msg::Image::SharedPtr msg = maro_bridge::toImageMsg(maro_image);
            
            // ================================================================
            
            m_publisher->publish(std::move(msg));
        }
    }

    rclcpp::TimerBase::SharedPtr m_timer;
    std::shared_ptr<image_transport::Publisher> m_publisher;
    
    std::unique_ptr<boost::interprocess::named_mutex> m_mutex;
    std::unique_ptr<boost::interprocess::windows_shared_memory> m_shm;
    std::unique_ptr<boost::interprocess::mapped_region> m_region;

    MaroPlugin::SharedImageHeader* m_header;
    unsigned char* m_buffer;
    uint64_t m_lastReadFrame;
};

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ImageBridgeNode>());
    rclcpp::shutdown();
    return 0;
}