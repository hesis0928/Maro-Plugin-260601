#pragma once

#include <string>
#include <memory>

// OpenCV 코어 헤더
#include <opencv2/core.hpp>

// ROS2 메시지 헤더
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/header.hpp>

namespace maro_bridge
{
    struct CvImage
    {
        /**
         * @brief 원본 ROS 메시지의 헤더 (타임스탬프, 좌표계 포함)
         */
        std_msgs::msg::Header header;

        /**
         * @brief 변환된 OpenCV 이미지 데이터
         */
        cv::Mat image;

        /**
         * @brief 이미지의 인코딩 형식 (본 설계에서는 "bgr8"로 고정)
         */
        std::string encoding;
    };

    /**
     * @brief ROS2 이미지 메시지(스마트 포인터)를 받아 OpenCV 이미지로 변환
     * @param source 변환할 원본 ROS2 이미지 메시지의 ConstSharedPtr
     * @return CvImage 변환된 이미지와 메타데이터를 포함하는 객체
     */
    CvImage toCvCopy(const sensor_msgs::msg::Image::ConstSharedPtr& source);

    /**
     * @brief OpenCV 이미지를 받아 ROS2 이미지 메시지(스마트 포인터)로 변환
     * @param source 변환할 CvImage 객체 (처리된 cv::Mat 포함)
     * @return sensor_msgs::msg::Image::SharedPtr 생성된 ROS2 이미지 메시지의 스마트 포인터
     */
    sensor_msgs::msg::Image::SharedPtr toImageMsg(const CvImage& source);

} // namespace maro_bridge