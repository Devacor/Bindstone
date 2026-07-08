# JaiScript Performance Report

**2026-07, VM-perf branch (HEAD 3528ea86).** The definitive current numbers: both JaiScript
backends head-to-head against Lua 5.4 (sol2), Squirrel 3.x, and ChaiScript 6.1, plus per-op
ladders, aliasing costs, parallel scaling, debugger overhead, and jaibite cache numbers.
Methodology in Appendix A; everything here is min-of-5 (min-of-3 for the long parallel
suite) integer-µs Foundry rows on a quiet machine, full suite green (88 suites, 1844 tests)
on both backends at the measured commit. The 2025-12 tree-walker-era analysis this file used
to hold is preserved in git history (`git show 9c268ffd:Source/JaiScript/docs/PERFORMANCE.md`);
its optimization-history table survives as Appendix B.

## Executive summary

1. **47 ns per VM hot-loop iteration** — the fused 1000-iteration compound loop runs 47 µs,
   holding the VM's 44–48 µs band at HEAD with the debugger compiled in and idle.
2. **Clean sweep vs ChaiScript**: JaiScript wins all 29 live head-to-head rows (the 30th,
   the script BST, stands at ~50× from its historical figure) — typically 8–20× (function
   calls 13×, method dispatch 13×, fib(6) 20×), and that is *after* retuning the ChaiScript
   suite's algorithm rows in ChaiScript's favor for fairness.
3. **12W / 6L / 1T vs Squirrel** (VM backend, 19 head-to-head rows): wins everything
   statement-, container-, string-, and object-shaped; the losses are deep recursion and the
   raw-loop family.
4. **vs Lua 5.4: an honest split, 7W / 10L / 2T.** JaiScript wins or ties statements,
   containers, and null checks; Lua wins loops (~2–4× post-fusion) and above all call-dense recursion —
   **fib(15) is ~9× faster in Lua** (697 vs 76 µs). Structural, diagnosed, and on the
   roadmap (flat-stack VM rewrite — see Known gaps).
5. **`parallel_transform` reaches 4.8× over the serial loop at W=8 on a 4C/8T machine**
   (100k-double map, VM), and the jaibite disk cache makes a 535-line game file load
   **2.4× faster** than parsing it (5.8 ms → 2.4 ms full run; the parse component itself,
   ~3.4 ms, is replaced by a sub-ms binary load).

Standing bonus row: the debugger is free until attached — VM 47/46 µs with the hook compiled
in (pre-debugger band exactly), ~+10% while a session is armed.

---

## The head-to-head: JaiScript vs Lua (sol2) vs Squirrel vs ChaiScript

Columns: JaiScript interpreter / JaiScript VM / Lua 5.4 / Squirrel / ChaiScript, integer
µs/iteration, min-of-5. JaiScript rows come from the same in-suite runs as each rival
(cross-checked across suites: agreement within ±1–2 µs). `—` = no equivalent row in that
suite. One structural caveat up front: **plain rows re-submit source each iteration.**
JaiScript's engine caches parses transparently (LRU), Lua/Squirrel recompile — so tiny-script
plain rows overstate rival cost; `[precompiled]` rows are quoted wherever they change the
story. ChaiScript has no precompiled path in the suite.

### Trivial statements & expressions

| benchmark | Jai interp | Jai VM | Lua | Squirrel | ChaiScript |
|---|---|---|---|---|---|
| Integer addition | 0 | 0 | 1 | 4 | 5 |
| Float multiplication | 0 | 0 | 2 | 5 | 4 |
| Variable ops (3 decls + add) | 1 | 0 | 2 | 5 | 14 |
| Null check | 1 | 0 | 2 | 5 | — |
| Complex expression | 0 | 0 | — | — | 12 |
| Constant expression (folded at parse) | 0 | 0 | — | — | 10 |

