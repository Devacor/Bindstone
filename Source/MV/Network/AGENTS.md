# MV Network

Networking helpers including client/server wrappers and HTTP utilities.

Key files:
- `network.h` / `network.cpp` – connection and client/server abstractions.
- `networkObject.h` – `NetworkObject` / `NetworkObjectPool`: the synchronized object state a match replicates.
- `webServer.h` / `webServer.cpp` – simple web server implementation.
- `download.*`, `url.*`, `email.h` – HTTP download, URL and email utilities.

Used by both game and lobby servers as well as the client.
