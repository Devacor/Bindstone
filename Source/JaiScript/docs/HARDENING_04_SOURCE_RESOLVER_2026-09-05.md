# Hardening 4: historical source-access inventory

Current decisions and verification are tracked in
[the integration note](HARDENING_04C_NAMED_PRIVILEGES_AND_INTEGRATION_2026-09-05.md).
The core checkpoint is committed as `627176b6`. Everything below is retained as
the earlier inventory/progress trail, not a claim about the current implementation.

## Historical work-in-progress snapshot (after `1b406166`)

This is NOT a completed decisions/results note. The inventory below is historical;
Fable's `624927b1` ladder is preserved by the current implementation work.

- Owner approved filesystem gating for path queries too, catchable checked errors,
  and revocation from a C++ callback affecting future calls only (no cancellation).
- Owner then requested no atomics/locking. The initial atomic grant mask was
  removed: grants are plain engine-owned bits. Configuration is engine-thread/idle
  only, and active parallel regions reject mutation. No new synchronization remains
  in the privilege implementation.
- Current API slice implements `jai::privilege::filesystem` / `jai::filesystem`,
  `enable`, `disable`, `is_enabled`, `require_privilege`, and protected callable
  registration. No other proposed privilege is advertised as implemented.
- Core source/cache/path gates and protected callable wrappers are implemented
  locally, not yet fully verified or committed. The debugger runtime and runner
  file/archive/download/audio-file bindings still need their integration work.
- Debugger normalization currently runs on its transport thread. Do not read the
  new plain mask there. A possible follow-up is ordered breakpoint updates consumed
  on the engine thread using the debugger's existing publication mechanism; this
  redesign is paused, not implemented.
- Tests first: all 12 initial regressions failed on both unchanged backends
  (0.051 s each). Expanded 15-test battery compiled against `1b406166` headers and
  the preserved Debug library: 15 failures on each backend (interpreter 0.020 s,
  VM 0.021 s). Logs: `out/hardening-filesystem/` and `baseline-probes/` below it.
  A fresh disk sibling returned 7 instead of supplied-source result 42; disabled
  queries/aliases/includes/imports/native bindings did not reject access.
- Original-site documentation edits are prepared. Static HTML/link/chrome checks
  passed (31 pages, 1,424 internal links); search regeneration, sample execution,
  full suites, Release integration/assets, hash gates and interleaved A/B remain.
- Previous Release runner/GLOOM binaries and Debug library are preserved under
  `out/hardening-filesystem/baseline-1b406166/`; baseline headers are in the isolated
  `out/hardening-baselines/1b406166-tree` checkout.

Latest focused verification: Debug tests/runner build succeeded after removing the
atomic mask. All 15 filesystem regressions pass on interpreter and VM (0.060 s
each), including revoke-and-resume from a native callback, aliases, methods and
reflection. Tests preserve the existing uncaught `: <category>` suffix while
checking caught/printed catalog text exactly. The identical final tests still fail
15/15 against the previous headers/library. This is NOT a full-suite/integration
pass. The added debugger virtual-label regression is deliberately still red:
baseline interpreter 1.514 s / VM 1.525 s, one failure of two tests each; its enabled
normalization control passes. Debugger runtime work is paused for the no-new-sync
design review. No commit has been made for this task.

Everything below predates the privilege implementation and is retained as an
inventory trail, not as a current API/behavior assertion.

Status: read-only inventory at `52664db7`; no resolver implementation or policy
change yet. The restricted-policy scope below requires the owner's decision.
Task 3's committed results are in `HARDENING_03_SAVED_PROGRAM_VALIDATION_2026-09-05.md`.

Update: this inventory predates Fable's `624927b1` / `30667b88` path consolidation.
The shared relative-path ladder now starts at the asking file's folder, then
include prefixes, then CWD; location queries moved into the engine and the runner's
`paths.hpp` was removed. Preserve that work. Re-inventory the current entry points
before implementing privileges; the tables below are historical baseline evidence.

## Engine/library paths found

Line numbers below refer to the inventory baseline, before Task 4 edits.
Paths are relative to `Source/JaiScript`.

