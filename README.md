# Maro — Maya / ROS 2 Axis Node Plugin

Maro robotizes objects modeled in Maya using Maya's own rigging and Dependency
Graph, and drives them live from ROS 2. Unlike external simulators (Gazebo,
CARLA), the robot lives entirely inside a Maya scene.

Two building blocks compose a robot:

- **Axis** (`maroAxis`) — binds to exactly one Maya object and drives its
  motion. Axes can be chained (`maroConnectAxis`) to form a hierarchy.
- **Capability nodes** (`maroRotation`, `maroLimit`, `maroSensorDirection`,
  `maroSensorRange`) — stack onto an axis's `capabilityIn` array. What the
  axis *becomes* (a plain rotating joint, a limited joint, a sensor, a moving
  sensor, ...) emerges from which capabilities are stacked, not from a type
  chosen up front.

Each axis has a `controlMode`: **Manual** (the user's own rigging/keyframes
drive the axis) or **ROS** (incoming ROS 2 commands drive it instead). A
background bridge (`maroStartBridge`) publishes `/joint_states` and `/tf` from
the live scene and applies inbound `/<robot>/joint_commands` to axes in ROS
mode.

## Prerequisites

- Windows, Visual Studio 2022 (MSVC), CMake >= 3.22
- Maya 2026 devkit
- ROS 2 Jazzy, built/installed for the same MSVC toolset
- vcpkg (for GoogleTest, used by the transform unit tests) — see `vcpkg.json`

## Configuring the build

The build needs two absolute paths, exposed as CMake cache variables. They
currently default to one developer's machine — **override both** for any
other environment:

| Cache variable | Purpose | Default |
|---|---|---|
| `DEVKIT_LOCATION` | Root of the Maya devkit (provides `cmake/pluginEntry.cmake`, Maya headers/libs) | `C:/Users/ckd30/Projects/devkitBase` |
| `ROS2_INSTALL` | ROS 2 install prefix (headers, `Lib/`, `bin/`, and the vendor `opt/*/bin` dirs) | `C:/dev/ros2_jazzy/install` |

Other useful options:

- `MARO_BUILD_PLUGIN` (default `ON`) — build the Maya plugin; needs devkit + ROS 2.
- `MARO_BUILD_TESTS` (default `ON`) — build and register the test suite.

Example configure + build from a Visual Studio "x64 Native Tools" (or
`VsDevCmd.bat`-initialized) shell:

```powershell
cmake -S . -B out/build -DDEVKIT_LOCATION=C:/path/to/devkit -DROS2_INSTALL=C:/path/to/ros2_jazzy/install
cmake --build out/build
```

## The PATH requirement (read this before your first `loadPlugin`)

The build stages every ROS 2 runtime DLL (and the `libyaml`/`spdlog`/
`console_bridge` vendor DLLs) next to the built plugin (`maro.mll`). That is
not sufficient by itself: Maya's plugin loader does not open `.mll` files
with `LOAD_WITH_ALTERED_SEARCH_PATH`, so Windows will not automatically search
the plugin's own directory for those dependencies.

**The plugin's output directory must already be on `PATH` before Maya (or
`mayapy`) starts.** If it isn't, `loadPlugin("maro")` fails with a generic
"cannot find dependent DLL" error that gives no hint that this is the actual
cause.

Add the build output directory (e.g. `out/build/src/maro_plugin/Debug`) to
`PATH` in the environment you launch Maya from, then start Maya.

## Running the tests

Tests are registered with CTest — the C++ transform unit tests (GoogleTest)
plus a set of `mayapy`-driven scenario scripts under `tests/maya/`. The
`mayapy`-based tests set their own `PATH`/`MARO_PLUGIN_PATH` via CTest test
properties, so you don't need to do that manually for `ctest` runs.

```powershell
ctest --test-dir out/build --output-on-failure
```

Some tests start a live ROS 2 bridge and talk to a peer process
(`maro_test_peer`); those are marked `RUN_SERIAL` because they share a DDS
domain and would otherwise interfere with each other.

## Registered `maro*` commands

| Command | Purpose |
|---|---|
| `maroBindAxis(axis, targetObject)` | Bind a `maroAxis` node to the Maya object it drives. |
| `maroConnectAxis(child, parent)` | Wire one axis as the child of another, building the axis hierarchy. |
| `maroSetControlMode(axis, 0\|1)` | Switch an axis between Manual (0) and ROS (1) control. |
| `maroStartBridge(robotName)` | Start the ROS 2 bridge: publishes `/<robotName>/joint_states` and `/tf`, and creates the command-device node that subscribes to `/<robotName>/joint_commands`. |
| `maroStopBridge()` | Stop the bridge and tear down the ROS 2 runtime. |
| `maroBridgeStats()` | Diagnostic counters: `[collected, drained, applied, threadTicks, publishErrors]`. |

## Layout

- `src/maro_plugin/` — the Maya plugin (nodes, commands, ROS 2 runtime).
- `src/maro_transform/` — coordinate/unit conversion library shared by the
  plugin and its unit tests.
- `tests/` — GoogleTest unit tests and `mayapy` scenario tests, wired into
  CTest.
- `docs/superpowers/` — design spec, plans, and task-by-task implementation
  history for this feature.
