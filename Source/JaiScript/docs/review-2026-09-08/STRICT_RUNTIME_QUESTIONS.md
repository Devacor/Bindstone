# Runtime questions found by strict-rule acceptance

## Can a callable slot be reset to null after it has held a function?

Approved rule 6 accepts a callable or null. The checker implements that rule,
but this program currently fails at the null store:

```jai
function f = []() { return 1; };
f = null;
f = []() { return 7; };
f();
```

Verified on both backends, on the c85ea0dd baseline and 9d0c3a85 plus the checker
candidate, in check/strict/off modes: checking accepts it; execution reports
`Cannot assign null to 'f' (declared function<void()>): Type mismatch`.

The same failure occurs for an auto-inferred callable, an instance field and a
static field. Initializing with null and then assigning a callable succeeds.
The baseline reproduces the failure, so the checker change did not introduce it.

Read: `convert_for_typed_slot_core` in `core/class_definition.hpp` has no function
target row. It falls through to the same-type-or-reject default; object and
handle targets have explicit null handling.

Resolution: Fable confirmed that callable-or-null includes reset and added the
shared runtime row in d141b9c2. Astra re-ran all six programs on both backends:
local, auto, instance-field and static-field resets now succeed, and calling the
reset slot raises a catchable error. The first increment's positive control passes.
Astra did not change the runtime table.

The null-call message currently reads `Cannot call 'f': it is a function, not a
function` on both backends. This diagnostic wording issue is reported to Fable;
the reset and catch behavior are verified.

Raw: `out/astra/strict-20260908/step1/callable-null/before-runtime-fix.json`
and `results.json` (72 processes each,
six programs, two builds, both backends, three modes), plus
`out/astra/strict-20260908/step1/focused-interpreter.log`
(metadata-v3, `rules_456_valid_runtime_boundaries`).

## Which value should an assignment expression return after conversion?

Both backends currently agree on these outcomes with a float RHS and an int slot:

| Assignment shape | Expression result |
|---|---|
| local = rhs; local += rhs | converted int |
| bare field name inside a method | converted int |
| obj.field = rhs; obj.field += rhs | unconverted float |
| Class::field = rhs | converted int |
| Class::field += rhs | unconverted float |
| array/map subscript store or compound | unconverted operation/RHS value |

The stronger reference check exposed the checker's old assumption that an explicit
field assignment result had the field's type. For
`auto x = (obj.intField = 3.9); float& r = x;`, checking rejected the float alias
although both runtimes bind it to a float. The checker now follows the observed
result types. This changes inference, not runtime assignment semantics.

Question for the runtime/contract lane: should assignment result conversion be
uniform across these shapes? No runtime change is part of Astra's checker work.
Array/map reference declarations from a literal are not lvalue bindings and
already fail at runtime, independent of their element contents.

Raw: `out/astra/strict-20260908/step1/element-boundaries/results.json` and its
individual programs. The local, bare-field, instance/static-field, accessor and
subscript outcomes were run on both backends in off mode; the false-positive
float-reference program was also checked on both.
