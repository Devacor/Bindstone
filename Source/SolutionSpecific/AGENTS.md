# Solution Specific Entry Points

Each subfolder here builds an executable:
- `BindstoneClient` – Windows client main (`main.cpp`).
- `BindstoneClient_Android` – Android client main.
- `BindstoneGameServer` – dedicated game server entry point.
- `BindstoneLobbyServer` – lobby/matchmaking server entry point.

These main files depend on code in `Source/MV` and `Source/Game`.
