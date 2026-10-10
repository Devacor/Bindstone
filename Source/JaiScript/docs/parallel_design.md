# Parallel execution design (`parallel_for` on one engine, thread_storage pads)

Current status: `parallel_for`, `parallel_transform`, captured reads, element-member work,
and native binding admission are implemented. Safe mode proves a body or rejects it;
small admitted workloads may run serially. Worker count does not change the semantic
admission rule. `thread_storage` pads and several extensions below remain proposals.
The staged descriptions and performance snapshots below are design history, not a
current feature checklist. See CLAUDE.md and the language guide for the shipped surface.

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

Iterations are pre-partitioned before the fork into a fixed per-thread set: without a weight
hint, blocks of `max(1, n / 4W)` consecutive elements dealt round-robin over the W threads
(block-cyclic: a cost that varies along the range spreads over every thread; small n means
expensive elements, large n means blocks of hundreds so no two threads write neighbouring
scalars); with the optional weight hint (RULED, §11 Q1), contiguous chunks split by cumulative
weight. `engine::last_parallel_chunk_bounds()` publishes the cumulative element counts per chunk.
Same input + seed ⇒ byte-identical `thread_storage` contents — replayable, smoke-hashable. This is
the point: **deterministic pads** replace the old sequential-equivalence contract as the
testable guarantee (§8 Testing).

Rejected for now: dynamic work-stealing. It balances load better but makes pad contents depend
on the race (which thread got iteration i), destroying determinism. Revisit only with a design
that keeps iteration→pad assignment fixed.

**Thread count and the pool.** `engine::parallel_thread_count()` is the total context count
(main runs chunk 0): the override when set, else `min(hardware_concurrency - 1, 6)` (Dev).
The pool threads are pre-allocated once per engine, sized to that count minus main, and reused
by every region; a region only pays a submit, a wake and a join per pool worker. Because the
partition never changes a result, the count woken is a pure cost knob, and both sides of it
are measured on the machine at hand: when the pool is built, three no-op round trips give the
fixed per-worker cost (`worker_overhead_ns`, the minimum; ~4 us on the reference box), and each
statement's region pool keeps the smoothed ns per element of main's own chunk
(`ns_per_element`). A statement's first region fans out to every thread with an even split
(the baseline); after that a region wakes only as many pool workers as can each be handed a
chunk worth `k_parallel_chunk_overhead_ratio` (3x) the wake cost. Every region of two or more
elements takes this path; there is no separate small-count cutoff (the fixed 16 that preceded the
cost model kept a 15-element region of 24 us elements on the calling thread). Measured on the floor
benchmark (vm, us per region, parallel vs serial): N=32 17 vs 7.5 (one context), N=256 55 vs 61,
N=2048 280 vs 500 - below a few hundred light elements the per-element host-to-script call on
the worker still dominates, and that call is the next lever.

**Shared strings detach at the barrier, wherever a worker can copy them.** The only alias a
value-closed element can still carry is a string node several slots share (a literal every
instance of a class was built from: a field default, a map key, a `push` in a loop). The element
walk detaches such a node in place in a string field, a string element or map key of a container
field (a `map<string, int>` or `array<string>` field is string-bearing, not primitive: the
borrow tier's all-primitive predicate does not apply to the proof), each slot of a typed string
buffer, and the elements of a nested object buffer; a source whose declared type reaches a
string is never exclusive by construction. A worker's by-value parameter clone (the read-only
elision needs a main-thread verdict) then copies handles no other thread touches.

**The interner is frozen in every mode.** `string_symbolizer` is not thread-safe, so a region
freezes it before the fork whether or not `allow_unsafe_parallel` is set: unsafe mode vouches
for the script's sharing, never for the runtime's tables. Every name a worker can probe is
interned at admission, single-threaded: member, getter and setter ids of every chain the body
names and of every method-call site (both backends probe the user-accessor id of a method name
before dispatch). A miss on the frozen interner is Debug's assert, the tripwire.

