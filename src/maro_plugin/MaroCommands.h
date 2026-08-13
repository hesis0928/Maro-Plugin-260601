#pragma once

#include <maya/MDGModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

namespace maro {

// 어트리뷰트 쓰기는 반드시 커맨드를 경유한다. MDGModifier가 undo/redo를 처리한다.
class MaroBindAxisCommand : public MPxCommand {
public:
    static void* creator();
    static MSyntax newSyntax();

    MStatus doIt(const MArgList& args) override;
    MStatus redoIt() override;
    MStatus undoIt() override;
    // 아무것도 바꾸지 않은 호출은 undo 큐에 올리지 않는다. no-op이 쌓이면
    // 사용자가 Ctrl+Z를 눌렀을 때 아무 일도 일어나지 않는다.
    bool isUndoable() const override { return m_stagedChange; }

private:
    MDGModifier m_modifier;
    bool m_stagedChange = false;
};

// 축 체인 연결. 순환은 평가할 때 터지게 두지 않고 연결하는 순간 거부한다.
class MaroConnectAxisCommand : public MPxCommand {
public:
    static void* creator();
    static MSyntax newSyntax();

    MStatus doIt(const MArgList& args) override;
    MStatus redoIt() override;
    MStatus undoIt() override;
    bool isUndoable() const override { return true; }

private:
    MDGModifier m_modifier;
};

// 브리지 제어. 사용자가 Maya 안에서 ROS 2 연동을 켜고 끄는 유일한 경로다.
// 시작 시 MaroCommandDeviceNode를 만들어 live로 세우고, 정지 시 지워
// Maya가 그 스레드를 정리하게 한다. MDGModifier를 쓰지만 스레드/네트워크
// 부작용은 되돌릴 수 없으므로 이 커맨드 자체는 undo 대상이 아니다.
class MaroStartBridgeCommand : public MPxCommand {
public:
    static void* creator();
    static MSyntax newSyntax();
    MStatus doIt(const MArgList& args) override;
};

class MaroStopBridgeCommand : public MPxCommand {
public:
    static void* creator();
    MStatus doIt(const MArgList& args) override;
};

// 펌프와 두 백그라운드 스레드(발행 쪽 MaroRosRuntime, 수신 쪽
// MaroCommandDeviceNode)가 실제로 일하고 있는지 보는 진단 커맨드.
class MaroBridgeStatsCommand : public MPxCommand {
public:
    static void* creator();
    MStatus doIt(const MArgList& args) override;
};

// 플러그인 언로드 시 브리지를 확실히 내린다.
void shutdownBridge();

}  // namespace maro
