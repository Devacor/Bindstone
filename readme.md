# Bindstone

Bindstone is a 1v1 lane-battle game: two players upgrade buildings that spawn creatures which path
across a shared map. It is built on **MutedVision** (`Source/MV`), Devacor's C++20 2D engine
(scene graph, rendering, UI, Box2D physics, pathfinding, networking), which scripts, reflects and
serializes through **JaiScript** (`Source/JaiScript`), an embedded scripting language with a property
system, archives, signals and a debugger. Layering is Bindstone < MV < JaiScript; JaiScript stays
generic.

## Executables (`Bindstone.sln`, Windows, MSVC)
- `BindstoneClient_Windows` → `Bindstone.exe`: the game, plus the Workbench editor (`-workbench`) and
  the headless gates below.
- `BindstoneGameServer_Windows`: authoritative match server.
- `BindstoneLobbyServer_Windows`: login/matchmaking server (PostgreSQL via libpqxx).
- `BindstoneClient_Android`: the Android client.

## Build and gates
Build instructions and command lines live in `AGENTS.md`; per-folder orientation in each folder's `AGENTS.md`;
JaiScript has its own `Source/JaiScript/AGENTS.md`. Gates:
- `Bindstone.exe -verifyassets` — every shipped scene/`.bindsnap`/catalog loads and round-trips headless;
  gameplay scripts eval, interface scripts static-check.
- `Bindstone.exe -loginshot [frames]` — renders the Login page headless to `login_shot.bmp`.
- `Bindstone.exe -workbenchsmoke` / `-workbenchshot` — the editor self-test (exit 0) and its screenshot.
- `Source\MV\Tests\mv_tests.exe` — engine regression suites (Foundry).
- `jaiscript_tests` — the language's own suite, both backends.

Assets are in `Assets/` (`.scene`, `.bindsnap`, JSON catalogs, JaiScript `.script` files, art, Spine).
