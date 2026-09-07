# Strict-checking gaps: executable evidence, 2026-09-06

The statement that only a `var`-sourced value can fail at runtime is stronger
than the checker currently enforces. Known typed container reads become unknown
to the checker. Literal nested-container violations are also admitted. Neither
case needs `var`. Both backends then reject the invalid value at runtime.

This inventory covers every row of the rule table in `docs/static_checking.md`.
It distinguishes proposed rejection coverage from explicitly documented leniency.
The `bool`, `string` and `var` rows accept anything; there is no honest
type-rejection counterexample for those rows. Their minimal programs are positive
controls. Throwing user conversions and out-of-range indices would not demonstrate
a checker type-rule gap.

## Method and identities

`out/astra/strict-20260906/probe.exe` links the full Debug JaiScript library
built in this checkout. Source reference: `725cba38d18c15cc0fa47004f99165972a1af351`.
For each program, a fresh engine performs one of three operations: explicit
`engine.check()`; execution with strict checking; execution with checking off.
Each operation runs separately on the interpreter and VM. The explicit check
does not execute the program. All 39 programs produced identical diagnostics,
strict outcomes and unchecked outcomes across the two backends.

`results.json` contains all 234 process results with exact stdout/stderr and
exit codes. `cases.json`, the individual `.jai` files, `probe.cpp`,
`build.log` and `identity.json` preserve the inputs and probe identity.
Observed script errors are caught by the probe and printed as `RUNTIME_ERROR`
or `STATIC_ERROR`; a zero probe exit alone is not success of the script.

## Rule-table coverage

| Rule row | Minimal evidence | Verdict |
|---|---|---|
| int | `int_subscript` | Typed string element reaches typed int; 0 check errors, runtime rejects |
| float | `float_subscript` | Same loss of element type, runtime rejects |
| bool | `bool_any` | Accepts an array by truthiness as specified; positive control |
| string | `string_any` | Converts int to string as specified; positive control |
| char | `char_subscript` | Known int element reaches char; 0 check errors, runtime rejects |
| array / map | `array_element`, `map_value` and nested cases | Literal violations pass checking; runtime enforces the element boundary |
| object C | `object_subscript` | Array<D> read reaches unrelated C; 0 check errors, runtime rejects |
| shared_ptr / weak_ptr / references / function slots | `weak_lenient`, `shared_lenient`, `reference_lenient`, `function_call_argument` | Mixed behavior, detailed below; documented leniency requires a policy decision |
| var | `var_any` and demotion cases | Explicit opt-out; controls and runtime demotion evidence |
| auto + initializer | `auto_subscript` and `auto_null` | Inferred int assignment from typed subscript passes checking; null-initialized auto is separately documented leniency |
| auto without initializer | `auto_no_initializer` | Straight-line lock to int is invisible statically; documented leniency |

The direct int/float/char/object/auto mismatch controls are rejected statically.
An array/int kind mismatch is also rejected. The issue is lost information and
deferred coverage, not absence of the basic assignment table.

## Proposed rejection cases

Each program below has zero static errors on both backends. Strict execution and
unchecked execution both produce the displayed runtime diagnostic. These are the
red assertions in the disabled battery. Rows involving documented leniency are
candidate stronger rules, not a decision to change the language.

### int_subscript

```jai
array<string> a = ["bad"]; int x = a[0]; x;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot initialize 'x' (declared int) with string: Type mismatch
```

### float_subscript

```jai
array<string> a = ["bad"]; float x = a[0]; x;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot initialize 'x' (declared float) with string: Type mismatch
```

### char_subscript

```jai
array<int> a = [65]; char c = a[0]; c;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot initialize 'c' (declared char) with int: Type mismatch
```

### array_element

```jai
array<int> a = ["bad"]; a;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot store string in array<int>: Array element type mismatch
```

### map_value

```jai
map<string, int> m = {{"key", "bad"}}; m;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot store string in map<string,int>: Array element type mismatch
```

### object_subscript

```jai
class C {} class D {} array<D> a = [D()]; C c = a[0]; c;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot initialize 'c' (declared C) with D: Type mismatch
```

### weak_lenient

