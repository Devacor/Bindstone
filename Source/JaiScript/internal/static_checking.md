# Static type checking

Backend-neutral AST checker that runs at parse-cache-entry creation — one hook in
`engine.cpp` covers `execute(string)`, jaibite creation, hot-reload re-parses (new source
= new cache entry) and registration-epoch changes uniformly. Amortized once per unique
source, like parsing. Always on and strict; `[[warn_mode]]` is the one opt-out, per region. Implementation:
`detail/type_checker.hpp` + `source/implementation/type_checker.cpp`.

## Check API

```cpp
check_report r = engine->check(source);        // parse (cached) + check without running; tooling
const check_report& d = engine->last_check_diagnostics();   // last checked execute/bite
```

Every load is checked: any error-severity diagnostic makes `execute()` / `engine->jaibite()` /
`jaibite_load*()` throw `jai::static_check_error` BEFORE anything runs. A strongly-typed →
strongly-typed mismatch (`int x = "s"`, returning a string from a function declared `-> int`,
passing an array to an `int` parameter, an unrelated class into a typed slot) is a load error
when the source type is known. Dynamic or unresolved sources and value-dependent failures still
reach runtime, where `try`/`catch` sees them. Examples include a computed float outside the int
range and a read of a missing map key. A `[[warn_mode]]` region ("Escape hatches") reports
its errors as warnings, so it loads and the runtime's own conversion errors apply there. There is
no engine-wide mode.

`what()` is the full accumulated, location-sorted, deduplicated listing
(cap 100), compiler style:

```
<script>:12:5: error: cannot assign 'string' to 'x' declared 'int' (declared at 8:3)
    x = "hello";
    ^
```

