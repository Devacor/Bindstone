# Static type checking

Backend-neutral AST checker that runs at parse-cache-entry creation — one hook in
`engine.cpp` covers `execute(string)`, jaibite creation, hot-reload re-parses (new source
= new cache entry) and registration-epoch changes uniformly. Amortized once per unique
source, like parsing. Strict by default; the off path costs one branch. Implementation:
`detail/type_checker.hpp` + `source/implementation/type_checker.cpp`.

## Mode API

```cpp
engine->static_checking(check_mode::off | warn | strict);   // default: strict
check_report r = engine->check(source);        // parse (cached) + check, ANY mode; tooling
const check_report& d = engine->last_check_diagnostics();   // last checked execute/bite
```

- **strict** (the default) — any error-severity diagnostic makes `execute()` /
  `engine->jaibite()` / `jaibite_load*()` throw `jai::static_check_error` BEFORE anything
  runs. A strongly-typed → strongly-typed mismatch (`int x = "s"`, returning a string from
  a function declared `-> int`, passing an array to an `int` parameter, an unrelated class
  into a typed slot) is therefore a load error; only a `var`-sourced value can still fail
  at runtime, where `try`/`catch` sees it.
- **warn** — diagnostics collected and retrievable; execution proceeds and the runtime's
  own conversion errors apply unchanged (the pre-checker behavior, selectable per engine).
- **off** — zero behavior change. No diagnostics collected anywhere.

`what()` under strict is the full accumulated, location-sorted, deduplicated listing
(cap 100), compiler style:

```
script:12:5: error: cannot assign 'string' to 'x' declared 'int' (declared at 8:3)
    x = "hello";
    ^
```

Diagnostics are mode-independent (`check_diagnostic {severity, line, column, message,
related_line/column, strict_region}`); mode only decides whether errors gate execution
(strict: every error; warn: only errors a `[[strict_mode]]` region raised - "Escape hatches").
Warning severity never gates.

## Rule table (mirrors `detail::convert_for_typed_slot` — the one table for declarations, assignment, parameters, returns and fields)

| target | accepts | notes |
|---|---|---|
| `int` | int, float (truncates), bool, object with `to_int()` | string/char/null/containers = error |
| `float` | float, int, bool, object with `to_float()` | |
| `bool` | anything | truthy conversion |
| `string` | anything | to_string conversion; objects use their `to_string()` when defined |
| `char` | char, object with `to_char()` | `char c = 65` is a runtime error too (`cast<char>` is the spelling) |
| `array` / `map` | array / map | element/key types NOT checked statically (lenient v1); the runtime element boundary runs on every path (`Cannot store string in array<int>`) |
| object `C` | null (a typed null); same class; derived→base; a `shared_ptr<C or derived>` handle; ctor conversion `C(src)` through exactly ONE true 1-param ctor whose parameter takes `src` directly (no chaining, no `to_X()` on the way in); on plain assignment, a user `operator=` overload of `C` (or an in-script base) that accepts `src` | unrelated classes with no viable 1-arg ctor or `operator=` = error; array/map/function into object = error; a registered ancestor's `operator=` is lenient |
| `shared_ptr` / `weak_ptr` / references / function slots | anything | lenient v1; `null` stores a typed null on every path |
| `var` | anything, forever | THE opt-out: a var-typed expression makes downstream types unknown (like TS `any`); unknown never errors |
| `auto` + initializer | inferred, then enforced as that type | a `null` initializer leaves it dynamic — null carries no type; the runtime locks at the first non-null store |
| `auto` without initializer | dynamic (unchecked) | runtime locks at first assignment; the checker can't see branch order, so it opts out — documented leniency |

