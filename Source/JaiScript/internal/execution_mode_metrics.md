> **MEASURED 2026-07-04** on the VM-perf branch, Release BENCHMARKS config
> (`/O2 /GL /MT`, default `JAISCRIPT_CHECKED_OVERFLOW`), pre-thin-value-fold lib snapshot.
> These cold/warm/crossover numbers are what informs the standing recommendation:
> **VM as the default backend, tree-walking interpreter kept as the parity oracle** —
> no tiered/auto-promotion mode (compile cost is ~⅓ of parse and amortizes by the
> 3rd–4th execution).

# JaiScript Execution-Mode Metrics: Tree-Walking Interpreter vs Bytecode VM

**Date:** 2026-07-04 · **Branch state:** VM-perf (lib snapshot from `x64-Release BENCHMARKS`, Jul 4 00:34)
**Question:** what does the "interpreter for one-offs, VM for hot scripts" tradeoff actually cost, and at what execution count N* does the VM overtake?

## Method

- Static-linked bench exe against a locked snapshot of `jaiscript.lib` (139 MB, `/O2 /GL /MT`, C++20, default `JAISCRIPT_CHECKED_OVERFLOW`, `JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES` — the latter is always-on for this lib target, both backends pay it equally).
- `std::chrono::steady_clock` (QPC-backed, 100 ns resolution; measured timer overhead median = 0 ticks). Medians of ≥1000 samples for cheap ops (batched ×8 where sub-µs), ≥150–300 for expensive ops. **Backends interleaved sample-by-sample** to equalize thermal/load drift.
- Two full runs: run 1 under 22–40% ambient CPU load (concurrent build workflow active), run 2 under 11–14%. Warm-path medians agreed within ~5% across runs; cold-path (allocation-heavy) numbers were 10–45% higher under load. **Tables below use run 2 (quiet) as primary; run 1 in parentheses where it differs materially.**
- Windows 10 (10.0.19045), MSVC 18 (VS 2026), x64.

### Scripts

| id | contents |
|---|---|
| `tiny_predicate` | `hpValue < 30 && distValue < 5.0` with `add_global`'d `hpValue` (int 25), `distValue` (double 3.2) |
| `small_function` | 6-line script: define `int damage_roll(int,int)` with branch + call it |
| `medium_class_loop` | ~30 lines: class `Mob` (3 fields, ctor, 2 methods) + `run_sim()` with a 100-iteration loop of method calls |

Smoke-verified: both backends return identical results (`true` / `33` / `1893`), including re-execution and jaibite re-execution.

## Raw medians

### A. Engine setup

| measurement | interp | vm |
|---|---:|---:|
| `engine::make()` alone (backend-independent) | 51.2 µs (55.3) | — |
| `make()` + `set_backend` + first trivial `execute("1")` | 90.5 µs (163.1) | 86.7 µs (154.5) |

Lazy backend construction + first-execute init costs ~35–40 µs on top of `make()`; **identical for both backends** (VM was marginally *cheaper* in both runs — noise-level).

### B. Cold one-shot on a fresh engine (per-sample: fresh engine, `add_global`×2 + `execute`)

Engine-create span is reported separately (≈60–70 µs under measurement conditions); the exec span below is *total minus engine-create*:

| script | interp cold exec | vm cold exec | VM premium |
|---|---:|---:|---:|
| tiny_predicate | 36.9 µs (43.1) | 34.8 µs (40.3) | **−2.1 µs (VM cheaper)** |
| small_function | 58.9 µs (61.5) | 60.4 µs (59.9) | ±1.5 µs (parity) |
| medium_class_loop | 403.1 µs (439.6) | 418.8 µs (454.6) | +15.7 µs (+3.9%) |

### C. Cache-MISS on a warm engine — unique source string every execution (the true "one-off predicate" pattern: parse [+compile] + run)

| script | interp | vm | VM premium |
|---|---:|---:|---:|
| tiny_predicate | 7.1 µs (8.1) | 8.4 µs (9.7) | **+1.3–1.6 µs (+18–20%)** |
| small_function | 29.1 µs (39.8) | 31.8 µs (44.7) | +2.7–4.9 µs (+9–12%) |
| medium_class_loop | 363.7 µs (390.6) | 368.3 µs (394.0) | +4.6 µs (+1.3%) |

### D. Cache-HIT on a warm engine — same string repeatedly (64-entry LRU: cached parse, and on the VM, cached chunk)

