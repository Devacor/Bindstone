# Closure conversion

A capturing lambda keeps its captured values in a flat array on its function object and runs without a capture
environment. Behavior is unchanged on both backends; the interpreter keeps its environments (it is the oracle).

## Today

`vm_backend::exec_closure` builds a capture `environment` (328 bytes, a `make_shared`) whose parent is the lambda's program
scope, defines each captured name in it (`clone_for_capture` for a copy, `share_env_ref` for `[&]`), and stores it as
`script_defined_function::closure_env`. A call installs that env as the frame's parent (`environment_ = function.closure_env`
on every entry path), and the body reaches a captured name through the environment-addressed forms of `op_load`, `op_store`,
the compound, inc/dec, concat-store and ref-return ops (`a = k_invalid_u32`, symbol in `b`) and their lookup caches. A
300k-closure array costs about 615 bytes per closure: the env 344, the function 176, the value node about 88.

## Shape

- `closure_proto` gains the capture layout, fixed at compile time: one entry per name the body may capture (explicit captures,
  then the `[=]`/`[&]` body names in `used_variables`), each with its mode (copy, cell, init) and its source (outer slot, or
  resolve through the capture ladder).
- `script_defined_function` gains `captures`: a `std::unique_ptr<script_value[]>` sized to the layout (16 bytes a name). An
  entry the runtime ladder declines (a global: it resolves when the lambda runs, `src.global`) holds the absent marker
  (raw-invalid, as a field cell does) and its reads fall back to the environment form.
- The body compiles captured names to capture-addressed operands: a new operand mode beside frame slots and the env
  (`k_capture_bit | index`) on every op that addresses a name today. A copy entry reads and writes its own value; a `[&]`
  entry holds the shared cell's reference value and goes through it (`deref`), exactly as the env value does now.
- A converted lambda's `closure_env` is null, so it enters through the capture-free call path (program-scope parent), and the
  running function's captures are reached from the frame: `call_frame` gains a pointer to the running function's capture
  array, set where the entry paths set `closure_env` today. That store is the one touch on the call protocol.

## Contracts kept

- **Copies and cells.** A copy capture is `clone_for_capture` of the source at creation; `[&]` shares the source's cell
  (`share_env_ref`), so writes on either side are seen by both. An init-capture takes `clone_for_assignment` of its value.
- **Globals at call time.** A global is never captured (`src.global`): its entry stays absent and the read resolves through
  the program scope when the lambda runs, so a reassigned global is seen.
- **`this`.** `[this]` and `[=]`/`[&]` with an instance keep the method environment (members resolve through it); only
  lambdas whose captures are all plain names convert. A lambda that captures `this` keeps today's path.
- **Nested lambdas.** A lambda created inside a converted body finds an enclosing captured name through the capture ladder:
  `detail::resolve_capture_source` gains the running function's capture array as the step after the outer frame's slots.
- **Debugger.** The locals walk lists the running function's captures after its slots, under the captured names (the layout
  keeps the symbol ids); a set-variable writes the entry.
- **Hot reload.** A reloaded body compiles against its own proto's layout; a closure made before the reload keeps its
  function object, whose chunk pin and layout stay matched. A layout never changes under a live function.
- **Parallel workers.** `provision_worker`'s function copy clones the capture array the way it clones `closure_env` today
  (copies detach, cells stay shared under the region's admission rules).
- **Coroutines.** A coroutine function decl keeps its capture env (its handle owns an env chain across suspensions).
- **jaibite.** The layout derives from the AST at compile time and the serializer writes no chunk state, so `.jaibite` is
  unchanged (no `k_jaibite_version` bump) unless the layout is persisted.

## Cost and order

Every name-addressing op family gains one operand mode (both env safe-lists in `vm_compiler.cpp`, the disassembler, the
manifest, the debugger's locals walk, the typed-env operand table). That is the bulk of the work and touches the compiler and
the op handlers, not the call protocol; it lands after the call-protocol rework so the frame's access to its function is the
reworked one.

Expected: about 350 of 615 bytes and two allocations (the env and its control block) per capturing closure; a captured read
becomes an indexed load instead of an env lookup cache probe.
