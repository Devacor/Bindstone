# Hardening 3: saved-program validation and engine-owned depth caps

Status: final combined-state correctness and performance gates complete.
Initial tests-first baseline: `887f0973`. Final A/B baseline: `63abd219`.

The separate source-location work landed during verification. It is preserved,
including its format-v14 encoding change. Earlier v13 results below are regression
evidence, not a substitute for the final combined-state gates.

## Decisions

- Extend the decoder's existing complete traversal; record frame/slot metadata,
  then validate it before marking, checking or compilation. Bound frame counts
  before allocation/narrowing; check local, captured, range, catch, destructuring
  and parallel slots. Preserve `SIZE_MAX` environment lookups and valid defaults.
- Require the correct body/member/initializer node kinds, type arity, parallel
  parameter layout, case fallthrough metadata and non-overflowing varints.
  Check type arity before interning can erase malformed extra parameters.
- Parallel frames cannot borrow numeric slots from their enclosing function.
  Nested lambdas can still capture their immediate parallel parent. The trusted
  in-memory body copier supplies its known frame; public loaders cannot.
- VM compilation honors completed saved lambda/coroutine capture plans, without
  rewriting AST metadata or adding call-dispatch work. This hardening changes no
  encoded field and adds no format bump; current v14 comes from the separate
  source-location feature. Loader diagnostics are in `diagnostics.md`.
- Reader/writer recursive AST/type entries reuse `native_stack_low()`, alongside
  the existing 1,000-level structural limit. The headroom query is Windows-only.
  The parser's 250 weighted recursion units are not raw AST depth.
- Per-engine defaults: interpreter 256 active calls, VM 10,000. Configure either
  between executions with the existing noun-style API; reject zero, values above
  `INT_MAX`, custom backend selectors and changes during execution. Workers
  snapshot both settings at each region entry, including reuse. An explicit
  legacy `JAI_MAX_CALL_DEPTH` build setting can still seed both defaults.
- One number-free checked-result depth error and one RAII counter guard cover
  function calls, host reentry and fresh/suspended coroutine resumes. Constructor
  initialization hands its depth to the body without double-counting `super()`
  or delegation. Preserve unrelated field-error behavior; guard environment and
  member-scope restoration. No mutable static/TLS state is added.
- Higher caps never disable native headroom protection. VM self tail calls remain
  constant-space. Different caps intentionally permit different depths; scripts
  that deliberately probe limits can observe different stopping points.

```cpp
auto eng = jai::engine::make();
eng->max_call_depth(jai::backend_type::interpreter, 256);
eng->max_call_depth(jai::backend_type::vm, 10000);
auto vm_limit = eng->max_call_depth(jai::backend_type::vm);
```

## Reproduced before fixing

Against the preserved `887f0973` Debug library, all 22 malformed regression cases
failed: 21 were accepted, one reached static checking instead of loader rejection.
Seven valid-file controls passed on the old interpreter; three exposed old VM
capture failures after interpreter-authored save-after-execute (undefined lambda
and coroutine captures, and global 99 returned instead of captured local 4).

Subsequent finite tests exposed suspended-resume bypasses on both backends,
fresh-resume bypass on VM, host reentry bypass on both, and implicit/explicit/
inherited constructor-field recursion bypasses on both. The field tests exhausted
a 20-construction counter despite cap 8; an outer member catch returned 0 instead
of 42. All targeted tests pass after fixing. Cap-1 constructor/super controls
passed before and after; the fix does not count initialization and body twice.

A separate 1 MB Windows-stack executable crashed in the old loader at 250 and
800 binary-expression levels on both backends (`0xC00000FD`). With the checks,
100/250/800-level loads reject catchably and recover to 42. The permanent 1 MB
thread test also reproduces the old crash; the 800-level large-stack control
preserves bytes. A separate call probe safely rejects 5,000 interpreter calls
with either cap 256 or 1,000,000, while VM completes 5,000 calls at its default.

The final frame audit added two red tests: finite foreign slots in a parallel
body loaded under both checker-off and strict. Both now reject structurally;
two positive controls preserve environment lookups and nested-lambda captures.

Two old deep-reference/scaling tests now explicitly request interpreter cap
1,000, preserving their original 800/400-depth workloads rather than reducing
them. One existing sub-10-ms hot-reload assertion failed during concurrent builds
and passed on isolated rerun; its threshold and implementation were not changed.

## Verified final combined state

| Gate | Interpreter | VM |
| --- | --- | --- |
| Debug full suite | 2,689 passed, 119.355 s | 2,689 passed, 90.605 s |
| Release full suite | 2,809 passed, 56.532 s | 2,809 passed, 28.665 s |

The full-suite timings are correctness-run observations, not controlled A/B data.
Final logs use `out/hardening-validator/combined-v14-*`.

- Release Bindstone integration build: succeeded; `-verifyassets`: **76/76**.
- Site verification: **31 pages, 178 samples; 13 host-dependent samples skipped,
  25 intentional-error samples matched; no failed checks or backend divergence**.