**Unsafe mode trusts the script's data, never the runtime's structures.** With
`engine::allow_unsafe_parallel(true)` the admission walk still runs, warming every name and
pinning every element method it can; only its script-semantic verdicts (captured-state writes,
aliasing, mutating methods on captured state, member stores, lambdas, `new`) go lenient. The
engine-seam rules stay walls in every mode: bare sibling-method calls, call targets the walk
cannot resolve, `yield`, includes and declarations inside a body, `math::random`, every `time::`
call (the engine-owned clock: a read would make the result differ from the serial run). The element
proof still collects the element classes and detaches shared strings; its verdict goes lenient.
The worker walls apply in every mode: an unpinned method call, operator dispatch on instances,
bound-method and constructor thunks, accessor reads and stores all raise their verdict in a
worker, because each of them copies a shared method value or touches an engine-owned cache. What
unsafe buys is the skipped proof, not a way past those counts.

**Wake protocol.** A pool worker that finishes its chunk spins on its own flag for up to
`engine::parallel_spin_wait_us` (default 1000) before parking on the condition variable, and
only when the pool's last two batches arrived within that budget; the joining thread spins the
same way on the outstanding count. Dense regions (a frame's worth of fork-joins) therefore
wake without a kernel transition on either side, while a program whose regions come one per
frame parks at once and burns nothing between them; 0 always parks. The submitter asks the
kernel only for parked targets and a worker pings the joiner only while it is parked, both
decided under the mutex the waiter sets its flag under, so no wake is lost. Placement and order
never change, so results do not.

**The element reference is a cursor.** A by-reference body binds its element to ONE reference
holder per chunk, minted at the barrier (the array pin and the `live_elem_refs` count are
main-thread facts) and re-pointed at the next index on the worker before each call; an admitted
body cannot retain it past its iteration (no lambdas, no `new`, stores dereference, captured
state is read-only, the result is discarded), and a retained holder is an internal error rather
than a silent re-point. The serial by-reference range-for does the same on both backends for
frame-slot loops (`script_value::element_cursor_holder`: the previous holder moves to the next
index when nothing else shares it, else a fresh mint) - top-level loops keep the per-iteration
mint so destructor timing stays byte-parallel. On a typed object or string array the holder's
boxed view is refreshed only when the slot's handle changed (`script_array::view_current`).

**The worker runs its chunk in-script.** A worker enters script ONCE per region: the
statement's chunk wrapper (`build_chunk_wrapper`, built once per statement from a parsed
skeleton) is a counter loop with the body block grafted in as the loop body -
`for (__k = 0; __k < __n; __k = __jai_parallel_advance(__k)) { <body> }` - so there are
zero calls per element: the loop variable is the wrapper's own parameter 0 (the body's slot,
same name), `__n`/`__k` are renumbered past the body's frame, and the advance is one vm
opcode (`op_parallel_advance`; a native on the interpreter) that re-points the chunk's
cursor. A by-reference loop variable binds the cursor once and follows it; a by-value one
copies through it per iteration (`__c = __jai_parallel_element()`). A body that returns from
inside a NESTED loop keeps its meaning through call mode (`__jai_parallel_body(__c)` per
element, the previous shape). Floor benchmark after (vm, us per region, parallel vs serial):
N=32 14 vs 7.8, N=256 45 vs 60, N=2048 160 vs 495; an inline field-update body at N=2048
runs 115 vs 427.

