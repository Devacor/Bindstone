# Strict-checking gaps: decision memo

Companion to `STRICT_GAPS.md` (Astra, `e0ad455d`), which proves with 39 programs on both
backends that several typed mistakes the documentation calls load errors reach the runtime.
This memo turns each proposed stronger rule into a decision: current behavior, the proposed
rule, the C++ analogue (C++ semantics are the contract), the blast radius in the scripts the
repository ships, and a recommendation. Counts come from grepping every `*.script` and `*.jai`
under `Assets` (48 files: creatures, interface, workbench) and `Source/JaiScript/examples`
(88 files: GLOOM, JaiDOOM, crawler, demoreel); the patterns are named per row.

## Rules

| # | Rule | Today (minimal program, 0 check errors, runtime rejects) | Proposed | C++ analogue | Blast radius | Recommendation |
|---|---|---|---|---|---|---|
| 1 | Typed element reads keep the element type | `array<string> a = ["bad"]; int x = a[0];` and the object, return, parameter and field twins (`int_subscript`, `object_subscript`, `int_return_subscript`, `int_parameter_subscript`, `int_field_subscript`) | `a[i]` on a statically typed `array<T>` / `map<K,V>` receiver has static type `T` / `V` and runs the assignment table; `var` and `array<var>` receivers stay unknown | `std::vector<std::string> a; int x = a[0];` is ill-formed | Assets: 0 typed reads into a mismatched slot. Examples: 747 typed element reads into typed slots, every one type-correct (they run every frame); a read from a `var&` parameter (GLOOM's row builders) stays unknown | **Adopt.** Only surfaces mistakes the runtime already rejects; the false-positive contract holds because the rule uses only the receiver's declared type and the existing table |
| 2 | Container literals check their elements at load | `array<int> a = ["bad"];`, `map<string, int> m = {{"key", "bad"}};`, nested shapes (`nested_array`, `nested_map_value`, `nested_map_key`, `nested_push`, `nested_subscript_write`) | A literal element (or a nested literal) that is itself a literal, a typed local or a typed expression is checked against the declared element / key type; `push` and `a[i][j] = v` on a statically typed receiver check the value the same way; a `var`-sourced element stays unknown | `std::vector<int> v = {"bad"};` is ill-formed; `v.push_back("bad")` too | Assets: 0 nested container declarations, 0 mismatched literals. Examples: 0 nested container declarations | **Adopt** together with 1 (same walk: the element type flows down the literal). Note the precision map and the guide already promise the opposite ("the checker deliberately leaves element and key types to the runtime"); both sentences change if this lands |
| 3 | Out-of-range float literal into an int element | `array<int> a = [1e30];` passes checking, `array<int> a = [3.9]` truncates to 3 | A float literal outside the int64 range is a load error into any int slot (element or scalar: `int x = 1e30;` is the same case) | `int x = 1e30;` is a narrowing conversion, an error in braces, a warning otherwise | Assets: 0. Examples: 0 | **Adopt as a literal-only rule.** The value is known at parse time; the runtime error text stays for computed values |
| 4 | `weak_ptr<T>` initializer | `weak_ptr<int> p = "bad";` passes checking; `shared_ptr<int> p = "bad";` already fails checking | `weak_ptr<T>` takes null, a `shared_ptr<T or derived>`, a `weak_ptr<T or derived>`; anything else with a known static type is a load error | `std::weak_ptr<T> w = "bad";` is ill-formed | Assets: 0 `weak_ptr` declarations with a non-null initializer. Examples: 0 | **Adopt.** The rule table already lists the shared handle row as strict in practice; the weak row is the inconsistency |
| 5 | Reference declaration type | `string s = "bad"; int& r = s;` passes; `r = 1` then writes an int into `s` | `T& r = e` requires `e`'s static type to be `T` (or a class derived from `T` for class references); unknown stays lenient | `std::string s; int& r = s;` is ill-formed | Assets: 0 reference declarations with a typed initializer. Examples: 0 | **Adopt.** A wrong-typed alias is the one row where the runtime does not even reject (it writes through), so the checker is the only line |
| 6 | Callable variables | `function f = 42;` passes and stores 42 | A `function` slot takes a callable (lambda, function value, bound method) or null; a literal or typed non-callable is a load error | `std::function<int()> f = 42;` is ill-formed | Assets: 0 `function`-typed variables. Examples: 0 | **Adopt for literals and typed non-callables.** Calls through such a slot stay unchecked (documented) |
| 7 | `auto` first store after a null initializer | `auto x = null; x = 1; x = "bad";` passes, runtime rejects the second store | Flow-sensitive locking | C++ has no `auto` without an initializer and no null-then-lock | Assets: 0. Examples: 0 | **Keep as documented leniency.** The checker does not see branch order; the guide and the rule table both say so already |
| 8 | `var v = typed_array` demotes | `array<int> a = [1]; var v = a; v[0] = "bad";` stores the string in the copy; `array<int> b = v` rejects at the boundary | None | `auto v = a;` in C++ keeps the type; JaiScript's `var` is the dynamic opt-out by ruling | Assets: 3 `var x = name;` declarations, all `= null` placeholders. Examples: 134 `var x = name;` copies, most of them arrays handed to `var&` helpers | **Keep as policy** (Dev's var hatch ruling). Document the demotion precisely, since today only the precision map implies it |

