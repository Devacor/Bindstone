# Parallel execution design (`parallel_for` on one engine, thread_storage pads)

`parallel_for` (including zip), `parallel_transform` and `parallel_reduce` are implemented with
captured reads, element members, construction, C++ calls that are parallel unless registered serial
(the library's own builtins are serial apart from pure math and `to_string`, §3) and random draws
from a per-region counter stream (§3). Safe mode proves a body or rejects
it; nothing silently runs serial for safety, though the cost cap may run a cheap small range on
one thread. Worker count does not change the semantic admission rule, and any merge over
per-worker results combines in iteration order. `thread_storage` pads, binding annotations, the
command buffer and print buffering are proposals (§1, §5, §8). The language guide holds the
user-facing surface.

## 0. Ground truth: script values can never be shared live across threads

`strong_ptr` is **purely non-atomic** — `core/strong_ptr.hpp` ("Purely non-atomic for
maximum single-threaded performance"; plain `size_t` strong and weak counts; the class doc:
"Thread safety is the caller's responsibility (critical sections, partitioning, etc.)"). Every heavy `script_value` alternative (array/map/string/object) rides it. Consequence
that drives this whole design: **reads are writes**. Copying a handle bumps a non-atomic count,
and script-level reads produce handle copies constantly — so "the body only reads `a`" is NOT a
safety argument by itself. Two threads touching the same live value is a data race, full stop.
Safety must come from partitioning: each thread's reachable set is exclusive, or it got a clone.

The matching asset: **zero static state**. Every `script_value` carries an `engine*`
(invariants.md §1: "`engine_` and `type_info_` are **the language**"); no non-const
statics/thread_locals anywhere (`Source/JaiScript/AGENTS.md` code rules). All engine-shared machinery is
instance state, so "what a worker may touch" is enumerable and gateable.

## 1. One engine, the Emitter pattern

There is **ONE engine**. No worker-engine mirrors, no jaibite provisioning, no per-worker
prelude state for `parallel_for` (the isolate-pool tier is the appendix). The model
is MV's `Emitter` particle threading (`Source/MV/Render/Scene/emitter.h`): a
`std::vector<ThreadData> threadData` indexed by group, each worker writes **only**
`threadData[a_groupIndex]`, shared state is read-only during the region, and the main thread
merges the pads after the join (`loadParticlePointsFromGroups`).

Not built: `thread_storage` pads. The sketch below shows the intended surface.

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
state**: the body's locals, `thread_storage` once built (inside the body it would name the
executing thread's own pad), and — when iterating by reference (`parallel_for (auto& x : arr)`) —
the thread's **own loop element**, mutable in place (§11 Q4: one thread runs each element (§3) and the
barrier element proof makes the element that thread's exclusive property). Any other store — global, outer local, shared
object field, and transitively through called functions — is an error ("parallel_for: body
writes enclosing state 'g' at line:col"). One rule buys transitive
purity: a callee that mutates a global or a captured map hits the same wall, so there is no
"pure function" annotation, coloring, or whole-program analysis.

Proposed: after the join, `thread_storage` is an ordinary array of per-thread pads (size = worker
count; the `thread_count()` builtin), readable and mergeable by the main thread like any array.
Pads are **per-`parallel_for`**: fresh (empty) at each loop's fork; contents persist after the
join until the next `parallel_for`. Each pad starts as an empty map; the body writes fields onto it.

With pads and the command buffer built, a `parallel_for` would have exactly **two output
channels**: the pads (data results) and the ordered world-write command buffer for annotated C++
bindings (§5). As built, its output is its own loop elements (and the zip's further arrays).

No `async`/`await`, no detached threads, no handles to leak: strictly structured fork-join. The
calling thread parks at the barrier; workers fan in.

## 2. Per-worker execution contexts inside the one engine

One shared engine means the engine's internals are shared, and outside a region they assume a
single executing thread. Each worker therefore runs in its own execution context, acquired at
the barrier (reused from the engine's slot pool while the provisioning fingerprint matches,
rebuilt otherwise; `parallel_transform.cpp` header). All of it is bound by the
zero-ambient-cost invariant (§11 Q3): context setup/teardown is charged to the region, never to
sequential execution.

- **Per-worker execution state**: each worker has its own backend instance (the engine's
  configured type), a root environment with no parent (a lookup that would escape fails as
  undefined), a private env-epoch sink and per-worker copies of every callable the body needs.
- **Symbol interner and type tables**: `string_symbolizer` is engine-bound (it carries the
  engine-wide env-shape epoch, `env_epoch`); every name and type shape a worker can need is
  interned at admission and the barrier, then the interner and type tables freeze for the region.
- **Parse cache / class definitions / vm chunks**: frozen for the region (no defines, no hot
  reload mid-loop); the write wall (§4c) forbids the script-visible mutations.
- **Budget/memory accounting**: each worker has its own `execution_limits`
  (`detail/execution_limits.hpp`); `engine::execution_limits()` resolves to the calling
  worker's accounting through the installed region table, so one worker's allocation bomb
  raises on that worker (§6).

## 3. Scheduling: shared blocks, results keyed by element (determinism)

Without a weight hint the range is cut into blocks of `max(1, n / 8W)` consecutive elements
(`detail/parallel_schedule.hpp`). Each of the W threads starts on its own first block, then
claims the next unclaimed one with a single atomic `fetch_add` on the region's block counter, so
blocks go out in increasing order to whichever thread finishes first and a slow or busy core
takes fewer. The claim takes no lock; a claim costs one cache-line transfer (about 150 cycles,
flat from 2 to 6 workers: the counter does not contend at this grain), so per-worker
work-stealing deques would buy nothing for a flat index range. With the optional weight hint
(§11 Q1), contiguous chunks split by cumulative weight, assigned statically.
`engine::last_parallel_chunk_bounds()` publishes the cumulative element counts each thread ran.

Results are keyed by element, never by thread: each element's result and in-place writes land in
its own slot; chunk-created instances register, failing destructors report and index writes come
back in element order whichever thread ran the element; a failing region reports its
lowest-index failing element (§6). Same input + seed ⇒ byte-identical results at every worker
count — replayable, smoke-hashable; determinism is the testable guarantee (§8 Testing). What is
not deterministic is which thread ran an element and, when a region fails, which elements past
the failing one had already run (their in-place writes stay) — and so whether a region near
`engine::memory_cap` trips, and at which element (§6).

**Random draws.** `math::random`, `math::random_int` and `math::random_range` in a body (or in a
function, method, constructor or field initializer it reaches) draw from a counter-based stream, not
the engine's: a region whose admitted code can draw (`parallel_admission::draws`) takes one draw
from the engine stream at the barrier as its seed, and each draw is `region_counter_draw(seed,
element, k)`, splitmix64 over the element index and the draw's number k within that element
(`math_rng_state::next_in_region`; the element is the chunk cursor's index, a chunk for
`parallel_reduce`). `engine::math_rng()` answers a chunk's thread with its stream through the region
table. A region's draws and the engine stream after it are therefore identical at every worker count,
and the stream advances by exactly one draw per region that can draw (none for a region that cannot,
or for an empty range). The values differ from the serial loop's sequence by design: the serial loop
interleaves its draws with the order elements run, which a parallel region does not have.
`math::random_seed` in a body is refused at admission (`math::random_seed cannot be called in a
parallel body ...`); a seed inside the region would have no order to apply in.

**Thread count and the pool.** `engine::parallel_thread_count()` is the total context count
(main runs chunk 0): the override when set, else `min(hardware_concurrency - 1, 6)`.
The pool threads are pre-allocated once per engine, sized to that count minus main, and reused
by every region; a region only pays a submit, a wake and a join per pool worker. Because the
partition never changes a result, the count woken is a pure cost knob, and both sides of it
are measured on the machine at hand: when the pool is built, three no-op round trips give the
fixed per-worker cost (`worker_overhead_ns`, the minimum), and each
statement's region pool keeps the smoothed ns per element of main's own chunk
(`ns_per_element`). A statement's first region fans out to every thread with an even split
(the baseline); after that a region wakes only as many pool workers as can each be handed a
chunk worth `k_parallel_chunk_overhead_ratio` (3x) the wake cost. Every region of two or more
elements (`k_parallel_small_n`) takes this path; there is no separate small-count cutoff.

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
freezes it before the fork. Every name a worker can probe is
interned at admission, single-threaded: member, getter and setter ids of every chain the body
names and of every method-call site (both backends probe the user-accessor id of a method name
before dispatch). A miss on the frozen interner is Debug's assert, the tripwire.

**Handle elements and index writes.** There is one mode, and it proves. A top-level element that is a
`shared_ptr` or a `weak_ptr` hands its object to the distinctness check (`repeated_handle_object`: one sort of
(object, element) pairs over the range and the zipped arrays), so one worker reaches each object; the object's class
is still walked as a flat value class. A nested handle still needs to be its object's only holder. `out[i] = v` with
`out` captured and `i` a loop variable the body never writes is an index write (`parallel_admission::index_writes`):
admission proves every touch of `out` is `out[i]`, the barrier proves the `i` values distinct, each worker writes a
fresh private copy (`parallel_capture_kind::written`), and the join copies each element's slot back from the worker
that ran it, in element order up to the first failing element.

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
body cannot retain it past its iteration (no lambdas, stores dereference, captured
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
element).

**parallel_transform is the zip.** `parallel_transform(xs, fn)` is
`parallel_for (auto& x, auto& o : xs, out) { o = fn(x); }` over a fresh output of `xs.size()`
nulls, and nothing else: one region core (`run_region`) runs both statements, the output is
the zip's second array (allocated by the call, so it skips the exclusivity walk the caller's
arrays get), the result is the ordinary store of a call result (a temporary; the return-site
rule already copies anything fn could name, so objects come out and never alias the source),
and there is no per-element sink, opcode or result contract of its own. When fn only reads its
one by-value parameter (the admission verdict below) and has no return inside a nested loop,
there is no call either: fn's BODY is grafted into the chunk loop at first use - the
body cloned through the jaibite serializer, its parameter bound by reference to the source
cursor (the in-place rule), each top-level `return e` rewritten to `{ o = cast<T>(e); continue; }`
with the cast dropped where the checker's return proof already says the value is the declared
type (`auto`/`var` returns store as is), a bare `return` to `continue` - built once per fn
(cached on the engine, keyed by the body) and compiled once per worker like any chunk
wrapper. A fn that mutates its parameter, returns from a nested loop or takes a weight
partner keeps the call form (`o = __jai_parallel_fn(x)` in the same loop), so a mutating fn
still never touches the source. Instances a pool worker constructs or copies do not register in the class
registry during the region: they park in the worker's pending list (`pending_instances`) and
the join registers the survivors (a copy that died meanwhile just expires; hot reload migrates
everything that lived). Cost: the transform is the inline zip plus the fresh output.

**Construction in a body.** `T(args)`, `T x(args);`, `T x;` and `new T(args)` record the class by
name at admission; the barrier resolves each against the live class (`admit_region_classes`) and
walks, as members of the new object's class (its fields writable, everything else under the body's
rules), every constructor the argument count admits, the base constructions each initializer list
names (an unnamed base default-constructs), each level's field initializers, and the destructor of
every class whose instance can die on a chunk: constructed classes, element classes, the classes an
index-written array holds, their fields' classes (an untyped field: every class) and subclasses.
Class code may call script functions and the whitelisted host functions; their worker copies live in
the method sandbox. A host constructor and destructor run code no walk sees, so the binder vouches
for them: safe on any thread by default, refused with every script class over the host class when it
is bound `.serial_only_constructor()`. A worker gets a copy of each host constructor a construction
reaches (`make_parallel_host_function_copy`), and a hybrid's script layer joins the engine's
script-layer map at the join (`pending_script_layers`). C++ calls are parallel unless registered serial
(`serial_function`, `serial_method`, `serial_static_method`; the library's builtins register serial
under `engine::library_registration` unless `parallel_pure_function`). A free function or static method
refuses at admission; a method's class is a runtime fact, so every class whose chain binds a called name
in C++ gets a worker pin (a copy of the bound value), and a serial one's pin raises when called. Receiver
safety follows constness, per overload by arity (`class_definition::host_overloads`): a non-const one or a property
store runs only on an instance whose `region_tag` is this region's (a range or zipped element, or a reduce accumulator,
the element proof found backed by a C++ object it proved the only element of; or a constructor's or copy's object made on
this chunk), a const one on any. A method's serial mark covers its non-const overloads unless it says
`serial_overloads::every`.
A serial-only chain's C++ object that dies on a chunk anyway
(`class_instance::defer_serial_native`) waits in the chunk's `deferred_releases`; the join releases them on the calling
thread by element, then in death order (`release_deferred_in_order`). Each worker gets a constructor value of its own for every
constructed class, defined in its root env and method sandbox, whose definition environment is the
sandbox; constructor and destructor bodies compile on each worker at the barrier; every memo
construction fills (instance plan, defaults, destructor and construction-order memos, implicit
default constructors, literal-chain verdicts) is filled at the barrier, and a region's construction
copies a shared default detached (`region_field_default`). A destructor that runs on a chunk runs on
that chunk's backend in its sandbox (the region table entry carries both), and so does any script a
chunk enters through a host trampoline (`engine::calling_backend`). An index-written array that
holds objects is copied per worker by sharing its slots' handles: each handle's count moves on one
thread only, the slot's owner during the region and main at the join.

