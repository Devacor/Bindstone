# JaiScript Debugger — Design

Status: BUILDING. Roadmap item under **Tooling** ("Debugger — breakpoints, stepping,
variable inspection"). This doc is the plan of record. **Landed:** phase 1 (extension +
grammar), phase 2 (source-filename plumbing), phase 3 (debug `controller` core + statement
hook, and the DAP `debug_connector` — raw-socket attach: handshake, breakpoints, stopped
events, stack location, scopes, read-only variables, continue/pause, step over/into/out),
phase 5 (VM backend hook — breakpoints/stepping/locals on BOTH backends, one shared
controller; see phasing item 5 for the as-built shape and "Debugger performance" for the
cost model + the parallel-region atomicity ruling). **Next:** multi-frame stack,
`setVariable`/`evaluate`, conditional breakpoints (phase 4).

**Landed since (tooling pass):** a `jai::debug::listen(engine&, port=default_port, host)`
convenience (build connector + attach + return it in one line — `connector.hpp`); the default
port is `jai::debug::default_port = 52472`; the connector **normalizes each incoming breakpoint
path** with `std::filesystem::canonical` (`connector.cpp` `normalize_bp_path`) so a VS Code path
binds against the engine's canonical node paths regardless of drive-letter case / slash
direction; and all three example hosts (`examples/{roguelike,crawler,demoreel}`) gained a
`--debug [--debug-port N]` flag that forces the interpreter and calls `listen()` — the
connector listens for the whole engine lifetime (attach/detach/re-attach any time), and the
demo boots immediately; an opt-in `--debug-wait` additionally holds boot until Enter for
catching boot-time script. The demoreel loads scenes via `execute_file` (not the concatenated
`.jaibite`) under `--debug` so `scenes/*.jai` breakpoints bind. Verified end-to-end over a real
loopback socket against both `jai_crawler` and `jai_demoreel` (attach → verified breakpoint →
`stopped` → `stackTrace`/`scopes`), including a deliberately mis-cased/forward-slash path.

Goal: a VS Code step-debugger for JaiScript that attaches to a running C++ host (Bindstone/MV),
sets breakpoints in `.script` files, pauses/steps execution, shows the call stack and every
in-scope variable, and edits variables live. Plus syntax highlighting.

## Core rulings

1. **No third-party transport, no asio.** The debug *core* (`controller`) — hook, breakpoint
   table, step state machine, park-and-pump loop, variable inspection/edit — is pure `std` and
   lives in JaiScript. Transport is a **raw OS-socket TCP server** (`#ifdef _WIN32` WinSock else
   BSD sockets) in an *optional, self-contained* `jai::debug_connector` class, constructed with a
   port and injected per engine: `engine->set_debug_connector(std::make_shared<jai::debug_connector>(1234))`.
   Compiled only when enabled — no asio, no Node adapter, no external tool. It runs on its own
   `std::thread` and uses JaiScript's own `stdlib/json.hpp` for DAP JSON. The same class serves
   **Bindstone and the JaiScript-only demos**; nothing in the core links it, so the default build
   is unchanged. (A host that already runs an event loop — e.g. Bindstone's asio `io_context` — may
   instead feed bytes to the engine's `controller` and skip `debug_connector`, but that's the host's
   option, never a dependency.)
2. **Protocol is the Debug Adapter Protocol (DAP), attach model, no separate adapter process.**
   The app opens a TCP port; VS Code connects directly via a `DebugAdapterServer` descriptor. DAP
   is a subset of JSON-over-a-stream; `debug_connector` speaks it directly.
3. **Interpreter backend first.** The tree-walker keeps every local named in `environment`, so
   inspection is trivial; it proves the pause/threading model end to end. Debug the interpreter
   with `MV_SCRIPT_INTERPRETER=1`. VM parity is phase 4 — and needs no baked-in name table: VM ops
   already carry `(slot, symbol id)`, so a frame's locals are named **lazily** via the interner's
   reverse lookup (`string_symbolizer::get_string(id)`) at stop time. Release chunks stay lean.
