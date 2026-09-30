# Known divergences (fuzz suppressions ledger)

The differential fuzzer (`jaiscript_fuzz`, harness in `fuzz_harness.hpp`) suppresses
divergences whose *shape* matches a known, already-triaged parity gap so campaigns surface
only NEW findings. Matchers live in `known_suppressions()` in `fuzz_harness.hpp`: a
suppression matches when every `program_needles` substring appears in the generated program
AND every `diff_needles` substring appears in the divergence summary.

Reproduce any entry: `jaiscript_fuzz --seed <N> --no-suppressions --minimize`
(seeds below are max-depth 4, the default). The fuzzer only REPORTS engine bugs; fixes are
the fix ledger's job.

## Active suppressions

| id | shape | repro seeds | wrong backend (judgment) | status |
|----|-------|-------------|--------------------------|--------|
| KD-ORDERING-THROW | `throw` inside a class `operator<` swallowed by one backend | none observed in pilot (fix may already be landed) | n/a | matcher retained defensively |
| KD-ENV-RECURSION-LIMIT | deep-but-legal recursion (under `JAI_MAX_CALL_DEPTH` native frames, e.g. ~7.3k): interpreter's ENVIRONMENT recursion guard errors first; the vm's in-loop frames complete it and continue (erroring later or succeeding). CHARTER WIDENED (Dev ruling, TCO landed 30d5c9cd): bare self tail calls (`return f(args)`, no enclosing try) now splice frames on the vm — tail recursion of ANY depth completes (or runs to the execution budget) where the interpreter raises `max_recursion_depth` at 10k. Same judgment, same suppression. | 2072 (needs `--timeout-ms 5000`; at the default 250ms Debug budget it hides as a budget-skip) | interpreter (the depth was legal; the env guard fires below the documented cap; for tail shapes the vm's unbounded depth is the DESIGNED behavior) | ACCEPTED divergence (Dev ruling 2026-07-09; tail charter 2026-07-14): closing it means refactoring the tree walker off native recursion — not needed; parked as a low-priority item. Suppression is permanent. Also surfaces as a printed-output-only diff (seed 8517 at depth 4, 2026-09-02: the program prints inside the loop that recurses, so the vm prints one more pass before both backends end with the identical recursion text; the harness reports the first differing dimension, so the diff needle does not see the error text). When the guard's error is CAUGHT and printed (seed 13066 at depth 5) only the outer `Maximum recursion depth (10000) exceeded` text reaches the diff; a second matcher entry with that needle covers it. Third disguise: a `budget-skip SUSPECT` (seed 30111 at depth 4): segment 1's `rec0(9792)` trips the interpreter guard so its `auto v4` never exists and segment 2 fails fast on the undefined variable, while the vm completes the recursion and then runs the generator's infinite loop (`i5 = -7` inside its own for-body) into the budget. Read a lone-backend budget hit next to a deep recursion as this class before suspecting a hang. |

## Fixed (matchers removed; recorded seeds re-verified agreeing with --no-suppressions)

All fixed on VM-perf, 2026-07-05. Regression tests live in
`source/tests/language/review_regression_tests.cpp` (the `fuzz_*` tests). After the fixes
landed and the matchers were removed, a full seeds-0..9999 flush campaign (per-seed
watchdog, 5s timeout) reported 0 new divergences and 0 crashes — the only reports were
suppressed KD-CORO-RESUME-PAST-END shapes and the budget-livelock hangs listed below.

| id | root cause | fix commit |
|----|-----------|------------|
| KD-CORO-RESUME-PAST-END | fixed 2026-07-11: the interpreter's `resume_coroutine` replay-completion branch never updated `handle.yield_value_` when the body ran past the final `yield` with no `return`, so the resume replayed the STALE last-yielded value (vm `run_fiber` correctly completes with null). Second shape in the same family: a zero-yield coroutine completing on its FIRST resume went through `call_function`, whose no-return epilogue applied the implicit-'this' method rule — a METHOD coroutine returned its receiver instead of null. Both interpreter paths now complete with null (explicit returns unaffected); recorded campaign seeds 4 38 98 195 216 387 626 840 1013 1099 1482 1729 2047 2049 2064 2076 2261 2483 2496 2990 (plus pilot 7, 116) re-verified agreeing with `--no-suppressions`. Test: `fuzz_coro_resume_past_end_yields_null` (review_regression_tests.cpp). | (this branch) |
| FZ-TYPED-FN-NO-RETURN (+ -CAUGHT) | NOT a typed-return bug: the interpreter's `++`/`--` identifier fast path looked up only `environment_->get_value_ptr`, never the call-frame slots where function params/typed locals live, so `++g` inside any function raised a bogus `Undefined variable 'g'`. Now resolves via `resolve_local_or_env` like every other identifier path. Fixing it also cleared seed 66's watched `Undefined variable 'to_string'` aspect. | b80f12fd |
| FZ-NEG-ZERO | interpreter `make_value(script_float)` returned the cached `+0.0` whenever `f == 0.0` — and IEEE `-0.0 == 0.0` is true, so every computed `-0.0` collapsed to `+0.0`. Cache now excludes the negative sign (`!std::signbit`). `-0.0`/`+0.0` stay distinct `std::strong_order` map keys on both backends. | d5a1daf1 |
| FZ-SPACESHIP-OVERFLOW-SWALLOW | the parser wrap-folds `1 + INT64_MAX` into a literal `INT64_MIN` (see finding below); the interpreter's literal unary-minus fast path then negated it UNCHECKED (silent wrap, and UB) while the VM raised. The literal path now routes through `ints::try_neg` with the same `Integer overflow in unary '-'` text. The VM's `'-'` attribution was in fact correct post-fold — the `+` "overflowed" at parse time. | 6fadaa19 |
| FZ-OVERFLOW-OP-NAME | the counting-for-loop update: the interpreter's native fast loop stepped UNCHECKED (the wrap let a later body op — the `'*'` — raise instead), the VM's `op_cfor_back` checked but always blamed `'+='`/`'-='` even for `i++`. Both now apply the overflow policy at the update and name the SOURCE operator (`'++'`/`'--'`/`'+='`/`'-='`), byte-identical. Fixing this exposed the `-=`-literal direction bug below. | 2cc2843d |
| FZ-FLOAT-DIVZERO-TEXT | float zero-divisor on the literal/identifier binary shapes: the interpreter's FP2/FP3 fast paths were int-only (float pairs fell to the generic handler's bare `Division by zero`) while the VM's `op_binary_fused` float branch raised `Division by zero` / `Modulo by zero`. FP2/FP3 gained the float branch (incl. float `%`); the VM's dormant `binary_fast_shape` twins were kept byte-parallel. | 423580cf |
| FZ-RECURSION-MSG-TEXT | two bugs: (1) the depth was passed to `checked_result` as a SYMBOL id (its 3rd param), so the symbolizer printed an empty `()`; (2) the interpreter trips the native-stack guard while the VM (in-loop frames) trips the depth cap, and the two conditions had different texts. One shared message with the depth stringified in (`JAI_MAX_CALL_DEPTH_MESSAGE`) is now used by both backends and both triggers. | f91678b3 |
| FZ-TYPED-DECL-RAISING-INIT | a typed declaration whose initializer RAISES (missing member read: the interpreter sets the unwinding flag, pushes null and returns success) enforced the declared type on that null and reported `Type mismatch in assignment`; the vm's run loop sees the flag before `exec_decl_var` and reports `Object has no member`. One unwinding check after each initializer evaluation in `visit_variable_decl`. Seed 22684 (depth 4, Release driver). | aa73d21d |
| FZ-FOR-FALLTHROUGH-UNBUDGETED | the interpreter's fast counting loop falls through to a continuation loop when the counter's storage kind changes mid-loop (bound by reference / captured by `[&]` -> boxed); that loop had no back-edge limit check, so an infinite loop in that state ran forever under any budget (the vm checks every back-edge). Surfaced as a HANG of the isolated child (seed 33294 at depth 4), not a divergence: the driver now carries a child-side wall-clock watchdog and reports hangs. | 6d043f9c |
| FZ-CALLEE-ORDER-METHOD (seed 132479, no matcher existed) | `v8.push(v8.length())` with a null `v8`: the interpreter looks the callee member up BEFORE evaluating arguments (C++17 [expr.call], the ruled order) and blamed `'push'`; the vm compiled receiver, args, then `op_call_method`, so the argument's `'length'` error won. `op_probe_member` now runs the member lookup (null receiver / builtin registry / object ladder, or the callable check for a computed callee) at the pre-args point on every site with a non-trivial argument; trivial-argument sites compile as before. Same pass gave the unknown-member-on-scalar read/call a text on BOTH backends (`int has no member 'nosuch'`; it was the empty `type_mismatch`). Tests: `callee_first_method_null_receiver_before_args`, `scalar_receiver_unknown_member_text`, `object_receiver_lookup_before_args`, `nested_callee_lookup_order`, `computed_callee_callable_check_before_args` (review_regression_tests.cpp). | (this branch) |
| FZ-UNDEF-VAR-THIS-DECORATION | the decoration was a red herring — `(no 'this' in scope)` / `(not a field of 'this')` exist symmetrically on BOTH backends and stay. The real divergence was compound-assign EVAL ORDER: the interpreter resolved the target before evaluating the rhs, so `v1 += v1` blamed the lhs while the VM (C++17: rhs sequenced first) blamed the rhs load. The interpreter fallback now evaluates the rhs first (side effects included). | 80e13786 |

