# Hardening 4 preparation: reserve the engine namespace

Status: implemented and verified on the combined `30667b88` tree. This is the separately
approved namespace reservation, not completion of the privilege/source-policy API.

## Decisions

- Extend the existing `is_reserved_namespace_name` predicate: scripts cannot
  declare or reopen `jai`, `reflect`, or either root's qualified subnames.
  This applies with checking off or strict and when executing a saved program.
- Reject before processing members or running their initializers. C++ may still
  populate either namespace; existing host entries remain readable from script.
- Match whole names and `::` prefixes, not arbitrary string prefixes: `jailed`,
  `jai_script`, `reflector`, and `outer::jai` remain ordinary names. `math` stays open.
- One checked-result error kernel feeds both backends. Generalize the old
  reflect-only parenthetical to `(C++-registered only)`; the complete diagnostic
  is catalogued in `diagnostics.md` and pinned byte-for-byte in tests.
- No new mutable global/TLS state, namespace registry, call/member/subscript
  checks, privilege defaults or revocation policy. No source resolver changes.
- Preserve Fable's concurrent path work (`624927b1`, `30667b88`); future source
  policy work must use that newer shared ladder rather than the earlier inventory.

## Tests-first evidence (run)

Against `52664db7`, before the implementation changed, four new regression tests
failed on each backend: script declarations into `jai` were accepted, host values
could be replaced by reopening it, saved programs could declare into it, and the
shared error wording was still reflect-specific. The prefix-boundary positive
control passed. Logs: `out/hardening-namespaces/red-{interpreter,vm}.log`.

The new tests cover functions, variables and classes; root/sub/deep names;
checker-off/strict execution; host registration and initializer side effects;
saved-byte loading/execution and engine recovery. They do not introduce actual
privilege constants or `reflect::is_enabled` yet.

## Final verification (run)

| Gate | Interpreter | VM |
| --- | --- | --- |
| Focused Debug reflection selection | 32 passed, 0.245 s | 32 passed, 0.279 s |
| Debug full suite | 2,696 passed, 97.404 s | 2,696 passed, 69.297 s |
| Release full suite | 2,816 passed, 56.313 s | 2,816 passed, 28.011 s |

Both builds completed and a second incremental build reported no work to do.
Release Bindstone integration build succeeded; `-verifyassets`: 76 passed, 0 failed.
Site verifier passed: 31 pages, 178 samples, 13 host-dependent skips, 25 matching
intentional-error samples. No new build was launched for the subsequent read-only
review of Fable's ladder commits.

GLOOM smoke: 300 ticks, seed 666. Both backends and worker counts retained frame
`7fecc09815bb64e0` and state `3580805725`. Single-run observations, not A/B timings:

| Workers | Interpreter total ms | VM total ms |
| --- | ---: | ---: |
| 0 | 2774.2 | 679.0 |
| 8 | 1758.5 | 587.1 |

Logs are under `out/hardening-namespaces/`. Full-suite and smoke timings are
correctness-run observations; some verification overlapped other work on the machine.

## Performance scope (source inspection)

The extra comparisons occur only when executing a namespace declaration. Ordinary
function binding, member access, subscripts and parallel-region execution are
unchanged, so no hot-path A/B claim is made for this preparatory change.