**One table.** By-value parameters, returns and field stores run exactly this table at run
time (`detail::convert_for_typed_slot`), the array/map element boundary included, so a value
the checker admits converts. A chained conversion (`TA -> TB -> TC`, `X.to_int() -> C(int)`)
is never implicit; a converting constructor or `to_X()` that throws surfaces its own error.
Each site names its slot in the rejection text (`docs/diagnostics.md` family D):
`Cannot initialize 'x' (declared int) with S` for declarations, `Cannot assign S to 'x'
(declared int)` for `=`, `Cannot assign S to field 'f' of C (declared int)` / `... to static 's'
of C ...` for fields, `Cannot pass S to parameter 'n' of 'fib' (declared int)` and
`Cannot return S from 'f' (declared -> int)` for parameters and returns, `Cannot convert S to
C (no suitable single-argument constructor)` for a class-target parameter or return, the
container text (`Cannot store S in array<int>`) on every path. `S` is the value's own type
(`string`, `null`, `array`, a class name), never a `var` tag. The
return conversion is part of the `return` statement (C++ semantics): a `try` around the
`return` inside the callee catches its failure - the catch's own `return` converts in turn,
a converting constructor or `to_X()` that throws during the conversion is caught the same
way, and so is a reference return's "must return an lvalue reference". Uncaught, the
returning function is the top frame of the stack trace with the call site below it (lambdas,
methods and resumed coroutines alike; a coroutine's frame sits above the resumer's) and the
error surfaces in the caller.

**Compound assign** (Dev ruling 2026-07, settled): typed targets follow C++ rules —
`x op= rhs` is `x = T(x op rhs)`, so `int x; x += 2.5` is fine (truncates) and
`x += "s"` is an error (string result into int). `var` targets keep dynamic behavior,
unchecked.

**Typed fields** (Dev ruling 2026-07, settled): declared field types are real and enforce
like locals — field initializers, member stores (`obj.field = v`, bare `field = v` inside
methods, inherited fields) all check against the declared type.

**Calls** — checked only when the callee resolves statically:
- same-script functions/ctors/methods: arity (with defaults) + per-arg assignability
  (same conversion table, element boundary included — note a `string` parameter accepts anything). Overloads: error
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
make later declarations visible), the host's per-call instance variables, the top-level
names of every unit in the program (Programs below — a literal `import`/`include` never
hides a name), `extern` declarations (Escape hatches below — a name defined outside the
program, typed or not), and the engine's full registration surface (globals, functions,
classes, template types, namespaces — visible because checking happens at execute/jaibite
time).
Only a unit with a dynamic-path `import(expr)`/`include(expr)` drops undeclared-identifier
to warning severity (that file can define anything). Lambda capture-list entries are not
identifier uses (`[this]` inside a method reaches the class); `weak_from_this()` /
`shared_from_this()` are call-shape builtins resolved before any lookup, like the runtime.

**Language namespaces** — `math::<fn>(...)` is the intrinsic lane (`detail/
math_intrinsics.hpp`, the same shape gate both backends use): typed by the kernel's result
(`int` for `itrunc`/`ifloor`/`wrap_*`, `float` for the transcendental and interpolation
families, operand-typed for `abs`/`min`/`max`/`clamp`/`random` — uniform int → `int`,
uniform float → `float`, mixed or unknown → unknown; `random_seed` unknown). Arity is the
kernel's runtime error, not a static one. A user namespace path `a::b::c` (a flat name;
declared here or registered with the engine) is lenient at every depth.

