#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <maya/MObject.h>
#include <maya/MPxThreadedDeviceNode.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

namespace maro {

// ROS 2 -> Maya 수신 전담. Maya가 백그라운드 스레드를 만들고 끝낸다.
// threadHandler()는 그 스레드에서 돈다 -- DG를 절대 건드리지 않는다
// (compute()만 건드린다). 큐도 뮤텍스도 우리가 만들지 않는다;
// acquireDataStorage()/pushThreadData()/popThreadData()가 devkit 내부
// 락과 링버퍼로 그 역할을 대신한다.
class MaroCommandDeviceNode : public MPxThreadedDeviceNode {
public:
    MaroCommandDeviceNode();
    ~MaroCommandDeviceNode() override;

    void postConstructor() override;
    MStatus compute(const MPlug& plug, MDataBlock& data) override;

    void threadHandler() override;
    void threadShutdownHandler() override;

    static void* creator();
    static MStatus initialize();

    // 메인 스레드에서, 노드를 만든 직후(live를 켜기 전)에 한 번만 부른다.
    // 로봇 네임스페이스를 스레드로 안전하게 건너 보낸다.
    void setRobotName(const MString& robotName);

    static void resetStats();
    static std::uint64_t appliedCommandCount();
    static std::uint64_t threadTickCount();
    static bool isThreadAlive();

    static MTypeId id;
    static MObject aCommandOut;   // 값 자체는 안 쓴다 -- dirty 표시 전용

private:
    std::string robotNameSnapshot() const;
    static void applyToMatchingAxis(const std::string& jointName, double value);

    mutable std::mutex m_configMutex;
    std::string m_robotName;

    static std::atomic<std::uint64_t> s_applied;
    static std::atomic<std::uint64_t> s_ticks;
    static std::atomic<std::uint64_t> s_dropped;
    static std::atomic<bool> s_threadAlive;
};

}  // namespace maro
