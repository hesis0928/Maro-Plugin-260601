#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "maro_transform/Types.h"

namespace maro {

// 메인 스레드 -> 백그라운드(발행 방향)로만 쓰인다. 수신 방향은
// MPxThreadedDeviceNode의 메모리 풀이 대신하므로 여기 없다 (Task 10 설계
// 노트 참고). 변환에 필요한 컨텍스트를 함께 실어 보낸다. 백그라운드는
// Maya를 일절 조회하지 않으므로 씬 단위와 보정값이 여기 들어간다.
struct AxisSample {
    std::string jointName;
    double value = 0.0;
    Vec3 position;
    Quat rotation;
    AxisConvention convention;
    SceneUnit unit;
};

// 상한이 있는 큐. 넘치면 오래된 것부터 버린다.
// 실시간 제어에서 의미 있는 건 최신 값이고, 상한이 없으면 ROS 2가 Maya보다
// 느릴 때 메모리가 고갈된다.
//
// 발행 방향에만 쓴다. 수신 방향은 이 큐 대신 MPxThreadedDeviceNode의
// 메모리 풀/락을 쓰므로 여기엔 뮤텍스를 우리가 직접 만드는 코드가
// 발행 방향 하나뿐이다.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity = 256) : m_capacity(capacity) {}

    void push(T item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (m_items.size() >= m_capacity) {
            m_items.pop_front();
            ++m_dropped;
        }
        m_items.push_back(std::move(item));
    }

    std::vector<T> drain() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<T> out(m_items.begin(), m_items.end());
        m_items.clear();
        return out;
    }

    std::size_t droppedCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dropped;
    }

private:
    mutable std::mutex m_mutex;
    std::deque<T> m_items;
    std::size_t m_capacity;
    std::size_t m_dropped = 0;
};

}  // namespace maro
