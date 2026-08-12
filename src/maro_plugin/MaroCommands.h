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
    bool isUndoable() const override { return true; }

private:
    MDGModifier m_modifier;
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

}  // namespace maro
