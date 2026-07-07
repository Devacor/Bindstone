# Static type checking (opt-in, 2026-07)

Backend-neutral AST checker that runs at parse-cache-entry creation — one hook in
`engine.cpp` covers `execute(string)`, jaibite creation, hot-reload re-parses (new source
= new cache entry) and registration-epoch changes uniformly. Amortized once per unique
source, like parsing. Off by default: the off path costs one branch. Implementation:
`detail/type_checker.hpp` + `source/implementation/type_checker.cpp`.

## Mode API

```cpp
engine->static_checking(check_mode::off | warn | strict);   // default: off
check_report r = engine->check(source);        // parse (cached) + check, ANY mode; tooling
const check_report& d = engine->last_check_diagnostics();   // last checked execute/bite
```

- **off** — zero behavior change. No diagnostics collected anywhere.
- **warn** — diagnostics collected and retrievable; execution proceeds.
- **strict** — any error-severity diagnostic makes `execute()` / `engine->jaibite()` /
  `jaibite_load*()` throw `jai::static_check_error` BEFORE anything runs. `what()` is the
  full accumulated, location-sorted, deduplicated listing (cap 100), compiler style:

```
script:12:5: error: cannot assign 'string' to 'x' declared 'int' (declared at 8:3)
    x = "hello";
    ^
```

Diagnostics are mode-independent (`check_diagnostic {severity, line, column, message,
related_line/column}`); mode only decides whether errors gate execution. Warning
severity never gates.

## Rule table (mirrors `interpreter::enforce_type_compatibility`)

| target | accepts | notes |
|---|---|---|
| `int` | int, float (truncates), bool | string/char/null/containers = error |
| `float` | float, int, bool | |
| `bool` | anything | truthy conversion |
| `string` | anything | to_string conversion |
| `char` | char only | `char c = 65` is a runtime error too |
| `array` / `map` | array / map | element/key types NOT checked (lenient v1) |
| object `C` | null; same class; derived→base; ctor conversion `C(src)` | unrelated classes with no viable 1-arg ctor = error; array/map/function into object = error |
| `shared_ptr` / `weak_ptr` / references / function slots | anything | lenient v1 |
| `var` | anything, forever | THE opt-out: a var-typed expression makes downstream types unknown (like TS `any`); unknown never errors |
| `auto` + initializer | inferred, then enforced as that type | |
| `auto` without initializer | dynamic (unchecked) | runtime locks at first assignment; the checker can't see branch order, so it opts out — documented leniency |

**Compound assign** (Dev ruling 2026-07, settled): typed targets follow C++ rules —
`x op= rhs` is `x = T(x op rhs)`, so `int x; x += 2.5` is fine (truncates) and
`x += "s"` is an error (string result into int). `var` targets keep dynamic behavior,
unchecked.

**Typed fields** (Dev ruling 2026-07, settled): declared field types are real and enforce
like locals — field initializers, member stores (`obj.field = v`, bare `field = v` inside
methods, inherited fields) all check against the declared type.

**Calls** — checked only when the callee resolves statically:
- same-script functions/ctors/methods: arity (with defaults) + per-arg assignability
  (same conversion table — note a `string` parameter accepts anything). Overloads: error
  only if NO overload matches. Reference params are lenient.
- host (C++) functions via `engine::host_function_signatures`: arity across overloads
  (variadic = anything), and per-arg only certainties (a known primitive with no builtin
  conversion to a primitive param — e.g. `square("s")`; numeric↔numeric is fine; objects
  and `script_value` wildcard params accept anything). Note the C++ boundary has NO
  numeric→string conversion, unlike script `string` slots.
- callables in variables (lambdas, cross-execute function values): unchecked.

**Returns** — must be assignable to the declared return type. A value from a `void` fn is
an error. Bare `return;` / falling off the end of a typed fn = warning severity in v1
(runtime returns null there per HEAD). Coroutines (functions or methods — the method form
is landing concurrently) are exempt: calling one yields a handle, and yield/return types
aren't checked in v1.

