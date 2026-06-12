# Local server stack (lobby + matchmaking + database)

`run_local.bat` at the repo root is the one-click: it ensures a local PostgreSQL is
running (portable binaries, downloaded on first run — no installer/admin/service),
then launches LobbyServer, GameServer, and two clients auto-logged-in as `test1` and
`test2`. Pass `Debug` to use the Debug binaries (default `Release`).

## Pieces

| Process | Listens | Connects to | Config (first line wins) |
|---|---|---|---|
| LobbyServer | 22325 (clients), 22326 (game servers) | postgres | `ServerConfig/database.config` |
| GameServer | ephemeral (advertised to lobby) | lobby :22326 | `ServerConfig/lobbyServerAddress.config` |
| Client | — | lobby :22325 | `ServerConfig/gameServerAddress.config` |

`ServerConfig/*` resolves cwd → AppData → `Assets/` (the shipped copies live in
`Assets/ServerConfig/`). All processes must run with the repo root as working
directory (the `.vcxproj.user` files already set `$(SolutionDir)`).

Match flow: both clients queue → lobby pairs them → tells an AVAILABLE game server →
game server confirms → lobby sends each client the game server endpoint + a secret →
clients connect and the battle instance runs on the game server.

## Database

- Portable postgres lives in `Tools/LocalServer/pgsql/`, data in `pgdata/` (both
  gitignored). Trust auth on localhost, no passwords. `setup_local_db.bat` is
  idempotent: download → initdb → start → createdb → apply `players_schema.sql`.
- Stop it with: `Tools\LocalServer\pgsql\bin\pg_ctl.exe -D Tools\LocalServer\pgdata stop`
- Poke at it with: `Tools\LocalServer\pgsql\bin\psql.exe -h localhost -U bindstone bindstone`
- The lobby seeds accounts listed in `ServerConfig/localTestAccounts.config`
  (`email handle password` per line) at startup, through the production hash path.

## Running from Visual Studio

- **One click, everything:** Tools → External Tools… → Add: Title `Run Local Bindstone`,
  Command `$(SolutionDir)run_local.bat`, Initial directory `$(SolutionDir)`. It then
  appears in the Tools menu (assign a hotkey via Tools → Options → Keyboard if wanted).
- **Debugging:** Solution → right-click → Configure Startup Projects → Multiple startup
  projects → set BindstoneLobbyServer_Windows, BindstoneGameServer_Windows, and
  BindstoneClient_Windows to *Start*. F5 runs all three under the debugger (the client
  auto-logs-in as test1 via its debug args). Postgres must already be running — run
  `run_local.bat` once, or just `Tools\LocalServer\setup_local_db.bat`.
- **Second client while debugging:** right-click BindstoneClient_Windows → Debug →
  Start New Instance (it will be test1 and kick the first), or launch
  `Builds\Windows\x64\Release\Bindstone.exe -n test2 -p password2` from the repo root.