**parallel_transform is the zip.** `parallel_transform(xs, fn)` is
`parallel_for (auto& x, auto& o : xs, out) { o = fn(x); }` over a fresh output of `xs.size()`
nulls, and nothing else: one region core (`run_region`) runs both statements, the output is
the zip's second array (allocated by the call, so it skips the exclusivity walk the caller's
arrays get), the result is the ordinary store of a call result (a temporary; the return-site
rule already copies anything fn could name, so objects come out and never alias the source),
and there is no per-element sink, opcode or result contract of its own. The call itself is
gone too: when fn only reads its one by-value parameter (the admission verdict below) and has
no return inside a nested loop, fn's BODY is grafted into the chunk loop at first use - the
body cloned through the jaibite serializer, its parameter bound by reference to the source
cursor (the in-place rule), each top-level `return e` rewritten to `{ o = cast<T>(e); continue; }`
with the cast dropped where the checker's return proof already says the value is the declared
type (`auto`/`var` returns store as is), a bare `return` to `continue` - built once per fn
(cached on the engine, keyed by the body) and compiled once per worker like any chunk
wrapper. A fn that mutates its parameter, returns from a nested loop or takes a weight
partner keeps the call form (`o = __jai_parallel_fn(x)` in the same loop), so a mutating fn
still never touches the source. Instances a pool worker constructs or copies no longer
register in the class registry under the region lock: they park in the worker's pending list
and the join registers the survivors (a copy that died meanwhile just expires; hot reload
still migrates everything that lived). Cost: the transform is the inline zip plus the fresh
output. 40k Vec3 -> float (`v.x * v.y`, vm, per region): inline zip 1.5, transform 1.9-2.0 (the
call form was 2.1-2.3, the previous emit path 2.9-3.2), of which the 40k-null allocation and
its release are the 0.35 difference; serial 33. The deep node shape runs 78 against 252.
Not yet admitted: constructing objects inside a body (the 'structs out' pass).

**The zip form.** `parallel_for (auto& v, auto& o : vecs, outs) { o.x = v.x * v.y; }` walks
further arrays of the same length in lockstep: thread n owns segment n of every array, so the
pre-allocated output struct is written in place - no construction, no result marshalling, no
registry (the emitter pattern; Dev: `outArr[n*x] = result`, which the cursor IS). Each further
array is proven exclusive like the range (whole reach: the body may write anything through its
loop variable; a typed primitive kind is demoted like the range, an object kind addressed in
place), gets its own chunk cursor, and the one advance opcode steps every cursor. Arrays of
different length, the range itself, or a non-array are load-time errors
(`parallel_for: zipped array 2 has 1 elements, the range has 3 (zipped arrays must have the same
length)`). A by-value loop variable keeps its copy per iteration unless the body only reads (the
in-place rule above, decided jointly for all loop variables). 40k Vec3 in, 40k Vec3 out (three
field writes): 4-5 ms per region against 34 serial; a float output 1.5 ms against 38 serial. jaibite 13 carries the extra loop variables and arrays.

**Body control flow.** The body is the chunk loop's body: a top-level `continue` reaches
the loop's advance on its own and a top-level `return` (any value) is rewritten to `continue`
once, at body-function creation - both mean the next element on both backends; a loop-level
`break` is rejected at admission (`parallel_for: 'break' is not allowed in a parallel body
(iterations are independent; use 'continue' for the next element) at line:col`); inside a
nested loop or switch break/continue keep their C++ meaning, and a `return` inside a nested
loop selects call mode for that body.

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
  path. Value semantics makes alias-freedom the common case (assignments and by-value
  parameters copy), so aliases exist only where someone made one on
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
1.6. **Captured reads land (v0.5)** (DONE — §13: touch-collection admission + the
   scalar/string/borrow/snapshot provisioning tiers + the runtime write wall; supersedes
   the first-touch-gate spelling of Q3 for the builtin — the barrier provisions
   everything up front, and the borrow tier makes the proven case cheaper than any gate).
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

## 13. SHIPPED (v0.5, 2026-07-08): captured reads — read enclosing state, proven read-only

A parallel body may READ enclosing names: globals through the environment chain, and the
locals of the function its statement runs in (the parser records their frame slots on the
statement, `parallel_for_stmt::frame_captures`, and the barrier provisions from that frame;
a global never shadows a local of the same name). Writes to enclosing state remain
errors in every shape, always (contract A). Implementation:
`source/implementation/parallel_transform.cpp` (admission + barrier + the borrow kernel),
`core/value.hpp` variant alternative 15 (`parallel_borrow_tag`), both backends' subscript
twins call the ONE shared kernel `detail::parallel_borrow_subscript_read` (parity by
construction, invariants.md §6).