**Members** — on a receiver whose class is fully known (in-script hierarchy resolvable, or
engine-registered `class_definition` incl. `_get_`/`_set_` properties and statics),
an unknown field/method is an error, reads and writes both (declared intent — the runtime
tolerates dynamic field creation on writes). A member that only a SUBTYPE declares is an
error on a receiver whose static class is certain, exactly like C++ — the fixes are a `var`
receiver or `cast<T&>(expr).member`. A member that a user `_get_x`/`_set_x` declares (same
unit or registered, anywhere in the chain) is known, and is never a proof source (reads or
writes). Its static type is the accessor's declaration when that declaration is unambiguous:
`expr.x` reads as the getter's declared return type when the receiver's chain (this unit and
the program table) holds exactly ONE user `_get_x`, parameterless, not a coroutine, declaring
a non-`var` type (`int _get_hp()` makes `char c = obj.hp` an error, `string _get_name()` makes
`int n = obj.name` one); `expr.x = v` and the compound forms check `v` against the single
one-parameter by-value user `_set_x`'s declared type (`void _set_hp(int)` makes `obj.hp = "s"`
the error `cannot assign 'string' to property 'hp' declared 'int'`; `obj.hp += "s"` needs the
getter's type too and is lenient without one). Two getter (or setter) overloads anywhere in
the chain, a `var`/`auto`-returning getter, a getter with parameters, a coroutine getter, a
reference or untyped setter parameter, or a chain that reaches a REGISTERED ancestor carrying
the accessor (a binder property, a class from an earlier execute: no declaration in view) all
leave the member unknown - lenient, as before. A bare `x` inside the class keeps the field's
declared type. Static fields are typed like instance fields: `C::s`, a bare `s` inside the class and the
compound forms check the store against the static's declared type (the runtime enforces the same
table on every spelling). Any unresolvable ancestor opens the class: silence.
Builtin container/string/map methods, subscripts, enum values (existence IS checked:
`Direction.zzz` errors), namespaces: lenient. The handle builtins every object answers
(`use_count`/`cpp_ref_count` → int, `unique`/`same_as` → bool, `reset`) are lenient on any
object receiver; a class method of the same name wins first.

**Casts** — `cast<T>(e)` types as certain `T` (a typed local initialized from it proves
`same_type`), `cast<T&>(e)` as `T` for member lookup only (a reference: never a store proof),
`cast<shared_ptr<T>>(e)` as the handle spelling; primitives type as the primitive. The operand
is the explicit typed boundary and is never checked against `T` — the verdict is the runtime's
(`Cannot cast 'X' to 'T'`, catchable). A `T` that names no class is an error
(`unknown type in cast`). A value-form class cast whose operand is statically a `shared_ptr<T>`
handle or a declared reference draws a warning (never gates): `cast<C>(h) copies the instance -
use cast<C&>(h) to alias it or cast<shared_ptr<C>>(h) to share it`; `cast<T&>`,
`cast<shared_ptr<T>>`, primitive targets and `var`/unknown operands stay silent, and a `weak_ptr`
operand is lenient here as everywhere (the runtime raises `Cannot cast 'weak_ptr' to 'T'`).

Open surfaces (misses silent, hits still typed):
- a class this unit declares that the engine already holds with a different instance or
  static field set is a hot-reload redefinition: receivers that carry the OLD shape are
  open — values that predate the unit (engine globals, whose type is read off a value tag)
  and `this` / bare field names inside the class's own methods (migration hooks run on the
  old shape; probes expect the runtime error). A fresh instance of the new definition in
  the same unit is checked against it, and an identical re-execution keeps the same field
  set and stays closed everywhere.
- a C++-bound class once any script layer was `extend`ed over it (`class_definition::
  extended_ever`), and — flow-insensitively, by name — every variable or member passed to
  `extend(...)` anywhere in the unit.
- a C++-bound class whose registration lacks the member while a REGISTERED subclass binds
  it: the binder surface is a lower bound on the C++ declaration (a virtual bound only on
  the derived still dispatches through a base-typed handle), so the miss is uncertain.
  In-script classes are closed by their own text and do not get this walk.

**Recovery** — an erroring expression assumes its declared/expected type, so one bad line
produces one diagnostic, not a cascade; (line, col, message) dedupe on top. Cap 100
(`detail::k_check_diagnostic_cap`), `report.capped` set.

## Programs

