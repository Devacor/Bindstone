# Game Instance

Components that manage a match.

Files:
- `gameInstance.*` – `GameInstance` (shared match state, building slots, network object pool) with
  `ClientGameInstance` and, under `BINDSTONE_SERVER`, `ServerGameInstance` (authoritative spawns).
- `team.*` – `Team` / `TeamSide`: a side's player, buildings and health.
