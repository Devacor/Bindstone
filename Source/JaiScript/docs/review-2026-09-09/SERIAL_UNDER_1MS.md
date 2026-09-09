# Structural measurements for GLOOM serial below 1.0 ms

Astra and Fable propose an isolated scalar-temp reuse experiment, a native cost
ledger, then a bounded typed-span pilot. The target is to remove at least
401 microseconds per serial tick while preserving the language's contracts.

## Target and measured starting point

The audited d6c9959b snapshot is 1.400805 ms/tick normal and 1.380335 padded;
its padding band is -2.01% to -1.10%. The normal reference therefore needs
400.805 microseconds removed per tick, a 28.61% reduction, to reach 1.0 ms.
These are target calculations, not measured optimization gains.

A fresh existing-VMPROF capture on c487a0e9 has exactly 79,572,958 opcode
executions over 600 measured ticks: 132,621.60 per tick. All 79 opcode counts
are identical in three captures and identical to the corrected c85ea0dd census.
Every capture passes the frame/state hashes and exactly 600 run_tick entries.

| Class | Executions per tick | Share of executions |
|---|---:|---:|
| Typed arithmetic | 27,073.58 | 20.41% |
| Typed compare/jump | 27,729.53 | 20.91% |
| Typed loads | 19,306.12 | 14.56% |
| Typed moves | 2,234.91 | 1.69% |
| Typed pushes | 2,850.18 | 2.15% |
| Call opcodes | 1,032.52 | 0.78% |
| Other store opcodes | 8,791.53 | 6.63% |
| Other opcodes | 43,603.21 | 32.88% |

These are instruction shares, not time shares. T_PUSH includes general/string
values, and T_* membership does not prove that a slot can remain raw throughout
its lifetime.

Using the separate audited wall-time reference, the all-work average is
10.56 ns per dispatch; the target budget is 7.54 ns. Neither is a pure dispatch
cost. The four leading functions in the earlier corrected profile are the row
builder, column renderer, ray step and billboard painter. Their instruction
counts remain useful for coverage; instrumented time shares do not establish
unprofiled savings.

## Corrections agreed during the brainstorm

- The earlier 6.7 million TSC ticks per frame and 25-31 ticks per typed add
  are instrumented charges. They cannot be treated as the current uninstrumented
  frame cost or compared directly with an uninstrumented Lua opcode.
- Payload-only paths already exist in typed_land_int/float and temp_lane_sink.
  The new census exposes a mismatched temp guard: no temp hits, although
  declaration and compound landings frequently update in place. Store landing
  still has its own stamp work. Measure these separately.
- gloom_particle_step takes var& and has a substantial body. Its roughly seven
  opcodes per call are an average dominated by early returns. pad_to uses strings
  and calls; the other proposed small callees need individual eligibility checks.
- In c487a0e9, auxiliary ENV, DECL_VAR, STORE and call-floor counters do not reset
  with the opcode table. Their warm-up/bootstrap totals cannot be divided by 600.
  Reset or take exact before/after differences first; dividing by 1200 would
  still mix unlike bootstrap and measured work.
- Raw cells, inlining and field-load improvements can remove the same work.
  Savings must be recorded in absolute microseconds with overlap accounted for.
  Adding speculative percentage ceilings is not a budget.

## First concrete lead: scalar-temp reuse and container lookup coverage

The table below is the pre-fix census. Fable supplied a c487a0e9 capture with
four new counter groups reset at the
600-tick measured marker. Astra checked two independent sums against its own
opcode census: typed-load sources total 11,583,674; typed landings plus fallback
entries total 16,244,149. These counts agree exactly.

