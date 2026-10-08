# const (design)

Status: **design, ruled** (Dev ruling 2026-10-07: a narrow `const`, for parameters, variable declarations and
expressiveness; Dev's decisions on the open questions are folded in and listed in section 14). Implementation in
progress.

`const` marks a binding that is set once and then only read. The rules are C++'s, cut down to the declaration:

- `const` qualifies a declaration (a variable, a class constant, a parameter, a loop variable), never a type, a method
  or a return.
- A `const` in a class body is a class-level constant shared by every instance (`static const`); there are no
  per-instance const fields.
- A const holding a value is deeply read-only: nothing reached from it by value can be written through it.
- A const holding a handle (`shared_ptr<T>`, `weak_ptr<T>`) can't be rebound, and its pointee stays writable.
- `const` is not part of a signature and never affects overload resolution.
- A program-scope or namespace const is folded into compiled code, so its uses look up nothing. Hot reload stays
  correct: a re-run that changes the value reaches every use on both backends.

## 1. Grammar

### 1.1 Accepted positions

| position | spelling | notes |
|---|---|---|
| program scope, namespace body, block, function body | `const T x = e;` `const T x(args);` `const T x{args};` `const auto x = e;` `const var x = e;` `const function f = e;` | every declaration form except the uninitialized one (1.2) |
| reference declaration | `const T& r = e;` `const auto& r = e;` `const var& r = e;` | binds an lvalue without a copy, or a temporary that lives as long as `r` |
| class constant | `const T s = e;` `static const T s = e;` `const static T s = e;` | the three spellings mean the same class-level constant (section 5) |
| `inline` and `extern` | `inline const T x = e;` `extern const T x;` | 6.3 |
| destructuring | `const auto [a, b] = e;` | every name is const |
| parameter (functions, methods, constructors, lambdas, arrow functions with parentheses) | `const T& p` `const T p` `const auto& p` `const var p` | section 4 |
| range-for and parallel_for loop variable | `for (const auto& x : c)` `for (const auto x : c)` `for (const auto& [k, v] : m)` `parallel_for (const auto& x : xs)` | these already parse; today `const` is ignored there, and with this design it takes effect |

`const` comes first and precedes everything except `static`, `inline` and `extern`. It appears at most once
(`const const int x` is a parse error). The lexer already reserves `const` as a keyword, so no script uses it as a
name.

**JavaScript spelling:** `const x = e;` reads as `const auto x = e;`, and an untyped
`const x` parameter reads as `const auto x`. This follows `let` (a `var` synonym), with C++'s deep meaning: once
`const xs = [1, 2];` is declared, `xs.push(3)` is an error, and the error says why (section 8).

### 1.2 Initializer

A const is initialized where it is declared. `const T x;` is a parse error for a primitive, string, container,
handle, `var` or `auto`: `const 'x' needs an initializer: const int x = 0;`. For a class type it default-constructs, as
`T x;` does (C++ allows this for a class with a default constructor). A class constant always needs its initializer.

### 1.3 Positions that stay foreign

`const` written anywhere else is a parse error that names what JaiScript supports
(`detail/foreign_keyword.hpp`, docs/diagnostics.md "Foreign keywords"). Each text is recognized only where `const` (or the
word) would otherwise fail to parse:

| written | text |
|---|---|
| `int size() const { ... }` | `JaiScript has no const methods: a method that writes no field already works on a const object` |
| `const int f()`, `const T& f()`, `f() -> const T` | `JaiScript has no const return types: return by value (the caller gets a copy) or by T&` |
| `array<const int>`, `map<string, const T>` | `const qualifies a declaration, not a type: write const array<int> xs` |
| `shared_ptr<const T>`, `weak_ptr<const T>` | `JaiScript has no pointer to const: const shared_ptr<T> keeps the handle from rebinding, and the object stays writable` |
| `const_cast<T&>(x)` | `JaiScript has no 'const_cast': a const stays const; copy it (auto x = c) for a writable value` |
| `constexpr int N = 4;` | `JaiScript has no 'constexpr': a const whose initializer is a constant expression is folded at compile time: const int N = 4;` |
| `mutable int cache;` (a field) | `JaiScript has no 'mutable' fields: every field of a const object is read-only; keep state that must change behind a shared_ptr field` |
| `catch (const e)` | the existing typed-catch text |

The current `const_declaration` and `const_parameter` texts are retired. The new words (`const_method`, `const_return`,
`const_type_argument`, `pointer_to_const`, `const_cast`, `constexpr`, `mutable_field`) join the `foreign_word` enum.

## 2. What a const is

### 2.1 A property of the name

Constness belongs to the binding, not to the value. Copying a const gives an ordinary value (`auto y = c;`,
`var y = c;` and `int y = c;` are all writable), as C++'s `auto` drops top-level const. A value never carries a const
bit. When it is copied into an array, passed by value, returned or captured by copy, the copy is just a value
(captures are the one exception, 7.5).

### 2.2 Deep through values, stopping at handles

A write through a const is any write whose path from the const name crosses no handle. A path is a chain of steps:

- a member (`.f`), a subscript (`[i]`, builtin or a user `operator[]`), or a call of a method returning `T&`;
- `cast<T&>(…)`, a conditional's arms, an assignment in target position;
- the element of a range-for or a structured binding over it.

A step whose value is a `shared_ptr` or `weak_ptr` (the `->` of C++) ends the const. Everything past it is the
pointee's, which a const handle does not own.

| declared | rebinding (`c = v`) | writes through it |
|---|---|---|
| `const int/float/bool/char/string/utf8_string` | error | `s[0] = 'x'`, `s += "!"`, `s.to_upper()`: errors |
| `const array<T>`, `const map<K, V>` | error | element and entry stores, `push`, `erase`, dot transforms (`sort`, `filter`, `map`, `append`, ...): errors. An element that is a handle can't be replaced (`a[0] = h2`), but its pointee is writable (`a[0].hp = 5`) |
| `const Foo` (by value) | error | field stores (`f.hp = 5`, `f.inv.items.push(x)`) and a call of a method that writes its receiver (3.3): errors. A `shared_ptr` field can't be rebound, but its pointee is writable |
| `const math::vec2` (and the other math values) | error | component stores, mutating methods: errors |
| `const shared_ptr<Foo>`, `const weak_ptr<Foo>` | error | none: `h.hp = 5` and `h.move()` write the pointee |
| `const function`, `const coroutine_handle` | error | none (calling or resuming is not a write, as C++'s `coroutine_handle::resume` is `const`) |

### 2.3 `const var` and `const auto`

`const auto x = e` takes `e`'s static type, as `auto` does, and follows that type's row. `const var x = e` holds
whatever `e` produced, can't be rebound, and is deep or top-level according to the kind it holds: a handle (a
`shared_ptr`, a `weak_ptr`, a host object held by pointer) is top-level, and every other kind is deep. Both a `var`'s
kind and a `var` path's steps are known only at run time. The checker decides what it can see, and the rest is decided
when the write runs (9.2).

## 3. What counts as a write

One definition serves the checker and both backends. It extends the kernel that already decides inferred-const
reference parameters (`detail/ref_param_writes.hpp`), with a const name as one more kind of root (alongside a `T&`
parameter and a reference alias). The rules below hold for any expression rooted at a const name under 2.2.

### 3.1 Stores

- `=`, every `op=`, prefix and postfix `++`/`--`, applied to the const name itself (a rebind) or to any lvalue rooted
  at it;
- a user `operator=` call (C++: assignment to a const is ill-formed, whatever the operator does).

### 3.2 Mutable aliases

Each of these makes a writable name for storage rooted at a const:

- a `T&` or `var&` reference declaration, parameter binding or range-for variable;
- `cast<T&>(…)` and `cast<shared_ptr<T>>(…)`;
- a store of a const-rooted by-value instance into a `shared_ptr<T>` slot (sharing it);
- passing it to a script function's reference parameter that writes (the existing verdict, followed through calls);
- passing it to a host function's non-const `T&` parameter (`detail::host_writes_back`; a host `const T&` is fine);
- passing it to a `shared_ptr` parameter;
- returning it from a `-> T&` function;
- `reflect::set` / `reflect::bind` on it (7.8).

`auto&` is not on the list. Following C++ deduction, `auto& r = c` makes `r` a const reference (7.2).

### 3.3 Calls on a const receiver

A method call on a const-rooted receiver is an error exactly when the method writes its receiver:

- **Script methods.** The receiver-writes verdict reuses the same kernel with `this` (and the bare fields of the chain)
  as the root. A method writes its receiver when its body does any of these:
  - stores to a field (any spelling, without crossing a handle);
  - calls a method on `this` that writes its receiver (transitively; recursion is assumed read-only until a write is
    found);
  - calls a mutating builtin on a field;
  - passes `this` or a field to a writer or to an unseen callee by reference;
  - binds a mutable alias of `this` or a field;
  - writes a field inside a lambda the body creates.

  Writing statics or globals is not a receiver write. Returning a field by `T&` is not one either: the call result is a
  step of the path (2.2), so `c.at(i) = 5` is an error at the store, while `x = c.at(i)` reads.

  The verdict is cached on the `function_decl` against `engine::definition_epoch()`, the way the parameter verdict is.
  At run time the method actually dispatched decides. The checker reports an error only when it is certain: the
  receiver's class is exact (a by-value const) or every override it can see writes.
- **Builtin methods** on strings, arrays and maps. Mutators are writes, and the registry's reading methods
  (`ref_write_reading_method`) are not. Dot transforms that have a copying namespace twin say so in the error
  (`array::filter(xs, f)` returns a filtered copy). A builtin whose callback takes the element by `T&` (`for_each`,
  `find`, ...) binds a const element: a callback that writes through that parameter is a write. To decide it, lambdas
  get the parameter verdict that declared functions already have.
- **Host methods.** The binder already knows the C++ signature: a `&T::f` whose type is a `const` member function,
  or a bound lambda whose first parameter is `const T&` or `T` by value, does not write. Anything else does. This fact
  comes from the signature, so a host has nothing to tag. A host property's setter is a write.
- Constructors and destructors of a const object run normally.

### 3.4 Not writes

- reading;
- copying (by-value declarations, parameters, returns, `push(c)`, `cast<T>(c)`, `to_json`);
- comparing, including `same_object`;
- calling a function value held in a const;
- a builtin reordering the elements of a non-const array (it moves elements and assigns nothing);
- the runtime's own construction paths (copy construction by the member-copy rule, archive loads, hot-reload
  migration).