```jai
weak_ptr<int> p = "bad"; p;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot initialize 'p' (declared weak_ptr<int>) with string: Type mismatch
```

### auto_subscript

```jai
auto x = 1; array<string> a = ["bad"]; x = a[0]; x;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot assign string to 'x' (declared int): Type mismatch
```

### auto_null

```jai
auto x = null; x = 1; x = "bad"; x;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot assign string to 'x' (declared int): Type mismatch
```

### auto_no_initializer

```jai
auto x; x = 1; x = "bad"; x;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot assign string to 'x' (declared int): Type mismatch
```

### nested_array

```jai
array<array<int>> a = [[1, "bad"]]; a;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot store string in array<int>: Array element type mismatch
```

### nested_map_value

```jai
map<string, array<int>> m = {{"key", [1, "bad"]}}; m;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot store string in array<int>: Array element type mismatch
```

### nested_map_key

```jai
map<int, array<int>> m = {{"bad", [1]}}; m;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot store string in map<int,array<int>>: Array element type mismatch
```

### nested_push

```jai
array<array<int>> a = [[1]]; a.push(["bad"]); a;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot store string in array<int>: Array element type mismatch
```

### nested_subscript_write

```jai
array<array<int>> a = [[1]]; a[0][0] = "bad"; a;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot assign 'string' to element of type 'int': Array element type mismatch
```

### container_float_overflow

```jai
array<int> a = [1e30]; a;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot convert float to integer: non-finite or out of range: Invalid numeric operand
```

### function_call_argument

```jai
function f = [](int x) -> int { return x; }; f("bad");
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot pass string to parameter 'x' of lambda (declared int): Type mismatch
```

### int_return_subscript

```jai
array<string> a = ["bad"]; int f() { return a[0]; } f();
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot return string from 'f' (declared -> int): Type mismatch
```

### int_parameter_subscript

```jai
array<string> a = ["bad"]; int f(int x) { return x; } f(a[0]);
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot pass string to parameter 'x' of 'f' (declared int): Type mismatch
```

### int_field_subscript

```jai
class C { int x = 0; } array<string> a = ["bad"]; C c = C(); c.x = a[0]; c.x;
```

Interpreter: 0 static errors; runtime rejects. VM: 0 static errors; runtime rejects.

```text
Cannot assign string to field 'x' of C (declared int): Type mismatch
```

## Dynamic hatch and contract controls

`var v = typed_array` does more than make a checker expression unknown: the
flat array copy can accept a different element kind at runtime. The original
typed array stays unchanged. Re-entering an `array<int>` slot checks the
demoted elements again. Demotion is not recursively identical: the copied
outer `var` array in the nested case retains an inner int element boundary.

| Case | Minimal program | Check errors, both | Strict and off outcome, both |
|---|---|---:|---|
| bool_any | <code>bool b = [1]; b;</code> | 0 | <code>VALUE true</code> |
| string_any | <code>string s = 42; s;</code> | 0 | <code>VALUE 42</code> |
| reference_lenient | <code>string s = "bad"; int& r = s; r;</code> | 0 | <code>VALUE bad</code> |
| var_any | <code>var v = 1; v = "bad"; v;</code> | 0 | <code>VALUE bad</code> |
| var_typed_array_read | <code>array&lt;int&gt; a = [1]; var v = a; string x = v[0]; x;</code> | 0 | <code>VALUE 1</code> |
| var_typed_array_write | <code>array&lt;int&gt; a = [1]; var v = a; v[0] = "bad"; v[0];</code> | 0 | <code>VALUE bad</code> |
| container_finite_float | <code>array&lt;int&gt; a = [3.9]; a[0];</code> | 0 | <code>VALUE 3</code> |
| function_slot_any | <code>function f = 42; f;</code> | 0 | <code>VALUE 42</code> |
| reference_write | <code>string s = "bad"; int& r = s; r = 1; s;</code> | 0 | <code>VALUE 1</code> |
| var_copy_original | <code>array&lt;int&gt; a = [1]; var v = a; v[0] = "bad"; a[0];</code> | 0 | <code>VALUE 1</code> |
| var_demoted_retyped | <code>array&lt;int&gt; a = [1]; var v = a; v[0] = "bad"; array&lt;int&gt; b = v; b;</code> | 0 | <code>Cannot store string in array<int>: Array element type mismatch</code> |
| var_nested_demotion | <code>array&lt;array&lt;int&gt;&gt; a = [[1]]; var v = a; v[0][0] = "bad"; v;</code> | 0 | <code>Cannot assign 'string' to element of type 'int': Array element type mismatch</code> |