### 13a. Two-layer design (Dev-ruled): cheap admission + runtime write wall

The admission walk does NOT prove read-onlyness — it only **collects the enclosing names
the body touches** (touch-collection; interned ids, one flag per name) and rejects, as
*free static diagnostics with line:col*, every write shape it can already see: direct
assigns, subscript/compound/incdec stores rooted at an enclosing name, `var&` alias
declarations, by-reference arguments into script callees, by-reference iteration over a
captured container, and any method on a captured receiver that is not a known read-only
builtin (a mutating method on a worker's private snapshot would silently diverge across
worker counts — so unknown = rejected, fail-closed). Soundness never depends on the
walker: the runtime write wall (the kernel's lvalue_write check; snapshots are
worker-private) holds regardless.

### 13b. Provisioning tiers (classified per region entry, from the LIVE value)

Every captured name resolves against the region's enclosing scope at the barrier
(single-threaded) and provisions per worker as one of (`detail::parallel_capture_kind`,
pinned by `engine::last_parallel_captures()`). A pinned method's captures are its own set
and its class's scope: they resolve against the global environment and provision into the
worker's separate method environment, so a caller-local and a global of the same name each
keep their binding (the body reads one, the method the other):

- **scalar** — null/int/float/char/bool (bound primitives decode once at the barrier):
  plain per-worker copy. Free.
- **string** — per-worker detached copy (string storage is shared under plain copy).
- **borrow** — the raw-read fast tier: every element of the container is a **primitive or
  a string** (typed `array<string>` included; by declared type or by a content walk) AND
  every body touch is a **subscript read**. Workers get ONE tagged raw pointer
  (`parallel_borrow_tag`, variant alt 15) whose copies touch no refcount; element reads
  mint worker-local values — a primitive is read from the buffer, a string read copies
  the BYTES into a fresh worker-owned string and never touches the live handle's count.
  **Zero copies at the barrier, zero refcount traffic, zero memory_cap charge.** GLOOM's
  bit-packed int64 grids are free to read, and its glyph and palette tables now ride the
  same tier: the per-read byte copy of a short escape string is far cheaper than copying
  hundreds of table entries per worker per region (GLOOM vm, 8 workers: 2.76 -> 1.92
  ms/tick against 2.27 serial once the tables moved from snapshot to borrow; before, the
  workers ran SLOWER than serial on the table copies).
- **snapshot** — everything else (nested containers, object elements, or any
  non-subscript use: iteration, read-only methods, whole-value reads): ONE detached deep
  copy per worker at the barrier, memory_cap-charged, content-proven and reused across
  regions when the shape allows (`flat_snapshot_equal`). A typed `array<string>` detach
  mints one fresh string per slot (`detached_typed_array`) - the str buffer's handles are
  refcounted and a buffer copy would share them across threads.

### 13c. Why the borrow is sound (and why direct shared reads never are)

`strong_ptr` counts are non-atomic (§0): even a read-only element copy of a live shared
container is a data race. The borrow never copies a shared handle — the value the worker
holds is a raw tagged pointer, and the ONE shared kernel reads elements by const& and
mints fresh worker-local values. Stability: (a) the write wall forbids every mutation of
enclosing state during the region, so the viewed container never reallocates and its
interior is frozen; (b) the barrier holds one anchor handle per borrowed container for
the region's duration, so the count is stationary and the container outlives the region.
Borrows are region-internal: the store kernel (`clone_for_assignment` →
`script_value::clone()`) and the region's result path materialize them through the
refcount-silent deep clone (`parallel_detached_copy` — traversal reads by const& and
constructs fresh nodes; it never makes even a transient shallow copy), so no borrow
survives the join, and a dormant worker slot holds no raw pointer (defines are nulled at
every region exit). Results that reach into a worker's snapshot ALSO materialize —
otherwise two iterations on one worker could alias while two on different workers don't,
a worker-count-visible difference.

