# Typed regions: proven code pays nothing

The VM executes proven scalar code on raw 8-byte slots with opcodes that cannot fail, and
leaves everything the checker cannot prove on the general path it has today. The general
path is the specification; a typed region is a lane through it that produces the same
values, the same errors and the same side effects, byte for byte. The interpreter never
gains typed slots: it is the oracle the differential gates run the VM against.

This is the structure behind the 2026-07 ruling (registers plus typed lanes), connected end
to end. The pieces it composes already exist: parse-time type proofs
(`type_proofs_design.md`), typed array buffers (`typed_array_design.md`), frame slots
that hold temporaries (register-file wave 1), the field-shape epoch, and the definition
epoch that parameter aliasing invalidates against.

## What Lua spends and what we spend

| cost | Lua 5.4 | JaiScript VM | typed region |
|---|---|---|---|
| a value | 16-byte tagged union | 32-byte `script_value`, constructed and destroyed per temp | raw `int64_t` / `double` in a typed slot, boxed only at a boundary |
| `a + b` | one opcode, tag test | generic op, storage switch, overflow policy, `checked_result` | `add_ii` / `add_ff`: no switch, no result object; overflow is one flag test that raises |
| `xs[i]` | table array part | typed buffer, then a boxed read and a re-checked store | `load_i64`, `store_f64`: bounds test only, straight to a typed slot |
| `p.x` | hash lookup | slot map, shape epoch, inline cache | flat record: `load [record + offset]`, the offset fixed at parse time |
| `f(a, b)` | register window | frame, environment bookkeeping, argument conversion | typed convention: arguments land in the callee's typed slots, no conversion, no environment |
| result checking | none | every operation | none inside a region |

The general path keeps every check. A region removes them where the checker proved them
redundant, which is the `checked_result` ruling (optimize by starving it) applied
structurally.

## Eligibility

A **typed slot** is a frame slot whose every definition and use inside one function the
checker proves to be one of `int`, `float`, `bool`, `char`. The checker's existing walk
already stamps same-type proofs on declarations, assignments, call arguments and returns;
the slot class is the conjunction of those proofs over the slot's whole lifetime. A slot with
one unproven use is a general slot for the whole function.

A **typed region** is a maximal straight-line or loop span of instructions whose operands
are typed slots, typed buffer elements, literals, or proven record fields. It ends at:

- a read or write of a general slot, a `var`, or an unproven field;
- a call the checker cannot prove typed (see the calling convention);
- an operator the class table may overload (the operator-table epoch guards this);
- a statement the debugger can stop on when a session is attached (the statement hook fires
  at region entry and exit; inside a region, statements are stepped through by falling back
  to the general path for that execution);
- a coroutine `yield`.

Region entry boxes nothing. Region exit materializes each live typed slot into its general
slot only if a later use needs a value, which the same liveness walk decides.

## Instruction set

The instruction stays `op, a, b, c`. Typed opcodes are new entries in the same enum and run
in the same loop; there is no second dispatcher. First set:

- arithmetic: `add_ii sub_ii mul_ii div_ii mod_ii neg_i`, `add_ff sub_ff mul_ff div_ff neg_f`,
  `i2f`, and the compare family into a bool slot;
- element: `load_i64 store_i64 load_f64 store_f64 len_typed`, with the bounds test and the
  memory-cap deferral inside the opcode;