| Measured event | Count in 600 ticks | Per tick |
|---|---:|---:|
| Typed temporary constructed | 4,553,221 | 7,588.70 |
| Typed temporary reused in place | 0 | 0 |
| Typed declaration stamp matched | 4,241,144 | 7,068.57 |
| Typed declaration constructed | 227,888 | 379.81 |
| Typed store payload written and type re-stamped | 2,503,181 | 4,171.97 |
| Typed compound updated in place | 4,481,391 | 7,468.99 |
| Typed arithmetic fallback entered | 237,324 | 395.54 |
| T_LOAD container resolved through environment | 8,138,198 | 13,563.66 |
| T_LOAD container read from frame | 3,445,476 | 5,742.46 |

After the first guard fix, Fable reports 4,222,280 temp reuse hits and 330,941
constructions over the same 600 ticks, with every other landing row unchanged
and the smoke hash intact. That converts 92.73% of the former constructions
to reuse hits. Astra has checked this arithmetic, but has not independently
run the candidate or measured its wall-time gain. The final accessor/guard
variant still needs a frozen-binary comparison.

The environment path handles 70.26% of typed loads. This sizes possible cache
coverage, not its time cost. The general INDEX result-kind counter finds 556.26
integer results per tick; assigning all of those to particular script containers
would require per-site counters.

Both agents independently found the temp-guard contradiction in the source.
`script_value(script_int, engine*)` and its float counterpart install the engine's
canonical type. The temp guards in `typed_land_int/float` and
`temp_lane_sink::raw_int/raw_float` require an absent type. A normal engine-built
scalar therefore misses that reuse test even when its slot is live and its
payload kind matches. The constructor also explains why the sink's comment
about untyped kernel results is stale.

First experiment: allow reuse of the canonical same-engine scalar while retaining
all other destination semantics, then compare uninstrumented binaries. Before
sizing the gain, count miss reasons: no live slot, wrong payload/reference,
canonical engine stamp, and other stamp/engine. Construction of a scalar value
is not itself a heap allocation. Record type-handle traffic and payload writes
separately from allocator events.

Keep this experiment separate from store re-stamping and global-container cell
caching. Use a borrowed canonical-type view for a hot guard where available;
`get_type_info()` currently returns an owning handle by value. Cache lifetimes
must remain bounded by the engine, and no cached name should be re-interned per
operation. Pointer/tag and engine identity must remain correct on every hit.

Fable reports the extended reset patch is in. A 100-versus-600 warm-up control
changes the workload: the lazy STRIPS/STRIP_SET cache survives the host reboot,
and the shorter warm-up leaves 59 extra strips to build in the measured span.
That pair does not establish identical measured work. The next exclusion control
is 600 versus 1200 after verifying equivalent cache readiness, or a fixed-work
synthetic harness. Old cumulative auxiliary numbers remain excluded.
No whole-program saving is claimed for the new counts or proposed fix.

## Measurement 1: a correctly scoped traffic census

Fable owns the profile-only counter patch. Counters live in each VM instance;
no mutable static or thread-local state. Every counter used in the ledger resets
at the measured-loop marker. A control with different warm-up lengths and the
same measured work must produce identical measured event counts. First verify
equivalent runtime and script cache state; use the 600-versus-1200 control above
only once the relevant cache is saturated. Otherwise use a synthetic fixed-work
control. Attribute any cache-related residue explicitly; matching unrelated rows
alone does not prove that every counter reset.

Record per function, weighted by dynamic execution:

| Counter | Decision it informs |
|---|---|
| Typed landing kind and outcome: payload write, construct, stamp change, refused/fallback | Remaining value-lifetime work after the existing fast paths |
| T_LOAD and indexed-store container source: frame slot or environment cell | Coverage of a pinned global-cell cache |
| LOAD/INDEX result kind and DECL_VAR source/destination kind | Scalar traffic still passing through general operations |
| Actual frame pushes/pops, argument counts, window initialization/clear bytes, environment births | Call/frame cost beyond the opcode name |
| Whole-function raw-cell eligibility and rejection reason | How much work an 8-byte scalar area can actually cover |
| Predicted live scalar materializations at general calls, references, yields and debugger boundaries | Whether boxing at boundaries consumes the proposed saving |
| Actual box/unbox events once a pilot exists | Validate the prediction rather than assuming it |

