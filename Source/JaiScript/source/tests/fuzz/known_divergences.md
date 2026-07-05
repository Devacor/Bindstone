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
| KD-CORO-RESUME-PAST-END | resume past the final `yield` of a coroutine with no `return`: interpreter repeats the last yielded value, VM yields null | 4, 7, 38, 116, 195, 216 | interpreter (a finished coroutine has no value; repeating the last yield invents one) | in-flight coroutine final-return fix territory |
| FZ-TYPED-FN-NO-RETURN | typed function (`-> bool` / `-> string`, …) falls off the end without `return`: interpreter raises bogus `Undefined variable '<some function-local>'` (e.g. the while-guard name), VM returns null | 66, 239 | interpreter (error names an unrelated local; either null or a "missing return" error would be defensible, this is neither) | NEW - found by fuzzer 2026-07-05 |
| FZ-UNDEF-VAR-THIS-DECORATION | undefined variable in compound-assign: interpreter error text carries `(no 'this' in scope)`, VM's does not. Same seeds also expose compound-assign *evaluation order*: `v1 += fn8()` with both sides undefined reports `v1` (lhs) on interpreter, `fn8` (rhs) on VM — C++17 sequences rhs first, so the interpreter order is the deviant one | 34, 28 | both texts must converge (invariant 6); order: interpreter | NEW - found by fuzzer 2026-07-05 |
| FZ-FLOAT-DIVZERO-TEXT | float division by zero: interpreter `Division by zero`, VM `Division by zero in float operation` (same error code, different text) | 201 | text parity violation; either side may win, they must match | NEW - found by fuzzer 2026-07-05 |
| FZ-RECURSION-MSG-TEXT | unbounded recursion: interpreter `Native stack exhausted - possible infinite recursion`, VM `Maximum recursion depth () exceeded - possible infinite recursion` (note the VM's empty `()` where the depth should be) | 137 | text parity violation + VM message formatting bug | NEW - found by fuzzer 2026-07-05 |
| FZ-TYPED-FN-NO-RETURN-CAUGHT | same bug as FZ-TYPED-FN-NO-RETURN with the bogus interpreter error caught by a script `try/catch`: interpreter prints `caught: Undefined variable '<local>'`, VM prints nothing (it returned null) | 2529, 3657, 3784 | interpreter | NEW - found by fuzzer 2026-07-05 |
| FZ-NEG-ZERO | unary minus on a `0.0`-valued float: interpreter yields `+0.0`, VM yields IEEE `-0.0` (visible via to_string: `0.000000` vs `-0.000000`) | 1985, 2148, 2255 | interpreter (IEEE 754 negation of +0.0 is -0.0; C++ agrees; the VM preserves it) | NEW - found by fuzzer 2026-07-05 |
| FZ-OVERFLOW-OP-NAME | checked-integer-overflow error text names the wrong operator, and a DIFFERENT wrong operator per backend (minimized: a `-` overflow inside a for body reports `Integer overflow in '*'` on interpreter, `Integer overflow in '+='` on VM) | 2706 | both (each names a wrong operator); must also converge textually | NEW - found by fuzzer 2026-07-05 |
| FZ-SPACESHIP-OVERFLOW-SWALLOW | checked integer overflow inside a `<=>` operand (minimized: `int v = ((-((1 + INT64_MAX))) <=> INT64_MAX);`): interpreter completes silently with null, VM raises `Integer overflow in unary '-'` | 9336 | interpreter (checked-overflow policy is "safe by default": the error must surface; note the VM's operator attribution is also wrong - the `+` overflowed) | NEW - found by fuzzer 2026-07-05 |
| KD-ORDERING-THROW | `throw` inside a class `operator<` swallowed by one backend | none observed in pilot (fix may already be landed) | n/a | matcher retained defensively |

## Harness lessons encoded (not engine bugs)

- A script can CATCH the execution-budget error and keep going, making printed output
  wall-clock dependent (campaign seed 324: a `var&` mutator resetting a for-loop variable
  produced an infinite loop whose `catch` swallowed the budget error). The harness now marks
  any segment consuming >=90% of the budget as budget-hit and skips the program.

## Watched (no matcher)

- `obj + primitive` where class `operator+` expects an object operand — listed as an
  in-flight crash fix; the pilot (300 seeds) produced no crash, so no matcher. A crash
  cannot be suppressed anyway: it surfaces as a dead campaign process (in-process mode)
  or a nonzero child exit (`--isolate`).
- for-body `var` redeclare scoping — not observed in the pilot; possibly already fixed.
- Seed 66's ORIGINAL (unminimized) program also showed the VM erroring
  `Undefined variable 'to_string'` where the interpreter said `Undefined variable 'g4'`
  — the minimized form loses that aspect; worth a second look when FZ-TYPED-FN-NO-RETURN
  is fixed.

## Maintenance protocol

- New divergence class you are not fixing yet: minimize (`--minimize`), write the corpus
  entry (`--corpus`), report it, then add a matcher keyed on the MINIMIZED shape plus a row
  here.
- When a fix lands: re-run the recorded seeds with `--no-suppressions`; delete the matcher
  and move the row to a "fixed" note. Matchers are substring-based and deliberately broad —
  after a fix lands, the re-run also flushes anything the matcher was masking.
