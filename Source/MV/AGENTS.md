# MutedVision Engine (`Source/MV`)

Core C++ engine code used by Bindstone.

Submodules:
- `ArtificialIntelligence` – grid pathfinding (`pathfinding.h`: `MapNode`, `NavigationAgent`).
- `Audio` – empty stub (`package.h` only).
- `Interface` – input (`tapDevice.h`: mouse/touch).
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