**Winner: JaiScript**, with an honest asterisk: precompiled, Lua and Squirrel also hit 0 µs —
at statement granularity everyone competent is sub-µs once compiled, and the visible rival
cost above is per-iteration recompilation that JaiScript's source cache avoids by design.
ChaiScript is 5–14 µs regardless. The design point that *is* real: JaiScript's plain
`execute(string)` path costs the same as its precompiled path, so naive embedding code
doesn't pay a tax.

### Function calls

| benchmark | Jai interp | Jai VM | Lua | Squirrel | ChaiScript |
|---|---|---|---|---|---|
| Declare + call | 1 | 0 | 1 | 5 | 13 |
| Call `[precompiled]` | 1 | 0 | 0 | 0 | — |
| Lambda create+call (capture-free) | 2 | 1 | — | — | — |
| Lambda create+call (capturing) | 3 | 2 | — | — | — |
| Pass function as value + call | 3 | 2 | — | — | — |

**Winner: floor-level tie** — JaiScript VM, Lua, and Squirrel are all ≤1 µs compiled; the
integer-µs harness can't separate them (the difference that matters surfaces under recursion
below). ChaiScript pays 13 µs per declare+call. Function *churn* (creating closures per
frame) stays in the 1–3 µs band on both JaiScript backends.

### Recursion (call-dense) — the honest loss

| benchmark | Jai interp | Jai VM | Lua | Squirrel | ChaiScript |
|---|---|---|---|---|---|
| Factorial(10) | 14 | 4 | 2 | 6 | 67 |
| Fibonacci(15) | 1877 | 697 | 75 | 165 | — |
| Fibonacci(15) `[precompiled]` | 1899 | 685 | 72 | 159 | — |
| Fibonacci(6) | 25 | 8 | — | — | 164 |
| Recurse, 10 locals, depth 15 | 66 | 41 | 3 | 7 | — |
| Recurse, 10 locals, depth 10 | 45 | 28 | — | — | 180 |

**Winner: Lua, decisively.** Lua runs fib(15) **~9× faster** than the JaiScript VM and the
locals-heavy recursion **~14× faster**; Squirrel is ~4–5× faster than the VM on the same
rows. This is the one structural gap: the project's own diagnosis (VM-perf branch) found
opcode *counts* at parity with Lua (~7 ops per fib call) — the cost is per-call frame setup,
not dispatch. The committed fix is the **flat-stack / register-window VM rewrite** on the
roadmap; Squirrel's same-architecture calls demonstrate the headroom. Two mitigations are
already real: the VM cut the interpreter's fib time 2.7× (1877 → 697), and it beats
ChaiScript by **~20×** on the same shape (fib(6): 8 vs 164). Practical guidance unchanged:
game scripts that are loop- and method-shaped don't feel this; avoid deep naive recursion in
per-frame hot paths until the rewrite lands.

### Loops

| benchmark | Jai interp | Jai VM | Lua | Squirrel | ChaiScript |
|---|---|---|---|---|---|
| For loop, 100 iters | 14 | 9 | 4 | 9 | 30 |
| Hot loop 1000 (`sum += i`)* | 133 | **42** | 10 | 36 | — |
| Hot loop 1000 `[precompiled]`* | 132 | **42** | 7 | 29 | — |
| Hot loop 1000, fused shape (`sum += i * 2`) | 150 | **47** | — | — | — |
| Range-for, 10 elements | 5 | 2 | 4 | 7 | — |
| Range-for by copy, 100 elements | 28 | 18 | — | — | 31 |

\* re-measured at 2191c59b (single-operand compound fusion landed: bare identifier/const
RHS joins `op_compound_fused`, min-of-5): the comparison-suite hot loop dropped 88 → 42 µs.
The other cells in this table are the 3528ea86 measurement session's.

**Winner: Lua, by 4.2×** (down from 8.8× pre-fusion; none of it is recompile artifact — the
precompiled rows agree). Against Squirrel the VM now splits the class three ways: tie on the
counted for-loop, win on range-for (2 vs 7), and the hot loop narrowed from a clear loss to
42 vs 36 (~1.2×, harness noise territory but not yet a flip). Per-iteration reality check:
the fused VM loop runs **42–47 ns/iteration**, and range-for is where JaiScript's loop
machinery is already the best of the four.

