# Compile-time and JIT-technique research (2026-10-23)

A census of the work the VM still does at run time that the parser, the checker, an AST pass or the VM compiler could do
before a `.jaibite` is written, plus run-time techniques borrowed from JITs, ranked in one table. This note is research:
nothing here has landed.

Evidence is tagged: **[measured]** from a RIP-sampler profile taken for this note, **[census]** from the copy-and-compile
and fast-path censuses (counting builds, master of early October), **[read]** from the code, **[inferred]** an estimate.

## Where the time goes

Profiles: `/O2 /Ob2 /Z7`, no LTCG, master `3f2f97007`, one run each, sampler at its default rate. Shares are of all samples
in the process, so a share is a ceiling on what removing that work could give. [measured]

| Workload | Top costs |
|---|---|
| OSSUARY (scene 15, 100x40, vm, 1 worker) | `cells_run_frame` 42%, `run_dispatch` 6.6%, sin/cos/remainder 8.4%, cell entry, writeback, site restore and re-entry 7.8%, typed general ops ~5% |
| fib_recursive (typed) | `cells_run_frame` 34%, `raw_call_enter` 24%, `exec_c_ret` 19%, `cell_land` 8.5%, `raw_follow` 7.8%: the raw call protocol is ~60% |
| tree (typed) | `run_dispatch` 16%; member temps, parameter binding, frame push and pop ~20%; reference holders (mint, pool acquire, `field_cell`, `deref_slow`, `reference_binding_admits`) ~14%; `script_value` copy and destroy ~11% |
| particles (typed) | `cells_rebind_apply` 58%, almost all on lines 7941-7974: the dependent loads from an `obj` element to its `class_instance`, to a prefix field's holder, to that holder's `class_definition` and its method epoch. These are cache misses, one chain per element |
| nbodies (typed) | `cells_run_frame` 29%, `cells_rebind_apply` 16%, member cell loads and stores 17%, entry and writeback 9%, the name string compare in the alias admission (`std::operator==`, `memcmp`) ~2% |
| dict (typed) | map find, entry pool and `short_of` 41%; `script_array::get` boxing 6% |
| queen (typed) | `cells_run_frame` 24%; block entry, writeback, site restore, re-entry and reland ~13%; `cfor_prep` 6%; the frame push and pop ~7% |

The censuses give the rest. [census]

- 87-92% of the games' operations already run in cells.
- Copies, refcounts and lookups are 2-5% of the games' run time and 1-10% of the kernels'.

Three costs remain:

1. **Dispatch inside the cell interpreter.** It is the top line wherever code is typed: `cells_run_frame` is 24-42%.
2. **The cell machinery's entry and exit.** Guards, unboxing, writeback, site restore and re-entry cost 8-13% on OSSUARY,
   nbodies and queen.
3. **Object and call structure.** Reference holders, frame build, parameter binding and per-element instance chains dominate
   tree, particles and fib_recursive. These are the kernels furthest from Lua: tree 8.4x, particles 5.8x, interop 4.5x,
   dict 4.3x and queen 4.0x in `PERFORMANCE.md` (2026-10-07).

Most of the cheap compile-time work is already done:

- typed proofs, typed regions, cell blocks and raw frames;
- grafts, which inline calls (`vm_inline.cpp`);
- hoisting, index folds and literal folding in the parser;
- scoped `const` folds, which self-patch at their first run (`op_const_folded`);
- enum entries cached per block;
- site plans for calls;
- sibling-method caches and host-method lanes.

What is left needs analysis that does not exist yet (interprocedural facts, layout facts, a mid-level optimizer over the
region IR), or it needs native code.

## Ranked table

Rank is expected whole-program value per unit of effort and risk, judged across JaiDOOM, OSSUARY, Gator Quest and the
kernels. "CT" means compile time and "RT" a run-time JIT technique.