## Engine findings that are not divergences

All three fixed on VM-perf, 2026-07-05 (Dev-ruled semantics). Regression tests:
`execution_budget_tests.cpp` (terminal/memory-cap suites) and
`review_regression_tests.cpp` (`parser_fold_*`, `incdec_overflow_*`).

- FZ-BUDGET-LIVELOCK (campaign seeds 436, 3507, 8285; +1213, 1462, 1788, 8356, 8391):
  **FIXED** — Dev ruling: budget/timeout errors are TERMINAL (a per-engine
  `detail::execution_limits::terminal_error` latch makes EVERY script catch skip —
  try/catch, nested, coroutines, lambdas, and across reentrant execute() boundaries —
  so `execute()` returns the budget error to the host instead of livelocking).
  Triaging the seeds surfaced two more root causes fixed alongside:
  (a) a TOP-LEVEL script statement calling `h.resume()` ran at call depth 0 and was
  mistaken for a host-level resume, re-arming the deadline every loop iteration
  (seeds 3507/8285/1213/8356/8391 hung on this, not on catch-swallowing); the
  host-level test is now `depth 0 && !executing` (interpreter) / `depth 0 &&
  frames_.empty()` (vm);
  (b) seed 8356 was a MEMORY bomb (exponential `s += s` — each append outlives the
  budget's 1024-tick clock gate), addressed by the new `engine::memory_cap` (first
  raise catchable, second terminal); the harness now runs every program with a 64 MB
  cap. All 8 seeds re-verified terminating with 0 divergences / 0 crashes.
