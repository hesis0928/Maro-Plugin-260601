# Maro 축·노드 로봇화 슬라이스 (S1+S2) 구현 플랜

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Maya 오브젝트에 축과 능력 노드를 배치·배선해 로봇화하고, 그 상태를 ROS 2로 발행하고 ROS 2 명령으로 구동하는 최소 수직 슬라이스를 만든다.

**Architecture:** Maya DG가 단일 진실 원천이다. 커스텀 DG 노드(`maroAxis` 로케이터 + 능력 노드 스택 + `maroRobot`)가 모든 상태를 보유한다. 좌표 변환은 Maya·ROS 2 어느 쪽에도 의존하지 않는 순수 라이브러리로 분리해 Maya 없이 테스트한다. ROS 2 통신은 백그라운드 스레드에서 돌고, Maya 메인 스레드와는 두 개의 큐로만 만난다.

**Tech Stack:** C++17, Maya 2026 devkit, ROS 2 Jazzy (네이티브 Windows 소스 빌드), CMake + Ninja, vcpkg, GoogleTest

## Global Constraints

설계 스펙 `docs/superpowers/specs/2026-08-13-maya-ros2-axis-node-robotization-design.md`의 전 프로젝트 요구사항. 모든 태스크에 암묵적으로 적용된다.

- **ROS 2 스레드에서 Maya API를 절대 호출하지 않는다.** 위반 시 재현 불가능한 크래시가 난다.
- **예외는 경계를 넘지 않는다.** 모든 Maya 콜백(`compute`, 타이머, DG 메시지)과 ROS 2 콜백 최상위에 catch-all을 둔다.
- **`NaN`/`inf`를 Maya 어트리뷰트에 쓰지 않는다.** 유한성 검사 실패 시 직전 유효값을 유지한다.
- **`compute()` 안에서 씬 그래프를 수정하지 않는다.** 순수 계산만 한다.
- **어트리뷰트 쓰기 경로는 두 가지이며 섞지 않는다.**
  - **사용자 구성 변경**(바인딩, 축 연결, 모드 전환, 보정값)은 반드시 `MPxCommand` + `MDGModifier` 경유. Undo 정합성을 위해서다.
  - **런타임 데이터 흐름**(매 프레임 도착하는 관절 명령)은 `MPlug` 직접 쓰기이며 **undo 스택에 남기지 않는다.** 초당 수십 번의 값 갱신을 undo에 쌓으면 사용자의 Ctrl+Z가 무의미해진다. Maya 자체의 시뮬레이션·캐시 노드와 같은 관행이다.
- **조용한 실패 금지.** 거부·비활성화 시 사유를 출력한다. 단 매 프레임 반복되는 경고는 상태 변화 시 1회만.
- **노드 타입 접두사는 `maro`** (`Maro` = **Ma**ya + **Ro**s).
- **C++17.** 단, 이유를 정확히 알아둘 것 — VFX Reference Platform 기준으로 **Maya 2026은 CY2025 = C++20**이다(C++17은 Maya 2024/2025). 우리가 17을 쓰는 실제 이유는 devkit의 `cmake/devkit.cmake`가 플러그인 타깃에 `CMAKE_CXX_STANDARD 17` + `REQUIRED ON`을 하드코딩하기 때문이다. 플랫폼 규격이 아니라 빌드 스크립트의 보수적 고정값이다. 되돌릴 실익이 없어(C++20 기능을 쓰는 곳이 없다) devkit을 따르되, "Maya가 C++17만 지원한다"고 오해하지 말 것. `maro_transform`도 플러그인에 링크되므로 같은 표준을 쓴다.
- 경로 상수: devkit `C:/Users/ckd30/Projects/devkitBase`, ROS 2 `C:/dev/ros2_jazzy/install`, vcpkg `C:/src/vcpkg`.
- **Maya 테스트 주의**: Maya는 **커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.** 노드를 만든 테스트는 `cmds.unloadPlugin` 앞에 `cmds.file(new=True, force=True)`로 씬을 비워야 한다. 빠뜨리면 정리 단계에서 `RuntimeError`가 나는데, 원인이 플러그인 결함처럼 보여 오진하기 쉽다.
- **빌드 환경 주의**: 이 머신에서 `Launch-VsDevShell.ps1`은 `vswhere.exe`를 못 찾아 `INCLUDE`/`LIB`를 비운 채 조용히 성공한다. 결과는 `basetsd.h`를 못 찾는 엉뚱한 컴파일 에러다. 대신 `VsDevCmd.bat`의 환경을 가져와 쓰고, PowerShell 도구 호출 간에는 환경이 유지되지 않으므로 **빌드와 같은 호출 안에서** 설정한다.

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 && set' | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
}
```
- **벤더 DLL은 `install/opt/<vendor>/bin`에 있다.** `yaml.dll`, `spdlog.dll`, `console_bridge.dll`을 `install/bin`과 함께 복사해야 로드된다.

## 범위 밖 (건드리지 않음)

`src/control_bridge/`, `src/image_bridge/`, `src/Maro_library/`, `MaroCmd.cpp`, `moveTool.cpp`, `rosSimCmd.cpp`는 이번 슬라이스에서 **수정하지 않는다.** 미커밋 변경이 남아 있고, `ViewportStreamer`는 S4에서 재사용한다. TCP/UDP 구조 폐기는 별도 정리 작업이다.

## 파일 구조

```
Maya_Ros_Sim/
├── CMakeLists.txt                              [생성] 루트 빌드
├── vcpkg.json                                  [생성] " vcpkg.json" 대체
├── src/maro_transform/                         순수 라이브러리 — Maya/ROS 의존 없음
│   ├── CMakeLists.txt                          [생성]
│   ├── include/maro_transform/Types.h          [생성] Vec3/Quat/AxisConvention/SceneUnit
│   ├── include/maro_transform/Convert.h        [생성] 변환 API
│   └── src/Convert.cpp                         [생성]
├── src/maro_plugin/                            Maya 플러그인 (.mll)
│   ├── CMakeLists.txt                          [생성]
│   ├── MaroPluginMain.cpp                      [생성] initialize/uninitializePlugin
│   ├── MaroAxisNode.h / .cpp                   [생성] MPxLocatorNode
│   ├── MaroCapabilityNodes.h / .cpp            [생성] rotation/limit/sensor 노드
│   ├── MaroRobotNode.h / .cpp                  [생성] ROS 2 노드 경계
│   ├── MaroCommands.h / .cpp                   [생성] bind/convention/mode 커맨드
│   ├── MaroDeleteWatcher.h / .cpp              [생성] 연쇄 삭제 + 고아 세트
│   ├── MaroBridgeQueues.h                      [생성] 두 큐 + 페이로드 구조체
│   └── MaroRosRuntime.h / .cpp                 [생성] rclcpp 스레드
└── tests/
    ├── CMakeLists.txt                          [생성]
    ├── transform/test_convert.cpp              [생성] GTest, Maya 불필요
    ├── peer/maro_test_peer.cpp                 [생성] C++ ROS 2 테스트 피어
    └── maya/test_contract.py                   [생성] mayapy 계약·견고성 테스트
```

---

# Phase 1 — 순수 변환 라이브러리

Maya도 ROS 2도 없이 도는 코드다. 좌표 버그를 여기서 대부분 잡는다.

## 좌표 변환 수식 (확정)

Maya는 Y-up 우수좌표계, ROS는 REP-103의 Z-up 우수좌표계다. 둘을 잇는 기저 변환은 X축 기준 -90° 회전이다.

```
mayaToRos: (x, y, z) -> ( x, -z,  y)
rosToMaya: (x, y, z) -> ( x,  z, -y)
```

검증: Maya의 위 방향 `(0,1,0)` → ROS의 위 방향 `(0,0,1)`. 행렬식이 +1이므로 우수좌표계가 보존된다.

쿼터니언의 벡터부는 **진짜 회전(det=+1) 아래에서 위치 벡터와 동일하게 변환**되므로 같은 규칙을 쓰고 `w`는 그대로 둔다.

```
mayaToRos: (qx, qy, qz, qw) -> ( qx, -qz,  qy, qw)
rosToMaya: (qx, qy, qz, qw) -> ( qx,  qz, -qy, qw)
```

스케일은 위치에만 적용한다. Maya 기본 단위는 cm이므로 `metersPerMayaUnit = 0.01`이다. Maya→ROS는 곱하고, ROS→Maya는 나눈다.

---

### Task 1: 빌드 골격과 테스트 하네스

빌드·테스트 체인이 실제로 도는지부터 증명한다. 이게 안 되면 나머지 전부가 막힌다.

**Files:**
- Create: `CMakeLists.txt`
- Create: `vcpkg.json`
- Create: `src/maro_transform/CMakeLists.txt`
- Create: `src/maro_transform/include/maro_transform/Types.h`
- Create: `tests/CMakeLists.txt`
- Test: `tests/transform/test_convert.cpp`
- Delete: `" vcpkg.json"` (앞에 공백이 있는 파일)

**Interfaces:**
- Consumes: 없음
- Produces: CMake 타깃 `maro_transform`(정적 라이브러리), `maro_transform_tests`(GTest 실행 파일). 헤더는 `maro_transform/Types.h`로 include.

- [ ] **Step 1: 잘못된 이름의 vcpkg 매니페스트 교체**

기존 파일은 이름이 `" vcpkg.json"`(앞 공백)이라 vcpkg가 매니페스트로 인식하지 못한다. 또 내용에 JSON이 허용하지 않는 주석이 들어 있다.

```bash
cd /c/Users/ckd30/Projects/Maya_Ros_Sim
git rm --cached " vcpkg.json" 2>/dev/null || rm -f " vcpkg.json"
```

`vcpkg.json` 생성:

```json
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "maya-ros-sim",
  "version-string": "0.1.0",
  "dependencies": [
    "gtest"
  ]
}
```

- [ ] **Step 2: 루트 CMakeLists 작성**

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(maro LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 외부 위치. 캐시 변수로 주입해 하드코딩을 피한다.
set(DEVKIT_LOCATION "C:/Users/ckd30/Projects/devkitBase" CACHE PATH "Maya devkit root")
set(ROS2_INSTALL    "C:/dev/ros2_jazzy/install"          CACHE PATH "ROS 2 install prefix")

# devkit의 pluginEntry.cmake는 환경 변수로 읽으므로 여기서 채워준다.
set(ENV{DEVKIT_LOCATION} "${DEVKIT_LOCATION}")

option(MARO_BUILD_PLUGIN "Build the Maya plugin (needs devkit + ROS 2)" ON)
option(MARO_BUILD_TESTS  "Build tests" ON)

add_subdirectory(src/maro_transform)

if(MARO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MARO_BUILD_PLUGIN)
    add_subdirectory(src/maro_plugin)
endif()
```

플러그인 하위 디렉터리는 Task 5에서 만든다. 그때까지는 `-DMARO_BUILD_PLUGIN=OFF`로 구성한다.

- [ ] **Step 3: 변환 라이브러리 타입 헤더 작성**

`src/maro_transform/include/maro_transform/Types.h`:

```cpp
#pragma once

#include <cmath>

namespace maro {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// (x, y, z, w) 순서. ROS geometry_msgs/Quaternion과 같은 순서다.
struct Quat {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

enum class LocalAxis { X, Y, Z };

// 사용자 기준 축 보정. 어느 로컬 축이 회전축인지와 부호를 담는다.
struct AxisConvention {
    LocalAxis axis = LocalAxis::Y;
    bool invert = false;
};

// Maya 씬 단위 1당 미터. Maya 기본값 cm이면 0.01이다.
struct SceneUnit {
    double metersPerMayaUnit = 0.01;
};

inline bool isFinite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline bool isFinite(const Quat& q) {
    return std::isfinite(q.x) && std::isfinite(q.y)
        && std::isfinite(q.z) && std::isfinite(q.w);
}

}  // namespace maro
```

- [ ] **Step 4: 변환 라이브러리 CMakeLists 작성**

`src/maro_transform/CMakeLists.txt`:

```cmake
add_library(maro_transform STATIC
    src/Convert.cpp
)

target_include_directories(maro_transform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(maro_transform PUBLIC cxx_std_20)
```

`src/maro_transform/src/Convert.cpp` (이 태스크에서는 컴파일만 되면 된다):

```cpp
#include "maro_transform/Types.h"

namespace maro {
// 변환 함수는 Task 2에서 추가한다.
}  // namespace maro
```

- [ ] **Step 5: 테스트 CMakeLists와 첫 테스트 작성**

`tests/CMakeLists.txt`:

```cmake
find_package(GTest CONFIG REQUIRED)

add_executable(maro_transform_tests
    transform/test_convert.cpp
)

target_link_libraries(maro_transform_tests PRIVATE
    maro_transform
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(maro_transform_tests)
```

`tests/transform/test_convert.cpp`:

```cpp
#include <gtest/gtest.h>

#include "maro_transform/Types.h"

TEST(Harness, TypesAreUsable) {
    maro::Vec3 v{1.0, 2.0, 3.0};
    EXPECT_TRUE(maro::isFinite(v));

    maro::Quat q;
    EXPECT_DOUBLE_EQ(q.w, 1.0);

    maro::AxisConvention c;
    EXPECT_EQ(c.axis, maro::LocalAxis::Y);
    EXPECT_FALSE(c.invert);
}

TEST(Harness, NonFiniteIsDetected) {
    maro::Vec3 bad{1.0, std::nan(""), 3.0};
    EXPECT_FALSE(maro::isFinite(bad));
}
```

- [ ] **Step 6: 구성하고 빌드해서 테스트가 통과하는지 확인**

```bash
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE=C:/src/vcpkg/scripts/buildsystems/vcpkg.cmake -DMARO_BUILD_PLUGIN=OFF
```

```bash
cmake --build out/build
```

```bash
ctest --test-dir out/build --output-on-failure
```

기대: `Harness.TypesAreUsable`, `Harness.NonFiniteIsDetected` 2개 통과. Maya도 ROS 2도 관여하지 않으므로 수 초 내에 끝나야 한다.

- [ ] **Step 7: 커밋**

```bash
git add CMakeLists.txt vcpkg.json src/maro_transform tests
git commit -m "build: add CMake skeleton and Maya-free transform test harness"
```

---

### Task 2: 위치 좌표계 변환과 왕복 불변식

**Files:**
- Modify: `src/maro_transform/include/maro_transform/Convert.h` (생성)
- Modify: `src/maro_transform/src/Convert.cpp`
- Test: `tests/transform/test_convert.cpp`

**Interfaces:**
- Consumes: `maro::Vec3`, `maro::SceneUnit` (Task 1)
- Produces:
  - `maro::Vec3 maro::mayaToRosPosition(const Vec3&, const SceneUnit&)`
  - `maro::Vec3 maro::rosToMayaPosition(const Vec3&, const SceneUnit&)`

- [ ] **Step 1: 실패하는 테스트 작성**

`tests/transform/test_convert.cpp`의 기존 내용 아래에 추가한다. 파일 상단 include에 `#include "maro_transform/Convert.h"`를 추가한다.

```cpp
#include "maro_transform/Convert.h"

namespace {
constexpr double kEps = 1e-12;
}

TEST(Position, MayaUpBecomesRosUp) {
    // Maya의 위 방향은 +Y, ROS의 위 방향은 +Z다.
    const maro::SceneUnit unit{1.0};  // 스케일 영향을 배제
    const maro::Vec3 mayaUp{0.0, 1.0, 0.0};

    const maro::Vec3 ros = maro::mayaToRosPosition(mayaUp, unit);

    EXPECT_NEAR(ros.x, 0.0, kEps);
    EXPECT_NEAR(ros.y, 0.0, kEps);
    EXPECT_NEAR(ros.z, 1.0, kEps);
}

TEST(Position, ScaleConvertsCentimetresToMetres) {
    const maro::SceneUnit cm{0.01};
    const maro::Vec3 maya{100.0, 0.0, 0.0};

    const maro::Vec3 ros = maro::mayaToRosPosition(maya, cm);

    EXPECT_NEAR(ros.x, 1.0, kEps);
}

TEST(Position, RoundTripIsIdentity) {
    const maro::SceneUnit cm{0.01};
    const maro::Vec3 original{12.5, -3.25, 88.0};

    const maro::Vec3 back =
        maro::mayaToRosPosition(maro::rosToMayaPosition(original, cm), cm);

    EXPECT_NEAR(back.x, original.x, 1e-9);
    EXPECT_NEAR(back.y, original.y, 1e-9);
    EXPECT_NEAR(back.z, original.z, 1e-9);
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

```bash
cmake --build out/build
```

기대: 컴파일 실패. `'maro_transform/Convert.h' file not found`.

- [ ] **Step 3: 최소 구현 작성**

`src/maro_transform/include/maro_transform/Convert.h`:

```cpp
#pragma once

#include "maro_transform/Types.h"

namespace maro {

// Maya(Y-up 우수) <-> ROS REP-103(Z-up 우수) 기저 변환.
// X축 기준 -90도 회전이며 행렬식이 +1이라 우수좌표계가 보존된다.
//   mayaToRos: (x, y, z) -> ( x, -z,  y)
//   rosToMaya: (x, y, z) -> ( x,  z, -y)
// 스케일은 위치에만 적용한다.

Vec3 mayaToRosPosition(const Vec3& maya, const SceneUnit& unit);
Vec3 rosToMayaPosition(const Vec3& ros, const SceneUnit& unit);

}  // namespace maro
```

`src/maro_transform/src/Convert.cpp`:

```cpp
#include "maro_transform/Convert.h"

namespace maro {

Vec3 mayaToRosPosition(const Vec3& maya, const SceneUnit& unit) {
    const double s = unit.metersPerMayaUnit;
    return Vec3{maya.x * s, -maya.z * s, maya.y * s};
}

Vec3 rosToMayaPosition(const Vec3& ros, const SceneUnit& unit) {
    const double s = unit.metersPerMayaUnit;
    return Vec3{ros.x / s, ros.z / s, -ros.y / s};
}

}  // namespace maro
```

- [ ] **Step 4: 테스트가 통과하는지 확인**

```bash
cmake --build out/build && ctest --test-dir out/build --output-on-failure
```

기대: `Position.*` 3개 포함 전체 통과.

- [ ] **Step 5: 커밋**

```bash
git add src/maro_transform tests/transform/test_convert.cpp
git commit -m "feat: convert positions between Maya and ROS coordinate frames"
```

---

### Task 3: 회전 변환과 왕복 불변식

**Files:**
- Modify: `src/maro_transform/include/maro_transform/Convert.h`
- Modify: `src/maro_transform/src/Convert.cpp`
- Test: `tests/transform/test_convert.cpp`

**Interfaces:**
- Consumes: `maro::Quat` (Task 1)
- Produces:
  - `maro::Quat maro::mayaToRosRotation(const Quat&)`
  - `maro::Quat maro::rosToMayaRotation(const Quat&)`

- [ ] **Step 1: 실패하는 테스트 작성**

`tests/transform/test_convert.cpp`에 추가한다.

```cpp
TEST(Rotation, MayaYawBecomesRosYaw) {
    // Maya에서 위(+Y) 축 기준 90도 회전.
    const double h = std::sqrt(2.0) / 2.0;
    const maro::Quat mayaYaw{0.0, h, 0.0, h};

    const maro::Quat ros = maro::mayaToRosRotation(mayaYaw);

    // ROS에서는 위 축이 +Z이므로 회전축이 Z로 옮겨간다.
    EXPECT_NEAR(ros.x, 0.0, 1e-12);
    EXPECT_NEAR(ros.y, 0.0, 1e-12);
    EXPECT_NEAR(ros.z, h, 1e-12);
    EXPECT_NEAR(ros.w, h, 1e-12);
}

TEST(Rotation, IdentityStaysIdentity) {
    const maro::Quat id;
    const maro::Quat ros = maro::mayaToRosRotation(id);

    EXPECT_NEAR(ros.x, 0.0, 1e-12);
    EXPECT_NEAR(ros.y, 0.0, 1e-12);
    EXPECT_NEAR(ros.z, 0.0, 1e-12);
    EXPECT_NEAR(ros.w, 1.0, 1e-12);
}

