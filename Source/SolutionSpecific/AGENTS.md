# Solution Specific Entry Points

Each subfolder here builds an executable:
- `BindstoneClient` – Windows client main (`main.cpp`); it parses the command-line modes
  (`-verifyassets`, `-loginshot`, `-workbench*`, `-arena`, `-drive`, …).
- `BindstoneClient_Android` – Android client main.
- `BindstoneGameServer` – dedicated game server entry point (`gameServerMain.cpp`).
- `BindstoneLobbyServer` – lobby/matchmaking server entry point (`lobbyServerMain.cpp`).

These main files depend on code in `Source/MV` and `Source/Game`.