| # | Item | Removes [share] | Analysis needed | Stage | Hot reload / invalidation | Effort | Risk | Exists today | Rulings touched |
|---|---|---|---|---|---|---|---|---|---|
| 1 | **RT: copy-and-patch native code for cell regions** (stitch precompiled stencils per cell op) | the dispatch inside `cells_run_frame`: 24-42% where code is typed; ~2-4x on those spans [inferred] | none new: the cell regions are already typed register code with known slots, kinds and miss points | compiler emits cells as today, a stencil stitcher turns them into native code when a region is admitted | code dies with its chunk; reload drops it the way it drops the cells; a miss exits to the existing flush | L | M-H (platforms, W^X, debugging) | cells and the region IR; no native code | the iOS no-JIT path must stay (the cell interpreter is that path); no speculation beyond the cells' own guards |
| 2 | **CT: whole-frame raw form with loops and boundary operations** ("Typed locals fast everywhere", not built) | the entry and exit of every block: 8-13% (OSSUARY, nbodies, queen); per-loop guard and unbox | slot sets per instruction (the census already computes them); per-operation miss | compiler | none new: the frame program's plan and epoch | M-L | M | frame programs for loop-free bodies; boundaries for cell blocks | none |
| 3 | **CT: interprocedural call facts** (proven callee, arity, scalar or class result, no-throw, no-escape) to thin the raw and general call protocol | fib_recursive ~60% call protocol; tree ~20% frame push and pop plus binding; DOOM and GQ call floor | call graph per module over `proven_callee_decl`; a summary per function (result type, parameter modes, whether it can raise, whether it touches globals or references) | checker plus compiler | summaries keyed by the function's definition epoch; an import or reload re-runs the summaries of the module and its dependents (check-then-apply is already engine-wide) | M | M | `proven_callee_decl`, frame programs, site plans | none (proofs, not guesses) |
| 4 | **CT: borrowed field and element access in place of minted reference holders** (escape analysis for `&` parameters and `var&` locals) | tree ~14% holder traffic plus part of the 11% copies; GQ and DOOM element references 0.2-0.5% [census #9, #10] | per function, whether a reference parameter or local escapes (stored, captured, returned) and whether its referent can be invalidated before its last use | checker | intraprocedural; callee summaries from #3 for argument positions | M | M (aliasing correctness) | read-only proofs for by-value parameters; `var&` cells | C++ semantics: a reference stays a reference, only its representation changes |
| 5 | **CT: layout facts for typed arrays of classes** (element class pinned per typed array; field slots, and the class of a prefix field, resolved at compile time and guarded by one shape epoch) | particles 58%, nbodies 16% `cells_rebind_apply` (the per-element class and epoch loads); per-element admission string compares | class hierarchy closed per module: is `Particle` final in this program, and is `pos`'s declared class final? `field_shape_epoch` already exists | checker plus compiler | the class epoch guards the plan; `extend()` and re-classing in place move the epoch | M | M | per-class memo in `cells_rebind_apply`; field proofs | Flat-classes ruling: this is the cheap half; flat storage removes the misses themselves |
| 6 | **CT: optimizer passes over the region IR** (constant propagation through locals, CSE of loads, loop-invariant code motion of guards, member loads and bounds checks, induction-variable bounds proofs) | part of #1's dispatch: fewer cell ops per iteration; bounds and overflow checks in `cells_run_frame` [inferred 5-15% on typed loops] | dataflow over `region_ir` (the CFG and liveness exist); range analysis for induction variables | compiler (region IR) | per chunk; nothing beyond today's recompile | M | M (a miss must still name the right op) | CFG, `verify`, live temps; parser literal folding only | overflow: checked by contract, so a range proof removes the check, never the semantics |
| 7 | **CT: math intrinsics with known domain** (sin and cos from the cells straight to the CRT entry, no general row; fused sin/cos pairs) | OSSUARY 8.4% sin, cos and remainder (mostly irreducible); the `op_math_temp` kernel miss [census fast-path #2] | none | compiler | none | S | L | cell math rows | none |
| 8 | **RT: quickening generalized** (an op rewrites itself after observing its operands: the `op_const_folded` mechanism extended to member reads, global reads in methods, typed-slot verdicts) | the general form's 8-13% of operations; DOOM's 2.5M global reference resolutions; OSS's 394k member-shadowing probes [census #5, #6] | none: it is observation; where the checker can prove the same thing, #3 and #5 should prove it instead | VM | rewritten ops revert on the definition epoch (as fold sites do now) | S-M per op | L-M | `op_const_folded`, ICs, site plans | **No speculative var types**: rewriting on an observed kind of a `var` is speculation; restrict quickening to resolution (which binding, which slot), never to kind |
| 9 | **RT: superinstructions from the ops that most often run in sequence** (offline, from the coverage census) | dispatch in the general form and in cells [inferred 2-5%] | an offline census of op pairs and sequences | compiler | none | S-M | L | many hand-picked fused ops already | PGO ruling: an offline census used to pick opcodes at development time is design input, not PGO of the binary; flag for a ruling |
| 10 | **CT: construction plans** (field initializers, a constructor's bind and `_cpp_object` intern resolved per class at compile time) | particles construction (`allocate_instance`, `begin_construction`, the double argument copy) ~5%; tree 40k interns [census #7, #13] | per class: default-field template, constructor parameter modes | compiler plus class registry | keyed by the class definition epoch | S-M | L | instance plans (lever A, partly) | none |
| 11 | **CT: string building pre-sized** (`a + b + c` chains and `join` sized at compile time; typed `str` lane for `join`) | GLOOM 2-4% [census #2]; DOOM detaches of empty strings | the concat chain's shape | parser or compiler | none | S | L | `compound_append_string` | none |
| 12 | **CT: proven error-free regions drop the `checked_result` wrapping** | most of the general form's propagation [inferred <2%: cells raise only on the general form already] | per function: no op that can raise (needs #3's summaries plus #6's range proofs) | checker | summary epochs as #3 | M | M | cells and raw frames already bypass it | checked_result ruling: it stays; this only starves it |
| 13 | **RT: baseline template JIT per hot function** | the general form plus call overhead | none new | VM | code invalidated by chunk death | XL | H | none | as #1, larger; #1 covers most of the same win on the hot spans |
| 14 | **RT: tracing compilation with guards and deoptimization; OSR** | loops in untyped (`var`) code | type feedback, deopt maps | VM | deopt on epoch moves | XL | H | cells enter at back edges already, which is a form of OSR | **No speculative var types** (a trace speculates on kind) and the **PGO ruling** (type feedback is dynamic profiling: allowed only if nearly free and incremental); both need Dev |
| 15 | **RT: polymorphic inline caches** (member, method, operator, builtin) | small: monomorphic ICs and plans hit 99%+ in the games [census] | none | VM | keyed by site and epoch (invariant 9) | S | L | monomorphic member and method ICs, site plans, host lanes | none |

## Top five: design sketches

### 1. Copy-and-patch cells (RT)

- **The unit.** One admitted cell region: a loop block, a straight-line region or a frame program.
- **Stencils.** Each cell op (`exec_c_*`, the arithmetic, compare and jump, load and store, index and member rows) gets one
  stencil per operand-encoding variant: a cell, a constant, a pinned site. A stencil is compiled ahead of time by the same
  compiler as the library, from a C++ function with holes for its operands. A miss stencil saves the ip and returns to the
  existing flush.
- **Stitching.** At admission the VM copies each stencil and patches its holes:
  - cell offsets;
  - constants;
  - jump targets, which become native branches;
  - for a miss, the existing `cells_miss(f, loop, ip, reason)`.

  The cell state stays where it is (`cs.kind`, `cs.payload`), so writeback, flush, the debugger's fallback and error raising are
  unchanged. The native code is only a faster `cells_run_frame`.
- **Platforms.**
  - Windows x64: `VirtualAlloc` RW, then `VirtualProtect` RX. Register an unwind table (`RtlAddFunctionTable`) so a C++
    exception thrown through a host call unwinds.
  - Android arm64: W^X through `mmap` and `mprotect`, or dual mapping; `__builtin___clear_cache` after the copy.
  - 32-bit (armeabi-v7a, x86): its own stencil set, or the cell interpreter.
  - iOS, and any host that sets a no-JIT flag: the cell interpreter, which already exists and stays the oracle of the native
    code.
- **Stencil extraction.** This is the build step that does not exist yet. One option compiles the stencils with clang
  (`-fno-pic -mcmodel=large`), parses the relocations out of the COFF or ELF object, and generates a table header. That needs
  clang in the build on MSVC hosts.
- **Gates.**
  - Every game hash on native and on the cell interpreter.
  - The fuzzer three ways: interpreter, VM-cells, VM-native.
  - The debugger forces the interpreter path.
- **Cost.** The native code is per engine, owned by the chunk; it is not static, so no shared cache.
- **Why first.** It is the only lever that attacks the 24-42% left after the typed work, and every typed program gets it
  without being rewritten.

### 2. Whole-frame raw form

This extends frame programs to bodies with loops, making the plan in "Typed locals fast everywhere" real.

- **The compiler.** It already knows each instruction's slot reads and writes.
- **Boundary operations.** An op without a cell form becomes one: box the slots it reads, run it on the general executor, land
  the slots it writes.
- **Misses.** They become per operation: a failing guard runs that one op on the general form and lands its result.
- **Loops.** They become plain jumps inside the frame program with the budget poll, so the per-loop `cells_enter`,
  `cells_writeback`, `cells_restore_sites` and `cells_sites_reenter` go: 8-13% on OSSUARY, nbodies and queen [measured].
- **Invalidation.** The frame program's existing plan and epoch.
- **Risk.** The interaction with references (a slot `&` reaches stays a window slot, as specified) and with the debugger
  (armed means general).

### 3. Interprocedural call summaries

- **The pass.** It runs per module after checking. For each function with a proven callee graph it computes:
  - its result kind;
  - whether it can raise (an op with a raising form whose operands are not proven safe);
  - whether it can run user code (a host call, a getter, an operator overload);
  - which reference parameters it writes or lets escape.
- **What it feeds.**
  - The raw-call protocol can skip `raw_follow` and the epoch re-validation at a return when the callee summary says "runs no
    general code".
  - `exec_c_ret` lands without the door check when the result kind is proven.
  - `bind_parameters` skips the reference-admission walk (`reference_binding_admits`, 1.7% of tree) when the parameter mode
    is proven.
- **The cost.** fib_recursive's 60% in the call protocol is mostly epoch and plan re-validation and landing generality, which
  the summary turns into compile-time facts [read, inferred].
- **Hot reload.** A summary is stamped with its function's definition epoch and those of its callees. An import or reload
  recomputes the summaries of the changed functions and their transitive callers (one engine-wide pass, as check-then-apply
  already is). A call site whose callee's epoch moved falls back to today's validated path.

### 4. Borrowed references

- **The analysis.** It runs per function on `T&` parameters and `var&` locals. A reference that is never stored, captured,
  returned or passed to a callee whose summary lets it escape is borrowed.
- **The representation.** A borrowed reference to a field or element is a frame-local pair (holder pointer, slot index)
  instead of a pooled `reference_holder`, plus a pin on the holder only when a callee could release it. The summary decides
  that.
- **What goes.** The mint, the pool acquire, the release and `deref_slow`'s re-resolution by id: tree's ~14% [measured], and
  the census's rows 9 and 10.
- **What stays.** Semantics are unchanged: a write goes through, and an invalidated referent raises the same text, because
  the pair re-validates by slot generation where the holder did.
- **Gates.** Interpreter parity is untouched: the interpreter keeps holders.

### 5. Layout facts for typed arrays of classes

- **The checker.** For `array<P>`, where `P` and the declared class of each prefix field (`pos` in `pos.x`) have no
  subclass in the loaded program, it marks the element class closed.
- **The compiler.** It records the field slots at compile time, together with the classes' `field_shape_epoch`.
- **At run time.** `cells_rebind_apply` then checks one epoch per block entry instead of per element, and per element it
  loads the instance and the slot directly. The per-element `get_class_definition`, the prefix holder's class and the method
  epoch loads, which are 7965-7974 and ~45% of particles [measured], go.
- **What still needs the fact.** A subclass loaded later, an `extend()` or a re-class in place moves the epoch, and the plan
  falls back to the memo path.
- **Also removed.** The admission's `get_name() != type_name` string compare becomes a class-id compare resolved at compile
  time.
- **What remains.** The pointer chase itself, `obj_slot` to holder to `class_instance` to field, remains; only flat storage
  (flat-classes ruling) removes it.

## Prototypes

None measured. The profiling build and the profiles above are this note's only measurements. The cheapest candidate with a
real signal, a compile-time class id for the alias admission in #5 (nbodies ~2%), sits below the 1.5% whole-program bar
on the games, so it was not worth an interleaved A/B on its own. The higher-value items are each a multi-day change. No
JaiDOOM, Gator Quest or GLOOM profile was taken. OSSUARY against Lua was not re-measured: the last figures are in
`PERFORMANCE.md` (2026-10-07).

## Open questions for Dev

1. **Native code at all.** Copy-and-patch for cell regions (#1) is the largest lever. It needs:
   - a stencil build step, probably clang-compiled stencils with a relocation extractor;
   - executable memory on Windows and Android;
   - a permanent no-JIT path (the cell interpreter) for iOS and 32-bit.

   Is native code in scope, and is clang acceptable as a build-time tool for the stencils?
2. **Speculation boundary.** Is quickening on *resolution* (which binding, which slot, which overload) allowed under the
   no-speculative-var-types ruling, with quickening on a `var`'s *kind* excluded? Tracing and deopt (#14) would need kind
   speculation.
3. **Offline op-sequence census for superinstructions (#9).** Is it design input or forbidden PGO?
4. **Closed hierarchies.** May the checker treat a class with no subclass in the loaded program as closed, guarded by an
   epoch that a later import, `extend()` or re-class moves (#5, devirtualization)? Or must closure be declared (a `final`
   keyword)?
5. **Summaries across modules (#3).** Recomputing transitive callers on reload makes a reload cost grow with the call
   graph. Is that acceptable, or are summaries capped at module boundaries?
