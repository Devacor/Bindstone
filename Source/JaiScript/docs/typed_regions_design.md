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

## The typed scalar set (stage 1 as it stands)

The first set runs on the existing `script_value` frame slots, not on a separate raw-cell
area: the guard is one tag compare per operand, and what the instruction removes is the
proto decode, the generic operand ladders, the push and pop round trips and the second
dispatch of the fused forms. Thirty-one opcodes, `op_t_*` in `vm/opcode.hpp`:

- arithmetic: `add sub mul div mod and or xor shl shr` on int/int, `add sub mul div mod` on
  float/float; `a` is the destination slot with its kind in the top bits (temp, decl, store,
  compound), `b` the left slot, `c` the right slot or a constant index behind
  `k_typed_const_bit`;
- compare-and-jump-if-false: the six comparisons on int/int and on float/float; `a` left,
  `b` right, `c` target;
- moves: `T x = y`, `T x = lit`, `x = y;`, `x = lit;` as one instruction each.

The compiler lowers in place after a callable's body is complete
(`vm_compiler::lower_typed_ops`; the moves lower at emission, where the LOAD or CONST just
emitted is rewritten and no DECL_VAR or STORE follows). A fused binary or compare-jump lowers
when the checker's operand class on the node is int/int or float/float and both operands are
direct frame slots, compiler temps or literals (a literal on the left swaps for the
commutative and mirrored operators); a decl landing needs the decl fast path's static
admission and a declared type the class fits; a store landing needs the same-type proof, the
provable-slot flag and statement position; a compound needs a bare right operand and
statement position, int rows only; a statement-position `++x` or `x--` on a direct frame slot
is the compound row with the constant 1 (`op_t_add_ii` / `op_t_sub_ii`), its miss running the
general increment. Lowering follows the type-proofs flag, so a proofs-off run
is a typed-ops-off run and the differential fuzz gate's second pass covers both.

An index binary the checker left unproven still takes the lane when one operand is a compiler
temp holding an element read and the others are direct slots or int literals (a read through a
`var&` container): the load's runtime guards decide, a miss runs the original instruction, and the
census counts such a site's misses. A one-argument `math::` intrinsic (`itrunc`, `ifloor`, `floor`, `ceil`,
`sqrt`, `abs`, `sin`, `cos`, `tan`, the degree conversions) inside a fused binary lands its result in a
compiler temp (`op_math_temp`; the integer family on a float lands raw in place, every other case runs the
shared kernel and lands its value), so the binary reads a direct operand and takes its typed row when the
checker classed the call; the call keeps its place in the operand order like a hoisted element read. An
index expression that is an arithmetic tree computes into a temp (the fused binary the compiler emits
last lands there) and the read is the identifier-plus-temp shape, which takes the `T_LOAD` row; a
reference-slot container is guarded ahead of the index: the guard materializes it into a temp only when the
slot's reference is not a plain cell (an element, field, map or property reference, whose dereference can
raise), and the readers take the slot itself otherwise, so a getter runs once and in its place and a plain
alias pays one tag test.

A closed loop may branch inside itself: a typed compare-jump or a jump whose target lies forward inside the
loop or at its back edge is copied as a cell branch (`op_c_jcmp`, `op_c_jump`), so one-arm and two-arm
diamonds over typed work, a `continue` and a guarded store close; a branch that leaves the loop (a `break`,
an early return) keeps it open. A container guard inside the loop keeps its place in the copy (`op_c_guard`:
it clears the dirty bit of a dead temp cell sharing the guard's slot and, for a pinned table entry, restores
the site's snapshot in the slot when another table sits there, so a miss's general form reads the handle it
expects): the entry resolves the container once and refuses a reference that is neither a plain cell nor a
pinned table entry (below).

A temp that holds an element the checker could not class (a read through a `var&` container) is a cell
too: its kind is pinned by a typed consumer (an index fold, a compare) or adopted at entry from the
slot's current value, the cell load lands the element's own kind, and every consumer checks the kind
at run time as it does for any cell, so a mismatch is a miss onto the general form.

