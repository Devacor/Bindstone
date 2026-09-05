# Hardening 1: process-owned test fixtures

## Decisions

One lazy, immutable process-lifetime owner reserves a temporary directory with an
atomic filesystem create. Its name combines PID, a monotonic timestamp and a retry
suffix, so concurrent processes and later PID reuse do not share fixtures. Cleanup
is best effort and owns only that reserved child directory, never the system temp
root. No mutable static or thread-local state was added.

All nine Foundry file-writing test units, including the Release-only parse-key
benchmark, and the standalone runner selftest now use this root. Include search
directories, sibling `.jaibite` caches and cleanup targets follow it. The selftest
retains its paths-with-spaces coverage. Deferred include-test callbacks now capture
their local helper lambdas by value; their previous reference captures dangled
after `forge_tests()` returned. Unrelated formatting was not changed.

The fuzz runner's explicit `--corpus` output is a persistent user-requested artifact,
not a temporary test fixture, and is unchanged. Normal engine file resolution,
language semantics and runtime hot paths are unchanged. This task therefore does
not require a runtime A/B benchmark; concurrent suite durations below are not
performance claims.

## Verified by running

Before the fixture fix, the new assertion in
`Exception Handling.execute_file_stamps_stack_frame_file` failed on both backends:
one test, one failure each, with
`File fixtures must live in a process-owned directory, not the shared temp root`.
After the fix it passed on both (0.021 s interpreter, 0.014 s VM).

Three consecutive rounds per build: each round launched two interpreter and two
VM full-suite processes concurrently. All 24 processes exited zero, with 2,613
tests per Debug process and 2,733 per Release BENCHMARKS process. Foundry-reported
durations in seconds (each cell lists the two concurrent copies):

| Build | Round | Interpreter seconds | VM seconds |
| --- | ---: | --- | --- |
| Debug | 1 | 55.743 / 55.796 | 50.841 / 50.815 |
| Debug | 2 | 52.772 / 52.739 | 48.057 / 48.039 |
| Debug | 3 | 50.127 / 50.384 | 45.361 / 45.043 |
| Release | 1 | 56.172 / 55.895 | 28.121 / 27.750 |
| Release | 2 | 57.244 / 57.243 | 29.194 / 29.229 |
| Release | 3 | 56.467 / 56.035 | 28.809 / 28.859 |

- Release standalone runner selftests: 25 passed, zero failed in each of two runs.
- Release Bindstone integration build succeeded; `-verifyassets`: 76 passed,
  zero failed.
- GLOOM, from `examples/gloom`, `--smoke --ticks 300 --seed 666`: both backends
  retained frame `7fecc09815bb64e0` and state `3580805725` at both 0 and 8 workers.
  Observed total milliseconds: workers 0, interpreter 2774.7 / VM 930.5; workers 8,
  interpreter 1712.1 / VM 574.4. Other suites were running; these are hash-gate
  observations, not controlled timing comparisons.
- `git diff --check` passed.

## Inferred from reading / limits

The helper also has a POSIX `getpid()` branch; this session tested Windows only.
Separately launched suite processes are supported. Fork-without-exec after the
owner has initialized would inherit the parent's directory and is not the suite's
process model. Forced termination may leave an old directory behind; a fresh run
reserves another one and never adopts or deletes that old directory.
