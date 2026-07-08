# Aliasing strategy costs: value vs `T&` vs `shared_ptr<T>`

What each way of holding/passing an object costs, measured. Semantics background:
`reference_model.md` (what a reference IS), site guide ch. 3 (value semantics + the
escape hatches). Benchmark rows: `source/tests/performance/benchmark_suite.cpp`
("Aliasing ..." rows + the three BST rows), gated so every variant computes the same
result.

**Methodology** (per `invariants.md` §7): Release BENCHMARKS, jaibite-precompiled
bodies, integer µs/row, **min-of-5** full runs per backend, i7-6920HQ, 2026-07.
Rows are 200 script calls each; per-call = row / 200. Deltas under ~5% are below the
harness noise floor — treat them as ties.

## Benchmark A — read-only compute (no aliasing *needed*)

`power()` aggregates over a meaty `Creature` (string name, 8-int `stats` array,
4 int scalars): 2 muls, a div, a range-for over `stats`, 7 field reads. 200 calls/row.

| strategy | interp µs/row | vm µs/row | interp/call | vm/call | vs `&` |
|---|---|---|---|---|---|
| pass `Creature c` (by value) | 1605 | 1324 | 8.0 | 6.6 | **1.29× / 1.38×** |
| pass `Creature& c` | 1245 | 956 | 6.2 | 4.8 | 1.0 |
| pass `shared_ptr<Creature> c` | 1270 | 980 | 6.4 | 4.9 | ~1.02× (noise) |
| method call, held in plain local | 1146 | 948 | 5.7 | 4.7 | — |
| method call, held via `auto&` alias | 1163 | 945 | 5.8 | 4.7 | — |
| method call, held via `new Creature(...)` | 1156 | 947 | 5.8 | 4.7 | — |

Readings:

- **By-value's premium is the deep copy, ~1.8 µs/call for this object on both
  backends** ((1605−1245)/200 and (1324−956)/200). It scales with the payload —
  a bigger stats array or more strings makes it worse, a scalar-only struct makes
  it cheap.
- **`&` and `shared_ptr` are call-boundary ties.** No measurable deref-per-access,
  refcount, or upcast tax in a field-read-heavy body (handles are non-atomic
  `strong_ptr`; a handle copy is a pointer copy).
- **Method calls don't care how you hold the receiver** — value local, `auto&`
  alias, and shared_ptr are within noise of each other on both backends (methods
  bind `self` by reference regardless).

## Benchmark B — mutation required (aliasing is the point)

Minimal mutating call (`heal(x, amount) { x.hp += amount; }`, 200 calls/row) and the
node-aliasing structure benchmark (BST, 15 nodes, insert+sum, each strategy written
in its own idiomatic shape).

| shape | interp µs/row | vm µs/row |
|---|---|---|
| `heal(Unit& u)` ×200 | 345 | 277 |
| `heal(shared_ptr<Unit> u)` ×200 | 353 | 287 |
| BST via `RNode&` params (in-place) | 285 | 248 |
| BST via shared_ptr nodes (`new`, return-and-reassign) | 375 | 337 |
| BST by value (naive; clones subtrees per pass) | 979 | 902 |

Readings:

- **Single mutating call: `&` vs `shared_ptr` is a tie** (≤4%, under noise, both
  backends). Pick by semantics, not speed.
- **BST: `&` in-place is ~1.35× faster than the shared_ptr shape** — the delta is
  the *shape* the handle style encourages (`root.left = insert(root.left, v)`:
  a field store + return per level), not a per-access penalty (heal parity pins
  that). Both aliasing strategies crush by-value (**~3.5×**), whose cost is
  subtree deep-copies at every recursion level.

## Decision guidance

- **Plain values (default)** for scalars, strings (O(1) shared copies), small
  structs, and anywhere you *want* snapshot semantics. Stop when the object
  carries containers and sits in a hot path — the per-call deep copy is the
  single biggest avoidable cost in this study.
- **`T&` params / `auto&` decls** when a function needs to see or mutate the
  caller's object: cheapest aliasing, and the fastest shape for in-place
  algorithms. Constraint: references are *aliases, not storage* — plain
  assignment stores a value through them, and a reference can't live in a field
  or container. `&` can't be your data model.
- **`shared_ptr<T>` (`new T(...)`)** when identity must outlive the call:
  fields holding objects, containers of entities, registries, trees/graphs where
  nodes hold nodes. Costs the same as `&` at call boundaries; assignment shares
  instead of deep-copying; the only measured tax is handle-churn shapes (~1.35×
  vs in-place `&` on the BST).

Rule of thumb: **values by default; `&` when a call needs the real thing;
`new`/`shared_ptr` when the alias needs to live somewhere.**

## Surprise check (Dev protocol: structural vs improvable — verdict first)

**No major surprise.** The plausible suspects (per-access deref through the
holder, refcount bumps on call paths, upcast checks making shared_ptr method
calls dearer than `&`) do not materialize above the noise floor on either
backend: shared_ptr tracks `&` within ~2–4% on read-compute, mutation, and
method-call rows. One smoke run showed sp-held method calls at 1680 µs vs ~950 —
it did not reproduce in any of 5×2 measured runs (min 947 vs 945/948): the ±50%
first-run variance the methodology warns about, not a signal. The two real,
stable deltas are both **structural and already documented**: by-value's deep
copy (that *is* value semantics — the escape hatches exist for exactly this) and
the BST handle-shape delta (algorithm shape, not holder overhead).

## Suite retune (2026-07, this study)

- `chaiscript_comparison.cpp`: `binarySearch` / `bubbleSort` retuned to
  `array<int>&` params — the old `auto arr` spelling deep-cloned the array per
  call (per recursion *level* for binarySearch) against reference-semantic
  ChaiScript arrays, measuring the clone instead of the algorithm. Gates added
  (found index 5; sorted-in-place `[0] == 1`). Fairness: ChaiScript side already
  passes by reference natively — both sides now do the same work.
- The deliberate naive rows are kept and labeled: `BST ... [naive by-value]`
  in `benchmark_suite.cpp` and the JaiScript side of every comparison suite's
  "Pure Script" BST pair — they are the feature-cost number for value
  semantics, not the idiom.