A unit whose top-level `import`/`include` declarations carry literal paths is the root of a
**program**. Before anything runs, the engine gathers the program's **closure**: every unit
those paths reach, resolved through the include paths exactly as the runtime resolves them,
recursively, each file once (cycles are fine), parsed into the same cache entries the unit's
later `import` executes through. The union of every unit's top-level names — functions,
variables, destructured names, classes, enums and their values, namespaces with every `::`
prefix, namespace members flattened — is handed to the checker as declared names, and the
root plus every closure unit whose entry is not already checked at the current surface epoch
is checked against it. A function in one file may therefore use a global or class another
file declares, whatever the import order (GLOOM's `maps.jai` reads `G`, declared in
`game.jai`, imported last). The closure carries **class models** as well as names: every
top-level class of every unit (parent, fields with declared types, methods with signatures,
constructors) is modeled exactly as a class declared in the unit being checked, and member
lookup consults the unit's own classes, then the program's, then the engine's registrations,
so a subclass whose parent lives in another unit resolves inherited fields and methods bare
in its methods, through `this`, and on typed receivers — down a chain that may end in a
registered C++ class — and a typed mismatch against a cross-unit field or method is the same
error the same-unit case reports (without the declaration's location, which belongs to the
other file). A parent found nowhere — not this unit, not the program, not registered (a
dynamic-path import, an `extern` name used as a base) — can define anything: bare names in
that class's methods type as unknown and are never `undeclared`. Class knowledge that comes
from the program table alone is for diagnostics: a cross-unit member types as tentative
(checked, never a proof source) until its declaring unit has run; that run moves the surface
epoch, the unit re-checks with the registered definitions in view, and proofs land on that
second check — the same second-check rule as a unit that declares its own classes.

Under strict, an error anywhere in the program fails the root's `execute()` before any unit
runs. `what()` lists the root's diagnostics first, then each erroring unit's own listing,
formatted against that unit's source and named by its resolved path:

```
D:/.../scripts/maps.jai:12:5: error: use of undeclared identifier 'G'
    G.map = m;
    ^
```

Under warn — and in the report `engine->check()` returns — closure diagnostics follow the
root's with line/column 0 and the path-qualified location leading the message (the snippet
belongs to another source, so none is printed).

The union is the engine's **program-name memory**: it lives for the engine's lifetime and
rides every later check, so a leaf re-parsed on its own after a hot edit (a new cache entry
reached through `import`) still knows the names of the program it belongs to. The memory is
a superset by construction — a name it admits is never a false error, only a typo the
checker cannot catch. The class table rides the engine the same way; a class re-declared with
a different shape replaces its entry at the next gather (the last unit gathered wins), and a
unit's own declaration always wins over the table's.

A unit the closure cannot resolve, read or parse contributes no names and is skipped; the
runtime reports it exactly as before, when the unit executes. A **dynamic-path**
`import(expr)`/`include(expr)`, or an import below the top level, can define anything: that
unit keeps warning severity for undeclared identifiers. Names the program never declares —
a host global bound after load, a value injected at run time — are outside the closure by
definition; `extern` (below) is the spelling that declares such a name to the checker without
defining it.

## Escape hatches

Two spellings tell the checker about what it cannot see or should not gate on, without
loosening anything else.

`extern` declares a name that exists outside the program - a host global bound after load,
a function the running instance registers, a value injected at run time:

```
extern function spawnOnNetwork;   // a callable of unknown signature: calls are unchecked
extern shared_ptr<Game> G;        // typed: G.hp is checked against Game and proven per "Proofs"
extern config;                    // untyped: var - reads and stores are dynamic
```

An extern name counts as declared, and a program's closure carries it to every unit like any
top-level name. It is also the spelling for a shared leaf that calls into a module only some
programs import (a netplay hook behind a runtime flag): the leaf declares the names it
reaches for, and in a program that imports the module too the declaration is inert - the
closure knows the real definition, and the call resolves to it at run time. The typed form
gives the name that certain type: member checks, assignment checks and proofs flow exactly
as for a declared variable of that type (a proof stays a runtime-guarded lane, so a host
value that contradicts the declared type runs the ordinary conversion). At run time the
declaration is a no-op on both backends - the name resolves dynamically as before, and a
name still missing when used raises the ordinary `Undefined variable 'name'` error.

