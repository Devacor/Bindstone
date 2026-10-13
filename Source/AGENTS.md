# Source Directory

Top level source code.  Major components:

- `MV` – the MutedVision engine: rendering, scene graph, networking, physics, pathfinding, utilities.
- `JaiScript` – the scripting language plus the property/serialization/signal substrate MV builds on
  (own `AGENTS.md`, CMake build, own test suite).
- `Game` – gameplay code: `game.h`, `building.h`, `creature.h`, `player.h`, the match instance and the
  network layer shared by client, game server and lobby server.
- `Workbench` – the editor (dockable panels, reflection inspector, gizmos, hot-reloaded `.jai` panels).
- `Editor` – the legacy in-game editor, kept until the Workbench reaches authoring parity.
- `SolutionSpecific` – entry points for each executable (client, Android client, game server, lobby server).

Engine tests and benchmarks live in `MV/Tests` and `MV/Bench`.

See the AGENTS files inside those folders for more details.