**The zip form.** `parallel_for (auto& v, auto& o : vecs, outs) { o.x = v.x * v.y; }` walks
further arrays of the same length in lockstep: a thread owns the same indices of every array, so
the pre-allocated output struct is written in place - no construction, no result marshalling, no
registry (the emitter pattern: `outArr[i] = result`, which the cursor IS). Each further
array is proven exclusive like the range (whole reach: the body may write anything through its
loop variable; a typed primitive kind is demoted like the range, an object kind addressed in
place), gets its own chunk cursor, and the one advance opcode steps every cursor. Arrays of
different length, the range itself, or a non-array are load-time errors
(`parallel_for: zipped array 2 has 1 elements, the range has 3 (zipped arrays must have the same
length)`). A by-value loop variable keeps its copy per iteration unless the body only reads (the
in-place rule above, decided jointly for all loop variables). The jaibite format carries the
extra loop variables and arrays.

**Body control flow.** The body is the chunk loop's body: a top-level `continue` reaches
the loop's advance on its own and a top-level `return` (any value) is rewritten to `continue`
once, at body-function creation - both mean the next element on both backends; a loop-level
`break` is rejected at admission (`parallel_for: 'break' is not allowed in a parallel body
(iterations are independent; use 'continue' for the next element) at line:col`); inside a
nested loop or switch break/continue keep their C++ meaning, and a `return` inside a nested
loop selects call mode for that body.