`[[warn_mode]]` and `[[strict_mode]]` set the checker's mode for a region of one unit. They
compose, innermost wins, and the engine's mode (`static_checking`) is the default for every
unit. An attribute leads one of three positions:

- **A unit.** As the first declaration of a file (a leading comment is fine, the trailing `;`
  optional) it sets the unit's mode: `[[warn_mode]];` at the top of a file keeps the checker
  running there but reports every error it finds as a warning.
- **A statement.** Before any statement - top level, in a function/method/lambda body, in an
  if/while/for body, in a bare block - it covers that one statement's own subtree, then the
  mode reverts: `int x = 5; [[warn_mode]] y = 5; z = 5;` leaves `y` lenient and `z` checked.
  A nested function or lambda body is a new scope that keeps the ENCLOSING mode:
  `[[warn_mode]] self.spawnOnNetwork([](){ x = 5; });` accepts `self`, the member and the
  call, but `x = 5` inside the lambda is still an error.
- **A block.** As the first statement of a block - a function/method/lambda body, an
  if/while/for body, a bare `{ }` - it covers the whole block, nested bodies included:
  `self.spawnOnNetwork([](){ [[warn_mode]] x = 5; });` demotes the lambda body only (the call
  is still checked); `{ [[warn_mode]] y = 5; z = 5; }` covers both statements. Two attributed
  statements in a row each cover their own.

In a warn region every error-severity diagnostic is reported as a warning (still collected,
still in `last_check_diagnostics()` and `engine->check()`), and an undeclared identifier is
accepted as unknown - an undeclared callee is assumed callable with an unknown result, so
nothing downstream is proven from it. In a strict region errors gate the load: `[[strict_mode]]`
inside a warn region re-arms strict there, and a `[[strict_mode]]` unit or statement in a
warn-mode ENGINE gates that load even though the engine's own errors do not (a diagnostic
raised in a strict region carries `check_diagnostic::strict_region`;
`check_report::has_strict_errors()` is what warn mode gates on). Engine mode `off` runs no
diagnostics walk at all (proofs still stamp), so attributes have no effect under off. The
runtime never changes: both backends treat the attribute as nothing, and a warn region's type
mistakes raise at run time exactly as they would without the checker. A unit's mode says
nothing about the units it imports, and a `[[warn_mode]]` leaf never fails its program.
`warn_mode` and `strict_mode` are the only attribute names: `[[bogus]]` is a parse error
naming the attribute, and `[[` in expression position opens a nested array literal.

## The false-positive contract

A program that runs clean and does not genuinely violate declared types must produce ZERO
error diagnostics. Everywhere the static story is uncertain the checker goes silent
(`var`, unknown/open classes, element types, includes, callable variables, host object
conversions). Enforced by `static_check_tests.cpp::fuzz_corpus_false_positive_gate`: 200
differential-fuzzer generator seeds; any program that executes cleanly must check clean.
Grow that seed count before loosening any lenient rule.

### Precision map

Where strict typing is lenient BY DESIGN - the whole false-negative surface, in one place. A
mistake in any of these spots is a run-time error, never a load error:

- `var` anywhere, and everything computed from it; `auto` without an initializer (locks at
  the first store, flow the checker cannot see); a `null` initializer.
- References (`T&` declarations, `auto&` loop variables, by-reference parameters) and
  `shared_ptr<T>` / `weak_ptr<T>` handles as STORE targets (member lookup through a typed handle
  or reference is still checked).
- Container elements: `array<T>` / `map<K,V>` element and key types (the runtime element
  boundary is the check), subscripts, builtin container/string methods.
- Engine globals (typed off the value tag: a later retag is invisible), host-object
  conversions, callables held in variables, host functions beyond arity and primitive
  certainties, binder (C++) methods reached through a script chain (no static signature:
  wrong arity or a wrong argument type raises at the call), binder properties (untyped).