A diagnostic is `check_diagnostic {severity, line, column, message, related_line/column}`.
Every error gates the load; warning severity never gates.

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
| `shared_ptr<T>` | null; an object of `T` or a subclass; a handle whose STATIC pointee is `T` or a subclass (an upcast); `cast<shared_ptr<T>>(h)`; a `var`/unknown source (judged at run time by its object's class) | a handle statically typed `shared_ptr<Base>` (a downcast needs the cast) or an unrelated class = error, at a declaration, `=`, a field, a `shared_ptr<T>` namespace variable, a parameter, a return and a container element |
| `weak_ptr<T>` | null or a compatible shared/weak handle | same pointee or derived-to-base; unknown sources stay lenient |
| `T&` declaration | T, or a derived class for a base-class reference | conversions do not make a mismatched reference binding valid; unknown sources stay lenient |
| `function` variable or field | callable or null | calls through a bare `function` stay unchecked |
| `function<R(A...)>` | null; a callable whose visible declaration fits (count with defaults, each parameter takes the signature's type, a declared result that is R or derives from it; void/var take any); a host function, an overload set or a callable of unknown declaration (judged at the store by `detail::signature_slot_refusal`) | a call through the slot checks count and arguments and has R's type (tentative); a lambda checked against it takes untyped parameters and an undeclared result from it (`open_lambda_context`, kept by the semantic walk, written as parsed by the serializer) |
| `coroutine<Y>` | null; a handle of a coroutine declared exactly Y (any handle for Y var) | next()/result()/range-for/advance typed by Y; a value of known type a yield or return gives is checked against Y |
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
Each site names its slot in the rejection text (`internal/diagnostics.md` family D):
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
methods and coroutines advanced by `next()` alike; a coroutine's frame sits above its caller's) and the
error surfaces in the caller.

**Compound assign**: typed targets follow C++ rules —
`x op= rhs` is `x = T(x op rhs)`, so `int x; x += 2.5` is fine (truncates) and
`x += "s"` is an error (string result into int). A `string` variable or field appends text
only, as the runtime does: `s += 'c'` and `s += "t"` append, `s += n` with a known non-text
operand (an int, a float, a bool, null, a container, an object) is `cannot apply '+=' to
'string' and 'int'` - the promoted type counts, so `s += to_char(a) + to_char(b)` is `string +=
int` (`internal/char_semantics.md`; `"" + to_char(a) + to_char(b)` is the text spelling). An
element target (`names[i] += 5`) concatenates at run time and stays lenient; a registered
global `+` operator makes the rule lenient. `++`/`--` on a string member (the `+= 1` desugar) is
`cannot apply '++' to 'string'`. `var` targets keep dynamic behavior, unchecked.

**Typed fields**: declared field types are real and enforce
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
- callables in a `function<R(A...)>` slot: count, arguments and result by the signature; in a bare `function` or a `var`: unchecked.
- host `std::function<R(A...)>` parameters: the binder records R and A... (`param_type_info::signature`), the checker reads them as `function<R(A...)>`.
- contextual lambda types come from declarations only (`peek_type`, a field, a parameter, a return type): a flow narrowing never types a lambda, since it changes no run-time path. A call through a signature slot is an unresolved callee to narrowing (the slot may hold a host function that writes back), so a name passed to it ends its narrowing.

**Returns** — a returned value must be assignable to the declared return type; a value from a `void` fn is
an error. A primitive, string or reference result holds no null, so a bare `return;` and a path that
reaches the end of the body are errors there (`detail/return_flow.hpp`, a load-time walk: no call or
return pays for it); an end reached only through a `catch` of an exception the walk cannot prove is
raised is a warning. A class, handle or container result returns null on those paths: warning
severity. Lambdas with a declared return type follow the same rule. Coroutines owe no return: calling one
yields a `coroutine<Y>`, and each yield and return of a known type is checked against Y.

**Identifiers** — undeclared = error, checked against: scopes, the current class's members
(incl. inherited) and, in a body that sees `this`, the members of a class derived from it,
every program-scope name of the unit (top level and namespace bodies, any order, so a
function reads a global declared after it; a namespace's names only inside a namespace),
the host's per-call instance variables, the top-level
names of every unit in the program (Programs below — a literal `import`/`include` never
hides a name), `extern` declarations (Escape hatches below — a name defined outside the
program, typed or not), and the engine's full registration surface (globals, functions,
classes, template types, namespaces — visible because checking happens at execute/jaibite
time).
Forward references work from a function body (it runs after the whole file has defined its functions), but top-level
code runs in order: a top-level call to a function the same file declares only further down is an error,
`'later' is called before its declaration (line N): top-level code runs in order, so declare it above the call or make
the call after it` (an earlier unit's or the host's function of that name makes the call fine).
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
families, operand-typed for `abs`/`min`/`max`/`clamp` — uniform int → `int`,
uniform float → `float`, mixed or unknown → unknown). Arity is the
kernel's runtime error, not a static one; each argument's shape (a number, text, an array, a map, a function, a math value)
is checked against the signatures of the name the api catalog generates (`detail/builtin_api_arities.hpp`,
`MATH_FUNCTION_SHAPES`; several signatures of one count union their shapes, a catalog type that names no single shape takes
anything): `argument 1 of 'math::sqrt': cannot pass 'string' where a number is expected`. A builtin method on a known
string, array or map receiver checks the same way from `BUILTIN_ARITIES::shapes` (`argument 1 of 'substr' on 'string': ...`).
A test runs every refused shape under `[[warn_mode]]` and requires the call to raise. A host namespace entry that carries a signature
(`script_namespace_data::signatures`: the arity range, each argument's kind, the result) is
checked against it - the `string::` library declares one per function, so
`string::repeat(to_char(a), 3)` is `argument 1 of 'string::repeat': cannot pass 'char' where a
string is expected`, a wrong count is `'string::pad_left' expects 2 to 3 argument(s), got 1`, and
the result is typed (text of the first argument's kind, `bool` for `is_valid_utf8`); the `time::`
entries declare theirs too (`'time::engine_elapsed' expects 0 argument(s), got 1`, `time::date_from_unix("x")` is `argument 1 of
'time::date_from_unix': cannot pass 'string' where a number is expected`, `time::local_now()` types as `time::date`). The core
`sleep` checks the same way (`argument 1 of 'sleep': cannot pass 'string' where a number is expected`), and its place: an
error in a `parallel_for` body or a lambda handed to `parallel_transform`/`parallel_reduce`, a warning in a destructor or a
`hot_reload_migrate` (a run cannot suspend there).

**Script namespaces** — a namespace's member has two spellings, `ns::f` anywhere (`using ns::f;` is a token rewrite, so it
is the qualified name) and a bare `f` in the namespace's code, and one resolution judges both: a bare name the walk's scopes
do not hold as a variable, and no member of the class being walked takes, is the member of the innermost enclosing namespace
that declares it (`enclosing_namespace_member`, over `namespace_member_declared`: the unit's own members, the program's
names, the engine's), and from there the bare use and the qualified one run the same code (`check_namespace_call`,
`namespace_member_read`, `check_namespace_variable_store`) and report under the qualified name. The lookup happens when the
code runs, so a bare name that something outside the namespace declares too (an enclosing namespace's member, a program-scope
name) is judged as the member only once the member's declaration is sure to have run: it stands above the use in the unit, or
the engine holds it (`namespace_member_settled`); otherwise a function called from top-level code above the declaration, or
before the declaring file's import, reaches the outside name, and the use stays lenient. A namespace's function is
never a variable of its body's scope: the walk still enters it there (`var_entry::namespace_function`, for the redeclaration
and shadow reports), and every lookup sees through the entry, so a function declared above the call checks as one declared
below it or in another block. A call checks against the function's declarations (`namespace_function_overloads`): the unit's
own (a later declaration of a signature replaces the earlier one), the ones other units of the program make (the pooled
table, under the qualified name) and the ones the engine holds from a run, an error only when none takes the call. Its
result is typed (tentative: diagnostics, never a proof) when the one declaration that takes it is the unit's own, or another
unit's that nothing else of its signature can stand in for: no second unit declares that signature, and the engine holds
none of it but that declaration or the same unit's earlier text, which its reload replaces. `[[nodiscard]]`, default
arguments, coroutines and the narrowing a call ends follow from the same declarations. A function read as a value is a
`function`, with its declarations for a signature slot to judge when the unit makes every one a call could reach. A
variable reads and stores by the declared type of the unit's own declaration, else of the one another unit of the program
makes (`ctype::declared_slot`: tentative, so no proof rests on it, since a class slot may hold a subclass, yet its kind is
certain, so what is read through it, a member, a method, an element, a builtin of a container, checks as through a declared
local, and an `auto` variable types by what the walk inferred once it has passed the declaration; a namespace's top-level
store to a member declared further down is left alone, as the read is, since the name still reaches what is outside).
`ns::C()` constructs and `ns::C::member` reads the class's statics by the same rules. The closure's units declare the
instances their text names before any unit is checked (`checked_report`), so a variable or result of a generic's instance
another unit declares has its class in the pool whatever the import order. A class of the
namespace's name keeps the call lenient (its statics answer too), as does a unit with a dynamic-path import for a member it
does not declare, and a member only the host registers. A class is named by the parser, which reads the bare name as the
qualified one (`parser::qualify_namespace_classes`), so the checker never counts a class as such a member, nor a nested
namespace, which only its path names. That pass also reads the first segment of a qualified name from where it stands
(C++ [namespace.qual]): `inner::k` in `outer` is `outer::inner::k` when an enclosing namespace, innermost first, holds a
namespace `inner` (this stream's, the pool's "outer::inner::" path, an earlier run's; the question `namespace_class_known`
answers carries the trailing `::`), so every later stage sees the full path. An extern's own name is left alone: it is
already relative to its namespace. An enum is named by the parser the same way ("Enums in a namespace" below). `::f` is
the program scope's, whatever a local or a namespace holds under the name,
and a call whose bare name a local, a member or a namespace's member takes is never judged as another unit's program-scope
function of that name (`reaches_program_scope`).

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
leave the member unknown - lenient. A bare `x` inside the class keeps the field's
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
as do `var` receivers and element reads. A builtin method of a declared array, map, string or
utf8_string read without a call is the runtime's text (`var f = a.push;`: `'push' is a method of array;
call it with a.push() or wrap the call in a lambda`; a builtin name wins over a map key at run time, so
any declared map is certain). A dot on a class name is the runtime's text too: `C.s` is `'C' is a class;
use C::s`. The builtin methods' arguments and results, subscripts,
enum values (existence IS checked: `Direction.zzz` and `Direction::zzz` error), namespaces: lenient. The handle builtins every object answers
(`use_count`/`cpp_ref_count` → int, `unique` → bool, `reset`) are lenient on any
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

**Enums in a namespace** — `namespace shop { enum Kind { a, b } }` declares the type `shop::Kind`: the declaration's name is
the qualified spelling (`enum_decl::name`, as a namespace class's), so `enums_`, the program's enum pool and every message
hold it under that name, and each rule of a program-scope enum applies unchanged. The parser reads a bare `Kind` in the
namespace's code as `shop::Kind` (the class rewrite, same question to the engine), except inside a function or a block
that declared an enum of that name before it, and except a value's own name in an enum body. A static chain that spells an
enum this unit declares is the enum's name (`namespace_enum`: its value table, a const `map<string,int>`), and one member
further is one of its values, checked for existence. An enum only another unit or an earlier run declares is a known type
whose slots take its values alone; its values are found when the code runs, as a program-scope enum's are. At run time the
value table is the namespace's const member under the bare name (`detail::declare_namespace_enum`), so `shop::Kind` reads it,
no store reaches it, and `shop::Kind::b` reads through it (`detail::namespace_enum_cell`); `::Kind::v` in the namespace's code
names the program scope's enum.

**Functions from an include inside a function body** — a literal include there is spliced into the body, so a
function the file declares is a nested function. When the program scope names it too (the unit's top
level, another unit of the program, or the engine) the checker warns at the enclosing body, the message led by the
included declaration's place: `inc.jai:1:10: function 'helper' from the include in function
'reload' is local to it and hides the program-scope 'helper' there without replacing it; import the file to replace
program-scope functions` (`in a lambda`). A declaration counts as spliced when its file differs from the enclosing body's.

**Access** — two levels: a member after `private:` (or `protected:`,
its synonym) is reachable from the declaring class and every class derived from it, by any
spelling and through any receiver of that family. An access outside the family whose member
the checker resolves - a receiver whose static class is certain (`w.secret` with `Wallet w`,
`Wallet::vault`, `this`/bare names inside an unrelated class's method, a free function, top-level
code) - is the error `Cannot access private member 'secret' of class 'Wallet'`, the runtime's
text. A `var` receiver stays lenient and raises the
same text when it runs.

**Const** (internal/const.md) — a `const` declaration, a `const T&` / `const T` parameter, a const loop variable or structured
binding, a class constant, an `extern const`, and a reference that deduces const (`auto&` over a const, a range-for `auto&`
over a const's elements) are const names. A write is a store, a compound store or `++`/`--` whose path from a const name
crosses no handle (a declared `shared_ptr` / `weak_ptr` step ends the const), a call of a method that certainly writes its
receiver (`detail/const_rules.hpp` `receiver_write_analysis`: a field store, a mutating builtin on a field, a writing call on
the receiver, every override the checker sees), a mutating builtin on a string, array or map, a `T&` / `var&` / `shared_ptr<T>`
binding, `cast<T&>` / `cast<shared_ptr<T>>`, a `-> T&` return, and a const passed to a reference parameter its callee writes
through (a `const T&` parameter takes it). Each is an error with the run time's text, lowercase (internal/diagnostics.md
"Const"); a path through a `var` or an untyped field is no error and the run time decides it. The walk stamps the verdict on
the node in every mode, so the backends raise what the checker would report. The constness of another unit's global comes
from the program table or from the engine's marked cell. Program-scope functions defined twice in one unit, and methods,
constructors or namespace functions defined twice with one parameter list (const ignored), are errors; a function another
unit defined is the warning `'f' redefines the function defined in <file>:4`.

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
(`internal/invariants.md` 11), so what the checker cannot see (a `var` map) mutates a copy and the
map is never touched. `m.keys()[i]` stays lenient (builtin results are untyped here); a pair
that escapes whole into a container or a by-value parameter loses the marker.

**References** — a reference local, parameter or range-for variable (and an `auto` range-for variable over class elements)
keeps its referent's class or typed container in `var_entry::referent`; `referent_receiver` follows it through fields and
elements. Member diagnostics read it exactly as a value receiver's class (no member, method values, arity and arguments, field
and accessor stores, read-only properties), while the receiver keeps the proof pass's view: unknown, so no call
proof, store proof or result type changes, and a reference's own reads and stores stay as before. `var&` and `auto&` over an
unknown value have no referent.

**Comparisons** — `== != < <= > >= <=>` between two non-tentative declared types with no comparison is an error: numbers and
chars are one family, strings and utf8 strings another, every other kind (bool, array, map, function) compares only with
itself, and a class operand passes when its chain declares any comparison operator or reflected form (or the checker cannot
see the chain whole). `null` against int, float, char, bool, string, utf8, array, map or math is an error (those slots raise or convert on a
null store; an array, map or math call result is skipped, since a body that falls off its end returns null); class values, handles,
weak handles and functions hold null. Two class operands, unknown and tentative operands, math kinds, weak and coroutine handles, and a host free
operator function of that name are left to the run time.

**Casts** — `cast<T>(e)` types as certain `T` (a typed local initialized from it proves
`same_type`), `cast<T&>(e)` as `T` for member lookup only (a reference: never a store proof),
`cast<shared_ptr<T>>(e)` as the handle spelling; primitives type as the primitive. `dynamic_cast` is the same node with
`cast_expr::dynamic` set (`dynamic_cast<T&>(e)` the reference form; `dynamic_cast<T>(e)` the handle form, its target the
`shared_ptr<T>` the parser makes of the class, which `detail::cast_dynamic_handle` answers with null where the cast raises), typed
as its cast is; `check_dynamic_cast` alone judges its operand (a declared non-class kind is the load's error, two unrelated
classes a warning). A class held by value that takes a dynamic handle cast directly (a declaration, a store, a field
initializer, a script parameter, a return: `warn_dynamic_handle_copy`, by `ctype::origin`) draws the warning beside
`cast<C>(h)`'s, naming the tested form and the reference form; the copy and its null check are the run time's. A condition's declaration (`if (auto d = e)`, `while (T x = e)`, `if (init; condition)`) is the parser's
lowering to a block (`{ T x = e; if (x) ... }`, `for (;;) { T x = e; if (!x) break; body }`): the checker and both backends see
ordinary declarations. A store through
`cast<T&>(e)` (`=`, `op=`, `++`/`--`) checks the stored value against `T` with the assignment table
(`cannot assign 'string' to a cast<int&> target declared 'int'`); a call returning `T&`, a conditional of lvalues and an
assignment in target position check against the referent's type (`... to the reference declared 'int'`). A target that
names no storage is an error with the runtime's text, lowercased (`cannot assign to a literal`, `cannot assign to a call
result: 'f' returns by value` for a call the checker resolves to by-value functions or a constructor, `cannot assign to a call
result: 'operator[]' returns by value` for a subscript store on a class whose `operator[]`, every override included, returns by
value). The operand
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
declares at program scope, `internal/global_shadowing.md`), is the root of a **program**. Before anything runs, the engine gathers the program's **closure**: every unit
those paths reach, resolved exactly as the runtime resolves them (the importing file's folder, then the include paths, then the working directory),
recursively, each file once (cycles are fine), parsed into the same cache entries the unit's
later `import` executes through. The union of every unit's top-level names — functions,
variables, destructured names, classes, enums and their values, namespaces with every `::`
prefix, namespace members by their qualified names — is handed to the checker as declared names, and the
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
(checked, never a proof source) until its declaring unit has run: the proof pass records the
class as deferred, and once an import completes with it defined the unit re-stamps its proofs
with the registered definition in view (`restamp_deferred`). The definition moves no surface
epoch: every check that read the pools modeled that very declaration, so no verdict
re-derives (`engine::note_class_defined`; a first definition the pools do not model moves it).

**Namespace classes and enums across units** — a parse reads a bare name in a namespace's code as that namespace's class
or enum when the unit's own text, an earlier run or the pooled classes and enums declare one
(`engine::namespace_class_known`, the one answer the parser, the cache and a bite's context ask). A unit is parsed to find
its imports, so before the units that may declare such a class or enum are pooled: once a closure is gathered (and once a
reload's group is pooled), every unit whose answers moved parses again and pools that parse (`reparse_if_moved`), the root
included. The classes and enums a unit declares do not depend on the answers, so one pass settles a program. The language
server keeps its own program tables and hands a program's classes and enums to its engine before it parses or checks one
of its units (`engine_access::pool_program_classes`).

Under strict, an error anywhere in the program fails the root's `execute()` before any unit
runs. `what()` lists the root's diagnostics first, then each erroring unit's own listing,
formatted against that unit's source and named by its resolved path:

```
D:/.../scripts/maps.jai:12:5: error: use of undeclared identifier 'G'
    G.map = m;
    ^