| script | interp | vm | VM speedup |
|---|---:|---:|---:|
| tiny_predicate | 1.03 µs | 0.60 µs (0.58) | **1.7–1.8×** |
| small_function | 3.48 µs (3.81) | 2.06 µs (2.21) | **1.7×** |
| medium_class_loop | 251.6 µs (258.9) | 241.4 µs (252.4) | 1.04× |

### E. jaibite (pre-parsed handle)

| measurement | script | interp | vm |
|---|---|---:|---:|
| E1 creation (= parse) | tiny | 3.8 µs | 3.8 µs |
| | small_function | 15.9 µs | 16.0 µs |
| | medium | 67.5 µs (66.2) | 67.6 µs (66.4) |
| E2 FIRST `bite.execute()` | tiny | 1.3 µs | 2.4 µs (2.3) |
| | small_function | 6.5 µs | 9.5 µs (9.2) |
| | medium | 263.6 µs | 276.6 µs |
| E3 warm `bite.execute()` | tiny | 0.85 µs | **0.38 µs** |
| | small_function | 3.10 µs | **1.69 µs** |
| | medium | 257.4 µs (244.3) | 247.8 µs (237.1) |

### F. Empirical cumulative cost, fresh engine, N executions of the same string (engine create + `add_global` untimed)

| script | N=1 | N=2 | N=5 | N=10 | N=50 |
|---|---|---|---|---|---|
| tiny (interp) | 35.9 µs | 36.0 | 40.4 | 48.6 | 90.2 |
| tiny (vm) | **32.9** | **32.5** | **35.4** | **39.3** | **63.4** |
| func (interp) | **53.9** | 60.7 | 75.2 | 86.5 | 221.3 |
| func (vm) | 54.5 | **60.4** | **65.8** | **74.6** | **152.1** |
| medium (interp) | 423.6 | **668.6** | **1392.9** | 2902.8 | 12954.8 |
| medium (vm) | **422.3** | 710.9 | 1536.5 | **2794.4** | **12467.2** |

(Bold = winner. Medium N=2/N=5 flipped between runs — the per-exec delta is ~4% of a 250 µs execution, i.e., inside noise at 25 reps; run 1 had VM ahead from N=5.)

## Parse vs compile attribution

Code-confirmed (`engine.cpp:916`, `vm_backend.cpp:7029`): **`engine->jaibite()` is parse-only on both backends; VM bytecode compilation happens lazily at the first execute** and is cached in the bite / source-cache entry. E1's backend-identical numbers verify this empirically.

VM compile cost, isolated as (E2ᵥₘ − E3ᵥₘ) − (E2ᵢ − E3ᵢ) (differences-of-differences cancels the shared cold-AST/first-run effect), consistent across both runs:

| script | parse cost | VM compile cost | compile / parse |
|---|---:|---:|---:|
| tiny_predicate | 3.8 µs | **≈1.5 µs** | ~39% |
| small_function | 15.9 µs | **≈4.2–4.4 µs** | ~27% |
| medium_class_loop | 67.5 µs | **≈20–23 µs** | ~31% |

**VM compilation costs roughly a third of what parsing costs.** Parse dominates the pre-execution pipeline on both backends.

## Crossover N*

### Regime 1 — same source string re-executed (source-cache hit after the first), fresh engine

Analytic (cold delta ÷ warm per-exec advantage) cross-checked against table F:

| script | N* (VM overtakes) | basis |
|---|---|---|
| tiny_predicate | **1** — VM never behind | VM cold is already cheaper; warm 1.7× faster |
| small_function | **≈2** | cold parity (±1.5 µs); VM gains 1.4 µs/exec warm |
| medium_class_loop | **≈3–4** (analytic: 15.7 µs ÷ ~9 µs/exec ≈ 3; empirical noisy, decisive by N=10) | |

### Regime 2 — jaibite (parse paid once, N executes)

Compile premium ÷ warm advantage: tiny 1.5/0.47 ≈ **4**; small_function 4.3/1.4 ≈ **4**; medium 21/9 ≈ **3**. The VM overtakes by the **3rd–4th execution** of any pre-parsed script.

### Regime 3 — every execution a unique string (cache-miss one-offs)

Per-exec costs from table C. The VM pays parse+compile+run every time and its faster run does not recover the compile premium on small scripts:

