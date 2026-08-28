# Creature Assets

Each subfolder is one creature:
- `Default/` – `unit.bindsnap` (the node snapshot) plus its art: `unit.atlas`/`unit.json`/`unit.png`
  and Spine data (`*.spine`).
- `main.script` and `mainClient.script` – server and client logic (JaiScript; spawn/update/death hooks).

Creature stats and ids live in `Assets/Catalogs/Creatures.json`.
