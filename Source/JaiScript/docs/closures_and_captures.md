# Closures and captures: the two-tier model

**Status: IMPLEMENTED — semantics pinned by tests on both backends** (`source/tests/properties/container_property_tests.cpp` "Lambda Capture Probes", `source/tests/language/function_tests.cpp`, `source/tests/language/test_lambda_captures.cpp`, `source/tests/vm/vm_backend_tests.cpp` ref-escape tests).

Every free name a lambda body references is classified into exactly one of two tiers, and
the tiers behave completely differently. Knowing which tier a name is in answers every
capture question.

## Tier 1 — enclosing-function locals and parameters (frame slots)

Names that live in a live call frame (declared with `var`/typed decls *inside a function*,
or parameters — the parser assigns these `slot_index` storage) are **captured at lambda
creation**:

- **Bare `[]` (auto-capture): snapshot BY VALUE.** Equivalent to `[=]` for outer locals.
  The closure holds a copy taken at creation; later assignments to the original are
  invisible to it.
- **`[&]` / `[&x]`: cell-share.** The local's storage is boxed into a shared cell; the
  closure and the original alias the same storage, writes are visible both ways, and the
  cell **outlives the frame** — escaping a `[&]` capture is legal and safe (no dangling,
  unlike C++).
- Parameters capture exactly like locals (they are slots 0..N).
- Nested lambdas: an inner lambda referencing an *outer lambda's* param/local captures it
  by the same rules at inner-creation time (the outer frame is live while the inner lambda
  is being created).

By-value capture uses `clone_for_capture`: pure script values (ints, strings, arrays,
script objects) are deep-copied; `shared_ptr` values and C++-bound objects are **shared,
not copied** — "by value" capture of an engine object pins *that exact object*, which is
what UI callbacks want.

## Tier 2 — globals and module-scope declarations

Names in the global environment are **never captured** — a closure's environment parents
the global environment directly, so these resolve **live, at call time**:

- `engine::add_global` rebinding is visible to closures created earlier.
- A **module-scope `var x = ...`** (top level of an executed file) *is* a global: it lives
  in the shared global environment, persists across `execute()` calls on the same engine,
  and a later file declaring `var x` again **updates the same storage in place**.
- Exception: `execute()` called with `instance_variables` puts top-level `var`s in a
  temporary overlay that evaporates (see `docs/LOCAL_VARIABLES_BEHAVIOR.md`).

To pin a global's current value into a closure, either capture it explicitly —
`[self](){ ... }` snapshots the global at creation (legal here; ill-formed in C++) — or
assign it to a *function-scope* local first and let Tier 1 capture it.

## Comparison with other languages

| Case | C++ | JaiScript | Lua / Python | Obj-C blocks |
|---|---|---|---|---|
| Bare `[]` referencing a local | compile error | by-value snapshot | by-reference (upvalue/cell) | by-value snapshot |
| By-reference capture escaping the frame | dangling, UB | cell-boxed, safe | safe (cells) | `__block`, safe |
| Explicit capture of a global | ill-formed | allowed (snapshots) | n/a | n/a |
| Global referenced in body | live at call time | live at call time | live at call time | live at call time |

C++ intuitions transfer with one exception: `[]` here means `[=]`, not "capture nothing".
Where JaiScript differs from C++ it is strictly safer (`[&]` cannot dangle) or more
permissive (globals may be explicitly captured). The by-value default also sidesteps
Python's loop-variable gotcha: each lambda created in a loop snapshots the loop variable's
current value.

## The embedding footgun (hosts, read this)

Re-executing script files on a **shared engine** has no C++ analogue, and Tier 2 is where
it bites: if a host injects `add_global("ctx", A)`, runs file 1, then injects `ctx = B` and
runs file 2, any *deferred* closure from file 1 that reads `ctx` (or a module-scope
`var self = ctx;`) now sees **B** — module-scope vars are one shared storage that later
files clobber.

**The rule for deferred closures (signal receivers, stored hooks): reference only Tier 1
names.** Receive the context as a hook *parameter* and let closures capture that; never
read injected globals or module-scope vars from inside a closure that outlives the file's
execution. (Pinned by `module_scope_var_is_shared_across_executes` and
`function_local_capture_survives_and_pins`.)

## Diagnostics

Under `check_mode::warn`/`strict`, a bare `[]` lambda whose body reaches an
enclosing-function local warns once: the capture happened implicitly (by value), and the
intent should be spelled — `[=]` for a snapshot, `[&]` to share updates. Explicit capture
lists of any spelling are silent, as are bare lambdas that touch only their own
params/locals or globals. (Snapshot-then-keep-mutating is a legitimate idiom — e.g. each
loop iteration's lambda pinning the current counter — so mutation after a value capture is
deliberately NOT warned.)