- Dynamic-path `import(expr)`/`include(expr)` units and below-top-level imports (undeclared
  identifiers drop to warnings), `extern` names (untyped: `var`; typed: checked), a parent found
  nowhere (its subclass's bare names type as unknown), namespaces at every depth.
- Accessor-bearing members whose declaration is ambiguous ("Members": two getter/setter
  overloads in the chain, `var`/`auto`/coroutine/parameterized getters, reference or untyped
  setter parameters, an accessor on a registered-only ancestor).
- Coroutines: yield and return types, and a call yields a handle (typed as unknown).
- Mixed-branch ternaries, `this`/`super` as values, hot-reload redefinitions (receivers that
  carry the old shape are open), `extend`ed classes and everything passed to `extend(...)`.
- For PROOFS (not diagnostics): cross-unit classes until their unit has run, every row above,
  and every accessor-bearing member, typed or not.

## Proofs

The same walk that produces diagnostics also produces **proofs**: annotations on AST nodes
saying "the conversion this site would run is the identity" (`assignment_expr::
static_store_proof`, `variable_decl::static_init_proof`, `call_expr::static_arg_proofs` +
`proven_callee_body`, `return_stmt::static_return_proof`) or "this receiver's field lives in
slot N of class object C at field-shape epoch E" (`member_expr` / `identifier_expr`
`proven_class`/`proven_slot`/`proven_epoch`/`proven_field_type`/`proven_field_write`).
`binary_expr::static_operand_class` is an annotation only. Both backends consume proofs
behind TOTAL runtime guards (`detail/type_proofs.hpp`: `same_type_payload`,
`proven_field_instance`/`proven_field_cell`): a proof selects a lane, the guard makes the
lane sound, and a guard miss runs the existing code byte for byte. There is therefore no
program whose value, printed output, error text or `type_of` differs between
`engine::type_proofs(true)` (the default) and `type_proofs(false)`.

**Certain types** — a proof is stamped only from a certain static type on BOTH sides: a
concrete `int`/`float`/`bool`/`char`/`string`, or a class known to the checker (in-script or
engine-registered) reached as a value object with the SAME interned spelling on both sides.
Everything the rule table treats leniently is uncertain by construction: `var`, `auto`
without an initializer, reference declarations and by-reference parameters, `shared_ptr<T>`
/ `weak_ptr<T>` handles, containers (`array<T>`, `map<K,V>` — container enforcement is not
the identity), engine globals (their type is read off a value tag, never a declaration),
includes, callable variables, mixed ternaries, `this`/`super`. The certain set is the
complement of the lenient rows, so the false-positive contract above doubles as the
proof-soundness contract.

**Field proofs** need a class object: the receiver's certain class must be registered with
the engine, its whole chain script-only (no C++ base, never `extend`ed), all-public, and the
member a declared instance field with the synthesized accessors (no custom `_get_`/`_set_`,
no method of the same name, not a static, not `_cpp_object`, not a shared_ptr builtin name,
not a global-tier name). Bare-name field proofs additionally require a method/ctor body or a
`[this]` lambda (not a static method, not a `: super(...)` argument list, no includes). A unit
that declares its own classes gets its field proofs on its SECOND check: registration moves
the surface, the cached entry re-checks, and identical reloads afterwards keep the stamps.
Writes are proven only for scalar/string/object/`var` fields.

**Calls** are proven only against exactly one same-script overload with exact arity — no
defaults in play, no reference parameters, not a coroutine, no includes — and the stamp
names the callee's body so a different function bound to the same name at runtime simply
misses.

**Hot reload** — every field proof carries `class_definition::field_shape_epoch()`, which
moves on field-set, declared-type, parent-chain, access-label and accessor changes (and on
every non-identical reload) but NOT on an identical reload. A retyped field therefore
misses its proof and converts or resets through the existing migration/enforcement paths;
a proof never produces an error the unproven path would not. Class registration and
non-identical redefinition also move the checker's surface epoch, so cached entries
re-stamp at their next execute.

