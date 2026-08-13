#include "MaroCommandDeviceNode.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>

#include "MaroAxisNode.h"

namespace maro {

namespace {

// 관절 이름 하나가 이 길이를 넘으면 조용히 버리되 s_dropped를 올려
// 메인 스레드(compute)가 상태 변화 시 1회 경고하게 한다 -- 스레드에서
// 직접 MGlobal을 부르면 안 되므로(ROS 2 스레드에서 Maya API 호출 금지)
// 카운터로 넘긴다.
constexpr std::size_t kMaxJointNameLength = 63;
constexpr int kPoolDepth = 64;

// 명령 하나의 고정 크기 표현. MCharBuffer는 원시 메모리라 가변 길이
// std::string을 그대로 못 담는다 -- 관절 이름 상한을 두고 POD로 담는다.
struct CommandRecord {
    char jointName[kMaxJointNameLength + 1];
    double rosValue;
};

}  // namespace

MTypeId MaroCommandDeviceNode::id(0x00135105);
MObject MaroCommandDeviceNode::aCommandOut;

std::atomic<std::uint64_t> MaroCommandDeviceNode::s_applied{0};
std::atomic<std::uint64_t> MaroCommandDeviceNode::s_ticks{0};
std::atomic<std::uint64_t> MaroCommandDeviceNode::s_dropped{0};
std::atomic<std::uint64_t> MaroCommandDeviceNode::s_poolExhausted{0};
std::atomic<int> MaroCommandDeviceNode::s_threadAliveCount{0};
std::uint64_t MaroCommandDeviceNode::s_lastReportedDropped{0};
std::uint64_t MaroCommandDeviceNode::s_lastReportedPoolExhausted{0};

namespace {

// RAII: s_threadAliveCount를 늘리고, 이 가드가 스코프를 벗어나면 -- 정상
// 종료든, isDone()/endThreadLoop()가 try 밖에서 예외를 던져 스택이
// 되감기든 -- 반드시 다시 내린다. 예전에는 루프 맨 끝에서
// s_threadAlive.store(false)를 직접 불렀는데, 그 줄 앞에서 예외가 새면
// 카운터가 영원히 "살아있음"에 걸려 shutdownBridge()가 매번 2초를 날리고
// 이미 죽은 스레드에 대해 경고를 찍게 된다.
class ThreadAliveGuard {
public:
    explicit ThreadAliveGuard(std::atomic<int>& counter) : m_counter(counter) {
        m_counter.fetch_add(1, std::memory_order_relaxed);
    }
    ~ThreadAliveGuard() {
        m_counter.fetch_sub(1, std::memory_order_relaxed);
    }
    ThreadAliveGuard(const ThreadAliveGuard&) = delete;
    ThreadAliveGuard& operator=(const ThreadAliveGuard&) = delete;

private:
    std::atomic<int>& m_counter;
};

}  // namespace

MaroCommandDeviceNode::MaroCommandDeviceNode() {}

MaroCommandDeviceNode::~MaroCommandDeviceNode() {
    // 소멸자에서 예외가 새면 std::terminate다. 다른 콜백들과 같은 규칙.
    try {
        destroyMemoryPools();
    } catch (const std::exception& e) {
        MGlobal::displayError(
            MString("Maro: command device destroyMemoryPools failed: ") + e.what());
    } catch (...) {
        MGlobal::displayError(
            "Maro: command device destroyMemoryPools failed with unknown error.");
    }
}

void* MaroCommandDeviceNode::creator() {
    return new MaroCommandDeviceNode();
}

MStatus MaroCommandDeviceNode::initialize() {
    MFnNumericAttribute numFn;

    // 값 자체는 의미가 없다. dirty 표시만을 위한 출력이라 베이스 클래스의
    // 제네릭 output 대신 우리 것을 쓴다 (devkit의 randomizerDevice 예제와
    // 같은 패턴 -- 그 예제도 자기 outputTranslate를 따로 둔다).
    aCommandOut = numFn.create("commandOut", "cmo", MFnNumericData::kBoolean, false);
    numFn.setStorable(false);
    numFn.setKeyable(false);
    numFn.setHidden(true);
    addAttribute(aCommandOut);

    attributeAffects(live, aCommandOut);
    attributeAffects(frameRate, aCommandOut);

    return MS::kSuccess;
}

void MaroCommandDeviceNode::postConstructor() {
    // Maya 콜백이다. 예외가 새면 Maya가 죽는다.
    try {
        MObjectArray attrs;
        attrs.append(aCommandOut);
        setRefreshOutputAttributes(attrs);

        // 명령 하나 = CommandRecord 하나. 여유 있게 64개 버퍼를 돌린다.
        const MStatus status = createMemoryPools(kPoolDepth, 1, sizeof(CommandRecord));
        if (!status) {
            MGlobal::displayError("Maro: failed to create command device memory pools.");
        }
    } catch (const std::exception& e) {
        MGlobal::displayError(
            MString("Maro: command device postConstructor failed: ") + e.what());
    } catch (...) {
        MGlobal::displayError(
            "Maro: command device postConstructor failed with unknown error.");
    }
}

void MaroCommandDeviceNode::setRobotName(const MString& robotName) {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_robotName = robotName.asChar();
}

std::string MaroCommandDeviceNode::robotNameSnapshot() const {
    std::lock_guard<std::mutex> lock(m_configMutex);
    return m_robotName;
}

void MaroCommandDeviceNode::resetStats() {
    s_applied.store(0);
    s_ticks.store(0);
    s_dropped.store(0);
    s_poolExhausted.store(0);
    // compute()의 방울 경고 기준선도 함께 되돌린다 (M9): 안 그러면 재시작
    // 후 dropped(0) != lastReported(N)에서 unsigned 뺄셈이 언더플로우해
    // 터무니없는 개수를 찍는다.
    s_lastReportedDropped = 0;
    s_lastReportedPoolExhausted = 0;
}

std::uint64_t MaroCommandDeviceNode::appliedCommandCount() { return s_applied.load(); }
std::uint64_t MaroCommandDeviceNode::threadTickCount() { return s_ticks.load(); }
// 개수 > 0 이면 인스턴스 중 최소 하나는 스레드가 살아있다는 뜻이다 -- 여러
// maroCommandDevice 인스턴스가 있어도 정확하다 (I5/M10).
bool MaroCommandDeviceNode::isThreadAlive() { return s_threadAliveCount.load() > 0; }

void MaroCommandDeviceNode::applyToMatchingAxis(const std::string& jointName, double value) {
    // compute()에서만 불린다 -- 메인 스레드다. DG를 만지는 유일한 지점이다.
    for (MItDependencyNodes it(MFn::kPluginLocatorNode); !it.isDone(); it.next()) {
        MFnDependencyNode axisFn(it.thisNode());
        if (axisFn.typeId() != MaroAxisNode::id) continue;

        // ROS 모드가 아닌 축은 명령을 받지 않는다 (Task 12의 계약).
        if (axisFn.findPlug(MaroAxisNode::aControlMode, false).asShort() != 1) continue;
        if (axisFn.findPlug(MaroAxisNode::aJointName, false).asString().asChar() != jointName) {
            continue;
        }

        // 런타임 데이터 흐름이므로 직접 쓴다. undo 스택에 남기지 않는다.
        axisFn.findPlug(MaroAxisNode::aRosCommand, false).setDouble(value);
        s_applied.fetch_add(1, std::memory_order_relaxed);
    }
}

void MaroCommandDeviceNode::threadHandler() {
    // Maya가 만든 백그라운드 스레드다. 여기서 예외가 새면 스레드가 조용히
    // 죽는다. DG는 절대 건드리지 않는다 -- 버퍼만 채운다.
    setDone(false);

    // ThreadAliveGuard는 이 스코프를 벗어나는 모든 경로(정상 종료 포함,
    // 아래 while 루프 밖의 isDone()/endThreadLoop()가 예외를 던져 스택이
    // 되감기는 경로 포함)에서 카운터를 내린다 -- I5/M10.
    ThreadAliveGuard aliveGuard(s_threadAliveCount);

    // setRobotName()은 메인 스레드에서, 이 스레드가 시작되는 것과 비슷한
    // 시점에 불린다 (devkit 문서에 순서 보장이 없다). 값이 채워질 때까지
    // 짧게 기다린다 -- 최대 1초, 그새 isDone()이 서면 즉시 포기한다.
    std::string robotName;
    for (int i = 0; i < 200 && robotName.empty() && !isDone(); ++i) {
        robotName = robotNameSnapshot();
        if (!robotName.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::shared_ptr<rclcpp::Node> node;
    rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr sub;
    std::vector<CommandRecord> pending;

    // M12: rclcpp::spin_some(node) 자유 함수는 호출마다 SingleThreadedExecutor
    // 하나(가드 컨디션, wait set까지)를 새로 만들고 버린다. 이 루프는
    // frameRate Hz로 돈다 -- 매 반복 그 생성/파괴 비용을 물 이유가 없다.
    // executor 하나를 스레드 수명 동안 들고 spin_some()만 반복 호출한다.
    rclcpp::executors::SingleThreadedExecutor executor;

    if (!robotName.empty()) {
        try {
            // 발행 쪽(MaroRosRuntime)과 별개인, 이 스레드 전용 노드다.
            // 두 노드 다 같은 전역 rclcpp 컨텍스트를 쓰므로 rclcpp::init()은
            // 여기서 부르지 않는다 -- MaroRosRuntime::start()가 이미 했고,
            // 이 스레드는 그 뒤에만 시작된다 (maroStartBridge 순서 참고).
            node = rclcpp::Node::make_shared(robotName + "_cmd_rx");
            sub = node->create_subscription<sensor_msgs::msg::JointState>(
                "/" + robotName + "/joint_commands", 10,
                [&pending](sensor_msgs::msg::JointState::SharedPtr msg) {
                    // spin_some() 안에서, 이 스레드 위에서 동기적으로 불린다.
                    if (msg->name.size() != msg->position.size()) return;
                    for (std::size_t i = 0; i < msg->name.size(); ++i) {
                        const double value = msg->position[i];
                        if (!std::isfinite(value)) continue;
                        if (msg->name[i].size() > kMaxJointNameLength) {
                            s_dropped.fetch_add(1, std::memory_order_relaxed);
                            continue;
                        }
                        CommandRecord rec{};
                        std::strncpy(rec.jointName, msg->name[i].c_str(), kMaxJointNameLength);
                        rec.rosValue = value;
                        pending.push_back(rec);
                    }
                });
            executor.add_node(node);
        } catch (...) {
            node.reset();
            sub.reset();
        }
    }

    while (!isDone()) {
        try {
            // 매 반복 begin/end로 감싼다 (isLive()가 꺼져 있어도, node가
            // 없어도) -- endThreadLoop()이 유일한 스로틀이므로, 여기서
            // 건너뛰면 바쁜 대기가 된다.
            beginThreadLoop();
            if (isLive() && node) {
                pending.clear();
                executor.spin_some();
                s_ticks.fetch_add(1, std::memory_order_relaxed);

                for (const CommandRecord& rec : pending) {
                    MCharBuffer buffer;
                    if (acquireDataStorage(buffer)) {
                        *reinterpret_cast<CommandRecord*>(buffer.ptr()) = rec;
                        pushThreadData(buffer);
                    } else {
                        // 조용한 실패 금지 (I4): 풀 64슬롯이 다 찼다.
                        // compute()가 못 따라잡을 때(정상적인 배압)의 일시적
                        // 상태일 수도 있고, 배치/헤드리스처럼 compute()가
                        // 애초에 안 불려 영구적인 상태일 수도 있다. 원인이
                        // 다르므로 s_dropped(과사이즈 이름)에 합치지 않고
                        // 별도 카운터로 구분해, "이름을 줄여라"는 엉뚱한
                        // 처방이 나가지 않게 한다.
                        s_poolExhausted.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        } catch (...) {
            // 스레드가 죽으면 브리지가 조용히 먹통이 된다. 반복을 계속한다.
        }
        endThreadLoop();   // frameRate 기준 스로틀. 우리가 sleep_for를 쓰지 않는 이유다.
    }

    if (node) {
        executor.remove_node(node);
    }
    sub.reset();
    node.reset();
    // s_threadAliveCount는 aliveGuard 소멸자가 스코프 종료 시 내린다.
}

void MaroCommandDeviceNode::threadShutdownHandler() {
    // File -> New, Exit, 또는 이 노드가 삭제될 때 Maya가 호출한다.
    // threadHandler()의 while(!isDone()) 루프를 깨운다. Maya 콜백이다 --
    // 예외가 새면 Maya가 죽는다.
    try {
        setDone(true);
    } catch (const std::exception& e) {
        MGlobal::displayError(
            MString("Maro: command device threadShutdownHandler failed: ") + e.what());
    } catch (...) {
        MGlobal::displayError(
            "Maro: command device threadShutdownHandler failed with unknown error.");
    }
}

MStatus MaroCommandDeviceNode::compute(const MPlug& plug, MDataBlock& data) {
    if (plug != aCommandOut) return MS::kUnknownParameter;

    try {
        MCharBuffer buffer;
        while (popThreadData(buffer)) {
            const CommandRecord* rec = reinterpret_cast<const CommandRecord*>(buffer.ptr());
            const std::string jointName(rec->jointName);
            const double value = rec->rosValue;
            releaseDataStorage(buffer);

            if (!std::isfinite(value)) continue;   // 이미 스레드에서 걸렀지만 안전망을 하나 더 둔다.
            applyToMatchingAxis(jointName, value);
        }

        // 조용한 실패 금지: 스레드가 버린 게 있으면 상태가 바뀔 때 1회만 경고한다.
        // lastReported 기준선은 class static이라 resetStats()가 함께 0으로
        // 되돌린다 (M9) -- 함수 로컬 static이면 재시작 후 dropped(0) !=
        // lastReported(N)에서 unsigned 뺄셈이 언더플로우한다.
        const std::uint64_t dropped = s_dropped.load();
        if (dropped != s_lastReportedDropped) {
            MGlobal::displayWarning(
                MString("Maro: dropped ") +
                static_cast<int>(dropped - s_lastReportedDropped) +
                " oversized joint command name(s) (limit 63 chars).");
            s_lastReportedDropped = dropped;
        }

        // I4: 버퍼 풀(64슬롯)이 꽉 차 스레드가 버린 명령도 같은 방식으로 알린다.
        const std::uint64_t poolExhausted = s_poolExhausted.load();
        if (poolExhausted != s_lastReportedPoolExhausted) {
            MGlobal::displayWarning(
                MString("Maro: dropped ") +
                static_cast<int>(poolExhausted - s_lastReportedPoolExhausted) +
                " inbound command(s): the command buffer pool (64 slots) was "
                "full. This means compute() is not keeping up (interactive) "
                "or is not running at all (batch/headless).");
            s_lastReportedPoolExhausted = poolExhausted;
        }

        MDataHandle out = data.outputValue(aCommandOut);
        out.setBool(true);
        out.setClean();
        return MS::kSuccess;
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: command device compute failed: ") + e.what());
        return MS::kFailure;
    } catch (...) {
        MGlobal::displayError("Maro: command device compute failed with unknown error.");
        return MS::kFailure;
    }
}

}  // namespace maro