| script | winner | margin |
|---|---|---|
| tiny_predicate | **interpreter, at every N (N* = ∞)** | 1.3–1.6 µs/exec (18–20%) |
| small_function | **interpreter, at every N (N* = ∞)** | 2.7–4.9 µs/exec (9–12%) |
| medium_class_loop | statistical parity | ~1% — faster VM run nearly cancels compile |

## Plain-language guidance

- **What a one-off game predicate actually costs end to end** (e.g. `hpValue < 30 && distValue < 5.0`):
  - Warm engine, novel string: **~7 µs interpreter, ~8.5 µs VM**. Parse is ~half of that on either backend.
  - Warm engine, repeated string (cache hit or jaibite): **~0.9–1.0 µs interpreter, ~0.4–0.6 µs VM**.
  - Fresh engine each time: **~125–165 µs either backend** — engine creation + lazy init (~90 µs) swamps the script itself by 10×. Backend choice is irrelevant here; engine reuse is the optimization.
- **The tree-walker wins only in one regime:** genuinely unique, never-repeated small scripts on a warm engine, by 1.3–5 µs (~10–20%) per execution. At 1,000 unique one-off predicates/frame that's ~1.5–5 ms — but the right fix there is reusing parsed jaibites or canonicalizing predicate strings (the cache then makes the VM faster anyway), not backend selection.
- **The VM wins everywhere else, and sooner than expected:** at parity or ahead on cold one-shots from a fresh engine (compile is cheap and its run is faster), 1.7–1.8× ahead on warm small scripts, and cumulatively ahead of the interpreter by the 1st–4th execution in every re-execution regime.
- Medium class-heavy scripts see only a ~4% warm VM advantage — per-execute class redefinition/hot-reload machinery and method-call overhead dominate, and those cost the same on both backends.

## Does this justify a tiered / auto-promotion mode (interpret first, compile when hot)?

**No — not on cost grounds.** The measured compile premium is 1.5 µs (tiny) / 4.4 µs (small) / ~21 µs (30-line), i.e. ~30–40% of the parse cost that both modes already pay, and it amortizes within 3–4 executions. A tiered mode's maximum possible saving is that premium on scripts executed exactly once or twice — single-digit microseconds — while it adds promotion bookkeeping, double-warm-up (interpreted runs then a compile anyway), and a second live execution path to keep semantically in sync. The 64-entry source LRU + jaibite already capture the "hot script" case with zero policy machinery.

## Surprises

1. **VM compile cost is negligible relative to parse** (~⅓ of parse). This kills the "keep the interpreter as the cheap one-off path" argument on cost grounds alone: even for a true cold one-shot, the VM is at parity or ahead because its faster execution refunds the compile. The interpreter's remaining value is non-cost (reference semantics, debuggability, coroutine-replay simplicity).
2. **Cold one-shot on a fresh engine is engine-dominated, not script-dominated:** ~90 µs of make+init before any script work. Any "one-off predicate" architecture that spins up engines per evaluation is paying 10–40× the script cost in overhead.
3. **The VM is 1.7–1.8× faster on warm tiny/small scripts** — not just on loop-heavy benchmarks; even a single-expression predicate dispatches measurably faster on bytecode.
4. The interpreter's only durable win (unique-string one-offs) is worth just 1.3–5 µs/exec, and disappears entirely at ~30 lines because run time swamps compile time.
5. Cold-path timings inflated up to ~45% under the concurrent build's ambient load (40% CPU) while warm-path medians barely moved — interleaving made the backend *comparison* robust, but absolute cold numbers should be read as upper bounds.

## Reproduction

```
# lib snapshot (taken under the shared build.lock):
#   C:\git\Bindstone\Source\JaiScript\out\build\x64-Release BENCHMARKS\lib\jaiscript.lib  (139,112,176 bytes, 2026-07-04 00:34)
# headers used in place: C:\git\Bindstone\Source\JaiScript\include

"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cl /nologo /std:c++20 /O2 /MT /EHsc /I C:\git\Bindstone\Source\JaiScript\include bench.cpp /link jaiscript.lib /STACK:134217728
# (linker auto-restarts with /LTCG because the lib was built /GL; squirrel_lib.lib NOT needed)
bench.exe
```

Files in `scratchpad\mode_metrics\`: `bench.cpp`, `bench.exe`, `jaiscript.lib` (snapshot), `bench_output.txt` (run 1, loaded), `bench_output_run2.txt` (run 2, quiet), this report.