```

In the report `engine->check()` and `last_check_diagnostics()` return, closure diagnostics follow the
root's with line/column 0 and the path-qualified location leading the message (the snippet
belongs to another source, so none is printed).

The union is the engine's **program-name memory**: it lives for the engine's lifetime and
rides every later check, so a leaf re-parsed on its own after a hot edit (a new cache entry
reached through `import`) still knows the names of the program it belongs to. The memory is
a superset by construction — a name it admits is never a false error, only a typo the
checker cannot catch. The class table rides the engine the same way; a class re-declared with
a different shape replaces its entry at the next gather (the last unit gathered wins), and a
unit's own declaration always wins over the table's.

**Functions and globals across units** — the path-keyed tables hold each unit's functions and variables, a namespace's
under their qualified names (`collect_unit_functions`, `collect_unit_globals`: `shop::tax`), which is what a namespace
member's check reads of other units (Script namespaces above). A call of a free function the unit does not declare checks
against the one declaration exactly one other unit of the program makes (`program_function`: its count, its arguments, its result as a
tentative type); a read of a program-scope variable another unit declares once with a spelled scalar, text, container or
math type is that type (tentative), as a store already checks. The engine may hold the name from that same unit's earlier
run (the table's declaration replaces it: a reload's dependents check against the new text); any other holder (another
unit's run, a host registration, a second declaring unit) leaves the call unchecked. Without a table entry, a call of a
script function the engine holds from an earlier run is an error only when no held declaration takes it. A unit with a
dynamic-path import checks none of this. The clean stamp keys on it already: every table and engine lookup notes the name,
and `outside_signature` folds the declaring units' keys (an overload set's members too).

**Reload dependents** (`engine::import_modules`, `check_group`): a module that ran before is
re-checked, before it re-runs, against the program as its new text would leave it. Its new
declarations go into the pools, its own check runs (with its closure), then every **loaded
unit** outside the re-running set is checked again with diagnostics only (`proofs_t::leave`,
parsed bodies in view) and never run again. `loaded_units_` holds the entry each unit last ran
(`run_unit` notes it once per entry): every file unit, and a string unit that declares a
function, class, namespace or variable. A gating error the unit's own last check did not
report (same line, column and text) is the reload's: one `static_check_error`, each unit's
listing against its own source and path. Any failure restores the pools to what the reload
found. A failing re-run is checked once more with every **pending** module's new text pooled
(a loaded module whose stamp moved and whose import has not been reached); a pass applies the
reached modules only, and a pending module re-runs at its own import, its entry left unchecked
so that import checks it again. A rejected reload's record keeps `failed_closure`: the module's
closure, the files its parse read and the failing dependents' files, each stamped at the
failure; a move of any of them releases the hold. The checker's coverage bounds the guarantee:
a receiver typed off an engine global's tag, which is tentative while its class is
mid-redefinition, is not member-checked.

A unit that fails to **parse** stops the program there: its parse error (the unit's path, line
and column, exactly the text executing that file directly gives) is the whole report, and no
unit is checked (the names it would declare are unknown, so any check would only list them as
undeclared). The root's `execute()` throws that `parse_error` before any unit runs; in
`engine->check()` the report holds the parse error alone, as an error diagnostic with the text as
its message. A fix to the file is seen on the next run. A unit the closure cannot resolve or read contributes
no names and is skipped; the runtime reports it when the unit executes. A **dynamic-path**
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
its own (a declaration would shadow it, `internal/global_shadowing.md`), and an extern may run
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
compose and innermost wins; outside every region a unit is strict. An attribute leads one of
three positions:

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
inside a warn region re-arms strict there. The runtime never changes: both backends treat the attribute as nothing, and a warn region's type
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

Two rules judge a program's intent rather than its types, and so reject code that runs clean: a lost write
(below) and a dropped `[[nodiscard]]` result. The gate counts neither: the generator writes copies freely (the fuzz
harness runs its programs in a `[[warn_mode]]` region) and calls no `[[nodiscard]]` callee as a statement.

### Lost writes

`lw_*` in `type_checker.cpp`. Candidates are by-value locals, parameters and range-for variables whose static type is
a value the walk knows: scalars, text, math values, typed containers of them, and script classes held by value whose
whole tree is quiet (no destructor, user accessor, `operator=` or host base, in any field's class or any subclass the
unit, the program tables or the engine knows). A by-value class parameter, an `auto` made from a handle and a class
variable over handle elements may share an object, so they are never candidates. Each function, lambda and the unit's
top level is one region: the walk records every resolved identifier (`lw_ident_`) and every expression's type
(`lw_types_`), and the region's close runs a backward liveness pass over the structured AST (loops to a fixpoint,
`break`/`continue` targets, a `try`'s handler live at every statement of its body, candidates masked outside their
scope). A store is judged only when it cannot raise or run code itself: member paths through quiet values, a final map
insert whose key needs no conversion, a conversion-free right side, `++`/`--`, and the in-place builtins that take no
count or index. A candidate that a reference binds, a cast to a reference names or a name the walk did not resolve
names is never judged; one whose tree holds class instances, or that a lambda names or a call takes as receiver or
argument, is "sensitive": any node that may run code (a call, a construction, an operator on a class, the death of a
local whose type is not plain) reads it, because `reflect::instances` can observe it there. An include or import in
the region gives the region up. The memory cap and the execution budget are not observations.

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
static_store_proof`, `call_expr::static_arg_proofs`, `return_stmt::static_return_proof`), the
one same-script callee a call resolves to (`call_expr::proven_callee_decl`,
`proven_method_decl`), and the static class of a binary's operands
(`binary_expr::static_operand_class`). The VM's compiler reads them: the store proof admits the
typed store lowering, the argument and return proofs and the resolved callee admit the
inliner, the operand class picks the typed row. Every lane they select keeps a TOTAL runtime
guard, and a guard miss runs the existing code byte for byte; the interpreter reads none of
them. There is therefore no program whose value, printed output, error text or `reflect::type_of`
differs between `engine::type_proofs(true)` (the default) and `type_proofs(false)`.

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

