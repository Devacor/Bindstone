# Parallel execution design (isolate pool + in-execute parallel_for)

Status: DESIGN ONLY — nothing below is implemented unless it carries a source anchor.
Names introduced here (`jai::parallel_pool`, `parallel_for`, `.thread_shareable()`, `channel`)
do not exist in the tree yet. Everything with a `file:line` anchor was verified against
VM-perf as of this writing. Red-pen freely.

## 0. Ground truth: script values can never be shared live across threads

`strong_ptr` is **purely non-atomic** — `strong_ptr.hpp:29` ("Purely non-atomic for maximum
single-threaded performance. Thread safety is handled at a higher level (critical sections,
partitioning, etc.)"), plain `size_t strong_count/weak_count` at :37-38, class doc :115-130.
Every heavy `script_value` alternative (array/map/string/object) rides it. Consequence that
drives this whole design: **reads are writes**. Copying a handle bumps a non-atomic count, and
script-level reads produce handle copies constantly — so "the body only reads `a`" is NOT a
safety argument. There is no read-only fast lane; two threads touching the same live value is
a data race, full stop.

The matching asset: **zero static state**. Every `script_value` carries an `engine*`
(invariants.md §1: "`engine_` and `type_info_` are **the language**"); no non-const
statics/thread_locals anywhere (CLAUDE.md code guidelines). Two engines share nothing, so
*separate engines on separate threads are already mutually thread-safe today*. Parallelism is
therefore built from engine isolation + explicit crossings, never from shared mutable values.

## 1. Tier 1 — host-level isolates: `jai::parallel_pool` (new)

N persistent worker threads, **one engine pinned per thread for the thread's lifetime**.
Pinning matters: worker engines accumulate prelude state, so tasks must land on a thread whose
engine is already provisioned — a job-hopping pool with no thread affinity doesn't fit.

**Layering flag (source pushes back on the discussed shape):** the discussion said "over
`MV::ThreadPool`" (`Source/MV/Utility/threadPool.hpp:14`; also `asioThreadPool.h`). But
JaiScript is the base library — MV includes JaiScript (signals, engine), never the reverse; a
JaiScript→MV dependency inverts the layering. And `MV::ThreadPool::Job` has no thread
affinity, which the pinned-engine model requires. Proposal: `parallel_pool` owns plain
`std::thread` workers inside JaiScript; MV/Bindstone wraps or forwards to it. Dev to rule.

### Worker provisioning: jaibite prelude

Each worker engine loads the shared prelude (functions/classes the tasks need) via the jaibite
binary path: `jaibite::save_bytes()` on the source engine, `engine::jaibite_load_bytes()` per
worker. Cross-engine relocation is exactly what that machinery was built for — symbols are
written as string-table indices and re-interned into the loading engine, type_info re-interned
structurally, the VM chunk never serialized (recompiles lazily): `detail/ast_serializer.hpp:6-17`,
`engine.hpp:115-129`, landed 80fd619b. `registration_fingerprint()` (engine.hpp:126-129) gives a
free staleness check: a worker whose fingerprint differs from the main engine needs
re-provisioning before it takes tasks.

### Inputs

Per-task snapshots, two grades:
- `script_value::clone()` (value.hpp:164) — deep copy (CLAUDE.md: "clone() = deep copy, copy =
  shallow"), rebound to the worker engine.
- `serialization/binary_archive.hpp` round-trip for anything that must cross as bytes (host VFS,
  future process isolation).

Big immutable assets skip both: bind `std::shared_ptr<const T>` into every worker via
`dynamic_binder`. Each engine gets its *own handle* (per-engine `script_value`s, so no strong_ptr
sharing); the `std::shared_ptr` control block is atomic, so cross-thread handle lifetime is sound;
`const T` makes immutability compiler-enforced at the binding surface. Registration is opt-in via
a `.thread_shareable()` flag on `dynamic_binder` (**new — does not exist**; today's binder has no
thread-safety notion, verified by grep). The flag is the host's promise that `const` methods on T
are genuinely thread-safe (no lazy caches, no mutable members).

### Results

Serialized worker-side (clone or binary archive), deserialized into the main engine at harvest.
API shape: a future per task, plus a `channel` type (**new**) for streaming partial results.
Locks exist only in C++ — the pool's queue and result slots. **No script-visible locking
primitives, ever.** A mutex in a game script is a deadlock a designer ships; if two tasks need
to coordinate, they do it through task boundaries (submit/await), not shared state.

### Per-task containment

Already built (eb4108a2): `execution_budget` and `memory_cap` are per-engine (`engine.hpp:167-185`)
and the terminal-error latch (`detail/execution_limits.hpp:17-54`) unwinds past every script
catch to the host execute() boundary. Precision on "un-swallowable": budget overruns latch
terminal ALWAYS; memory-cap gives the script exactly ONE catchable raise per execute (counter
re-arms so it can free caches), the second raise latches terminal (execution_limits.hpp:57-70,
Dev's ruling recorded there). Either way the worker engine survives and the failure surfaces as
that task's failed future — a livelocking or allocation-bombing task cannot take the pool down.

## 2. Tier 2 — in-language structured fork-join: `parallel_for` (new)

Tier 1 is host plumbing. The language feature is:

```jaiscript
parallel_for (auto x : a) {
    results ← one value per iteration          // ordered bucket, see below
}
```

One engine, otherwise single-threaded as always. At the `parallel_for`, execution fans out to
hidden pool workers, the calling thread parks at the barrier, workers fan in, and per-iteration
results land in **ordered buckets** (indexed by iteration) readable after the join. No
`async`/`await`, no detached threads, no handles to leak: strictly structured fork-join.

### The contract: semantic equivalence

`parallel_for` ≡ sequential `for` with the same body restrictions. Parallelism is observable
ONLY as elapsed time — same results, same error text, same print output, same engine state
after. This is what makes it a language feature instead of a footgun, and it is directly
testable (see Testing).

### Why zero-copy fan-out is even possible

Naive "the body doesn't write to `a`, so workers can read it in place" fails on §0: reads bump
non-atomic refcounts. The rescue is that the fork barrier is a **single-threaded moment**. At
the barrier, run an O(n) alias walk over `a`'s element subtrees, collecting each element's set
of reachable strong_ptr control-block pointers (plus the set reachable from the body's captured
enclosing reads):

- **Disjoint sets** → exclusive-subtree handoff: element i's subtree is touched by exactly one
  worker, so its refcount traffic is single-threaded *per subtree*. Zero-copy reads, zero
  atomics, zero locks — partitioning, exactly the higher-level discipline strong_ptr.hpp:30
  says it was designed for.
- **Any overlap** (element↔element or element↔captured) → those elements deep-clone at the
  barrier. Correctness NEVER depends on the fast path; aliasing only costs a clone.

Value semantics makes alias-freedom the common case: assignment and parameter passing deep-copy
by default (docs/JaiScript_DeepCopyDesign.md — "all assignments and parameter passing create
deep copies"), so aliases exist only where someone made one on purpose (references, ref params,
`for (auto& x : ...)` — `reference_holder`, value.hpp:1581-1583). References additionally
carry `weak_ptr<environment> sourceEnv` and frame-lifetime anchors (invariants.md §3), which do
not survive a thread crossing — a ref reachable from an element forces that element onto the
clone path unconditionally.

### Writes

- **Iteration-locals: free.** They live in the worker's own frames.
- **Per-iteration result: one bucket slot per index.** Disjoint by construction, so lock-free
  without cleverness; the join hands the buckets back in iteration order.
- **ANY write to enclosing state: runtime error** — "cannot write enclosing state in a parallel
  body". Enforced at the store chokepoints (slot/symbol/ref stores) for frames below the fork,
  in both backends via a shared `detail/` kernel (invariants.md §6 — parity by construction, not
  twins). This one rule also buys transitive purity: a called function that tries to mutate a
  global or a captured map hits the same wall at runtime, so there is no separate "pure
  function" annotation, coloring, or whole-program analysis.

### Workers under the hood

Tier 2 runs on Tier 1's pool: hidden worker engines mirroring the program's functions/classes
via prelude jaibites, invalidated by epoch on hot reload (same pattern as the existing
env-shape epoch, `string_symbolizer.hpp:79-83`; mirrors rebuild lazily on the next
`parallel_for` after a reload). Per-element handling by type:

- Primitives, arrays, maps, strings: alias-walk fast path (zero-copy handoff) or barrier clone.
- **Script-class instances: always the clone path.** `class_definition` is engine-scoped
  (symbol IDs, method tables, hot-reload machinery — class_definition.hpp:383-396, migration
  :1077); instances cannot be zero-copy-read from an engine that holds a different definition
  object. Clone + worker-side re-bind against the mirror's definition.
- Bound C++ functions/objects: callable in a parallel body only if registered
  `.thread_shareable()` (§1). Everything else raises at the call site.
- `yield` inside a parallel body: error. Coroutine fibers/continuations are per-engine and a
  parked barrier cannot resume them coherently.
- `print`: buffered per-iteration, replayed in iteration order at the join — required by
  semantic equivalence (the fuzz harness compares printed output, fuzz_harness.hpp:3-5).

### Testing

The differential fuzz harness already runs one generated program on both backends and
byte-compares result + printed output + error text + engine-alive-after, fully deterministic
from a uint64 seed (`source/tests/fuzz/fuzz_harness.hpp:3-6`, seeded splitmix64 rng :35-51).
Extend it with a third leg: generate `parallel_for` bodies (including deliberate aliasing,
enclosing-write attempts, throws mid-iteration), run sequential-vs-parallel on the same seed,
byte-compare. Determinism holds because generation is seed-driven and any future script-visible
rng gets a per-task seed. Divergences log to `known_divergences.md` like today.

## 3. Rejected: shared-memory script threading

Atomics/mutexes on script values were rejected outright. Atomic refcounts tax every copy in
every single-threaded script — the copy is THE hot operation, and the whole VM-perf effort has
been about shaving exactly that path; fences also poison the pooled call records, env-lookup
caches, and interned-symbol machinery that assume single-threaded mutation. And the payoff is a
programming model (locks, ordering, torn invariants) that game scripts demonstrably don't need
and can't debug. Isolation + explicit crossings gives parallelism without charging scripts that
never use it.

## 4. Honest costs

- **Snapshot cloning per task** (Tier 1) and per aliased element (Tier 2). Mitigations:
  `shared_ptr<const T>` for the big read-only assets; chunked `parallel_for` (one worker gets a
  contiguous index range → one handoff/clone per chunk, not per element).
- **Worker mirror maintenance**: every hot reload invalidates N mirrors; rebuild is a jaibite
  reload per worker (lazy, but it's real work and it's on the reload path Dev uses constantly).
- **Alias walk is O(reachable) at every fork.** Cheap for flat arrays of primitives, real for
  deep object graphs; chunking amortizes it but doesn't remove it.
- **Two engines' worth of memory per worker thread** (engine + mirrors + snapshots in flight).

## 5. Open questions for Dev

1. Chunk-size heuristic: fixed count, `n / workers`, or cost-model-per-element? Who owns it —
   language default with an optional hint, or explicit in the syntax?
2. Does v1 `parallel_for` iterate only arrays, or also maps/ranges? (Maps make ordered buckets
   and the alias walk hairier.)
3. `channel` API shape for Tier 1 streaming results — or does v1 ship futures-only?
4. Is `parallel_pool` public host API, or purely Tier 2's private substrate at first?
5. Pool ownership per §1's layering flag: JaiScript-owned threads, or an injected host executor
   interface that MV::ThreadPool can implement?
6. Captured enclosing reads in Tier 2: alias-walk them for zero-copy like elements, or always
   snapshot-clone the captured set (simpler, slower)? Draft above assumes walk-them.

## 6. Sequencing sketch

1. **Pool + isolates** (~1.5k lines + tests): parallel_pool, pinned worker engines, jaibite
   provisioning, clone-in/clone-out futures, containment wiring. Gate: foundry suite exercising
   N tasks × failure modes (throw, budget, memory-cap terminal) with the main engine and all
   sibling futures unharmed.
2. **`.thread_shareable()` + shared const assets** (~300 lines): binder flag, per-engine handle
   binding, call-site enforcement. Gate: concurrent const-method hammering under TSan-equivalent
   stress on a real bound type.
3. **`parallel_for` sequential-semantics first** (~1k lines): parse, both backends, buckets,
   enclosing-write wall, print buffering — running on ONE thread. Gate: full foundry green on
   both backends with parallel_for lowered to sequential; this pins the semantics before any
   concurrency exists.
4. **Alias walk + handoff + real fan-out** (~1.5k lines): control-block set walk, clone
   fallback, chunking, barrier. Gate: the differential fuzzer's third leg (§2 Testing) seed-swept
   overnight, zero divergences; plus targeted alias-torture foundry tests.
5. **Perf pass**: prove the zero-copy path actually beats clone-everything on representative
   workloads (Foundry benchmarks are ±50% integer-µs — invariants.md §7 — so use dedicated
   micro-benches for the walk itself).

Tier 2 ships only after Tier 1's gates; step 3 before step 4 is the load-bearing ordering —
semantics are frozen while everything is still single-threaded and debuggable.