Semantics: reads see barrier-time content, indistinguishable from pure since writes are
banned; iteration-order error selection, budget rails, and 1-vs-N determinism are
unchanged and re-pinned over capture workloads.

### 13d. Host-const bonus: DEFERRED (design note)

The proposal — auto-mark const-qualified host METHODS as read-safe callables inside
parallel bodies via dynamic_binder's static knowledge of member-function constness — has
no reachable surface: parallel bodies cannot hold host objects at all (elements,
captures, and results are value-semantic - script value classes, never bound host
objects), so there is no receiver a const method could be invoked on. The plumbing only becomes
meaningful with the §5 binding-annotation sweep / object tier, where `const T&`-receiver
methods would slot in as automatic `read_world`/`self_only` candidates — the binder can
detect constness via the member-function-pointer type at registration (cheap), but the
admission walker would also need static receiver typing to resolve WHICH binding a
member call hits, which is exactly the object-tier work. Revisit there.

### 13e. The pair idiom (the sanctioned pattern where capture isn't enough)

Captured reads cover shared INPUT. When each iteration also needs distinct per-element
inputs AND you want structured multi-value outputs, the standard approach is
**pair-shaped elements**: pack each element as `{input...}`, return `{outputs...}`, and
collect from the transform result — hand-chunking like GLOOM's 16-column ray records
remains the way to amortize per-element costs the scheduler can't see. Worked example:

```jaiscript
var grid = [];        // shared read-only input: captured, not carried per element
// ... fill grid ...
var jobs = [];
for (var s = 0; s < 8; s++) { jobs.push([s * 128, s * 128 + 128, s]); }   // {x0, x1, tag}
var f(var job) {
    int x0 = job[0];
    int x1 = job[1];
    var best = 0;
    var total = 0;
    for (var x = x0; x < x1; x++) {
        var v = grid[x];                  // captured read (borrow tier)
        total += v;
        if (v > best) { best = v; }
    }
    return [job[2], total, best];         // pair-shaped output
}
for (auto r : parallel_transform(jobs, f)) {
    // r[0] = tag, r[1] = total, r[2] = best — collected in iteration order
}
```

This is now SECONDARY to capture: use captured reads for shared inputs, the pair idiom
for per-chunk inputs/outputs; `thread_storage` pads (§1) remain the future answer for
accumulation across iterations.

## 14. SHIPPED: element members — a body reads (and stores) members of its own element

A `parallel_transform` body may name members on chains rooted at its element parameter
or any worker-private local: `fn(Node n) { return n.first.a.v + n.xs[1] + n.counts["k"]; }`.
The parameter is the worker's private by-value copy, so a store into it (`n.x = 5`,
`n.x++`) is legal and invisible to the source. Chains rooted at a captured name keep the
admission rejection (`member access reaches captured state 'g' (captured reads are
read-only, and objects cannot be captured)`); methods on the element stay behind the
worker method wall in the transform (the barrier admits element methods for `parallel_for`
only), and `this`/getters/setters are rejected as before. The admission walk is ONE walk
for both forms (`walk_local_member_chain`); it records the members every element-rooted
chain names (`element_reached_members` / `element_reached_whole` / `element_reach_all`)
for both forms.

