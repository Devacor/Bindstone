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
  into a typed slot) is a load error when the source type is known. Dynamic or
  unresolved sources and value-dependent failures still reach runtime, where
  `try`/`catch` sees them. Examples include a computed float outside the int range
  and a missing map key returning null.
- **warn** — diagnostics collected and retrievable; execution proceeds and the runtime's
  own conversion errors apply unchanged (the pre-checker behavior, selectable per engine).
- **off** — zero behavior change. No diagnostics collected anywhere.

`what()` under strict is the full accumulated, location-sorted, deduplicated listing
(cap 100), compiler style:

```
<script>:12:5: error: cannot assign 'string' to 'x' declared 'int' (declared at 8:3)
    x = "hello";
    ^
```

Diagnostics are mode-independent (`check_diagnostic {severity, line, column, message,
related_line/column, strict_region}`); mode only decides whether errors gate execution
(strict: every error; warn: only errors a `[[strict_mode]]` region raised - "Escape hatches").
Warning severity never gates.

Tools drive the walk directly: `detail::run_static_check(eng, decls, extra, options)` with the
program tables of their choosing (`program_names`, `program_classes`), and
`check_options::observer` (a `check_observer`) receives every expression's inferred type in the
checker's own lattice (`check_expression_type`: kind, class, declared `type_info`, spelling). The
language server (`tools/lsp`) is built on exactly this; the observer changes nothing the walk
reports. `parser::recovered_declarations()` hands a tool the top-level declarations a failed parse
read around its errors.

## Rule table (known source and destination types)