4. **The parked script thread does all engine-state access.** Whichever thread runs the script
   (host's choice — main loop or a worker) is the one that blocks at a breakpoint and services
   inspect/edit via *posted closures*. The transport thread never touches live engine structures.
   The only invariant is **transport-thread ≠ script-thread**; no assumption about *which* thread
   runs the script. (If a host instead ran scripts on a worker and polled variable snapshots under
   a lock after each tick, that works too, but it can't offer true pause/step — the park/pump model
   is what buys stepping.)
5. **`.jaibite` is a first-class target — source-filename plumbing LANDED (phase 2, ✅).**
   `.jaibite` round-trips `source_location.filename` + line per node (`ast_serializer.hpp:107`/`600`).
   Previously plain `execute(content)` stamped every node `"<script>"` and `include`/`import`/
   `execute_file` dropped their known paths. Now: the private `engine::execute_source(content,
   vars, sourcePath)` is the one lex+parse+run entry — it threads `sourcePath` into the lexer (every
   node's `location.filename`) and keys the parse cache on `(sourcePath, content)` (length-prefixed,
   collision-free) so identical content under two paths no longer aliases one AST. Public hosts get
   filenames via **`execute_file(path, vars)`** (path carried for free — no debugging-only param on
   `execute`); `include`/`import` pass their resolved path; `check` uses `"<script>"`. Verified by
   `Exception Handling.execute_file_stamps_stack_frame_file` (both backends). Remaining for phase 3:
   MV must call `execute_file` (or the debugger path) so gameplay scripts carry absolute paths.
6. **No true reverse execution.** Host side effects (mutating entities, rendering) can't be
   un-run. "Step back" ships last, as read-only *value history*, not replay. See phase 5.

## What the code already gives us

- **One shared AST, every node stamped `{file, line, column}`** — `ast_node::location`
  (`detail/ast.hpp:160`), `source_location` (`core/types.hpp:59`). Universal breakpoint/step
  ground truth for both backends.
- **Clean per-statement choke points.**
  - Interpreter: `dispatch_stmt` / `dispatch_decl` (`interpreter.cpp:12088` / `12126`) — run once
    per statement and already write `call_frame::current_node`.
  - VM: top of the `run_dispatch` `for(;;)` (`vm_backend.cpp:7728`); a statement boundary is
    `chunk::stmt_nodes[ip]` changing value.
- **VM line table already exists** — `chunk::stmt_nodes` (`vm/chunk.hpp:125`), a
  `vector<const ast_node*>` parallel to `code`, appended in lockstep in `vm_compiler::emit`
  (`vm_compiler.cpp:141`). `stmt_nodes[ip]->location` = source line for any offset. So breakpoint
  mapping is solved on **both** backends.
- **Inspectable call stacks.** Interpreter: `call_stack_` (`std::vector<call_frame>`,
  `interpreter.hpp:689`; frame `environment.hpp:263`). VM: `frames_` / `call_records_`
  (`vm_backend.hpp:98,128`). Stack depth drives step-over/into/out.
- **Named scopes.** `environment` (`environment.hpp:39`) maps interned symbol id → `script_value*`
  (`flat_lookup_`), parent-linked (`parent_`). Enumerate via `get_local_variables()`
  (`environment.cpp:577`) / `get_all_variables()` (`environment.cpp:692`). Names never erased —
  `string_symbolizer` (`detail/string_symbolizer.hpp:30`) keeps id↔name for the engine's life.
- **Value display.** `script_value::to_string()` (`value.cpp:563`); richer object rendering via
  `value_to_string_with_method` (`vm_backend.hpp:310`, calls a script `to_string()`); `to_json`
  (`stdlib/json.hpp`) for the expandable arrays/maps/objects tree.
- **No existing debug hook** (roadmap confirms). We add one at **statement granularity** — a
  relaxed-atomic gate load in `dispatch_stmt`/`dispatch_decl` (interpreter) and an `enabled_`-gated
  `stmt_nodes[ip] != last_stmt` compare at the top of `run_dispatch` (VM). NOT the budget-check
  sites (loop back-edges + call entry): those aren't statement boundaries, so straight-line code
  between them would never poll the pause flag or hit a breakpoint, and `next` could overshoot.
  This is a more frequent site than the budget check — re-measure the detached-path cost for it
  (the gate is one predictable branch when disabled, but validate against the VM-perf baseline).
- **Transport is trivial and dependency-free.** A listening TCP socket is ~150 lines of
  `#ifdef _WIN32` WinSock / BSD sockets — both OS-provided. VS Code attaches to the port directly.
  (If a host prefers, Bindstone's asio `io_context` on a worker thread — `network.cpp:129`/`210`,
  main-thread-drained inbox `network.cpp:152` — is a ready alternative, but not required.)
- **`.jaibite` keeps source locations.** `ast_serializer` writes/reads `loc.filename` per node
  (`detail/ast_serializer.hpp:107`/`600`); ROADMAP "jaibite binary save/load of parsed scripts" =
  the located AST round-trips. Bytecode-loaded scripts debug against their `.jai` source unchanged.
- **Host seam.** MV builds one `jai::engine` per `Game` in `Services` (`script.cpp:41`,
  `game.cpp:47`); `MV::Script::eval` (`script.cpp:68`) receives the script's **file path** as its
  identifier (`standardScriptMethods.h:48`) — the anchor for the path↔unit breakpoint map.

## The threading model (the crux)

In Bindstone today scripts run on the main render thread (`Game::update` → per-entity
`update(self,dt)` into the backend, every frame), but the host is free to run them on a worker.
The design assumes only that the **transport thread is a different thread from the one running the
script**, so it can keep servicing DAP while the script thread is parked:

```
 script thread (main OR worker)         transport thread (debug_connector, own std::thread)
 ------------------------------         ----------------------------------------------
 ... executing script ...
 hook(): breakpoint hit?
   freeze execution budget
   emit `stopped` event  ---------->     forward as DAP `stopped` (+ top frame's source path
   park: cv.wait, draining                → VS Code opens/focuses that file)
     command queue           <----(post_command)---- DAP request:
   run closure on live state                          "variables", "setVariable",
   reply result  ------------------>                  "evaluate" -> post_command(fn)
   ... loop until resume/step ...  <----(resume/step_*)---- DAP "continue"/"next"/...
   thaw budget; set step mode
 ... resume script ...
```

- The transport thread **must keep servicing DAP while the script thread is parked** — the socket
  lives entirely on the `debug_connector` thread; it never needs the script thread to pump it.
- Every inspect/edit runs inside a `post_command` closure **on the parked thread**, against live
  `environment` / `call_stack_`. No engine data crosses threads.
- **Execution budget: re-arm at resume, don't "thaw".** The deadline is an *absolute*
  `steady_clock` time_point (`interpreter.hpp:673`, `vm_backend.hpp:228`), and
  `prepare_for_execution` → `arm_execution_deadline` runs **unconditionally** on every
  `execute`/reentrant `include` (`engine.cpp:886`). So a naive "re-enable on resume" leaves a
  long-past deadline → the next 1024-tick sample raises a *terminal* budget error and the paused
  script is killed by the act of debugging. Fix: `resume()`/`step_*()` call `arm_execution_deadline()`
  fresh **before** unparking (the script gets a full budget for the remainder), and a controller
  `debug_suspended` flag is the single authority over arm/check so nested `prepare_for_execution`
  during a pause (e.g. a repl `evaluate`) can't silently re-activate the clock.
- Whatever the script thread is doing freezes while parked (in Bindstone: the whole game) —
  acceptable for single-player/editor debugging; flag for any future timed-server-tick debugging.
- Reentrant `include(...)`/`execute` (e.g. `creature.jai` entered several times a frame) is fine:
  each stop reports a stack whose frames carry file+line, and VS Code auto-opens the top frame's
  file — so stepping "follows" across files with no extra work.

## Component layout

```
JaiScript core (pure std, always built)     JaiScript optional (compiled when enabled)
---------------------------------------     ------------------------------------------
jaiscript::debug::controller                jai::debug_connector(port)
  - atomic<bool> enabled_ (hook gate)          - raw TCP listener, own std::thread
  - breakpoint table (file -> {lines})           (#ifdef _WIN32 WinSock / BSD sockets)
  - step state machine (mode + base depth)     - DAP codec: Content-Length framed JSON,
  - park loop + command queue (mutex/cv)         built with stdlib/json.hpp
  - on_stopped callback                        - DAP request  -> controller call
  - post_command(fn), resume(), step_*()       - controller event -> DAP event
  - get_stack_frames / get_scopes /            engine->set_debug_connector(shared_ptr<...>)
    get_variables / set_variable               serves BOTH Bindstone and JS-only demos
  hook at dispatch_stmt (interp)
    / run_dispatch (vm)                      Host wiring (MV, or a demo main)
                                               - engine->set_debug_connector(make_shared<
VS Code extension                                  debug_connector>(1234))
  - TextMate grammar (.tmLanguage.json)        - register path <-> unit at eval/include time;
  - launch.json "attach" -> DebugAdapterServer(port)   invalidate on hot-reload
  - (later) LSP client for engine::check()     - resolvable .jai path stamped at parse time
```

**Q1 — resolved.** The `controller` is owned by / reached through `jai::engine` (internal accessor
`engine::debugger()` for programmatic breakpoints). Transport is injected per engine:
`engine->set_debug_connector(std::make_shared<jai::debug_connector>(1234))` — typically **one
connector per engine**. The hook is one atomic-gated call at the statement choke points; when
`enabled_` is false it is a single predictable branch — **statement-only granularity (Q2), no
per-expression stepping**, to keep the detached hot path free.

## Feature mapping (DAP request → mechanism)

| DAP | Mechanism |
|-----|-----------|
| `setBreakpoints` | Store per-file line set as an atomically-published snapshot (see Concurrency contract). **Resolve at hit time, not set time** — the hook compares `current_node->location` / `stmt_nodes[ip]->location` (path,line) against the snapshot. VM function bodies compile to their own chunks lazily on first call (`chunk_for_body`), so pre-scanning `stmt_nodes` at set time misses any not-yet-called function; hit-time compare needs no pre-index (the `ast_node*` carries its location either way). Snap a line with no statement to the next statement's line and report it back. |
| `stackTrace` | Walk `call_stack_` (interp) / `frames_` (VM); each frame → `{name, source.path, line}` from `current_node->location` / `stmt_nodes[ip]->location`. VS Code opens/focuses the top frame's `source.path` automatically — this is what makes stepping auto-open the right `.jai` file across `include`s. |
| `scopes` | Per frame: "Locals" (frame env / `call_frame`), "Globals" (root `environment`). |
| `variables` | `environment::get_local_variables()` for the frame + walk `parent_`. Render with `value_to_string_with_method` → `to_string()` fallback; arrays/maps/objects get a `variablesReference` expanded via `to_json`/element access. **Handles are generation-scoped** (see Concurrency contract) — never store a raw `environment*`/`call_frame*` across an unpark. |
| `setVariable` | Parse the new value, then **route through `enforce_type_compatibility`** (`interpreter.cpp:5740` / `vm_backend.hpp:361`) before `environment::assign` — raw `assign` bypasses the int/auto type ladder for locals. Object fields self-enforce (`class_instance::enforce_field_write`). |
| `continue` | `resume()`: clear step mode, **re-arm the deadline**, set `resume_requested` and notify (all under the controller mutex). |
| `next` / `stepIn` / `stepOut` | Set step mode with base depth = current stack depth; unpark. Hook stops when: In = next statement, any depth; Over = depth ≤ base; Out = depth < base. |
| `evaluate` | `post_command` → run expression in the selected frame's env via the existing execute-with-locals path. **Q4 — resolved by DAP context:** `context:"watch"`/`"hover"` re-run on *every* stop, so treat them as read-only (evaluate, but a side-effecting watch is the user's footgun — don't special-case); `context:"repl"` allows full mutation. |
| `pause` | Arm a one-shot "stop at next statement" flag the hook checks. |

## Breakpoint resolution notes

- Breakpoints are keyed by **(source path, line)** — but see ruling 5: the engine must first be
  taught to stamp real source paths on nodes (`execute(content, source_path)` + `include` fix +
  cache re-key). Until then all nodes read `"<script>"` and no file can be distinguished.
- **`.jaibite` and `.jai` are indistinguishable to the debugger.** A jaibite unit deserializes to
  the same located AST; its nodes carry the original `.jai` path (`ast_serializer.hpp:600`). Key on
  that path, and a breakpoint set in `creature.jai` binds whether the running unit was loaded from
  source or from `creature.jaibite`. The host must ensure the parse-time filename is a resolvable
  `.jai` path (not the `.jaibite` name) so VS Code can open it.
- Hot reload (`MV_SCRIPT_HOT_RELOAD`, re-eval on mtime change) must invalidate and rebind the map;
  keying on (path, line) lets breakpoints survive a reload and re-resolve against the new unit.
- A line with no executable statement snaps to the next statement's line (report the adjusted line
  back via the `breakpoint` event, per DAP).
