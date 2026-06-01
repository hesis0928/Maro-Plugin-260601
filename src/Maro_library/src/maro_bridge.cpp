#include "maro_library/maro_bridge.hpp"
#include <rclcpp/rclcpp.hpp>
#include <cstring> // memcpy 사용

namespace maro_bridge
{

    CvImage toCvCopy(const sensor_msgs::msg::Image::ConstSharedPtr& source)
    {
        CvImage dest;

        // 안전을 위한 널 포인터 검사
        if (!source) {
            RCLCPP_ERROR(rclcpp::get_logger("MaroLibrary"), "[toCvCopy] 입력된 source 포인터가 null입니다.");
            return dest;
        }

        // 2. 헤더 정보 복사
        dest.header = source->header;

        // 3. 인코딩 유효성 검사 ("bgr8" 전용)
        if (source->encoding != "bgr8") {
            RCLCPP_ERROR(rclcpp::get_logger("MaroLibrary"),
                "[toCvCopy] 지원하지 않는 인코딩 형식입니다: %s. (bgr8만 지원)",
                source->encoding.c_str());
            return dest; // 비어있는 dest 객체 반환
        }

        // 4. 인코딩 정보 복사
        dest.encoding = source->encoding;

        // 5. 원본 메시지의 높이와 너비를 가지는 3채널 8비트 cv::Mat 객체 생성
        dest.image = cv::Mat(source->height, source->width, CV_8UC3);

        // 6. 픽셀 데이터 복사 (height * width * 3)
        size_t expected_data_size = source->height * source->width * 3;

        // 버퍼 오버플로우 방지를 위한 방어 코드 추가
        if (source->data.size() >= expected_data_size) {
            std::memcpy(dest.image.data, source->data.data(), expected_data_size);
        }
        else {
            RCLCPP_ERROR(rclcpp::get_logger("MaroLibrary"),
                "[toCvCopy] ROS 메시지의 데이터 크기가 예상보다 작습니다.");
        }

        // 7. 완성된 dest 반환
        return dest;
    }

    sensor_msgs::msg::Image::SharedPtr toImageMsg(const CvImage& source)
    {
        // 1. 새로운 ROS2 이미지 메시지 객체 생성
        auto msg = std::make_shared<sensor_msgs::msg::Image>();

        if (source.image.empty()) {
            RCLCPP_WARN(rclcpp::get_logger("MaroLibrary"), "[toImageMsg] 변환할 OpenCV 이미지가 비어있습니다.");
            return msg;
        }

        // 2~7. 메타데이터 할당
        msg->header = source.header;
        msg->height = source.image.rows;
        msg->width = source.image.cols;
        msg->encoding = source.encoding;
        msg->is_bigendian = 0;
        msg->step = static_cast<uint32_t>(source.image.step);

        // 8. 전체 데이터 크기 계산
        size_t data_size = source.image.step * source.image.rows;

        // 9. 메시지의 데이터 버퍼 크기 할당
        msg->data.resize(data_size);

        // 10. 픽셀 데이터 복사
        if (data_size > 0 && source.image.data != nullptr) {
            std::memcpy(msg->data.data(), source.image.data, data_size);
        }

        // 11. 완성된 msg 스마트 포인터 반환
        return msg;
    }

} // namespace maro_bridge