An environment scalar read by a typed-lane index store inside the loop (a global view width) is a
read-only cell: nothing in a cell body can change it, so the entry resolves it once (an int, else the
block is refused), the copy's store reads it through a synthetic slot, and the flush never writes it.

Arithmetic the typed heads left general (an operand that is an environment scalar or a value the checker
could not class) joins a block as cell arithmetic with run-time kinds (`op_c_arith_any`): the general
kernel's int rows, float rows and the int to float promotion of the four arithmetic operators, landing
by the destination cell's kind (a float declaration widens an int; any other disagreement is a miss onto
the general form). An environment cell adopts int or float from its value at entry when no typed use pins
it. Typed moves copy into cells (`op_c_mov`), `math::itrunc` and `math::ifloor` of a cell run the kernel's
conversion in the block (`op_c_math`), and an intrinsic's binary argument lands in a temp so the block can
carry it. A guarded container that is an element reference into a hetero array (a texture table's entry)
is resolved once at entry like a plain cell when no store site of the loop writes that table; a store into
the table refuses the block, since the general form re-reads the entry at every guard.

A counted loop (`for (int i = lit; i cmp end; i += step)`, the `op_cfor_prep` / `op_cfor_back` form) closes
under the same body rules with the var, the end and the step as int cells. Its copy has no head compare
(the head slot is a placeholder, so the copy stays one instruction per original) and its back edge is
`op_c_cfor_back`: the step on the var cell, the compare against the end cell or literal, then the body
again or the exit with the counted state popped; a step that overflows misses onto the general back edge,
which raises its own text. The original back edge becomes `op_cells_cfor_back`: the general step and
compare through the counted state's cached pointers, then the entry for the next iteration. A store
value with no other use in the loop is a cell of adopted kind.

### The contract

Admission is a translation, run once per chunk after the typed lowering, from the loop body's instructions
into canonical operations over admitted values, followed by a legality pass; nothing else decides. The
values: semantic locals (frame slots, identity the slot, kind pinned by a typed use or adopted from the slot
at entry), compiler values (the temps of an expression, live inside their statement, any kind lands),
environment values (globals read once at entry), container bindings (arrays resolved once at entry through a
slot, a plain reference, an environment name or a pinned table entry), snapshots (a guard's container value,
live from the guard to the read it guards) and int and float literals. The operations: `arith` (the ten
binary operators, pinned kinds as `op_c_arith`, run-time kinds as `op_c_arith_any`), `mov` (`op_c_mov`),
`cmp` with its test (`op_c_cmp` and `op_c_jcmp` for pinned kinds, `op_c_cmp_any` and `op_c_jcmp_any` for
run-time kinds; a head's false edge exits the block), `load` (`op_c_load`, through a site row the typed
lowering built or the translation builds for a general read), `store` (`op_c_index_store`), `math`
(`op_c_math`), `jump`, the back edge (`op_c_back` or `op_c_cfor_back`) and `guard` (`op_c_guard`). A
landing's conversion contract belongs to the operation: a stamped decl lands its declared kind (a float
decl widens an int), a store lands the slot's kind, a compiler value lands any kind; every other case is a
miss onto the general form, which owns every conversion and every error text.

Every copy instruction carries its origin (`cell_loop::origin`): a miss flushes the dirty values and resumes
the general form at that instruction, which re-runs whole (the encoding is one cell instruction per original
instruction; the origin table, not position arithmetic, is what the runtime consults). Legality after
translation: a container binding's slot is never written inside the body; a compiler value is defined before
it is read inside its own statement; a snapshot's slot is written only by its own guard between the guard
and its read (the guard restores the snapshot when the slot holds another table, since temps are reused per
statement); a pinned table entry is admitted only when no store site of the loop writes that table (checked
at entry). A refusal names its class and its first offending instruction: `representation` (a value the model
does not take: strings, maps, members, getters, nested typed arrays, a local of two kinds), `effect` (a call,
an exit, a binding write, a scope), `translation` (a supported core operation in an encoding the translation
does not normalize yet: a coverage gap), `malformed` (a lifetime rule failed), `head` (a loop form the model
does not take). The profile prints the closed and refused loops per function (`cell translation`), and the
disassembly lists them under each chunk (`-- cell loop`, `-- open loop`).