## 4. Parameters

### 4.1 `const T&`

This is the explicit form of the inferred-const reference parameter. It binds a variable without a copy, and binds a
literal or temporary to a temporary cell that lives for the call. It does this whatever the body does, lambdas
included (today a lambda's `T&` refuses temporaries). A write through the parameter is a load error under strict and
raises at run time otherwise. Passing a const name to it is fine.

### 4.2 `const T` by value

The caller copies as for `T`, and the callee can't write the parameter (deep, under 2.2). This guarantees the "callee
never mutates" half of the read-only parameter alias proof (docs/parallel_design.md §15). Using it to elide more copies
is a later performance change and is not part of these semantics.

### 4.3 Not part of the signature

`f(const T& x)` and `f(T& x)` have the same signature, as do `f(const T x)` and `f(T x)`. `const` never enters
overload viability or scoring (`core/overload_resolution.hpp` is unchanged), and declarations that differ only by
`const` follow the general redefinition rule (Dev ruling 2026-10-07), with nothing special for `const`:

- Two definitions of one signature in the same unit are a load error: two methods or two constructors of one class
  with the same parameter types, two namespace functions with the same parameter types, two program-scope functions of
  one name (program-scope functions do not overload, so the name is the signature):
  `'m' is already defined with parameters (int&) at 4:5` (`'f' is already defined at 4:5` for a program-scope
  function).
- A definition from another unit (another module, another execute block) is a redefinition, the hot-reload path: it
  replaces the earlier one, and the checker warns when it sees the earlier definition
  (`'f' redefines the function defined in <file>:4`). A same-unit re-run replaces its own definitions and warns about
  nothing.
- An override may add or drop `const` on a parameter. It overrides by arity and types, as in C++.

### 4.4 Plain `T&` stays as it is

A plain `T&` parameter that the body never writes still takes literals and temporaries. A const argument passed to a
plain `T&` is accepted exactly when that verdict says the parameter is never written. Otherwise the call is an error
(section 8).

## 5. Class constants

A `const` declaration in a class body is a class-level constant: `const int MAX_HP = 100;`, `static const int MAX_HP =
100;` and `const static int MAX_HP = 100;` declare the same thing, one value shared by every instance (Dev ruling
2026-10-07: const in a class is the simple, static form). There are no per-instance const fields, so no constructor
ever sets a const, and a class with constants copies and copy-assigns like any other class.

- It needs an initializer (`const 'MAX_HP' needs an initializer: const int MAX_HP = 0;`, a class type included).
- It is initialized when the class is defined, in declaration order with the class's other statics, and nothing
  writes it afterwards: `C::MAX_HP = 5`, a bare `MAX_HP = 5` in a method, `this.MAX_HP = 5`, compound stores and
  `++`/`--` raise the read-only text, and so does `reflect::set`.
- It reads as `C::MAX_HP`, as a bare `MAX_HP` inside the class's methods, and as `obj.MAX_HP` on an instance (the
  spellings a static already reads by).
