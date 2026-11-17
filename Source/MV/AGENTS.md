# MutedVision Engine (`Source/MV`)

Core C++ engine code used by Bindstone.

Submodules:
- `ArtificialIntelligence` – grid pathfinding (`pathfinding.h`: `MapNode`, `NavigationAgent`).
- `Audio` – empty stub (`package.h` only).
- `Interface` – input (`tapDevice.h`: mouse/touch and the keyboard focus; `controllerDevice.h`: game controllers,
  read as one pad, `navigate` to turn the pad into focus moves on a TapDevice, and a cursor mode in which the left
  stick moves a pointer the TapDevice reads as its mouse; `Render/Scene/controllerCursor.h` draws that pointer). `Render/Scene/focusGroup.h` marks a node as a
  selection context (a window or a tab strip); `TapDevice::focusContext` keeps the focus to the one in use.
- `Network` – networking utilities (`network.h`, `networkObject.h`, `webServer.h`, etc.).
- `Physics` – `collider.*`: the Box2D-backed collider component.
- `Render` – rendering system, textures/atlases and the scene graph (`Scene` holds `Node`, `Sprite`,
  `Text`, `Clickable`/`Button`, emitters, Spine, …; every serializable type is a `jai::property_owner`).
  `Backends` holds the render devices (GL, Vulkan, headless) behind the RHI.
- `Script` – JaiScript eval wrapper + `makeScriptEngine()` policy factory (`script.h`; impl in `Source/Game/Script/script.cpp`).
- `Serialization` – `serialize.h`: `toJson`/`fromJson`/`toBinaryString`/`fromBinaryString` over JaiScript archives.
- `Utility` – misc helpers (thread pools, logging, async tasks, `services.hpp` registry). Signals and
  properties come from JaiScript (`jai::signal`, `JAI_PROPERTY`).
- `Tests` – `mv_tests.exe` Foundry suites (`build_tests.bat`, `README.md`); `Bench` – micro/scene/frame benchmarks.

The engine was written by Devacor (Michael Hamilton).
