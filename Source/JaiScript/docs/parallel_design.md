# Parallel execution design (`parallel_for` on one engine, thread_storage pads)

Status: DESIGN with the v0 slice SHIPPED — **`parallel_transform(arr, fn[, weight_fn])` and
`thread_count()` are live** (the prove_or_serial §5 sequencing step; implementation:
`detail/parallel_transform.hpp` + `source/implementation/parallel_transform.cpp`, tests in
`source/tests/language/parallel_transform_tests.cpp`, scaling benches in
`source/tests/performance/parallel_transform_bench.cpp`). The `parallel_for` keyword,
`thread_storage` pads, and the binding annotations remain design-only. Dev rulings
2026-07-06 are final where marked RULED. All four §11 questions are RULED — the only
remaining open sub-item is the Q1 weight-hint syntax spelling for the future KEYWORD form;
the shipped BUILTIN needs no new syntax (the hint is simply the third argument).

## 0. Ground truth: script values can never be shared live across threads

`strong_ptr` is **purely non-atomic** — `core/strong_ptr.hpp:29` ("Purely non-atomic for
maximum single-threaded performance"), plain `size_t strong_count/weak_count` at :37-38, class
doc :115-130 ("Thread safety is the caller's responsibility (critical sections, partitioning,
etc.)"). Every heavy `script_value` alternative (array/map/string/object) rides it. Consequence
that drives this whole design: **reads are writes**. Copying a handle bumps a non-atomic count,
and script-level reads produce handle copies constantly — so "the body only reads `a`" is NOT a
safety argument by itself. Two threads touching the same live value is a data race, full stop.
Safety must come from partitioning: each thread's reachable set is exclusive, or it got a clone.

The matching asset: **zero static state**. Every `script_value` carries an `engine*`
(invariants.md §1: "`engine_` and `type_info_` are **the language**"); no non-const
statics/thread_locals anywhere (CLAUDE.md code guidelines). All engine-shared machinery is
instance state, so "what a worker may touch" is enumerable and gateable.

## 1. RULED: one engine, the Emitter pattern

There is **ONE engine**. No worker-engine mirrors, no jaibite provisioning, no per-worker
prelude state for `parallel_for` (the isolate-pool tier is demoted to the appendix). The model
is the one the engine already ships in C++ — `Emitter` particle threading
(`Source/MV/Render/Scene/emitter.h:359-368`): a `std::vector<ThreadData> threadData` indexed by
group, each worker writes **only** `threadData[a_groupIndex]` (emitter.cpp:179-211), shared
state is read-only during the region, and the main thread merges the pads after the join
(`loadParticlePointsFromGroups`, emitter.h:348).

```jaiscript
parallel_for (auto x : a) {
    thread_storage.total = thread_storage.total + cost(x);   // own pad only
}
// after the join, thread_storage is the array of pads:
auto sum = 0;
for (auto pad : thread_storage) { sum += pad.total; }
```

### The contract

During the region the script may **READ anything in scope** and may **WRITE ONLY its own
state**: `thread_storage` (inside the body it names the executing thread's own pad), the body's
locals, and — when iterating by reference (`parallel_for (auto& x : arr)`) — the thread's **own
loop element**, mutable in place (RULED, §11 Q4: under static chunks + the barrier alias walk
the element is the thread's exclusive property). Any other store — global, outer local, shared
object field, and transitively through called functions — is a **runtime error** ("cannot write
enclosing state in a parallel body"). One rule buys transitive
purity: a callee that mutates a global or a captured map hits the same wall, so there is no
"pure function" annotation, coloring, or whole-program analysis.

After the join, `thread_storage` is an ordinary array of per-thread pads (size = worker count;
`thread_count()` builtin), readable and mergeable by the main thread like any array. Pads are
**per-`parallel_for`**: fresh (empty) at each loop's fork; contents persist after the join until
the next `parallel_for`. Each pad starts as an empty map; the body writes fields onto it.

A `parallel_for` has exactly **two output channels**: the pads (data results) and the ordered
world-write command buffer for annotated C++ bindings (§5). Nothing else escapes the region.

No `async`/`await`, no detached threads, no handles to leak: strictly structured fork-join. The
calling thread parks at the barrier; workers fan in.

## 2. Prerequisite: per-thread execution contexts inside the one engine

One shared engine means the engine's internals are shared, and today they all assume a single
executing thread. Before ANY script body runs off-thread, each worker needs its own execution
context, and the residual shared structures need a concurrency stance. All of it is bound by
the zero-ambient-cost invariant (§11 Q3): context setup/teardown is charged to the
`parallel_for` call, never to sequential execution. Engine-internal work items — this is the
real cost center of the whole feature:

- **Per-worker execution state**: value/call stacks, in-flight call records, and the vm's
  per-run state must be per-context, not per-engine (today the engine reuses one persistent
  interpreter and pools call records on the assumption of one runner).
- **Environment/value pools**: pooled environments and env-lookup caches assume single-threaded
  mutation — per-context pools, or the region freezes the shared ones.
- **Symbol interner**: `string_symbolizer` is engine-bound (string_symbolizer.hpp:79-83
  documents the engine-wide env-shape epoch riding on it) — read-mostly during the region,
  short lock on the rare runtime intern.
- **Parse cache / class definitions / vm chunks**: frozen for the region (no defines, no hot
  reload mid-loop); the write wall (§4c) already forbids the script-visible mutations.
- **Budget/memory accounting**: `execution_limits` is per-engine instance state
  (`detail/execution_limits.hpp:15-17`) — per-worker accounting that rolls up, so one worker's
  allocation bomb raises on that worker (§6).

## 3. RULED: scheduling is static chunks (determinism)

Iterations are pre-partitioned into **contiguous per-thread chunks** before the fork — exactly
`n / thread_count()` by default, boundary-shifted by the optional weight hint (RULED, §11 Q1).
Same input + seed ⇒ byte-identical `thread_storage` contents — replayable, smoke-hashable. This is
the point: **deterministic pads** replace the old sequential-equivalence contract as the
testable guarantee (§8 Testing).

Rejected for now: dynamic work-stealing. It balances load better but makes pad contents depend
on the race (which thread got iteration i), destroying determinism. Revisit only with a design
that keeps iteration→pad assignment fixed.

## 4. Read safety under non-atomic refcounts

> CELLS caveat on (a): the alias rationale below cites the old `reference_holder` mechanism —
> the in-flight CELLS refactor replaces that machinery; the conclusion likely survives, the
> cited mechanism won't.

Three mechanisms, one per hazard class:

- **(a) The iterated container** — static contiguous chunks + a barrier alias-walk. At the fork
  (a single-threaded moment), an O(n) walk over the container's element subtrees collects each
  chunk's reachable strong_ptr control-block set. Disjoint chunks ⇒ each thread's chunk is its
  **exclusive reachable set** ⇒ zero-copy element reads, no atomics — the partitioning
  discipline core/strong_ptr.hpp:115-130 was designed for. Any cross-chunk alias (or alias into
  a capture) deep-clones that element at the barrier; correctness never depends on the fast
  path. Value semantics makes alias-freedom the common case (docs/JaiScript_DeepCopyDesign.md —
  assignments and parameter passing deep-copy), so aliases exist only where someone made one on
  purpose (`reference_holder`, value.hpp). References are owner-pinned handles
  (cell/element/field/map-entry, invariants.md §3) whose strong_ptr refcounts are NON-ATOMIC
  and whose pins alias engine-owned state — a ref reachable from an element forces that
  element onto the clone path.
- **(b) Everything else read from outer scope** (captures, globals, config objects) —
  **first-touch gate**: on a thread's first touch of each outer variable, a short global lock +
  `script_value::clone()` (value.hpp:164 — deep copy) into that thread's read cache; lock-free
  thereafter. Cost proportional to *variables touched*, not iterations.
- **(c) Writes** — checked at the store chokepoints (slot/symbol/ref stores) via the executing
  context's parallel flag, in both backends through a shared `detail/` kernel (invariants.md
  §6 — parity by construction, not twins). The check admits stores to **own** state — the
  thread's pad, the body's locals, and the thread's own loop element (§11 Q4) — and rejects
  everything else.

## 5. Bound C++: the phase discipline (three-level binding annotation)

Bound functions/methods get a registrar-level thread annotation; unannotated bindings raise at
the call site inside a parallel body (today's binder has no thread-safety notion — every level
is a host promise):

- **`self_only`** — touches only its receiver. Callable in parallel bodies; the receiver is
  chunk-owned or pad-owned, so it inherits the partitioning safety.
- **`read_world`** — reads world state but writes none. Callable during the parallel phase
  precisely because **nobody writes world state during it** — the phase discipline, not
  per-object locking, is what makes the reads safe.
- **`write_world`** — NOT executed inline. The call and its arguments (captured by clone at
  enqueue) go into an **ordered command buffer**, applied serially in **ITERATION order** after
  the join. World effects stay deterministic under static chunks, and the world never mutates
  under a reader.

So the frame shape is: **parallel phase** = read world + write own pad/self + enqueue;
**serial phase** = apply the queue in order. Script-side world writes stay flatly forbidden
(§1) — the command buffer is the only sanctioned world-mutation channel out of a region.

Honest cost: annotating the ~76 existing registrar sites (dynamic_binder methods/properties
across MV/Bindstone) — mechanical but real, and each annotation is a reviewed claim.

## 6. Errors

Any iteration's throw fails the whole `parallel_for`: workers finish or abandon their chunks,
the command buffer is discarded, and the join re-raises the **FIRST error in ITERATION order** —
deterministic under static chunks (thread T's first error has a known iteration index; the join
takes the minimum). The terminal rails apply per worker: `execution_budget`
(engine.hpp:198-203) and `memory_cap` (engine.hpp:209-214) with the terminal-error latch
(`detail/execution_limits.hpp:18-27`; the one-catchable-raise memory ruling at :61-70) — a
livelocking or allocation-bombing body fails the loop, not the process.

## 7. RULED: threads are `jai::thread_pool` (hoisted from MV)

JaiScript owns the pool (answers old open question 5). `MV::ThreadPool`
(`Source/MV/Utility/threadPool.hpp:14`) was hoisted **into** JaiScript —
`include/jaiscript/detail/thread_pool.hpp` has LANDED (shared queue + pinned `submit_to`; it
cites this doc) with a pinned-worker mode; that header is the `parallel_for` substrate, and MV
wraps or forwards to it. Standard C++ only (`std::thread`/`std::mutex`/`std::condition_variable`), no OS APIs —
JaiScript stays the base library; the old JaiScript→MV layering inversion is dead.

## 8. Kept invariants

- **No script-visible locking primitives, ever.** A mutex in a game script is a deadlock a
  designer ships. Coordination is the join + pad merge + command buffer, nothing else.
- **No `yield` in parallel bodies** — error. Coroutine fibers/continuations cannot resume
  coherently across a parked barrier.
- **`print` buffered per thread**, replayed in iteration order at the join (contiguous chunks
  make that a concatenation in chunk order). Required because the fuzz harness byte-compares
  printed output (fuzz_harness.hpp:3-6).
- **Per-thread seeded rng streams** — any script-visible rng derives a per-chunk seed, keeping
  pads deterministic.
- **Bound C++ callable only under a thread annotation** (§5); everything else raises at the
  call site.
- **Testing: deterministic pads ARE the contract.** The differential fuzz harness already runs
  one generated program on both backends and byte-compares result + printed output + error
  text + engine-alive-after, fully seed-deterministic (`source/tests/fuzz/fuzz_harness.hpp:3-6`,
  splitmix64 rng :35-42). New leg: generate `parallel_for` bodies (deliberate aliasing,
  enclosing-write attempts, throws mid-iteration), run the same program with 1 thread vs N
  threads on the same seed, byte-compare `thread_storage` contents + print + error text.
  Divergences log to `known_divergences.md` like today.

## 9. Rejected

- **Shared-memory script threading** (atomics/mutexes on script values): atomic refcounts tax
  every copy in every single-threaded script — the copy is THE hot operation the whole VM-perf
  effort shaves; fences also poison the pooled call records, env-lookup caches, and
  interned-symbol machinery that assume single-threaded mutation. The payoff is a programming
  model (locks, ordering, torn invariants) game scripts can't debug.
- **Dynamic work-stealing** (§3): nondeterministic pads.
- **Multiple engines as the `parallel_for` substrate** (RULED out): mirror maintenance on every
  hot reload, jaibite re-provisioning, engine-scoped class definitions forcing clones of every
  instance, and N engines' worth of memory — all to get isolation the write-wall + first-touch
  gate provide on one engine. Isolates survive only as the appendix.

## 10. Honest costs & applicability

- **Per-thread execution contexts (§2) are the bulk of the engineering**, not the loop itself.
- **Barrier alias walk is O(reachable) over the container at every fork** — cheap for flat
  arrays of primitives, real for deep object graphs.
- **First-touch gate**: one lock + deep clone per (thread × outer variable touched); big
  read-only assets can still bind as `std::shared_ptr<const T>` per the appendix to skip
  cloning.
- **Static chunks can load-imbalance** (one expensive chunk gates the join) — the price of
  determinism, mitigated by the optional weight hint (§11 Q1), which itself costs an O(n)
  single-threaded evaluation at the barrier when present.
- **Registrar annotation sweep** (~76 sites, §5).
- **Per-thread pads + read caches** are live memory until the next `parallel_for`.
- **Applicability, honestly**: in Bindstone's actual frame profile, script update is not the
  hot path — pathfinding, spine, and particles are, and those parallelize in C++ behind
  bindings (the Emitter already does) without touching script semantics. `parallel_for` is a
  language capability play more than Bindstone's cheapest frame-time win; size the effort
  accordingly.

## 11. RULED: the four open questions (Dev, 2026-07-06)

Question text kept for history; rulings are final. The one genuinely open sub-item is the Q1
weight-hint syntax spelling, deferred to the implementation pass.

### Q1 — chunk grain. RULED: `n / thread_count()` contiguous chunks + optional per-index weight hint

> Was: exactly `n / thread_count()`, or a finer fixed grain (still statically assigned) to
> soften imbalance? Language default with an optional hint, or explicit in the syntax?

Default = exactly `n / thread_count()` contiguous chunks, no tuning knob. The imbalance answer
is an **optional per-index weight hint** — Dev's motivating example: parsing an array of JSON
strings, weight by text length so chunks balance by **effort**, not element count. Semantics
when the hint is supplied:

- The weight expression evaluates **per element at the barrier** — single-threaded, before
  fan-out — and contiguous chunk boundaries are chosen to equalize cumulative weight.
- The O(n) weight evaluation is paid only when a hint is present; hint-less loops keep the flat
  split at zero added cost.
- The hint expression must be **side-effect-free** — the same rule as the body's read
  discipline; it runs under the barrier's single-threaded safety anyway, but a mutating hint is
  still an error.

SHIPPED NOTE (v0): the `parallel_transform` BUILTIN sidesteps the spelling question entirely —
the optional weight hint IS just the third argument: `parallel_transform(arr, fn, weight_fn)`.
The candidates below apply only to the future `parallel_for` KEYWORD form.

Dev wants "a really *really* easy OPTIONAL syntax" for the hint. Candidate spellings for the
implementation pass to choose from (**syntax final call at implementation**):

1. Inline `weighted` clause — `parallel_for (auto& x : arr weighted x.size()) { ... }`. Pro:
   the hint sits right next to the loop variable it uses; con: a contextual keyword inside the
   range clause.
2. Trailing `by` clause — `parallel_for (auto& x : arr) by x.size() { ... }`. Pro: the range
   clause stays untouched; con: the hint dangles between header and body.
3. Second header expression — `parallel_for (auto& x : arr; x.size()) { ... }`. Pro: pure
   punctuation, no new keyword; con: least discoverable, and the semicolon reads C-style-for.

### Q2 — iteration domains. RULED: arrays only in v1

> Was: arrays only, or also maps/ranges? (Maps make chunk boundaries and the alias walk
> hairier.)

Arrays only — "that's a fine restriction." Maps/ranges deferred; the parenthetical above stands
as the reason (map chunk boundaries + the alias walk get hairier for nothing v1 needs).

### Q3 — captured-read gate. RULED: first-touch lock+clone as drafted

> Was: first-touch lock+clone as drafted, or snapshot the whole captured set up front at the
> barrier (simpler, pays for untouched variables)?

First-touch lock+clone (§4b) stands — it is the cheapest option. The ruling adds a hard
constraint of its own:

> **INVARIANT — zero concurrency cost outside `parallel_for`.** No lock, atomic, or
> synchronization consideration may exist anywhere in the engine except while a parallel region
> is actively executing. The first-touch gate's mutex exists only inside the region; sequential
> code paths must be provably untouched. This binds the per-thread execution contexts (§2)
> too: their setup/teardown costs belong to the `parallel_for` call, never ambient.

### Q4 — own-element mutation. RULED: yes

> Was: may a body mutate its OWN loop element in place (`for (auto& x : ...)` style), alongside
> the pad? Disjoint ownership says likely yes under static chunks + the alias walk — the element
> is the thread's exclusive property — but it widens the write wall's definition of "own".

Yes — `parallel_for (auto& x : arr)` may mutate its own loop element in place; under static
chunks + the barrier alias walk the element is the thread's exclusive property. The write
wall's definition of "own" = the thread's pad + its own loop element + body-locals. The
contract (§1) and the write check (§4c) are updated to match.

## 12. Sequencing sketch

1. **`jai::thread_pool` lands** (DONE — `detail/thread_pool.hpp`, §7) with pinned-worker mode + foundry coverage.
1.5. **`parallel_transform` v0 lands** (DONE — the prove_or_serial §5 "step 3.5" tier:
   value-semantic elements, fail-closed admission walk, per-worker execution contexts
   built at the call and torn down at the join, static chunks + optional weight hint,
   iteration-order error selection, per-worker budget/cap rails, 1-vs-N determinism +
   fuzz battery both backends). Deferred from v0 toward the steps below: pads, captured
   reads (first-touch gate), own-element mutation, command buffer/annotations, objects.
2. **`parallel_for` sequential-semantics first** (~1k lines): parse, both backends,
   `thread_storage` pads, the enclosing-write wall, binding annotations + command buffer,
   print buffering, `thread_count()` — running on ONE thread. Gate: full foundry green on both
   backends with `parallel_for` lowered to sequential; semantics frozen while everything is
   still single-threaded and debuggable.
3. **Per-thread execution contexts** (§2): the engine-internal refactor, gated by the existing
   full suite staying green single-threaded.
4. **Static chunking + alias walk + first-touch gate + real fan-out** (~1.5k lines): chunk
   partition (flat + weighted, §11 Q1), control-block set walk, clone fallback, read caches,
   barrier, iteration-order error selection + buffer apply. Gate: the fuzzer's 1-vs-N leg (§8) seed-swept overnight,
   zero divergences; plus targeted alias-torture foundry tests.
5. **Perf pass**: prove zero-copy chunk reads beat clone-everything on representative workloads
   (Foundry benchmarks are ±50% integer-µs — invariants.md §7 — use dedicated micro-benches for
   the walk itself).

Step 2 before steps 3-4 is the load-bearing ordering.

## Appendix: possible future — background jobs on separate engines

NOT the `parallel_for` substrate (RULED). Kept as a sketch for long-running background work
(pathfinding batches, procedural generation) if it's ever needed:

- N pinned worker threads, one engine per thread for the thread's lifetime (workers accumulate
  prelude state, so tasks need thread affinity).
- Provisioning via the jaibite binary path: `jaibite::save_bytes()` on the source engine,
  `engine::jaibite_load_bytes()` per worker — symbols re-interned into the loading engine,
  type_info re-interned structurally, VM chunk recompiles lazily (engine.hpp:116-125, landed
  80fd619b); `registration_fingerprint()` (engine.hpp:127-130) gives a free staleness check.
- Crossings by value only: `script_value::clone()` or `serialization/binary_archive.hpp`
  round-trip; big immutable assets bind `std::shared_ptr<const T>` into each engine via a
  `.thread_shareable()` binder flag (atomic control block carries lifetime; `const T` enforces
  immutability at the binding surface).
- Per-task containment already built (eb4108a2): per-engine budget/memory rails + the terminal
  latch mean a failed task surfaces as a failed future, never takes the pool down.
- Script-class instances always cross by clone — `class_definition` is engine-scoped (symbol
  IDs interned per engine, hot-reload migration machinery: class_definition.hpp:87-90, :1078-1080).
