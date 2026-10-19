# MV Network

Networking helpers including client/server wrappers and HTTP utilities.

Key files:
- `network.h` / `network.cpp` – connection and client/server abstractions.
- `networkObject.h` – `NetworkObject` / `NetworkObjectPool`: the synchronized object state a match replicates.
- `webServer.h` / `webServer.cpp` – simple web server implementation.
- `download.*`, `url.*`, `email.h` – HTTP download, URL and email utilities.

Used by both game and lobby servers as well as the client.

Message limits (`MessageLimits`, passed to `Server`/`Client::make`): a header over the connection's max
message size is rejected before anything is allocated (32 KiB until the game layer calls
`Connection::authenticate()`, 2 MiB after); a connection's unread bytes are capped (512 KiB / 8 MiB) by
pausing reads until the main thread drains the inbox; a message that does not finish arriving, or a paused
inbox that is not drained, within `unreadTimeout` (30 s) is rejected. A rejection sends the peer a
`ProtocolFrame` (reason code, limit, text) and closes after the flush; `authenticate()` sends the peer its
new limit the same way. Protocol frames set `PROTOCOL_FRAME_BIT` in the header and never reach game code.