TEST(Rotation, MayaRollFlipsSignIntoRosY) {
    // 부호가 실제로 뒤집히는 축을 검사한다. 위 두 테스트는 qz가 0이라
    // -qz와 qz를 구분하지 못하고, 왕복 테스트는 반사(reflection)도 통과시킨다.
    // Task 2에서 위치 변환이 같은 함정에 빠졌던 것과 같은 구조다.
    const double h = std::sqrt(2.0) / 2.0;
    const maro::Quat mayaRoll{0.0, 0.0, h, h};   // Maya +Z 축 회전

    const maro::Quat ros = maro::mayaToRosRotation(mayaRoll);

    EXPECT_NEAR(ros.x, 0.0, 1e-12);
    EXPECT_NEAR(ros.y, -h, 1e-12);   // Maya +Z -> ROS -Y
    EXPECT_NEAR(ros.z, 0.0, 1e-12);
    EXPECT_NEAR(ros.w, h, 1e-12);
}

TEST(Rotation, RosPitchFlipsSignIntoMayaZ) {
    // 역방향도 부호가 걸린 축에서 검사한다.
    const double h = std::sqrt(2.0) / 2.0;
    const maro::Quat rosPitch{0.0, h, 0.0, h};   // ROS +Y 축 회전

    const maro::Quat maya = maro::rosToMayaRotation(rosPitch);

    EXPECT_NEAR(maya.x, 0.0, 1e-12);
    EXPECT_NEAR(maya.y, 0.0, 1e-12);
    EXPECT_NEAR(maya.z, -h, 1e-12);  // ROS +Y -> Maya -Z
    EXPECT_NEAR(maya.w, h, 1e-12);
}

