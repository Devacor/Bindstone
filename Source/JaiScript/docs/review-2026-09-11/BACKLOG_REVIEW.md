# Compiler backlog review, 2026-09-11

Update 2026-09-12: the two guard findings below are fixed and independently verified at 7fc75044 and its string-row descendant ee2af60c. See the resolution at the end of this document and the [final committed-tip comparison](../../examples/gloom/ports/RESULTS_FINAL_TIPS_2026-09-12.md).

All fifteen implementation items through 121a and the newly flagged string-cell row have been reviewed. Fourteen of the original fifteen clear with their corrective commits. The remaining row, 118d, has two reproduced defects in the latest committed guard implementation, e5b8c649. A conservative repair passes the independent checks described below in an isolated source copy. It is a proposal for Fable, not a landed runtime fix.

The original review base is bace83ee5a1ad1697c7753340028bf561350b19d. Sources came from local Git objects and Fable's exported patches. Builds, test hosts and raw evidence are under `Source/JaiScript/out/astra/backlog-drain-20260911/`. No Fable worktree was inspected or changed. No live runtime-owned file was edited.

## Acceptance and disposition

A source review requires reading the affected paths and executing controls that reach them. Full-suite success alone does not establish optimizer correctness. Controls compare both backends with proofs and passes independently on and off; listing checks distinguish a transform actually executing from success through fallback. Invalid source may be rejected by both static checkers. No var or warning-mode escape was added to conceal a valid-program failure.

| Row | Reviewed identity | Verdict and independent evidence |
|---|---|---|
| 116b1 | `48383949` | verified. original-regressions.json: removed_map_untaken/taken, 16 runs, source presence check reviewed |
| 118.1 | `b3ccb381` | source review and corrective checks passed. Compound RHS ordering and same-scope VM catch:32strict-mode controls pass;10aba full Debug3093x2, complete chain3097x2. |
| 118b | `00e75545` | source review and controls passed. Move lowering, widening and dynamic moves; move_adopt_widen and full pass corpus |
| 118c | `18479811` | source review and controls passed. Unary operand dereference, checked INT_MIN negation, IEEE sign and temp sinks; unary_nested_values/unary_late_overflow |
| 118d | `f5d21689` | Hold committed e5b8c649; two regressions, tested proposal below. e5-focused/astra_null_guard.out; liveness.json; conversion.json; original ordering and reload cases are fixed |
| 118e | `0d5a242a` | source review and controls passed. Lexer stoll bounds justify literal fold; sign/maximum controls and changing global counted bounds |
| 119.1 | `3b299e35` | source review and controls passed. loop-frame-controls.json:64 isolatedDebug/Release runs, counted/range/string loops with calls/continue/bound/step;full DebugsuitealsoPASS |
| 119a | `78b8c079` | source review and corrective checks passed. Argument/callee/local-reference getter ordering and post-getter failure controls pass inDebug/Release;discarded-result companion fix verified. |
| 119b | `3fb925fb` | source review and corrective checks passed. Declaration-stamp changes and discarded-result continuation now pass inDebug/Release. |
| 119c | `7e64b575` | source review and controls passed. One evaluation of value binary before index store, computed stores and effectful RHS control |
| 119d | `54675126` | source review and controls passed. Truthiness, bool moves/calls, fallback to general element conversion; bool_cells/element_fallback/var_kind |
| 119e | `e035fe40` | source review and corrective checks passed. 128-cell masks and declaration-stamp reload validated;node/binding source review complete. |
| 119f | `b69a0544` | source review and controls passed. Return/break statement boundaries, original-IP replay and flush; early_return_effects/early_break_counted/return_throw_flush |
| 120 | `fb91944d` | source review and controls passed. CFG must-definition intersection at joins/backedges, untouched snapshot guards; branch controls and128-cell boundary |
| 121a | `bace83ee` | source review and controls passed. Pass API compile sites and lambda propagation; kind-only elision with checked arithmetic retained; listing proves C_ARITH_P/C_CMP_P/C_JCMP_P on and checked forms off;768 corpus runs |
| 122 | `2400e9ff` | source and independent Debug checks passed; final guard integration pending. string-2400e9ff/debug-suites.json; string-checks/ |

The earlier design reviews of 115, 116a, 117 and 118a are complete. The canonical scalar-lowering direction and call-boundary design are supported by the source and controls in this review, subject to the concrete corrections below. The 119g v3 binding-token design is approved without entry reuse or element-content reuse; an implementation audit requires an implementation export. The virtual-stack proposal is shelved, and the older function-region proposal is future work. These are not unreviewed implementations in this batch. No 123+ work was added.

## Correctness findings and repairs already verified