## 4. Read safety under non-atomic refcounts

Three mechanisms, one per hazard class:

- **(a) The iterated container** — one thread per element (§3) + the barrier element proof
  (`prove_elements`, §14). At the fork (a single-threaded moment) the proof establishes that each
  element's reachable strong_ptr control blocks belong to that element alone ⇒ each thread's
  share is its **exclusive reachable set** ⇒ zero-copy element reads, no atomics — the
  partitioning discipline `strong_ptr` was designed for. Engine-internal aliases clone and
  shared strings detach at the barrier; an alias the proof cannot dissolve rejects.
  Value semantics makes alias-freedom the common case (assignments and by-value
  parameters copy), so aliases exist only where someone made one on purpose. References are
  owner-pinned handles (`reference_holder`, internal/reference_model.md) whose strong_ptr
  refcounts are NON-ATOMIC and whose pins alias engine-owned state.
- **(b) Everything else read from outer scope** (captures, globals, config objects) —
  provisioned per worker at the barrier (§13b: scalar copy, string copy, zero-copy borrow, or a
  deep snapshot); no lock is taken during the region.
- **(c) Writes** — checked at the store chokepoints (slot/symbol/ref stores) via the executing
  context's parallel flag, in both backends through a shared `detail/` kernel (invariants.md
  §6 — parity by construction, not twins). The check admits stores to **own** state — the
  body's locals and the thread's own loop element (§11 Q4) — and rejects everything else.

