# Game Network Layer

Contains all network messaging and server implementations.

Highlights:
- `networkAction.h` – `NetworkAction` base: `serialize(Archive&)` plus one `execute` overload per host
  (client `Game`, `GameServer`, lobby connection states). Each subclass has a
  `jai::registrar<T, MV::Services, jai::bases<NetworkAction>>` so it travels as `shared_ptr<NetworkAction>`.
- `accountActions.*` – `CreatePlayer`, `LoginRequest`, `FindMatchRequest`, `ExpectedPlayersNoted` (client → lobby).
- `clientActions.*` – `MessageAction`, `LoginResponse`, `IllegalResponse`, `ServerDetails`, `MatchedResponse` (lobby → client).
- `gameServerActions.*` – game-server lifecycle and match messages (`AssignPlayersToGame`, `GetInitialGameState`, `RequestBuildingUpgrade`, `MatchResult`, …).
- `synchronizeAction.*` – `SynchronizeAction`: network-object-pool state sync during a match.
- `gameServer.*` – in-match server (`BINDSTONE_SERVER`).
- `lobbyServer.*` – lobby/matchmaking server (`BINDSTONE_SERVER`).
- `package.h` – umbrella include.
