# MutedVision Engine (`Source/MV`)

Core C++ engine code used by Bindstone.

Submodules:
- `Audio` – simple sound helpers (currently placeholder).
- `Interface` – input devices (e.g. `tapDevice.h`).
- `Network` – networking utilities (`network.h`, `webServer.h`, etc.).
- `Physics` – physics components (wraps Box2D).
- `Render` – rendering system and scene graph (`Scene` subfolder holds `Node`, `Sprite`, etc.).
- `Script` – JaiScript eval wrapper + `makeScriptEngine()` policy factory (`script.h`; impl in `Source/Game/Script/script.cpp`).
- `Serialization` – JaiScript-archive helpers (`serialize.h`).
- `Utility` – misc helpers (thread pools, logging, async tasks, properties, etc.).

The engine was written by Devacor (Michael Hamilton).