## 5. Bound C++: the phase discipline (three-level binding annotation)

Not built: the annotations and the command buffer. As built, a parallel body may call only the
pure-math host whitelist (`parallel_host_whitelisted`); every other host function, `print`
included, is rejected at admission. The proposal: bound functions/methods get a registrar-level
thread annotation; unannotated bindings raise at the call site inside a parallel body (the
binder has no thread-safety notion — every level is a host promise):

- **`self_only`** — touches only its receiver. Callable in parallel bodies; the receiver is
  chunk-owned or pad-owned, so it inherits the partitioning safety.
- **`read_world`** — reads world state but writes none. Callable during the parallel phase
  precisely because **nobody writes world state during it** — the phase discipline, not
  per-object locking, is what makes the reads safe.
- **`write_world`** — NOT executed inline. The call and its arguments (captured by clone at
  enqueue) go into an **ordered command buffer**, applied serially in **ITERATION order** after
  the join. World effects stay deterministic whichever thread ran each element, and the world never mutates
  under a reader.

So the frame shape is: **parallel phase** = read world + write own pad/self + enqueue;
**serial phase** = apply the queue in order. Script-side world writes stay flatly forbidden
(§1) — the command buffer is the only sanctioned world-mutation channel out of a region.

Honest cost: annotating the ~76 existing registrar sites (dynamic_binder methods/properties
across MV/Bindstone) — mechanical but real, and each annotation is a reviewed claim.

