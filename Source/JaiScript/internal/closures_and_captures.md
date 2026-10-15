# Closures and captures: the two-tier model

**Pinned by tests on both backends** (`source/tests/semantics/static_check_tests.cpp` `lambda_capture_lists_are_exhaustive`, `source/tests/properties/container_property_tests.cpp` "Lambda Capture Probes", `source/tests/language/function_tests.cpp`, `source/tests/language/test_lambda_captures.cpp`, `source/tests/vm/vm_backend_tests.cpp` ref-escape tests).

Every free name a lambda body references is classified into exactly one of two tiers, and
the tiers behave completely differently. Knowing which tier a name is in answers every
capture question.

## Tier 1 — enclosing-function locals and parameters (frame slots)

Names that live in a live call frame (declared with `var`/typed decls *inside a function*,
or parameters — the parser assigns these `slot_index` storage) are **captured at lambda
creation**:

- **Bare `[]`: capture nothing.** It may use its own parameters/locals and live program-scope
  names. Enclosing locals and instance state require an explicit capture choice.
- **`[=]` / `[x]`: snapshot by value.** Later assignments to the original are invisible.
- A named list is exhaustive: `[x]` does not also capture `y`. Every intervening lambda
  must admit a name needed by a nested closure, using a default or a named capture.
- **`[&]` / `[&x]`: cell-share.** The local's storage is boxed into a shared cell; the
  closure and the original alias the same storage, writes are visible both ways, and the
  cell **outlives the frame** — escaping a `[&]` capture is legal and safe (no dangling,
  unlike C++).
