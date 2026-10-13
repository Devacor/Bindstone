# JaiDOOM Lua comparison port

The Lua 5.4.8 port runs the original recorded 3984-tic E1M1 playthrough at 120x78: simulation, BSP traversal, textured walls and flats, sprites, weapon overlay, HUD and terminal-frame string construction. Audio stays disabled on both runtimes. Frames are built and discarded in the timed workload.

This is a headless comparison port. Its entry points are p10_timedemo, p7_bench and p4_smoke. Interactive play, bootstrap downloads, audio playback, netplay and the automap entry point are outside this delivery.

## Inputs and conformance

SOURCE_INPUTS.json pins the original 17 modules, demo, WAD and Lua archive. Game data is not committed. Use the existing JaiDOOM bootstrap to obtain wads/doom1.wad.

The timedemo finishes at state 1746772898. State alone does not prove rendering equivalence: verify_conformance.py additionally compares every tic's state, frame byte count and cumulative FNV-1a64 over frame bytes. Each runtime executes a cold replay and a second replay in the same state. The VM, interpreter and both Lua layouts must agree on 7968 records.

At the pinned source and workload:
- Cold frame-stream hash: 0ad5f98ab443f0bb.
- Warmed frame-stream hash: c2502bc18d9f42c2.
- Both finish with 3984 tics, state 1746772898 and a 79443-byte final frame.

The two frame hashes differ because presentation state persists across main() calls. Lua must match each corresponding replay, not merely reproduce the cold hash twice. Trace runs are conformance instrumentation and are never timing results.

## Porting rules

- The same arrays, maps, classes, coroutines, control flow, inputs and algorithms remain in script. No physics, collision, BSP or drawing kernel is moved into C++.
- Source array indices become ordinary 1-based Lua table accesses. Numeric map keys stay unchanged. A per-state null sentinel keeps null array elements present and preserves array length.
- Lua int64 and double sizes are asserted by the host. Truncating integer division, signed remainder, arithmetic right shifts and declared float-to-int boundaries retain their JaiScript results. Constant arithmetic right shifts use exact Lua floor division by a positive power of two.
- Character reads use byte values. Byte construction and UTF-8 output retain the original bytes.
- The three instance classes use Lua metatables; the source constructs them with explicit new/shared-handle semantics. Coroutines use Lua coroutine suspension and resume, including their completion state.
- Read-only container aliases use normal Lua table references. The port does not emulate JaiScript's internal allocation/refcount machinery. Conformance is established for the pinned replay and smoke gates; this tool is not a general JaiScript-to-Lua compiler.
- Append-only frame strings use ordered fragment tables and table.concat. Repeated immutable concatenation would add quadratic copying to a source operation that appends in place. The two explicit Lua equivalents are in tools/lua_overrides; every output byte is checked.
- Native numeric for loops are used only when the AST identifies a declared integer iterator, a fixed nonzero step, invariant local/literal bounds, and no writes/reference-argument exposure of those values. Other loops retain explicit condition/update behavior.
- Lua garbage collection remains enabled with its default policy.

The host binds binary file IO, paths, a steady millisecond clock, integer/floor conversion and JaiScript's exact engine RNG. The RNG is mt19937_64 with the original inclusive floating mapping and modulo integer-range mapping, not Lua's different math.random sequence. Each Lua state owns its RNG; there is no process-wide mutable port state. Both runtimes execute the same number/order of gameplay RNG calls, as verified by the per-tic state trace.

## Build

Use an x64 Visual Studio Native Tools shell on Windows. All build directories below are under out/astra.

    cmake -S Source/JaiScript -B Source/JaiScript/out/astra/doom-comparison -G Ninja -DCMAKE_BUILD_TYPE=Release -DJAISCRIPT_BUILD_TESTS=OFF -DJAISCRIPT_BUILD_EXAMPLES=ON -DJAISCRIPT_BUILD_RUNNER=ON -DJAISCRIPT_RUNNER_PADDING_CONTROL=ON -DJAIDOOM_PORT_LUA=ON -DJAIDOOM_PORT_TOOLS=ON -DJAIDOOM_LUA_ROOT=D:/git/Bindstone/Source/JaiScript/out/astra/deps/lua-5.4.8
    cmake --build Source/JaiScript/out/astra/doom-comparison --target jaiscript_runner jaiscript_runner_padding jai_doom_lua jai_doom_lua_padding jai_doom_reference

The existing GLOOM build_comparison.ps1 also builds these targets. Its preparation script verifies the pinned Lua archive. The Lua host can be built standalone from this ports directory with JAIDOOM_LUA_ROOT set; the reference/export tools require the main JaiScript project.

## Run

    jai_doom_lua --game-root PATH/TO/examples/jaidoom --scripts PATH/TO/ports/lua -- PATH/TO/demo.json 120x78
    jai_doom_lua --script p4_smoke
    jai_doom_lua --script p7_bench

--repeat 1: the first complete replay warms the runtime, then main() runs again without recreating the state. Both state hashes are required. No frame tracing occurs unless --trace is supplied. --limit requires --trace and produces only a diagnostic prefix.

    python Source/JaiScript/examples/jaidoom/ports/verify_conformance.py --build Source/JaiScript/out/astra/doom-comparison --output Source/JaiScript/out/astra/doom-conformance

For a frozen source copy, also pass --game-root. Conformance defaults to both JaiScript backends and both Lua layouts, with one warm replay. It refuses to reuse an evidence directory and records input/executable SHA256 hashes.

## Measure

Use the existing whole-program runner:

    python Source/JaiScript/examples/gloom/ports/run_comparison.py --build Source/JaiScript/out/astra/doom-comparison --engines jai-lua --workloads timedemo --rounds 3 --output Source/JaiScript/out/astra/doom-pair

With both runtimes selected, each round runs Jai normal, Lua normal, Jai padded, Lua padded, then repeats that sequence. Three rounds give six accepted samples per runtime/layout. Every process runs a full warm-up and replay, pinned to one detected performance core. The padding binary has 40 retained volatile statements in its cold help function. Report the paired padding range beside each median.

The measured interval is the existing replay-loop timer. It includes gameplay, rendering, HUD, frame construction and normal runtime/GC work. It excludes parsing, map setup, warm-up, trace hashing and final summaries. ms/tic is the reported integer elapsed milliseconds divided by 3984; use that value rather than the rounded two-decimal display. The process-wide CPU screen and coordinated quiet window remain required.

The runner automatically creates a full frame certificate before timing Lua, or accepts --timedemo-conformance PATH to reuse a certificate for the exact inputs. It rechecks the raw traces, both final frame hashes, source/data hashes and Lua executable hashes. A diagnostic prefix or a stale certificate is rejected.

--engines jai preserves the existing JaiScript-only campaign. Default/all now includes the Lua timedemo. Input changes, failed hashes, nonzero exits and contaminated timings cannot produce accepted points. See the GLOOM ports README for resume, topology, raw logs and padding-band definitions.

## Regeneration

Build jai_doom_export_ast, export the 17 source modules to .jai.json files, then:

    python Source/JaiScript/examples/jaidoom/ports/tools/transpile.py --ast PATH/TO/AST --output Source/JaiScript/examples/jaidoom/ports/lua

The exporter uses JaiScript's actual parser, including its template-string desugaring. The generator propagates workload types, rejects unsupported AST forms and emits readable Lua. Keep runtime.lua and the explicit string-builder equivalents. A regenerated port is not a measurement input until its source manifest and full cold/warm frame oracle are reverified.
