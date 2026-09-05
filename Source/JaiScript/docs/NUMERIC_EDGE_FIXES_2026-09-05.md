# Numeric edge fixes — 2026-09-05

Small follow-up to the prelaunch correctness batch; no new numeric representation,
overload machinery, arithmetic dispatch changes, or compiler policy switches.

## Changes

- `math::min`, `math::max` and `math::clamp` compare integer pairs as int64 values,
  fixing operand selection above 2^53. Mixed pairs still promote to double. Selected
  operand types, ties, signed zero and existing floating/NaN selection behavior stay
  unchanged.
- One `checked_float_to_integer` kernel validates scalar float-to-int conversions:
  `math::itrunc`, `math::ifloor`, scalar typed declarations/assignments, fields,
  parameters, returns, `to_int()` results, and `cast<int>`. Non-finite/out-of-range
  input returns `checked_result` with one catalog text; the error is catchable on
  both backends. Invalid scalar stores leave the destination unchanged.
- Floating RNG seeds use the same kernel specialized for uint64, preserving their
  existing representable unsigned domain. Integer seeds retain their bit patterns.
  Invalid floating seeds fail before changing the generator.

The exclusive upper boundaries are 2^63 (script ints) and 2^64 (floating seeds),
not the integer maximum rounded to double. Range comparisons also reject NaN and
infinities. Signed conversion needs no extra `trunc` call: adjacent binary64 values
at these limits are already integral. `ifloor` floors before conversion as before.

## Scope and performance

The int/int arithmetic loops and value layout are untouched. Exact integer
selection avoids conversion through double; checked float-to-int boundaries add
ordered range comparisons, without allocations or symbol interning. Both backends
use the same kernel, including during parallel execution.

This is not a blanket numeric conversion rewrite. The public C++ `as_int()`
accessor retains its existing saturation contract. Bare stdlib `min`/`max`/`clamp`
retain their float-only signatures; the exact selection fix is in `math::`.
Low-level typed-container/reference stores and native-width narrowing conversions
remain a separate audit; this batch does not claim to remove every unchecked cast.
Mixed int/float precision, floating arithmetic overflow, and literal parsing are
unchanged.

## Verification

Six new regression tests, including expected-answer cases around 2^53, int64 and
uint64 limits, NaN/infinity, bound C++ operands, mixed values, failed stores, and seed
sequence preservation. Four of six tests failed on both backends before the fix;
all pass after it.

- Debug: 2,613 tests passed per backend.
- Release (benchmarks enabled): 2,733 tests passed per backend.
- Differential fuzz, seeds 180000–182999: 2,938 agree, 62 execution-budget skips,
  zero new divergences, zero suppressions, zero crashes/hangs.
- Bindstone Release rebuilt; `-verifyassets`: 76 passed, zero failed.
- GLOOM, 300 ticks / seed 666, both backends at 0 and 8 workers: frame
  `7fecc09815bb64e0`, state `3580805725`, byte-identical to the required baseline.
- JaiDOOM full 3,984-tick replay: state `1746772898` on both backends, including
  a repeated VM run.
- `git diff --check` passed.

Hash checks establish unchanged workload results, not a controlled before/after
microbenchmark or a guarantee of zero timing variation.
