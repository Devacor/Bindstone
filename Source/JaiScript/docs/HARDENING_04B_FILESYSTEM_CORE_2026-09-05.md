# Filesystem privilege: core checkpoint

Checkpoint requested by the owner before finishing runner/debugger integration.
This is not the final Task 4 acceptance report.

## Decisions

- Filesystem is enabled by default. `jai::filesystem` aliases the scoped
  `jai::privilege::filesystem`; no other proposed privileges are implemented yet.
- Grants are plain engine-owned bits: no new atomics, locks, globals or TLS.
  Change them while idle or on the execution thread, including inside a native
  callback. Active parallel regions reject changes until workers join.
- Changes affect future operations only. Started operations are not cancelled.
- The one checked gate supplies `Insufficient privilege: filesystem.`. Script
  catches see that text; uncaught checked errors retain the existing category
  suffix. Public throwing host APIs translate the same checked refusal.
- Core path queries, include/import and explicit file/jaibite load/save require
  the grant. Automatic disk caching and checker disk discovery are skipped when
  disabled. Host-supplied source/bytecode bytes and filename labels remain usable.
- Requirements attach to actual native callables, including method overloads;
  saved aliases and reflection do not bypass revocation. Unrestricted callables
  keep their original invocation path. `reflect::is_enabled` checks all supplied
  valid flags and cannot grant access.
- Preserve Fable's existing resolution ladder and normal missing-file behavior.
  Separate engines isolate script environments, not deliberately shared native
  objects, arbitrary host callbacks, or the process itself.

## Verified by running

- Debug library, runner and Foundry executable build succeeded.
- 15 focused filesystem tests pass on each backend: interpreter 0.060 s;
  VM 0.060 s. These cover native-callback revocation, saved aliases, methods,
  reflection, engine isolation, caught/printed text, include/import, explicit
  host I/O gates, skipped sidecars, and restricted checker discovery.
- Tests first: the identical final 15 tests compiled against `1b406166` headers
  and its preserved Debug library fail on both backends. Initial 12-test runs
  also failed before implementation. Logs: `out/hardening-filesystem/`.
- `git diff --check` is clean.

## Pending, not claimed verified

Runner file/directory/archive/download/audio-file requirements, debugger path
normalization without background-thread grant reads, final original-site docs,
full Debug/Release suites, Release Bindstone integration and 76/76 asset gate,
site samples, GLOOM hashes and interleaved performance measurements. Existing
debugger red tests remain outside this checkpoint. No performance conclusion is
drawn from the focused test timings.
