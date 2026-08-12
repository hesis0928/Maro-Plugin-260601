#pragma once

#include <maya/MCallbackIdArray.h>
#include <maya/MDGModifier.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>

namespace maro {

// 삭제 규칙은 비대칭이다.
//   오브젝트 삭제 -> 바인딩된 축도 삭제
//   축 삭제       -> 오브젝트는 생존, 능력 노드는 고아로 남음
//
// addNodeAboutToDeleteCallback이 넘겨주는 MDGModifier에 작업을 실으면
// 사용자의 삭제와 같은 undo 청크로 묶인다. 직접 청킹할 필요가 없다.
class MaroDeleteWatcher {
public:
    static MStatus install();
    static MStatus uninstall();

private:
    static void onNodeAdded(MObject& node, void* clientData);
    static void onObjectAboutToDelete(MObject& node, MDGModifier& modifier,
                                      void* clientData);
    static void onAxisAboutToDelete(MObject& node, MDGModifier& modifier,
                                    void* clientData);

    static MCallbackIdArray s_callbacks;
};

}  // namespace maro