- flow: `jmp_if_b`, `loop_back` (carries the budget tick and the debugger sync exactly like
  today's back edge);
- boundary: `box_i box_f box_b unbox_i unbox_f unbox_b` between a typed slot and its general
  slot;
- records (stage 3): `load_field_i64 store_field_f64` with the shape-epoch guard folded into
  region entry, not into each access;
- calls (stage 2): `call_typed` with a signature id, `ret_i ret_f`.

Every typed opcode has one failure mode at most (overflow, bounds, division by zero) and
raises the same catalog text the general opcode raises for the same input
(`docs/diagnostics.md`). Integer overflow keeps the build policy: checked builds raise, wrap
builds wrap, through the same `jai::ints` helpers.

## Frame layout

A frame keeps its `script_value` slots and adds a typed area of raw 8-byte cells. The
compiler assigns each typed slot one cell; the general slot for the same variable exists
only if the variable ever crosses a boundary. Coroutine fibers save the typed area with the
frame. The parallel workers' frames are ordinary frames, so a region inside a parallel body
runs unchanged on a worker.

## Invalidation

A compiled function records the epochs its regions assumed: the definition epoch (callee
identities and signatures), the class shape epochs of every record it touches, and the
operator-table epoch. A mismatch on entry recompiles the function on the general path and
retries; nothing runs on a stale assumption. Hot reload therefore costs one recompile per
affected function, which the `parameter_alias` machinery already does for its proofs.

## Calling convention (stage 2)

A call is typed when the checker resolves the callee to one script function whose parameter
and return types are declared scalars, and the call site's arguments are typed slots or
literals. `call_typed` writes the arguments into the callee's typed cells and enters the
frame without an environment; the callee's return lands in the caller's typed slot. Methods
qualify with `this` as a record pointer once stage 3 exists. Small proven callees inline at
compile time: the body is grafted into the caller's region the way `parallel_transform`
grafts its function body today, with the callee's slots renamed into the caller's frame.
Evaluation order and observable copies hold because the graft happens on the AST the
checker already ordered.

## Flat records (stage 3)

A class is **flat** when every field is a declared scalar or a flat class, it has no user
accessor, is not `nocopy`, has no `var` field, and no class in its chain violates these. Its
instances are one contiguous record; `array<Flat>` is one buffer of records; a proven
`p.x` is a load at a fixed offset. Everything else about the class is unchanged: methods,
inheritance among flat classes, serialization, hot reload (a shape change re-lays the record
and recompiles). A class that stops being flat on reload drops to the general
representation for its instances, which the migration path already knows how to do.

## Parity and gates

- The interpreter is unchanged and remains the oracle. The differential fuzz gate runs every
  generator program on both backends with regions on, and again with regions off
  (`engine::typed_regions(false)`, the same shape as `type_proofs(false)`).
- Every increment lands with a hash-gated GLOOM run (300 ticks, seed 666, frame
  `7fecc09815bb64e0`, state `3580805725`) and the JaiDOOM timedemo (state `1746772898`).
- Credit is an interleaved A/B on GLOOM ms/tick and the timedemo ms/tic; a micro loop is a
  development gauge only and carries the padding control (`PERF_PASS_2026-09-02.md` row 48).
- Both backends must raise the same texts for the same inputs on every typed opcode; the
  catalog tests extend to each new opcode.

## Order of work

1. Typed slots and the scalar arithmetic set, with boxing at boundaries. Measured on GLOOM's
   per-pixel loops; no new runtime data structures.
2. Typed element access on `array<int>` / `array<float>` and the loop back edge.
3. Typed calls between proven functions, then inlining of small proven callees.
4. Flat records and `array<Flat>`.
5. Throughout: reprofile after each stage; the standing profile shows `array.push` and
   indexed reads at roughly a sixth each, and those numbers move as stages land.

## What this is not

It is not a JIT. Every stage is interpreter-level: new opcodes, new slot storage, the same
run loop. Bindstone ships on iOS, where runtime code generation is not permitted, so native
code would be an ahead-of-time step and a separate decision. LuaJIT is a valid reference
point for the ceiling of the technique and is measured as such, labeled as a JIT.

## Comparison suite

The reference numbers are the GLOOM ports under bit-exact conformance hashes
(`examples/gloom/ports`). The suite carries Lua 5.4 today; it gains Lua 5.5 (array storage
and collector changed between the two) and LuaJIT, each run warmed, with the core count
stated, on the same 600-tick workload. Whole-application numbers (GLOOM, the JaiDOOM
timedemo) are reported separately from native-bound object workloads.