- PARSER-FOLD-WRAPS (found triaging FZ-SPACESHIP-OVERFLOW-SWALLOW, 2026-07-05):
  **FIXED** — `try_constant_fold` now folds int arithmetic through
  `jai::ints::try_add/try_sub/try_mul/try_div` (and `ints::mod`); on overflow it
  declines to fold so the runtime's checked path raises with the correct operator
  name (`9223372036854775807 + 1` now raises `Integer overflow in '+'` at runtime on
  both backends; wrap builds keep folding the wrapped value). This also corrects the
  FZ-SPACESHIP attribution: the un-folded `+` raises before unary `-` ever runs.
- INCDEC-WRAPS-SILENTLY (2026-07-05): **FIXED** — plain `a++`/`--a`/`++a`/`a--` on int
  variables and this-fields now routes through `ints::try_add/try_sub` on both
  backends, raising `Integer overflow in '++'` / `'--'` (same text as the checked
  counting-for update). `JAISCRIPT_WRAP_ON_OVERFLOW` builds keep wrapping at zero cost.

## Harness lessons encoded (not engine bugs)

- A script can CATCH the execution-budget error and keep going, making printed output
  wall-clock dependent (campaign seed 324: a `var&` mutator resetting a for-loop variable
  produced an infinite loop whose `catch` swallowed the budget error). The harness now marks
  any segment consuming >=90% of the budget as budget-hit and skips the program.
  (Historical: budget errors are terminal since 2026-07-05, so the swallow shape is gone,
  but the >=90% skip stays — one-sided wall-clock timeouts are still never findings.)