### Containers

| benchmark | Jai interp | Jai VM | Lua | Squirrel | ChaiScript |
|---|---|---|---|---|---|
| Array push/pop | 5 | 5 | 5 | 9 | 47 |
| Map/table insert + lookup | 3 | 2 | 3 | 7 | 29 |

**Winner: JaiScript VM and Lua, tied at the floor** (the VM edges the map row 2 vs 3).
Squirrel pays 2–4×, ChaiScript ~10–15×. Container micro-ops are a JaiScript strength on both
backends — the interpreter already matches Lua here.

### Strings

| benchmark | Jai interp | Jai VM | Lua | Squirrel | ChaiScript |
|---|---|---|---|---|---|
| Concat, 20 iterations | 13 | 7 | 5 | 10 | 51 |
| Copy long string ×5 | 2 | 2 | — | — | 36 |
| find / substr / size chain | 2 | 2 | — | — | 37 |

**Winner: Lua by ~1.4×** on the concat loop; the VM beats Squirrel by the same margin and
ChaiScript by 7×. JaiScript strings are O(1)-copy (shared), which is why the copy and
method-chain rows sit at 2 µs where ChaiScript pays 36+.

### Method dispatch & object creation

| benchmark | Jai interp | Jai VM | Lua | Squirrel | ChaiScript |
|---|---|---|---|---|---|
| Class creation | 5 | 5 | 3 | 6 | 47 |
| Method invocation | 5 | 3 | 3 | 6 | 39 |
| Method invocation `[precompiled]` | 5 | 3 | 0 | 0 | — |
| Class inheritance (define + instantiate)* | 175 | 174 | — | — | — |

\* perf-suite row, both backends equal — the cost is class (re)definition bookkeeping, not
dispatch; batch class definitions once, don't re-execute them per frame.

**Winner: Lua by a hair on plain rows — and the precompiled rows show its real method-call
cost is below the harness floor** (that cheapness is what compounds into the recursion gap).
The VM matches Lua's plain method row (3 vs 3) and halves Squirrel's; ChaiScript is 8–13×
behind. The idiom ladder below adds the honest per-call figure: a JaiScript method call in a
tight loop costs ~2.4 µs of real work on either backend — method dispatch is the natural
next target after call frames.

### Object-graph algorithms & C++ interop

| benchmark | Jai interp | Jai VM | Lua | Squirrel | ChaiScript |
|---|---|---|---|---|---|
| BST 15 nodes, idiomatic `&` (in-place) | 295 | 263 | 22 | 33 | — |
| BST idiomatic `[precompiled]` | 300 | 261 | 11 | 12 | — |
| BST 15 nodes `[naive by-value]`* | 1087 | 1010 | 28 | 43 | 52698† |
| Binary search (recursive, `array<int>&`) | 7 | 4 | — | — | 52 |
| Bubble sort, 10 elements | 135 | 110 | — | — | 215 |
| C++-bound BST (native nodes) | 55 | 49 | 18 | 31 | 196 |

\* deliberately naive value-semantics spelling, kept and labeled as the feature-cost row —
Lua/Squirrel/ChaiScript objects are reference-semantic, so their "same" program never copies.
† historical measurement (ChaiScript's script BST is too slow to run live in the suite).

**Winner: Lua** (recursion cost and reference-shape cost stacking: 12–24× on precompiled BST
rows; Squirrel similar at 8–22×). This class is the recursion gap wearing a costume — the
same flat-stack roadmap item pays here. What JaiScript controls today it does well: the
idiomatic `&` spelling is **3.7–3.9× faster than naive by-value** (full three-way study
condensed below), and the retuned binary-search/bubble-sort rows beat ChaiScript 2–13×. On
C++ interop, sol2's usertype dispatch is genuinely lean (18 vs the VM's 49); JaiScript pays
`dynamic_binder` property dispatch and still beats ChaiScript 4×.