| Finding | Reproduced behavior | Corrective commits and independent result |
|---|---|---|
| A compound assignment dereferenced an element before evaluating its RHS | A removed array/map entry restored to 20 by the RHS produced 22 on VM, but the interpreter raised with zero RHS calls | `10aba0f1` defers the read; `af702b0c` also makes the program-scope catch work on VM. Restore, throwing RHS and catch controls agree on both backends. |
| A computed argument crossed an earlier getter, including a callee getter | `take(V,G+1)` summed 73 instead of 93 despite three getter calls | `f631a266` keeps the probe in order, re-lands after an observed getter and treats deferred environment/reference argument reads as barriers. Argument, callee, local-reference and before-argument controls pass. |
| A callback changed an environment binding's declared type without changing its raw kind | After var G became int G, the VM retained the var conversion rule and returned 2.5 instead of 2 | `c0065928` revalidates the declaration stamp. It also fixes a landing miss that resumed at a POP after the statement result was already discarded, which our intermediate build reproduced as a Debug assertion. |
| A nested subscript deferred its outer value past an effect | P[Q[0]] returned 90 instead of 10 when Q's operator or index getter rebound P | `a9d7660a` fixes the original global, captured-local, reference-parameter, private class operator, custom numeric operator and receiver-getter cases. `e5b8c649` additionally fixes a function cached before a host redeclares P as a property reference. That latest repair introduces the two defects below. |

Raw evidence: `compound-recovery.json`, `ordering-extension.json`, `getter-order-extended.out`, `call-stamp-check.out`, `corrected/compound-after-v2-steps1-3.json`, `corrected/partial-assert-note.txt`, `stable-a9d7660a/focused/`, `stable-a9d7660a/focused-release/`, and `stable-a9d7660a/e5-focused/`. Original failures are retained alongside later verification. The directory name stable-a9d7660a is historical: its runtime-revision.json records the e5 overlay used by e5-focused; the earlier Release evidence remains identified as a9d.

## Two remaining defects in e5b8c649

### A saved null is mistaken for an absent snapshot

The native fixture compiles and caches probe while P is a value, then redeclares P as a reference to a host property. The property returns null on its first observed read and an array on its second. Evaluating P[Q[0]] should read the property once and raise. Interpreter and VM proofs-off do that. VM proofs-on reads twice and returns an element. A getter that always returns null raises after two calls instead of one.

The environment guard stores null to mean no saved value, while its readers use is_null() to decide whether to resolve the binding again. Null is also a real language value. Snapshot validity must be represented independently, or the guard must always preserve its value.

Exact host: `null-guard-controls.cpp`. Sixteen outcomes: `stable-a9d7660a/e5-focused/astra_null_guard.out`.

### Cleanup overwrites a later live temporary

The following valid program prints 6 then 5 on the interpreter and with VM proofs off. On e5b8c649 with proofs on it raises `Invalid operands for * operator: Type mismatch`.

```jai
array<int> P = [1, 2, 3, 4];
function probe(int n) {
	array<int> Q = [0, 1, 2, 3];
	int total = 0;
	int value = 0;
	int i = 0;
	while (i < n) {
		total = total + P[Q[i]];
		value = (i + 0.5) * 2.0;
		i = i + 1;
	}
	print(total);
	print(value);
}
probe(3);
```

A guard uses physical temporary slot 7. A later addition reuses slot 7 for the multiply's operand. During fallback, cells_flush materializes live numeric values and then nulls every historical env_guard_temps slot, erasing that operand. The companion checked-overflow case raises the wrong diagnostic for the same reason. Cleanup must follow the active logical definition, not a list of physical slots used earlier.

Exact programs: `probes/env_guard_temp_conversion.jai` and `probes/env_guard_temp_liveness.jai`. Eight outcomes per case and the emitted listing: `stable-a9d7660a/e5-focused/conversion.json`, `liveness.json`, `liveness.listing`.

## Isolated repair proposal

`proposals/always-env-guard.patch` changes two compiler predicates so environment-name containers in these hoisting paths use the existing always-snapshot guard. A real null remains a saved value, and these paths no longer omit a guard and register its temporary for later blanket clearing. The existing snapshot and liveness machinery preserves the operand at its original point. No syntax or language restriction changes.

Base: `e5b8c64976c99f9d1864d480e4a59b7ec2510fef`. Patch SHA-256: `ed2e0b6be78f41388683fa6eb84a49f709014cdcb42892f7d528d976e92e74a2`. The separate test patch updates one instruction-shape expectation from a counted-loop placeholder alone to the placeholder plus an explicit snapshot guard. Its original 73/74 run is retained in `guard-experiment/cell-loops-before-test-adjustment.log`; the behavioral expectations were unchanged.

Verified by running on this proposal:

- Both full Debug suites: 124 suites, 3098 tests per backend, all pass (`guard-experiment/debug-suites.json`).
- The 48-program pass corpus: 384 outcomes, all match expectations and the older reference (`guard-experiment/pass-debug/results.json`).
- 160 focused outcomes in Debug and 160 in Release, including both new failures and earlier ordering, stamp, reload and temporary-reuse controls (`guard-experiment/focused/results.json`, `focused-release/results.json`).
- 18 Release hash gates: GLOOM 300/600, both backends, zero/eight workers, normal/padded binaries; JaiDOOM normal/padded (`guard-experiment/release-checks/hashes.json`). GLOOM 300 is frame 7fecc09815bb64e0/state 3580805725; GLOOM 600 is frame dfd969ed5a336dca/state 1503537018; JaiDOOM state 1746772898.

The proposal remains uncommitted and Fable-owned runtime files remain untouched. A narrower omission scheme would need explicit snapshot validity and definition-aware cleanup, with these same cases as acceptance tests. Any different final implementation needs its own verification; passing this proposal does not approve e5b8c649 as it stands.

## String-cell row 122

Source review covered the original22d488ec and its2400e9ff rebase, including the move of re-borrowing to cells_reland. The string node remains owned by its slot, environment, constant or container. The append path checks the current destination node, uniqueness and the memory cap. String element temporaries are restricted to the adjacent append pair, and fallback must replay the producer because those borrowed temporaries are not flushed. Calls and observed callee getters refresh the borrowed strings before further use. These rules avoid extra interning or ownership traffic.

Independent 2400e9ff evidence: both full Debug suites, 3099 tests per backend; 56 string-program outcomes covering self-append, aliases, UTF-8, empty strings, fallback and environment rebinding; 16 native getter/quota outcomes. All pass. Raw: `string-2400e9ff/debug-suites.json`, `string-checks/script-results.json`, `string-checks/host.out`.

The latest committed rebase e542c0b6 inherits the e5 guard defects. With the same conservative repair, the isolated string-experiment passes both full Debug suites (3100 tests per backend), the 384-outcome differential corpus, 160 focused Debug and 160 focused Release outcomes, 56 string-script and 16 native outcomes in each configuration, and 18 Release game-hash gates. Its experiment.json identifies the exact base and patches. Raw: string-experiment/debug-suites.json, pass-debug/results.json, focused/results.json, focused-release/results.json, strings-debug/, strings-release/ and release-checks/hashes.json. These results certify that identified experiment, not unmodified e542c0b6.

## Existing review evidence and measurement

On frozen 121a, both full Debug suites pass 3092 tests per backend. The 48-program corpus passes 384Debug plus 384Release outcomes. The canonical spelling oracle covers 29 spellings, 116 runtime checks and 11 valid groups. Original regression controls include 80 script runs and 28 hosted ordering outcomes. Extended controls cover 35 programs in eight modes on both configurations. Deep 120-to128-term Debug controls initially exhausted the test host's 1 MB stack; matching Foundry's 128 MB reserve clears 120 focused reruns. The failed host-limit runs remain archived and are not counted as language defects.

The pass-switch listing reaches C_ARITH_P/C_CMP_P/C_JCMP_P with passes on and the checked forms with passes off. The mask controls admit 128 cells and refuse 129. The corpus is bounded evidence, not a proof for every future optimization.

The two historical comparisons and the five-source snapshot are complete in `examples/gloom/ports/RESULTS_BACKLOG_2026-09-11.md`, commit `acf8de244b6d7d5d70b579c9f556c5cd63f08a5f`. Independent audit: 247 processes, 50 gates, 180 accepted timings and 17 retained busy exclusions, every hash and exit correct. Row 112 to 113 improves JaiDOOM 5.40/5.43 percent in normal/padded layouts. Row 114b to 115 improves it 1.24/1.53 percent. Row 112 to 121a improves it 24.4 percent in both layouts. See the report for every number's padding band; the wide worker bands do not support small worker-performance claims.

Squirrel support in the paired runner, build and padding controls is committed as `c7f97a09ff4be9f0dca4c588ce09460183ef1d5f`. Fifteen runner checks and 26 game-hash processes pass. The conformance runs were under load and their timings are not performance evidence. LuaJIT 2.1 is labelled JIT throughout; its number is a technique ceiling, not a portable Bindstone target.

## Generalization feedback for Fable

The reviewed rows use language operations and compiler value definitions rather than game-function names. The compiler has a useful typed execution layer and the beginnings of a middle end. It does not yet have the complete value numbering, copy propagation, rematerialization and alias analysis that make a mature optimizing compiler broadly effective.