| Location | Path operation / callers |
| --- | --- |
| `source/implementation/interpreter.cpp:2144` | Existing shared `resolve_include_path`: `exists`/`canonical` at 2146-2147; then configured include prefixes joined/tested/canonicalized at 2151-2157. Declaration: `include/jaiscript/core/engine.hpp:1142`. |
| `source/implementation/interpreter.cpp:12266` | Shared interpreter include-declaration/expression implementation: resolves at 12293, opens at 12300, reads at 12306-12308, calls `engine::execute_source` at 12312. Wrappers at 12319/12323. |
| `source/implementation/vm/vm_backend.cpp:11843` | VM include: resolves at 11860, independently opens at 11866 and reads at 11870-11872, then `engine::execute_source` at 11878. |
| `source/implementation/interpreter.cpp:12327`; `source/implementation/vm/vm_backend.cpp:11894` | Import resolves at 12354 / 11911; both delegate to `engine::execute_import` at 12361 / 11918. No separate import stream in either backend. |
| `source/implementation/engine.cpp:704` | Literal top-level dependency extraction; `collect_closure_units` at 751 resolves at 756, stats at 760, opens at 762, reads at 765-766, acquires source/cache entry at 769. Recursive closure discovery at 778. |
| `source/implementation/engine.cpp:785` | `closure_due` stats dependencies at 791. Called from `checked_report` at 828; reachable from ordinary source execution, cached execution, and `engine::check` at 1913. |
| `source/implementation/engine.cpp:1015` | Shared `stat_mtime_size`: Windows `GetFileAttributesExW` at 1019; elsewhere `last_write_time`/`file_size` at 1030-1031. Used by execute-file, dependency checking, and timestamp imports. |
| `source/implementation/engine.cpp:720` | `acquire_entry` derives and probes sibling cache at 724-727; `.jaibite` sibling naming at 1061; freshness reads cache/source timestamps at 1073-1074. |
| `source/implementation/engine.cpp:1083` | Automatic cache opens/reads binary sibling at 1086-1090; corrupt/stale/fingerprint-mismatched entries fall back to source parsing. |
| `source/implementation/engine.cpp:1118` | Automatic cache opens/truncates sibling at 1127, writes/closes at 1132-1133, removes partial failed write at 1137. Best-effort failure counter, not a script error. |
| `source/implementation/engine.cpp:1643` | `execute_file_source` receives host-provided text but still invokes the sibling-cache path. `execute_source` acquires at 1689 and writes stale/missing sibling at 1709. A virtual-looking filename is not a disk-access barrier. |
| `source/implementation/engine.cpp:1837` | Explicit `jaibite::save`: open/truncate at 1839 and write at 1843. Unlike `save_bytes`, this is a disk API. |
| `source/implementation/engine.cpp:1849` | Explicit `engine::jaibite_load`: open at 1850, read at 1854, then the in-memory loader. `jaibite_load_bytes` itself does not read a file. |
| `source/implementation/engine.cpp:1971` | `execute_file`: stat at 1978, `weakly_canonical` at 1984, equal-stamp verification read at 1997-2000, normal source open/read at 2015-2022. In-memory file cache is keyed by canonical path. |
| `source/implementation/engine.cpp:3167` | `execute_import`: timestamp-policy stat at 3189; open/read at 3216-3223; execute at 3226; record post-execution stamp at 3232. Import-once and cycle tracking share this entry. |
| `source/implementation/debug/connector.cpp:53` | Debugger breakpoint normalization calls `canonical` at 56 and `weakly_canonical` at 58; invoked for incoming breakpoint paths at 629. These are disk metadata queries even without file reads. |

## Findings from reading, not new runtime proofs

- Default include/import lookup tries the path relative to the process working
  directory first, then the configured include prefixes. It does not implicitly
  search relative to the including source file. Preserve that ordering.
- `type_checker.cpp` itself does not access files: it consumes supplied trees and
  name/class maps. The engine's discovery walk opens only literal top-level
  include/import declarations, not dynamic paths, nested statements or expression
  includes. That walk is best-effort; unresolved/unreadable units are deferred to
  runtime. `execute("...")` and `check("...")` can therefore cause dependency I/O.
- `jaibite_cache(false)` disables sibling caching, not includes, imports, metadata
  probes or explicit bytecode load/save. It is not a source-access restriction.
- Import-once and timestamp import share `execute_import`; no independent
  `include_once` language door was found. Include/import are already rejected by
  parallel admission (`parallel_transform.cpp:980-982,1076-1077`).
- VM compilation lowers include/import nodes; it does not open them independently.
  Archive classes consume supplied bytes/streams rather than opening source paths.
  Host-provided streams, native bindings, and custom policy code retain the host's
  own authority; an engine source policy cannot intercept arbitrary C++ I/O.

## Runner and host-owned paths (scope distinction)

The standalone runner deliberately registers more than a bare engine. A restricted
source resolver must not be advertised as restricting these native bindings.