- The harness runs every program with `memory_cap(64 MB)` so memory bombs terminate
  deterministically. Memory-cap accounting is approximate and backend-dependent (env and
  clone charges differ per backend), so a memory-cap hit — including one CAUGHT by the
  script's first allowed catch — is treated like a budget hit: skip, never a finding.

## Watched (no matcher)

- `obj + primitive` where class `operator+` expects an object operand — listed as an
  in-flight crash fix; the pilot (300 seeds) produced no crash, so no matcher. A crash
  cannot be suppressed anyway: it surfaces as a dead campaign process (in-process mode)
  or a nonzero child exit (`--isolate`).
- for-body `var` redeclare scoping — not observed in the pilot; possibly already fixed.
- ~~Seed 66's ORIGINAL (unminimized) program also showed the VM erroring
  `Undefined variable 'to_string'`~~ — RESOLVED 2026-07-05: same root cause as
  FZ-TYPED-FN-NO-RETURN (seed 66 re-run agrees end-to-end with --no-suppressions).

## Ordering band (`--order-band`, `--order-args`)

The band's programs put a call that writes a global, a captured local, an element, a map entry or a field
beside reads of that storage, and hold three oracles: the backends agree on the program and on its
metamorphic variant (every operand holding a call or a storage read evaluated into an `auto` local in
source order), the program agrees with its variant, and every run prints what the generator's model
computes (`fuzz_order_band.hpp`; the model walks the generator's tree, it never runs the engine).

- The model follows the order both backends implement where the documents are not explicit yet: a compound
  store reads a member's (a static's, a namespace variable's) value ahead of its right side and evaluates an
  element's index ahead of it, while a name's and an element's value are read after it (docs/invariants.md 12);
  `auto x = y` takes the declared type y's value carries (a var's value makes x a var).
- `--order-args` also puts a writing call beside a read in call arguments. The model captures each by-value
  argument where it is evaluated (the call-site capture ruling), and every oracle holds there as everywhere
  else. Fixed by the ruling: seeds 67, 6006, 6461, 6702, 6763, 7126, 7152, 7165, 7289, 7398 (the backends agreed
  with each other, not with the ruled order) and 6809, 7284 (`math::max(A[i], w())` with `w` writing `A[i]`: the
  vm's graft captured the element, the interpreter and a kept call read it after `w` returned).
- The model abstains (the run still holds the first two oracles) on an error, an overflow and on output past
  the harness's print cap.

## Maintenance protocol

- New divergence class you are not fixing yet: minimize (`--minimize`), write the corpus
  entry (`--corpus`), report it, then add a matcher keyed on the MINIMIZED shape plus a row
  here.
- When a fix lands: re-run the recorded seeds with `--no-suppressions`; delete the matcher
  and move the row to a "fixed" note. Matchers are substring-based and deliberately broad —
  after a fix lands, the re-run also flushes anything the matcher was masking.