- Parameters capture exactly like locals (they are slots 0..N).
- **A cell and the closures holding it can form a cycle** (`var g; g = [&](int n) { ... g(n - 1) ... };`:
  the cell holds the closure, the closure's captures hold the cell). When a frame whose locals a lambda
  captured by reference exits (a function's or a lambda's frame, a coroutine's when it finishes or is
  dropped, and a loop iteration's declaration when the next one replaces it), it checks the cells it
  shared: a cell that only closures reachable from those cells still hold is cleared, so the cycle dies
  there and the destructors of what it held run at that exit. A closure that escaped (returned, stored
  in a global, a field, a container or another frame) keeps the cycle alive and working; such cells are
  checked again as the engine's list of them grows, and at engine teardown. The check is refcount
  arithmetic over the frame's own cells and the closures they reach (`script_value::release_cell_cycles`),
  never a heap scan, and a frame whose locals no lambda captures by reference does no work for it (the
  parser marks the function body, `block_stmt::shares_cells`, and the VM's chunk carries it).
- The source of a captured name is resolved in the runtime's lookup order - the
  enclosing frame's locals and parameters, then the class's members (a `[&]` on a bare
  field cell-shares the field), then the environment chain - so a same-named global
  never wins over a local, a parameter or a field (`detail/capture_ladder.hpp`, one
  kernel for both backends). An explicit `[x]` / `[&x]` names a variable only: a member
  is not capturable by name (C++: ill-formed) and raises `Cannot capture undefined
  variable 'x'` whether or not a global shares the name - use `[this]` or `[=]`/`[&]`.
- Nested lambdas: an inner lambda referencing an *outer lambda's* param/local captures it
  by the same rules at inner-creation time (the outer frame is live while the inner lambda
  is being created).

`[this]` captures the instance itself (C++ captures the `this` pointer): the lambda reads and
writes the object it was made in, never a copy.

By-value capture uses `clone_for_capture`: pure script values (ints, strings, arrays,
script objects) are deep-copied; `shared_ptr` values and C++-bound objects are **shared,
not copied** — "by value" capture of an engine object pins *that exact object*, which is
what UI callbacks want.

## Tier 2 — globals and module-scope declarations

Names in the global environment are **never captured** — a closure's environment parents
the global environment directly, so these resolve **live, at call time**:

- `engine::add_global` rebinding is visible to closures created earlier.
- A **module-scope `var x = ...`** (top level of an executed file) *is* a global: it lives
  in the shared global environment and persists across `execute()` calls on the same engine.
  The same file (or the same literal string) declaring `var x` again **updates the same
  storage in place**; a *different* file declaring `var x` gets its own binding, and the
  first file's functions and closures keep theirs (`internal/global_shadowing.md`).
- Exception: `execute()` called with `instance_variables` puts top-level `var`s in a
  temporary overlay that evaporates (see `internal/LOCAL_VARIABLES_BEHAVIOR.md`).

This holds for `[=]`, `[&]` and capture-free `[]`: all take no
global, so a global named in the body is read, and written, when the lambda runs. `[&g]` is the
same as not listing `g`.

To pin a global's current value into a closure, capture it explicitly — `[self](){ ... }` copies
the global at creation (legal here; ill-formed in C++) — or copy it through an init-capture.

## Init-captures

`[x = e]` evaluates `e` when the lambda is created, in the enclosing scope, and copies the value
into the lambda's own `x` (value semantics: an array or a class instance is copied, a
`shared_ptr` handle shares its object). `e` is any expression; `x` names nothing outside, and an
outer variable of the same name is not captured. Init-captures combine with defaults and other
entries (`[=, w = 5]`, `[z = x + y, &y]`). A reference capture takes no initializer: `[&r = x]` is
the parse error `A reference capture takes no initializer`.

## Comparison with other languages

| Case | C++ | JaiScript | Lua / Python | Obj-C blocks |
|---|---|---|---|---|
| Bare `[]` referencing a local | compile error | load error | by-reference (upvalue/cell) | by-value snapshot |
| By-reference capture escaping the frame | dangling, UB | cell-boxed, safe | safe (cells) | `__block`, safe |
| Explicit capture of a global | ill-formed | allowed (snapshots) | n/a | n/a |
| Global referenced in body (`[=]`, `[&]`, `[]`) | live at call time | live at call time | live at call time | live at call time |
| Init-capture `[x = e]` | copy of `e` | copy of `e` | n/a | n/a |

`[]` agrees with C++'s no-capture intent. Escaped reference captures retain shared cells,
and explicit captures of program-scope names are permitted. To snapshot a loop variable
on each iteration, use `[=]` or name the variable.

## The embedding footgun (hosts, read this)

Re-executing script files on a **shared engine** has no C++ analogue, and Tier 2 is where
it bites: if a host injects `add_global("ctx", A)`, runs file 1, then injects `ctx = B` and
runs file 2, any *deferred* closure from file 1 that reads `ctx` now sees **B**: an injected
global is one shared storage the host rewrites. A module-scope `var self = ctx;` is safe
against a *different* file's declaration of `self` (that file gets its own binding), but the
same file executed again for another context rewrites it in place.

**The rule for deferred closures (signal receivers, stored hooks): reference only Tier 1
names.** Receive the context as a hook *parameter* and let closures capture that; never
read injected globals or module-scope vars from inside a closure that outlives the file's
execution. (Pinned by `module_scope_var_is_shared_across_executes_of_the_same_unit` and
`function_local_capture_survives_and_pins`.)

## Diagnostics

An unlisted enclosing frame local is a parse error, independent of check mode. The checker
diagnoses unlisted enclosing names and instance members; closure construction also guards
environment/member sources when checking is disabled or an AST is loaded. `this` and
`super` require `[this]` or a default that admits instance capture. An anonymous
`function(args) { ... }` expression is the JavaScript form: it closes over its scope by
reference, exactly `[&]` (shared live cells that outlive the scope, `this` included), so
`function(x) { return function(y) { return x + y; }; }` works and sees later writes to `x`.

Namespaces are program scopes: a lambda made there continues to read their live variables.
Named captures and init-captures still take the requested creation-time snapshot.