- Final v14 mutation campaign: **10,000 mutants, 1,807 accepted, 8,193 identical
  catchable rejections, zero findings/crashes/hangs, 20,000 byte-stable original
  loads**. The two preceding complete v13 runs recorded the same totals.
  Each isolated mutant is loaded on both backends under a watchdog; mutants
  are not executed. Ordinary execution fuzzing remains a separate oracle.

## Controlled performance gates

A pristine detached `63abd219` baseline was rebuilt after the concurrent
source-location change. Effective MSVC Release flags match current: C++20, static
runtime, debugger enabled, `/O2 /Ob2 /Oi /Ot /GL` and `/LTCG /OPT:REF /OPT:ICF`.
The LTO cache option is off in both, but the MSVC Release branch still enables
`/GL` and `/LTCG`. Only source/build paths differ.

Ran A/B/A/B/A/B on this machine with no concurrent builds/tests. A is the clean
baseline; B is this patch. All observations below are elapsed milliseconds.

GLOOM: 300 ticks, seed 666, workers 0 and 8; each invocation runs both backends.
All 24 backend observations kept frame `7fecc09815bb64e0` and state `3580805725`.

| Backend / workers | A trials | B trials | A min | B min |
| --- | --- | --- | ---: | ---: |
| Interpreter / 0 | 2779.5, 2755.9, 2755.3 | 2753.4, 2754.6, 2755.0 | 2755.3 | 2753.4 |
| VM / 0 | 713.6, 692.9, 687.9 | 688.2, 694.5, 684.4 | 687.9 | 684.4 |
| Interpreter / 8 | 1764.6, 1760.1, 1741.7 | 1753.3, 1728.2, 1737.2 | 1741.7 | 1728.2 |
| VM / 8 | 597.0, 577.8, 580.3 | 592.6, 582.2, 587.8 | 577.8 | 582.2 |

Call-path probe: 10,000 warmups per path, then 400,000 function calls, method
calls and constructions each; 100,000 coroutine creations with two resumes each.
All checksums matched: 1,800,000 / 2,800,000 / 1,400,000 / 800,000 respectively.
`clock_ms()` has 1 ms resolution; these are workload times, not isolated dispatch
latencies. Construction includes a field-initializer call, constructor and read.

| Backend / path | A trials | B trials | A min | B min |
| --- | --- | --- | ---: | ---: |
| Interpreter / functions | 120, 121, 120 | 124, 122, 123 | 120 | 122 |
| Interpreter / methods | 126, 124, 126 | 123, 123, 126 | 124 | 123 |
| Interpreter / construction | 999, 1000, 998 | 1013, 1009, 1010 | 998 | 1009 |
| Interpreter / coroutines | 177, 182, 177 | 180, 180, 180 | 177 | 180 |
| VM / functions | 29, 28, 29 | 32, 31, 31 | 28 | 31 |
| VM / methods | 32, 33, 33 | 33, 33, 33 | 32 | 33 |
| VM / construction | 553, 553, 551 | 552, 553, 554 | 551 | 552 |
| VM / coroutines | 144, 138, 140 | 139, 140, 142 | 138 | 139 |

A follow-up free-function-only workload increased to 4,000,000 calls, again
alternating A/B three times; all checksums were 18,000,000:

| Backend | A trials | B trials | A min | B min |
| --- | --- | --- | ---: | ---: |
| Interpreter | 1210, 1203, 1197 | 1213, 1210, 1233 | 1197 | 1210 |
| VM | 283, 285, 285 | 310, 313, 310 | 283 | 310 |

This confirms a measurable VM free-call microbenchmark regression: +27 ms over
4,000,000 calls, or +9.54% of that workload's best time. It is not just timer
quantization. Interpreter free calls were +1.09% on the longer workload. The
largest GLOOM slowdown was +0.76% (VM / 8 workers); the other GLOOM minima improved
slightly. Do not interpret those improvements as a proven optimization. The
configurable cap replaces a constant comparison with an engine-state load, but
these measurements do not isolate that cost from code-layout/compiler effects.
Keep the checks; investigating this microbenchmark cost is a performance follow-up.

Artifacts under `out/hardening-validator/`: `combined-v14-gloom-ab/` (metadata,
binary hashes, per-run logs, CSV), `call-ab-20260905-211113983-40264/`, and
`free-calls-long-ab/`. Workloads: `call_paths_bench.jai`, `free_calls_long.jai`.

## Source conclusions and follow-ups (not runtime proofs)

The 1,000-level saved-tree limit alone is not portable stack-safety proof.
Parser recursion is weighted; type and right-assignment paths are not covered
by that counter. Some VM host/constructor/resume paths still recurse natively.
This work does not claim that every native-stack entry or allocation-failure
transaction is now covered.

Nullable type arguments in `type_info::to_string()` and `weak_ptr<auto>`
constructor rejection remain separate read-only audit candidates, not reproduced
or fixed here. They are not reasons to reject valid inferred serialized types.

Task 4 and the prepared numeric follow-up remain unapplied. Report this note and
the Task 3 commit before starting the Task 4 source-path inventory.
