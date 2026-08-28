# Game Assets

JSON catalogs, scenes, BindSnaps, scripts and art loaded at runtime (working directory = repo root).

Key entries:
- `Animations.json` and sprite atlases under `Atlases/` (packed `.png` + folder per pack).
- `Audio/` – ogg music.
- `BattleEffects/`, `Buildings/`, `Creatures/` – one folder per unit: `Default/*.bindsnap` plus
  `main.script` (server) and `mainClient.script` (client).
- `Catalogs/` – `Buildings.json`, `Creatures.json`, `BattleEffects.json` describing the units.
- `Fonts/`, `Images/`, `Map/`, `Pixel/`, `Spine/` – art.
- `Interface/` – UI pages: `Login/`, `Main/`, `interfaceManager.script`.
- `Localization/` – `languages.json`, `translations.json`.
- `Prefabs/` – shared `.bindsnap` snapshots (Button, SimpleButton, LoginName, missile, …).
- `Scenes/map.scene` – the battle map (anchor nodes only; buildings and creatures are attached at match start).
- `ServerConfig/` – `*.config` addresses, database and local test accounts.
- `Shaders/` – GLSL `.vert`/`.frag`.
- `Workbench/` – `.jai` editor panels, hot reloaded by the Workbench.

Formats: `.scene` and `.bindsnap` are JaiScript JSON archives (see the root `AGENTS.md`
serialization chapter); `.script` and `.jai` are JaiScript, evaluated through `MV::Script`.
`Bindstone.exe -verifyassets` loads and round-trips all of it.