| Location | Host-owned operations |
| --- | --- |
| `tools/jaiscript/main.cpp:990,998,1022-1034` | `read_file`, `write_file` (including parent-directory creation), `file_exists`, `delete_file`; console, networking, fetch and path APIs registered on every runner engine. |
| `tools/jaiscript/main.cpp:1121-1201` | CLI source read, absolute path construction, existence checks, file/watch mode and reload timestamp polling. |
| `tools/jaiscript/main.cpp:1293,1328,1373-1379` | REPL pasted-file detection, CWD include prefix and file-command checks/path normalization. |
| `tools/jaiscript/paths.hpp:53-91` | Shared `generic_dir` uses `weakly_canonical`; executable/CWD discovery includes platform APIs and Linux `/proc/self/exe` readlink. |
| `tools/jaiscript/paths.hpp:108-179` | App-data/cache directory selection and creation; root/current script path helpers use absolute paths and the shared canonicalizer. |
| `tools/jaiscript/paths.hpp:182-214` | Registered location helpers, `working_dir`, `app_data_dir`, `app_cache_dir`, `dir_exists`, `make_dirs`, `list_dir`. `parent_dir` also calls the disk-probing canonicalizer despite its lexical-looking first step; `file_name` is lexical only. |
| `tools/jaiscript/fetch.hpp:114,340,385-386` | File hashing, ZIP entry reads and ZIP extraction, including destination directory creation/write. |
| `tools/jaiscript/fetch.hpp:409,421,441,491,539,591` | Download destination directory creation, size checks, partial-file removal and platform-specific output streams. |
| `tools/jaiscript/main.cpp:509,534` | `play_wav`/`play_midi` pass file paths to Windows multimedia APIs; no-op on other platforms. |
| `tools/jaiscript/main.cpp:955`; `tools/jaiscript/fetch.hpp:468-585` | Explicit TCP host bindings, HTTP(S) download bindings and platform networking implementations (including a POSIX curl subprocess fallback). Not core source loading. |

Other inspected demo-host paths: crawler file lookup/read at
`examples/crawler/main.cpp:246-274` and save bindings at 399-408; demoreel
scene lookup/read at `examples/demoreel/main.cpp:379-399`, explicit bytecode
load/save at 464-475 and output at 558/972; GLOOM script lookup at
`examples/gloom/gloom_host.cpp:65-79`, frame dump at 248 and normal imports in
`gloom_adapter_jai.cpp:73-86`. These are host capabilities, not new resolver doors.

Preserve the recent source-location work: engine root/current source identities,
v14 moved-file rebinding and their tests (`exception_handling_tests.cpp:331,361`).
JaiDOOM uses host APIs for game-root discovery, writable-file probing and fallback
(`examples/jaidoom/scripts/wad.jai:45-83`), downloads/extraction/provenance
(`boot.jai:16-27,103-229`), save/demo files (`save.jai:22,113`, `demo.jai:70,78`)
and generated audio (`sound.jai:34-42`, `mus.jai:127-136`). Those default behaviors
must remain unchanged; restricting core sources alone does not restrict them.

Runner selftests, Foundry fixtures, fuzz corpus files, build tools, documentation
verification and demo-host asset I/O are host/tooling activity, not language source
resolution. Existing behavior should remain unchanged by default. No runner
restricted-mode switch or broad native-I/O interception is proposed in this task.

## Documentation corrections identified

- Primary claims: `docs/why_jaiscript.md:81-82` and
  `docs/site/embedding/05-safety-rails.html:35` deny built-in file access despite
  include/import and automatic sidecars.
- Related no-ambient-authority summaries: `docs/why_jaiscript.md:142-144`,
  safety-page metadata at 7/13 and closing paragraph at 97,
  `docs/site/embedding/index.html:41`, and the generator entry in
  `docs/site/assets/tools/apply_chrome.py:71`.
- `include/jaiscript/core/engine.hpp:163` and the source-execution comment at
  `engine.cpp:1685` overstate that string execution never touches disk: it does
  not cache the root string on disk, but its dependencies can still access files.

## Proposed restricted contract (awaiting decision)

1. One engine-owned policy routes source resolution, source reads, metadata,
   automatic cache reads/writes/removal, explicit bytecode load/save, and debugger
   source-path normalization. Keep the existing resolver as the shared front door,
   not a second filesystem implementation beside it. No mutable global/TLS policy.
2. The default filesystem policy preserves today's lookup order, attribution,
   import behavior, cache rules and normal diagnostics.
3. Restricted means no engine-initiated filesystem operations, including metadata.
   Reject path-based source/bytecode requests with one catalogued, catchable text;
   permit host-supplied source/bytecode bytes and filename labels. Automatic disk
   caching is skipped, not an error. Debugger path handling must not probe disk.
4. The policy is not a sandbox for arbitrary host bindings or supplied streams.
   Runner/game native file and network functions remain explicitly host-controlled.

The owner's decision is needed on whether restricted should also refuse explicit
host calls to `execute_file`, `jaibite_load`, and `jaibite::save`, or leave those as
privileged bypasses. The recommendation is to refuse them too and use the existing
in-memory APIs when the host intentionally supplies a program.

## Verification still to do

No Task 4 runtime behavior has been changed or tested. After the policy decision:
tests first against this baseline, both complete backend suites, exact rejection
text checks, no-disk/cache-bypass checks, custom in-memory resolver consistency,
default file/cache/import regression tests, Release Bindstone 76/76, site verifier,
and both GLOOM hash gates. Benchmark any affected warm execution path with the
required interleaved three-trial A/B. Report actual observations separately from
the source conclusions above before committing Task 4.