- A redefinition of the class (hot reload) runs its initializer again: a class constant has no state to keep, unlike a
  mutable static. Folded uses take the new value (section 10).
- Access labels apply as to any static: a `private:` constant is reachable from the class's family only.
- `to_json` and the archives write instance fields only, so class constants are never serialized.

`const T& p` and `const T p` parameters are allowed on constructors exactly as on any function (section 4).

## 6. Program scope, namespaces and units

### 6.1 Re-runs

A const always has an initializer, so a same-unit re-run (a module that re-runs, a host `execute_file` of the same
file, a hook string run again) always runs it again. That is initialization, not a store, so it never raises.
`hot_reload_migrate(previous)` runs after the re-run and can't assign a const (the re-run has already set it). Keep
state that a reload carries over in non-const variables.

### 6.2 Shadowing

Constness belongs to the cell. A unit that declares a name another unit declared gets its own cell under
docs/global_shadowing.md, and each cell keeps its own constness: a plain variable can shadow a const, and a const can
shadow a plain variable. A reference bound to a cell before the redeclaration stays on it, together with that cell's
constness. A re-run that changes its own declaration between const and plain converts the cell in place (the
`inline` switch's precedent), and folded code adjusts (10.3).

### 6.3 inline, extern, the host

- `inline const T x = e;` is one const that every declaring unit shares. As with `inline`, the first to run
  initializes it and later ones adopt it without running their initializers. Every inline declaration of the name
  agrees on `const`. A mismatch raises in either order:
  `'X' is declared inline const in <file>; declare every definition 'inline const' to share it` (and its plain twin).
- `extern const T x;` promises that this unit doesn't write `x`. Its own writes are errors, whether or not the defining
  unit declared `x` const.
- The host reads a const like any global. `engine::add_global` / `add_global_ref` over a const cell throws
  `Cannot assign to 'MAX': it is read-only`. A host-defined constant (`engine::add_constant`) would be a separate,
  later addition.

## 7. Interactions

### 7.1 `cast`

- `cast<T>(c)` copies.
- `cast<T&>(c)` and `cast<shared_ptr<T>>(c)` of a const-rooted operand are errors (no `const_cast`).
- `cast<Foo&>(h.target)` through a const handle is fine, since the path crossed the handle.

### 7.2 References

| binding | over a const-rooted lvalue |
|---|---|
| `const T& r = c`, `const auto& r = c`, `const var& r = c` | fine; `r` is const |
| `auto& r = c` | fine; `r` is a const reference (C++ deduces `const T&`) |
| `T& r = c`, `var& r = c` | error: `Cannot bind 'r' (declared int&) to const 'c'; declare it const int& or auto&` |
| `int& f() { return MAX; }` | error: `Cannot return const 'MAX' from 'f' by reference (declared -> int&); return it by value` |
| `(cond ? x : MAX) = 5` | error whichever arm runs: the conditional is const when either arm is (C++) |

A const reference to non-const storage forbids writes through itself only. The storage stays writable under its own
name, as in C++.

### 7.3 range-for

- `for (const auto& x : c)` binds each element as a const reference, over any container.
- `for (const auto x : c)` takes a const copy per iteration.
- Over a const container, `for (auto& x : c)` binds const references (deduction, 7.2), `for (T& x : c)` /
  `for (var& x : c)` are errors, and `for (auto x : c)` copies writable values.
- Map pairs follow: over a const map, `kv.second` is const as well as `kv.first`. The map-key rules are unchanged:
  keys stay read-only by the static checker alone, with their own texts.

### 7.4 Structured bindings

- `const auto [a, b] = e;` makes each name const.
- `for (const auto& [k, v] : m)` makes both const.
- Over a const map, `for (auto& [k, v] : m)` binds `v` as a const reference (7.3).

### 7.5 Captures

C++'s rules, applied to JaiScript's capture model (docs/closures_and_captures.md):

- `[x]` and `[=]` copy a const local into a const member: the lambda's `x` is const (C++ [expr.prim.lambda.capture]:
  the member has the entity's type, `const T`).
- `[&x]` and `[&]` share the cell, and the lambda's `x` is a const reference.
- An init-capture `[y = c]` declares `y` as `auto y = c` would, which drops the top-level const, so `y` has the
  mutability every JaiScript init-capture has.
- A default capture never captures a program-scope const: the lambda reads it when it runs, and its constness comes
  from the cell. An explicit `[G]` copies it at creation into a const member, by the first rule.

One existing difference from C++ stays as it is: a JaiScript lambda's copy captures and init-captures are writable in
its body (C++ needs `mutable` for that), so JaiScript lambdas behave as C++ `mutable` lambdas. A copy of a const is
const in a `mutable` lambda too, so the rules above are C++'s exactly.

### 7.6 Coroutines

Const locals and parameters live in the coroutine frame across `yield`, and `yield c` copies. A `const T&` parameter
of a coroutine binds a temporary cell owned by the coroutine's frame. The interpreter's continuation replay
re-initializes const declarations as part of the replay, which is not a store.

### 7.7 Parallel bodies

- Program-scope consts read in a body are provisioned like any global read. No unit runs during a region, so their
  value is fixed while it runs, and a folded use is a constant in the body's chunk.
- `parallel_for (const auto& x : xs)` closes the loop's output channel for `x`.
- A write to a const remains an error (the existing walls still apply to everything else).

### 7.8 `reflect::`

- `reflect::fields(x)` rows gain `const` (bool), true for a class constant.
- `reflect::set(x, "f", v)` raises `Cannot assign to 'f': it is read-only` when `f` is a class constant, or when `x` is
  a const-rooted value.
- `reflect::bind(c, "m")` on a const receiver raises when `m` writes its receiver.
- `reflect::get` reads.

### 7.9 Serialization

- `to_json` and the binary archive write a const value like any value; class constants are not instance state and are
  not written.
- `const Hero h = from_json(t);` initializes. Assigning a loaded value to an existing const is an ordinary store, and
  an error.

### 7.10 Debugger, checker tooling and the LSP

- **DAP.** The locals view marks const variables. `setVariable` on a const local, global or class constant is refused
  with the read-only text.
- **Checker observer.** `check_expression_type` gains a const flag for a const-rooted expression.
- **LSP** (`tools/lsp`):
  - hover shows `const int MAX = 10`;
  - semantic tokens add the standard `readonly` modifier to const names;
  - diagnostics carry the section 8 texts.

### 7.11 jaibite

Version 41. New fields:

- `variable_decl::is_const` and `destructuring_decl::is_const`;
- the parser's const stamps (9.2): `identifier_expr::names_const` and the raising node;
- `variable_decl::folded_value` (10.1).

`parameter::is_const` and `range_for_stmt::is_const` are already serialized, but their meaning changes, so a sibling
written before this version re-parses. A const loop that wrote through its variable is an error now.

## 8. Diagnostics

Every runtime error is `type_mismatch`, in the read-only family (docs/diagnostics.md family B). The checker reports the
same fact at load, lowercase, with the declaration as the related location (`(declared at 3:1)`). Warn mode reports it
as a warning, and the run-time text then applies when the write runs.

| case | run time | checker |
|---|---|---|
| store to the const itself (`=`, `op=`, `++`/`--`): local, parameter, global, namespace, class constant | `Cannot assign to 'MAX': it is read-only` (the existing text, verbatim) | `cannot assign to const 'MAX'` |
| store through a const (2.2) | `Cannot assign to 'c.hp': it is read-only ('c' is const)` (the target spelled by `detail/lvalue_shape.hpp`) | `cannot assign to 'c.hp': 'c' is const` |
| method that writes its receiver | `Cannot call 'move' on 'p': 'p' is const and 'move' writes its receiver (line 12)` | `cannot call 'move' on 'p': 'p' is const and 'move' writes its receiver (line 12)` |
| mutating builtin | `Cannot call 'push' on 'xs': 'xs' is const and 'push' changes it` (a dot transform adds `(array::filter(xs, f) returns a copy)`) | same, lowercase |
| mutable reference to a const | `Cannot bind 'r' (declared int&) to const 'c'; declare it const int& or auto&` (`an element of const 'xs'` for a loop variable) | same, lowercase |
| writing `T&` argument | `Cannot pass const 'n' to parameter 'x' of 'inc': 'inc' writes through it (line 12)` | `'inc' writes through parameter 'x' (line 12); 'n' is const` (the existing literal text's shape) |
| host `T&` argument | `Cannot pass const 'n' to parameter 1 of 'swap': it takes int&` | same, lowercase, when the host signature is known |
| `cast<T&>` / `cast<shared_ptr<T>>` | `Cannot cast const 'c' to int&: cast<int>(c) copies it` | same, lowercase |
| sharing a const instance | `Cannot share const 'c' as shared_ptr<Foo>; copy it into a variable first` | same, lowercase |
| reference return | `Cannot return const 'MAX' from 'f' by reference (declared -> int&); return it by value` | same, lowercase |
| two definitions of one signature in one unit (4.3) | none | `'m' is already defined with parameters (int&) at 4:5`, `'f' is already defined at 4:5` |
| a redefinition from another unit (4.3) | none | warning: `'f' redefines the function defined in <file>:4` |
| inline const mismatch (6.3) | `'X' is declared inline const in <file>; declare every definition 'inline const' to share it` | same, at the declaration |
| missing initializer (1.2) | parse error: `const 'x' needs an initializer: const int x = 0;` | - |

Every text except the first is new and goes into docs/diagnostics.md as a "Const" section of family B. The texts
take symbol ids, so they intern nothing on the error path. A parallel worker gets the slot-less form of each text.

## 9. Enforcement

### 9.1 Load time

The checker generalizes its `const_key` marker (the read-only map key of a range-for pair, docs/static_checking.md
"Map keys") into a const marker on every const binding. Each const-rooted expression carries it, and so do copies of a
pair and conditionals over it. The marker reuses that marker's two rule families, mutation and alias, under the
section 3 definition. Map keys keep their own texts. The receiver-writes verdict and the parameter verdict run in the same walk. Under the false-positive contract only certain errors are reported. A `var` path or an
unseen class stays lenient, and the run time decides it.

### 9.2 Run time

Constness is static wherever the parser can see the name, so most enforcement costs nothing at run time:

- **Slot-resident bindings** (function locals, parameters, loop and catch variables, captures, a coroutine's frame).
  The parser resolves the name, so it knows every write site. A certain violation becomes a raising node
  (`const_violation_expr`): it evaluates what C++17 evaluates (the right side, then the target's receiver and index,
  per docs/diagnostics.md "Order of errors in an assignment") and raises. Some sites can only be decided when they run:
  - a `var` path whose handle crossing is unknown;
  - a method call that needs the dispatched method's verdict;
  - a `T&` argument whose callee resolves at the call.

  Those sites carry a flag. The interpreter tests it, and the VM compiles it into a guard instruction emitted only at
  those sites. Code that names no const pays nothing, and no store lane changes.
- **Environment-resident bindings** (program scope, namespaces, top-level blocks, class statics). Code the parser
  can't see reaches them by name: other units, runtime includes, the host, `reflect::`. So the cell carries a const
  mark (a side table on the root environment and on namespace scopes, never a bit in the value). It is checked
  wherever writable access to a cell is handed out:
  - the full store paths (`store_popped_value_full`, the interpreter's assign);
  - the store-cell caches (`env_store_cell` refuses to cache a const cell, so the cached lane never sees one);
  - reference minting and `host_reference_argument`;
  - `environment::assign`, `add_global` and static stores.

  Same-unit uses the parser can resolve are also rewritten as above.
- **Class constants** are statics with the const mark on their static cell: every static store path (`C::s = v`, a bare
  or `this.` store in a method, `reflect::set`) checks the mark where it resolves the static, and no store cache admits
  a marked static.
- **References.** A reference minted at run time over a const cell, a class constant or a const reference is minted in a
  const holder mode. That mode fails the reference fast lanes' admission (as a property reference does), so its stores
  reach the general path, and the general path raises. Minting from a const reference yields a const reference. A
  reference variable whose initializer the parser can't prove writable has its receiver-call sites flagged, as above.
  References the parser knows are const don't need the mode, because their stores are already rewritten.

The VM needs a new instruction (or a flag on an existing one) for a const declaration whose cell must be marked,
alongside `op_decl_keep` / `op_decl_share`. The interpreter marks the cell in `visit_variable_decl`.

## 10. Compile-time folding

### 10.1 What folds

The VM folds a read of a program-scope, namespace or static const whose current value is a scalar (int, float, bool,
char) or a string. A folded read compiles to a constant-table entry instead of an environment lookup or a cache probe.
As a constant, it becomes a literal operand for typed lowering, compare-jumps and cell blocks, and its type is known
to the type proofs.

There are two sources of the value:

- **An initialized const cell, any initializer.** A const's value changes only by re-initialization, which is the
  invalidation point (10.3). So the compiler folds the value the cell holds at the moment it compiles the chunk.
  Chunks compile lazily, at a function's first call, so a function called after the constants module has run folds
  its constants.
- **A constant expression in the code's own unit.** The defining unit's top-level code compiles before its own const
  declarations run. For it, the parser computes `variable_decl::folded_value` when the initializer is a constant
  expression:
  - literals;
  - unary and binary builtin operators over them (the parser's `try_constant_fold` kernel, which already refuses
    overflow and zero division so the runtime raises);
  - the conversion of the declared type (`const float F = 1;` is 1.0);
  - parentheses, and a ternary with a constant condition;
  - earlier same-unit folded consts, and same-unit enum values.

  Top-level code folds such a const only at uses textually after its declaration, which always run after it. A
  function body compiled before the declaration runs doesn't fold it, because the interpreter would find it undefined
  (or holding its previous run's value).

Aggregates (`const array<int> T = [...]`) are const but not folded. The lookup they still pay is the existing one.

### 10.2 Resolution across units

A folded read resolves the name exactly as the read would (docs/global_shadowing.md), at compile time, for the
code's unit:

- its own unit's cell;
- else the newest cell declared no later than the code's compile stamp (for a function, the run that declared it).

Bindings that rule makes stable fold. A name that resolves only through the forward-reference rung (no cell declared
before the code) doesn't fold, because a later declaration can still change what it reads. A bare name inside a method
folds only when the class chain is fully visible and declares no member of that name (locals, then members, then
globals). `::MAX` and `ns::MAX` always fold.

### 10.3 Invalidation (hot reload)

Folding is invisible: a use always sees the value its const's cell holds now. The interpreter doesn't fold, so it is
the oracle, and both backends observe identical values at every instruction.

Each const cell keeps a record: its value, its kind, and its dependents (a chunk and a constant index for each folded
read, and the instructions lowered from it). The re-initialization that a re-run performs is the one event that can
change a const's value, and it compares the new value with the old:

- **Same kind and same value** (the usual module re-run): nothing happens.
- **Same kind, new value:** the engine rewrites each dependent's constant-table entry in place. Every chunk sees the
  new value at its next instruction, including frames that are running and old lambdas from earlier runs. A constant
  derived from the const at compile time (a folded `MAX * 2`) records the expression it came from and is recomputed by
  the same kernel.
- **New kind** (`const auto X = 5` edited to `5.5`): the constants are rewritten, and each typed instruction or
  cell-block entry that assumed the old kind is rewritten in place to the general instruction it was lowered from.
  `chunk::typed_sites` already records those originals for the guard fallback. The chunk is also retired from the
  chunk cache, so the next entry compiles it again (retired chunks stay pinned, the cache's invariant).
- **The const stops being const** (a re-run drops `const`), or its cell stops being the one the code resolves: each
  folded read is rewritten in place to the general load it replaced, and the chunk is retired. The compiler records
  that load beside each folded site, as typed sites record their fallbacks. This requires a folded constant to enter
  a fused or typed instruction only where that instruction's general form has a one-instruction equivalent that reads
  the global. Where it doesn't, the compiler doesn't fold. The edit applies live, like every hot-reload change (Dev
  ruling 2026-10-07: development never needs a restart of JaiScript to resolve a problem).

Re-initialization runs inside a unit's run: an import, an `execute_file`, a hook string. Every live VM frame is then
stopped at a call or an import, so the in-place rewrites complete before any of them resumes. A unit can't run inside a
cell block (closed typed loops call nothing) or inside a parallel region. Shadowing never invalidates anything: code
compiled before a new declaration keeps its binding, which is the existing rule.

### 10.4 Cost

- Folded reads cost nothing.
- A dependent record costs one entry per folded site.
- A re-run whose consts are unchanged pays one comparison per const.
- Patching costs one store per dependent.
- Code that reads no const is unchanged.

## 11. Enums

Today an `enum E { a, b }` declaration defines `E` as a mutable `map<string, int>` variable. `E::v` reads it through
`detail::enum_scoped_value`, which builds a string key and hashes it on every read. The copy census found 3.2M of
these per Gator Quest run, re-hashed on every cell-block entry, because Gator Quest declares its enums in one module
(`gq_enums.jai`) and reads them everywhere. C++ enumerators are constants, and so are JaiScript's (Dev ruling 2026-10-07):

- A variable that holds an enum value (`int d = Dir::n;`, a field, a parameter) is an ordinary variable.
- The enum's name binds a const cell: `E = 5`, `E["c"] = 2` and `E.erase("a")` become errors, and `E.size()`, iteration
  and `E::v` keep working.
- `E::v` and `E.v` fold exactly like a const int (section 10). The folded read's dependency is on `E`'s cell, and a
  re-run that reorders or adds values patches every dependent, so the 3.2M lookups go to zero on the VM.
- An enum declared in a function or a block stays local (docs/global_shadowing.md). Its values are fixed by the text,
  so the parser folds them at the uses it resolves.
- A missing value (`E::zzz`) doesn't fold and raises `No value 'zzz' in enum 'E'` as today. The checker already reports
  it.

Making the enum map const is a behavior change for a script that edits an enum's map. No script in the repository
does.

## 12. What changes where

- **Lexer:** nothing (`const` is already a keyword).
- **Parser:**
  - `const` at the 1.1 positions; `variable_decl::is_const`, `destructuring_decl::is_const`;
  - the initializer rule;
  - the foreign words of 1.3;
  - per-slot constness in the function scope tables;
  - `identifier_expr::names_const`;
  - `const_violation_expr` rewrites and the flagged sites (9.2);
  - `folded_value` (10.1);
  - same-unit enum folding.
- **Checker** (`type_checker.cpp`):
  - the const marker (generalizing `const_key`);
  - the receiver-writes verdict;
  - the redefinition rule (4.3);
  - the inline mismatch;
  - the observer flag.
- **`detail/ref_param_writes.hpp`:** a const name and `this` as roots; the lambda parameter verdict; receiver-writes
  verdicts cached on `function_decl`.
- **Environment:** the cell const mark (root and namespace scopes) and the checks at the writable-access entry points;
  `inline_declare` agreement.
- **class_definition:** the const mark on static cells; store-cache admission; class-constant re-initialization on
  redefinition.
- **value / reference_holder:** the const holder mode, with the fast-lane admission refusing it.
- **dynamic_binder:** const-method facts from member function pointer types and first-parameter types.
- **Interpreter:** marking the cell at the declaration; the raising node; the flagged-site tests. No folding.
- **VM:**
  - the declaration op for const cells;
  - the raising node and guard instructions;
  - compile-time folding;
  - the const records and dependents;
  - in-place patching and rewriting (10.3);
  - adding any new opcode to the env safe-lists and the debugger's locals walk (CLAUDE.md, typed instruction set).
- **stdlib:**
  - `reflect::fields` / `set` / `bind`;
  - the registry's mutator classification for builtin calls on const receivers;
  - the element-callback rule.
- **Engine:** `add_global` / `add_global_ref` refusal; jaibite 41.
- **Debugger:** the locals const marker; `setVariable` refusal.
- **LSP:** hover, the `readonly` semantic modifier.
- **Docs:**
  - this note, which becomes the reference;
  - docs/diagnostics.md (the Const section; the Foreign keywords table);
  - docs/grammar.md;
  - docs/static_checking.md;
  - docs/global_shadowing.md (constness per cell, folding across units);
  - the guide: a `const` section in chapter 3 (value semantics), and a mention in chapter 13 (strict checking).

## 13. Tests and gates

- A dedicated Foundry suite (`Const`) that runs on both backends with the interpreter as the oracle. It covers:
  - each 1.1 position;
  - each 2.2 row;
  - each section 3 write, in its static form (load error) and its dynamic form (`var` paths, other units, `reflect::`,
    warn and off modes raising the run-time text);
  - class constants;
  - section 7's interactions;
  - folding with hot reload: a value change, a kind change and constness removal, each with a live frame in a loop
    around an `import`, plus old lambdas, shadowing units and the forward-reference rung;
  - proofs on and off giving identical results.
- A fuzz generator extension for const declarations and writes; fuzz 3000 with 0 divergences.
- Debug suites on both backends.
- Release game hashes: GLOOM 7fecc09815bb64e0 / 3580805725, demoreel 006d304a70772e5f, crawler 3849293868, Gator Quest
  3218840367, JaiDOOM 1746772898.
- `verify_site.py` for the guide section.

## 14. Decisions (Dev, 2026-10-07)

1. `const x = e` (and an untyped `const x` parameter) is `const auto`, with C++'s deep meaning.
2. A `const` in a class body is a class-level constant (`static const`); there are no per-instance const fields and no
   constructor sets a const. `const T&` / `const T` parameters are allowed on constructors like on any function.
3. Enum member values are compile-time constants and fold; variables holding enum values stay ordinary variables.
4. A re-run that switches a name between const and plain applies live: folded code is patched or rewritten to the
   general load in place. Hot reload never requires a restart (standing rule).
5. Declarations that differ only by `const` follow the general redefinition rule: two definitions in the same unit are
   an error, a definition from another unit is a redefinition with a warning, uniformly for methods, constructors and
   functions.
6. `const Foo f;` default-constructs a class; every other type needs an initializer.
7. Captures follow C++ exactly: a copy of a const is const, a reference to a const is a const reference, an
   init-capture deduces by `auto`. JaiScript lambdas behave as C++ `mutable` lambdas (an existing difference, kept).
8. `auto&` over a const deduces a const reference.
9. `add_global` / `add_global_ref` over a const throws the read-only text.
