# JaiDOOM in Lua

Lua 5.4.8 runs the same 3984-tic recorded game as JaiScript, including the complete renderer, weapon overlay, HUD and frame strings. Every tic's state and rendered-frame fingerprint matches across cold and warmed replays.

See [porting, build and measurement instructions](../../../internal/examples/jaidoom/ports/PORTING.md).

The combined runner lives in [GLOOM ports](../../gloom/ports/README.md). Select --workloads timedemo --engines jai-lua for this comparison. It enforces frame conformance, warm-up, interleaving, core affinity and cold-function padding controls.
