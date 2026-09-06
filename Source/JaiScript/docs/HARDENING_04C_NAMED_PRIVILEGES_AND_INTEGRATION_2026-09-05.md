# Named privileges and filesystem integration

Continuation of the core checkpoint `627176b6`. Acceptance checks below are still
in progress; do not read this draft as a completed Task 4 report.

## Decisions

- Custom permissions use ordinary names: `register_privilege("app.edit")`,
  `enable("app.edit")`, and `add_function("save", save, {"filesystem", "app.edit"})`.
  No public handle/set type, engine identifiers, or cross-engine ownership machinery.
- Registration is explicit, idempotent, and initially disabled. The existing
  `"filesystem"` name is an alias, not a second grant. Unknown-name queries return
  false; unknown grant/revoke/require/binding requests reject instead of registering.
- Lists require every name. Refusals name a missing builtin first, then a missing
  custom permission in registration order. All names are validated before a grant
  list changes anything. Empty host lists impose no requirement.
- Names are owned by the existing engine interner. Binding consumes caller views
  immediately and resolves them once; invocation does not look up names or allocate.
  The registry, grants, and resolved callables belong to the engine. No mutable
  global/static/TLS permission state and no new locks or atomics.
- The original builtin flag API remains. Unguarded callables and builtin-only guards
  retain their existing invocation paths. Aliases, overloads, reflected calls and
  worker callable copies carry the actual guard. Grants change only on the engine
  thread/while idle, never during an active parallel region. Started calls finish;
  subsequent calls, including nested execution, see revocation.
- Runner file/directory/archive/hash operations, audio-file starts and download
  starts declare filesystem requirements. Download progress remains available except
  the POSIX curl-executable fallback's `received()`, which polls the output file.
  This is not a network/audio permission or cancellation mechanism.
- Debugger DAP path normalization now happens on the execution thread when applying
  the existing published snapshot. No transport-thread read of the permission mask.
  Native labels remain exact; denied DAP paths remain exact virtual labels. Existing
  snapshot/version synchronization is reused; no new queue, lock, or atomic field.
- Fable's source resolution ladder remains intact. The original site is updated;
  site-v2 is untouched. This is an engine filesystem capability boundary, not an OS
  sandbox or a general host-provided virtual filesystem. Arbitrary native APIs and
  deliberately shared C++ objects still require host discipline.

## Verified by running so far

- String API regressions compiled against `627176b6` headers and its preserved Debug
  library: 17/17 fail on interpreter (0.011 s) and VM (0.010 s), because the string
  registration/grant API is absent. The first 15 were red before implementation;
  the final two additional cases were also demonstrated red on that same commit.
- Current isolated Debug custom suite: 17/17 pass, interpreter 0.023 s / VM 0.022 s.
  Includes independent engines using the same names, borrowed name lifetime,
  registration growth past 64 names, checked/caught/printed text, no interning on
  queries, active-region refusal, worker callable copies and nested revocation.
- Runner tests first: private `627176b6` Debug runner before gates, 47 pass / 48 fail;
  after gates, 95 pass / 0 fail. The selftest explicitly exercises both backends.
  It uses process-owned fixtures, unsupported URL schemes, and nonexistent audio
  files; no real network transfer or audible playback is needed.
- Debugger baseline: 18/20 pass on each backend, with precisely the two new restricted
  path failures. Added native/DAP same-spelling control also passed before runtime
  edits. Current isolated Debug suites: interpreter 21/21, 7.301 s; VM 21/21, 7.328 s.
- Full Debug suites: 116 suites / 2,736 tests on each backend; interpreter
  115.575 s, VM 75.369 s, no failures.
- Current Release runner selftest: 95 pass / 0 fail, both backends included.
- Release Bindstone integration build succeeds; `-verifyassets`: 76 pass / 0 fail.
- GLOOM `--smoke --ticks 300 --seed 666 --workers 0`, then `--workers 8`:
  interpreter and VM both retain frame `7fecc09815bb64e0` and state `3580805725`
  in all four cases. These runs were hash gates, not isolated performance trials.
- Original-site verifier passes all checks: 31 pages, 1,427 internal links,
  regenerated search index; 180 samples discovered, 13 host-dependent samples
  skipped, 25 intentional-error samples matched, executable samples run on both
  backends. No failed checks.

Raw logs/private binaries are under `out/hardening-filesystem/` (ignored artifacts).
Focused test timings are correctness evidence, not performance measurements.

## Pending acceptance

Full Release suites on both backends; alternating Release A/B against both `627176b6`
and pre-core `1b406166`, at least three runs of each binary per workload/backend.

## Inferred from reading, not separately execution-proved

The POSIX progress fallback gate was inspected but not compiled on Windows. Already
started audio/download operations are unchanged and keep their original completion
behavior; revocation during a real transfer/playback was not exercised. Native
registration allocation-failure rollback was inspected, not fault-injected. The
existing interner's allocation/noexcept contract is unchanged.
