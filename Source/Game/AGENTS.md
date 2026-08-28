# Game Code (`Source/Game`)

Gameplay specific C++ source. Server-only paths are wrapped in `#ifdef BINDSTONE_SERVER`.

Notable files:
- `game.h`/`game.cpp` – the client `Game`: window, scenes, lobby connection, match hand-off.
- `gameEditor.*` – title menu (`Game` / legacy `Editor` buttons) and the `-loginshot` frame runner.
- `managers.h` – `Managers`: engine systems bundled behind `MV::Services`.
- `player.h`, `building.h`, `creature.h`, `battleEffect.h` – core gameplay objects; the last three are
  `MV::Scene::Component`s with Client/Server subclasses and a network-state twin each.
- `state.*` – `GameData`: the three catalogs plus `Constants`.
- `wallet.*` – per-player currency with change signals.
- `standardScriptMethods.h` – spawn/update/death hooks bound from a unit's `main.script`/`mainClient.script`.
- `Instance/` – `gameInstance.*` and `team.*` run a match.
- `Interface/` – UI pages via `interfaceManager.*`, control factories in `guiFactories.*`.
- `NetworkLayer/` – network actions plus game and lobby server implementations.
- `Script/script.cpp` – `MV::makeScriptEngine` and the game's script bindings.
