# Creature Assets

Each subfolder is one creature:
- `Default/` – `unit.bindsnap` (the node snapshot) plus its art: `unit.atlas`/`unit.json`, the Spine
  skeleton and texture named after the creature (`whisper.spine`/`whisper.png`).
- `main.script` and `mainClient.script` – server and client logic (JaiScript; spawn/update/death hooks).
  Each server script is a class deriving `CreatureKind` from `Shared/creatureKind.script`.

The scripts are generated: edit `Tools/Creatures/generate_creatures.py` and re-run it, not the
`.script` files. Creature stats and ids live in `Assets/Catalogs/Creatures.json`.