Predicted materializations are a compiler/dataflow estimate until raw cells exist.
They must not be reported as current runtime events. Count full 32-byte value
replacement separately from the 8-byte payload writes already in use.

An environment-cell cache is a candidate only if the corrected count and native
cost justify it. It must preserve binding identity, global-environment generation,
reload and getter behavior. The recent caller-local/global palette collision is
a mandatory control, not a reason to weaken lexical scope.

## Measurement 2: the native time ledger

Collect serial CPU samples from a plain Release binary with matching PDBs and
measured-loop markers. Use enough repeated frames to resolve useful categories.
Use the existing measured-loop ETW markers and fixed 600-tick workload. The
current capture period is 1000 microseconds; a single short serial run has too
few samples to resolve small subcategories. Aggregate repeated identical
measured spans across at least three captures and both layouts, report sample
counts and per-capture variation, and leave sparse or unresolved rows grouped.
Do not lengthen the workload into a different game state to inflate coverage.

Report disjoint leaf/address-based buckets:

- dispatch, decode and loop bookkeeping;
- operand addressing and guards;
- value landing/lifetime work;
- environment resolution;
- call preparation, binding, frame entry/exit and return handling;
- remaining indexed reads/stores;
- remaining string/push/join work;
- allocator/free work;
- other native work and unresolved samples.

Allocator and value-lifetime samples are assigned once, not also charged to a
container or call bucket. Native callee bodies are not call-frame overhead.
Sampling locations are attribution evidence, not exact causal instruction costs.

Turn shares into CPU microseconds per tick using the measured CPU-time basis.
Keep off-CPU/scheduling time and the difference from wall time explicit. Do not
silently multiply a CPU-sample share by the full wall clock. Validate lost events,
stack coverage and inline-address resolution as in the existing profile workflow.

If hardware counters are available, add retired instructions, branch misses and
cache-miss/stall evidence. Otherwise report logical bytes and working-set sizes
as estimates, not measured cache traffic. This determines whether density, guards,
dispatch or frame management deserves the first implementation.

## Measurement 3: interpreter-technique controls

Use separate bounded micro gauges for arithmetic/compare, load/store and calls.
Run the current VM, Lua 5.4.8, LuaJIT 2.1 JIT with JIT disabled, and a small C++
switch interpreter over the same abstract operations.

For shared arithmetic calibration, stay in an exact integer range below 2^53.
Keep bitwise work separate: LuaJIT's bit.band call and Lua 5.4's native bitwise
opcode do not measure the same interpreter machinery. Check results, inspect
bytecode and normalize by actual executed instructions as well as iterations.

The C++ control must read a runtime instruction stream and consume its result.
Inspect generated machine code to ensure the switch/loop remains. Label it a
minimal technique control; it does not implement all JaiScript contracts.
An empty-handler or replay-only loop is not a GLOOM speed data point.

Ratios on these gauges diagnose mechanisms. They are not whole-program ceilings,
because the real opcode mix, value traffic and calls differ.

## Measurement 4: the bounded structural pilot

Fable nominates render_view_impl's column inner loop: strip read, two pixel stores
and integer updates. The ray-step DDA loop is the alternative. Use the census
to choose a span with enough coverage and a manageable number of boundaries.
The current design ends a region at an unproven var read; strip is exactly that
boundary case. Count exits and required live-value materializations per iteration
before choosing this loop. Use the DDA alternative if that boundary dominates;
do not silently widen the design's eligibility rule to make the pilot work.

Start by comparing a proven span using the existing 32-byte slots with the same
span using an 8-byte scalar area. Apply equivalent entry proofs and semantic
kernels so this comparison isolates remaining storage/lifetime effects as far
as possible. Separately compare ordinary dispatch with a clustered or fused
execution of that same span. A combined variant measures interactions.