A comparison used as a value and the `JUMP_IF_TRUE` or `JUMP_IF_FALSE` that consumes it (the `||` and `&&`
chains, `!e`) translate to one `cmp` whose target word carries the jump's sense (never an inverted operator,
since a NaN fails both `<` and `>=`); the pair's origin is the value instruction and the jump's copy is a
placeholder no branch may target. A compare-headed loop's head is the span from the back edge's target to the
compare: the condition's temps and reads translate like the body and run every iteration before the compare,
and the compare's miss resumes at its own origin. `math` covers the pure one-argument intrinsics (`itrunc` and
`ifloor` land an int, `abs` keeps its argument's kind, `floor`, `ceil`, `sqrt`, `sin`, `cos`, `tan` and the
angle conversions land a float). `x op= y` and `++x` / `x--` in statement position on a slot the typed
lowering left general are `arith` with the compound landing, kinds at run time.

A guarded container that is a reference to a map entry (a sprite table keyed by lump) is admitted like a pinned
table entry: no cell store writes a map, so the entry holds for the block. A container site that refuses the
entry marks the loop in the call record's cell state and the entry is not attempted again in that call (the
refusal valve): a binding does not change within a call, and the guards would otherwise run at every back edge
for nothing.

A local's landing follows its declared type, which is the language's own rule: the entry records the stamp of
each cell whose kind is decided at run time (an adopted kind: the canonical int or float of an inferred `auto` or
unproven local, the `any` stamp of a `var`) and the flush restores it; a pinned cell needs none, its slot carries
the canonical stamp and its landing is the declared kind. An int slot lands ints (a float result misses onto the
checked conversion), a float slot lands
floats and widens ints, by a declaration or a store alike, and an `any` slot lands whatever kind the operation
produced. So `var` and `auto` locals are cells of adopted kind, judged at run time only where the language
itself decides at run time; typed locals keep the pinned forms.

An element read lands in place when its destination is a pinned-kind local or a compiler value (`op_c_load`:
the site's expected kind is the cell's kind by construction) and through the declared-type rule when the
destination's kind is adopted at run time (`op_c_load_any`), the same split the arithmetic has between its
pinned and its any-kind form.

Every typed instruction keeps the instruction it replaced in `chunk::typed_sites` at its
own ip (two for a move). The fast path lands exactly as the general op lands: a payload
write into a same-tagged slot, otherwise a fresh value carrying the decl's stamp; a store as
the proven-identity slot store; a compound in place. Any guard miss (a slot boxed behind a
reference, a var slot, an overflow, a zero divisor, a shift out of range) runs the original
instruction, so error texts, evaluation order and post-states are the general kernel's by
construction and the interpreter stays the oracle unchanged.

What this set does not yet do, in the order the rest of this document gives it: raw cells
(the slot is still 32 bytes with a tag), regions with boundary boxing, typed element
instructions in the same encoding (element reads and stores run through the proven lanes of
the fused index ops), typed calls, flat records.

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

## Closed typed loops run as cell blocks

A loop whose head is a typed compare-jump to the exit and whose body holds only typed arithmetic,
`T_LOAD` into a stamped scalar declaration and no-result typed-lane index stores (the store's index
chain included), with every scalar slot's kind agreed across its uses, gets a cell-form copy of its
body appended to the chunk (`op_c_cmp`, `op_c_arith`, `op_c_load`, `op_c_index_store`, `op_c_back`;
operands are cell indices). The loop's original back edge becomes `op_cells_back`: after one
ordinary iteration it polls the budget, checks every cell's slot (the cell's kind, the engine's
canonical stamp, the engine), resolves each load and store site's container once (a site whose
container is not a plain array node keeps the loop on the general form), unboxes the live-in slots
into the call record's raw area (an 8-byte payload and a kind byte per cell, floats bit-preserved),
and runs the copy as one block, each cell instruction through its own handler with no dispatch
between them.

