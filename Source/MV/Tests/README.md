# MV (Bindstone) regression tests

`mv_tests.exe` — a Bindstone test runner built on **JaiScript's Foundry** framework (header-only)
plus Foundry's generic `main_test_runner.cpp`, linked against the built engine (`mutedvision.lib`).
It guards the behavior the rendering/transform optimizations depend on.

## Build & run

```bat
:: builds mv_tests.exe against the optimized engine lib and runs it (returns non-zero on failure)
Source\MV\Tests\build_tests.bat
:: or run directly:
Source\MV\Tests\mv_tests.exe --verbose
Source\MV\Tests\mv_tests.exe "MV Scene Transform"      :: additive: runs the whole suite
Source\MV\Tests\mv_tests.exe "MV Matrix.rotate"        :: dotted: suite AND test
```

Prereq: a Release engine lib at `Builds\Windows\x64\Release\mutedvision.lib` (build
`MutedVision_Windows.vcxproj`). `build_tests.bat <libdir>` can point at a different lib.

## Suites

- **MV Matrix** — `affineMultiply` is bit-identical to the generic 4×4 multiply for affine inputs;
  in-place `rotateX/Y/Z` are numerically identical to the old build-identity-then-multiply form;
  rotate preserves the translation column; `rotateZ(+90°)` maps +X→+Y.
- **MV Scene Transform** — a `Clipped` override-parent draw does not pollute persistent world
  transforms; dynamic transform changes apply and survive override draws; `worldTransform` equals
  the manual ancestor-chain product; and a **PERF GATE**:
  `recalculateMatrixCalls == 0` over 500 idle frames of a static clipped subtree. That gate is a
  *deterministic* guard (a count, not a timing) against re-introducing the per-frame matrix
  re-dirty that the Clipped dirty-flag fix removed.
- **MV Scene Serialization** — Node JSON save→load round trips through a headless `Services`
  host (`mv_scene_host.hpp`: renderer, TextLibrary, TapDevice, SharedTextures, script engine):
  a loaded `Text` re-attaches its `TEXT_`/`CURSOR_` nodes under its owner and a re-save never
  writes them at any depth; `postLoadStep` ×3 leaves child and anchor counts alone; Button
  views survive the childComponents-before-childNodes wire order; Stencil/Palette keep their
  own `$type`; PathMap keeps Drawable-level state; `Component::clone` copies the whole
  property chain; a persisted Clickable script receiver fires after load. Uses
  `Assets/Fonts/Verdana.ttf` (resolved from the tests dir or the repo root).
- **MV Texture Handles** — 9-slice margins squeeze proportionally in a box smaller than the
  margins; flipX survives a pack-based `postLoadInitialize`; `logicalSize` scales by texture
  size, not content size.
- **MV Network** — `Server`/`Connection`/`Client` on loopback over TLS: connection state lives and dies on
  the updating thread, messages arrive whole and in order, a close flushes queued writes first; and
  the message limits (`MessageLimits`): an oversized header (4 GiB, over 32 KiB before
  `authenticate()`, over 2 MiB after) is rejected before allocating, the unread cap pauses reading
  (512 KiB / 8 MiB) and resumes on drain without telling the peer, a trickling message and an
  undrained paused inbox are rejected at the unread timeout, each rejection reaching the peer as a
  `ProtocolFrame` reason code before the close; the client learns the server's limit from the
  announcement and `send` throws `MessageTooLargeException` past it.
- **MV TLS** — certificates made at test time by a throwaway CA (`tls_test_pki.hpp`, temp folder): a link
  negotiates TLS 1.3 (1.2 on an OpenSSL without it) with an AEAD cipher and carries messages both ways; a client
  refuses a server certificate from another CA (also one with the CA's exact name), one naming another host (DNS
  and IP), an expired or not-yet-valid one, and a client certificate posing as a server; a server requiring client
  certificates refuses none, another CA's and a server certificate, never building a connection state or reading
  what they send, and exposes a valid one's identity (`PeerCertificate`); a plaintext peer of the old protocol and
  silent peers are closed (the latter at the handshake deadline) and a client gives up on a server that never
  handshakes; a missing CA file fails each attempt with the reason until it exists; the HTTPS download client
  sends SNI and refuses wrong-host certificates without sending its request; the SMTP client verifies after
  STARTTLS and sends no credentials to a wrong-host or wrong-CA server.
- **Pathfinding** — `Map` budget, `NavigationAgent` paths/signals, footprint block counts
  across moves, clones and off-map placement.
- **MV Utility** — `angle2D`/`angle2DRad` overloads, `wrappingDistance` and `wrappingMix` (the
  short-way angle interpolation `Collider::interpolateDrawAngle` draws with), `startsWith`,
  `BoxAABB::centerPoint` and `BoxAABB::collides` with and without depth.
- **MV Thread Pool** — how MV drives `jai::thread_pool`: `MV::initializeThreadPool` parks at once and logs
  what tasks and posted work throw (std and non-std); a decode on the pool continues on the main
  thread at the next `run_posted()`, even when it throws (the texture streaming shape); the lobby's
  one-worker database pool runs queries one at a time in order and drains each continuation once on
  the lobby thread; `io_context` workers block in `run()` until the context stops and the pool then
  joins. The pool's own contract is JaiScript's `thread_pool continuations` suite.
- **MV Emitter** — the particle pipeline (`simulateWhileHeadless` runs it under the headless test
  renderer): particles spawn, age and die with four points and six in-range indices each, the
  population respects `maximumParticles`, a headless renderer skips it unless asked; the
  properties (`properties(const&)` and `properties()` writes) and the textures (swapped, cleared,
  re-cornered) change every frame while the frame's stages run; replacing the properties while the
  pipeline waits for a worker leaves the emitter updating and destructible (the old pipeline froze
  and its destructor spun forever); the emitter dies at once with a pipeline in flight; a stage
  that throws still finishes its pipeline and the next update starts a new one.

## Benchmarks (measurement, not assertions)

See `../Bench/` (`bench_math`, `bench_scene`, `bench_frame` + `RESULTS_math.md`) for the
before/after performance numbers. Benchmarks are informational; the deterministic correctness +
recalc-count gates live here.

## Adding a Bindstone test

```cpp
#include <jaiscript/testing/foundry.hpp>
// ... MV headers ...
namespace mv_tests {
class my_suite : public jai::foundry::suite {
public:
    my_suite() : suite("My Suite") {}
    void forge_tests() override {
        test("does the thing", []{ jai::foundry::check(cond, "msg"); });
    }
};
}
using my_suite = mv_tests::my_suite;   // FOUNDRY_REGISTER needs a global-scope name
FOUNDRY_REGISTER(my_suite)
```
Add the `.cpp` to `build_tests.bat`'s source list. Build flags must include
`/MD /DSDL_MAIN_HANDLED` and link gl3w.c + the SDL/Win libs (see the bat).