### Value-type micro-costs (vs ChaiScript's BoxedValue)

`script_value` constructs int/string/bool/float in **0 µs** where BoxedValue takes 3; type
check 0 vs 8; array construction 0–1 vs 14–15; mixed types 1 vs 18. The 32-byte thin value
is free where BoxedValue is a malloc.

### Scorecard

| vs | verdict (VM backend) | where they win | where JaiScript wins |
|---|---|---|---|
| **Lua 5.4** | 7W / 10L / 2T | recursion (9–14×), loops (2–4× post-fusion), BST, C++ interop, concat | statements, containers, range-for, null checks — and every Lua win except recursion/BST is ≤8 µs absolute |
| **Squirrel** | **12W / 6L / 1T** | fib (4×), locals recursion (5×), hot loop (42 vs 36 post-fusion — a near-tie), both BSTs, C++ BST | everything else: statements 4–5×, containers 2–4×, methods 2×, strings 1.4×, range-for 2.3× |
| **ChaiScript** | **29W / 0L** (all live rows) | nothing measured | everything, 1.6× (C++-adjacent) to ~20× (recursion), typically 8–20× |

(The previously published Squirrel line was 13W/6L; at HEAD the counted for-loop reads
10 vs 10 — a tie at harness resolution, so the honest count is 12W/6L/1T.)

---

## Per-op ladders (Performance Benchmarks suite, interp / VM)

| row | interp µs | VM µs |
|---|---|---|
| Integer addition / float mul / complex expr | 0 | 0 |
| Variable operations | 1 | 0 |
| Function calls | 2 | 1 |
| Simple compound assignment ×100 | 3 | 2 |
| Variable lookup heavy (10 lookups) | 3 | 1 |
| String concatenation | 2 | 1 |
| String copy (long) / method chaining | 2 / 3 | 1 / 2 |
| String passing to function | 6 | 5 |
| Array push/pop | 5 | 5 |
| Map insert/lookup | 3 | 2 |
| For loop (100 iterations) | 14 | 10 |
| Hot loop (1000 iterations, fused) | 150 | **47** |
| Class creation | 15 | 14 |
| Method invocation | 12 | 9 |
| Class inheritance | 175 | 174 |
| Fibonacci(15) | 1861 | 701 |
| Recurse 10 locals (depth 15) | 66 | 42 |
| BST `[naive by-value]` / shared_ptr / by-ref | 976 / 389 / 292 | 915 / 346 / 251 |
| Ref-param pass-through relay ×100 | 142 | 66 |
| Engine creation | 77 | 73 |
| Stdlib registration | 311 | 312 |

jaibite variants of the hot rows match their plain counterparts (the source cache already
removed parse cost from plain rows): hot loop 150/46, method invocation 11/8, for-loop 13/9.

Array/map literals (auto vs var, 10-int array through 3-level nesting): everything is 1–5 µs
on both backends; `var` runs 0–1 µs cheaper than `auto` per row (deferred element typing) —
an observation, not a recommendation; the type ladder is `static_checking.md`'s story.

Warm embedding costs (from `execution_mode_metrics.md`, ns-resolution methodology): warm
`bite.execute()` of a tiny script is **0.38 µs on the VM** (0.85 interp); engine creation
~90 µs all-in. Compile is ~⅓ of parse cost and amortizes by the 3rd–4th execution — the
basis for "VM as default backend, no tiered mode".

## Script-idiom cost ladder (what a line of your script costs)

Dedicated ns-resolution bench (25-rep medians over 20k-op jaibite loops, min-of-5 runs):

| idiom | interp ns/op | VM ns/op |
|---|---|---|
| empty counted-loop iteration | 32 | 15 |
| local float compound (`x += 1.5`) | 174 | 80 |
| field via `this` inside a method (`t += 1.5`) | 170 | 89 |
| array element read (`s += a[i & 1023]`) | 575 | 370 |
| compound element store (`a[i] += 1`) | 1469 | 560 |
| read+write element store (`a[i] = a[i] + 1`) | 1076 | 872 |
| free function call in loop | 920 | 440 |
| method call in loop | 2470 | 2366 |
| template-string build (small) | 1019 | 802 |