- **Hot-reload lifetime:** the controller stores only `(path,line)` + a per-unit generation, never
  a cached `ast_node*`/chunk pointer across a reload. Reload frees the old AST while
  `chunk::stmt_nodes` and any resolved-node caches hold raw `const ast_node*`; drop all node/chunk-
  derived caches in the same invalidation that rebinds the path↔unit map, *before* the old unit dies.

## Concurrency & lifecycle contract

This is the part that must be exact — it's where debuggers deadlock, race, or hang. The park/pump
sketch above is the shape; these are the rules.

- **One controller mutex** guards `{paused, resume_requested, step_mode, step_base_depth,
  command_queue}`. **One condition_variable.** The hook, under the mutex, sets `paused=true` and
  `resume_requested=false` *before* firing `on_stopped`, then waits on a **predicate**:
  `cv.wait(lock, []{ return resume_requested || !command_queue.empty(); })`. `resume()`,
  `step_*()`, and `post_command()` all mutate under the same mutex, then `notify`. This closes the
  **lost-wakeup race** (a `continue` arriving between "emit stopped" and "enter wait" is not lost)
  and gives the happens-before edge for step mode read after unpark.
- **Breakpoint table is a swapped snapshot, not closure-mutated.** `setBreakpoints` arrives on the
  transport thread while the script thread is *running* and reading the table every statement — so
  it can't go through `post_command` (closures drain only while parked → initial attach breakpoints
  would never bind). Publish it via `atomic<shared_ptr<const bp_table>>`; the hook does one relaxed
  load per statement. The `pause` one-shot flag and step-cancel are likewise transport-writable
  atomics. **Rule:** posted closures are *only* for live engine-state access; controller-owned
  state (breakpoints, pause flag, step mode) is transport-writable under explicit synchronization.
