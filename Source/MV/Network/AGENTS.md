# MV Network

Networking helpers including client/server wrappers and HTTP utilities.

Key files:
- `network.h` / `network.cpp` – connection and client/server abstractions.
- `tls.h` / `tls.cpp` – TLS configuration and certificate identity; `tlsAsio.h` – the contexts with the settings below
  and peer verification.
- `networkContext.h` – an io_context run by a `jai::thread_pool`, shared by downloads and email.
- `networkObject.h` – `NetworkObject` / `NetworkObjectPool`: the synchronized object state a match replicates.
- `webServer.h` / `webServer.cpp` – simple web server implementation.
- `download.*`, `url.*`, `email.*` – HTTP download, URL and email utilities.

No header here includes asio: each class keeps its asio state in an `Asio*` struct its `.cpp` defines, and
`tlsAsio.h` / `networkContextAsio.h` are included only from `.cpp` files.

Used by both game and lobby servers as well as the client.

Message limits (`MessageLimits`, passed to `Server`/`Client::make`): a header over the connection's max
message size is rejected before anything is allocated (32 KiB until the game layer calls
`Connection::authenticate()`, 2 MiB after); a connection's unread bytes are capped (512 KiB / 8 MiB) by
pausing reads until the main thread drains the inbox; a message that does not finish arriving, or a paused
inbox that is not drained, within `unreadTimeout` (30 s) is rejected. A rejection sends the peer a
`ProtocolFrame` (reason code, limit, text) and closes after the flush; `authenticate()` sends the peer its
new limit the same way. Protocol frames set `PROTOCOL_FRAME_BIT` in the header and never reach game code.

## TLS

Every `MV::Server` / `MV::Client` link is TLS (`tls.h`); there is no plaintext mode. TLS gives each link three things:
nobody on the network can read it (passwords at login, the match secrets the lobby hands out), nobody can change it
in flight, and each side knows who it is talking to, because the other side proves it holds the private key of a
certificate signed by a CA it trusts.

What each link checks:

| Link | Server proves | Client proves | Who checks what |
|---|---|---|---|
| Player → lobby, port 22325 | lobby certificate | nothing (the password, later) | the client: certificate chains to the private CA and names the lobby's host |
| Game server → lobby, port 22326 | lobby certificate | the game server's *client* certificate | both ways (mutual TLS); the lobby drops a peer without a valid client certificate before reading anything from it |
| Player → game server | that game server's certificate | nothing (the match secret, later) | the client, as for the lobby, against the host the lobby sent it to |

`download.h` (HTTPS) and `email.h` (SMTP STARTTLS) talk to public hosts: they trust the system's root certificates
(the Windows ROOT store; OpenSSL's default paths elsewhere) and verify the host name the same way.

### The settings

TLS 1.3 is preferred and TLS 1.2 is the minimum; SSL 3, TLS 1.0 and 1.1 are refused. TLS 1.2 uses only ECDHE key
exchange with AEAD ciphers (AES-GCM, ChaCha20-Poly1305); TLS 1.3's own suites are all AEAD. Compression,
renegotiation, session tickets and session resumption are off. Every client verifies the peer (`verify_peer`), sends
the host name (SNI) and checks the certificate names that host: a DNS name for a name, an IP address entry for an
address. Servers that need client certificates refuse a peer without one (`verify_fail_if_no_peer_cert`).
A certificate's purpose is checked too: a server certificate cannot log in as a client and a client certificate
cannot serve. Each process logs the OpenSSL it runs at startup (`TLS: OpenSSL ...`) and each handshake's protocol
and cipher (`[handshake] 127.0.0.1:port -> TLSv1.3 TLS_AES_256_GCM_SHA384 ...`).

On the wire: the TLS handshake comes first, bounded by `MessageLimits::handshakeTimeout` (10 s); a peer that connects
and never finishes it, fails it, or speaks the old plaintext protocol is closed without ever becoming a `Connection`,
so game code never sees it. The message limits start with the first message after the handshake. A server-side close
sends what is queued (a rejection frame, a kick reason), then TLS's close_notify, then drains until the peer closes
or 2 s pass.

### The certificates

One private CA signs everything; nothing uses public certificates. The files live in `Certificates/Tls/` at the
repo root (gitignored: keys and certificates are never committed), and every process runs from the repo root:

| File | What it is | Who needs it |
|---|---|---|
| `ca.pem` | the CA's certificate: the only thing anyone trusts | every client, the lobby, every game server |
| `ca.key` | the CA's private key: whoever has it can mint any server | only the machine that makes certificates |
| `lobby.pem`, `lobby.key` | the lobby's identity on 22325 and 22326 | the lobby |
| `gameservers/NAME/server.pem`, `.key` | game server NAME's identity to players | that game server |
| `gameservers/NAME/client.pem`, `.key` | game server NAME's identity to the lobby (common name `gameserver:NAME`) | that game server |

A game server picks its files with `GameServer.exe -identity NAME` (default `local`). The paths are in
`Source/Game/NetworkLayer/certificates.h`. A shipped client needs only `ca.pem` (public, safe to bundle as
`Assets/Certificates/Tls/ca.pem`); a private key never goes into `Assets/`.

### Making and rotating them

`python Tools/LocalServer/make_certs.py` makes whatever is missing or expires within 14 days (the CA, the lobby,
game server `local`); `_run_local.bat` runs it before starting the stack. It needs Python and an `openssl`
executable (Git for Windows ships one). Keys are ECDSA P-256; the CA lasts 5 years, the others 90 days.

- A new game server: `python Tools/LocalServer/make_certs.py gameserver NAME --host its.public.name --host 203.0.113.7`
  (every name and address players will use to reach it; localhost is always included), copy `ca.pem` and
  `gameservers/NAME/` to it, run `GameServer.exe -identity NAME`.
- Renewing: run the same command again (or the plain `make_certs.py`, which renews anything near expiry) and restart
  that server. Clients need nothing: they trust the CA, not a particular certificate.
- A new lobby host name: `make_certs.py lobby --host lobby.name`, restart the lobby.
- A leaked server key: reissue that server's certificate. There is no revocation list, so the leaked one stays valid
  until it expires (90 days at most); keep lifetimes short for that reason.
- A leaked CA key or a CA near expiry: `make_certs.py ca` makes a new CA; then reissue every certificate and give
  clients the new `ca.pem`. A `ca.pem` may hold several CA certificates, so a client can trust the old and new CA
  while servers move over.

### What the lobby checks when a game server registers

On 22326 the handshake already proved the peer holds a client certificate from the CA. `GameServerAvailable` is then
accepted only if that certificate's common name starts with `gameserver:` and its subjectAltNames name the host the
game server advertises (the host the lobby will send players and their match secrets to). Otherwise the lobby logs
`Refused game server registration ...` and drops the connection. The game server would fail with players anyway
(they verify that host against its server certificate); refusing at registration stops the lobby from handing out
match secrets for a host the game server is not certified for, and puts the reason in the lobby's log. A
registration logs `Game server [NAME] registered from [address] for url:port (TLS session, certificate ...)`.

On a game server, only a connection that presented a valid match secret counts as a player: it alone hears the
match, can act in it, and ends it (a forfeit) by disconnecting. Anything else that connects is ignored, and its
disconnect changes nothing.