1. Give reads, snapshots, calls, conversions and stores explicit ordered effects. A private local's binding can be stable while a later index expression runs user code. Error order and a callee getter are part of the same contract as argument order. Future passes should consume these facts instead of reconstructing quietness from expression shapes.
2. Use the existing per-definition identities in the fallback state map, including guard state. Track saved-value presence separately from payload, and preserve the exact stack, temporary and environment state expected at each continuation. This is the prerequisite for safely deleting copies or repeated computations.
3. Keep binding identity, element contents and borrowed-node lifetime separate. A binding token may justify skipping re-resolution after a call; it cannot justify reusing an element after a store. String borrowing may remove boxing traffic without repeated interning, provided every observing call refreshes or invalidates the borrow.

Next useful pass: dominated scalar common-subexpression reuse on that state map, with unchanged operands and preserved error order. Then copy propagation and constant folding. Element-load reuse waits for content/alias analysis. Retain checked integer overflow, float association and signed-zero behavior, dynamic operator invalidation and the var escape. No extra source-language stiffness is needed to make these optimizations general.

Reading the guard's copy path also confirms that an existing parallel borrow keeps its trivially copyable tagged pointer representation; it does not acquire a parent container owner. This is source reasoning from exec_operand_temp and script_value storage, distinct from the executed parallel hash gates.

## Final paired snapshot and remaining handoff

The three-variant comparison and fresh rivals are complete in [RESULTS_REVIEWED_2026-09-11.md](../../examples/gloom/ports/RESULTS_REVIEWED_2026-09-11.md). Independent audit: 377 processes, 78 gates, 252 accepted timings, 47 retained busy exclusions; all hashes and exits pass. Raw: final-comparison/audit.json and the accompanying process logs, metadata, summary and differences.

The string-cell row improves JaiDOOM beyond the observed layout bands on the repaired base. Small GLOOM changes receive no credit. The wide frozen 121a worker and LuaJIT JIT ranges, including slow accepted observations, are retained and do not support precise small-delta claims. The snapshot gives every median and its band; the complete corrective-stack cost is not attributed to the guard patch alone.

Source-review work and the requested comparisons are complete. The remaining implementation action belongs to Fable: repair the two committed guard defects and incorporate permanent regression tests before landing the dependent stack. The verified conservative patch and native/script controls are available in the raw evidence directory. The unmodified e5b8c649/e542c0b6 tips remain held; a different final repair needs its relevant controls and gates.

One policy decision remains with Dev/Fable: do the pinned opcode variants justify their maintenance cost when the recorded isolated 121a pair is neutral? This review independently verifies the switch propagation, emitted forms and semantic controls; it does not add an isolated 121a speed claim. The verifier and switch support future passes either way. Expanding the opcode family should require a measured benefit.

## Resolution on the final committed tips, 2026-09-12

The original null-snapshot and temporary-reuse findings are fixed at stable 7fc7504443885bccb24b7825d4a08671a038107b and strings ee2af60c6052094fcea6692116e0b0f405ebd340. These are committed runtime sources with no Astra runtime overlays.

Source review: 118d.3 adopts the always-snapshot rule, removes the null-sentinel readers and the environment guard's omission/cleanup paths, and checks the saved array against the validated site's node before resolving the source again. Admission still refuses observing reference bindings; post-call revalidation still refreshes changed nodes. The final string-row runtime diff matches its previously reviewed predecessor ignoring diff offsets.

Verified by Astra: full Debug suites 3099 tests per backend at the stable tip and 3101 at the string tip; 384 semantic-corpus outcomes per tip; 160 original and 32 new guard-path outcomes in each configuration (Debug and Release) per tip; 18 game-hash runs per tip; string-specific 56 script and 16 native outcomes in each configuration. All pass. The new probes cover array replacement, content mutation with stable identity, temporary reuse between sites, and a new invocation after rebinding. Their listings confirm admitted guarded loops and calls where intended. Fable's full Release/XTHREAD/fuzz gates are separately recorded as reported.

The committed-tip pair is complete and independently audited: 145 processes, 30 conformance gates, 108 accepted timings, seven retained busy exclusions; every hash and exit passes. Medians, padding bands and limitations are in [RESULTS_FINAL_TIPS_2026-09-12.md](../../examples/gloom/ports/RESULTS_FINAL_TIPS_2026-09-12.md). The stable worker padding outlier is retained and prevents small worker-performance claims.

Raw evidence: out/astra/final-tips-20260912/. See REVIEW.md, source-inputs.json under each tip, debug-suites.json, pass-debug/, focused-debug/, focused-release/, guard-paths-debug/, guard-paths-release/, release-checks/, comparison/audit.json and noise-review.json. The previous failures and overlay measurements remain separate records. No remaining Astra correctness-review blocker applies to these two tips; Fable's ledger/client-gate/landing sequence remains its own work.