The cell compare exits by flushing every dirty cell through the landing that defined it (the
declaration's stamp, the canonical stamp, a compound's payload in place) and continuing after the
loop. Anything a cell instruction cannot do exactly (a kind byte that does not match, overflow, a
shift out of range, a division by zero, bounds, an element of another kind, a `var` element of
another kind) flushes first and hands the general dispatch the original instruction's ip, which runs
it with its fallback; the next back edge re-enters through the guards. A budget stop at the cell back
edge flushes before raising. Cells run only on window-backed record frames; fiber frames take the
general form; a debugger armed or a registered numeric operator takes the general back edge. The
interpreter is untouched, so texts and post-states are the general form's by construction.

Controls: suites `Closed Typed Loops` and `Cell Loops` (interpreter parity on every shape). Profile
builds count entries, refusals, cell iterations, unboxes, exits, misses by step, cell instructions,
flushes in place and replaced, and the resident raw area. Measured on GLOOM's render column loop
(21% of the tick's dispatches, zero misses): see `PERF_PASS_2026-09-02.md` rows 92 to 98.

Element reads inside expressions take the same row: a subscript expression the fused index op can read
(an identifier container with an identifier or literal index, a member-base container, or a computed index
chain) lands in a compiler temp through the declaration-landing op with a temp flag, and the consumer takes
the temp as a direct slot operand: a binary's operand (flat or lifted), the value of an element store, the
index of an outer read. The typed lowering gives the read the `T_LOAD` row, landing the proven kind when
the checker proved the binary and the runtime kind otherwise, in place when the temp already holds it; a
non-scalar element lands boxed in the temp; the read happens before its partner only when the partner
cannot raise first (a slot, a literal, a temp, another read), so the interpreter's error order stands.

A reference alias of a declared typed container (`var& pix = PIX;` with `PIX` an `array<int>`) carries the
container's type for element reads: the checker keeps it a reference for diagnostics and marks the element
kind a proof source, and a subscript node carries that kind in its operand class, so the compiler's temps land
the proven kind for store values, nested indexes and binaries the checker could not class on their own. The parser's typed-store classification (`assignment_expr::typed_store_provable`, guarded at runtime
like every proof) knows program-level `array<int>` and `array<float>` declarations by symbol and reference
aliases of typed containers, so `acc = acc + G[idx]` and `acc = acc + pix[i]` are parse-proven typed stores and
such loops close as cell blocks. A typed value parameter classifies the same way (`vfx = vfx + dvfx` with both
declared `int` is a parse-proven typed store), so a loop over a function's own parameters closes too. An index expression that itself contains an element read
only a temp can carry (`palcm[pcm + tp[base + iv]]`) lands that read in a temp first, left before right, when its
partner cannot raise ahead of it (a slot, a literal or another such read); the outer read then fuses with direct
operands and takes the `T_LOAD` row when the index binary's class is proven. A hoisted read keeps the
interpreter's evaluation order: an operand the interpreter evaluates before the read whose dereference may run a
getter (a frame slot naming a reference declaration) is materialized into a temp first, so the getter runs once, in
its place, and the consumer reads the temp; a reference bound to a value the unit declares (a local, a parameter, a
program-level value declaration, an alias of one) dereferences a cell and needs no snapshot, so such loops still
close as cell blocks; inside a class body a program-level name does not count, since a member or a native property
of the same name resolves first there. A reference declaration or a reference return that binds a bare name resolves the
frame's own member before a global of the same name on both backends (locals, then members, then globals). An index store evaluates its right-hand side before it dereferences the target on both
backends, so the store-value hoist is already in that order. A reference declaration into a frame slot
(`var& pix = PIX;`) keeps its function on the lazy-environment path: the reference lands in the alias's slot and the
referent resolves slot-first, then through the chain the frame already runs in, so the body needs no per-call scope
environment and its env-resident reads keep the two-compare fast head across calls.