- **`detach()` guarantees a parked thread always wakes.** On socket EOF/error, on the DAP
  `disconnect` request (reply *then* detach), and from the `debug_connector` destructor, call
  `detach()`: atomically clear `enabled_`, clear breakpoints + step mode, set `resume_requested`,
  notify. Without this, a client disconnect/crash while parked hangs the host forever — and in
  Bindstone the parked thread *is* the main thread, so the app can't even process its quit event.
  **Teardown order:** `debug_connector` torn down (thread joined) **before** the engine, with
  `detach()` first, so the script thread leaves the hook before anything it references dies.
- **Single outbound writer.** `on_stopped` runs on the *script* thread; request responses are
  written by the *transport* thread. Two writers interleave `Content-Length` frames and corrupt the
  DAP stream. **All socket writes happen on the connector thread only:** `on_stopped` and closure
  replies enqueue an outbound message under a mutex and poke the wake socket; the poll loop drains
  and writes.
- **Reentrancy guard.** A repl `evaluate` runs script code on the parked thread through the normal
  interpreter, so a breakpoint or still-armed step *inside* the evaluated call would re-enter the
  hook and park recursively. Set an `in_debug_command` flag around every posted-closure execution;
  the hook returns immediately when it's set (no breakpoints, no stepping inside debug evaluations)
  — same as gdb/DevTools.
