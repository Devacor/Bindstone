# Approved strict-checker acceptance list

Dev approved rules 1 to 6 on 2026-09-08. This list precedes implementation.
The checker and checker tests are Astra's for this work. VM, interpreter and
compiler ownership remains with Fable. Fable reviews each concrete diff before
its commit. Each implementation increment and the documentation refresh is a
separate commit.

Dev's follow-up permits slightly stricter checking when it improves correctness,
provided the language keeps its scripting flexibility. Reject proven declared-type
mistakes; keep unknown/var values lenient and preserve unit/block warn_mode.
Record intentional checker-stricter cases for Fable before landing them.
Review newly failing tests individually. Deliberately dynamic tests may use var or
warn_mode; more than 10 newly failing tests requires reassessing the checker before
adapting tests. Do not blanket-suppress suites.

## Increment 1: rules 4, 5 and 6

- weak_ptr<T> accepts null and compatible shared/weak handles, including a
  derived pointee into a base pointee. Known non-handles and incompatible
  pointees produce load diagnostics. Unknown var-sourced values remain lenient.
- T& declarations require the source's known static type to be T. Class
  references additionally accept derived-to-base binding. Numeric conversions,
  converting constructors and to_X methods do not make a mismatched reference
  declaration valid. Unknown sources remain lenient. Runtime reference behavior
  and reference-parameter rules are unchanged.
- Function variables and fields accept callable values and null. Namespace
  declarations and static fields preserve the same spelling through saved ASTs.
  Known non-callable
  initializers/stores produce load diagnostics. Calls through callable variables
  stay unchecked; no signature-flow rule is introduced. Function-typed parameters
  keep their existing leniency; they are outside this increment.
- Activate pending_reject_weak_lenient and the reference/function-slot cases
  individually, with rejection expectations. Cover valid handles/references,
  function/lambda/bound-method values, dynamic sources, and warn_mode at both
  unit and block scope.
- Preserve the existing runtime outcomes with checking off. No VM/interpreter
  contract change is part of this increment.
- Known checker-stricter runtime controls: unrelated weak_ptr<A> wa = weakB
  currently stores without a pointee check; function f = 42 currently stores 42.
  Rules 4 and 6 reject these at load time. Runtime corrections belong to Fable.

## Increment 2: rules 1, 2 and 3

- Declared array<T>/map<K,V> subscript reads retain T/V for typed destinations,
  parameters, returns and fields. var and array<var> remain unknown.
- Declared container literals validate statically known elements/keys, recursively
  through nested literals. Typed push arguments and nested subscript stores use
  the same assignment table. Unknown var-sourced elements stay lenient.
- Float literals outside the int64 range produce load diagnostics when stored
  into int slots, including container elements. Finite in-range conversion keeps
  the existing table; computed values retain runtime checking and diagnostics.
- Enable the matching pending_reject element/type-flow cases individually.
  Keep boolean/string/var acceptance controls and var-container demotion policy.
- The auto-null case becomes a checker acceptance control only in this lane.
  Runtime rule 7 is not authorized here. auto_no_initializer and
  function_call_argument remain deferred.

## Gates and review for each implementation increment

- Full Debug suites on interpreter and VM.
- The same 39-program matrix on check/strict/off and both backends (234 processes),
  with exact diagnostic/runtime outputs retained and changed expectations audited.
- Differential fuzz: 3000 seeds and 1500 with --no-proofs.
- GLOOM300 seed 666: frame 7fecc09815bb64e0, state 3580805725; JaiDOOM state
  1746772898 remains the whole-program gate.
- Isolated Release client from the candidate based on landed master:
  -verifyassets must remain 76/76; -loginshot 30 and -workbenchsmoke cover the
  existing client gate set (the brief names two without specifying the second).
  Verify the committed source identity against that reviewed/tested candidate.
- Fable's recorded review of the concrete diff is required before commit.

## Documentation increment

Update the rule-table/precision-map/guide sentences listed in STRICT_DECISIONS.md
to describe the implemented rules, dynamic escape and remaining leniency.
Do not enact rule 7 or change cookie policy. Run verify_site.py --no-samples.

## Independent measurement lane

When Fable supplies a landed borrowed-buffer read or cookie variant, preserve
the existing frozen-input, pinned-P-core, warmed, three-round paired protocol
with normal and 40-line padding layouts, both GLOOM worker settings and JaiDOOM.
Publish the fresh Lua snapshot and per-configuration bands. Never overlap timing
with the other lane's builds, suites or measurements.

## Increment 1 verification

Verified on 6e2bc1f5 plus the reviewed checker/test overlay:
119 suites and 2790 tests passed on each Debug backend. The 39-program
check/strict/off matrix passed its 234-process audit with unchanged off-mode
outcomes. Fuzz ran 3000 seeds and 1500 with proofs disabled at depth 4: zero new
divergences, crashes or hangs. The existing recursion suppression occurred once;
61 and 33 budget-skipped programs are not counted as agreement.

GLOOM300 frame/state hashes held on both backends at zero and eight workers.
JaiDOOM held state 1746772898. The isolated Release client passed 76 asset checks,
30 login frames and 120 Workbench frames with zero exits. Fable approved the
main diff and the small member-assignment-result inference correction.

Raw: out/astra/strict-20260908/step1/final-verification.json,
gates-debug-post-unwind/, matrix/, gates-client-post-unwind/ and client/.
The earlier failing fuzz source and its baseline reproduction remain recorded;
Fable fixed its left-operand unwind propagation in 6e2bc1f5 without a suppression.