Readings (this refreshes the previously circulated idiom advice):

- **The compound-store advice is backend-dependent** (new finding): on the VM, `a[i] += 1` is
  1.6× *cheaper* than the read-then-write spelling (560 vs 872 ns). On the interpreter it is
  1.4× *dearer* (1469 vs 1076). The VM is the default backend — prefer compound stores, and
  know the interpreter inverts the advice.
- **Method calls cost ~2.4 µs on both backends** — dispatch machinery, not loop overhead.
  In per-element hot loops prefer a free function (0.4–0.9 µs) or inline field work
  (~90–170 ns); or move the loop *inside* the method (the `this`-field row).
- Element access through a local `var&` alias vs. the global chain directly is a wash at this
  size, and a one-time 1024-element array *copy* amortized over 20k reads disappears
  entirely. Copies cost per-call, not per-read — see the aliasing section.

## Aliasing costs (condensed from `aliasing_costs.md` — the pre-study joins this report)

Value vs `T&` vs `shared_ptr<T>`, measured on both backends, 200-call rows, meaty `Creature`
(string + 8-int array + 4 scalars):

- **Pass-by-value's premium is the deep copy: ~1.8 µs/call for that object**, both backends
  (read-only aggregate: 1605/1324 µs by value vs 1245/956 by `&`). It scales with payload.
- **`&` and `shared_ptr` are call-boundary ties** (≤4%, under noise) — no per-access deref,
  refcount, or upcast tax materializes. Method calls don't care how the receiver is held.
- **Structure shape is the real shared_ptr cost**: a BST written handle-style
  (`root.left = insert(...)`, store-and-return per level) runs ~1.35× the in-place `&`
  shape; both crush naive by-value ~3.5×.
- Rule of thumb: **values by default; `&` when a call needs the real thing; `new`/shared_ptr
  when the alias must outlive the call.** Full tables and the no-surprise verdict:
  [aliasing_costs.md](aliasing_costs.md).

This session's perf-suite aliasing rows reproduced the study within noise (e.g. VM read
cluster 1350/996/1028; heal pair 287/290).

## Parallel scaling (`parallel_transform`, post worker-reuse)

i7-6920HQ, 4 cores / 8 threads. min-of-3, ms. The serial baseline is the equivalent script
`for`+`push` loop.

| workload (VM backend) | serial | W=1 | W=2 | W=4 | W=8 | W=8 speedup |
|---|---|---|---|---|---|---|
| Math, 100k doubles | 323 | 242 | 140 | 90 | 67 | **4.8×** (3.6× vs W=1) |
| Strings, 10k | 68 | 61 | 36 | 21 | 18 | 3.7× |
| Math, 1k (small-array) | 3.2 | 2.3 | — | 0.90 | 0.80 | 4.0× |

| workload (interp backend) | serial | W=1 | W=2 | W=4 | W=8 | W=8 speedup |
|---|---|---|---|---|---|---|
| Math, 100k doubles | 438 | 262 | 150 | 94 | 74 | **5.9×** |
| Strings, 10k | 88 | 66 | 39 | 24 | 20 | 4.3× |
| Math, 1k | 4.3 | 2.5 | — | 0.96 | 0.86 | 5.0× |

- Scaling against W=1 is ~2.6–2.9× at W=4 (physical cores) and ~2.9–3.6× at W=8
  (hyperthreads add ~10–25%) — healthy for a 4C machine on script-bound work.
- **W=1 beats the serial loop on every row**: the serial baseline pays a script-level
  `out.push(x)` per element while `parallel_transform` runs a chunked builtin loop — the
  builtin is worth ~25–40% before any parallelism at all.