**Switch** — `engine::type_proofs(bool)`, default on. Off: the pass does not run for fresh
entries, and cached entries/jaibites already carrying proofs are stripped at their next
execute, so an engine toggled off is observationally the old engine. `engine::last_proof_stats()`
counts the stamps of the most recent pass (stores/decls/args/returns/members/calls) for tests.
Proofs are never serialized: a loaded jaibite re-derives them like the parser marking passes.

**Bytecode carriers** — the vm reads decl, call, member and bare-identifier proofs off the
AST node at execution time (a chunk pins the nodes it consumes, so a re-stamp reaches code
compiled earlier), while store and return proofs ride instruction flags that freeze when
the chunk compiles. A store/return proof that appears on a later re-check therefore
benefits only chunks compiled afterwards; a proof that disappears leaves a bit the guard
turns into a plain skip. Method bodies compile at their first call, so a class whose unit
is executed before its methods run carries its field-store proofs in bytecode; a unit that
defines and immediately calls its own class compiles those bodies before its second check
and keeps the node-borne read/write lanes only.

**Not proven** — conversions (int→float widening, bool/string coercions, null→object,
derived→base, ctor conversions), containers and typed maps, compound assignment and the
`++`/`--` desugars, `store_through` references, static fields, `super.f`, `?.` receivers,
engine globals, `_cpp_object`, hybrid (C++-base) and `extend`ed classes, transparent
wrappers, callable locals, engine functions, `new_expr`, coroutine callees, defaulted or
by-reference parameters, worker (parallel) frames, and `binary_expr::static_operand_class`
(annotation only). All of these run the existing code with proofs on or off. A return proof
is consumed at the `return` statement itself on both backends (the in-region conversion op
and every completion door alike), always behind the payload guard.

**Differential gate** — the contract is pinned four ways: `static_check_tests.cpp::
proof_differential_gate` runs 200 generator seeds on {interpreter, vm} × {proofs on, off}
(fresh engines, identical registrations) and requires identical segment results, printed
output, error text and liveness per backend; `strong_types_tests.cpp::proof_*` batteries run
every certain kind through stores/decls/args/returns/field reads+writes on the same four
lanes and pin the values, with `last_proof_stats()` proving the lanes are reachable;
`hot_reload_correctness_tests.cpp::proof_*` arm field proofs and then retype, drop, hide,
re-parent, hybridize or `extend` the class against a `type_proofs(false)` twin; and
`jaiscript_fuzz --no-proofs` (plus the fuzz smoke suite's no-proofs band) keeps the
interpreter-vs-vm differential green in both proof states.

## jaibite

A bite created under warn/strict is checked at creation (strict throws there);
`bite.checked_clean()` / `bite.check_diagnostics()` expose the result. `save()` stamps a
checked-clean bit in the header's existing flags u32 (no format version bump; old loaders
ignore it). On load the stamp is trusted ONLY when the registration fingerprint matches
the saving engine's; otherwise the bite loads unchecked and, under warn/strict, is
re-checked at load against THIS engine's surface (strict throws on errors). A bite is
checked once — registrations after that don't re-trigger; re-create the bite to re-check.
Proofs are independent of that: they stamp at creation/load/execute in every mode and
re-stamp whenever the surface epoch or the proof switch moved, while the diagnostics stay
checked-once.

## Cost

One AST walk per unique source per surface epoch, in every mode, cached beside the parse
(the proof pass rides it; diagnostics are collected only under warn/strict). Re-run only
when the registration surface epoch moves (`add_function`/`add_global`/`add_class`/
`register_template_type`, script-class first definition and non-identical redefinition) or
the proof switch flips. `type_proofs(false)` under `off` restores zero walks (one branch at
cache-entry creation). Executes with host instance variables check fresh per call (the name
set is per-call; that per-call walk never touches the cached stamps). Walk cost is
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