- **Generation-scoped handles.** `variablesReference`s are valid only for the current suspension.
  Encode `(generation, index)`; bump the generation and clear the handle table on every resume; a
  stale `variables`/`setVariable` (VS Code races these around continue) is rejected by the transport
  thread with a DAP error *without* posting a closure — never deref an unwound `environment*`.

## DAP lifecycle & request routing

The feature table starts at `setBreakpoints`, but a working attach needs the full lifecycle first,
or VS Code hangs in "starting":

- **Handshake:** `initialize` → respond with capabilities (at minimum
  `supportsConfigurationDoneRequest`) → on `attach`, respond **then send the `initialized` event**
  (without it VS Code never sends breakpoints/`configurationDone`) → accept `setBreakpoints` /
  `setExceptionBreakpoints` → respond to `configurationDone` → live.
- **Threads:** answer `threads` with one static thread (`id 1`, "script"); every `stopped` /
  `continued` event carries `threadId 1`. VS Code sends `threads` *outside* stops.
- **Disconnect:** respond, then `detach()`, then return the listener to `accept` for reconnection.
- **Request routing — answer-anytime vs park-only.** Closures drain only while parked, so a
  park-only request sent while running would hang VS Code's REPL/views. The connector classifies:
  *answer-anytime on the transport thread* — `initialize`, `threads`, `setBreakpoints`, `pause`,
  `disconnect`; *park-only via `post_command`* — `stackTrace`, `scopes`, `variables`, `setVariable`,
  `evaluate`. A park-only request received when not paused gets an immediate DAP error ("not
  stopped"), never a silent queue.
- **Framing:** a byte-buffer layer that handles partial reads, multiple messages per `recv`, and the
  `\r\n\r\n` header terminator; a send-all helper handling short writes and `EINTR`/`WSAEWOULDBLOCK`.

## Transport internals (portability)

Do **not** block in `accept()`/`recv()` — unblocking them at teardown diverges per platform
(`close()` doesn't wake a Linux `accept`; `shutdown()` on a listener is `ENOTCONN` on BSD/macOS).
Instead run the connector loop on `poll()`/`WSAPoll()` over `{listener, client, wake}` with sockets
**non-blocking**, where `wake` is a **self-connected loopback socket pair** (identical on all three
desktop targets; pipes don't work with `WSAPoll`). `stop()`/`detach()` writes one byte to `wake`.
This one structure also handles reconnect (return to polling the listener on client EOF) and
outbound delivery (drain the write queue when `wake` fires). Bracket `WSAStartup`/`WSACleanup` in
the connector ctor/dtor.

## Syntax highlighting

A TextMate grammar (`.tmLanguage.json`) in the extension — keywords, the `int`/`auto`/`var` type
ladder, `function`/`lambda`, strings/format-strings, comments, numbers. Fully independent of the
debugger; ships first. A later LSP can layer semantic highlighting + diagnostics on
`engine::check()` (`engine.hpp`), which already yields per-diagnostic `source_location`s.

## Phasing

1. **Extension + TextMate grammar.** Syntax highlighting. Zero engine changes. **(Done.)**
2. **Prerequisite: source-filename plumbing (core). ✅ Done.** Private `execute_source(content,
   vars, sourcePath)` threads the path onto every node; `execute_file(path, vars)` is the clean
   public entry (no debug param on `execute`); `include`/`import`/`execute_file` carry their paths;
   parse cache re-keyed on `(sourcePath, content)`. 1713/1713 green on both backends (Release).
3. **Debug core + DAP attach, interpreter backend. ✅ Done.** The concurrency & lifecycle contract
   (one-mutex predicate cv, snapshot breakpoint table, `detach()`, single outbound writer,
   generation-scoped handles, park-only vs answer-anytime routing) + the DAP handshake
   (`initialize`/`initialized`/`configurationDone`/`threads`/`disconnect`) + poll/wake-socket
   transport (`jai::debug::debug_connector`, gated by `JAISCRIPT_ENABLE_DEBUGGER`, wired via
   `engine::set_debug_connector`). Breakpoints, pause/continue, **step over/into/out**, top-frame
   stack location, **read-only** variables. Proven end-to-end over a real loopback socket by the
   `Debugger Connector` suite. Deferred to phase 4: multi-frame stack walk, `setVariable` editing,
   `evaluate`. (`MV_SCRIPT_INTERPRETER=1`.)
4. **Stepping + variable editing + conditional breakpoints.** step over/into/out (depth machine),
   budget re-arm on resume, reentrancy guard, `setVariable` with type-ladder enforcement, `evaluate`
   (repl mutates, watch/hover read-only-by-convention), conditional breakpoints + logpoints (Q5).
5. **VM backend parity. ✅ Done (as built).** The vm mirrors the interpreter's plain-cache
   architecture against the SAME engine-owned controller (bloom/snapshot/step state all live
   controller-side — nothing duplicated): a cached `debug_hook_` gate tested once per dispatch
   iteration (a predictable branch when no session — the vm hot-loop band holds, see the cost
   table), statement boundaries from `chunk::stmt_nodes[ip]` via an out-of-line edge detector
   (`debug_statement_boundary`: fires when the stamped node changes OR a backward jump
   re-enters the same statement, so loop-body breakpoints re-fire per iteration; out of line so
   `run_dispatch`'s Debug frame stays flat — invariants.md §5), sync points at
   `prepare_for_execution` + the vm's 1024-tick budget twin + park exit, and the SAME park (the
   script thread blocks in the hook inside `run_dispatch`; suspended fibers just stay suspended
   — no fiber-level suspension machinery needed, and none was). A stopped frame's locals are
   named **lazily** as planned: `get_current_frame_locals` scans the parked frame's chunk
   (`debug_paused_frame_`, exact even for native-entry frames) for decl/load/store/incdec
   operands PLUS the fused side tables (`fused_binary_protos`, `counted_for_protos`,
   `compound_fused_protos`, `iter_protos`, `destructure_protos`) and maps symbol→name through
   the interner. No name table baked into release chunks. Known deltas from the interpreter: a
   breakpoint on a loop-HEADER line (`for`/`while` condition) re-fires per iteration on the vm
   (the header ops re-execute; the interpreter dispatches the loop statement once), and a
   parameter the body never references is invisible in the vm Locals view.
   `engine::wire_backend` re-wires the controller across backend swaps, so
   `debugger()`-before-`set_backend(vm)` works in either order.
6. **Value history ("step back") + LSP.** Snapshot touched variables per stop for read-only
   backward inspection; optional LSP on `engine::check()`.

## Portability

Targets: Linux, Windows, macOS, plus Android/iOS *shipping* builds (no on-device script
debugging). One `#ifdef _WIN32` → WinSock, `#else` → BSD sockets covers all desktop targets; that
is the entire platform surface. The `controller` core is portable `std` everywhere. The
`debug_connector` class is **compiled out on Android/iOS** (and any build without the debug
option), so mobile ships zero debug/socket code. Guard: a CMake option (e.g.
`JAISCRIPT_ENABLE_DEBUGGER`), off by default, on for desktop dev/editor configs.

## Resolved rulings (from open questions)

- **Q1 → `engine::debugger()`.** Controller owned by / reached through `jai::engine`.
- **Q2 → statement-only.** No per-expression stepping; keep the hot path free.
- **Q3 → game-specific, per-engine controller.** No built-in multi-session concept; the host owns
  policy. Multiple `execute`/`include` reentries per frame are expected — each stop carries file+line
  and VS Code auto-opens the top frame's file, so line-by-line stepping follows across files. Engines
  may live off the main thread; those sharp edges are the host's to manage (ruling 4).
- **Q4 → DAP-context split.** watch/hover read-only-by-convention; repl allows mutation. See table.
- **Q5 → phase 3.** Conditional breakpoints + logpoints land with stepping/editing.
- **Q6 → lazy interner reverse lookup.** No baked slot→name table; reconstruct at stop time. See phase 4.
- **Watch side effects → document, no forced read-only.** VS Code re-runs every Watch/hover
  expression on *every* stop, and JaiScript can't prove an expression is side-effect-free, so a
  watch like `self.take_damage(10)` would silently fire on each step. We match gdb/DevTools: document
  "don't put state-changing expressions in a watch" and ship. A `launch.json` opt-out toggle is a
  trivial later add if it ever bites — not built up front.
- **Multi-engine → one `debug_connector` per engine (per thread ideal).**
  `engine->set_debug_connector(std::make_shared<jai::debug_connector>(port))` — normally one
  connector (one port) per engine. Connectors *can* be shared across engines/threads, but then
  thread-safety is **the host's responsibility** (their critical sections); JaiScript does not claim
  to support unsynchronized cross-thread debugging. The clean pattern for multi-threaded hosts is a
  **separate `debug_connector` (distinct port) per thread**.

## Debugger performance (cost model — supersedes the "relaxed-atomic gate" hook description above)

Dev's rulings (2026-07 perf pass): zero impact when not enabled is non-negotiable; enabled but
not connected must also be free; the executing script must never consult a live/shared/locked
structure on the hot path — connection detection **and** breakpoint synchronization happen
off-cycle, at the same two points as the script-timeout clock. The implementation:

- **The per-statement path reads plain script-thread memory only.** `dispatch_stmt`/
  `dispatch_decl` test one cached plain pointer (`interpreter::debug_hook_`, null unless a
  session is enabled). When it is non-null, `controller::wants_statement(line)` reads two more
  plain members: a step/pause flag and an 8 KB **line bloom** (bit = breakpoint line mod 65536).
  No atomics, no mutexes, no `shared_ptr` loads, no string hashing — those all happened per
  statement in the first cut and were the observed enabled-mode slowness (see table).
- **Debug sync points (the off-cycle pattern).** The script thread pulls transport-side state
  (attach/detach, breakpoint edits, pause requests) into its cache only at: (a) **`execute()`
  entry** (`prepare_for_execution` → `sync_debug_hook`), (b) **the every-1024-ticks budget
  sample** (`execution_budget_exhausted` cold block — the same sampling the script-timeout
  clock uses; the tick now counts even for budget-0 hosts so a long-running script still
  syncs), and (c) **park exit** (resume/step/detach re-sync before the next statement runs, so
  stepping is exact, never 1024-statements late). Cost of a steady-state sync: three relaxed
  loads + a version compare; the breakpoint table/bloom rebuild runs only when
  `setBreakpoints` actually changed something (`bp_version_`).
- **Consequence (documented latency):** a breakpoint set or pause requested *mid-execution*
  binds within ~1024 statements, or at the next `execute()` entry, whichever comes first.
  Anything done while parked (the normal IDE flow) is exact and immediate.
- **Transport side stays lock-based/atomic** — `set_breakpoints` publishes an immutable
  snapshot + bumps `bp_version_` (release); the script thread re-caches on version mismatch
  (acquire). The hand-off is the sync point; the hot path never touches the shared table.
- **Budget interaction.** While the hook is armed the interpreter skips wall-clock deadline
  checks (a script parked for minutes must not be killed by the act of debugging — and no
  cross-thread engine-budget write happens on attach anymore); `sync_debug_hook` re-arms a
  fresh deadline the moment the session ends. Memory-cap enforcement stays active throughout.
- **Connection detection** was never per-statement: the connector runs its own poll loop on
  its own thread (`accept` there), and DAP `attach` just flips `enabled_` — which the script
  thread notices at the sync points above. Constructing a `debug_connector` opens no thread
  until `engine::set_debug_connector` calls `start()`; no ambient threads exist when unused.
- **Call frames** carry one unconditional pointer store (`call_frame::debug_function`, used to
  name slot locals at a stop) — within the "one cached test per call-frame push" budget.
- **`engine::execute` path:** the (sourcePath, content) parse-cache key is built in a reused
  member buffer — no per-execute allocation for the key (debugger-era plumbing had introduced
  one).

Measured (Foundry `Debugger` suite benches, Release BENCHMARKS, i7-6920HQ, min-of-3, the
"Hot Loop (1000 iterations)" script on the interpreter — integer µs, ±50% harness variance):

| configuration                          | before (4e2540a4+merge) | after this pass |
|----------------------------------------|-------------------------|-----------------|
| no debugger constructed                | 152                     | 142             |
| controller constructed, no session     | 146                     | 145             |
| session enabled, no breakpoints        | 187                     | 144             |
| session enabled, bp in a cold file     | 187                     | 142             |
| session enabled, bp line collides      | 191                     | 160             |

Flat within harness noise in every configuration except the deliberate worst case — a
breakpoint in *another file* whose line number collides with the hot statement's line, which
pays one string compare per collision (bounded, and vanishingly rare in practice). The
backends hold their pre-debugger bands with the debugger compiled in and enabled hooks fixed
(vm: hot loop 44-55, fib(15) 666-742 vs the recorded 47-48/687-741 — measured pre-phase-5;
the vm's own hook cost model is below). Paused/stepping cost is not budgeted (human-paced),
but a step-over across hot code re-checks breakpoints through the bloom, not the string table.

### VM backend (phase 5) cost model

Identical architecture, one adaptation: the vm has no per-statement chokepoint, so the gate is
tested once per **dispatch iteration** (per op) — still one plain predictable branch when no
session is enabled, and the boundary/edge work runs only while armed. Same 5 configurations,
same script, vm backend (min-of-3, integer µs):

| configuration                          | vm, with the hook |
|----------------------------------------|-------------------|
| no debugger constructed                | 46                |
| controller constructed, no session     | 46                |
| session enabled, no breakpoints        | 50                |
| session enabled, bp in a cold file     | 52                |
| session enabled, bp line collides      | 63                |

The disabled/unattached rows sit exactly on the vm's pre-debugger 44-48 band (Dev's hard
requirement). An armed session costs ~10% on the vm (the statement-boundary edge detection
runs per dispatch iteration while armed — an out-of-line call per op; the interpreter's armed
cost hides inside its statement dispatch). The bloom keeps breakpoint filtering line-cheap
exactly as on the interpreter; the collision worst case is the same bounded string-compare.

### Parallel regions are ATOMIC to the debugger (Dev ruling, both backends)

- A breakpoint ON the `parallel_transform` call line breaks normally (before the call); a
  step-over at the call line runs the entire region and lands after the join; **step-into is
  the same as step-over** — there is no stepping into a region.
- Breakpoints on lines INSIDE the parallel body **never fire while the body executes as part
  of a region — including chunk 0 on the calling thread.** This is *structural*, not a
  suppression flag: every region context (chunk 0 included) is a fresh worker-slot backend
  (`provision_worker`) that `set_debug_controller` was never called on, its `debug_hook_`
  gate stays null, and its budget-tick sync sees a null controller — workers never consult
  the debugger at all. The live engine backend is parked inside the builtin for the region's
  duration and executes no body statements. Pinned by the `Debugger` suite
  (`parallel_region_atomic_to_debugger`, both backends).
- Rationale (Dev delegation, finalized): pausing chunk 0 would leave N-1 workers running
  against live budget clocks (debugging would *cause* terminal budget errors), fire for only
  1/W of elements, and expose a half-filled output array + frozen region structures.
- The same body function called OUTSIDE a region (a normal serial call) breaks normally.
- **Sanctioned workflow for debugging a parallel body:** the body is by construction a pure
  function of its element (admission-enforced), so debug it serially — call `fn(arr[i])`
  directly (console/temporary code) or swap the region for a plain `for` loop; the purity
  contract guarantees identical semantics.

**Deliberately not done:** a literally-empty statement path when disabled would require dual
dispatch of the whole interpreter (or a per-statement indirect call — worse); the single
predictable plain-pointer branch measures below harness noise (same for the vm's per-dispatch
gate). `set_enabled` keeps breakpoints across detach (the connector clears sessions via
`set_enabled(false)`).