Elements are proven exclusive by the ONE element proof (`prove_elements`), shared with
`parallel_for`: all-primitive sources by construction; an object-kind source by declared
type (`declared_class_exclusive` over the distinct slot tags, no instance read) when no
live reference pins the array (`script_array::live_elem_refs == 0`); everything else by
the per-element normalizing walk (`normalize_element_inplace`: shared strings detach in
place, engine-internal aliases clone, a non-flat instance or a shared_ptr-tagged instance
with a live alias rejects). Workers then read the source LIVE — a hetero vector through
`raw_input`, an object-kind node through `typed_input` — and the by-value binding makes
each private copy on the worker; the barrier detaches nothing per element. The transform
reaches every element WHOLE (the clone touches every field, and a string copy is a shared
node's count bump), so its proof takes an open reach; `parallel_for` narrows the walk to
the members its body names. Rejections name the offending leaf exactly as before:
`parallel_transform: element N: value is not value-semantic (object | shared_ptr |
weak_ptr | function | bound host object | ...)`. Results still cannot be objects.

The barrier's type pre-warm (`prewarm_region_types`) interns every shape a worker can mint
before the symbolizer and type tables freeze: it walks the declared-type closure of every class
an element can hold - the source's element type, the slot tags and instances the proof met, and
every registered class declaring a member the body names (`element_reached_members`, the seed
when a trusted or untyped source hands the barrier no class) - interning each type's `T&` twin
and container components, each class's field ladder (declared types, accessor ids) and its
registered subclasses, memoized per fork. A class reached only through a typed container
(`n.extras[0].v` on an `array<Leaf>` field, `array<Node> -> array<Mid> -> array<Leaf>`,
`map<string, array<Leaf>>`) therefore warms exactly like the element's own class; the
deny-intern backstop (`type shape was not pre-warmed at the region barrier`) names a shape the
walk missed.

## 15. SHIPPED: read-only by-value parameters bind by handle (the admission walk as a purity proof)

A by-value class/array/map parameter costs a deep clone per call; `xy(Vec3 v)` inside a zip
body ran 7 ms per 40k against 2.5 by reference. The clone exists to make the callee's copy
unobservable to the caller and vice versa - so when nothing can observe it, it goes (Dev: a
copy we can prove away goes). The proof reuses the admission builder in `proof_mode`
(`prove_readonly_parameters`, cached per `script_defined_function` against
`engine::definition_epoch()`, which moves on every top-level run, function/class/global
definition and host registration):

- the callee and every script function it calls would be admitted in a parallel body, so
  the call graph writes nothing but its own locals (no enclosing store, no by-ref hand-off of
  enclosing state, no host call outside the pure whitelist, no lambda, no `new`, no `yield`);
- the parameter is the walk's element root, and beyond `element_mutated` (a store through
  it, a mutating builtin on it, a subscript store into it) every identity-observing use
  escapes it: a bare use (comparison, argument, return, range source, `[&]`), an alias
  (`var& r = p`), a compound assignment or `++`/`--` on it (user operators), a user method or
  accessor on it, and a member/subscript chain whose static terminal is not a primitive or a
  string - objects and arrays compare by identity, so `p.child == g.child` would tell an
  alias from a copy. Read-only builtins on a receiver that is statically a container or
  string (`p.ids.size()`) are reads; rebinding the parameter itself (`p = x`) is not an
  escape (it replaces the slot, never the referent);