**Calls** are proven only against exactly one same-script overload with exact arity — no
defaults in play, not a coroutine — and a reference parameter takes no argument proof.

**Hot reload** — class registration and non-identical redefinition move the checker's
surface epoch, so cached entries re-stamp at their next execute; a proof never produces an
error the unproven path would not.

**Switch** — `engine::type_proofs(bool)`, default on. Off: the pass does not run for fresh
entries, and cached entries/jaibites already carrying proofs are stripped at their next
execute, so an engine toggled off behaves as one that never stamped proofs. `engine::last_proof_stats()`
counts the stamps of the most recent pass (stores/args/returns, and the deferred classes) for tests.
Proofs are never serialized: a loaded jaibite re-derives them like the parser marking passes.

**Bytecode carriers** — store proofs ride `op_store`'s `store_flag_same_type`, which freezes
when the chunk compiles; a store proof that appears on a later re-check benefits only chunks
compiled afterwards. The inliner reads the call and return proofs off the AST when it grafts.

**Not proven** — conversions (int→float widening, bool/string coercions, null→object,
derived→base, ctor conversions), containers and typed maps, compound assignment and the
`++`/`--` desugars, `store_through` references, field stores, static fields, `super.f`, `?.`
receivers, engine globals, hybrid (C++-base) and `extend`ed classes, transparent wrappers,
callable locals, engine functions, `new_expr`, coroutine callees, defaulted parameters and
worker (parallel) frames. All of these run the existing code with proofs on or off.

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
A reload that passes its dependents' check moves the declarations those units' proofs read (a program function's result, the
inliner's decl): every other loaded unit re-stamps once the reload's modules ran (`script_cache_entry::proofs_moved`), so a
body compiled after it meets the proofs a fresh engine stamps; the verdict needs no re-key, since the dependents' check is
the reload's gate (`reload_dependents_listing`).

**Differential gate** — the contract is pinned four ways: `static_check_tests.cpp::
proof_differential_gate` runs 200 generator seeds on {interpreter, vm} × {proofs on, off}
(fresh engines, identical registrations) and requires identical segment results, printed
output, error text and liveness per backend; `strong_types_tests.cpp::proof_*` batteries run
every certain kind through stores/decls/args/returns/field reads+writes on the same four
lanes and pin the values, with `last_proof_stats()` proving the stamps land;
`hot_reload_correctness_tests.cpp::proof_*` store and read typed fields and then retype, drop,
hide, re-parent, hybridize or `extend` the class against a `type_proofs(false)` twin; and
`jaiscript_fuzz --no-proofs` (plus the fuzz smoke suite's no-proofs band) keeps the
interpreter-vs-vm differential green in both proof states.

## Flow narrowing

The language rule is `docs/site/reference/rules.html#flow-narrowing`; this is how the walk keeps it. Code: the "flow
narrowing" block of `type_checker.cpp` (`narrow_*`).

**Facts.** A `narrow_fact` names a variable by `var_entry::narrow_key` (its own declaration: a `variable_decl`, a
`parameter`, a `range_for_stmt`, a `try_stmt`) and the ctype it holds. `narrow_declare` gives a key to locals, parameters,
range-for and catch variables only: program-scope and namespace variables (any function writes them), statics, fields, `this`,
elements and member paths never get one. A lambda's named capture copies the outer entry with `capture_copy` /
`capture_alias`; `narrow_frames_` records each body's boundary (a function and a `parallel_for` body alias their enclosing
frame, a `[=]` lambda copies it, `[&]` and a bare `[]` alias it), and `narrow_aliased` refuses a fact to a name read through an
aliasing boundary.

**Producing.** `narrow_collect(e, sense)` reads a condition: `!` flips the sense, `&&`/`||` take both sides where both had the
value and the facts both sides give otherwise, a condition that is a dynamic handle cast of the name (`dynamic_cast<T>(x)`: true where
the handle is not null) and `reflect::is_instance_of(x, "T")` give a class (the VM compiles that call, its name a literal, to
`op_instance_test`: `detail::instance_of_class` in place while the reflect entry is the library's and a class stands under the
text, the call through its site otherwise), `reflect::type_of(x) == "k"` a
kind (the engine's own `reflect::type_of` only: a local, a script function, a member or a held script declaration of that name
disqualifies it). `narrow_class_fact` judges against the declared type and the narrowing in force: an ancestor or the
same class adds nothing, an unrelated class marks the fact `open` (diagnostics leave a miss to the run time, both for a `var`
and after two unrelated tests), a value class keeps a value type_info, a handle the tested class's `shared_ptr` type_info, a
reference's fact is `referent` (read only by `referent_receiver`, as its declared referent is). A `var`'s fact, class or kind,
is `proof_only`: the diagnostics walk reads the var as before (the false-positive contract: a `var` narrowed to `int` would
make `x == "5"` and `x == null`, false at run time, typed-side load errors) and hands the fact to the observer alone
(`narrow_observed_`); the proof walk (`semantic_walk()`) reads it.

**Consumers.** `walk_if`, `walk_while`, `walk_for`, the ternary and `&&`/`||` push facts around the parts they govern;
`walk_listed` marks a statement of a list so an `if` whose one branch cannot complete (`return_flow`) or a loop with no
`break` pushes the other outcome for the statements after it, popped at the list's end. `infer_identifier` and
`referent_receiver` return the fact's type.

**Ends.** `narrow_end` marks every fact of the variable dead and logs the event in `narrow_ended_` (a condition drops the
facts its own evaluation ended: `narrow_drop_ended`). It runs at a store to the name (`=`, compound, `++`/`--`, a
`cast<T&>` or conditional target), `reset()`, a non-const reference declaration over it, and an argument a reference may bind:
`narrow_binds_reference` says no only for a by-value or `const&` parameter of every overload and override the checker
resolved, a library builtin, or a host function whose writeback mask (`engine::host_function_writeback`) and held
declarations leave the position by value; anything unresolved binds. An end through a frame boundary, and every reference
binding, is an escape (`narrow_escaped_`): the variable never narrows again in that walk, because the writer may run at any
later point. A call or a construction ends every reference's fact (`narrow_end_references`).

**Loops and lambdas run their bodies again.** Before a loop body (and a `for`'s update) or a lambda body is walked for real,
`narrow_walk_ahead` walks it with no facts, no diagnostics, no proofs and no observer, collects what it ends for variables
declared outside it, and applies those ends first: the back edge and every later call of the lambda see them. It runs only
when a fact is live or the body holds a test (`narrow_has_test`), so a program without tests pays nothing.

**What never reads a fact.** An `auto` (and `shared_ptr<auto>`) declaration types from its initializer's declared type
(`narrow_declared_type` re-infers it ahead), so the run time's declared types never change; a copy `auto c = x;` starts with
`x`'s fact instead. Two proofs have a reader with no run-time guard, and a value read through a narrowing stays out of them:
the return proof (`parallel_transform`'s rewrite; the lambda's inferred return kind too) and a range-for's
`element_conversion_free`. `reference_admission_proven` likewise. Every other proof a narrowed read reaches (operand classes,
store and argument proofs, method grafts) selects a lane whose guard is total (`internal/type_proofs_design.md` section 0).

## jaibite

A bite is checked at creation (an error throws there);
`bite.checked_clean()` / `bite.check_diagnostics()` expose the result. `save()` stamps a
checked-clean bit in the header's flags u32 beside the bite's context section (`detail::jaibite_context`): every
name the clean check asked the engine or the program pools about (`check_options::outside`, recorded by the checker's
`outside_engine`) and a key over what each resolves to (`outside_signature`: the program's declaring units by
`unit_key`, the cell's kind and type, registrations, the script class, namespace and namespace member by the unit that
ran them). The stamp also names the checker's revision (flag bits 8..31): `detail::jaibite_checker_revision()`, a hash of
the code of every C++ file under `include/` and `source/` but the tests (a rule that refuses a program can live in any of
them) that `tools/ci/checker_revision.py` generates into `detail/jaibite_checker_revision.hpp`, the `checker-revision` gate
holds to the tree and `jaibite_checker_revision.cpp` alone includes. On load the stamp is trusted ONLY when the revision is this library's, the
registration fingerprint matches the saving engine's AND those names key alike here; otherwise the bite is re-checked at load against THIS engine's surface (an error throws). The
disk cache holds a stamp the same way, judged at the check it stands in for (check_entry, after the program's names
are gathered), and rewrites the cache when a re-check replaced it. The same section carries the parse's namespace class
and enum lookups (`parser::namespace_queries`); a cache whose lookups answer otherwise reparses, and an in-memory parse asks
them again whenever the engine's count of namespace classes and enums or its pooled ones moved (`parse_moved`). A bite is
checked once — registrations after that don't re-trigger; re-create the bite to re-check.
Proofs are independent of that: they stamp at creation/load/execute and
re-stamp whenever the surface epoch or the proof switch moved, while the diagnostics stay
checked-once.

## Cost

One AST walk per unique source per surface epoch and program union, cached beside the parse
(the proof pass rides it). Re-run only when the registration surface epoch moves
(`add_function`/`add_global`/`add_class`/`register_template_type`, a script class's
non-identical redefinition, and a first definition the program pools do not model), or when the
proof switch flips. The program union (`program_union_epoch_`: a pool gained a name, a class
model changed or a unit was pooled with other declarations; putting the pools back after a
failed reload moves it too) re-derives a gather's root and any failing verdict, whose missing
names may be there now; a clean unit a sibling's edit breaks is the reload's dependents check.
A program executed again unchanged is not checked again: its closure is gathered only when a
stamp moved, the surface moved or the program is failing. A unit an import runs re-gathers its
own closure through the texts its load already read and keeps the verdict the root's gather
gave it. A `.jaibite` stamped checked-clean under a matching fingerprint and context skips
the diagnostics walk. Executes with host instance variables check fresh per call (the name
set is per-call; that per-call walk never touches the cached stamps). Walk cost is
parse-order (same tree, no allocation-heavy state).

## Shadowing warnings

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

At run time a bare name in a method resolves locals, then enclosing scopes and captures,
then members, then globals (see the classes guide chapter, "Name resolution in methods").
The checker follows the same order: a name that is both a global and a field takes the field's
declared type inside methods, and `::name` reaches the global (`field_global_collision_follows_the_runtime_order`).