**Identifiers** — undeclared = error, checked against: scopes, the current class's members
(incl. inherited), top-level declarations (two-pass, so forward references work), every
name declared ANYWHERE in the script (flow-insensitive safety net — execution order can
make later declarations visible), the host's per-call instance variables, and the
engine's full registration surface (globals, functions, classes, template types,
namespaces — visible because checking happens at execute/jaibite time). If the script
contains `include`/`import`, undeclared-identifier drops to warning severity (included
files can define anything).

**Members** — on a receiver whose class is fully known (in-script hierarchy resolvable, or
engine-registered `class_definition` incl. `_get_`/`_set_` properties and statics),
an unknown field/method is an error, reads and writes both (declared intent — the runtime
tolerates dynamic field creation on writes). Any unresolvable ancestor opens the class:
silence. Builtin container/string/map methods, subscripts, enum values (existence IS
checked: `Direction.zzz` errors), namespaces: lenient.

**Recovery** — an erroring expression assumes its declared/expected type, so one bad line
produces one diagnostic, not a cascade; (line, col, message) dedupe on top. Cap 100
(`detail::k_check_diagnostic_cap`), `report.capped` set.

## The false-positive contract

A program that runs clean and does not genuinely violate declared types must produce ZERO
error diagnostics. Everywhere the static story is uncertain the checker goes silent
(`var`, unknown/open classes, element types, includes, callable variables, host object
conversions). Enforced by `static_check_tests.cpp::fuzz_corpus_false_positive_gate`: 200
differential-fuzzer generator seeds; any program that executes cleanly must check clean.
Grow that seed count before loosening any lenient rule.

## jaibite

A bite created under warn/strict is checked at creation (strict throws there);
`bite.checked_clean()` / `bite.check_diagnostics()` expose the result. `save()` stamps a
checked-clean bit in the header's existing flags u32 (no format version bump; old loaders
ignore it). On load the stamp is trusted ONLY when the registration fingerprint matches
the saving engine's; otherwise the bite loads unchecked and, under warn/strict, is
re-checked at load against THIS engine's surface (strict throws on errors). A bite is
checked once — registrations after that don't re-trigger; re-create the bite to re-check.

## Cost

Zero when off (one branch at cache-entry creation). When on, one AST walk per unique
source, cached beside the parse; re-run only when the registration surface epoch moves
(`add_function`/`add_global`/`add_class`/`register_template_type`). Executes with host
instance variables check fresh per call (the name set is per-call). Walk cost is
parse-order (same tree, no allocation-heavy state).

## Shadowing warnings (2026-07)

Warning severity ALWAYS (never gates strict — shadowing is legal). Shapes:

- local / parameter / range-for variable / catch variable shadows a FIELD (or static
  field) of the enclosing class, including inherited in-script bases:
  `local 'hp' shadows field 'hp' of class 'Creature' (field declared at 4:2)`.
  Constructor params named after the field they initialize are the established idiom
  and stay silent; `[=]`/`[&]` lambda bodies can't reach fields, so only `[this]`
  lambdas participate.
- local shadows an outer local/param: `local 'x' shadows a declaration in an
  enclosing scope (declared at 2:5)`.
- declaration shadows a global the same function already READ through the global
  tier: `local 'total' shadows global 'total' used earlier in this function`.
  (Plain local-over-global with no earlier use is silent — too common to flag.)

Related inference fix shipped with this: runtime bare-name resolution reaches GLOBALS
before this-fields (see the classes guide chapter, "Name resolution in methods"), so a
name that is both a global and a field is flow-order-ambiguous to the static walk —
member hits on such names degrade to unknown instead of trusting the field's declared
type (the old field-first inference produced a strict-mode false positive, pinned in
`field_global_ambiguity_is_lenient`).