## 6. Errors

Any iteration's throw fails the whole `parallel_for`: workers finish or abandon their chunks,
the command buffer is discarded, and the join re-raises the **FIRST error in ITERATION order** —
deterministic at every worker count: a thread stops at its first error, blocks are claimed in
increasing order, so every element below the least failing one ran to its end, and the join takes
the minimum over the threads. The terminal rails apply per worker: `engine::execution_budget` and
`engine::memory_cap` with the terminal-error latch (`execution_limits::terminal_error` in
`detail/execution_limits.hpp`; memory-cap exhaustion allows one script catch per execute, then
latches terminal) — a livelocking or allocation-bombing body fails the loop, not the process.
Each worker may spend the whole allowance left at the fork and the join charges their sum, so a
worker trips on what its own elements allocated: whether a region close to the cap fails, and at
which element, follows which elements each thread ran. Charging in element order would not make
it exact: the vm charges frame storage to whichever element first grows a worker's stack, and a
body may catch a cap error and go on, so the serial loop's failing element is not recoverable
from per-element totals without recording every charge site.
(A command buffer, were it built, would be discarded.)

## 7. Threads are `jai::thread_pool`

JaiScript owns the pool: `include/jaiscript/thread_pool.hpp` (shared queue + pinned
`submit_to`, public, its body in `source/implementation/thread_pool.cpp`) is the `parallel_for` substrate. Standard C++ only
(`std::thread`/`std::mutex`/`std::condition_variable`), no OS APIs — JaiScript stays the base
library. A host uses the same class for its own work: `submit(task, then, where)` and
`submit_batch(tasks, then, where)` continue on a worker or posted, and `post` / `run_posted` is the
queue the host's own thread drains (MV's pools are instances of it). The parallel regions use only
`submit_to` / `submit_pinned_batch` (the library's `detail/thread_pool_access.hpp`) and their own pool instance.

## 8. Kept invariants

- **No script-visible locking primitives, ever.** A mutex in a game script is a deadlock a
  designer ships. Coordination is the join (plus, if built, the pad merge and command buffer),
  nothing else.
- **No `yield` in parallel bodies** — error. Coroutine fibers/continuations cannot resume
  coherently across a parked barrier.
- **`print`, `math::random_seed` and `time::` are rejected at admission** (random draws run on
  the region's counter stream, §3). Proposed: `print` buffered per thread and replayed in iteration
  order at the join (the fuzz harness byte-compares printed output).
- **Bound C++ is callable only from the pure-math whitelist** (§5); everything else raises at
  admission.
- **Testing: determinism IS the contract.** The differential fuzz harness runs one generated
  program on both backends and byte-compares result + printed output + error text +
  engine-alive-after, fully seed-deterministic (`source/tests/fuzz/fuzz_harness.hpp`, a
  splitmix64 rng). Parallel bodies are pinned by 1-vs-N worker-count determinism tests on both
  backends. Divergences are triaged as suppressions in `fuzz_harness.hpp`
  (`known_suppressions`).

## 9. Rejected

- **Shared-memory script threading** (atomics/mutexes on script values): atomic refcounts tax
  every copy in every single-threaded script — the copy is THE hot operation of the VM; fences
  also poison the pooled call records, env-lookup caches, and
  interned-symbol machinery that assume single-threaded mutation. The payoff is a programming
  model (locks, ordering, torn invariants) game scripts can't debug.
- **Multiple engines as the `parallel_for` substrate**: mirror maintenance on every
  hot reload, jaibite re-provisioning, engine-scoped class definitions forcing clones of every
  instance, and N engines' worth of memory — all to get isolation the write wall + barrier
  provisioning give on one engine. Isolates survive only as the appendix.

## 10. Honest costs & applicability

- **Per-thread execution contexts (§2) are the bulk of the engineering**, not the loop itself.
- **The barrier element proof is O(reachable) over the container at every fork** (unless the
  declared type proves exclusivity by construction) — cheap for flat arrays of primitives, real
  for deep object graphs.
- **Captured state is provisioned per worker at the barrier** (§13b); no lock is taken during
  the region. A snapshot costs one deep copy per worker, memory_cap-charged.
- **A weight-hinted region is a static partition and can load-imbalance** (one expensive share
  gates the join); the hint costs an O(n) single-threaded evaluation at the barrier. Hint-less
  regions balance through the shared blocks (§3).
- **Registrar annotation sweep** (~76 sites, §5), if annotations are built.
- **Applicability, honestly**: in Bindstone's actual frame profile, script update is not the
  hot path — pathfinding, spine, and particles are, and those parallelize in C++ behind
  bindings (the Emitter already does) without touching script semantics. `parallel_for` is a
  language capability play more than Bindstone's cheapest frame-time win; size the effort
  accordingly.

## 11. The four design questions

The one open sub-item is a weight-hint spelling for the `parallel_for` statement.

### Q1 — chunk grain: shared blocks + optional per-index weight hint

> Question: exactly `n / thread_count()`, or a finer fixed grain (still statically assigned) to
> soften imbalance? Language default with an optional hint, or explicit in the syntax?

Default: blocks of `max(1, n / 8W)` consecutive elements claimed in order by whichever thread is
free (§3), no tuning knob. The imbalance answer beyond that is an **optional per-index weight hint**
— for example, parsing an array of JSON strings, weight by text length so chunks balance by
**effort**, not element count. Semantics when the hint is supplied:

- The weight expression evaluates **per element at the barrier** — single-threaded, before
  fan-out — and contiguous chunk boundaries are chosen to equalize cumulative weight.
- The O(n) weight evaluation is paid only when a hint is present; hint-less loops keep the flat
  split at zero added cost.
- The hint expression must be **side-effect-free** — the same rule as the body's read
  discipline; it runs under the barrier's single-threaded safety anyway, but a mutating hint is
  still an error.

The `parallel_transform` builtin sidesteps the spelling question — the optional weight hint is
the third argument: `parallel_transform(arr, fn, weight_fn)`. The `parallel_for` statement has
no weight hint. Candidate spellings for one (a really easy OPTIONAL syntax):

1. Inline `weighted` clause — `parallel_for (auto& x : arr weighted x.size()) { ... }`. Pro:
   the hint sits right next to the loop variable it uses; con: a contextual keyword inside the
   range clause.
2. Trailing `by` clause — `parallel_for (auto& x : arr) by x.size() { ... }`. Pro: the range
   clause stays untouched; con: the hint dangles between header and body.
3. Second header expression — `parallel_for (auto& x : arr; x.size()) { ... }`. Pro: pure
   punctuation, no new keyword; con: least discoverable, and the semicolon reads C-style-for.

### Q2 — iteration domains: arrays only

> Question: arrays only, or also maps/ranges? (Maps make chunk boundaries and the alias walk
> hairier.)

Arrays only. Maps/ranges are not supported; the parenthetical above is the reason.

### Q3 — captured reads: provisioned at the barrier

> Question: a first-touch lock+clone, or snapshot the captured set up front at the barrier?

The barrier provisions every captured name the admission walk collected (§13b); the borrow tier
makes the proven read-only case cheaper than any gate. The hard constraint:

> **INVARIANT — zero concurrency cost outside `parallel_for`.** No lock, atomic, or
> synchronization consideration may exist anywhere in the engine except while a parallel region
> is actively executing; sequential code paths must be provably untouched. This binds the
> per-thread execution contexts (§2) too: their setup/teardown costs belong to the region,
> never ambient.

### Q4 — own-element mutation: yes

> Question: may a body mutate its OWN loop element in place (`for (auto& x : ...)` style)?

Yes — `parallel_for (auto& x : arr)` may mutate its own loop element in place; under the static
partition + the barrier element proof the element is the thread's exclusive property. The write
wall's definition of "own" = its own loop element (and the zip's same-index elements) +
body-locals.

## 12. Sequencing sketch

1. **`jai::thread_pool` lands** (DONE — `thread_pool.hpp`, §7) with pinned-worker mode + foundry coverage.
1.5. **`parallel_transform` v0 lands** (DONE — the builtin ahead of the statement:
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

## 13. Captured reads — read enclosing state, proven read-only

A parallel body may READ enclosing names: globals through the environment chain, and the
locals of the function its statement runs in (the parser records their frame slots on the
statement, `parallel_for_stmt::frame_captures`, and the barrier provisions from that frame;
a global never shadows a local of the same name). Writes to enclosing state are
errors in every shape, always (contract A). Implementation:
`source/implementation/parallel_transform.cpp` (admission + barrier + the borrow kernel),
`core/value.hpp` storage index 15 (`TYPEID_PARALLEL_BORROW`, `parallel_borrow_tag`), both backends' subscript
twins call the ONE shared kernel `detail::parallel_borrow_subscript_read` (parity by
construction, invariants.md §6).

### 13a. Two-layer design: cheap admission + runtime write wall

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
  (`parallel_borrow_tag`, storage index 15) whose copies touch no refcount; element reads
  mint worker-local values — a primitive is read from the buffer, a string read copies
  the BYTES into a fresh worker-owned string and never touches the live handle's count.
  **Zero copies at the barrier, zero refcount traffic, zero memory_cap charge.** GLOOM's
  bit-packed int64 grids and its glyph and palette tables ride this tier: the per-read byte
  copy of a short escape string is far cheaper than copying hundreds of table entries per
  worker per region.
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
banned; iteration-order error selection, budget rails, and 1-vs-N determinism hold and are
pinned over capture workloads.

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

Use captured reads for shared inputs and the pair idiom for per-chunk inputs/outputs;
`thread_storage` pads (§1, not built) are the proposed answer for accumulation across iterations.

## 14. Element members — a body reads (and stores) members of its own element

A `parallel_transform` body may name members on chains rooted at its element parameter
or any worker-private local: `fn(Node n) { return n.first.a.v + n.xs[1] + n.counts["k"]; }`.
The parameter is the worker's private by-value copy, so a store into it (`n.x = 5`,
`n.x++`) is legal and invisible to the source. Chains rooted at a captured name keep the
admission rejection (`member access reaches captured state 'g' (captured reads are
read-only, and objects cannot be captured)`); methods on the element stay behind the
worker method wall in the transform (the barrier admits element methods for `parallel_for`
only), and `this`/getters/setters are rejected. The admission walk is ONE walk
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
the members its body names. Rejections name the offending leaf:
`parallel_transform: element N: value is not value-semantic (object | shared_ptr |
weak_ptr | function | bound host object | ...)`. Results may be objects: the transform's
result is the ordinary store of a call result, a copy independent of the source (§3, "parallel_transform is the zip").

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

## 15. Read-only by-value parameters bind by handle (the admission walk as a purity proof)

A by-value class/array/map parameter costs a deep clone per call. The clone exists to make the
callee's copy unobservable to the caller and vice versa - so when nothing can observe it, it
goes. The proof reuses the admission builder in `proof_mode`
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
Only plain functions are candidates (methods and coroutines bind through
other paths); parallel workers never prove (interning, global lookups) - the barrier proves
the live function and restamps every worker copy per region entry. Kill switch
`engine::parameter_alias(false)`; gates `parameter_alias_differential_gate` (generator
programs with by-value class parameters, on vs off, both backends) and
`jaiscript_fuzz --no-param-alias`. A proven by-value helper costs what a by-reference one does.

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

NOT the `parallel_for` substrate. A sketch for long-running background work
(pathfinding batches, procedural generation) if it's ever needed:

- N pinned worker threads, one engine per thread for the thread's lifetime (workers accumulate
  prelude state, so tasks need thread affinity).
- Provisioning via the jaibite binary path: `jaibite::save_bytes()` on the source engine,
  `engine::jaibite_load_bytes()` per worker — symbols re-interned into the loading engine,
  type_info re-interned structurally, VM chunk recompiles lazily; `registration_fingerprint()`
  gives a free staleness check.
- Crossings by value only: `script_value::clone()` or `serialization/binary_archive.hpp`
  round-trip; big immutable assets bind `std::shared_ptr<const T>` into each engine via a
  `.thread_shareable()` binder flag (atomic control block carries lifetime; `const T` enforces
  immutability at the binding surface).
- Per-task containment exists: per-engine budget/memory rails + the terminal latch mean a
  failed task surfaces as a failed future, never takes the pool down.
- Script-class instances always cross by clone — `class_definition` is engine-scoped (symbol
  IDs interned per engine, hot-reload migration machinery).
