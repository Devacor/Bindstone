# Call-boundary corrections, 2026-09-29

Runtime increment: 4343df54eaf965f70d10c5748d3f89ca1b286290, following 51e554e0. Reviewed baseline: 0865b71e.

## Corrected behavior

- Compound assignments read their RHS before a target that may change it. All five compound operators are pinned for array and member targets, with proof and IR modes varied.
- Earlier arguments are captured at their original evaluation points and held in owned compiler slots. TAKE_TEMP moves them into the call at the consuming expression, preserving operand-stack order without another deep copy. Reference binding retains the original argument-source metadata. A Foundry disassembly assertion requires the writing helper to remain inlined.
- Mixed overloads retain the possible value snapshot, evaluated shared-handle identity and original lvalue until selection. Arity filtering avoids impossible candidates; reference/sharing-only sets make no deep snapshot.
- Script static dispatch retains its parameter metadata through aliases. Opaque host-call behavior stays on the host binding path.
- Ordinary constructors and new expressions retain reference argument metadata on both backends. Constructor selection examines the captured value's type.
- An abandoned call releases its staged arguments before catch. Handler metadata is owned across destructor callbacks that can reenter and grow the handler stack.

The public script_value remains 32 bytes. All added runtime state belongs to an engine, frame, compiled call site or temporary capture. No mutable static/thread-local state was introduced. Existing checker proofs, scalar capture elision and read-only parameter aliasing remain active.

## Executed gates

| Gate | Result |
|---|---|
| Full Debug interpreter / VM | 3546 tests each, green |
| Full Release interpreter / VM | 3546 tests each, green |
| Call Argument Capture | 19 tests, including snapshot, alias, nesting, fallback, destructor/reentry and code-generation checks |
| GLOOM 300, seed 666 | Frame 7fecc09815bb64e0, state 3580805725 on both backends, 0/8 workers, Debug and Release |
| Differential fuzz, 3000 seeds | 0 new divergences, 2 known suppressions, 62 budget skips, 0 crashes/hangs |
| Proofs-off fuzz, 1500 seeds | 0 new divergences, 1 known suppression, 34 budget skips, 0 crashes/hangs |
| Ordering/arguments fuzz, 1000 seeds | 1000 backend and metamorphic agreements; 995 model checks passed, 5 abstained |
| Comparison tools | 22 existing runner tests and 2 OSSUARY runner tests passed |
| Site verification | verify_site.py --no-samples passed |

## GLOOM and JaiDOOM pair

One pinned P core for serial/timedemo; eight P cores for the worker configuration. Three interleaved rounds, full in-process warm-up and normal/padded executables. GLOOM measures 600 ticks; JaiDOOM measures 3984 tics. Units are ms/tick for GLOOM and ms/tic for JaiDOOM.

| Configuration | P cores | Normal median | Padded median | Paired padding band |
|---|---:|---:|---:|---:|
| JaiScript VM serial baseline | 1 | 0.879085 | 0.879299 | -0.57% to +1.46% |
| JaiScript VM serial candidate | 1 | 0.878734 | 0.875641 | -1.07% to +0.90% |
| JaiScript VM 8 workers baseline | 8 | 0.675703 | 0.694301 | -2.45% to +5.56% |
| JaiScript VM 8 workers candidate | 8 | 0.685854 | 0.678292 | -4.95% to +3.52% |
| JaiDOOM VM timedemo baseline | 1 | 1.725025 | 1.734061 | +0.04% to +0.66% |
| JaiDOOM VM timedemo candidate | 1 | 1.726531 | 1.729543 | -0.17% to +0.70% |

All 72 required timings were accepted. Every GLOOM hash and JaiDOOM state 1746772898 held. Background activity was 0.198 to 1.077 core equivalents, median 0.549, under the user-authorized 1.50-core ceiling.

Normal/padded candidate-to-baseline median deltas are -0.04/-0.42 percent for GLOOM serial, +1.50/-2.31 percent for GLOOM eight workers, and +0.09/-0.26 percent for JaiDOOM. Those differences are inside the corresponding bands. The worker readings do not establish a directional change.

The complete OSSUARY before/after and Lua/Squirrel comparison is in [the OSSUARY snapshot](../../examples/demoreel/ports/RESULTS_2026-09-29.md). All 120 accepted scene timings matched the interpreter reference. Natural code is within about one to two percent of shaped code in the two layouts, subject to the recorded bands.

## Evidence and scope

Raw evidence is under Source/JaiScript/out/astra/fixes-20260929/: boundary-gates-debug/, boundary-gates-release/, performance-whole/, performance-ossuary/, audit.json, frozen build manifests and all focused red/green logs. out/astra/review-20260929/ retains the original review and minimal reproductions.

The original three defects and adjacent static/new/reference/lifetime cases are corrected. This increment preserves helper inlining through aggregate argument boundaries; broader by-value object-callee admission still has the inliner's existing body/return/type requirements.

An additional older edge remains separate: constructors combining var parameters with differently typed tag parameters can choose the first arity-compatible declaration. extra-probes/var-ctor-order-control.jai reproduces it on the pre-fix baseline too. It was not changed as part of argument capture.

No new Bindstone client, Linux/Clang, or injected-miss build was run in this increment. The performance statements apply to the frozen, audited snapshots and workloads above.
