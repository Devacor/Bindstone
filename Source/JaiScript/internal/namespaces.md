# Namespaces

A namespace is a named scope for functions, variables and classes. Its name is flat: `namespace a::b { }`
declares the one namespace `a::b`, and `namespace a { namespace b { } }` declares the same one. A member is
reached by its qualified name, `a::b::x`, from anywhere. A namespace can be reopened: a later
`namespace a::b { }` adds to the same scope.

```jaiscript
namespace geometry {
    var scale = 2;
    int calls = 0;
    function area(w, h) { calls += 1; return w * h * scale; }
    class Rect { int w = 1; int h = 1; }
}
geometry::area(3, 4);        // 24
geometry::scale = 10;        // the variable itself
geometry::area(3, 4);        // 120
geometry::calls;             // 2
```

## Variables

A namespace variable is a variable of the namespace's scope: one cell, declared with the same spellings a
program-scope variable takes (`var`, `auto`, a type, `shared_ptr<T>`, `weak_ptr<T>`, a reference). Its
initializer runs in the namespace's scope, so it sees the members declared before it (`var y = x + 1;`
inside the namespace reads the namespace's `x`).

- `ns::x` reads the cell. `ns::x = v`, `ns::x += v`, `++ns::x` and `ns::x++` write it under the declared
  type's assignment table, exactly as a program-scope variable of that type takes a store (`int y` rejects a
  string with the text a global `int` gives, `Cannot assign string to 'y' (declared int)`; a `var` stays
  dynamic; a reference-declared variable stores through to what it refers to). The value of the assignment
  expression is the stored value.
- A namespace function reads and writes the namespace's variables by their bare names: its free names
  resolve in the namespace's scope first, then in the program scope. `function set(v) { x = v; }` inside
  `ns` writes `ns::x`, and every later read, qualified or bare, sees it. A lambda inside such a function
  captures the variable like any variable of an enclosing scope (`[&]` shares the cell).
- Reopening the namespace and declaring a name again writes the same cell in place; functions that read it
  see the new value.
- A namespace's scope is one scope for every unit (`internal/global_shadowing.md`): a unit's own `var x` at
  program scope never touches `ns::x`, and `ns::x = v` from any unit writes the namespace's cell.
- The qualified spelling is not an lvalue to a reference: `var& r = ns::x;` and passing `ns::x` to a `var&`
  parameter raise the non-lvalue texts (`Cannot take reference of non-lvalue expression ...`, `Cannot pass
  non-lvalue to reference parameter`). Bind the reference inside the namespace's own function, where `x` is
  a plain variable.
- The strict checker treats `ns::x` as a dynamic read and store (a namespace path is not a class); typed
  namespace variables are enforced at run time by the assignment table.

## Functions

Namespace functions are overload sets by arity. A later declaration with the same name and arity needs
`override`. A namespace function is called as `ns::f(...)`; inside the namespace a sibling function is also
reached by its qualified name (`ns::b()` from within `a`), since the bare name of a function is not a
variable of the scope. A namespace function's environment is the namespace's scope over the program scope:
it does not see the caller's locals or `this`.

## Classes

A class declared inside a namespace is defined the way a program-scope class is and is recorded as the
namespace's member: `ns::Rect()` constructs it, and `ns::Rect::method()` reaches its statics. The bare
class name is defined too, so `Rect()` also constructs it; a namespace does not hide its classes.

## Host-registered entries

The host can place values in a namespace directly (`script_namespace_data::variables`), the way the engine
registers `string::to_lower`, `jai::filesystem` and `reflect::is_enabled`. Such an entry is read through
`ns::name` (inside the namespace's functions too: the strict checker does not know the entry's bare name),
and it is read-only from script: a store raises `Cannot assign to 'name': it is read-only`. A script
declaration and a host entry never share a name.

A host function entry may declare its signature in `script_namespace_data::signatures` (`host_entry_signature`: arity range, argument
kinds, the result and an object result's class): the strict checker then checks `ns::f(...)` calls against it and types the result, the way
the stdlib's `string::` and `time::` entries are checked. A host class registered under a qualified name (`dynamic_binder<T>(eng,
"time::date")`) is a type by that name: `time::date d;`, a `time::date` parameter or field, `array<time::date>`
and `cast<time::date>(x)` all spell it, and the namespace entry of the same bare name (`time::date()`) holds its
constructor.

## Using declarations

`using ns::name;` (or a comma list, `using math::vec3, math::dot;`) at the start of a statement makes `name` stand
for `ns::name` in the rest of the source text of the unit it appears in: an executed string, an imported module or an
included file's own text. It is a parse-time rewrite of that unit's tokens, so the checker, both
backends and `.jaibite` see only the qualified name and nothing runs for it. It is not transitive: a file that includes or
imports this one does not inherit its using declarations, and a file this one includes does not see them. Any
namespace works, a nested path too (`using a::b::c;`); `using namespace` does not exist.

The name is read as the qualified one wherever it stands - a type, a call, a value, a template string's splice - except
after `.`, `?.`, `::` or a member `->` (a member name). It cannot be declared again in that unit: a variable,
parameter, member, function, class or enum of that name, and a second using of the same name, are parse errors.

## Reserved names

`reflect` and `jai` (and every `reflect::...` / `jai::...`) are the engine's: a script cannot declare or
reopen them (`Namespace 'jai::x' is reserved for the engine (C++-registered only)`). `math` is open.

## Reflection

`reflect::functions()` lists namespace functions and the function-valued namespace variables as
`ns::name`; `reflect::call("ns::f", ...)` and the other name-taking queries resolve a namespace function or a
function held by a namespace variable the way direct call syntax does.