`shared_lenient` is a documentation mismatch in the other direction:
`shared_ptr<int> p = "bad"; p;` produces one static error, although the
combined rule row describes handles as lenient:

```text
cannot initialize 'p' declared 'int' with 'string'
```

With checking off, it raises `Cannot initialize shared_ptr with this type: Invalid shared_ptr conversion`.
The weak_ptr spelling passes checking and fails at runtime. A mismatched
`int&` alias can read a string and write an int through it; function slots
can hold a non-callable int. These are observed boundaries, not authorization
to redefine reference or function-slot contracts.

## Direct rejection controls

Each check reports one error; strict execution throws a static error before
execution. Unchecked runtime results are preserved in the raw JSON.

| Case | Minimal program | Exact static diagnostic, both backends |
|---|---|---|
| int_direct_control | <code>int x = "bad"; x;</code> | <code>cannot initialize 'x' declared 'int' with 'string'</code> |
| float_direct_control | <code>float x = "bad"; x;</code> | <code>cannot initialize 'x' declared 'float' with 'string'</code> |
| char_direct_control | <code>char c = 65; c;</code> | <code>cannot initialize 'c' declared 'char' with 'int'</code> |
| object_direct_control | <code>class C {} class D {} C c = D(); c;</code> | <code>cannot initialize 'c' declared 'C' with 'D'</code> |
| auto_direct_control | <code>auto x = 1; x = "bad"; x;</code> | <code>cannot assign 'string' to 'x' declared 'int' (declared at 1:6)</code> |
| array_kind_control | <code>array&lt;int&gt; a = 1; a;</code> | <code>cannot initialize 'a' declared 'array<int>' with 'int'</code> |

## Questions for the language owner

1. Should a read from a statically known `array<T>`/`map<K,V>` retain T/V
   for checking? Primitive, object, return, parameter and field cases all fail
   without a dynamic source, while their direct controls are rejected.
2. Should literal container contents and nested shapes be checked at load time?
   The table explicitly defers them, while its introductory guarantee says typed
   mistakes are load errors. Every nested case reaches the runtime boundary.
3. Should a literal float outside the int range be rejected when initializing
   `array<int>`? Finite `3.9` truncates to 3; `1e30` is admitted
   statically and fails at runtime. No constant-range rule is assumed here.
4. Keep or revise the documented leniencies for weak handles, reference aliases,
   callable variables and auto's first non-null store? The straight-line cases
   make the limitation concrete. Stronger rejection tests await this decision.
5. Is flat `var` array demotion, with nested element boundaries retained, the
   intended hatch? This document makes neither static rejection nor recursive
   demotion a requirement.

## Disabled Foundry battery

`source/tests/semantics/strict_gaps_pending_tests.cpp` contains 39 tests:
20 proposed rejection assertions and 19 contract/control checks.
It declares a suite class but creates no suite instance and has no registration.
CMake includes the file in compilation; ordinary Foundry runs do not execute it.
The `pending_reject_*` tests currently fail their `has_errors()` assertion
if enabled. Fable can move or enable each one as its rule lands. Foundry's
`make_engine()` respects the selected interpreter/VM backend.

Verified by running: every check/strict/off outcome on both backends. Inferred
from reading: the rule table and subscript leniencies explain the unknown types;
intended future policy remains a question. No checker or runtime file was
changed. These are correctness results and carry no performance claim.

Landing gates: disabled battery compiled successfully; it did not register a
suite. Full Debug interpreter and VM runs each passed 116 suites / 2740 tests.
The 300-tick GLOOM run on both backends retained frame `7fecc09815bb64e0` and
state `3580805725`. Evidence: `out/astra/debug-build-brief.log`,
`out/astra/debug-brief-interpreter.log`, `out/astra/debug-brief-vm.log`
and `out/astra/gloom-brief-300.log`.
