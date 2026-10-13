# const (design)

Status: **implemented**: a narrow `const`, for parameters, variable declarations and expressiveness; the decisions on
the open questions are listed in section 14. Section 9.3 lists what is not enforced yet.

`const` marks a binding that is set once and then only read. The rules are C++'s, cut down to the declaration:

- `const` qualifies a declaration (a variable, a class constant, a parameter, a loop variable), never a type, a method
  or a return.
- A `const` in a class body is a class-level constant shared by every instance (`static const`); there are no
  per-instance const fields.
- A const holding a value is deeply read-only: nothing reached from it by value can be written through it.
- A const holding a handle (`shared_ptr<T>`, `weak_ptr<T>`) can't be rebound, and its pointee stays writable.
- `const` is not part of a signature and never affects overload resolution.
- A scoped const read (`C::K`, `ns::x`, `E::v`) is folded into the VM's code, so it looks up nothing. Hot reload stays
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

- **Script methods.** The receiver-writes verdict (`detail::receiver_write_analysis`) takes `this` and the bare fields
  of the chain as the root. A method writes its receiver when its body does any of these:
  - stores to a field (any spelling, without crossing a handle);
  - calls a method on `this` that writes its receiver (transitively; recursion is assumed read-only until a write is
    found);
  - calls a mutating builtin on a field;
  - passes `this` or a field to a writer or to an unseen callee by reference;
  - binds a mutable alias of `this` or a field;
  - writes a field inside a lambda the body creates.

  Writing statics or globals is not a receiver write. Returning a field by `T&` is not one either: the call result is a
  step of the path (2.2), so `c.at(i) = 5` is an error at the store, while `x = c.at(i)` reads.

  At run time (a `const_runtime` site) the class of the receiver that is actually there decides. The checker reports an error only when it is certain: the
  receiver's class is exact (a by-value const) or every override it can see writes.
- **Builtin methods** on strings, arrays and maps. Mutators are writes, and the registry's reading methods
  (`ref_write_reading_method`) are not. Dot transforms that have a copying namespace twin say so in the error
  (`array::filter(xs, f)` returns a filtered copy). A builtin whose callback takes the element by `T&` (`for_each`,
  `find`, ...) is a reading builtin; a callback that writes through that parameter is not caught yet (9.3).
- **Host methods** run: the binder could tell a `const` member function from its signature, which it does not record
  yet (9.3). A host property's setter is a store, and a store through a const raises.
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
`const` follow the general redefinition rule, with nothing special for `const`:

- Two definitions of one signature in the same unit are a load error: two methods (or two static methods, or two
  constructors) of one class, two namespace functions, or two program-scope functions with the same parameter types:
  `'m' is already defined with parameters (int&) at 4:5` (`'f' is already defined at 4:5` for a program-scope
  function). A declaration marked `override` replaces on purpose. Declarations of one name and different parameters are
  overloads; a coroutine does not overload, so a function and a coroutine of one name warn and the
  later replaces the earlier.
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
100;` and `const static int MAX_HP = 100;` declare the same thing, one value shared by every instance (const
in a class is the simple, static form). There are no per-instance const fields, so no constructor
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

- `reflect::set(x, "f", v)` raises `Cannot assign to 'f': it is read-only` when `f` is a class constant.
- `reflect::fields(x)` rows carry no `const` key yet, and `reflect::set` on a const-rooted value and `reflect::bind` of a
  writing method on a const run (9.3).
- `reflect::get` reads.

### 7.9 Serialization

- `to_json` and the binary archive write a const value like any value; class constants are not instance state and are
  not written.
- `const Hero h = from_json(t);` initializes. Assigning a loaded value to an existing const is an ordinary store, and
  an error.

### 7.10 Debugger, checker tooling and the LSP

The language server reports the section 8 diagnostics through the checker. Hover, a `readonly` semantic-token modifier
and the debugger's refusal of `setVariable` on a const are not done (9.3).

### 7.11 jaibite

Version 41: `variable_decl::is_const`, `destructuring_decl::is_const` and `extern_decl::is_const` are serialized. The
const verdicts are not: every load runs the checker's walk, which stamps them. `parameter::is_const` and
`range_for_stmt::is_const` were serialized before and ignored; now they mean const, so a sibling written before version
41 re-parses.

## 8. Diagnostics

Every run-time error is `type_mismatch`, in the read-only family (docs/diagnostics.md family B, section "Const"). The
checker reports the same fact at load, lowercase, with the declaration as the related location (`(declared at 3:1)`); warn
mode reports it as a warning. A stamped node raises its text when it is reached, before any part of it runs (the right side
of an assignment included), so the error is the same whichever operand would have failed first.

| case | run time | checker |
|---|---|---|
| store, compound store or `++`/`--` of the const itself: a local, a parameter, a global, a namespace const, a class constant | `Cannot assign to 'MAX': it is read-only` (the existing text) | `cannot assign to const 'MAX'` (`'C::K'`, `'cfg::W'` when scoped) |
| store through a const (2.2) | `Cannot assign to 'c.hp': it is read-only ('c' is const)` (the target spelled by `detail/lvalue_shape.hpp`) | `cannot assign to 'c.hp': 'c' is const` |
| method that writes its receiver | `Cannot call 'move' on 'p': 'p' is const and 'move' writes its receiver (line 12)` | the same, lowercase |
| mutating builtin | `Cannot call 'push' on 'xs': 'xs' is const and 'push' changes it` | the same, lowercase |
| a `T&` / `var&` declaration over a const | `Cannot bind 'r' (declared int&) to const 'c'; declare it const int& or auto&` | the same, lowercase |
| a typed reference loop variable over a const's elements | `Cannot bind 'x' (declared int&) to an element of const 'xs'; declare it const int& or auto&` | the same, lowercase |
| a const into a reference parameter its callee writes through | `Cannot pass const 'n' to parameter 'x' of 'inc': 'inc' writes through it (line 12)` | `'inc' writes through parameter 'x' (line 12); 'n' is const` |
| `cast<T&>` of a const | `Cannot cast const 'c' to int&: cast<int>(c) copies it` | the same, lowercase |
| `cast<shared_ptr<T>>` of a const, a const stored into a `shared_ptr<T>` | `Cannot share const 'c' as shared_ptr<Foo>; copy it into a variable first` | the same, lowercase |
| a const returned by reference | `Cannot return const 'MAX' from 'f' by reference (declared -> int&); return it by value` | the same, lowercase |
| two definitions of one signature in one unit (4.3) | none | `'m' is already defined with parameters (int&) at 4:5`; a program-scope function: `'f' is already defined at 4:5` |
| a function another unit defined (4.3) | none | warning: `'f' redefines the function defined in <file>:4` |
| missing initializer (1.2) | parse error: `const 'x' needs an initializer: const int x = value;` | - |
| the host writing a const (6.3) | `engine::add_global` throws `Cannot assign to 'MAX': it is read-only` | - |

The texts are built by `detail/const_rules.hpp` at check time and carried on the node; nothing is interned on the error
path.

## 9. Enforcement

### 9.1 Load time and the stamps

The checker (`type_checker.cpp`, the `const_path` analysis beside the `const_key` marker of map keys) walks every write and
binding site. A const name is a const declaration or parameter, a const loop variable or structured binding, a class
constant, an `extern const`, a reference that deduces const (`auto&` over a const, a range-for `auto&` over a const's
elements), a program-scope const another unit declares (from the program table, or from the engine's marked cell once that
unit ran), and an enum's name (section 11). Each site gets a verdict on its node (`const_site` on the assignment, unary,
call, cast, declaration, return and range-for nodes):

- `const_raises`: every step of the path is typed and none is a handle. Strict reports the error; the node raises.
- `const_runtime`: a step is untyped (a `var`, an untyped field, a method whose writes the checker cannot type). Nothing is
  reported; the run time decides.
- none: no const is reached, or a handle ends it.

The walk runs in every check mode, the stamp pass included, so the verdict is on the node under strict, warn and off alike
and the backends never depend on the diagnostics.

### 9.2 Run time

- **Stamped nodes.** The interpreter tests the node's verdict where it visits the node (`interpreter::const_guard_at`); the
  VM compiles `const_raises` into `op_error` with the text and `const_runtime` into `op_const_guard` ahead of the node
  (`vm_compiler::emit_const_guard`). Code without a const pays nothing: the interpreter tests one byte per assignment,
  unary, call, cast, declaration, return and range-for visit, the VM emits nothing. Grafting and hoisting step aside for a
  guarded call, and the counted-for fast paths for a guarded initializer or update.
- **`const_runtime`** walks the path's values without running code (`detail::const_path_crosses_handle`): the root's
  storage, then each field and each element by a literal or a variable index. A handle on the way lets the write run; none
  raises. A call additionally asks whether the dispatched class's method writes its receiver (`receiver_write_analysis`
  over the engine's class definitions).
- **Program-scope and namespace cells** carry a const mark (`engine::mark_const_cell`, set by both backends' declaration of
  the cell); the checker of a later unit reads it, and `engine::add_global` refuses a marked cell.
- **Class constants** carry a mark on their class (`class_definition::mark_const_static`); `enforce_static_write`, which
  every static store path runs, raises for them.
- **`const T&` parameters** take a temporary on both backends (`ref_parameter_takes_temporary`, and the interpreter's
  binders for lambdas).

### 9.3 Gaps

Known places where a write through a const runs without an error:

- **Host methods.** A call of a host (C++) method on a const value runs whatever the method does: the binder does not yet
  record which methods are `const` member functions.
- **Paths the run time can only walk by running code**: a step through a property getter, a computed index (`c[i + 1].x`)
  or a call result. The checker reports such a path when it can type it; otherwise the write runs.
- **A write checked before its const existed.** A unit checked before the unit that declares the const ran (no literal
  import or include connects them) sees no const; the write is caught when that unit is checked again.
- **Callbacks**: a builtin's callback that writes through its `T&` element parameter (`xs.for_each((int& x) => x = 1)`
  over a const `xs`) writes the element.
- **`reflect::`**: `reflect::fields` rows have no `const` key yet; `reflect::set` raises only for a class constant, and
  `reflect::bind` of a writing method on a const runs.
- **Tooling**: the debugger's `setVariable` does not refuse a const, and the language server has no const hover or
  `readonly` semantic token yet.

## 10. Compile-time folding

### 10.1 What folds

The VM folds scoped const reads: `C::K` of a class constant, `ns::x` of a namespace const, and `E::v` of a program-scope
enum (section 11), when the value is an int, a float, a bool or a char. The first execution of the read resolves it through
the general path, and when the member is a const the instruction becomes `op_const_folded`, a push of a constant the chunk
holds. Later executions read the constant.

Bare-name reads of program-scope consts are not folded: their loads already go through the environment lookup cache, and a
typed block resolves them once at its entry. The interpreter folds nothing and is the oracle.

### 10.2 Invalidation (hot reload)

Each folded site is remembered with the read it replaced. Every declaration that can change a folded const (a class
definition, a namespace body, a program-scope enum) runs the check over the sites:

- the same value: the constant stays;
- another value: the constant is patched in place, so every frame, a running one included, reads the new value at its next
  instruction;
- the member is no longer a const, or no longer foldable: the site is rewritten in place back to its general read.

Nothing waits for a restart, and both backends observe the same value at every instruction. Folding is skipped in a
parallel region, under the debugger and for a chunk whose lifetime the backend cannot hold.

### 10.3 Cost

A folded read costs a constant push. A declaration that may change a const pays one check per folded site. Code that reads
no scoped const is unchanged.

## 11. Enums

An `enum E { a, b }` declaration defines `E` as a `map<string, int>`. C++ enumerators are constants, and so are
JaiScript's:

- The enum's name is a const: `E = 5`, `E["c"] = 2` and `E.erase("a")` are errors (the checker knows the enum by its
  declaration; both backends mark a program-scope enum's cell), and `E.size()`, iteration, `E::v` and `E.v` read.
- A variable that holds an enum value (`int d = Dir::n;`, a field, a parameter) is an ordinary variable.
- `E::v` of a program-scope enum folds like a class constant (section 10): the VM's first execution turns the read into a
  constant, and an enum declaration that runs again patches every folded value or rewrites the read back.
- A cell block reads an enum member it holds (`Fx.fire` as an operand) at its entry. The entry finds the member's map
  entry once per enum map and keeps it while the enum's name holds the same map: the map is const, so the same node
  holds the same entries. This removes the per-entry string hash the copy census measured (3.2M lookups per Gator Quest
  run).
- An enum declared in a function or a block stays local (docs/global_shadowing.md) and is never folded.
- A missing value (`E::zzz`) raises `No value 'zzz' in enum 'E'` as before; the checker reports it.

## 12. What changed where

- **Parser:** `const` at the 1.1 positions, the initializer rule, the foreign words of 1.3 (`detail/foreign_keyword.hpp`).
- **AST:** `variable_decl::is_const`, `destructuring_decl::is_const`, `extern_decl::is_const`, and a `const_site` verdict on
  the assignment, unary, call, cast, declaration, return and range-for nodes; jaibite 41.
- **Checker** (`type_checker.cpp`): `const_path` and the const sites, the receiver-writes verdict, the redefinition rule
  (4.3).
- **`detail/const_rules.hpp`:** the texts, `receiver_write_analysis`, the run-time path walk and `const_guard`.
- **Engine:** the const cell mark, `add_global` refusal.
- **class_definition:** the class-constant mark, `enforce_static_write` refusal, re-initialization on redefinition.
- **Interpreter:** the guard at the stamped nodes, the cell mark, `const T&` temporaries for lambdas.
- **VM:** `op_const_guard`, `op_error` for a certain violation, folding (`op_const_folded`, `vm_backend::fold_site`,
  `revalidate_folds`), the enum entry memo on a cell block's environment operand.
- **Docs:** this note, docs/diagnostics.md ("Const" and the foreign keywords), docs/grammar.md, docs/static_checking.md,
  the guide (chapter 3) and the bridges.

## 13. Tests and gates

- The Foundry suite `Const` (`source/tests/language/const_tests.cpp`) runs on both backends with the interpreter as the
  oracle: declarations and the JavaScript spelling, the initializer rule, deep writes through values and the handle rule,
  `const var` decided at run time, references, casts and returns, parameters, the redefinition rule, range-for and
  structured bindings, captures, class constants, other units and the host, namespaces, folding with hot reload (a
  value change, constness removal, a running frame) and enums.
- The foreign-keyword samples in `Bridge Additions`.
- Gates: the Debug suites on both backends, the Release game hashes (GLOOM 7fecc09815bb64e0 / 3580805725, demoreel
  006d304a70772e5f, crawler 3849293868, Gator Quest 3218840367, JaiDOOM 1746772898), fuzz 3000 with no divergence, and
  `verify_site.py` for the guide. The fuzz generator does not write const declarations yet.

## 14. Decisions

1. `const x = e` (and an untyped `const x` parameter) is `const auto`, with C++'s deep meaning.
2. A `const` in a class body is a class-level constant (`static const`); there are no per-instance const fields and no
   constructor sets a const. `const T&` / `const T` parameters are allowed on constructors like on any function.
3. Enum member values are compile-time constants and fold; variables holding enum values stay ordinary variables.
4. A re-run that switches a name between const and plain applies live: folded code is patched or rewritten to the
   general load in place. Hot reload never requires a restart (standing rule).
5. Declarations that differ only by `const` follow the general redefinition rule: two definitions of one signature in
   the same unit are an error, a definition from another unit is a redefinition with a warning, uniformly for methods,
   constructors and functions. Program-scope functions of one name and different signatures are overloads; an
   `override` declaration replaces on purpose.
6. `const Foo f;` default-constructs a class; every other type needs an initializer.
7. Captures follow C++ exactly: a copy of a const is const, a reference to a const is a const reference, an
   init-capture deduces by `auto`. JaiScript lambdas behave as C++ `mutable` lambdas (an existing difference, kept).
8. `auto&` over a const deduces a const reference.
9. `add_global` / `add_global_ref` over a const throws the read-only text.