| Pilot | Scalar storage | Dispatch |
|---|---|---|
| A | Existing 32-byte values | Ordinary |
| B | Raw 8-byte scalar area | Ordinary |
| C | Existing 32-byte values | Clustered or fused |
| D | Raw 8-byte scalar area | Clustered or fused |

B versus A tests storage/lifetime cost; C versus A tests dispatch and repeated
bookkeeping; D measures the combined result. Measure all boundary work as part
of each variant and compare proof/guard placement explicitly.

Keep the required overflow, bounds, budget/backedge, debugger, reload, reference
and evaluation-order behavior. An engine poll can be hoisted or batched only
when its contract is preserved. Guard failure must not evaluate a getter and
then replay it. Every materialized script_value still carries its engine reference.

Each viable variant runs both whole-program hash gates and the existing warmed,
pinned ABAB protocol, normal and 40-line padding layouts. Micro gains are a gauge.
GLOOM serial and JaiDOOM provide the performance credit; the interpreter remains
the oracle. Both full Debug suites, fuzz 3000 plus 1500 with proofs disabled, XTHREAD for
frame/worker changes, and the client gates apply before landing. Fable reviews
each runtime increment; the 300-tick GLOOM gate remains frame 7fecc09815bb64e0
and state 3580805725, and JaiDOOM remains state 1746772898.

## Decision rules

For a slice occupying fraction f of current time and sped up k times,
the remaining time is T * ((1 - f) + f / k). Reaching 1.0 from 1.400805 requires:

| Speedup of the affected slice | Minimum affected time share |
|---|---:|
| Perfect elimination | 28.61% |
| 2x | 57.22% |
| 4x | 38.15% |

These are planning thresholds under the stated model. Opcode shares cannot be
substituted for time shares. If a measured slice cannot pay enough of the
401-microsecond budget, combine independently measured mechanisms or choose
another target. Do not keep polishing a small row to make the projection add up.

The first isolated candidate is the temp reuse guard above. The next may be
global-cell pinning or a guarded typed span, depending on the native ledger.
Raw cells follow when measured eligibility and
boundary traffic support them. General call-frame work follows a real call census;
inlining additionally preserves argument evaluation, observable copies, default
arguments, references, lexical binding, failure locations and invalidation.

PGO and more explicit GLOOM table types remain separately labeled experiments.
Use identical scripts on both sides of an engine comparison. PGO needs an
independent training/evaluation split and validation on JaiDOOM and other states;
its gain is a build-configuration result. Neither axis is added speculatively to
the structural budget.

## Evidence and current status

Completed by running: three c487a0e9 VMPROF captures, matching hashes and
600-entry scopes, repeated-count audit and comparison with c85ea0dd.
No structural wall-time gain has been measured or claimed by Astra. Fable
reports a counter improvement from the first temp-guard candidate, as recorded
above; its final binary and paired timings remain pending.

Read/source-audited: existing typed landing paths, callee signatures/bodies,
the profile-reset limitation, Fable's new counter capture, and the typed-regions
design. Fable accepted the
corrections and owns the counter patch/pilot implementation; Astra owns capture,
analysis and whole-program comparison.

Raw: out/astra/serial-structure-20260909/{source-inputs,captures,census,opcode-captures}.json,
gloom-1..3.log/.err, profile/compile_commands.json, capture.ps1 and analyze.py.
Copied Fable capture and cross-checks:
out/astra/serial-structure-20260909/fable-census-initial/{gloom.err,gloom.out,audit.json}.
Audited wall-time baseline: examples/gloom/ports/RESULTS_2026-09-09.md.
The profiling times remain diagnostic and are not used as shipping speed numbers.
Fable reviewed the plan and approved publication with the warm-up-cache qualifier
and pre-fix/after-counter distinction above. Site verification passed with
verify_site.py --no-samples. This increment changes documentation only.