| target | accepts | notes |
|---|---|---|
| `int` | int, float (truncates), bool, object with `to_int()` | string/char/null/containers = error |
| `float` | float, int, bool, object with `to_float()` | |
| `bool` | anything | truthy conversion |
| `string` | anything | to_string conversion; objects use their `to_string()` when defined |
| `char` | char, object with `to_char()` | `char c = 65` is a runtime error too (`cast<char>` is the spelling) |
| `s[i]` of a declared `string` / `utf8_string` (store target) | char, string, utf8_string (the text replaces the character) | anything else is `cannot store into 'string': a string character takes a char or a string, got 'int'`; a compound store checks the operator's result on the character (`s[i] += 1` is int, rejected; `s[i] += "x"` is text) |
| `array<T>` / `map<K,V>` | compatible container elements and keys | known literal elements, nested literals and declared container types are checked; typed subscript reads carry T/V, and typed push arguments and subscript stores are checked; var-sourced values stay lenient |
| object `C` | null (a typed null); same class; derived→base; a `shared_ptr<C or derived>` handle; ctor conversion `C(src)` through exactly ONE true 1-param ctor whose parameter takes `src` directly (no chaining, no `to_X()` on the way in); on plain assignment, a user `operator=` overload of `C` (or an in-script base) that accepts `src` | unrelated classes with no viable 1-arg ctor or `operator=` = error; array/map/function into object = error; a registered ancestor's `operator=` is lenient |
| `shared_ptr<T>` | null; an object of `T` or a subclass; a handle whose STATIC pointee is `T` or a subclass (an upcast); `cast<shared_ptr<T>>(h)`; a `var`/unknown source (judged at run time by its object's class) | a handle statically typed `shared_ptr<Base>` (a downcast needs the cast) or an unrelated class = error, at a declaration, `=`, a field, a `shared_ptr<T>` namespace variable, a parameter, a return and a container element (R-q6, Dev ruling 2026-09-30) |
| `weak_ptr<T>` | null or a compatible shared/weak handle | same pointee or derived-to-base; unknown sources stay lenient |
| `T&` declaration | T, or a derived class for a base-class reference | conversions do not make a mismatched reference binding valid; unknown sources stay lenient |
| `function` variable or field | callable or null | calls through callable variables and function-typed parameters remain unchecked |
| `var` | anything, forever | the dynamic escape: downstream types are unknown; a typed container copied into var retains its element constraint; explicitly use array<var> for a dynamic-element copy, and crossing back into a typed container validates its contents |
| `auto` + initializer | the initializer's static type, then enforced as that type | a `var` or unknown static type (a `var` local, an `array<var>` element, a `var`-returning call, a mixed-branch ternary other than int with float, whose static type is float) makes the slot a `var`: the unit's first proof pass writes that into the declaration in every mode (proofs on or off, any check mode, a `.jaibite` load), once, so the runtime never locks to a value's run-time type; a `null` initializer leaves it dynamic — null carries no type; the runtime locks at the first non-null store |
| `auto` without initializer | dynamic (unchecked) | runtime locks at first assignment; the checker can't see branch order, so it opts out — documented leniency |

**Conversion rules.** Scalar by-value parameters, returns and field stores use the
runtime conversion table (`detail::convert_for_typed_slot`). Container elements
use their own boundary: int/float and bool/int conversions, matching string/char
types, compatible object classes and recursive nested containers. Storing an int
in a string element requires to_string(value) or cast<string>(value). Map keys never coerce.
Shared- and weak-handle elements admit null; shared-handle elements also admit a
live instance of the declared class or a subclass. By-value elements stay non-nullable. Float literals outside
the int64 range are load errors into int slots; computed values are checked at
runtime. Unknown values and user conversion bodies can still fail at runtime.

The checker also enforces the declared weak-handle, reference and callable rules
even where an unchecked runtime store would accept the value. Known container
types follow the runtime element boundary, including empty typed containers.
`var` preserves dynamic checking, and `[[warn_mode]]` downgrades diagnostics. A chained conversion (`TA -> TB -> TC`, `X.to_int() -> C(int)`)
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
`x += "s"` is an error (string result into int). A `string` variable or field appends text
only, as the runtime does: `s += 'c'` and `s += "t"` append, `s += n` with a known non-text
operand (an int, a float, a bool, null, a container, an object) is `cannot apply '+=' to
'string' and 'int'` - the promoted type counts, so `s += to_char(a) + to_char(b)` is `string +=
int` (`docs/char_semantics.md`; `"" + to_char(a) + to_char(b)` is the text spelling). An
element target (`names[i] += 5`) concatenates at run time and stays lenient; a registered
global `+` operator makes the rule lenient. `++`/`--` on a string member (the `+= 1` desugar) is
`cannot apply '++' to 'string'`. `var` targets keep dynamic behavior, unchecked.

**Typed fields** (Dev ruling 2026-07, settled): declared field types are real and enforce
like locals — field initializers, member stores (`obj.field = v`, bare `field = v` inside
methods, inherited fields) all check against the declared type.

**Calls** — checked only when the callee resolves statically:
- same-script functions/ctors/methods: arity (with defaults) + per-arg assignability
  (same conversion table, element boundary included — note a `string` parameter accepts anything). Overloads: error
  only if NO overload matches. A reference parameter aliases compatible storage without conversion: the exact primitive or math kind, the class or a subclass, and for containers and handles the same concrete type (an argument of unknown element type stays lenient). A string
  literal for a viable `string&` overload is accepted whether or not the callee writes through
  the parameter (the runtime binds a private cell; a string variable still aliases). For other types, a literal
  (a number, string, char, bool or null literal, an array or map literal) where every viable
  overload binds the parameter by reference is accepted when no overload - and, for a method,
  no override in a class derived from the receiver's - writes through the parameter
  (inferred-const reference parameters, `detail/ref_param_writes.hpp`: the runtime binds a
  temporary cell, like C++ `const T&`); otherwise it is `'inc' writes through parameter
  'x' (line 12); pass a variable`, the line of the first write (or of the call that passes
  the parameter on to a writer). The runtime refuses a temporary for a writer with
  `Cannot pass non-lvalue to reference parameter`. A write is an assignment, compound
  assignment or `++`/`--` rooted at the parameter or a reference alias of it, a method call on
  it (on an array, map or string parameter the library's reading methods excepted), passing
  it on to a writing `&` parameter (followed through the calls; an overloaded name counts as
  unseen), a `shared_ptr` parameter, a constructor or an unseen callee (a lambda, a host
  function, a user method; the printing/conversion builtins, `math::`/`string::` and the
  container builtins take copies), or returning it by reference. A lambda called through a
  variable keeps needing a variable (a `string&` excepted). A coroutine binds a copy of its arguments, so a literal
  is fine there.
- host (C++) functions via `engine::host_function_signatures`: arity across overloads
  (variadic = anything), and per-arg only certainties (a known primitive with no builtin
  conversion to a primitive param — e.g. `square("s")`; numeric↔numeric is fine; objects
  and `script_value` wildcard params accept anything). Note the C++ boundary has NO
  numeric→string conversion, unlike script `string` slots. The call's result is typed by the
  C++ return type when every registration of the name declares the same scalar (`int`,
  `float`, `bool`, `char`, `string` - `to_char(n)` is a `char`, `engine::host_function_result`);
  anything else (a `script_value`, an object, registrations that disagree) is unknown. The
  type is a diagnostics fact, never a proof source.
- callables in variables (lambdas, cross-execute function values): unchecked.

**Returns** — a returned value must be assignable to the declared return type; a value from a `void` fn is
an error. A primitive, string or reference result holds no null, so a bare `return;` and a path that
reaches the end of the body are errors there (`detail/return_flow.hpp`, a load-time walk: no call or
return pays for it); an end reached only through a `catch` of an exception the walk cannot prove is
raised is a warning. A class, handle or container result returns null on those paths: warning
severity. Lambdas with a declared return type follow the same rule. Coroutines are exempt: calling one
yields a handle, and yield/return types aren't checked.

**Identifiers** — undeclared = error, checked against: scopes, the current class's members
(incl. inherited), top-level declarations (two-pass, so forward references work), every
name declared ANYWHERE in the script (flow-insensitive safety net — execution order can
make later declarations visible), the host's per-call instance variables, the top-level
names of every unit in the program (Programs below — a literal `import`/`include` never
hides a name), `extern` declarations (Escape hatches below — a name defined outside the
program, typed or not), and the engine's full registration surface (globals, functions,
classes, template types, namespaces — visible because checking happens at execute/jaibite
time).
`::name` reaches the program scope only (the unit's top level, then the program and the engine),
never a local or a field of the same name, for reads and stores alike. A store to a
program-scope name the unit does not declare checks against that name's declaration: the
engine's cell once a unit has run (typed off its tag, like any engine global), else the one
declaration with a spelled type that exactly one unit of the program makes - so a reload stub's
`::PACK_MAX = "lots";` is the same load error as `int PACK_MAX = "lots";` in the declaring file.
Only a unit with a dynamic-path `import(expr)`/`include(expr)` drops undeclared-identifier
to warning severity (that file can define anything). Lambda capture-list entries are not
identifier uses (`[this]` inside a method reaches the class); `weak_from_this()` /
`shared_from_this()` are call-shape builtins resolved before any lookup, like the runtime.

**Language namespaces** — `math::<fn>(...)` is the intrinsic lane (`detail/
math_intrinsics.hpp`, the same shape gate both backends use): typed by the kernel's result
(`int` for `itrunc`/`ifloor`/`wrap_*`, `float` for the transcendental and interpolation
families, operand-typed for `abs`/`min`/`max`/`clamp`/`random` — uniform int → `int`,
uniform float → `float`, mixed or unknown → unknown; `random_seed` unknown). Arity is the
kernel's runtime error, not a static one. A host namespace entry that carries a signature
(`script_namespace_data::signatures`: the arity range, each argument's kind, the result) is
checked against it - the `string::` library declares one per function, so
`string::repeat(to_char(a), 3)` is `argument 1 of 'string::repeat': cannot pass 'char' where a
string is expected`, a wrong count is `'string::pad_left' expects 2 to 3 argument(s), got 1`, and
the result is typed (text of the first argument's kind, `bool` for `is_valid_utf8`); the `time::`
entries declare theirs too (`'time::now' expects 0 argument(s), got 1`, `time::sleep("x")` is `argument 1 of
'time::sleep': cannot pass 'string' where a number is expected`, `time::local_now()` types as `time::date`). A unit that
declares the namespace itself, or a script function of the entry's name, stays lenient. Any
other user namespace path `a::b::c` (a flat name; declared here or registered with the engine)
is lenient at every depth.

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
declared type. A binder property registered with a getter only (a `nullptr` setter, or a
member pointer whose type is not copy-assignable) is READ-ONLY: `h.ro = 5`, `h.ro += 1`,
`h.ro++`, a hybrid method's bare `ro = 5` / `ro++` and `this.ro = 5` are the error
`cannot assign to read-only property 'ro'` (the runtime's `Cannot assign to 'ro': it is
read-only`) - certain only when the property's class is closed (an engine `cpp_class` never
`extend`ed) and nothing takes the store instead: a script `_set_ro` in the modeled chain, a
same-named script field, a registered subclass owning a `_set_ro` (a writable
re-registration, an override, a shadowing field) or a `var` receiver all stay lenient. Static fields are typed like instance fields: `C::s`, a bare `s` inside the class and the
compound forms check the store against the static's declared type (the runtime enforces the same
table on every spelling). Any unresolvable ancestor opens the class: silence.
A method called on a declared container or string (`array<T>`, `string`, `utf8_string`, a `map<K,V>`
whose values cannot be called: V a primitive, a string or a container) must be one the builtin
registry dispatches (`detail/builtin_methods.hpp`, the one list both backends use):
`array<string> a; a.insert(0, "y")` is `no member 'insert' on 'array<string>'`, with the closest
registry name when one is within two edits (`no member 'pusj' on 'array<int>' (did you mean
'push'?)`); the runtime raises family B's `array has no member 'insert'`. A map whose values may be
callable answers other names through its keys (`m.f(x)` calls the value at "f") and stays lenient,
as do `var` receivers and element reads. The builtin methods' arguments and results, subscripts,
enum values (existence IS checked: `Direction.zzz` errors), namespaces: lenient. The handle builtins every object answers
(`use_count`/`cpp_ref_count` → int, `unique`/`same_as` → bool, `reset`) are lenient on any
object receiver; a class method of the same name wins first.

**Enums in a function or block** — an enum declared inside a function or a block is local to
that scope, as at run time: its values resolve there (a program-scope enum of the same name is
hidden, never checked against), and it vanishes with the scope. When the program scope already
names the enum (the unit's own top level, another unit of the program, or the engine) the
declaration leaves that enum unchanged, and the checker says so at warning severity:
`enum 'Color' declared in function 'reload' is local to it: the program-scope 'Color' keeps its
values` (`in a block`, `in a lambda`). A file run by an `include` inside a function or a block
declares its enums there too; that include's report (`last_check_diagnostics()`) carries the same
warning, path-qualified, for each enum the file declares under a program-scope name, and so does the
host's warning channel (`engine::report_script_warning`, stderr unless the host sets a handler),
since a run-time include has no load listing to print.

**Access** — two levels (Dev ruling 2026-09-27): a member after `private:` (or `protected:`,
its synonym) is reachable from the declaring class and every class derived from it, by any
spelling and through any receiver of that family. An access outside the family whose member
the checker resolves - a receiver whose static class is certain (`w.secret` with `Wallet w`,
`Wallet::vault`, `this`/bare names inside an unrelated class's method, a free function, top-level
code) - is the error `Cannot access private member 'secret' of class 'Wallet'`, the runtime's
text, and the member's field proof is withheld. A `var` receiver stays lenient and raises the
same text when it runs.

**Map keys** — the pair variable of a range-for over a statically known map (`map<K,V> m`,
an `auto` map literal; a `var` map is dynamic) carries a read-only `first` - C++
`pair<const K, V>` - by value and by reference alike (`auto& kv` aliases the value AND the key;
the key is read-only). `kv.first` infers as the key type with a `const_key` marker, which a
copy of the pair (`var p = kv`, `auto& p = kv`) and a ternary over keys keep, so two rules
follow the marker rather than the spelling. Mutation is `cannot mutate 'kv.first': map keys
are read-only`: a string mutator (`kv.first.to_upper()`, `insert`, `erase`, ... - the shared
`k_string_mutator_method_names` list), a store or compound (`kv.first = v`, `kv.first += "hey"`,
`kv.first++`). A mutable alias is `cannot alias 'kv.first': map keys are read-only (copy it
instead: var k = kv.first)`: a reference declaration (`var& cheeky = kv.first`, `auto&`, `T&`),
a shared_ptr declaration or store (`shared_ptr<var> cheeky = kv.first`, `shared_ptr<auto>`,
`h = kv.first` into a shared_ptr-declared variable), `cast<T&>(kv.first)`,
`cast<shared_ptr<T>>(kv.first)`, `kv.first` passed where every viable same-script overload
binds the parameter as `T&` or `shared_ptr<T>`, and `return kv.first` from a `-> T&` or
`-> shared_ptr<T>` function. A copy is always fine (`var okay = kv.first; okay.to_upper()`,
`string s = kv.first`, `cast<string>(kv.first)`, a by-value parameter or return) and is the
copy-on-write path at runtime: strings share their node until the first in-place write
(`docs/invariants.md` 11), so what the checker cannot see (a `var` map) mutates a copy and the
map is never touched. `m.keys()[i]` stays lenient (builtin results are untyped here); a pair
that escapes whole into a container or a by-value parameter loses the marker.

**Casts** — `cast<T>(e)` types as certain `T` (a typed local initialized from it proves
`same_type`), `cast<T&>(e)` as `T` for member lookup only (a reference: never a store proof),
`cast<shared_ptr<T>>(e)` as the handle spelling; primitives type as the primitive. A store through
`cast<T&>(e)` (`=`, `op=`, `++`/`--`) checks the stored value against `T` with the assignment table
(`cannot assign 'string' to a cast<int&> target declared 'int'`); a call returning `T&`, a conditional of lvalues and an
assignment in target position check against the referent's type (`... to the reference declared 'int'`). A target that
names no storage is an error with the runtime's text, lowercased (`cannot assign to a literal`, `cannot assign to a call
result: 'f' returns by value` for a call the checker resolves to by-value functions or a constructor). The operand
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

A unit whose top-level `import`/`include` declarations carry literal paths, or which holds a
literal-path `import` anywhere (an import takes no scope: in a function, a block or a lambda it still
declares at program scope, `docs/global_shadowing.md`), is the root of a **program**. Before anything runs, the engine gathers the program's **closure**: every unit
those paths reach, resolved exactly as the runtime resolves them (the importing file's folder, then the include paths, then the working directory),
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

A unit that fails to **parse** stops the program there: its parse error (the unit's path, line
and column, exactly the text executing that file directly gives) is the whole report, and no
unit is checked (the names it would declare are unknown, so any check would only list them as
undeclared). Under strict the root's `execute()` throws that `parse_error` before any unit
runs; under warn - and in `engine->check()` - the report holds the parse error alone, as an
error diagnostic with the text as its message, and the import raises it when it executes. A
fix to the file is seen on the next run. A unit the closure cannot resolve or read contributes
no names and is skipped; the runtime reports it exactly as before, when the unit executes. A **dynamic-path**
`import(expr)`/`include(expr)`, or an include below the top level (a literal include in a function
body is spliced into it instead), can define anything: that unit keeps warning severity for undeclared identifiers. Names the program never declares —
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

At run time an extern declares nothing: the unit uses the visible global instead of declaring
its own (a declaration would shadow it, `docs/global_shadowing.md`), and an extern may run
before the unit or the host that defines the name. A read, store or call of the name while
nothing defines it raises `'G' is declared extern but never defined (an 'inline' declaration
creates it)` (a function extern: `'f' is declared extern but never defined`), on both backends.
A declaration in any unit, a host `add_global`/`add_global_ref` and a host function all count as
the definition. An extern name counts as declared, and a program's closure carries it to every unit like any
top-level name. It is also the spelling for a shared leaf that calls into a module only some
programs import (a netplay hook behind a runtime flag): the leaf declares the names it
reaches for, and in a program that imports the module too the declaration is inert - the
closure knows the real definition, and the call resolves to it at run time. The typed form
gives the name that certain type: member checks, assignment checks and proofs flow exactly
as for a declared variable of that type (a proof stays a runtime-guarded lane, so a host
value that contradicts the declared type runs the ordinary conversion).

`inline` on a variable is the C++17 inline variable: one variable shared by every unit that
declares it. `inline T name;`, `inline name;` (a `var`) and `inline T name = init;` are allowed at
program scope and in a namespace. The first declaration to run creates the variable (the
initializer, else the type's default: `int` 0, `string` "", a class default-constructed, `var`
null); every later one, in any unit or a re-run of the same unit, adopts the existing variable
and runs no initializer, and it adopts a host global (`add_global`, `add_global_ref`) the same way.
Inline and plain declarations of one name never mix across units, whichever runs first: a plain
declaration of a name an inline declaration created raises `'x' is already defined inline; declare it 'inline int x;' to share it` (the type as written;
`var` spells `inline x;`), and an inline declaration of a name another unit's plain declaration
created raises `'x' is already defined (not inline) in <file>; declare every definition 'inline' to share it` (a literal is `<script>`). A unit that switches its own declaration
between the two on a re-run (a hot-reload edit) converts the variable in place: to inline it keeps
its value, to plain it follows the same-unit re-run rule (an initializer re-runs; without one the
value stays unless the declared type changed). The checker reports a mix across the closure once, at
the plain declaration. The checker treats it as a declared global of its type, like any declaration; it is
the spelling for a name a unit needs to exist whether or not the module that owns it runs.

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

Where static information remains incomplete. These paths can defer a type mistake
to runtime; the declarations around them still receive their applicable checks:

- `var` anywhere, and everything computed from it; `auto` without an initializer (locks at
  the first store, flow the checker cannot see); a `null` initializer.
- Stores through references, `auto&` loop variables and by-reference parameters.
  A reference declaration's known initializer type is checked; reference use
  otherwise stays conservative. Handle declarations and known handle stores are checked.
- Elements reached through var or array<var>, inferred literals without a declared
  element type in the checker, and builtin container/string method results.
  Declared container reads, literal elements, push arguments and subscript stores
  are checked. Bounds, missing map keys and computed numeric range failures remain runtime checks.
- Calls through callable variables and function-typed parameters. Storing a known
  non-callable in a function variable or field is a load error.
- Engine globals (typed off the value tag: a later retag is invisible), host-object
  conversions, callables held in variables, host functions beyond arity and primitive
  certainties, binder (C++) methods reached through a script chain (no static signature:
  wrong arity or a wrong argument type raises at the call), binder properties (untyped).
- Dynamic-path `import(expr)`/`include(expr)` units (undeclared identifiers drop to warnings), `extern` names (untyped: `var`; typed: checked), a parent found
  nowhere (its subclass's bare names type as unknown), namespace-qualified lookups
  at every depth (declarations inside a namespace are still checked).
- Accessor-bearing members whose declaration is ambiguous ("Members": two getter/setter
  overloads in the chain, `var`/`auto`/coroutine/parameterized getters, reference or untyped
  setter parameters, an accessor on a registered-only ancestor).
- Read-only binder properties on an open surface: an `extend`ed class, a registered subclass
  owning a `_set_` for the name, a script class from an earlier execute as the receiver's
  static class, a subclass in the SAME unit that overrides `_set_` while the receiver is typed
  as the base (not yet registered when the unit is checked); the runtime raise stands.
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

**Typed references prove their elements.** An element read through a reference declared as a typed container (a parameter
`array<int>& tp` or a local `array<int>& row = rows[i];`) is a proof source for the element kind, as the alias of a typed global
is: the typed rows that consume the proof guard the container's kind at run time and fall back to the general instruction.
Diagnostics keep reading such a receiver as tentative.

**Program globals** — a top-level variable exactly one unit of the program declares, once, with a spelled non-reference
type (`array<int> ND_X`, `shared_ptr<Game> G`) is typed by that declaration in every other unit's proof pass, the way a class
from the program table is; the diagnostics read the table for stores only (Identifiers above; a read in a diagnostics walk leaves the name dynamic, and the proofs
come from a proof-only walk after it), so no load-time text changes. A class-typed global is typed once its class is defined,
and a unit whose proofs waited for a class re-stamps after each import completes (`proof_stats::deferred`); a body compiled
before that (a lambda compiles with its unit's top level; a named function compiles at its first call) keeps the proofs it had.
Every such proof stays behind the runtime guards, so a declaration the runtime contradicts costs a guard, never a value.

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