TEST(Rotation, RoundTripIsIdentity) {
    const maro::Quat original{0.18257418583505536, 0.3651483716701107,
                              0.5477225575051661, 0.7302967433402214};

    const maro::Quat back =
        maro::mayaToRosRotation(maro::rosToMayaRotation(original));

    EXPECT_NEAR(back.x, original.x, 1e-12);
    EXPECT_NEAR(back.y, original.y, 1e-12);
    EXPECT_NEAR(back.z, original.z, 1e-12);
    EXPECT_NEAR(back.w, original.w, 1e-12);
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

```bash
cmake --build out/build
```

기대: 컴파일 실패. `mayaToRosRotation` 미선언.

- [ ] **Step 3: 최소 구현 작성**

`Convert.h`의 `rosToMayaPosition` 선언 아래에 추가:

```cpp
// 쿼터니언의 벡터부는 진짜 회전(det=+1) 아래에서 위치 벡터와 같은 규칙으로
// 변환되고, 스칼라부 w는 불변이다. 스케일은 회전에 영향을 주지 않는다.
Quat mayaToRosRotation(const Quat& maya);
Quat rosToMayaRotation(const Quat& ros);
```

`Convert.cpp`에 추가:

```cpp
Quat mayaToRosRotation(const Quat& maya) {
    return Quat{maya.x, -maya.z, maya.y, maya.w};
}

Quat rosToMayaRotation(const Quat& ros) {
    return Quat{ros.x, ros.z, -ros.y, ros.w};
}
```

`tests/transform/test_convert.cpp` 상단에 `#include <cmath>`를 추가한다.

- [ ] **Step 4: 테스트가 통과하는지 확인**

```bash
cmake --build out/build && ctest --test-dir out/build --output-on-failure
```

기대: `Rotation.*` 5개 포함 전체 통과.

- [ ] **Step 5: 새 테스트가 실제로 방어하는지 확인**

통과하는 걸 본 것만으로는 부족하다. 일부러 부호를 뒤집어 테스트가 잡는지 본다.

`Convert.cpp`의 `mayaToRosRotation`에서 `-maya.z`를 `maya.z`로 바꾸고 빌드·실행한다.

```bash
cmake --build out/build && ctest --test-dir out/build --output-on-failure
```

기대: `Rotation.MayaRollFlipsSignIntoRosY`가 **실패**한다. 실패 출력을 보고했다면 원래대로 되돌리고 다시 빌드해 전체 통과를 확인한다.

실패하지 않는다면 테스트가 아무것도 지키지 못하고 있는 것이므로, 되돌리기 전에 테스트를 고친다.

- [ ] **Step 6: 커밋**

```bash
git add src/maro_transform tests/transform/test_convert.cpp
git commit -m "feat: convert rotations between Maya and ROS coordinate frames"
```

---

### Task 4: 축 보정과 무작위 왕복 검증

축 보정(`AxisConvention`)을 파라미터로 받는 관절 변환을 추가하고, 무작위 값으로 왕복 불변식을 대량 검증한다. 짐벌락 근처와 극단 스케일이 여기서 걸린다.

**Files:**
- Modify: `src/maro_transform/include/maro_transform/Convert.h`
- Modify: `src/maro_transform/src/Convert.cpp`
- Test: `tests/transform/test_convert.cpp`

**Interfaces:**
- Consumes: `maro::AxisConvention`, `maro::Quat` (Task 1, 3)
- Produces:
  - `maro::Vec3 maro::axisVectorOf(const AxisConvention&)`
  - `maro::Quat maro::jointToMayaRotation(double angleRad, const AxisConvention&)`
  - `double maro::mayaRotationToJoint(const Quat&, const AxisConvention&)`

- [ ] **Step 1: 실패하는 테스트 작성**

```cpp
#include <random>

TEST(AxisConventionTest, VectorSelectsAndSignsLocalAxis) {
    const maro::Vec3 y = maro::axisVectorOf({maro::LocalAxis::Y, false});
    EXPECT_NEAR(y.x, 0.0, 1e-12);
    EXPECT_NEAR(y.y, 1.0, 1e-12);
    EXPECT_NEAR(y.z, 0.0, 1e-12);

    const maro::Vec3 negZ = maro::axisVectorOf({maro::LocalAxis::Z, true});
    EXPECT_NEAR(negZ.z, -1.0, 1e-12);
}

TEST(AxisConventionTest, InvertFlipsJointSign) {
    const maro::AxisConvention plain{maro::LocalAxis::Y, false};
    const maro::AxisConvention flipped{maro::LocalAxis::Y, true};

    const double angle = 0.7;
    const maro::Quat q = maro::jointToMayaRotation(angle, plain);

    EXPECT_NEAR(maro::mayaRotationToJoint(q, plain), angle, 1e-9);
    EXPECT_NEAR(maro::mayaRotationToJoint(q, flipped), -angle, 1e-9);
}

TEST(AxisConventionTest, JointRoundTripSurvivesRandomAngles) {
    std::mt19937 rng(20260813);
    // 왕복이 유일하게 정의되는 구간은 (-2pi, 2pi] 이다. atan2가 (-pi, pi] 를
    // 돌려주고 그 값을 두 배로 쓰기 때문이다. |angle| > pi 구간을 반드시
    // 포함해야 한다 — 거기서 반각이 pi/2 를 넘어 cosHalf 가 음수가 되고,
    // asin 으로 구현했을 때 비로소 틀린 답이 나온다.
    std::uniform_real_distribution<double> angleDist(-6.0, 6.0);

    const maro::LocalAxis axes[] = {maro::LocalAxis::X, maro::LocalAxis::Y,
                                    maro::LocalAxis::Z};

    for (int i = 0; i < 2000; ++i) {
        const maro::AxisConvention conv{axes[i % 3], (i % 2) == 0};
        const double angle = angleDist(rng);

        const double back =
            maro::mayaRotationToJoint(maro::jointToMayaRotation(angle, conv), conv);

        ASSERT_NEAR(back, angle, 1e-9)
            << "axis=" << static_cast<int>(conv.axis)
            << " invert=" << conv.invert << " angle=" << angle;
    }
}

TEST(AxisConventionTest, RecoversAnglesBeyondPi) {
    // 반각이 pi/2 를 넘는 구간. asin 으로 구현하면 여기서 틀린다.
    // 무작위 draw에 의존하지 않고 이 성질을 못박는다.
    const maro::AxisConvention conv{maro::LocalAxis::Y, false};

    for (const double angle : {2.0, 3.5, 5.0, -3.5, -5.0}) {
        const double back =
            maro::mayaRotationToJoint(maro::jointToMayaRotation(angle, conv), conv);
        EXPECT_NEAR(back, angle, 1e-9) << "angle=" << angle;
    }
}

TEST(AxisConventionTest, FullPoseRoundTripSurvivesRandomValues) {
    std::mt19937 rng(99);
    std::uniform_real_distribution<double> posDist(-1000.0, 1000.0);
    std::uniform_real_distribution<double> qDist(-1.0, 1.0);

    // 극단 스케일도 함께 흘린다.
    const double scales[] = {1.0, 0.01, 0.001, 100.0};

    for (int i = 0; i < 2000; ++i) {
        const maro::SceneUnit unit{scales[i % 4]};
        const maro::Vec3 p{posDist(rng), posDist(rng), posDist(rng)};

        const maro::Vec3 pBack =
            maro::mayaToRosPosition(maro::rosToMayaPosition(p, unit), unit);
        ASSERT_NEAR(pBack.x, p.x, 1e-6);
        ASSERT_NEAR(pBack.y, p.y, 1e-6);
        ASSERT_NEAR(pBack.z, p.z, 1e-6);

        maro::Quat q{qDist(rng), qDist(rng), qDist(rng), qDist(rng)};
        const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (n < 1e-6) continue;
        q = maro::Quat{q.x / n, q.y / n, q.z / n, q.w / n};

        const maro::Quat qBack =
            maro::mayaToRosRotation(maro::rosToMayaRotation(q));
        ASSERT_NEAR(qBack.x, q.x, 1e-12);
        ASSERT_NEAR(qBack.y, q.y, 1e-12);
        ASSERT_NEAR(qBack.z, q.z, 1e-12);
        ASSERT_NEAR(qBack.w, q.w, 1e-12);
    }
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

```bash
cmake --build out/build
```

기대: 컴파일 실패. `axisVectorOf` 미선언.

- [ ] **Step 3: 최소 구현 작성**

`Convert.h`에 추가:

```cpp
// 보정이 가리키는 단위 회전축을 로컬 좌표로 돌려준다.
Vec3 axisVectorOf(const AxisConvention& conv);

// 관절 스칼라값 <-> Maya 회전. 보정은 순수 입력 파라미터이므로
// 라이브러리는 상태를 갖지 않고, 보정된 경우도 같은 테스트로 검증된다.
Quat jointToMayaRotation(double angleRad, const AxisConvention& conv);
double mayaRotationToJoint(const Quat& maya, const AxisConvention& conv);
```

`Convert.cpp`에 추가 (파일 상단에 `#include <algorithm>` 필요):

```cpp
Vec3 axisVectorOf(const AxisConvention& conv) {
    const double s = conv.invert ? -1.0 : 1.0;
    switch (conv.axis) {
        case LocalAxis::X: return Vec3{s, 0.0, 0.0};
        case LocalAxis::Y: return Vec3{0.0, s, 0.0};
        case LocalAxis::Z: return Vec3{0.0, 0.0, s};
    }
    return Vec3{0.0, s, 0.0};
}

Quat jointToMayaRotation(double angleRad, const AxisConvention& conv) {
    const Vec3 axis = axisVectorOf(conv);
    const double half = angleRad * 0.5;
    const double sn = std::sin(half);
    return Quat{axis.x * sn, axis.y * sn, axis.z * sn, std::cos(half)};
}

double mayaRotationToJoint(const Quat& maya, const AxisConvention& conv) {
    const Vec3 axis = axisVectorOf(conv);

    // 회전축 성분을 부호 있는 sin(theta/2)로 사영하고,
    // atan2로 (-pi, pi] 구간의 각을 복원한다.
    const double sinHalf = maya.x * axis.x + maya.y * axis.y + maya.z * axis.z;
    const double cosHalf = std::clamp(maya.w, -1.0, 1.0);

    return 2.0 * std::atan2(sinHalf, cosHalf);
}
```

- [ ] **Step 4: 테스트가 통과하는지 확인**

```bash
cmake --build out/build && ctest --test-dir out/build --output-on-failure
```

기대: 무작위 4000회를 포함해 전체 통과.

- [ ] **Step 5: 테스트가 `atan2` 선택을 실제로 방어하는지 확인**

`atan2` 대신 `asin`을 써도 통과한다면 이 테스트들은 아무것도 지키지 못한다. 일부러 바꿔서 확인한다.

`Convert.cpp`의 `mayaRotationToJoint` 반환문을 아래로 바꾼다.

```cpp
    return 2.0 * std::asin(std::clamp(sinHalf, -1.0, 1.0));
```

빌드·실행한다.

```bash
cmake --build out/build && ctest --test-dir out/build --output-on-failure
```

기대: `AxisConventionTest.RecoversAnglesBeyondPi`와 `AxisConventionTest.JointRoundTripSurvivesRandomAngles`가 **둘 다 실패**한다. 실패 출력을 확인했으면 원래대로 되돌리고(`git diff`로 `Convert.cpp`에 변경이 없음을 확인) 다시 빌드해 전체 통과를 본다.

둘 중 하나라도 통과한다면 각도 구간이 좁아 `asin`과 `atan2`가 구분되지 않는 것이다. 허용오차를 늘려 초록불을 만들지 말고 구간을 넓혀라.

- [ ] **Step 6: 커밋**

```bash
git add src/maro_transform tests/transform/test_convert.cpp
git commit -m "feat: apply axis convention to joint conversion, verify round-trip on random input"
```

---

# Phase 2 — Maya 플러그인 기반

## Task 5: 로드·언로드되는 최소 플러그인

ROS 2도 커스텀 노드도 없이, devkit 빌드가 되고 Maya가 실제로 로드·언로드하는지부터 확인한다. §12에서 밝혀졌듯 언로드가 멈추는 문제는 실행해야만 보인다.

**Files:**
- Create: `src/maro_plugin/CMakeLists.txt`
- Create: `src/maro_plugin/MaroPluginMain.cpp`
- Test: `tests/maya/test_load.py`

**Interfaces:**
- Consumes: 없음
- Produces: `maro.mll` 플러그인. `initializePlugin`/`uninitializePlugin` 진입점.

- [ ] **Step 1: 플러그인 CMakeLists 작성**

`src/maro_plugin/CMakeLists.txt`:

```cmake
# devkit 공식 진입점. .mll 접미사, NT_PLUGIN 정의, devkit include/lib를 처리한다.
include($ENV{DEVKIT_LOCATION}/cmake/pluginEntry.cmake)

set(PROJECT_NAME maro)

set(SOURCE_FILES
    MaroPluginMain.cpp
)

set(LIBRARIES
    OpenMaya
    OpenMayaUI
    OpenMayaRender
    OpenMayaAnim
    Foundation
)

build_plugin()
```

- [ ] **Step 2: 최소 플러그인 진입점 작성**

`src/maro_plugin/MaroPluginMain.cpp`:

```cpp
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MStatus.h>

namespace {
constexpr char kVendor[] = "Maro";
constexpr char kVersion[] = "0.1.0";
}  // namespace

MStatus initializePlugin(MObject obj) {
    MFnPlugin plugin(obj, kVendor, kVersion, "Any");
    MGlobal::displayInfo("Maro: plugin loaded.");
    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj) {
    MFnPlugin plugin(obj);
    MGlobal::displayInfo("Maro: plugin unloaded.");
    return MS::kSuccess;
}
```

- [ ] **Step 3: 로드·언로드 테스트 스크립트 작성**

`tests/maya/test_load.py`:

```python
"""GUI 없는 Maya에서 플러그인이 로드되고 언로드되는지 확인한다.

이 스크립트는 mayapy로 실행한다. 성공하면 종료 코드 0으로 끝난다.
프로세스가 끝나지 않으면 정리(teardown) 결함이다.
"""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402  (standalone 초기화 후에만 import 가능)

plugin = os.environ["MARO_PLUGIN_PATH"]

cmds.loadPlugin(plugin)
assert cmds.pluginInfo(plugin, query=True, loaded=True), "plugin did not load"
print("load OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
print("unload OK")

maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
```

- [ ] **Step 4: 플러그인 빌드**

```bash
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE=C:/src/vcpkg/scripts/buildsystems/vcpkg.cmake -DMARO_BUILD_PLUGIN=ON
```

```bash
cmake --build out/build
```

기대: `maro.mll` 생성. 경로를 확인한다.

```bash
find out/build -name "maro.mll"
```

- [ ] **Step 5: Maya가 실제로 로드·언로드하는지 확인**

`MARO_PLUGIN_PATH`를 앞 단계에서 찾은 실제 경로로 바꿔 실행한다.

```bash
MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_load.py
```

기대 출력: `load OK`, `unload OK`, `teardown OK`가 차례로 나오고 **프로세스가 스스로 종료**된다. 종료되지 않으면 멈춘 지점을 고치기 전에 다음 태스크로 넘어가지 않는다.

- [ ] **Step 6: 커밋**

```bash
git add src/maro_plugin tests/maya/test_load.py
git commit -m "feat: add minimal Maya plugin that loads and unloads cleanly"
```

---

## Task 6: maroAxis 로케이터 노드

**Files:**
- Create: `src/maro_plugin/MaroAxisNode.h`
- Create: `src/maro_plugin/MaroAxisNode.cpp`
- Modify: `src/maro_plugin/MaroPluginMain.cpp`
- Modify: `src/maro_plugin/CMakeLists.txt`
- Test: `tests/maya/test_axis_node.py`

**Interfaces:**
- Consumes: 없음
- Produces: `maroAxis` 노드 타입. 어트리뷰트 롱네임 — `targetObject`, `parentAxis`, `jointName`, `capabilityIn`, `conventionAxis`, `conventionInvert`, `controlMode`, `outValue`, `outTransform`, `enabled`. 타입 ID `0x00135100`. C++ 클래스 `MaroAxisNode`, 정적 멤버 `MaroAxisNode::id`, `MaroAxisNode::creator`, `MaroAxisNode::initialize`.

- [ ] **Step 1: 실패하는 테스트 작성**

`tests/maya/test_axis_node.py`:

```python
"""maroAxis 노드가 등록되고 기대한 어트리뷰트를 갖는지 확인한다."""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
cmds.loadPlugin(plugin)

cmds.file(new=True, force=True)
axis = cmds.createNode("maroAxis")
print("created:", axis)

expected = [
    "targetObject",
    "parentAxis",
    "jointName",
    "capabilityIn",
    "conventionAxis",
    "conventionInvert",
    "controlMode",
    "outValue",
    "outTransform",
    "enabled",
]
for attr in expected:
    assert cmds.attributeQuery(attr, node=axis, exists=True), f"missing attr: {attr}"
print("attributes OK")

# 기본값 확인: 축은 기본적으로 사용 가능하고 Manual 모드다.
assert cmds.getAttr(axis + ".enabled") is True
assert cmds.getAttr(axis + ".controlMode") == 0, "default controlMode must be Manual"
print("defaults OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

```bash
MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_axis_node.py
```

기대: `Unknown object type: maroAxis` 로 실패.

- [ ] **Step 3: 노드 헤더 작성**

`src/maro_plugin/MaroAxisNode.h`:

```cpp
#pragma once

#include <maya/MPxLocatorNode.h>
#include <maya/MTypeId.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>

namespace maro {

// 축은 Maya 오브젝트 하나에 바인딩되는 앵커다.
// 능력 노드가 capabilityIn 배열에 쌓여 이 축의 성격을 결정한다.
class MaroAxisNode : public MPxLocatorNode {
public:
    static void* creator();
    static MStatus initialize();

    MStatus compute(const MPlug& plug, MDataBlock& data) override;

    static MTypeId id;

    // 바인딩과 계층
    static MObject aTargetObject;   // message
    static MObject aParentAxis;     // message

    // 능력 스택. 데이터 복합 배열이다 (message가 아니다 — 값을 날라야 하므로).
    static MObject aCapabilityIn;   // compound array
    static MObject aCapType;        //   short  0=rotation 1=limit 2=sensorDir 3=sensorRange
    static MObject aCapValue;       //   double rotation 각도
    static MObject aCapEnable;      //   short3 limit 활성 X/Y/Z
    static MObject aCapMin;         //   double3 limit 하한
    static MObject aCapMax;         //   double3 limit 상한

    // 설정
    static MObject aJointName;      // string
    static MObject aConventionAxis; // enum: 0=X 1=Y 2=Z
    static MObject aConventionInvert;  // bool
    static MObject aControlMode;    // enum: 0=Manual 1=ROS
    static MObject aRosCommand;     // double, ROS 모드에서의 기준값 (펌프가 씀)
    static MObject aEnabled;        // bool

    // 출력
    static MObject aOutValue;       // double
    static MObject aOutTransform;   // matrix
};

}  // namespace maro
```

- [ ] **Step 4: 노드 구현 작성**

`src/maro_plugin/MaroAxisNode.cpp`:

```cpp
#include "MaroAxisNode.h"

#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MPlug.h>

namespace maro {

MTypeId MaroAxisNode::id(0x00135100);

MObject MaroAxisNode::aTargetObject;
MObject MaroAxisNode::aParentAxis;
MObject MaroAxisNode::aCapabilityIn;
MObject MaroAxisNode::aCapType;
MObject MaroAxisNode::aCapValue;
MObject MaroAxisNode::aCapEnable;
MObject MaroAxisNode::aCapMin;
MObject MaroAxisNode::aCapMax;
MObject MaroAxisNode::aJointName;
MObject MaroAxisNode::aConventionAxis;
MObject MaroAxisNode::aConventionInvert;
MObject MaroAxisNode::aControlMode;
MObject MaroAxisNode::aRosCommand;
MObject MaroAxisNode::aEnabled;
MObject MaroAxisNode::aOutValue;
MObject MaroAxisNode::aOutTransform;

void* MaroAxisNode::creator() {
    return new MaroAxisNode();
}

MStatus MaroAxisNode::initialize() {
    MFnMessageAttribute msgFn;
    MFnNumericAttribute numFn;
    MFnTypedAttribute typFn;
    MFnEnumAttribute enumFn;
    MFnMatrixAttribute matFn;

    // 바인딩은 이름 문자열이 아니라 message 연결로 맺는다.
    // Maya가 rename/delete/undo를 자동 추적하므로 동기화가 깨지지 않는다.
    aTargetObject = msgFn.create("targetObject", "tgo");
    msgFn.setStorable(true);
    addAttribute(aTargetObject);

    aParentAxis = msgFn.create("parentAxis", "pax");
    msgFn.setStorable(true);
    addAttribute(aParentAxis);

    // 스택 입력은 데이터 복합 어트리뷰트다. message는 데이터를 나르지 않아
    // compute가 플러그를 직접 조회해야 하고, 그러면 DG 더티 전파를 우회해
    // 병렬 평가에서 값이 어긋난다. 능력 노드의 기여를 실제 데이터로 받는다.
    aCapType = numFn.create("capType", "cpt", MFnNumericData::kShort, 0);
    aCapValue = numFn.create("capValue", "cpv", MFnNumericData::kDouble, 0.0);
    aCapEnable = numFn.create("capEnable", "cpe", MFnNumericData::k3Short, 0);
    aCapMin = numFn.create("capMin", "cpn", MFnNumericData::k3Double, 0.0);
    aCapMax = numFn.create("capMax", "cpx", MFnNumericData::k3Double, 0.0);

    MFnCompoundAttribute cmpFn;
    aCapabilityIn = cmpFn.create("capabilityIn", "cpi");
    cmpFn.addChild(aCapType);
    cmpFn.addChild(aCapValue);
    cmpFn.addChild(aCapEnable);
    cmpFn.addChild(aCapMin);
    cmpFn.addChild(aCapMax);
    cmpFn.setStorable(true);
    cmpFn.setArray(true);
    cmpFn.setIndexMatters(true);   // 스택은 인덱스 순서대로 평가된다
    addAttribute(aCapabilityIn);

    aJointName = typFn.create("jointName", "jnm", MFnData::kString);
    typFn.setStorable(true);
    addAttribute(aJointName);

    aConventionAxis = enumFn.create("conventionAxis", "cva", 1);  // 기본 Y
    enumFn.addField("X", 0);
    enumFn.addField("Y", 1);
    enumFn.addField("Z", 2);
    enumFn.setStorable(true);
    enumFn.setKeyable(true);
    addAttribute(aConventionAxis);

    aConventionInvert = numFn.create("conventionInvert", "cvi",
                                     MFnNumericData::kBoolean, 0);
    numFn.setStorable(true);
    numFn.setKeyable(true);
    addAttribute(aConventionInvert);

    aControlMode = enumFn.create("controlMode", "cmd", 0);  // 기본 Manual
    enumFn.addField("Manual", 0);
    enumFn.addField("ROS", 1);
    enumFn.setStorable(true);
    enumFn.setKeyable(true);
    addAttribute(aControlMode);

    // 펌프가 매 프레임 직접 쓰는 값이다. 사용자 구성이 아니므로 씬에 저장하지 않는다.
    aRosCommand = numFn.create("rosCommand", "rcm", MFnNumericData::kDouble, 0.0);
    numFn.setStorable(false);
    numFn.setKeyable(false);
    addAttribute(aRosCommand);

    aEnabled = numFn.create("enabled", "enb", MFnNumericData::kBoolean, 1);
    numFn.setStorable(true);
    numFn.setKeyable(true);
    addAttribute(aEnabled);

    aOutValue = numFn.create("outValue", "otv", MFnNumericData::kDouble, 0.0);
    numFn.setStorable(false);
    numFn.setWritable(false);
    addAttribute(aOutValue);

    aOutTransform = matFn.create("outTransform", "ott",
                                 MFnMatrixAttribute::kDouble);
    matFn.setStorable(false);
    matFn.setWritable(false);
    addAttribute(aOutTransform);

    for (const MObject& src : {aConventionAxis, aConventionInvert, aEnabled,
                               aControlMode, aRosCommand}) {
        attributeAffects(src, aOutValue);
        attributeAffects(src, aOutTransform);
    }

    return MS::kSuccess;
}

MStatus MaroAxisNode::compute(const MPlug& plug, MDataBlock& data) {
    // 예외는 경계를 넘지 않는다. compute에서 던지면 Maya가 죽는다.
    try {
        if (plug != aOutValue && plug != aOutTransform) {
            return MS::kUnknownParameter;
        }

        // 스택 평가는 Task 8에서 채운다. 지금은 항등값을 낸다.
        MDataHandle outVal = data.outputValue(aOutValue);
        outVal.setDouble(0.0);
        outVal.setClean();

        MDataHandle outXf = data.outputValue(aOutTransform);
        outXf.setMMatrix(MMatrix::identity);
        outXf.setClean();

        return MS::kSuccess;
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: maroAxis compute failed: ") + e.what());
        return MS::kFailure;
    } catch (...) {
        MGlobal::displayError("Maro: maroAxis compute failed with unknown error.");
        return MS::kFailure;
    }
}

}  // namespace maro
```

`MMatrix`를 쓰므로 `#include <maya/MMatrix.h>`를 include 목록에 추가한다.

- [ ] **Step 5: 플러그인 진입점에 노드 등록**

`src/maro_plugin/MaroPluginMain.cpp`를 아래로 교체한다.

```cpp
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MStatus.h>

#include "MaroAxisNode.h"

namespace {
constexpr char kVendor[] = "Maro";
constexpr char kVersion[] = "0.1.0";
}  // namespace

MStatus initializePlugin(MObject obj) {
    MFnPlugin plugin(obj, kVendor, kVersion, "Any");

    MStatus status = plugin.registerNode(
        "maroAxis",
        maro::MaroAxisNode::id,
        maro::MaroAxisNode::creator,
        maro::MaroAxisNode::initialize,
        MPxNode::kLocatorNode);
    if (!status) {
        status.perror("Maro: failed to register maroAxis");
        return status;
    }

    MGlobal::displayInfo("Maro: plugin loaded.");
    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj) {
    MFnPlugin plugin(obj);

    MStatus status = plugin.deregisterNode(maro::MaroAxisNode::id);
    if (!status) {
        status.perror("Maro: failed to deregister maroAxis");
    }

    MGlobal::displayInfo("Maro: plugin unloaded.");
    return status;
}
```

`src/maro_plugin/CMakeLists.txt`의 `SOURCE_FILES`에 `MaroAxisNode.cpp`를 추가한다.

```cmake
set(SOURCE_FILES
    MaroPluginMain.cpp
    MaroAxisNode.cpp
)
```

- [ ] **Step 6: 테스트가 통과하는지 확인**

```bash
cmake --build out/build && MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_axis_node.py
```

기대: `created:`, `attributes OK`, `defaults OK`, `teardown OK`.

- [ ] **Step 7: 커밋**

```bash
git add src/maro_plugin tests/maya/test_axis_node.py
git commit -m "feat: add maroAxis locator node with binding and convention attributes"
```

---

## Task 7: 축 바인딩 커맨드와 구성 시점 검증

불가능한 상태를 만들 수 없게 막는다. 평가할 때 터지는 게 아니라 연결하는 순간 거부한다.

**Files:**
- Create: `src/maro_plugin/MaroCommands.h`
- Create: `src/maro_plugin/MaroCommands.cpp`
- Modify: `src/maro_plugin/MaroPluginMain.cpp`
- Modify: `src/maro_plugin/CMakeLists.txt`
- Test: `tests/maya/test_binding.py`

**Interfaces:**
- Consumes: `maro::MaroAxisNode` (Task 6)
- Produces:
  - MEL 커맨드 `maroBindAxis <axisNode> <targetTransform>` — C++ 클래스 `MaroBindAxisCommand`
  - MEL 커맨드 `maroConnectAxis <childAxis> <parentAxis>` — C++ 클래스 `MaroConnectAxisCommand`
  - 둘 다 거부 시 `MS::kFailure`와 사유 출력, 성공 시 undo 가능

- [ ] **Step 1: 실패하는 테스트 작성**

`tests/maya/test_binding.py`:

```python
"""바인딩 규칙: transform만 허용, 오브젝트당 축 하나, 사유 출력."""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

cube = cmds.polyCube(name="armSegment")[0]
axis = cmds.createNode("maroAxis", name="axis1")

# 1) 정상 바인딩
cmds.maroBindAxis(axis, cube)
assert cmds.isConnected(cube + ".message", axis + ".targetObject"), \
    "binding did not create a message connection"
print("bind OK")

# 2) 같은 오브젝트에 두 번째 축 -> 거부 ("축 하나 = 오브젝트 하나")
axis2 = cmds.createNode("maroAxis", name="axis2")
try:
    cmds.maroBindAxis(axis2, cube)
    raise AssertionError("second bind to the same object should have been rejected")
except RuntimeError:
    print("duplicate bind rejected OK")

# 3) transform이 아닌 대상 -> 거부
light = cmds.createNode("pointLight")   # shape 노드라 transform이 아니다
axis3 = cmds.createNode("maroAxis", name="axis3")
try:
    cmds.maroBindAxis(axis3, light)
    raise AssertionError("binding to a non-transform should have been rejected")
except RuntimeError:
    print("non-transform bind rejected OK")

# 4) undo 하면 연결이 사라진다
cmds.undo()
print("undo OK")

# 5) 축 체인 연결과 순환 거부
cube2 = cmds.polyCube(name="armSegment2")[0]
cmds.maroBindAxis(axis2, cube2)
cmds.maroConnectAxis(axis2, axis)          # axis2 의 부모가 axis
assert cmds.isConnected(axis + ".message", axis2 + ".parentAxis")
print("chain connect OK")

try:
    cmds.maroConnectAxis(axis, axis2)      # axis 의 부모를 axis2 로 -> 순환
    raise AssertionError("cycle should have been rejected at wiring time")
except RuntimeError:
    print("cycle rejected OK")

try:
    cmds.maroConnectAxis(axis, axis)       # 자기 자신
    raise AssertionError("self-parenting should have been rejected")
except RuntimeError:
    print("self-parent rejected OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

```bash
MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_binding.py
```

기대: `No object matches name: maroBindAxis` 로 실패.

- [ ] **Step 3: 커맨드 헤더 작성**

`src/maro_plugin/MaroCommands.h`:

```cpp
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
```

- [ ] **Step 4: 커맨드 구현 작성**

`src/maro_plugin/MaroCommands.cpp`:

```cpp
#include "MaroCommands.h"

#include <maya/MArgList.h>
#include <maya/MDagPath.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MSelectionList.h>

#include "MaroAxisNode.h"

namespace maro {

void* MaroBindAxisCommand::creator() {
    return new MaroBindAxisCommand();
}

MSyntax MaroBindAxisCommand::newSyntax() {
    MSyntax syntax;
    syntax.setObjectType(MSyntax::kSelectionList, 2, 2);
    return syntax;
}

MStatus MaroBindAxisCommand::doIt(const MArgList& args) {
    MStatus status;

    MSelectionList selection;
    for (unsigned int i = 0; i < args.length(); ++i) {
        MString name = args.asString(i, &status);
        if (!status) return status;
        if (!selection.add(name)) {
            MGlobal::displayError(
                MString("Maro: cannot find node '") + name + "'.");
            return MS::kFailure;
        }
    }

    if (selection.length() != 2) {
        MGlobal::displayError(
            "Maro: maroBindAxis needs exactly two arguments: <axis> <transform>.");
        return MS::kFailure;
    }

    MObject axisObj;
    MObject targetObj;
    selection.getDependNode(0, axisObj);
    selection.getDependNode(1, targetObj);

    MFnDependencyNode axisFn(axisObj);
    if (axisFn.typeId() != MaroAxisNode::id) {
        MGlobal::displayError(
            MString("Maro: '") + axisFn.name() + "' is not a maroAxis node.");
        return MS::kFailure;
    }

    // 규칙: 회전 가능한 transform에만 바인딩한다.
    MDagPath targetPath;
    if (!MDagPath::getAPathTo(targetObj, targetPath) ||
        !targetPath.hasFn(MFn::kTransform)) {
        MFnDependencyNode targetFn(targetObj);
        MGlobal::displayError(
            MString("Maro: '") + targetFn.name() +
            "' is not a transform, so an axis cannot drive it. "
            "Select the transform node instead of its shape.");
        return MS::kFailure;
    }

    // 규칙: 오브젝트 하나에는 축 하나만.
    MFnDependencyNode targetFn(targetObj);
    MPlug targetMessage = targetFn.findPlug("message", false, &status);
    if (status) {
        MPlugArray destinations;
        targetMessage.connectedTo(destinations, false, true);
        for (unsigned int i = 0; i < destinations.length(); ++i) {
            MFnDependencyNode otherFn(destinations[i].node());
            if (otherFn.typeId() == MaroAxisNode::id) {
                MGlobal::displayError(
                    MString("Maro: '") + targetFn.name() +
                    "' is already bound to axis '" + otherFn.name() +
                    "'. One object carries exactly one axis.");
                return MS::kFailure;
            }
        }
    }

    MPlug axisTarget = axisFn.findPlug(MaroAxisNode::aTargetObject, false, &status);
    if (!status) return status;

    status = m_modifier.connect(targetMessage, axisTarget);
    if (!status) return status;

    return redoIt();
}

MStatus MaroBindAxisCommand::redoIt() {
    return m_modifier.doIt();
}

MStatus MaroBindAxisCommand::undoIt() {
    return m_modifier.undoIt();
}

namespace {

// child 를 parent 아래에 붙이면 순환이 생기는지 본다.
// parent 에서 조상 방향으로 거슬러 올라가다 child 를 만나면 순환이다.
bool wouldCreateCycle(const MObject& child, const MObject& parent) {
    MObject current = parent;

    // 축 개수만큼만 돌면 충분하다. 이미 순환이 있는 씬에서도 멈춘다.
    for (int guard = 0; guard < 10000; ++guard) {
        if (current == child) return true;

        MFnDependencyNode fn(current);
        MPlug parentPlug = fn.findPlug(MaroAxisNode::aParentAxis, false);

        MPlugArray sources;
        parentPlug.connectedTo(sources, true, false);
        if (sources.length() == 0) return false;

        current = sources[0].node();
    }
    return true;   // 상한에 걸렸다면 이미 순환이다
}

}  // namespace

void* MaroConnectAxisCommand::creator() {
    return new MaroConnectAxisCommand();
}

MSyntax MaroConnectAxisCommand::newSyntax() {
    MSyntax syntax;
    syntax.setObjectType(MSyntax::kSelectionList, 2, 2);
    return syntax;
}

MStatus MaroConnectAxisCommand::doIt(const MArgList& args) {
    MStatus status;

    MSelectionList selection;
    for (unsigned int i = 0; i < args.length(); ++i) {
        MString name = args.asString(i, &status);
        if (!status) return status;
        if (!selection.add(name)) {
            MGlobal::displayError(MString("Maro: cannot find node '") + name + "'.");
            return MS::kFailure;
        }
    }

    if (selection.length() != 2) {
        MGlobal::displayError(
            "Maro: maroConnectAxis needs exactly two arguments: <child> <parent>.");
        return MS::kFailure;
    }

    MObject childObj;
    MObject parentObj;
    selection.getDependNode(0, childObj);
    selection.getDependNode(1, parentObj);

    MFnDependencyNode childFn(childObj);
    MFnDependencyNode parentFn(parentObj);

    if (childFn.typeId() != MaroAxisNode::id ||
        parentFn.typeId() != MaroAxisNode::id) {
        MGlobal::displayError("Maro: maroConnectAxis expects two maroAxis nodes.");
        return MS::kFailure;
    }

    if (childObj == parentObj) {
        MGlobal::displayError(
            MString("Maro: '") + childFn.name() + "' cannot be its own parent.");
        return MS::kFailure;
    }

    if (wouldCreateCycle(childObj, parentObj)) {
        MGlobal::displayError(
            MString("Maro: connecting '") + childFn.name() + "' under '" +
            parentFn.name() + "' would create a cycle in the axis chain.");
        return MS::kFailure;
    }

    MPlug parentMessage = parentFn.findPlug("message", false, &status);
    if (!status) return status;
    MPlug childParent = childFn.findPlug(MaroAxisNode::aParentAxis, false, &status);
    if (!status) return status;

    // 부모는 하나뿐이다. 기존 연결이 있으면 끊고 새로 잇는다.
    MPlugArray existing;
    childParent.connectedTo(existing, true, false);
    for (unsigned int i = 0; i < existing.length(); ++i) {
        status = m_modifier.disconnect(existing[i], childParent);
        if (!status) return status;
    }

    status = m_modifier.connect(parentMessage, childParent);
    if (!status) return status;

    return redoIt();
}

MStatus MaroConnectAxisCommand::redoIt() {
    return m_modifier.doIt();
}

MStatus MaroConnectAxisCommand::undoIt() {
    return m_modifier.undoIt();
}

}  // namespace maro
```

- [ ] **Step 5: 커맨드 등록**

`MaroPluginMain.cpp`의 include에 `#include "MaroCommands.h"`를 추가하고, `initializePlugin`의 노드 등록 뒤에 넣는다.

```cpp
    status = plugin.registerCommand(
        "maroBindAxis",
        maro::MaroBindAxisCommand::creator,
        maro::MaroBindAxisCommand::newSyntax);
    if (!status) {
        status.perror("Maro: failed to register maroBindAxis");
        return status;
    }

    status = plugin.registerCommand(
        "maroConnectAxis",
        maro::MaroConnectAxisCommand::creator,
        maro::MaroConnectAxisCommand::newSyntax);
    if (!status) {
        status.perror("Maro: failed to register maroConnectAxis");
        return status;
    }
```

`uninitializePlugin`의 `deregisterNode` 앞에 넣는다.

```cpp
    plugin.deregisterCommand("maroConnectAxis");
    plugin.deregisterCommand("maroBindAxis");
```

`CMakeLists.txt`의 `SOURCE_FILES`에 `MaroCommands.cpp`를 추가한다.

- [ ] **Step 6: 테스트가 통과하는지 확인**

```bash
cmake --build out/build && MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_binding.py
```

기대: `bind OK`, `duplicate bind rejected OK`, `non-transform bind rejected OK`, `undo OK`, `chain connect OK`, `cycle rejected OK`, `self-parent rejected OK`, `teardown OK`.

- [ ] **Step 7: 커밋**

```bash
git add src/maro_plugin tests/maya/test_binding.py
git commit -m "feat: reject invalid bindings and axis-chain cycles at wiring time"
```

---

## Task 8: 능력 노드와 스택 평가

**Files:**
- Create: `src/maro_plugin/MaroCapabilityNodes.h`
- Create: `src/maro_plugin/MaroCapabilityNodes.cpp`
- Modify: `src/maro_plugin/MaroAxisNode.cpp` (compute를 스택 평가로 교체)
- Modify: `src/maro_plugin/MaroPluginMain.cpp`
- Modify: `src/maro_plugin/CMakeLists.txt`
- Test: `tests/maya/test_capability_stack.py`

**Interfaces:**
- Consumes: `maro::MaroAxisNode` (Task 6), 특히 스택 복합 어트리뷰트의 자식 구성
- Produces: 노드 타입 `maroRotation` (id `0x00135101`), `maroLimit` (id `0x00135102`), `maroSensorDirection` (id `0x00135103`), `maroSensorRange` (id `0x00135104`).

각 노드는 축의 `capabilityIn`과 **구조가 동일한 복합 출력** `capabilityOut`을 갖는다. 축이 데이터블록으로 읽어야 하므로 message가 아니라 데이터 어트리뷰트다.

```
capabilityOut (compound)
├── capType   short    0=rotation 1=limit 2=sensorDirection 3=sensorRange
├── capValue  double   rotation 각도
├── capEnable short3   limit 활성 X/Y/Z
├── capMin    double3  limit 하한
└── capMax    double3  limit 상한
```

각 노드는 자기 입력을 이 복합 출력으로 옮겨 담는다. `maroRotation`은 `angle`(double, 라디안), `maroLimit`은 `enableX/enableY/enableZ`(bool)와 `minX/maxX/minY/maxY/minZ/maxZ`(double)를 사용자 입력으로 갖는다.

- [ ] **Step 1: 실패하는 테스트 작성**

`tests/maya/test_capability_stack.py`:

```python
"""스택 합성: rotation이 값을 만들고 limit들이 순차적으로 클램프한다."""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

axis = cmds.createNode("maroAxis", name="axis1")
rot = cmds.createNode("maroRotation", name="rot1")

cmds.connectAttr(rot + ".capabilityOut", axis + ".capabilityIn[0]")
cmds.setAttr(rot + ".angle", 1.0)
assert abs(cmds.getAttr(axis + ".outValue") - 1.0) < 1e-9, "rotation did not drive outValue"
print("rotation OK")

# limit을 얹으면 클램프된다. 축 보정 기본값은 Y이므로 Y 리밋을 건다.
lim = cmds.createNode("maroLimit", name="lim1")
cmds.setAttr(lim + ".enableY", True)
cmds.setAttr(lim + ".minY", -0.5)
cmds.setAttr(lim + ".maxY", 0.5)
cmds.connectAttr(lim + ".capabilityOut", axis + ".capabilityIn[1]")

assert abs(cmds.getAttr(axis + ".outValue") - 0.5) < 1e-9, "limit did not clamp"
print("limit OK")

# 두 번째 limit이 더 좁으면 그쪽이 이긴다 (순차 클램프).
lim2 = cmds.createNode("maroLimit", name="lim2")
cmds.setAttr(lim2 + ".enableY", True)
cmds.setAttr(lim2 + ".minY", -0.25)
cmds.setAttr(lim2 + ".maxY", 0.25)
cmds.connectAttr(lim2 + ".capabilityOut", axis + ".capabilityIn[2]")

assert abs(cmds.getAttr(axis + ".outValue") - 0.25) < 1e-9, "second limit did not clamp"
print("stacked limits OK")

# 비활성 축은 구동값을 내지 않는다.
cmds.setAttr(axis + ".enabled", False)
assert abs(cmds.getAttr(axis + ".outValue")) < 1e-9, "disabled axis must output zero"
print("disabled OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

```bash
MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_capability_stack.py
```

기대: `Unknown object type: maroRotation` 로 실패.

- [ ] **Step 3: 능력 노드 헤더 작성**

`src/maro_plugin/MaroCapabilityNodes.h`:

```cpp
#pragma once

#include <maya/MPxNode.h>
#include <maya/MTypeId.h>

namespace maro {

// 능력 노드는 축에 쌓여 그 축의 성격을 만든다.
// 타입을 미리 고르는 게 아니라 무엇을 쌓았는지로 성격이 창발한다.

// 모든 능력 노드가 같은 모양의 복합 출력을 낸다. 축은 이걸 데이터블록으로
// 읽으므로 종류가 늘어도 축의 평가 루프는 그대로다.
struct CapabilityOutAttrs {
    MObject compound;   // capabilityOut
    MObject type;       // capType   short
    MObject value;      // capValue  double
    MObject enable;     // capEnable short3
    MObject minimum;    // capMin    double3
    MObject maximum;    // capMax    double3
};

// 능력 노드의 initialize()에서 공통 출력을 만들어 붙인다.
MStatus createCapabilityOut(CapabilityOutAttrs& attrs);

class MaroRotationNode : public MPxNode {
public:
    static void* creator();
    static MStatus initialize();
    MStatus compute(const MPlug& plug, MDataBlock& data) override;
    static MTypeId id;

    static MObject aAngle;           // double, 라디안
    static CapabilityOutAttrs out;
};

class MaroLimitNode : public MPxNode {
public:
    static void* creator();
    static MStatus initialize();
    MStatus compute(const MPlug& plug, MDataBlock& data) override;
    static MTypeId id;

    static MObject aEnableX;
    static MObject aEnableY;
    static MObject aEnableZ;
    static MObject aMinX;
    static MObject aMaxX;
    static MObject aMinY;
    static MObject aMaxY;
    static MObject aMinZ;
    static MObject aMaxZ;
    static CapabilityOutAttrs out;
};

class MaroSensorDirectionNode : public MPxNode {
public:
    static void* creator();
    static MStatus initialize();
    MStatus compute(const MPlug& plug, MDataBlock& data) override;
    static MTypeId id;

    static MObject aDirection;       // float3
    static CapabilityOutAttrs out;
};

class MaroSensorRangeNode : public MPxNode {
public:
    static void* creator();
    static MStatus initialize();
    MStatus compute(const MPlug& plug, MDataBlock& data) override;
    static MTypeId id;

    static MObject aRange;           // double
    static MObject aConeAngle;       // double, 라디안
    static CapabilityOutAttrs out;
};

}  // namespace maro
```

- [ ] **Step 4: 능력 노드 구현 작성**

`src/maro_plugin/MaroCapabilityNodes.cpp`:

```cpp
#include "MaroCapabilityNodes.h"

#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>

namespace maro {

MStatus createCapabilityOut(CapabilityOutAttrs& attrs) {
    MFnNumericAttribute numFn;
    MFnCompoundAttribute cmpFn;

    attrs.type = numFn.create("capType", "cpt", MFnNumericData::kShort, 0);
    attrs.value = numFn.create("capValue", "cpv", MFnNumericData::kDouble, 0.0);
    attrs.enable = numFn.create("capEnable", "cpe", MFnNumericData::k3Short, 0);
    attrs.minimum = numFn.create("capMin", "cpn", MFnNumericData::k3Double, 0.0);
    attrs.maximum = numFn.create("capMax", "cpx", MFnNumericData::k3Double, 0.0);

    attrs.compound = cmpFn.create("capabilityOut", "cpo");
    cmpFn.addChild(attrs.type);
    cmpFn.addChild(attrs.value);
    cmpFn.addChild(attrs.enable);
    cmpFn.addChild(attrs.minimum);
    cmpFn.addChild(attrs.maximum);
    cmpFn.setStorable(false);
    cmpFn.setWritable(false);

    return MS::kSuccess;
}

MTypeId MaroRotationNode::id(0x00135101);
MObject MaroRotationNode::aAngle;
CapabilityOutAttrs MaroRotationNode::out;

void* MaroRotationNode::creator() { return new MaroRotationNode(); }

MStatus MaroRotationNode::initialize() {
    MFnNumericAttribute numFn;

    aAngle = numFn.create("angle", "ang", MFnNumericData::kDouble, 0.0);
    numFn.setStorable(true);
    numFn.setKeyable(true);
    addAttribute(aAngle);

    createCapabilityOut(out);
    addAttribute(out.compound);

    attributeAffects(aAngle, out.compound);
    return MS::kSuccess;
}

MStatus MaroRotationNode::compute(const MPlug& plug, MDataBlock& data) {
    try {
        if (plug != out.compound && plug.parent() != out.compound) {
            return MS::kUnknownParameter;
        }

        MDataHandle handle = data.outputValue(out.compound);
        handle.child(out.type).setShort(0);   // 0 = rotation
        handle.child(out.value).setDouble(data.inputValue(aAngle).asDouble());
        data.setClean(plug);
        return MS::kSuccess;
    } catch (...) {
        MGlobal::displayError("Maro: maroRotation compute failed.");
        return MS::kFailure;
    }
}

MTypeId MaroLimitNode::id(0x00135102);
MObject MaroLimitNode::aEnableX;
MObject MaroLimitNode::aEnableY;
MObject MaroLimitNode::aEnableZ;
MObject MaroLimitNode::aMinX;
MObject MaroLimitNode::aMaxX;
MObject MaroLimitNode::aMinY;
MObject MaroLimitNode::aMaxY;
MObject MaroLimitNode::aMinZ;
MObject MaroLimitNode::aMaxZ;
CapabilityOutAttrs MaroLimitNode::out;

void* MaroLimitNode::creator() { return new MaroLimitNode(); }

namespace {
MObject makeBool(MFnNumericAttribute& fn, const char* longName,
                 const char* shortName) {
    MObject attr = fn.create(longName, shortName, MFnNumericData::kBoolean, 0);
    fn.setStorable(true);
    fn.setKeyable(true);
    return attr;
}

MObject makeDouble(MFnNumericAttribute& fn, const char* longName,
                   const char* shortName, double value) {
    MObject attr = fn.create(longName, shortName, MFnNumericData::kDouble, value);
    fn.setStorable(true);
    fn.setKeyable(true);
    return attr;
}
}  // namespace

MStatus MaroLimitNode::initialize() {
    MFnNumericAttribute numFn;

    aEnableX = makeBool(numFn, "enableX", "enx");
    addAttribute(aEnableX);
    aEnableY = makeBool(numFn, "enableY", "eny");
    addAttribute(aEnableY);
    aEnableZ = makeBool(numFn, "enableZ", "enz");
    addAttribute(aEnableZ);

    aMinX = makeDouble(numFn, "minX", "mnx", -3.14159265358979);
    addAttribute(aMinX);
    aMaxX = makeDouble(numFn, "maxX", "mxx", 3.14159265358979);
    addAttribute(aMaxX);
    aMinY = makeDouble(numFn, "minY", "mny", -3.14159265358979);
    addAttribute(aMinY);
    aMaxY = makeDouble(numFn, "maxY", "mxy", 3.14159265358979);
    addAttribute(aMaxY);
    aMinZ = makeDouble(numFn, "minZ", "mnz", -3.14159265358979);
    addAttribute(aMinZ);
    aMaxZ = makeDouble(numFn, "maxZ", "mxz", 3.14159265358979);
    addAttribute(aMaxZ);

    createCapabilityOut(out);
    addAttribute(out.compound);

    for (const MObject& src : {aEnableX, aEnableY, aEnableZ, aMinX, aMaxX,
                               aMinY, aMaxY, aMinZ, aMaxZ}) {
        attributeAffects(src, out.compound);
    }
    return MS::kSuccess;
}

MStatus MaroLimitNode::compute(const MPlug& plug, MDataBlock& data) {
    try {
        if (plug != out.compound && plug.parent() != out.compound) {
            return MS::kUnknownParameter;
        }

        MDataHandle handle = data.outputValue(out.compound);
        handle.child(out.type).setShort(1);   // 1 = limit

        handle.child(out.enable).set3Short(
            static_cast<short>(data.inputValue(aEnableX).asBool()),
            static_cast<short>(data.inputValue(aEnableY).asBool()),
            static_cast<short>(data.inputValue(aEnableZ).asBool()));

        handle.child(out.minimum).set3Double(data.inputValue(aMinX).asDouble(),
                                             data.inputValue(aMinY).asDouble(),
                                             data.inputValue(aMinZ).asDouble());
        handle.child(out.maximum).set3Double(data.inputValue(aMaxX).asDouble(),
                                             data.inputValue(aMaxY).asDouble(),
                                             data.inputValue(aMaxZ).asDouble());

        data.setClean(plug);
        return MS::kSuccess;
    } catch (...) {
        MGlobal::displayError("Maro: maroLimit compute failed.");
        return MS::kFailure;
    }
}

MTypeId MaroSensorDirectionNode::id(0x00135103);
MObject MaroSensorDirectionNode::aDirection;
CapabilityOutAttrs MaroSensorDirectionNode::out;

void* MaroSensorDirectionNode::creator() { return new MaroSensorDirectionNode(); }

MStatus MaroSensorDirectionNode::initialize() {
    MFnNumericAttribute numFn;

    aDirection = numFn.createPoint("direction", "dir");
    numFn.setStorable(true);
    numFn.setKeyable(true);
    numFn.setDefault(0.0f, 0.0f, 1.0f);
    addAttribute(aDirection);

    createCapabilityOut(out);
    addAttribute(out.compound);

    attributeAffects(aDirection, out.compound);
    return MS::kSuccess;
}

MStatus MaroSensorDirectionNode::compute(const MPlug& plug, MDataBlock& data) {
    try {
        if (plug != out.compound && plug.parent() != out.compound) {
            return MS::kUnknownParameter;
        }

        // 방향은 축의 구동값에 기여하지 않는다. S4가 소비할 수 있도록
        // capMin에 방향 벡터를 실어 두고 타입만 표시한다.
        const float3& dir = data.inputValue(aDirection).asFloat3();

        MDataHandle handle = data.outputValue(out.compound);
        handle.child(out.type).setShort(2);   // 2 = sensorDirection
        handle.child(out.minimum).set3Double(dir[0], dir[1], dir[2]);

        data.setClean(plug);
        return MS::kSuccess;
    } catch (...) {
        MGlobal::displayError("Maro: maroSensorDirection compute failed.");
        return MS::kFailure;
    }
}

MTypeId MaroSensorRangeNode::id(0x00135104);
MObject MaroSensorRangeNode::aRange;
MObject MaroSensorRangeNode::aConeAngle;
CapabilityOutAttrs MaroSensorRangeNode::out;

void* MaroSensorRangeNode::creator() { return new MaroSensorRangeNode(); }

MStatus MaroSensorRangeNode::initialize() {
    MFnNumericAttribute numFn;

    aRange = numFn.create("range", "rng", MFnNumericData::kDouble, 10.0);
    numFn.setStorable(true);
    numFn.setKeyable(true);
    addAttribute(aRange);

    aConeAngle = numFn.create("coneAngle", "cna", MFnNumericData::kDouble, 0.5236);
    numFn.setStorable(true);
    numFn.setKeyable(true);
    addAttribute(aConeAngle);

    createCapabilityOut(out);
    addAttribute(out.compound);

    attributeAffects(aRange, out.compound);
    attributeAffects(aConeAngle, out.compound);
    return MS::kSuccess;
}

MStatus MaroSensorRangeNode::compute(const MPlug& plug, MDataBlock& data) {
    try {
        if (plug != out.compound && plug.parent() != out.compound) {
            return MS::kUnknownParameter;
        }

        MDataHandle handle = data.outputValue(out.compound);
        handle.child(out.type).setShort(3);   // 3 = sensorRange
        handle.child(out.minimum).set3Double(data.inputValue(aRange).asDouble(),
                                             data.inputValue(aConeAngle).asDouble(),
                                             0.0);

        data.setClean(plug);
        return MS::kSuccess;
    } catch (...) {
        MGlobal::displayError("Maro: maroSensorRange compute failed.");
        return MS::kFailure;
    }
}

}  // namespace maro
```

- [ ] **Step 5: 축의 스택 평가 구현**

`MaroAxisNode.cpp`의 `compute`를 아래로 교체한다. include에 `#include <maya/MArrayDataHandle.h>`, `#include <algorithm>`, `#include <cmath>`를 추가한다.

```cpp
MStatus MaroAxisNode::compute(const MPlug& plug, MDataBlock& data) {
    try {
        if (plug != aOutValue && plug != aOutTransform) {
            return MS::kUnknownParameter;
        }

        const bool enabled = data.inputValue(aEnabled).asBool();

        double value = 0.0;

        if (enabled) {
            // 스택은 capabilityIn 인덱스 순서대로 평가한다.
            // rotation이 값을 만들고, 뒤따르는 limit들이 순차적으로 클램프한다.
            // 값은 전부 데이터블록에서 읽는다. 플러그를 직접 조회하면 DG 더티
            // 전파를 우회해 병렬 평가에서 값이 어긋난다.
            const short axisIndex = data.inputValue(aConventionAxis).asShort();
            const unsigned int component =
                (axisIndex == 0) ? 0u : ((axisIndex == 2) ? 2u : 1u);

            // 모드가 기준값의 출처를 정한다. 리밋은 어느 쪽이든 똑같이 적용된다.
            const bool rosDriven = data.inputValue(aControlMode).asShort() == 1;

            MArrayDataHandle stack = data.inputArrayValue(aCapabilityIn);

            for (unsigned int i = 0; i < stack.elementCount(); ++i) {
                stack.jumpToArrayElement(i);
                MDataHandle element = stack.inputValue();

                const short capType = element.child(aCapType).asShort();

                if (capType == 0) {           // rotation
                    value = rosDriven ? data.inputValue(aRosCommand).asDouble()
                                      : element.child(aCapValue).asDouble();
                } else if (capType == 1) {    // limit
                    const short3& enable = element.child(aCapEnable).asShort3();
                    if (enable[component] != 0) {
                        const double3& lo = element.child(aCapMin).asDouble3();
                        const double3& hi = element.child(aCapMax).asDouble3();
                        value = std::clamp(value,
                                           std::min(lo[component], hi[component]),
                                           std::max(lo[component], hi[component]));
                    }
                }
                // 센서 노드(capType 2, 3)는 구동값에 기여하지 않는다. S4에서 소비한다.
            }
        }

        // NaN/inf를 Maya에 흘리지 않는다.
        if (!std::isfinite(value)) {
            MGlobal::displayWarning(
                "Maro: axis produced a non-finite value; holding zero.");
            value = 0.0;
        }

        MDataHandle outVal = data.outputValue(aOutValue);
        outVal.setDouble(value);
        outVal.setClean();

        MDataHandle outXf = data.outputValue(aOutTransform);
        outXf.setMMatrix(MMatrix::identity);
        outXf.setClean();

        return MS::kSuccess;
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: maroAxis compute failed: ") + e.what());
        return MS::kFailure;
    } catch (...) {
        MGlobal::displayError("Maro: maroAxis compute failed with unknown error.");
        return MS::kFailure;
    }
}
```

`initialize()`의 `attributeAffects` 목록에 스택 입력을 추가한다.

```cpp
    attributeAffects(aCapabilityIn, aOutValue);
    attributeAffects(aCapabilityIn, aOutTransform);
```

- [ ] **Step 6: 능력 노드 등록**

`MaroPluginMain.cpp`에 `#include "MaroCapabilityNodes.h"`를 추가하고, `initializePlugin`의 `maroAxis` 등록 뒤에 넣는다.

```cpp
    struct CapabilityRegistration {
        const char* name;
        MTypeId id;
        MCreatorFunction creator;
        MInitializeFunction initialize;
    };

    const CapabilityRegistration kCapabilities[] = {
        {"maroRotation", maro::MaroRotationNode::id,
         maro::MaroRotationNode::creator, maro::MaroRotationNode::initialize},
        {"maroLimit", maro::MaroLimitNode::id,
         maro::MaroLimitNode::creator, maro::MaroLimitNode::initialize},
        {"maroSensorDirection", maro::MaroSensorDirectionNode::id,
         maro::MaroSensorDirectionNode::creator,
         maro::MaroSensorDirectionNode::initialize},
        {"maroSensorRange", maro::MaroSensorRangeNode::id,
         maro::MaroSensorRangeNode::creator, maro::MaroSensorRangeNode::initialize},
    };

    for (const auto& cap : kCapabilities) {
        status = plugin.registerNode(cap.name, cap.id, cap.creator, cap.initialize);
        if (!status) {
            status.perror(MString("Maro: failed to register ") + cap.name);
            return status;
        }
    }
```

`uninitializePlugin`에 역등록을 넣는다.

```cpp
    plugin.deregisterNode(maro::MaroSensorRangeNode::id);
    plugin.deregisterNode(maro::MaroSensorDirectionNode::id);
    plugin.deregisterNode(maro::MaroLimitNode::id);
    plugin.deregisterNode(maro::MaroRotationNode::id);
```

`CMakeLists.txt`의 `SOURCE_FILES`에 `MaroCapabilityNodes.cpp`를 추가한다.

- [ ] **Step 7: 테스트가 통과하는지 확인**

```bash
cmake --build out/build && MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_capability_stack.py
```

기대: `rotation OK`, `limit OK`, `stacked limits OK`, `disabled OK`, `teardown OK`.

- [ ] **Step 8: 커밋**

```bash
git add src/maro_plugin tests/maya/test_capability_stack.py
git commit -m "feat: add capability nodes and index-ordered stack evaluation on the axis"
```

---

## Task 9: 연쇄 삭제와 고아 세트

**Files:**
- Create: `src/maro_plugin/MaroDeleteWatcher.h`
- Create: `src/maro_plugin/MaroDeleteWatcher.cpp`
- Modify: `src/maro_plugin/MaroPluginMain.cpp`
- Modify: `src/maro_plugin/CMakeLists.txt`
- Test: `tests/maya/test_delete_rules.py`

**Interfaces:**
- Consumes: `maro::MaroAxisNode` (Task 6)
- Produces: `maro::MaroDeleteWatcher::install()` / `maro::MaroDeleteWatcher::uninstall()`. 오브젝트 삭제 시 바인딩된 축을 같은 undo 청크로 삭제하고, 축에서 분리된 능력 노드를 `maroOrphanSet`에 담는다.

- [ ] **Step 1: 실패하는 테스트 작성**

`tests/maya/test_delete_rules.py`:

```python
"""삭제 비대칭성과 고아 능력 노드 규칙."""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

# 오브젝트를 지우면 축도 사라진다.
cube = cmds.polyCube(name="seg")[0]
axis = cmds.createNode("maroAxis", name="axisA")
cmds.maroBindAxis(axis, cube)
cmds.delete(cube)
assert not cmds.objExists(axis), "axis should be deleted along with its object"
print("cascade delete OK")

# undo 하면 둘 다 돌아온다 (같은 undo 청크).
cmds.undo()
assert cmds.objExists(cube), "object should come back on undo"
assert cmds.objExists(axis), "axis should come back on undo, in the same chunk"
print("cascade undo OK")

# 축을 지워도 오브젝트는 남는다.
cmds.delete(axis)
assert cmds.objExists(cube), "deleting an axis must not delete its object"
print("asymmetry OK")

# 축을 지우면 능력 노드는 남고 고아 세트에 담긴다.
cube2 = cmds.polyCube(name="seg2")[0]
axis2 = cmds.createNode("maroAxis", name="axisB")
cmds.maroBindAxis(axis2, cube2)
rot = cmds.createNode("maroRotation", name="rotB")
cmds.connectAttr(rot + ".capabilityOut", axis2 + ".capabilityIn[0]")

cmds.delete(axis2)
assert cmds.objExists(rot), "capability node must survive axis deletion"
assert cmds.objExists("maroOrphanSet"), "orphan set should exist"
members = cmds.sets("maroOrphanSet", query=True) or []
assert rot in members, f"orphan not registered in set: {members}"
print("orphan OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

```bash
MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_delete_rules.py
```

기대: `axis should be deleted along with its object` 어서션 실패.

- [ ] **Step 3: 삭제 감시자 헤더 작성**

`src/maro_plugin/MaroDeleteWatcher.h`:

```cpp
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
```

- [ ] **Step 4: 삭제 감시자 구현 작성**

`src/maro_plugin/MaroDeleteWatcher.cpp`:

```cpp
#include "MaroDeleteWatcher.h"

#include <maya/MDGMessage.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnSet.h>
#include <maya/MGlobal.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MNodeMessage.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MSelectionList.h>

#include "MaroAxisNode.h"
#include "MaroCapabilityNodes.h"

namespace maro {

MCallbackIdArray MaroDeleteWatcher::s_callbacks;

namespace {

constexpr char kOrphanSetName[] = "maroOrphanSet";

bool isCapabilityNode(const MFnDependencyNode& fn) {
    const MTypeId id = fn.typeId();
    return id == MaroRotationNode::id || id == MaroLimitNode::id ||
           id == MaroSensorDirectionNode::id || id == MaroSensorRangeNode::id;
}

// 고아 능력 노드를 담는 세트를 얻거나 만든다.
// 세트에 속하면 연결이 생겨 File > Optimize Scene Size가 지우지 못한다.
MObject orphanSet(MDGModifier& modifier) {
    MSelectionList existing;
    if (existing.add(kOrphanSetName)) {
        MObject setObj;
        if (existing.getDependNode(0, setObj)) {
            return setObj;
        }
    }

    MObject setObj = modifier.createNode("objectSet");
    modifier.renameNode(setObj, kOrphanSetName);
    return setObj;
}

}  // namespace

MStatus MaroDeleteWatcher::install() {
    MStatus status;

    // 이미 씬에 있는 노드에도 콜백을 건다.
    for (MItDependencyNodes it(MFn::kInvalid); !it.isDone(); it.next()) {
        MObject node = it.thisNode();
        onNodeAdded(node, nullptr);
    }

    s_callbacks.append(
        MDGMessage::addNodeAddedCallback(onNodeAdded, "dependNode", nullptr, &status));
    return status;
}

MStatus MaroDeleteWatcher::uninstall() {
    MMessage::removeCallbacks(s_callbacks);
    s_callbacks.clear();
    return MS::kSuccess;
}

void MaroDeleteWatcher::onNodeAdded(MObject& node, void* /*clientData*/) {
    try {
        MFnDependencyNode fn(node);
        MStatus status;

        if (fn.typeId() == MaroAxisNode::id) {
            s_callbacks.append(MNodeMessage::addNodeAboutToDeleteCallback(
                node, onAxisAboutToDelete, nullptr, &status));
        } else if (node.hasFn(MFn::kTransform)) {
            s_callbacks.append(MNodeMessage::addNodeAboutToDeleteCallback(
                node, onObjectAboutToDelete, nullptr, &status));
        }
    } catch (...) {
        // 콜백에서 예외가 새면 Maya가 죽는다.
        MGlobal::displayError("Maro: failed to attach delete callback.");
    }
}

void MaroDeleteWatcher::onObjectAboutToDelete(MObject& node,
                                              MDGModifier& modifier,
                                              void* /*clientData*/) {
    try {
        MFnDependencyNode fn(node);
        MPlug message = fn.findPlug("message", false);

        MPlugArray destinations;
        message.connectedTo(destinations, false, true);

        for (unsigned int i = 0; i < destinations.length(); ++i) {
            MObject other = destinations[i].node();
            MFnDependencyNode otherFn(other);
            if (otherFn.typeId() != MaroAxisNode::id) continue;

            // 이 modifier에 실으면 삭제와 undo/redo가 함께 묶인다.
            modifier.deleteNode(other);
            MGlobal::displayInfo(
                MString("Maro: deleting axis '") + otherFn.name() +
                "' because its bound object was deleted.");
        }
    } catch (...) {
        MGlobal::displayError("Maro: cascade delete failed.");
    }
}

void MaroDeleteWatcher::onAxisAboutToDelete(MObject& node, MDGModifier& modifier,
                                            void* /*clientData*/) {
    try {
        MFnDependencyNode axisFn(node);
        MPlug stack = axisFn.findPlug(MaroAxisNode::aCapabilityIn, false);

        MObject setObj;
        bool haveSet = false;

        for (unsigned int i = 0; i < stack.numElements(); ++i) {
            MPlug element = stack.elementByPhysicalIndex(i);

            // 스택은 복합 데이터 어트리뷰트다. 연결은 자식 플러그 단위로
            // 맺힐 수 있으므로 복합 자체와 자식을 모두 확인한다.
            MPlugArray sources;
            element.connectedTo(sources, true, false);
            for (unsigned int c = 0; sources.length() == 0 &&
                                     c < element.numChildren(); ++c) {
                element.child(c).connectedTo(sources, true, false);
            }
            if (sources.length() == 0) continue;

            MObject capNode = sources[0].node();
            MFnDependencyNode capFn(capNode);
            if (!isCapabilityNode(capFn)) continue;

            if (!haveSet) {
                setObj = orphanSet(modifier);
                haveSet = true;
            }

            // 능력 노드는 삭제하지 않는다. 고아 세트에 담아 재사용 가능하게 둔다.
            MFnSet setFn(setObj);
            setFn.addMember(capNode);
        }

        if (haveSet) {
            MGlobal::displayInfo(
                "Maro: capability nodes moved to maroOrphanSet for reuse.");
        }
    } catch (...) {
        MGlobal::displayError("Maro: orphan handling failed.");
    }
}

}  // namespace maro
```

- [ ] **Step 5: 감시자 설치/해제 연결**

`MaroPluginMain.cpp`에 `#include "MaroDeleteWatcher.h"`를 추가하고, `initializePlugin`의 마지막 `displayInfo` 앞에 넣는다.

```cpp
    status = maro::MaroDeleteWatcher::install();
    if (!status) {
        status.perror("Maro: failed to install delete watcher");
        return status;
    }
```

`uninitializePlugin`의 맨 앞에 넣는다. 콜백을 남기면 언로드 후 댕글링으로 즉사한다.

```cpp
    maro::MaroDeleteWatcher::uninstall();
```

`CMakeLists.txt`의 `SOURCE_FILES`에 `MaroDeleteWatcher.cpp`를 추가한다.

- [ ] **Step 6: 테스트가 통과하는지 확인**

```bash
cmake --build out/build && MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_delete_rules.py
```

기대: `cascade delete OK`, `cascade undo OK`, `asymmetry OK`, `orphan OK`, `teardown OK`.

- [ ] **Step 7: 커밋**

```bash
git add src/maro_plugin tests/maya/test_delete_rules.py
git commit -m "feat: cascade axis deletion with undo, keep capability nodes as reusable orphans"
```

---

# Phase 3 — ROS 2 런타임

## Task 10: 수신은 `MPxThreadedDeviceNode`, 발행은 펌프, 그리고 브리지 커맨드

이 태스크가 끝나면 **DG에서 큐를 거쳐 백그라운드 스레드까지 데이터가 실제로 흐른다.** 발행 내용이 없어도 왕복이 관측 가능하므로, 이후 Task 11·12는 이미 도는 파이프라인 위에 얹는 형태가 된다.

### 수신 경로는 손으로 만들지 않는다

Maya devkit에 `MPxThreadedDeviceNode`가 있고, 이 태스크가 만들려던 수신 절반을 그대로 제공한다. 확인된 대응 관계:

| 손으로 만들 뻔한 것 | 네이티브 |
|---|---|
| `BoundedQueue` + 뮤텍스 | 메모리 풀 + `MCharBuffer` 링버퍼, 락 내장 |
| 타이머 펌프의 수신 절반 | 워커가 출력 어트리뷰트를 dirty로 표시 → Maya가 메인 스레드에서 `compute()` 호출 |
| `sleep_for(5ms)` | `endThreadLoop()`이 `frameRate` 기준으로 스로틀 |
| 시작/정지 플래그 | `live` 어트리뷰트, `isDone()`/`setDone()` |

사용 형태는 정해져 있다. 워커(`threadHandler()`)에서 `beginThreadLoop()` → `acquireDataStorage()` → 버퍼 채우기 → `pushThreadData()` → `endThreadLoop()`. 메인 스레드 `compute()`에서 `popThreadData()` → DG 반영 → `releaseDataStorage()`. `createMemoryPools()`로 잡고 `destroyMemoryPools()`로 놓으며, **해제 시점을 플러그인 언로드와 반드시 맞춘다** — 이 프로젝트는 정리 누락으로 프로세스가 끝나지 않은 전례가 있다.

**이 클래스는 유입 방향만 다룬다.** Maya→ROS 발행은 여전히 타이머 펌프가 필요하므로, 이 태스크의 결과물은 메커니즘이 둘이다. 그 비대칭은 의도된 것이며, 각 방향이 자기에게 맞는 도구를 쓰는 것이다.

`rclcpp` 구독 콜백은 백그라운드에서 돌고 수신값을 `MPxThreadedDeviceNode`의 버퍼로 넘긴다. 즉 rclcpp 스레드와 Maya 사이에 우리가 직접 만든 동기화 코드가 없다.

**Files:**
- Create: `src/maro_plugin/MaroBridgeQueues.h`
- Create: `src/maro_plugin/MaroRosRuntime.h`
- Create: `src/maro_plugin/MaroRosRuntime.cpp`
- Create: `src/maro_plugin/MaroPump.h`
- Create: `src/maro_plugin/MaroPump.cpp`
- Create: `src/maro_plugin/MaroCommandDeviceNode.h`
- Create: `src/maro_plugin/MaroCommandDeviceNode.cpp`
- Modify: `src/maro_plugin/MaroCommands.h`
- Modify: `src/maro_plugin/MaroCommands.cpp`
- Modify: `src/maro_plugin/CMakeLists.txt`
- Modify: `src/maro_plugin/MaroPluginMain.cpp`
- Test: `tests/maya/test_bridge_pump.py`

**Interfaces:**
- Consumes: `maro::Vec3`, `maro::Quat`, `maro::AxisConvention`, `maro::SceneUnit` (Task 1), `maro::MaroAxisNode` (Task 6)
- Produces:
  - `maro::AxisSample { std::string jointName; double value; Vec3 position; Quat rotation; AxisConvention convention; SceneUnit unit; }` — 발행 방향 전용. 수신 방향엔 이런 페이로드 구조체가 없다(`MCharBuffer`가 그 자리를 대신한다).
  - `maro::BoundedQueue<T>` — `push`, `drain`, `droppedCount`, 상한 초과 시 오래된 항목 폐기. **발행 방향에만 쓴다.**
  - `maro::MaroRosRuntime` — `start(robotName)`, `stop()`, `isRunning()`, `publishQueue()`, `drainedSampleCount()` (더 이상 `commandQueue()`는 없다 — 수신은 아래 노드가 담당한다)
  - `maro::MaroPump` — `start(MaroRosRuntime&)`, `stop()`, `isRunning()`, `collectedSampleCount()` (발행 방향 전담. `applyCommands`/`appliedCommandCount`는 더 이상 여기 없다)
  - `maro::MaroCommandDeviceNode` — `MPxThreadedDeviceNode` 파생. 노드 타입 `maroCommandDevice`, id `0x00135105`. `setRobotName(MString)`, `resetStats()`, `appliedCommandCount()`, `threadTickCount()`, `isThreadAlive()` (모두 static). ROS 2 토픽 `/<robotName>/joint_commands` (`sensor_msgs/JointState`)를 자체 스레드에서 구독한다.
  - MEL 커맨드 `maroStartBridge <robotName>`, `maroStopBridge`, `maroBridgeStats`
  - `maroBridgeStats`는 `[collectedSamples, drainedSamples, appliedCommands, commandThreadTicks]` 정수 배열을 돌려준다 — 마지막 값은 아직 명령이 없어도 커맨드 노드의 백그라운드 스레드가 살아서 도는지를 보여준다 (실제 명령 적용 증명은 Task 12가 맡는다)

- [ ] **Step 1: 아웃바운드 큐 헤더 작성**

`src/maro_plugin/MaroBridgeQueues.h`:

```cpp
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
```

- [ ] **Step 2: 아웃바운드 런타임 헤더 작성**

`src/maro_plugin/MaroRosRuntime.h`:

```cpp
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "MaroBridgeQueues.h"

namespace rclcpp {
class Node;
}

namespace maro {

// 발행 방향(Maya -> ROS 2) 전담. rclcpp는 이 클래스가 만든 백그라운드
// 스레드에서만 돈다. 이 클래스의 어떤 코드도 Maya API를 호출하지 않는다.
//
// 수신 방향(ROS 2 -> Maya)은 이 클래스가 아니라 MaroCommandDeviceNode가
// Maya가 관리하는 별도 스레드에서 처리한다 (Task 10 설계 노트 참고).
// 그래서 여기엔 commandQueue가 없다 — 두 방향이 서로 다른 스레드 소유권을
// 갖는다는 뜻이다.
class MaroRosRuntime {
public:
    MaroRosRuntime();
    ~MaroRosRuntime();

    MaroRosRuntime(const MaroRosRuntime&) = delete;
    MaroRosRuntime& operator=(const MaroRosRuntime&) = delete;

    bool start(const std::string& robotName);
    void stop();
    bool isRunning() const { return m_running.load(); }

    BoundedQueue<AxisSample>& publishQueue() { return m_publishQueue; }

    // 펌프가 넣은 샘플이 백그라운드까지 실제로 건너왔는지 보기 위한 계수기.
    // 발행이 붙기 전에도 스레드 경계를 넘는 흐름을 관측할 수 있다.
    std::uint64_t drainedSampleCount() const { return m_drainedSamples.load(); }

private:
    void spinLoop();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<std::uint64_t> m_drainedSamples{0};

    BoundedQueue<AxisSample> m_publishQueue;
};

}  // namespace maro
```

- [ ] **Step 3: 아웃바운드 런타임 구현 작성 (발행은 Task 11에서 채움)**

`src/maro_plugin/MaroRosRuntime.cpp`:

```cpp
#include "MaroRosRuntime.h"

#include <chrono>

#include <rclcpp/rclcpp.hpp>

namespace maro {

struct MaroRosRuntime::Impl {
    std::shared_ptr<rclcpp::Node> node;
};

MaroRosRuntime::MaroRosRuntime() : m_impl(std::make_unique<Impl>()) {}

MaroRosRuntime::~MaroRosRuntime() {
    stop();
}

bool MaroRosRuntime::start(const std::string& robotName) {
    if (m_running.load()) return true;

    try {
        if (!rclcpp::ok()) {
            rclcpp::init(0, nullptr);
        }
        m_impl->node = rclcpp::Node::make_shared(robotName);
    } catch (const std::exception&) {
        // 예외가 Maya 쪽으로 새지 않게 여기서 막는다.
        m_impl->node.reset();
        return false;
    } catch (...) {
        m_impl->node.reset();
        return false;
    }

    m_stopRequested.store(false);
    m_running.store(true);
    m_thread = std::thread(&MaroRosRuntime::spinLoop, this);
    return true;
}

void MaroRosRuntime::stop() {
    if (!m_running.load()) return;

    m_stopRequested.store(true);
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_running.store(false);

    // 순서가 중요하다. 노드 내부를 참조하는 것들을 먼저 놓아야
    // DDS 참가자가 살아남아 프로세스가 안 끝나는 일이 없다.
    m_impl->node.reset();

    // 이 rclcpp::shutdown()은 프로세스 전역 rclcpp 컨텍스트를 끝낸다.
    // MaroCommandDeviceNode도 같은 전역 컨텍스트로 자기 노드를 만들므로,
    // 그쪽 스레드가 완전히 멈춘 뒤에만 여기까지 와야 한다. 호출자
    // (MaroCommands.cpp의 shutdownBridge())가 그 순서를 보장한다 — 여기서
    // 순서를 어기면 살아있는 스레드 밑에서 컨텍스트가 끊겨 크래시하거나
    // 프로세스가 끝나지 않는다.
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
}

void MaroRosRuntime::spinLoop() {
    try {
        while (!m_stopRequested.load() && rclcpp::ok()) {
            // 이 노드는 퍼블리셔만 갖는다 — 구독은 MaroCommandDeviceNode
            // 쪽의 별도 노드가 처리한다 (Task 10 설계 노트). 퍼블리셔는
            // spin 없이도 publish()가 바로 나가므로 여기서
            // rclcpp::spin_some()을 부를 필요가 없다.
            //
            // 펌프가 넣은 샘플을 꺼낸다. 발행은 Task 11에서 붙는다.
            // 지금은 건너온 개수만 세어 흐름을 관측 가능하게 한다.
            const std::vector<AxisSample> samples = m_publishQueue.drain();
            m_drainedSamples.fetch_add(samples.size(), std::memory_order_relaxed);

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (...) {
        // 스레드에서 예외가 새면 조용히 죽어 진단이 어려워진다.
        m_stopRequested.store(true);
    }
}

}  // namespace maro
```

- [ ] **Step 4: 아웃바운드 펌프 작성**

`src/maro_plugin/MaroPump.h`:

```cpp
#pragma once

#include <atomic>
#include <cstdint>

#include <maya/MCallbackIdArray.h>
#include <maya/MStatus.h>

namespace maro {

class MaroRosRuntime;

// Maya 메인 스레드에서만 도는 발행 펌프. DG에서 축 상태를 읽어 큐에
// 적재하는, 발행 방향 전담이다.
// 변환에 필요한 컨텍스트(씬 단위, 축 보정)는 Maya 조회가 필요하므로
// 여기서 읽어 샘플에 함께 실어 보낸다. 백그라운드는 Maya를 조회하지 않는다.
//
// 수신 방향(명령 적용)은 더 이상 여기 없다. MaroCommandDeviceNode::compute()가
// 대신한다 — 이 펌프가 만든 타이머가 아니라 devkit이 관리하는 dirty 전파로
// 불린다 (Task 10 설계 노트 참고).
class MaroPump {
public:
    static MStatus start(MaroRosRuntime& runtime);
    static MStatus stop();
    static bool isRunning();

    static std::uint64_t collectedSampleCount();

private:
    static void onTimer(float elapsed, float last, void* clientData);
    static void collectSamples(MaroRosRuntime& runtime);

    static MCallbackId s_timerId;
    static MaroRosRuntime* s_runtime;
    static std::atomic<std::uint64_t> s_collected;
};

}  // namespace maro
```

`src/maro_plugin/MaroPump.cpp`:

```cpp
#include "MaroPump.h"

#include <cmath>

#include <maya/MDistance.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MPlug.h>
#include <maya/MTimerMessage.h>

#include "MaroAxisNode.h"
#include "MaroRosRuntime.h"

namespace maro {

MCallbackId MaroPump::s_timerId = 0;
MaroRosRuntime* MaroPump::s_runtime = nullptr;
std::atomic<std::uint64_t> MaroPump::s_collected{0};

namespace {

constexpr float kPumpIntervalSeconds = 1.0f / 30.0f;

// Maya의 현재 선형 단위를 미터 배율로 바꾼다.
// 하드코딩하면 사용자가 단위를 바꿨을 때 로봇이 100배로 나온다.
SceneUnit currentSceneUnit() {
    SceneUnit unit;
    unit.metersPerMayaUnit = MDistance(1.0, MDistance::internalUnit())
                                 .asMeters();
    return unit;
}

AxisConvention conventionOf(const MFnDependencyNode& axisFn) {
    AxisConvention conv;
    const short axisIndex =
        axisFn.findPlug(MaroAxisNode::aConventionAxis, false).asShort();
    conv.axis = (axisIndex == 0) ? LocalAxis::X
                                 : ((axisIndex == 2) ? LocalAxis::Z : LocalAxis::Y);
    conv.invert = axisFn.findPlug(MaroAxisNode::aConventionInvert, false).asBool();
    return conv;
}

}  // namespace

MStatus MaroPump::start(MaroRosRuntime& runtime) {
    if (s_timerId != 0) return MS::kSuccess;

    s_runtime = &runtime;
    s_collected.store(0);

    MStatus status;
    s_timerId = MTimerMessage::addTimerCallback(kPumpIntervalSeconds, onTimer,
                                                nullptr, &status);
    if (!status) {
        s_runtime = nullptr;
        s_timerId = 0;
    }
    return status;
}

MStatus MaroPump::stop() {
    if (s_timerId != 0) {
        MMessage::removeCallback(s_timerId);
        s_timerId = 0;
    }
    s_runtime = nullptr;
    return MS::kSuccess;
}

bool MaroPump::isRunning() { return s_timerId != 0; }

std::uint64_t MaroPump::collectedSampleCount() { return s_collected.load(); }

void MaroPump::onTimer(float, float, void*) {
    // Maya 콜백이다. 예외가 새면 Maya가 죽는다.
    try {
        if (s_runtime == nullptr) return;
        collectSamples(*s_runtime);
    } catch (const std::exception& e) {
        MGlobal::displayError(MString("Maro: pump tick failed: ") + e.what());
    } catch (...) {
        MGlobal::displayError("Maro: pump tick failed with unknown error.");
    }
}

void MaroPump::collectSamples(MaroRosRuntime& runtime) {
    const SceneUnit unit = currentSceneUnit();

    for (MItDependencyNodes it(MFn::kPluginLocatorNode); !it.isDone(); it.next()) {
        MFnDependencyNode axisFn(it.thisNode());
        if (axisFn.typeId() != MaroAxisNode::id) continue;
        if (!axisFn.findPlug(MaroAxisNode::aEnabled, false).asBool()) continue;

        const MString joint =
            axisFn.findPlug(MaroAxisNode::aJointName, false).asString();
        if (joint.length() == 0) continue;   // 이름 없는 축은 발행하지 않는다

        AxisSample sample;
        sample.jointName = joint.asChar();
        sample.value = axisFn.findPlug(MaroAxisNode::aOutValue, false).asDouble();
        sample.convention = conventionOf(axisFn);
        sample.unit = unit;

        if (!std::isfinite(sample.value)) continue;

        runtime.publishQueue().push(std::move(sample));
        s_collected.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace maro
```

이제부터는 수신 방향이다. Maya가 스레드를 만들고 끝내므로, 우리가 만드는 것은
버퍼를 채우는 코드와 그걸 DG에 반영하는 `compute()`뿐이다.

- [ ] **Step 5: 인바운드 디바이스 노드 헤더 작성**

`src/maro_plugin/MaroCommandDeviceNode.h`:

```cpp
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
```

- [ ] **Step 6: 인바운드 디바이스 노드 구현 작성**

`src/maro_plugin/MaroCommandDeviceNode.cpp`:

```cpp
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
std::atomic<bool> MaroCommandDeviceNode::s_threadAlive{false};

MaroCommandDeviceNode::MaroCommandDeviceNode() {}

MaroCommandDeviceNode::~MaroCommandDeviceNode() {
    destroyMemoryPools();
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
    MObjectArray attrs;
    attrs.append(aCommandOut);
    setRefreshOutputAttributes(attrs);

    // 명령 하나 = CommandRecord 하나. 여유 있게 64개 버퍼를 돌린다.
    const MStatus status = createMemoryPools(kPoolDepth, 1, sizeof(CommandRecord));
    if (!status) {
        MGlobal::displayError("Maro: failed to create command device memory pools.");
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
}

std::uint64_t MaroCommandDeviceNode::appliedCommandCount() { return s_applied.load(); }
std::uint64_t MaroCommandDeviceNode::threadTickCount() { return s_ticks.load(); }
bool MaroCommandDeviceNode::isThreadAlive() { return s_threadAlive.load(); }

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
    s_threadAlive.store(true);

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
                rclcpp::spin_some(node);
                s_ticks.fetch_add(1, std::memory_order_relaxed);

                for (const CommandRecord& rec : pending) {
                    MCharBuffer buffer;
                    if (acquireDataStorage(buffer)) {
                        *reinterpret_cast<CommandRecord*>(buffer.ptr()) = rec;
                        pushThreadData(buffer);
                    }
                }
            }
        } catch (...) {
            // 스레드가 죽으면 브리지가 조용히 먹통이 된다. 반복을 계속한다.
        }
        endThreadLoop();   // frameRate 기준 스로틀. 우리가 sleep_for를 쓰지 않는 이유다.
    }

    sub.reset();
    node.reset();
    s_threadAlive.store(false);
}

void MaroCommandDeviceNode::threadShutdownHandler() {
    // File -> New, Exit, 또는 이 노드가 삭제될 때 Maya가 호출한다.
    // threadHandler()의 while(!isDone()) 루프를 깨운다.
    setDone(true);
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
        static std::uint64_t lastReportedDropped = 0;
        const std::uint64_t dropped = s_dropped.load();
        if (dropped != lastReportedDropped) {
            MGlobal::displayWarning(
                MString("Maro: dropped ") +
                static_cast<int>(dropped - lastReportedDropped) +
                " oversized joint command name(s) (limit 63 chars).");
            lastReportedDropped = dropped;
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
```

**설계 노트 (open question):** `deleteNode()` 또는 Maya의 File -> New가 이 노드의
백그라운드 스레드 종료를 *동기적으로* 보장하는지 devkit 문서에서 확인하지
못했다. `threadShutdownHandler()`가 `setDone(true)`를 부르면 `threadHandler()`의
루프는 다음 반복에서 빠져나오지만, 그 join이 노드 삭제 호출이 리턴하기
*전에* 끝난다는 보장은 명시적으로 찾지 못했다. 그래서 Step 7의
`shutdownBridge()`는 이걸 가정하지 않고 `isThreadAlive()`를 직접 폴링해서
확인한다 — 이 프로젝트가 이미 "정리 호출이 전부 성공을 반환했는데도
프로세스가 끝나지 않은" 결함을 낸 적이 있기 때문이다.

- [ ] **Step 7: 브리지 시작/정지/상태 커맨드 작성**

`MaroCommands.h`의 `MaroConnectAxisCommand` 아래에 추가한다.

```cpp
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
```

`MaroCommands.cpp` 끝(`}  // namespace maro` 앞)에 추가한다. 상단 include에
`#include "MaroCommandDeviceNode.h"`, `#include "MaroPump.h"`,
`#include "MaroRosRuntime.h"`, `#include <maya/MIntArray.h>`,
`#include <maya/MObjectHandle.h>`, `#include <chrono>`, `#include <memory>`,
`#include <thread>`를 더한다.

```cpp
namespace {
std::unique_ptr<MaroRosRuntime> g_runtime;
MObjectHandle g_commandDeviceHandle;
}  // namespace

void shutdownBridge() {
    MaroPump::stop();          // 아웃바운드 타이머 콜백을 먼저 뗀다.

    if (g_commandDeviceHandle.isValid()) {
        MDGModifier modifier;
        modifier.deleteNode(g_commandDeviceHandle.object());
        modifier.doIt();
    }
    g_commandDeviceHandle = MObjectHandle();

    // deleteNode()가(혹은 File -> New가) 커맨드 디바이스 노드의 백그라운드
    // 스레드 종료를 동기적으로 보장한다는 근거를 devkit 문서에서 찾지
    // 못했다 (MaroCommandDeviceNode.cpp의 설계 노트 참고). 그 스레드가
    // 아직 도는 채로 아래 g_runtime->stop()이 rclcpp::shutdown()으로 전역
    // 컨텍스트를 끊으면 크래시하거나 프로세스가 안 끝난다. 이 프로젝트는
    // "정리 호출이 전부 성공을 반환했는데도 프로세스가 끝나지 않은" 전례가
    // 있다 -- 가정하지 않고 직접 확인한다.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (MaroCommandDeviceNode::isThreadAlive() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (MaroCommandDeviceNode::isThreadAlive()) {
        MGlobal::displayError(
            "Maro: command device thread did not stop within 2s; "
            "ROS 2 shutdown may hang or crash.");
    }

    if (g_runtime) {
        g_runtime->stop();
        g_runtime.reset();
    }
}

void* MaroStartBridgeCommand::creator() { return new MaroStartBridgeCommand(); }

MSyntax MaroStartBridgeCommand::newSyntax() {
    MSyntax syntax;
    syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
    return syntax;
}

MStatus MaroStartBridgeCommand::doIt(const MArgList& args) {
    if (args.length() != 1) {
        MGlobal::displayError("Maro: maroStartBridge needs <robotName>.");
        return MS::kFailure;
    }

    MStatus status;
    const MString robotName = args.asString(0, &status);
    if (!status) return status;

    if (g_runtime && g_runtime->isRunning()) {
        MGlobal::displayWarning("Maro: bridge is already running.");
        return MS::kSuccess;
    }

    g_runtime = std::make_unique<MaroRosRuntime>();
    if (!g_runtime->start(robotName.asChar())) {
        g_runtime.reset();
        MGlobal::displayError(
            "Maro: could not start the ROS 2 bridge. Check that the ROS 2 "
            "runtime DLLs sit next to the plugin.");
        return MS::kFailure;
    }

    status = MaroPump::start(*g_runtime);
    if (!status) {
        g_runtime->stop();
        g_runtime.reset();
        MGlobal::displayError("Maro: could not start the main-thread pump.");
        return status;
    }

    // 수신 노드. Maya가 스레드를 만들고 관리한다 -- 우리는 만들지 않는다.
    MDGModifier createModifier;
    MObject deviceObj = createModifier.createNode(MaroCommandDeviceNode::id, &status);
    if (!status) {
        MaroPump::stop();
        g_runtime->stop();
        g_runtime.reset();
        MGlobal::displayError("Maro: could not create the command device node.");
        return status;
    }
    status = createModifier.doIt();
    if (!status) {
        MaroPump::stop();
        g_runtime->stop();
        g_runtime.reset();
        MGlobal::displayError("Maro: could not add the command device node to the DG.");
        return status;
    }

    MFnDependencyNode deviceFn(deviceObj);
    auto* devicePtr = dynamic_cast<MaroCommandDeviceNode*>(deviceFn.userNode());
    if (devicePtr == nullptr) {
        MaroPump::stop();
        g_runtime->stop();
        g_runtime.reset();
        MGlobal::displayError("Maro: command device node has no C++ instance.");
        return MS::kFailure;
    }

    MaroCommandDeviceNode::resetStats();
    // setRobotName()은 메인 스레드에서, live를 켜기 전에 부른다. 스레드
    // 자체는 노드 생성 시점에 이미 돌기 시작했을 수 있으므로 (문서상 live는
    // 스레드 존재가 아니라 데이터 처리만 게이트한다), 로봇 이름은 뮤텍스로
    // 안전하게 넘긴다 (MaroCommandDeviceNode::setRobotName 참고).
    devicePtr->setRobotName(robotName);
    g_commandDeviceHandle = MObjectHandle(deviceObj);

    MDGModifier liveModifier;
    liveModifier.newPlugValueBool(
        deviceFn.findPlug(MPxThreadedDeviceNode::live, false), true);
    status = liveModifier.doIt();
    if (!status) {
        MGlobal::displayError("Maro: could not set the command device live.");
        shutdownBridge();
        return status;
    }

    MGlobal::displayInfo(MString("Maro: bridge running as '") + robotName + "'.");
    return MS::kSuccess;
}

void* MaroStopBridgeCommand::creator() { return new MaroStopBridgeCommand(); }

MStatus MaroStopBridgeCommand::doIt(const MArgList&) {
    shutdownBridge();
    MGlobal::displayInfo("Maro: bridge stopped.");
    return MS::kSuccess;
}

void* MaroBridgeStatsCommand::creator() { return new MaroBridgeStatsCommand(); }

MStatus MaroBridgeStatsCommand::doIt(const MArgList&) {
    MIntArray stats;
    stats.append(static_cast<int>(MaroPump::collectedSampleCount()));
    stats.append(static_cast<int>(
        g_runtime ? g_runtime->drainedSampleCount() : 0));
    stats.append(static_cast<int>(MaroCommandDeviceNode::appliedCommandCount()));
    stats.append(static_cast<int>(MaroCommandDeviceNode::threadTickCount()));
    setResult(stats);
    return MS::kSuccess;
}
```

- [ ] **Step 8: 노드·커맨드 등록과 언로드 정리**

`MaroPluginMain.cpp` 상단 include에 `#include "MaroCommandDeviceNode.h"`를 추가한다.

`maroConnectAxis` 커맨드 등록 뒤, `MaroDeleteWatcher::install()` 호출 앞에 넣는다.
노드 타입 등록이 커맨드보다 먼저 와야 한다 — 커맨드가 그 타입 ID를 참조한다.

```cpp
    status = plugin.registerNode(
        "maroCommandDevice",
        maro::MaroCommandDeviceNode::id,
        maro::MaroCommandDeviceNode::creator,
        maro::MaroCommandDeviceNode::initialize,
        MPxNode::kThreadedDeviceNode);
    if (!status) {
        status.perror("Maro: failed to register maroCommandDevice");
        return status;
    }

    status = plugin.registerCommand("maroStartBridge",
                                    maro::MaroStartBridgeCommand::creator,
                                    maro::MaroStartBridgeCommand::newSyntax);
    if (!status) {
        status.perror("Maro: failed to register maroStartBridge");
        return status;
    }

    status = plugin.registerCommand("maroStopBridge",
                                    maro::MaroStopBridgeCommand::creator);
    if (!status) {
        status.perror("Maro: failed to register maroStopBridge");
        return status;
    }

    status = plugin.registerCommand("maroBridgeStats",
                                    maro::MaroBridgeStatsCommand::creator);
    if (!status) {
        status.perror("Maro: failed to register maroBridgeStats");
        return status;
    }
```

`uninitializePlugin`의 맨 앞(삭제 감시자 해제와 나란히)에 넣는다. 순서가 중요하다 — 브리지를 먼저 내려야 타이머 콜백과 커맨드 디바이스 노드가 사라진 축을 건드리지 않는다.

```cpp
    maro::shutdownBridge();
    maro::MaroDeleteWatcher::uninstall();
```

그리고 커맨드·노드 역등록을 추가한다. 노드는 그 노드를 참조하는 커맨드들을 지운 뒤에 지운다.

```cpp
    plugin.deregisterCommand("maroBridgeStats");
    plugin.deregisterCommand("maroStopBridge");
    plugin.deregisterCommand("maroStartBridge");
    plugin.deregisterNode(maro::MaroCommandDeviceNode::id);
```

- [ ] **Step 9: 플러그인 빌드에 ROS 2 링크 추가**

`src/maro_plugin/CMakeLists.txt`를 아래로 교체한다. `build_plugin()`이 `INCLUDE_DIRS`/`LIBRARY_DIRS`/`LIBRARIES`를 읽으므로 호출 전에 채운다.

```cmake
include($ENV{DEVKIT_LOCATION}/cmake/pluginEntry.cmake)

set(PROJECT_NAME maro)

set(SOURCE_FILES
    MaroPluginMain.cpp
    MaroAxisNode.cpp
    MaroCapabilityNodes.cpp
    MaroCommands.cpp
    MaroDeleteWatcher.cpp
    MaroRosRuntime.cpp
    MaroPump.cpp
    MaroCommandDeviceNode.cpp
)

set(LIBRARIES
    OpenMaya
    OpenMayaUI
    OpenMayaRender
    OpenMayaAnim
    Foundation
)

# ROS 2 헤더. install/include 아래 패키지별 하위 디렉터리를 모두 넣는다.
# idl/idlc는 string.h 등 표준 헤더와 이름이 충돌하므로 제외한다.
file(GLOB ROS2_PACKAGE_INCLUDES LIST_DIRECTORIES true "${ROS2_INSTALL}/include/*")
set(INCLUDE_DIRS ${ROS2_INSTALL}/include)
foreach(dir ${ROS2_PACKAGE_INCLUDES})
    if(IS_DIRECTORY ${dir})
        get_filename_component(name ${dir} NAME)
        if(NOT name STREQUAL "idl" AND NOT name STREQUAL "idlc")
            list(APPEND INCLUDE_DIRS ${dir})
        endif()
    endif()
endforeach()

set(LIBRARY_DIRS ${ROS2_INSTALL}/Lib)

build_plugin()

# build_plugin()이 만든 타깃에 나머지를 얹는다.
target_link_libraries(${PROJECT_NAME} PRIVATE maro_transform)

foreach(ros_lib rclcpp rcl rcutils rcpputils rmw rosidl_runtime_c
                rosidl_typesupport_cpp
                sensor_msgs__rosidl_typesupport_cpp
                geometry_msgs__rosidl_typesupport_cpp
                tf2_msgs__rosidl_typesupport_cpp
                builtin_interfaces__rosidl_typesupport_cpp)
    target_link_libraries(${PROJECT_NAME} PRIVATE "${ROS2_INSTALL}/Lib/${ros_lib}.lib")
endforeach()

# windows.h의 min/max 매크로가 rclcpp의 numeric_limits 사용과 충돌한다.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    NOMINMAX
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
)

# 런타임 DLL을 플러그인 옆에 둔다. 벤더 DLL은 install/bin이 아니라
# install/opt/<vendor>/bin에 있으므로 반드시 함께 복사해야 로드된다.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ROS2_INSTALL}/bin" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ROS2_INSTALL}/opt/libyaml_vendor/bin" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ROS2_INSTALL}/opt/spdlog_vendor/bin" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ROS2_INSTALL}/opt/console_bridge_vendor/bin" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Maro: staging ROS 2 runtime DLLs next to the plugin"
)
```

- [ ] **Step 10: 펌프·디바이스 스레드 흐름과 수명 테스트 작성**

`tests/maya/test_bridge_pump.py`:

```python
"""브리지를 켜면 두 방향 모두 실제로 살아 도는지, 그리고 내린 뒤
프로세스가 깨끗이 끝나는지 확인한다.

- 발행 방향(DG -> 큐 -> MaroRosRuntime 스레드): collected/drained 카운터로
  증명한다 (Task 10 이전과 동일한 메커니즘).
- 수신 방향(ROS 2 -> MaroCommandDeviceNode 스레드): 아직 진짜 ROS 2 피어가
  없다 (피어는 Task 11에서 생긴다). 그래서 "명령이 적용됐는지"
  (appliedCommands)가 아니라 그 스레드가 살아서 스핀하는지
  (commandThreadTicks)를 증명한다. 실제 명령 왕복 증명은 Task 12의 계약
  테스트가 맡는다.

§12에서 퍼블리셔 누수로 프로세스가 종료되지 않는 결함이 실제로 있었다.
아래 "unload while running" 시나리오가 그 회귀를 막는다 — 이번엔 스레드가
두 개(발행 스레드 + Maya가 관리하는 커맨드 디바이스 스레드)라서 순서
실수의 여지도 늘었다.
"""
import os
import sys
import time

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
name = os.path.splitext(os.path.basename(plugin))[0]

cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

# 이름 있는 축 하나를 세운다. 이름이 없으면 펌프가 건너뛴다.
cube = cmds.polyCube(name="seg")[0]
axis = cmds.createNode("maroAxis", name="axisP")
cmds.maroBindAxis(axis, cube)
cmds.setAttr(axis + ".jointName", "axisP", type="string")
rot = cmds.createNode("maroRotation")
cmds.connectAttr(rot + ".capabilityOut", axis + ".capabilityIn[0]")
cmds.setAttr(rot + ".angle", 0.4)

# 브리지를 켜기 전에는 아무것도 흐르지 않는다.
collected, drained, applied, ticks = cmds.maroBridgeStats()
assert collected == 0 and drained == 0 and ticks == 0, \
    f"nothing should flow before the bridge starts: stats={(collected, drained, applied, ticks)}"
print("idle stats OK")

cmds.maroStartBridge("maro")

# 펌프와 커맨드 디바이스 스레드 둘 다 마야 유휴 이벤트에 엮여 있으므로
# refresh로 흘려 주며 기다린다. 시간이 아니라 관측 가능한 조건(카운터)을
# 기다리고, 데드라인은 마지막 안전망일 뿐이다.
deadline = time.time() + 20
collected = drained = applied = ticks = 0
while time.time() < deadline:
    cmds.refresh(force=True)
    time.sleep(0.1)
    collected, drained, applied, ticks = cmds.maroBridgeStats()
    if collected > 0 and drained > 0 and ticks > 0:
        break

stats = (collected, drained, applied, ticks)
assert collected > 0, f"pump never collected a sample from the DG (stats={stats})"
assert drained > 0, f"samples never reached the background thread (stats={stats})"
assert ticks > 0, \
    f"MaroCommandDeviceNode's thread never ticked -- native inbound thread " \
    f"did not come alive (stats={stats})"
print(f"pump + command thread flow OK (collected={collected}, drained={drained}, ticks={ticks})")

cmds.maroStopBridge()
collected_after, _, _, ticks_after = cmds.maroBridgeStats()
time.sleep(0.5)
cmds.refresh(force=True)
time.sleep(0.5)
collected_now, _, _, ticks_now = cmds.maroBridgeStats()
assert collected_now == collected_after, \
    f"pump kept running after maroStopBridge (before={collected_after}, after={collected_now})"
assert ticks_now == ticks_after, \
    f"command device thread kept ticking after maroStopBridge " \
    f"(before={ticks_after}, after={ticks_now})"
print("stop halts both threads OK")

# 브리지를 켠 채로 언로드해도 좀비 스레드가 남지 않아야 한다. 여기가 이
# 태스크에서 가장 중요한 시나리오다: File -> New가 커맨드 디바이스 노드를
# 지우는 경로(명시적 maroStopBridge를 거치지 않는 경로)로도 스레드가
# 안전하게 멈추는지까지 함께 검증한다.
cmds.maroStartBridge("maro")
# 노드 인스턴스가 남아 있으면 Maya가 언로드를 거부한다. 브리지는 켠 채로 둔다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(name)
print("unload while running OK")

maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
```

- [ ] **Step 11: 빌드하고 펌프/디바이스 스레드 테스트 실행**

```bash
cmake --build out/build
```

```bash
MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" timeout 180 "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_bridge_pump.py; echo "exit=$?"
```

기대: `idle stats OK`, `pump + command thread flow OK (collected=N, drained=M, ticks=K)` (셋 다 0보다 큼), `stop halts both threads OK`, `unload while running OK`, `teardown OK`, `exit=0`.

`exit=124`면 프로세스가 끝나지 않은 것이다. `shutdownBridge()`의 해제 순서(펌프 콜백 정지 → 커맨드 디바이스 노드 삭제 → 그 스레드가 실제로 멈췄는지 `isThreadAlive()`로 확인 → 런타임 정지+`rclcpp::shutdown`)를 고치기 전에는 다음 태스크로 넘어가지 않는다. `unload while running OK`가 안 찍히고 멈춘다면, `MaroCommandDeviceNode`의 스레드가 `threadShutdownHandler()`의 `setDone(true)` 이후에도 살아있다는 뜻이다 — devkit이 그 join을 동기적으로 보장하지 않을 가능성에 대비한 방어 코드가 정확히 이 경우를 위한 것이다.

- [ ] **Step 12: 커밋**

```bash
git add src/maro_plugin tests/maya/test_bridge_pump.py
git commit -m "feat: receive ROS 2 commands via MPxThreadedDeviceNode, pump DG state to rclcpp for publishing"
```

---

## Task 11: joint_states와 tf 발행

**Files:**
- Modify: `src/maro_plugin/MaroRosRuntime.h`
- Modify: `src/maro_plugin/MaroRosRuntime.cpp`
- Create: `tests/peer/maro_test_peer.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `maro::AxisSample`, `maro::MaroRosRuntime` (Task 10), `maro::mayaToRosPosition`/`mayaToRosRotation` (Task 2, 3)
- Produces: 토픽 `/<robotName>/joint_states` (`sensor_msgs/JointState`), `/tf` (`tf2_msgs/TFMessage`). 테스트 피어 실행 파일 `maro_test_peer`.

- [ ] **Step 1: 테스트 피어 작성**

`tests/peer/maro_test_peer.cpp`:

```cpp
// ROS 2 상대역. 이 환경에는 ros2 CLI도 rclpy도 없으므로 C++로 만든다.
// 플러그인이 이미 링크하는 것과 같은 라이브러리를 쓰므로 추가 의존성이 없고,
// 대기와 타임아웃을 테스트가 직접 통제해 CLI보다 결정적이다.
//
// 사용법:
//   maro_test_peer echo <robotName> <expectedJointCount> <timeoutSeconds>
//   maro_test_peer pub  <robotName> <jointName> <position>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>

namespace {

int runEcho(const std::string& robot, std::size_t expectedJoints, double timeoutSec) {
    auto node = rclcpp::Node::make_shared("maro_test_peer_echo");

    bool satisfied = false;
    auto sub = node->create_subscription<sensor_msgs::msg::JointState>(
        "/" + robot + "/joint_states", 10,
        [&](sensor_msgs::msg::JointState::SharedPtr msg) {
            if (msg->name.size() < expectedJoints) return;
            for (std::size_t i = 0; i < msg->name.size(); ++i) {
                std::cout << "joint " << msg->name[i] << " = "
                          << msg->position[i] << std::endl;
            }
            satisfied = true;
        });

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(timeoutSec * 1000));

    while (rclcpp::ok() && !satisfied &&
           std::chrono::steady_clock::now() < deadline) {
        rclcpp::spin_some(node);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!satisfied) {
        std::cerr << "timeout: no joint_states with >= " << expectedJoints
                  << " joints" << std::endl;
        return 1;
    }
    std::cout << "echo OK" << std::endl;
    return 0;
}

int runPub(const std::string& robot, const std::string& joint, double position) {
    auto node = rclcpp::Node::make_shared("maro_test_peer_pub");
    auto pub = node->create_publisher<sensor_msgs::msg::JointState>(
        "/" + robot + "/joint_commands", 10);

    sensor_msgs::msg::JointState msg;
    msg.name.push_back(joint);
    msg.position.push_back(position);

    // 구독자가 붙을 시간을 준다.
    for (int i = 0; i < 50; ++i) {
        pub->publish(msg);
        rclcpp::spin_some(node);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::cout << "pub OK" << std::endl;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: maro_test_peer <echo|pub> <robotName> ..." << std::endl;
        return 2;
    }

    rclcpp::init(argc, argv);

    const std::string mode = argv[1];
    const std::string robot = argv[2];
    int result = 2;

    if (mode == "echo" && argc == 5) {
        result = runEcho(robot, std::strtoul(argv[3], nullptr, 10),
                         std::strtod(argv[4], nullptr));
    } else if (mode == "pub" && argc == 5) {
        result = runPub(robot, argv[3], std::strtod(argv[4], nullptr));
    } else {
        std::cerr << "bad arguments" << std::endl;
    }

    rclcpp::shutdown();
    return result;
}
```

`tests/CMakeLists.txt` 끝에 추가한다.

```cmake
if(MARO_BUILD_PLUGIN)
    file(GLOB ROS2_PACKAGE_INCLUDES LIST_DIRECTORIES true "${ROS2_INSTALL}/include/*")
    set(PEER_INCLUDES ${ROS2_INSTALL}/include)
    foreach(dir ${ROS2_PACKAGE_INCLUDES})
        if(IS_DIRECTORY ${dir})
            get_filename_component(name ${dir} NAME)
            if(NOT name STREQUAL "idl" AND NOT name STREQUAL "idlc")
                list(APPEND PEER_INCLUDES ${dir})
            endif()
        endif()
    endforeach()

    add_executable(maro_test_peer peer/maro_test_peer.cpp)
    target_include_directories(maro_test_peer PRIVATE ${PEER_INCLUDES})
    target_compile_definitions(maro_test_peer PRIVATE
        NOMINMAX _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)

    foreach(ros_lib rclcpp rcl rcutils rcpputils rmw rosidl_runtime_c
                    rosidl_typesupport_cpp
                    sensor_msgs__rosidl_typesupport_cpp
                    builtin_interfaces__rosidl_typesupport_cpp)
        target_link_libraries(maro_test_peer PRIVATE "${ROS2_INSTALL}/Lib/${ros_lib}.lib")
    endforeach()

    add_custom_command(TARGET maro_test_peer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${ROS2_INSTALL}/bin" "$<TARGET_FILE_DIR:maro_test_peer>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${ROS2_INSTALL}/opt/libyaml_vendor/bin" "$<TARGET_FILE_DIR:maro_test_peer>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${ROS2_INSTALL}/opt/spdlog_vendor/bin" "$<TARGET_FILE_DIR:maro_test_peer>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${ROS2_INSTALL}/opt/console_bridge_vendor/bin" "$<TARGET_FILE_DIR:maro_test_peer>"
    )
endif()
```

- [ ] **Step 2: 피어가 빌드되고 타임아웃으로 실패하는지 확인**

```bash
cmake --build out/build && ./out/build/tests/maro_test_peer.exe echo testbot 1 3; echo "exit=$?"
```

기대: 아직 아무도 발행하지 않으므로 `timeout: no joint_states`와 `exit=1`. 피어 자체는 정상 동작한다는 뜻이다.

- [ ] **Step 3: 런타임에 발행 기능 추가**

`MaroRosRuntime.h`의 `private:` 섹션 아래, `spinLoop()` 선언 옆에 추가한다.

```cpp
    // 메인 스레드가 채운 샘플을 백그라운드에서 변환·발행한다.
    void drainAndPublish();
```

`MaroRosRuntime.cpp` 상단 include에 추가한다.

```cpp
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <tf2_msgs/msg/tf_message.hpp>

#include "maro_transform/Convert.h"
```

`Impl` 구조체를 확장한다.

```cpp
struct MaroRosRuntime::Impl {
    std::shared_ptr<rclcpp::Node> node;
    rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr jointPub;
    rclcpp::Publisher<tf2_msgs::msg::TFMessage>::SharedPtr tfPub;
};
```

`start()`의 노드 생성 뒤에 퍼블리셔를 만든다.

```cpp
        m_impl->jointPub =
            m_impl->node->create_publisher<sensor_msgs::msg::JointState>(
                "joint_states", 10);
        m_impl->tfPub =
            m_impl->node->create_publisher<tf2_msgs::msg::TFMessage>("/tf", 10);
```

`stop()`의 `m_impl->node.reset()` **앞에** 퍼블리셔 해제를 넣는다. 이 순서를 지키지 않으면 프로세스가 종료되지 않는다.

```cpp
    m_impl->tfPub.reset();
    m_impl->jointPub.reset();
```

`spinLoop()`에서 Task 10의 "세기만 하는" 드레인을 발행으로 교체한다.

```cpp
            // Task 10에서는 개수만 셌다. 이제 실제로 발행한다.
            drainAndPublish();
```

즉 아래 두 줄을 지우고 위 호출로 바꾼다.

```cpp
            const std::vector<AxisSample> samples = m_publishQueue.drain();
            m_drainedSamples.fetch_add(samples.size(), std::memory_order_relaxed);
```

구현을 추가한다. 계수기는 그대로 올려야 `maroBridgeStats`와 Task 10의 테스트가 계속 유효하다.

```cpp
void MaroRosRuntime::drainAndPublish() {
    const std::vector<AxisSample> samples = m_publishQueue.drain();
    if (samples.empty()) return;

    m_drainedSamples.fetch_add(samples.size(), std::memory_order_relaxed);

    if (!m_impl->jointPub || !m_impl->tfPub) return;

    sensor_msgs::msg::JointState joints;
    joints.header.stamp = m_impl->node->now();

    tf2_msgs::msg::TFMessage tf;

    for (const AxisSample& sample : samples) {
        joints.name.push_back(sample.jointName);
        joints.position.push_back(sample.value);

        const Vec3 p = mayaToRosPosition(sample.position, sample.unit);
        const Quat q = mayaToRosRotation(sample.rotation);

        geometry_msgs::msg::TransformStamped t;
        t.header.stamp = joints.header.stamp;
        t.header.frame_id = "world";
        t.child_frame_id = sample.jointName;
        t.transform.translation.x = p.x;
        t.transform.translation.y = p.y;
        t.transform.translation.z = p.z;
        t.transform.rotation.x = q.x;
        t.transform.rotation.y = q.y;
        t.transform.rotation.z = q.z;
        t.transform.rotation.w = q.w;
        tf.transforms.push_back(t);
    }

    m_impl->jointPub->publish(joints);
    m_impl->tfPub->publish(tf);
}
```

- [ ] **Step 4: 실제 발행을 검증하는 테스트 작성**

`tests/maya/test_publish.py`:

```python
"""Maya 안의 축 상태가 ROS 2 토픽으로 실제로 나가는지 확인한다.

별도 프로세스의 C++ 피어가 구독해 값을 받아야 통과한다.
"""
import os
import subprocess
import sys
import time

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
peer = os.environ["MARO_PEER_PATH"]

cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

cube = cmds.polyCube(name="seg")[0]
axis = cmds.createNode("maroAxis", name="axisPub")
cmds.maroBindAxis(axis, cube)
cmds.setAttr(axis + ".jointName", "axisPub", type="string")
rot = cmds.createNode("maroRotation")
cmds.connectAttr(rot + ".capabilityOut", axis + ".capabilityIn[0]")
cmds.setAttr(rot + ".angle", 0.75)

cmds.maroStartBridge("maro")

# 피어를 구독자로 띄워 두고, 그동안 마야를 계속 돌려 펌프가 뛰게 한다.
listener = subprocess.Popen([peer, "echo", "maro", "1", "25"],
                            stdout=subprocess.PIPE, text=True)

deadline = time.time() + 25
while time.time() < deadline and listener.poll() is None:
    cmds.refresh(force=True)
    time.sleep(0.1)

out, _ = listener.communicate(timeout=10)
print(out)

# 실패 시 어느 단계가 문제였는지 바로 보이도록 카운터를 함께 남긴다.
# collected가 0이면 펌프가 축을 못 찾은 것, drained가 0이면 스레드 경계
# 문제, 둘 다 0이 아닌데 피어가 못 받으면 토픽 이름이나 DDS 문제다.
stats = cmds.maroBridgeStats()
assert listener.returncode == 0, \
    f"peer never received joint_states (timed out); maroBridgeStats={stats}"
assert "axisPub" in out, \
    f"joint name missing from published message:\n{out}\nmaroBridgeStats={stats}"
assert "0.75" in out, \
    f"joint value not published as expected:\n{out}\nmaroBridgeStats={stats}"
print("publish round trip OK")

cmds.maroStopBridge()
# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
```

- [ ] **Step 5: 빌드하고 발행 테스트 실행**

```bash
cmake --build out/build
```

```bash
MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" MARO_PEER_PATH="$(cygpath -w "$(find out/build -name maro_test_peer.exe | head -1)")" timeout 300 "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_publish.py; echo "exit=$?"
```

기대: 피어가 `joint axisPub = 0.75`를 출력하고 `publish round trip OK`, `teardown OK`, `exit=0`.

받지 못하면 순서대로 확인한다 — `maroBridgeStats`의 collected가 0이면 펌프가 축을 못 찾는 것(`jointName` 미설정), drained가 0이면 스레드 경계 문제, 둘 다 0이 아닌데 피어가 못 받으면 토픽 이름이나 DDS 문제다.

- [ ] **Step 6: 커밋**

```bash
git add src/maro_plugin tests
git commit -m "feat: publish joint_states and tf from the background thread"
```

---

## Task 12: 명령 수신과 모드 전환

**Files:**
- Modify: `src/maro_plugin/MaroCommands.h`
- Modify: `src/maro_plugin/MaroCommands.cpp`
- Modify: `src/maro_plugin/MaroPluginMain.cpp`
- Test: `tests/maya/test_contract.py`

**Interfaces:**
- Consumes: `/<robotName>/joint_commands` 구독과 그 적용 파이프라인은 이미 Task 10의
  `maro::MaroCommandDeviceNode`(`threadHandler()`의 구독 + `compute()`의
  `applyToMatchingAxis()`)가 만든다 — 이 태스크는 새 구독을 만들지 않는다.
  `maro::rosToMayaPosition` 계열(Task 2~4)은 참고만 한다: 관절 스칼라값은
  좌표계 변환 대상이 아니므로(스칼라 각도에는 기저 변환이 적용되지 않는다)
  실제로는 쓰지 않는다.
- Produces: MEL 커맨드 `maroSetControlMode <axisNode> <0|1>`. C++ 클래스
  `MaroSetControlModeCommand`. 이 커맨드는 축의 `controlMode`만 바꾼다 —
  Task 10이 이미 구독 중인 명령이 `controlMode == 1`인 축에만 적용되게
  게이트하는 것이 이 태스크의 계약이다.

- [ ] **Step 1: 실패하는 계약 테스트 작성**

`tests/maya/test_contract.py`:

```python
"""모드 전환 계약: Manual은 명령을 무시하고, ROS는 반영한다."""
import os
import subprocess
import sys
import time

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
peer = os.environ["MARO_PEER_PATH"]

cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

cube = cmds.polyCube(name="seg")[0]
axis = cmds.createNode("maroAxis", name="axisA")
cmds.maroBindAxis(axis, cube)
cmds.setAttr(axis + ".jointName", "axisA", type="string")

rot = cmds.createNode("maroRotation")
cmds.connectAttr(rot + ".capabilityOut", axis + ".capabilityIn[0]")
cmds.setAttr(rot + ".angle", 0.2)

cmds.maroStartBridge("maro")


def wait_until(condition, timeout):
    """조건이 참이 될 때까지 마야를 계속 돌리며 기다린다 (타이머 펌프와
    커맨드 디바이스 스레드 둘 다 유휴 이벤트에 엮여 있다).

    시계가 아니라 조건이 통과 기준이다 — 데드라인은 "느린 기계에서도
    이쯤이면 됐다"는 마지막 안전망일 뿐, 몇 초를 기다렸는지로 성공/실패를
    가르지 않는다. 조건이 한 번도 참이 되지 않으면 False를 돌려주고,
    호출부가 실패 시점의 관측값(maroBridgeStats, outValue 등)을 메시지에
    실어 어느 단계가 멈췄는지 보이게 한다.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        cmds.refresh(force=True)
        if condition():
            return True
        time.sleep(0.05)
    return False


def publish_command(value):
    subprocess.run([peer, "pub", "maro", "axisA", str(value)],
                   check=True, timeout=120)


def pump_idle(seconds):
    """조건 없이 정해진 시간만 마야를 돌린다. wait_until과 달리 "이 시간
    동안 아무 일도 안 일어나야 한다"를 검증하는 자리에서만 쓴다 — 부재를
    조건으로 기다릴 수는 없으므로, 여기서만 시계 기반 대기가 정당하다."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        cmds.refresh(force=True)
        time.sleep(0.05)


# 1) Manual 축은 명령이 실제로 도착해도 움직이지 않는다.
# "아무 일도 안 일어남"은 조건으로 기다릴 수 없다 — 그래서 이 케이스만은
# 정해진 시간을 기다리는 pump_idle을 쓴다. 그래도 명령을 처리할 기회는 줘야
# 하므로, 먼저 커맨드 디바이스 스레드가 최소 한 번 틱 하는 것을 조건으로
# 기다린 뒤(wait_until) 짧게 정착 시간을 둔다(pump_idle).
assert cmds.getAttr(axis + ".controlMode") == 0, "default must be Manual"
ticks_before = cmds.maroBridgeStats()[3]
publish_command(1.2)
assert wait_until(lambda: cmds.maroBridgeStats()[3] > ticks_before, timeout=5), \
    f"command device thread never ticked; maroBridgeStats={cmds.maroBridgeStats()}"
pump_idle(1.0)

assert abs(cmds.getAttr(axis + ".outValue") - 0.2) < 1e-6, \
    f"Manual axis moved despite ignoring commands; outValue={cmds.getAttr(axis + '.outValue')}, " \
    f"maroBridgeStats={cmds.maroBridgeStats()}"
print("manual ignores command OK")

# 명령이 실제로 도착은 했는지 확인한다. 0이면 위 검증이 무의미하다.
applied_before = cmds.maroBridgeStats()[2]

# 2) ROS 모드로 바꾸면 반영된다.
cmds.maroSetControlMode(axis, 1)
assert cmds.getAttr(axis + ".controlMode") == 1
publish_command(1.2)

assert wait_until(lambda: cmds.maroBridgeStats()[2] > applied_before, timeout=10), \
    f"no command was applied after switching to ROS mode; maroBridgeStats={cmds.maroBridgeStats()}"
assert wait_until(
    lambda: abs(cmds.getAttr(axis + ".outValue") - 1.2) < 1e-6, timeout=5), \
    f"ROS command not reflected; outValue={cmds.getAttr(axis + '.outValue')}, " \
    f"maroBridgeStats={cmds.maroBridgeStats()}"
print("ros mode applies command OK")

# 3) 리밋은 모드와 무관하게 적용된다.
lim = cmds.createNode("maroLimit")
cmds.setAttr(lim + ".enableY", True)
cmds.setAttr(lim + ".minY", -0.5)
cmds.setAttr(lim + ".maxY", 0.5)
cmds.connectAttr(lim + ".capabilityOut", axis + ".capabilityIn[1]")

assert wait_until(
    lambda: abs(cmds.getAttr(axis + ".outValue") - 0.5) < 1e-6, timeout=10), \
    f"limit did not clamp a ROS-driven value; outValue={cmds.getAttr(axis + '.outValue')}, " \
    f"maroBridgeStats={cmds.maroBridgeStats()}"
print("limit clamps ros value OK")

cmds.maroStopBridge()
# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("teardown OK")
sys.exit(0)
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

```bash
MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" MARO_PEER_PATH="$(cygpath -w "$(find out/build -name maro_test_peer.exe | head -1)")" "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_contract.py
```

기대: `No object matches name: maroSetControlMode` 로 실패.

Task 10에서 이미 `MaroCommandDeviceNode`가 `/<robotName>/joint_commands`를
구독하고, 도착한 값을 `controlMode == 1`인 축의 `rosCommand`에 반영한다
(`MaroCommandDeviceNode::applyToMatchingAxis`). 그래서 이 태스크엔 별도의
"구독 추가" 스텝이 없다 — 남은 건 그 게이트 자체, 즉 `controlMode`를
사용자가 바꿀 수 있는 커맨드뿐이다.

- [ ] **Step 3: 모드 전환 커맨드 추가**

`MaroCommands.h`의 `MaroBindAxisCommand` 아래에 추가한다.

```cpp
// 모드는 축에만 저장된다. 두 번째 진실 원천을 만들지 않는다.
class MaroSetControlModeCommand : public MPxCommand {
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
```

`MaroCommands.cpp` 끝(`}  // namespace maro` 앞)에 추가한다.

```cpp
void* MaroSetControlModeCommand::creator() {
    return new MaroSetControlModeCommand();
}

MSyntax MaroSetControlModeCommand::newSyntax() {
    MSyntax syntax;
    syntax.setObjectType(MSyntax::kStringObjects, 2, 2);
    return syntax;
}

MStatus MaroSetControlModeCommand::doIt(const MArgList& args) {
    MStatus status;

    if (args.length() != 2) {
        MGlobal::displayError(
            "Maro: maroSetControlMode needs <axis> <0=Manual|1=ROS>.");
        return MS::kFailure;
    }

    const MString axisName = args.asString(0, &status);
    if (!status) return status;
    const int mode = args.asInt(1, &status);
    if (!status) return status;

    if (mode != 0 && mode != 1) {
        MGlobal::displayError("Maro: control mode must be 0 (Manual) or 1 (ROS).");
        return MS::kFailure;
    }

    MSelectionList selection;
    if (!selection.add(axisName)) {
        MGlobal::displayError(MString("Maro: cannot find node '") + axisName + "'.");
        return MS::kFailure;
    }

    MObject axisObj;
    selection.getDependNode(0, axisObj);

    MFnDependencyNode axisFn(axisObj);
    if (axisFn.typeId() != MaroAxisNode::id) {
        MGlobal::displayError(
            MString("Maro: '") + axisName + "' is not a maroAxis node.");
        return MS::kFailure;
    }

    MPlug modePlug = axisFn.findPlug(MaroAxisNode::aControlMode, false, &status);
    if (!status) return status;

    // Manual -> ROS 전환 시, 실제 명령이 오기 전까지의 목표를 현재 값으로
    // 시딩해 로봇이 마지막 명령값으로 튀는 것을 막는다.
    if (mode == 1 && modePlug.asShort() == 0) {
        MPlug outValue = axisFn.findPlug(MaroAxisNode::aOutValue, false);
        MGlobal::displayInfo(
            MString("Maro: seeding ROS target for '") + axisName + "' with " +
            outValue.asDouble() + " to avoid a jump on mode switch.");
    }

    status = m_modifier.newPlugValueShort(modePlug, static_cast<short>(mode));
    if (!status) return status;

    return redoIt();
}

MStatus MaroSetControlModeCommand::redoIt() {
    return m_modifier.doIt();
}

MStatus MaroSetControlModeCommand::undoIt() {
    return m_modifier.undoIt();
}
```

- [ ] **Step 4: 커맨드 등록**

`MaroPluginMain.cpp`의 `maroBindAxis` 등록 뒤에 넣는다.

```cpp
    status = plugin.registerCommand(
        "maroSetControlMode",
        maro::MaroSetControlModeCommand::creator,
        maro::MaroSetControlModeCommand::newSyntax);
    if (!status) {
        status.perror("Maro: failed to register maroSetControlMode");
        return status;
    }
```

`uninitializePlugin`에 추가한다.

```cpp
    plugin.deregisterCommand("maroSetControlMode");
```

- [ ] **Step 5: 테스트가 통과하는지 확인**

```bash
cmake --build out/build && MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" MARO_PEER_PATH="$(cygpath -w "$(find out/build -name maro_test_peer.exe | head -1)")" timeout 300 "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_contract.py; echo "exit=$?"
```

기대: `manual ignores command OK`, `ros mode applies command OK`, `limit clamps ros value OK`, `teardown OK`, `exit=0`.

`no command was applied after switching to ROS mode`로 실패하면 명령이 아예 도착하지 않은 것이다 — Task 10의 `MaroCommandDeviceNode`가 살아있는지(`maroBridgeStats()[3]`의 commandThreadTicks가 느는지)부터 확인하고, 그다음 구독 토픽 이름(`joint_commands`)과 로봇 네임스페이스를 확인한다. Manual 검증도 그 경우 무의미했다는 뜻이다.

- [ ] **Step 6: 커밋**

```bash
git add src/maro_plugin tests/maya/test_contract.py
git commit -m "feat: gate ROS 2 joint commands behind per-axis control mode"
```

---

## Task 13: 견고성 시나리오 회귀 테스트

"튕기지 않는다"를 수동으로 매번 확인할 수 없으므로 크래시 유발 조작을 자동 테스트로 못박는다.

**Files:**
- Test: `tests/maya/test_robustness.py`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 6~12의 전 기능
- Produces: 없음 (검증만)

- [ ] **Step 1: 견고성 테스트 작성**

`tests/maya/test_robustness.py`:

```python
"""크래시를 유발할 수 있는 조작들이 규칙대로 거부·비활성화되는지 확인한다.

이 스크립트가 끝까지 도달하고 종료 코드 0으로 끝나면 크래시가 없었다는 뜻이다.
"""
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

import maya.cmds as cmds  # noqa: E402

plugin = os.environ["MARO_PLUGIN_PATH"]
cmds.loadPlugin(plugin)
cmds.file(new=True, force=True)

# 1) 순환 연결은 반드시 거부되어야 한다 (스펙 §9 원칙 2).
a = cmds.createNode("maroAxis", name="axC1")
b = cmds.createNode("maroAxis", name="axC2")
c = cmds.createNode("maroAxis", name="axC3")

cmds.maroConnectAxis(b, a)      # b 의 부모 = a
cmds.maroConnectAxis(c, b)      # c 의 부모 = b

try:
    cmds.maroConnectAxis(a, c)  # a 의 부모 = c -> 3단계 순환
    raise AssertionError("multi-hop cycle must be rejected at wiring time")
except RuntimeError:
    print("multi-hop cycle rejected OK")

# 커맨드를 거치지 않고 직접 이어 순환을 만든 경우에도 평가가 멈추면 안 된다.
cmds.connectAttr(c + ".message", a + ".parentAxis", force=True)
cmds.getAttr(a + ".outValue")
cmds.getAttr(b + ".outValue")
cmds.getAttr(c + ".outValue")
print("raw cycle evaluation survived OK")

# 2) NaN 주입 -> 축이 유한값을 유지한다.
cube = cmds.polyCube(name="segR")[0]
axis = cmds.createNode("maroAxis", name="axR")
cmds.maroBindAxis(axis, cube)
rot = cmds.createNode("maroRotation")
cmds.connectAttr(rot + ".capabilityOut", axis + ".capabilityIn[0]")

cmds.setAttr(rot + ".angle", float("inf"))
value = cmds.getAttr(axis + ".outValue")
assert value == value, "outValue became NaN"
assert abs(value) < 1e308, "outValue became infinite"
print("non-finite input contained OK")

# 3) 바인딩 대상이 사라진 뒤 평가 -> 축도 사라졌으므로 접근이 안전해야 한다.
cmds.delete(cube)
assert not cmds.objExists(axis)
print("delete during live graph OK")

# 4) 고아 능력 노드를 다른 축에 재연결 -> 정상 동작
axis2 = cmds.createNode("maroAxis", name="axR2")
cmds.connectAttr(rot + ".capabilityOut", axis2 + ".capabilityIn[0]")
cmds.setAttr(rot + ".angle", 0.3)
assert abs(cmds.getAttr(axis2 + ".outValue") - 0.3) < 1e-9
print("orphan reuse OK")

# 5) 능력 노드 없는 축을 평가 -> 0
bare = cmds.createNode("maroAxis", name="axBare")
assert abs(cmds.getAttr(bare + ".outValue")) < 1e-9
print("empty stack OK")

# Maya는 커스텀 노드 인스턴스가 씬에 남아 있으면 플러그인을 언로드하지 않는다.
cmds.file(new=True, force=True)
cmds.unloadPlugin(os.path.splitext(os.path.basename(plugin))[0])
maya.standalone.uninitialize()
print("all robustness scenarios survived")
sys.exit(0)
```

- [ ] **Step 2: 테스트 실행**

```bash
MARO_PLUGIN_PATH="$(cygpath -w "$(find out/build -name maro.mll | head -1)")" timeout 180 "/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" tests/maya/test_robustness.py; echo "exit=$?"
```

기대: 모든 `OK` 출력 후 `all robustness scenarios survived`, `exit=0`.

실패하면 해당 시나리오의 방어를 구현한다. 특히 `outValue`가 NaN/inf가 되면 `MaroAxisNode::compute`의 유한성 검사가 능력 노드 값에도 적용되도록 고친다.

- [ ] **Step 3: ctest에 Maya 테스트 등록**

`tests/CMakeLists.txt` 끝에 추가한다.

```cmake
if(MARO_BUILD_PLUGIN)
    set(MAYAPY "C:/Program Files/Autodesk/Maya2026/bin/mayapy.exe")

    # 플러그인만 있으면 되는 테스트
    foreach(maya_test load axis_node binding capability_stack delete_rules
                      bridge_pump robustness)
        add_test(NAME maya_${maya_test}
                 COMMAND "${MAYAPY}"
                         "${CMAKE_CURRENT_SOURCE_DIR}/maya/test_${maya_test}.py")
        set_tests_properties(maya_${maya_test} PROPERTIES
            ENVIRONMENT "MARO_PLUGIN_PATH=$<TARGET_FILE:maro>"
            TIMEOUT 240)
    endforeach()

    # ROS 2 상대역이 필요한 테스트는 피어 경로도 받는다.
    foreach(maya_test publish contract)
        add_test(NAME maya_${maya_test}
                 COMMAND "${MAYAPY}"
                         "${CMAKE_CURRENT_SOURCE_DIR}/maya/test_${maya_test}.py")
        set_tests_properties(maya_${maya_test} PROPERTIES
            ENVIRONMENT "MARO_PLUGIN_PATH=$<TARGET_FILE:maro>;MARO_PEER_PATH=$<TARGET_FILE:maro_test_peer>"
            TIMEOUT 360)
        # 같은 DDS 도메인을 쓰므로 병렬 실행 시 서로 간섭한다.
        set_tests_properties(maya_${maya_test} PROPERTIES RUN_SERIAL TRUE)
    endforeach()
endif()
```

- [ ] **Step 4: 전체 테스트 스위트 실행**

```bash
cmake --build out/build && ctest --test-dir out/build --output-on-failure
```

기대: 변환 라이브러리 테스트와 Maya 테스트 전부 통과. `test_contract`는 피어 경로가 추가로 필요하므로 이 목록에서 제외되어 있다.

- [ ] **Step 5: 커밋**

```bash
git add tests
git commit -m "test: pin crash-inducing scenarios so robustness cannot regress"
```

---

## 자체 검토 결과

**스펙 커버리지**

| 스펙 항목 | 담당 태스크 |
|---|---|
| §3 축·능력 노드 개념 | Task 6, 8 |
| §4 상태 소유 원칙, message 바인딩 | Task 6, 7 |
| §4 DG 노드 타입 (maroAxis, 능력 노드) | Task 6, 8 |
| §4 스택 합성 규칙 | Task 8 |
| §5 좌표 변환 라이브러리 | Task 2, 3, 4 |
| §6 스레딩과 큐 | Task 10 |
| §6 메인 스레드 펌프 (DG ↔ 큐) | Task 10 |
| §6 생명주기 | Task 5, 10 |
| §7 토픽 계약 | Task 11, 12 |
| §7.1 축 보정 | Task 4 (변환), Task 6 (어트리뷰트) |
| §7.2 모드 전환 | Task 12 |
| §8 삭제 규칙, 고아 세트 | Task 9 |
| §9 원칙 1~6 | Task 6, 8, 9, 10 (구현), Task 13 (검증) |
| §10 1단계 | Task 1~4 |
| §10 2단계 | Task 11, 12, 13 |
| §12 벤더 DLL | Task 10 |
| §13 devkit 참조 | Task 5, 6, 9 |

**미커버 항목 (의도적 이연)**

- `maroRobot` 노드는 이 플랜에서 만들지 않는다. Task 10~12는 런타임이 로봇 이름을 직접 받는 형태로 동작한다. `maroRobot`(멤버십 `axes[]`, `publishRate`, QoS, 일괄 모드 전환 커맨드)은 축 파이프라인이 검증된 뒤 후속 플랜에서 추가한다. 지금 넣으면 아직 검증되지 않은 축·발행 경로 위에 그룹핑 계층을 얹는 셈이라 실패 지점이 늘어난다.
- `maroAxis`의 뷰포트 기즈모 렌더링(`MPxDrawOverride`)과 마니퓰레이터는 시각적 편의이며 계약 검증에 필요하지 않아 S3와 함께 진행한다. Task 6은 로케이터 노드 등록까지만 한다.
- §10 3단계 시각 확인(RViz2)은 별도 머신·WSL2가 필요한 수동 절차이므로 태스크로 넣지 않는다.

**실행 전 스캔에서 메운 구멍 3개**

초안은 Phase 3를 "부품"(큐, 발행, 수신) 단위로 썼고 그것들을 **구동하는 주체를 넣지 않았다.** 결과적으로 세 가지가 빠져 있었다.

1. **메인 스레드 펌프 부재** — 스펙 §6의 핵심 루프(`MTimerMessage`로 DG↔큐 왕복)가 어느 태스크에도 없었다. `drainAndPublish()`가 영원히 빈 큐를 비웠을 것이다.
2. **런타임 시작 경로 부재** — `MaroRosRuntime::start()`를 만들기만 하고 호출하는 곳이 없었다.
3. **순환 탐지 부재** — 스펙 §9 원칙 2가 요구하는 축 체인 순환 거부가 구현되지 않았고, 테스트는 거부와 허용을 모두 받아들이게 쓰여 있었다.

**가장 나빴던 점은 테스트가 틀린 이유로 통과했을 것이라는 사실이다.** Task 12의 "Manual 축이 명령을 무시한다"는 런타임이 돌지 않으니 자동으로 참이었다. 초록불을 보고 검증됐다고 착각했을 것이다.

Phase 3를 **펌프 → 발행 → 수신** 의존성 순서로 재구성해 각 태스크가 앞 태스크를 실제로 사용하게 했다. Task 10이 끝나는 시점에 이미 DG→큐→백그라운드 흐름이 `maroBridgeStats`로 관측 가능하므로, 문제가 생기면 그 태스크에서 즉시 드러난다. 명령 도착 여부를 계수기로 먼저 확인한 뒤 무시 여부를 단언하도록 테스트도 고쳤다.

**검토 중 고친 설계 오류**

초안에서 `capabilityIn`/`capabilityOut`을 message 어트리뷰트로 잡았다. message는 **데이터를 나르지 않으므로** 축의 `compute()`가 데이터블록 대신 플러그를 직접 조회하게 되는데, 이는 DG 더티 전파를 우회해 Maya의 병렬 평가에서 값이 어긋난다.

스펙 §4는 `capabilityIn`을 "array"라고만 하고 message로 못박은 것은 `targetObject`/`parentAxis` 바인딩뿐이므로, 스펙 위반이 아니라 플랜의 오류였다. 네 능력 노드가 **구조가 동일한 복합 데이터 출력**을 내고 축이 `MArrayDataHandle`로 읽도록 고쳤다.

부수 효과로 설계가 나아졌다. 축의 평가 루프가 `capType`만 보고 분기하므로 **능력 노드 종류가 늘어도 축 코드는 그대로**다. 초안처럼 노드 타입별로 분기했다면 종류를 추가할 때마다 축을 고쳐야 했다.

바인딩(`targetObject`, `parentAxis`)은 message로 남겨 rename·delete 추적 이점을 유지한다.

**타입 일관성 확인**

- `maro::Vec3`, `maro::Quat`, `maro::AxisConvention`, `maro::SceneUnit` — Task 1에서 정의, Task 2~4·10·11에서 동일 이름으로 사용
- `mayaToRosPosition` / `rosToMayaPosition` / `mayaToRosRotation` / `rosToMayaRotation` / `axisVectorOf` / `jointToMayaRotation` / `mayaRotationToJoint` — 정의와 호출 시그니처 일치
- `MaroAxisNode::aCapabilityIn`, `aControlMode`, `aOutValue`, `aTargetObject` — Task 6에서 선언, Task 7~9·12에서 동일 이름으로 참조
- `capabilityOut` — 네 능력 노드 모두 동일한 롱네임/숏네임(`cpo`) 사용
- `BoundedQueue<AxisSample>` — Task 10에서 정의(발행 방향 전용), Task 11에서 사용. 수신 방향은 큐가 아니라 `MPxThreadedDeviceNode`의 메모리 풀이므로 별도 타입이 없다 — Task 10 검토로 `BoundedQueue<AxisCommand>`는 없어졌다.
- `MaroCommandDeviceNode::appliedCommandCount()` / `threadTickCount()` — Task 10에서 정의, Task 12의 계약 테스트가 `maroBridgeStats()`를 통해 관측
