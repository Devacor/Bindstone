# Latest compiler review, 2026-09-14

Two reproducible correctness issues remain: the new cell push can observe a getter without refreshing cached cells, and the held literal-include change can consume an older parsed cache that skips splicing. The saved-callable unit fix is verified. The include return rule remains a policy question.

## Sources and scope

| Source | Identity | Role |
|---|---|---|
| Landed master | 9ede9f6d9dd89326807af4830b38fe409d248d0d | Rows 127 through 134 plus the earlier callable fix |
| Held nested loops | 7b3987091927b9fb83ae8fa551554be7327663d9 | Row 135, before the later table-pin correction |
| Held literal splice | f503a7d879de48a27897f13bb7f297e5b5671c97 | Existing frozen library used for include return/cache controls |

Runtime sources were read from local Git archives and prior frozen snapshots. No Git worktree was accessed. Fresh builds and hosts live under `Source/JaiScript/out/astra/resume-review-20260914/`, abbreviated `raw/` below. Only the isolated CMake verification-host overlay differs from archived source. `raw/verification-inputs.json` records the comparison and binary hashes.

## 1. C_PUSH keeps stale cells after an observable element read

**Confirmed, requires a runtime correction.** A native embedding stores an owner-pinned property reference in an `array<var>` using its native array view. The getter increments global `G` by 10 and returns 1. A function performs four iterations:

    dst.push(SRC[0]);
    sum = sum + G;

| Mode | Sum | Getter calls | Final G |
|---|---:|---:|---:|
| Interpreter, proofs on or off | 100 | 4 | 40 |
| VM, proofs off | 100 | 4 | 40 |
| VM, proofs on | 40 | 4 | 40 |

Both source tips reproduce this in Debug and Release, with IR passes on and off: 32 mode runs, eight wrong optimized-VM results. The mismatch is stale state after observation; getter execution count alone does not catch it.

Reading `exec_c_push` explains the result: a heterogeneous source selects an element and calls `pv->deref()` while the cell block remains active. It does not apply the observing-operation flush/re-landing protocol. Admission of the outer array does not prove that its current element is free of getters.

The conservative correction is to miss before an effectful element dereference and let the original operation execute once. Missing after dereference would replay the getter. Supporting such reads inside a block requires the full effect protocol, including operand lifetime, receiver snapshots, produced-value retention and post-observation validation. This rule should be shared by operation families rather than patched for the benchmark's data shapes.

Evidence: `raw/push-getter.cpp`, `raw/native-results.json`, both tips' `debug-push-getter.log` and `release-push-getter.log`, and `raw/PUSH_GETTER_FINDING.md`.

## 2. Literal splicing needs parsed-cache invalidation

**Confirmed on the held row 126 source.** The included file contains `return 3;`; the function includes it and then executes `return 7;`.

Using the exact same source pathname and held executable:

| Cache state | VM result |
|---|---:|
| Parsed cache produced by pre-splice master | 7 |
| That generated cache backed up and removed | 3 |
| New cache produced by the splice build | 3 |

Fresh-path tests also return 3 on both backends, cold and warm. A separate newly compiled host linked to the frozen splice library confirms that fresh parsing splices the return into the surrounding function.

The parser change must invalidate pre-splice parsed units. Otherwise source behavior depends on whether a previous executable populated the cache. The initial cross-version CLI results are retained but do not establish fresh-parse semantics.

Evidence: `raw/include-return/same-path-cache-results.json`, `fresh-results.json`, the preserved `literal-master-cache.saved`, and `raw/return-host/` containing the native host, CMake project and logs.

## 3. Include scope and return policy

Dev has approved reading and modifying the surrounding function's parameters and locals. Current master and row 135 still reject or fail those function-local includes. Literal support was verified previously in the held splice source; computed-path support remains an implementation gap, not an accepted restriction.

Fable's proposed immediately invoked reference-capture body is a workable direction, subject to these requirements:

- Capture the lexical scope visible at the include site. A flat walk of all declarations in the function exposes later or sibling-block locals and mishandles shadowing.
- Cache parsed/compiled templates by file identity and generation, lexical site and unit. Create live captures for each invocation, including recursion.
- Check the runtime-parsed body against captured types and preserve ordinary reference, read-only and observable-getter behavior.
- Keep diagnostics attached to the included file.

The proposed assertion that literal and computed returns already agree is disproved by the frozen-source runs. With an included `return 3;`, a literal include returns 3 from the surrounding function; a computed include finishes its file and the surrounding function returns 7. This is a policy question. Declarations flowing out of a computed include are a separate question. Neither changes Dev's existing authorization for parameter/local reads and writes.

Evidence and acceptance controls: `raw/COMPUTED_INCLUDE_PLAN_REVIEW.md`, `raw/return-host/run.log` and the include-return results.

## Verified fixes and gates

The saved named-function and anonymous-lambda unit identity fix in `c0adc0a95893e7ba6750a376186dc621dcb10195` is verified on both current source tips: saved callables from units A and B yield `11,22,11,22` on both backends, proofs/passes on and off, Debug and Release. The include harness records 128 rows per tip/build, including the still-open scope controls; it is not a blanket 128-pass scope claim.

| Gate | Master | Held row 135 |
|---|---|---|
| Full Debug interpreter | 3,139 passed | 3,140 passed |
| Full Debug VM | 3,139 passed | 3,140 passed |
| Existing 48-program semantics matrix | 384 runs passed | 384 runs passed |
| New 18-program script matrix | 288 runs passed | 288 runs passed |
| Release GLOOM and timedemo hashes | 18 checks passed | 18 checks passed |

The 18-program matrix covers branching, ternary landings, pushes, member arrays, nested control flow and fallback. The late-budget case enters the outer optimized block before its inner loop becomes endless. Its listing contains the backward `C_JUMP` with budget flag 1, and every mode raises within the bounded host timeout. Listings also confirm C_PUSH and member C_LOAD coverage. The matrix's original float expectation was corrected from `6.5` to the actual print spelling `6.500000`; original results are preserved, and all execution modes agreed.

GLOOM was checked at 300 and 600 ticks, seed 666, both backends, zero and eight workers, normal and padded executables. The 300-tick hashes are frame `7fecc09815bb64e0`, state `3580805725`; 600-tick hashes are frame `dfd969ed5a336dca`, state `1503537018`. The two timedemo checks per tip use VM normal/padded executables and state `1746772898`.

Raw gates: per-tip `debug-suites.json`, `pass-debug/`, `release-checks/hashes.json`; `raw/focused-results.json`, `focused-summary.json`, `listing-coverage.json` and per-tip `listings/`.

## Limits and handoff

No performance number is credited by this review. Builds and correctness gates ran while the box was shared; their durations are not benchmarks. The prepared baseline/master/row135 pair remains held for the C_PUSH correction. Fable's later row135.1 table-pin correction and the rebased row125 extraction have not been independently verified here. Release focused probes and game hashes ran; this turn did not rerun the full Release suites, fuzz or Bindstone client gate.

No runtime patch was applied. Findings and acceptance controls were sent through `out/astra/TO_FABLE.md`, `CURRENT_HANDOFF.md` and `QUEUE.md`; the status file keeps the timing window closed. The watcher records file changes with a 15-second fallback.