Rules 1, 2 and 3 are one checker change: carry the receiver's declared element type through
subscript reads, literal elements, `push` arguments and nested stores, and run the existing
table. The runtime is untouched by rules 1 to 3. Enable the matching element/type-flow
cases in `source/tests/semantics/strict_gaps_pending_tests.cpp`; the battery also
contains separate proposals about handles, callable variables and auto inference.
Those require their own decisions. Existing runtime rejections for the element
cases remain the `--check=off` behavior.

## Sentences that promise more than the checker enforces

| Where | Sentence | Fix if rules 1 to 3 land | Fix if they do not |
|---|---|---|---|
| `docs/static_checking.md:19-22` | "A strongly-typed → strongly-typed mismatch (...) is therefore a load error; only a `var`-sourced value can still fail at runtime" | Keep, and add "or a literal the runtime alone can evaluate" | Replace "only a `var`-sourced value" with "a value from a position the precision map lists (a `var`, a container element, a handle or reference store, a host value)" |
| `docs/static_checking.md:50` (rule table, array / map row) | "element/key types NOT checked statically (lenient v1)" | "element and key types checked when the receiver's declared type is known; `var` receivers stay lenient" | Keep |
| `docs/static_checking.md:347-348` (precision map) | "Container elements: `array<T>` / `map<K,V>` element and key types (the runtime element boundary is the check), subscripts, builtin container/string methods" | Narrow to "builtin container/string method results and elements of `var` receivers" | Keep |
| `docs/site/guide/02-type-ladder.html:37` | "A mismatch it cannot see (a value that came through a `var`) is a catchable run-time error" | Keep | Add "or a container element" inside the parenthesis; the list at line 62-63 of the same page already says it |
| `docs/site/guide/09-containers-strings.html:113` | "The checker deliberately leaves element and key types to the runtime (it cannot see what a loop will push)" | "The checker checks literal elements and typed stores; what a loop pushes from a `var` is the runtime's" | Keep |
| `docs/site/guide/13-strict-checking.html:124` | "Container elements and keys: `array<T>` / `map<K,V>` enforce at run time" | Same narrowing as the precision map | Keep |
| `docs/static_checking.md:52` (rule table, handle row) | "`shared_ptr` / `weak_ptr` / references / function slots: anything, lenient v1" | Split the row: `shared_ptr` (checked today), `weak_ptr`, references and function slots (checked under rules 4 to 6) | Split the row anyway: `shared_ptr<int> p = "bad"` is a load error today, the row says it is not |

The last row is a documentation error today regardless of the decision.

## Order of work if adopted

1. Rules 4, 5 and 6 (one table row each in `type_checker.cpp`, the disabled tests
   `pending_reject_weak_lenient`, the reference and function rows). Smallest change, no
   flow analysis.
2. Rules 1 to 3 (element type flow). Enable the remaining `pending_reject_*` tests,
   re-run the 39-program matrix (`out/astra/strict-20260906/probe.exe`) on both backends,
   then the two Bindstone gates (`-verifyassets` runs the interface scripts through the
   strict checker).
3. The sentence list above, in one docs commit, plus `verify_site.py --no-samples`.

## Rulings (Dev, 2026-09-06)

| Rule | Ruling | Consequence |
|---|---|---|
| 1 to 3 | Adopt. "That's a failure of the checker." A read that needs a conversion is spelled `cast<int>(a[0])` (or a `to_int()` on a value that has one), never an implicit typed-slot conversion the checker cannot see | `type_checker.cpp` carries the receiver's declared element type through subscript reads, literal elements, `push` arguments and nested stores; out-of-range float literals into int slots are load errors; enable the matching element/type-flow rejection tests; the six sentences above take their "rules land" column |
| 4 to 6 | Adopt | `weak_ptr<T>`, `T&` and `function` slots run the table on a known static type; the handle row of the rule table splits |
| 7 | `auto x = null;` IS `var x = null;` | A null initializer makes the declaration dynamic for the variable's whole life: no lock at the first non-null store, no inference downstream. Both backends drop the runtime lock for that shape, the checker treats the name as `var`, and the rule table's `auto` row and guide 02-type-ladder.html "auto locks on first use" change wording. `auto x;` without an initializer is untouched by this ruling |
| 8 | Keep as policy | Document the flat demotion in the `var` row |

Checker and runtime changes are the perf lane's files; this memo records the ruling.


### Battery activation after the rulings

Do not register the entire pending battery unchanged. Its expectations describe
the earlier proposal, including stronger checks that these rulings do not adopt.

- `pending_reject_auto_null` becomes an acceptance/runtime control: the declaration
  is now permanently dynamic and the later string assignment must succeed.
- `pending_reject_auto_no_initializer` remains deferred; the no-initializer form
  retains its documented checker leniency.
- `pending_reject_function_call_argument` remains deferred: rule 6 restricts what a
  function slot stores, while calls through callable variables remain unchecked.
- The reference-declaration controls and `control_function_slot_any` need rejection
  expectations under rules 5 and 6.
- Enable the matching element/type-flow and weak-handle rejection cases individually,
  and rerun the complete check/strict/off matrix after updating these expectations.
