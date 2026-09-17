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
slot, a plain reference, an environment name, a pinned table entry or a plain field of a frame-slot instance, read once at
entry and again at every landing after a call), pushes (`recv.push(v)` onto a frame-slot array: the receiver a container
site, the value a cell, a constant or an element of a second container read as a handle at the push, the append the push
kernel's rows; a declined kind, a refused charge or a typed hetero receiver, or a source element that is a property or element reference (its getter belongs to the general push, which runs after the flush) misses onto the general push), and inner while
loops (an inner loop's back edge is a backward jump inside the outer block that polls the execution budget; its head compare
is a body compare, its exit a forward edge; the inner loop's own block stays for the general form; an inner counted loop's
`CFOR_PREP` is its head compare with the exit as the false edge, its `CFOR_BACK` the step on the var and, in the place of the
first generic update instruction, the compare back to the body, the generic cond and update code, the loop back and the pop
dead in the block, and a miss inside it pushes the general form's counted state first, `cells_miss`), snapshots (a guard's container value,
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
at entry); a table several sites reach is pinned once, and a block pins up to eight distinct tables. An instruction of the `translation` class inside a conditional span of the body (reached only through a forward
branch before it whose target lies after it) is a `bail` rather than a refusal of the loop: a copy instruction whose
execution is a miss at its own origin with nothing observed first, standing for the rest of its arm (the copy resumes at
the nearest target after the bail of a forward branch before it, since nothing between the two is reachable inside the block,
so none of the arm's later locals leaves a cell for the entry guard), so the loop closes and the arm's frequency decides
the cost (the miss-site table names the site); the head span, the back edge, the main path and the representation and
effect classes refuse as before. A refusal names its class and its first offending instruction: `representation` (a value the model
does not take: strings, maps, members, getters, nested typed arrays, a local of two kinds), `effect` (a call,
an exit, a binding write, a scope), `translation` (a supported core operation in an encoding the translation
does not normalize yet: a coverage gap), `malformed` (a lifetime rule failed), `head` (a loop form the model
does not take). The profile prints the closed and refused loops per function (`cell translation`), and the
disassembly lists them under each chunk (`-- cell loop`, `-- open loop`).

An element alias declared in the body (`var& strip = STRIPS[sidx]`, an element reference bound by `DECL_REF_VALUE`) is a
`rebind`: the alias's sites (the loads and stores whose container is the alias slot, after it) take the source table's
element as their container each time the declaration runs, and the entry resolves and pins the source table instead of
the alias (a store site into the table refuses the entry, as any pinned table does). The alias slot is a compiler value of
kind reference whose payload is the element index; the block never writes the slot, and a flush where the alias is live
binds the element reference the declaration would have bound, so the general form resumes with it. A landing after a call
names the element again for every alias live there. The element must be an array node that is plain, as the entry has it;
anything else misses at the declaration, whose general form raises or binds as it does. One alias per slot in a body, its
source never an alias, and a push receiver never an alias. An alias is a local the general continuation may read anywhere
after its declaration (a return inside the body, a break, a bail, a miss), never a statement's value: it is live from its
rebind to the end of the body, so every flush after the rebind materializes it. An inner counted loop's step is preceded
by the budget poll, as an inner while's back edge is: on exhaustion the block misses onto the step's own origin, the general
back edge, which raises the budget text ahead of its own step.

A range-for over an array (`for (auto& p : pool)`, `for (int v : a)`) is a block head: its back edge is the jump to `ITER_NEXT`,
the head (`range`) compares an index cell against the container's live size and leaves for the `ITER_POP` on exhaustion, and the
head's landing binds the element into the loop variable, by value as a load of the element's own kind (a use that pins the
variable's cell makes another kind miss), by reference as a rebind of the element. The index cell is the iteration state's
index at entry, stepped by the block's back edge (`C_BACK_STEP`) and written back to the state by a miss (the next element's
index) and at the exit; the state's container is the head's site, resolved at entry and holding through calls (no callee
rebinds it; the body may grow the array, as the general form reads its size every iteration). An element the cells do not
take (a string by value, an instance by reference) misses at the landing once and refuses the block for the rest of the call.

A member read whose base is a frame-slot identifier (`p.x`) lands in a compiler temp in the general form (`MEMBER_TEMP`:
the base pushed as its `LOAD` would have, `GET_MEMBER`'s kernel with its cache, proofs and texts, the value popped into the
temp), emitted where the operand is evaluated, so it is a direct operand of the fused binaries, the compare-jumps, the call
arguments and the assignment values, and the typed lowering takes it when the checker proved the pair; a member store on such
a base reads the base from the slot (`SET_MEMBER_FUSED`, `SET_MEMBER` whole otherwise). Static members, null-safe accesses,
environment bases and `this` keep the general shape. Inside a loop these instructions are a translation gap until the block
takes member sites.

A block's push of a cell or a typed element into a typed buffer moves the raw payload (an int, a float, a string handle)
without an intermediate value, under the general push's guards in their order, its conversion subset (an int widens into a
float buffer; every other mismatch misses onto the general push and its text), its memory charge and its fallback; a hetero
destination still takes a value, a cell's boxed with its declared stamp as the general push has it. The VMPROF report counts
the direct pushes by kind and the value pushes (`cell pushes`).

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
table entry while its key is present: no cell store writes a map, so the entry holds for the block. Admission is
non-observing: a missing or dead binding refuses the block, it never raises, and the general form raises only
where the program reaches the read (a key erased since the reference was taken is such a binding). A container
site that refuses the
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

A compiler value is a definition, not a slot. The compiler reuses a temp slot across statements, so one slot may
carry an int element read in one statement and a float subtraction in the next; the translation opens a new cell
for each temp definition whose kind differs from the slot's current one (a local keeps one cell: two kinds on a
declared local are the general form's conversion), reads bind to the latest definition in program order, and the
copy names cells directly, in its instruction words, in its own site rows at the copy instruction's index and in
its cloned store protos. A definition is live from the instruction after it through its last read; the flush
writes a slot only from a live definition, so a dead value never overwrites the one the general continuation
reads.

The move rows (`op_t_store_mov`, `op_t_store_const`, `op_t_decl_mov`, `op_t_decl_const`) are the general form
of a statement-position move from a slot or a literal, and `x = a[i];` lands its read in a compiler temp and
moves it with the same row. Their guard is total: the same-type payload test on the live slot lands in place,
anything else replays the exact general pair from the typed site, so a proof adds nothing to their soundness and
they are emitted with proofs on or off. The proofs flag gates the arithmetic and compare rows only.

A unary minus, complement or not over an operand is a definition into a compiler value (`op_unary_temp`, the shape
of `op_math_temp`): the lift, the flat pre-step, a math argument, a declaration, a statement-position store and an
element store take it as an operand, the general unary runs verbatim underneath, and the cell copy lands neg and
bnot (`op_c_neg`, `op_c_bnot`) with the checked negation's overflow as a miss.

A pure math call as a declaration's, a store's or an element store's value lands in a compiler temp (the
math row) and moves, as a unary does; the nested index `a[b[i]]` takes an environment identifier as its
container (the name resolves inside the op, as a lifted binary's environment operand does); and the translation
takes a truth test on a local, as a loop head (`while (n)`) or inside a body (`if (x)`), as a compare against
zero with the jump's sense, the language's truthiness for an int or a float (a slot of another kind refuses at
the entry).

A negated numeric literal is a literal at parse time (`-1`, `-1.5`), so every consumer sees a constant, and a
counted loop whose bound or step is an environment scalar enters the block (the general form reads the global
through a pointer at every back edge and no cell instruction writes a global, so the environment cell read once
at entry holds the same value).

A call inside a body is one operation of the block (the design note's section 9). Its arguments are values: a cell
boxed at the call, a constant or an environment name pushed as the general instruction pushes it, or a computed
argument (a fused binary, an element read, a math call or a unary over a slot) landing in a compiler value of its
own ahead of the call. The callee probe, the pushes and the call run the general executors verbatim over three
copy instructions at the call's place, the callee runs in the dispatcher with the caller parked on the call's copy,
and the landing after the return lands the result by the declared-type rule into a scalar declaration or a
no-result store, or drops a statement's. The call is the effect point: a slot argument's cell is flushed ahead of
the call so a by-reference parameter binds the current value and its kind is checked after it (a local that
escaped is the slot's from then on), the environment values are re-read, the container sites re-resolved, and any
change misses at the consumer with the result on the stack; a callee that raises into the caller's handler flushes
the block first. The copy is not one instruction per original: an argument the call boxes and a jump a compare
carries have no copy instruction, a call has four, and branch targets and guard offsets map through the copy
positions.

An environment scalar a body writes (a fused store, a compound, an increment, a move or a call result landing on a
global, or on the member a bare name resolves to; `::name` stays general) is the same environment cell its reads
share, written by the declared-type rule against the stamp read at entry (an int global takes ints, a float global
widens, a var lands any kind); the flush writes the payload back through the resolution the reads use, in place
when the storage still holds that kind and stamp, and a call, which may read or rewrite the environment, has the
written cells flushed ahead of its probe and re-read after its return.

A bool is a cell kind beside int and float (payload 0 or 1, the canonical bool stamp): a bool local, environment value
or compiler value enters, lands and flushes like the numeric kinds, and a bool cell takes bools only (an int
declaration fed a bool, or a bool fed an int, is the general conversion). A truth test is an operation of its own
(the `!` token on the compare ops): the language's truthiness of an int, a float or a bool, on a local or an
environment value, as a head (`while (RUNNING)`) or in a body (`if (solid)`, `if (!x)` through the jump's sense).
`!x` as a value inverts that truthiness into a bool, and a comparison as a value (`bool lt = i < 2`, a comparison
argument) runs the compare kernel and lands a bool. A general move the typed rows left alone (a bool constant
declared or stored, a bool copied from a slot) is one move by the declared-type rule.

An element store's value binary is a value like a unary or a math call there: `a[i] = e op f` lands the binary in a
compiler temp (its fused binary rewritten to land there) and the store takes the temp as a direct operand, so the
typed store lane's shape holds and the block reads the value as a cell.

A block holds up to 128 cells: the dirty set, the compiler-value set, the environment-value set and the live sets
are two-word masks (`cell_mask`), and a loop with more cells stays on the general form (`too many cells`).

A local the body declares enters empty: its cell needs no entry kind and no unbox (its slot may hold a placeholder
until the declaration runs, in a branch the general form has not taken), its declaration is the first write and
lands by the declared kind (an int declaration takes ints and truncates a float in range as the declaration's kernel does,
a float declaration widens an int, a var or auto declaration takes any kind), the declaration's type stays the cell's
contract on every later iteration of the block (a landing on the defined cell judges by it where the slot has no stamp),
and a read of an empty cell misses like any other kind mismatch.

An exit ends a block: a `return` statement or a `break` that leaves the loop ends it at the statement's first
instruction, the cells flush and the general form runs the statement whole (its pushes, region exits, scope pops
and the return or jump), so any expression may appear in it; a compare whose taken edge leaves the loop lands on
an exit appended after the copy's back edge (`C_EXIT`, its operand the general ip).

The translation verifies a region over its control flow graph before emitting it: an operation's successors are
the next operation, a compare's target (or the exit), a jump's target, nothing after an exit, and the back edge
re-enters at the first operation; a slot the block enters with undefined (a compiler value, a local the body
declares) must be defined on every path to each of its reads (the must-define dataflow), so a definition in an
untaken branch does not reach a read after the join; a region that fails is refused as malformed. The listing
prints each closed region's operations with their targets, writes and reads (`-- ir`).

A pinned cell (a declared int, float or bool) holds its kind for the whole block, since every write lands that kind or
misses and the verifier proves no read ahead of a first declaration; an arithmetic or compare whose operand cells are
pinned to its kind (constants of that kind included) is emitted in its unchecked form (`C_ARITH_P`, `C_CMP_P`,
`C_JCMP_P`), the same handler with the per-read kind checks folded away; an adopted kind (a `var`, an unproven slot)
keeps the checked form.

`engine::ir_passes(bool)` is the switch over every proof-based elision and pass of the region compiler (the pinned
forms first); off, a region runs its checked canonical form, observationally identical, and `jaiscript_fuzz
--no-passes` is the differential gate, as `--no-proofs` is for the typed lowering.

A string is a cell kind: the cell borrows the string's node (the slot, the constant, the environment binding or the
container keeps ownership), a string element loads into a statement temp, a string environment value is a cell, and
`C_APPEND`, the compound plus with run-time kinds, appends in place when the destination's slot still holds the
borrowed node uniquely and the memory cap accepts the bytes (any other case misses onto the general form, whose
unshare, char-as-text and mismatch texts stand; numeric kinds are the any-kind arithmetic); a string cell is never
flushed, and a slot-backed string cell is re-borrowed after every call. String moves, declarations from a string
value, compares and concatenations into a new string keep the general form.

A landing after a call re-validates in proportion to what the call could have changed: the copy's sites are a list,
the entry records how each site's container is reached (its frame slot, or its environment binding) and each
environment cell's binding with the environment and its epoch, and a landing whose environment and epoch still
hold re-reads the environment cells through their bindings and checks each site's container by identity (the
same array node behind the slot or binding, a plain-cell reference dereferenced); any difference resolves the sites
in full again and misses only if that fails.

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

A cell's kind is the contract of the copy's operations (a pinned operation reads the payload as that kind without a check).
A live-in cell's slot proves it at the entry; a local the body declares proves it at its landings: a typed declaration lands
by its own kind's conversion, a `var` or `auto` declaration takes the kind its value has (a constant seeds the cell's kind at
admission, so a typed use of another kind refuses the block), and a landing of any other kind into a cell with a fixed kind is a
miss, never a conversion the language does not make.

A build with `JAISCRIPT_MISS_INJECTION` defined carries a testing lever, `engine::cells_miss_every(n)`: every nth cell
instruction whose own miss re-runs its original instruction (a compare, an arithmetic, a load, a store, a move, a math
call, an append, a push) misses before it runs, as a failed guard would; the back edges, an iteration head's landing (a
miss there refuses the block for the call), the call door, a bail, an exit and a rebind keep their own protocols. So the
flush, the liveness of temps and aliases, an inner counted loop's state and the continuation on the general form are
exercised at every such position against the interpreter (`jaiscript_fuzz --miss-every n`, the runner's
`--miss-every=<n>` and GLOOM's `--miss-every N` under the hash tripwires, suite `Cell Loops`). A build without the
define never reads the knob and throws on a nonzero setting, so a gate cannot run without the misses it asked for;
`engine::miss_injection_available()` says which build this is.

Element reads inside expressions take the same row: a subscript expression the fused index op can read
(an identifier container with an identifier or literal index, a member-base container, or a computed index
chain) lands in a compiler temp through the declaration-landing op with a temp flag, and the consumer takes
the temp as a direct slot operand: a binary's operand (flat or lifted), the value of an element store, the
index of an outer read. The typed lowering gives the read the `T_LOAD` row, landing the proven kind when
the checker proved the binary and the runtime kind otherwise, in place when the temp already holds it; a
non-scalar element lands boxed in the temp; the read happens before its partner only when the partner
cannot raise first (a slot, a literal, a temp, another read), so the interpreter's error order stands.
A call the expression consumes as an operand takes the same row when its callee is a plain name and its partner is a
literal or a private local (a slot no user code can rebind, so running the call ahead of the partner's read is
unobservable): the call, then `op_call_temp` lands the result in a compiler temp, and the binary or the compare reads
the temp as a slot; inside a block the door's third consumer lands the result in a temp cell. An environment partner
keeps the stack form, since the callee may write the global the partner reads.

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

### Straight-line spans

A function body's instructions outside every loop form spans: the maximal runs cut at the targets of jumps from before the
run (its own arms' joins stay inside), at the machinery around a loop and before an implicit return. A span of at
least `chunk::k_span_min_instructions` instructions is admitted by the loop body's walk (the same operations and refusals,
the bails inside arms, the calls through the door, a return as an exit) and, when it closes, gets a cell-form copy like a
loop's: its entry (`op_cells_span`) takes the place of its first instruction and runs the guards, the site resolution and
the unboxing a back edge runs (no budget poll: a span has no back edge, the calls and loops around it poll), the copy ends
in an exit that flushes and continues after the span, and a miss re-runs the original instruction as in a loop. The first
instruction lives on behind the copy, followed by a jump to the second (the trampoline): the general form an entry refusal
A cell instruction that carries an original ip in its own word (a load's, a push's, an index store's or a rebind's origin, a bail's
target, a container guard's snapshot instruction) names the trampoline when that ip was the span's first.
runs, and the miss path of every copy instruction standing for the first. A span the walk refuses splits at the refused
statement's boundaries (never inside a statement: a temp defined in one half and consumed on the general form would be dead at
the half's exit); the statement's extent closes over the jumps inside it (an if's arms and join, the enclosing statement of a nested
refusal), so a half begins where the common path lands; each half is admitted on its own. A run or a half that begins with a return
statement keeps the general form (its block would enter and exit at once), and an exit whose statement begins at a span's head runs
the general form from the trampoline, as every instruction-carried ip does. A span's exit into a return statement (`return x;`, `return k;`) flushes
the environment cells and x's cell only, the frame dying at the return; a function with a try block keeps the full flush. A span runs once per call, so its entry is paid per call; the minimum
length keeps the general form where a copy would not pay for it. An environment operand's lookup row in a block is the general
instruction's row for that operand (a read's container at 0, or 2 behind a binary index; a simple index at 1; a binary index's sides
at 0 and 1; a store's value at 2): the rows are keyed by environment and epoch, never by name, so two names on one row would answer
each other; a store whose index is a binary and whose value is an environment scalar has no free row and keeps the general form. The listing names a span's copy and trampoline, and a
refused span as an open span; the profile counts span entries and their refusals.
### Fused cell pairs

The block loop runs an adjacent pair of plain cell instructions as one dispatch when the pair is one of the hot shapes
(`C_ARITH_P C_ARITH_P`, `C_JCMP_P C_JCMP_P`, `C_LOAD C_LOAD`, `C_ARITH_P C_LOAD`, `C_LOAD C_JCMP_P`, `C_ARITH_P C_JUMP`): after a
loop's copy is complete, a peephole gives the first instruction of such a pair the fused opcode and leaves its word and the second
instruction untouched; the fused executor runs the first instruction's own executor, and when the block is still live and the ip fell
through to the next instruction, the second's on the second's own word. A jump into the second instruction dispatches it alone, a miss
inside either half reads its own origin, its own site row and its own liveness, the injection lever counts both halves, and the middle
end's switch (`engine::ir_passes`) off leaves every pair two instructions. The site logic asks `cell_first_op` what a fused instruction
was. The fused executors are out of line (the block switch's shape rule).

### Member read sites

A member read of a frame-slot instance that lands in a compiler temp (`p.x`, `op_member_temp`) is a load site of the block:
the entry resolves the base slot's instance and its plain field once through the read-side probe (the class, the field slot,
no user accessor in the chain) and keeps the field's value for the copy, and `op_c_mload` lands the field's kind and payload
in the temp cell at every read. A base that is not such an instance, a field that is not an int, a float or a bool, refuses
the entry, and the general form runs; the hold check after a call resolves the field again and misses on a change (a callee
that rebound the slot's instance or migrated its fields). Nothing is deferred: the site reads the instance's field storage as an
element site reads an array's, so the miss protocol needs no flush of members. A base that is an element alias (`for (auto& p
: pool)`, `var& e = list[i]`) makes the site the alias's dependent: each rebind resolves the field from the element instance (a
hetero table's object, a class-typed buffer's slot) through the probe, with the slot cached per site by class (a subclass element
re-probes), and the hold check after a call re-applies the rebinds with a re-probe. A member store in statement position
(`p.x = v;`, `op_set_member_value`: the value an operand, no push and no result on the general form) is a store site resolved
the same way; `op_c_mstore` writes the field's payload in place when the value's kind is the field's kind and the field carries
that kind's plain stamp, and misses onto the general store otherwise (its conversions, its accessors, its texts). Environment-name
bases (`G.player`) follow in their row. An element alias passed as a call argument passes its element reference: the door
flushes the alias (the reference its declaration binds, built into the slot) and pushes the slot's value as the general LOAD does,
so a loop whose body calls a function on each element (`for (auto& p : pool) { step(p); }`) closes and lands without a miss.

### The region IR module

The canonical operations of a closed loop and the analyses over them live in `include/jaiscript/vm/region_ir.hpp`
and `source/implementation/vm/region_ir.cpp`: the operation set (`ir`, `ir_op`), `region_ir` (one loop's operations
with the span of the general form they stand for; an operation's index is its id, `first + k` its original ip),
`control_flow` (successors and predecessors over the operations, the back node and the exit node), `verify` (the
must-define dataflow over that graph for every slot the block enters with undefined, the snapshot spans, a container
binding never written; the first offence in operation order is the verdict), `listing` (the text the listing tool
prints per region) and `live_temps` (the liveness of compiler values per copy instruction, the flush's map).
`vm_compiler::lower_typed_ops` translates a loop into a `region_ir`, asks the module for the verdict, the listing and
the liveness, and emits the cell copy; every pass over regions consults the module and nothing else.

The listing tool: `engine::disassembly_sink` hands every chunk the VM compiles (a program, a function at its define, a
closure body, a standalone expression or declaration) to a sink as it is compiled, off by default; the runner's
`--disasm=<file>` writes the readable listings (`vm/disassembler.hpp`, the regions' operations included) and
`--manifest=<file>` the semantic manifests (`vm/chunk_manifest.hpp`: every instruction's fields, every constant's
exact bits, the symbols by spelling, every proto table, the typed sites, each cell loop's cells, origins, live masks,
sites, calls and refusals, every nested chunk; no runtime cache or counter). A compiler change that claims unchanged
output proves it by identical manifests over a corpus; a listing alone cannot tell two constants or two live masks
apart.