- a reference of the callee may name a part of the caller's argument (`f(a, a.hp)`), so a write
  through one (another reference parameter, a reference local, a by-reference loop variable)
  escapes the parameter whenever the reference's type can occur in the parameter's value tree
  (`type_may_hold`: fields, elements, subclasses' fields); a reference argument rooted at the
  parameter that the callee writes (`w(p.x)`, `detail/ref_param_writes.hpp`) mutates it;
- program-scope member chains are read in place when every step is a declared field with no
  user accessor (`AFFIXES[i].code`); a mutating method on one, a writing reference argument
  rooted in program state and a by-reference loop over it refuse the proof. A `var` global
  roots no such chain: its value's typed twin is not its type, and the next store may change it;
- a store or declaration of class type `T` (`T d = ITEMS[i]`, `T x = c.pack[i]` over `C& c`,
  `T y = local.f`) converts nothing when its source's static class is `T` - the value is `T` or a
  subclass, which a `T` slot takes without running code. The static class comes from a name (the
  root, a local's declared type, a reference's declared target) or a member/subscript chain whose
  every step is declared (a field's or an element's declared type, never `var` or `auto`) from such
  a name or from a global whose type is fixed for its life: a typed container, or a class no other
  class derives from (a global of a subclassed class may be re-classed by a store). Any other source
  may run a converting constructor or `operator=` and escapes the root;
- script destructors, per type (`class_definition::value_tree_runs_destructor`: the chain or a
  subclass declares one, or a by-value field's type can hold such an instance; handles share
  and never count; cached against the definition epoch): the parameter's type may not run one
  (the elided copy's destructor would never run), and nowhere in the walk may a value whose type
  can run one die (a by-value local or parameter, a replaced slot, a mutating builtin's
  receiver, a by-value call result or cast, a thrown instance) - its destructor is code the walk
  cannot see.

A proven parameter binds the argument by handle (`script_value(derefed)` instead of
`.clone()`) on both backends, in the exact-class fast branch and after the conversion
table alike - an identity conversion shares, a converting one is already a fresh value.
The argument side gates it too: script class instances that are not `nocopy` (a nocopy
by-value pass keeps raising whatever the callee does), arrays and maps; host objects keep
the clone. A call the VM grafts binds each by-value parameter by the call's own exits
(`vm_backend::bind_graft_parameter`): the callee its guard admitted, resolved again at the bind, gives
the read-only alias, and a prvalue argument (its private temp released) moves in as
`bind_parameters`'s C++17 elision moves a stack argument; both count and behave as the call does.
Only plain functions are candidates in v1 (methods and coroutines bind through
other paths); parallel workers never prove (interning, global lookups) - the barrier proves
the live function and restamps every worker copy per region entry. Kill switch
`engine::parameter_alias(false)`; gates `parameter_alias_differential_gate` (generator
programs with by-value class parameters, on vs off, both backends) and
`jaiscript_fuzz --no-param-alias`. Measured (vm): a serial `xy(Vec3)` call 430 -> 113 ns,
a struct with an array field 830 -> 476, the zip by-value helper 7.1 -> 2.5 ms per 40k
(level with by-reference); GLOOM neutral (its helpers already take `var&`).

### Read-only by-value locals

`T x = lvalue;` in a plain function (T a script class, an array or a map; the initializer a
name, member or subscript chain) deep-copies its source; the copy goes when nothing can tell
(`prove_local_aliases`, the same walk in `proof_mode` with a `local_mode` on top):

- the source sits in a by-value tree: its chain roots at a by-value local, parameter or
  program-scope variable and every step is statically a field, element or value held by
  value (never a handle, `var` or reference), and its static type is exactly T;
- the statements after the declaration in its block are walked at depth 0 with the
  function's locals visible there, x as the root (the parameter rules: reads through
  primitive/string terminals and read-only builtins only), and every function they call is
  walked as the parameter proof walks its callees; a store into x itself also escapes it;
- nothing in that range may write the source: a write rooted at a local lands only in that
  local's own value tree (a by-value chain from a by-value local other than the source's
  root) or replaces a local's slot; a write through a reference, a handle or a loop variable
  escapes, and a callee's reference parameter defers to its call sites, which judge the
  argument they pass;
- the destructor rule of the parameter proof, per type: T may not run a script destructor and
  no value whose type can run one may die in the range.

The mint stamps every candidate declaration of a plain function with its home (parameters,
return type, body; `variable_decl::alias_home`); the declaration's first run in a definition
epoch proves every candidate of its home and stamps the home's verdicts (a graft's copy of
the declaration reads its original's verdict). At run time a proven
declaration whose value's type is exactly T and passes the argument-side gate
(`parameter_alias_allows`: not nocopy, not a host object) binds the source's object instead of
the clone, on both backends at the one clone site of the declaration. Declarations in a
switch's cases, in methods, lambdas and coroutines keep the copy. Kill switch
`engine::local_alias(false)` (runner `--no-local-alias`); counter `engine::local_alias_binds()`;
gates `parameter_alias_differential_gate` (the read-only local band always on, both switches
flipped) and `jaiscript_fuzz --no-param-alias` (both switches; `--local-alias-band` forces the
band).

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