- Small arrays stay profitable post worker-reuse: the 1k row is ahead of serial even at W=1
  and reaches 4–5× at W=8 (context *acquisition* used to dominate; slot reuse fixed it).
- Debugger interaction: parallel regions are atomic to the debugger (structural — worker
  contexts never consult it); see `DEBUGGER_DESIGN.md`.

## Debugger cost (confirmation pass at HEAD — matches the recorded tables)

Hot Loop 1000-iteration script, min-of-3:

| configuration | interp µs | VM µs |
|---|---|---|
| no debugger constructed | 149 | 47 |
| controller constructed, no session | 148 | 46 |
| session enabled, no breakpoints | 148 | 50 |
| session enabled, bp in a cold file | 146 | 51 |
| session enabled, bp line collides | 165 | 64 |

Confirms `DEBUGGER_DESIGN.md`'s recorded 142–160 (interp) and 46/46/50/52/63 (VM) within
harness noise. The disabled rows sit exactly on the pre-debugger bands — the debugger is
free until a session arms; an armed session costs ~10% on the VM (statement-edge detection
per dispatch iteration), and the only expensive configuration is the deliberate worst case
(a breakpoint in another file whose line number collides with the hot statement: one bounded
string compare per collision).

## jaibite disk cache (parse once, load thereafter)

Representative 535-line game file (the crawler's `data.jai` + `combat.jai`: 10 classes, data
tables, combat logic), fresh engine per sample, 30-sample medians, 3 runs:

| backend | parse path (full run) | cache hit (full run) | first cached run (parse + write) | speedup |
|---|---|---|---|---|
| interp | 5.82 ms | 2.43 ms | 7.7 ms | **2.4×** |
| VM | 5.32 ms | 2.22 ms | 6.7 ms | **2.4×** |

The `.jaibite` for this file is 31,334 bytes. Both timed columns include *executing* the file
(class registration etc.), so the parse component itself (~3.4 ms of the 5.8) is replaced by
a sub-ms binary load; the write-through cost is paid once, on the first run after a change.
This is the automatic `execute_file`/include path — unchanged scripts never parse again,
across processes.

## Known gaps & roadmap

- **Call-dense recursion vs Lua (~9× on fib(15), ~14× locals-heavy)** — structural per-call
  frame cost; opcode counts are already at parity. Fix: the flat-stack / register-window VM
  rewrite (a roadmap commitment, not a hope; Squirrel's same-architecture calls at 4×
  demonstrate the headroom).
- ~~**Single-operand compound fusion** (`sum += i`)~~ **LANDED (2191c59b)**: bare
  identifier/const RHS joined `op_compound_fused`; the comparison-suite hot loop runs
  42 µs (was 88), Lua's loop margin fell 8.8× → 4.2×, and the Squirrel hot-loop row
  narrowed to 42 vs 36 (a near-tie, not the predicted flip).
- **Method-call dispatch (~2.4 µs/call both backends)**: the idiom ladder shows it dwarfing
  free calls (0.4–0.9 µs); flattened dispatch already landed, the binder/lookup path is next.
- **C++ interop dispatch**: sol2 usertypes at 18 µs vs `dynamic_binder` at 49 µs on the
  bound-BST row — property-dispatch cost, worth a targeted pass.
