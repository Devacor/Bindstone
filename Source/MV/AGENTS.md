# MutedVision Engine (`Source/MV`)

Core C++ engine code used by Bindstone.

Submodules:
- `ArtificialIntelligence` – grid pathfinding (`pathfinding.h`: `MapNode`, `NavigationAgent`).
- `Audio` – empty stub (`package.h` only).
- `Interface` – input devices (e.g. `tapDevice.h`).
- `Network` – networking utilities (`network.h`, `webServer.h`, etc.).
- `Physics` – physics components (wraps Box2D).
- `Render` – rendering system, textures/atlases and the scene graph (`Scene` holds `Node`, `Sprite`,
  `Text`, `Clickable`/`Button`, emitters, Spine, …; every serializable type is a `jai::property_owner`).
- `Script` – JaiScript eval wrapper + `makeScriptEngine()` policy factory (`script.h`; impl in `Source/Game/Script/script.cpp`).
- `Serialization` – `serialize.h`: `toJson`/`fromJson`/`toBinaryString`/`fromBinaryString` over JaiScript archives.
- `Utility` – misc helpers (thread pools, logging, async tasks, `services.hpp` registry, signals).
- `Tests` – `mv_tests.exe` Foundry suites (`build_tests.bat`, `README.md`); `Bench` – micro/scene/frame benchmarks.

The engine was written by Devacor (Michael Hamilton).
