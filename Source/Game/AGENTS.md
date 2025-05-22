# Game Code (`Source/Game`)

Gameplay specific C++ source.

Notable files:
- `game.h`/`game.cpp` – main client/game logic.
- `managers.h` – struct bundling engine systems.
- `player.h`, `building.h`, `creature.h` – core gameplay objects.
- `Instance/` – `gameInstance.*` manages a running match.
- `Interface/` – GUI helpers via `interfaceManager.*`.
- `NetworkLayer/` – network actions plus game and lobby server implementations.
- `Script/` – bindings for game scripts.