- **Band-edge verdict (this session's open question): NOT a regression.** At HEAD the VM runs
  hot loop 47 µs (band 44–48) and fib(15) 701 µs (band 687–741), min-of-5 on a verified-quiet
  machine; the earlier 50–56/756 spot-check was taken under load and did not reproduce (one
  56 µs outlier in 5 runs — the ±50% single-run variance the methodology warns about). No
  bisect warranted; the post-stage-C changes (vm debug hook et al.) carry no measurable cost.
- **Class redefinition bookkeeping** (~175 µs inheritance row, identical on both backends):
  per-execute class re-registration — batch definitions once, don't re-execute per frame.

## Appendix A — methodology

- **Machine**: i7-6920HQ (4C/8T), Windows 10 19045. **Toolchain**: MSVC (VS 18 Community)
  x64, `x64-Release BENCHMARKS` config (`/O2 /GL /LTCG`, benchmarks ON,
  `JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES` on for the tests target — both backends pay equally).
- **Harness**: Foundry benchmark rows report **integer µs/iteration with ±50% single-run
  variance** (`invariants.md` §7). Everything quoted is **min-of-5** full-suite runs per
  backend (min-of-3 for the parallel suite and the debugger confirmation), on a quiet machine
  verified free of competing processes. Sub-µs deltas are never claimed from this harness;
  the ns-resolution numbers (idiom ladder, warm dispatch) come from dedicated `steady_clock`
  micro-benches with 25–30-rep medians.
- **Rivals**: Lua 5.4 via sol2 (`source/tests/performance/sol2_comparison.cpp`), Squirrel 3.x
  (`squirrel_comparison.cpp`), ChaiScript 6.1.0 (`chaiscript_comparison.cpp`; headers restored
  to `External/ChaiScript-6.1.0` for live measurement). Suites reviewed for idiomatic
  fairness; the ChaiScript algorithm rows were retuned in 2be96668 (JaiScript params changed
  to `array<int>&` so both sides pass arrays by reference — the old spelling measured
  JaiScript deep-clones against ChaiScript's reference-semantic arrays). Rival
  `[precompiled]` rows use each engine's native precompilation, JaiScript's use
  `engine::jaibite`. **Deprecation note (Dev ruling, 2026-07-08):** the explicit-jaibite
  `[precompiled]` twins were scaffolding to show parse-free numbers; the parse-avoidance
  ladder (da8eac7e) made plain `execute()` the parse-free warm path, so future refreshes
  fold or drop those rows — keep a precompiled row only where that language's users
  genuinely precompile (e.g. Lua's loadbuffer-then-call embedding shape), symmetrically.
  Don't read a dropped `[precompiled]` row in a future table as a measurement change.
- **Backends**: every JaiScript number is reported for both the tree-walking interpreter and
  the bytecode VM (`--backend=vm`). Full regression suite (88 suites / 1844 tests) green on
  both backends at HEAD 3528ea86 as part of this measurement session.
- Each table is reproducible via
  `jaiscript_tests.exe "<suite name>" --verbose [--backend=vm]` — suite names: "Performance
  Benchmarks", "Lua (sol2) Performance Comparison", "Squirrel Performance Comparison",
  "ChaiScript Performance Comparison", "Function Churn Bench", "Parallel Transform Bench",
  "Debugger".

## Appendix B — optimization history (git-verified, tree-walker era → VM)

| Date | Commit | For Loop (100) | Hot Loop (1000) | Notes |
|------------|----------|----------------|-----------------|---------------------------------|
| 2026-07-07 | 3528ea86 | 14 (VM 10) | 150 (**VM 47**) | this report; VM default, fused superinstructions |
| 2025-12-28 | — | 8 | 42 | slot-based locals (fib 1051→878) |
| 2025-12-26 | cfc7720c | 8 | 44 | string interning improvements |
| 2025-12-24 | 13d54d35 | 15 | 49 | strong_ptr + shared string storage |
| 2025-12-18 | 9878fe2d | 17 | 76 | dynamic_cast removal + throw cleanup |
| 2025-12-15 | 9310a48f | 28 | 197 | switch-based AST dispatch |
| 2025-12-04 | bb2b900a | 42 | ~400 | "only 2x loop perf of ChaiScript" |
| 2025-11-07 | bf50a1a6 | 67–79 | 715 | aggressive string interning |
| 2025-11-04 | 47b8a6aa | 101 | 1136 | initial PERFORMANCE.md |

(The 2025-12 interpreter rows were measured on a different machine/config than this report's;
the trend is the point, not a same-basis comparison. The 2026-07 interpreter loop numbers
sitting above 2025-12's reflects config/machine differences plus added always-on semantics —
checked overflow, budget clock, debugger hook — not a regression: the VM band is the tracked
baseline and it holds.)
