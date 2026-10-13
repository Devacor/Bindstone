# Per-Execution Local Variables (`instance_variables`)

The engine can overlay a set of caller-supplied variables onto a single execution:

```cpp
// types.hpp
using instance_variables = std::unordered_map<std::string, script_value>;

// engine.hpp
script_value execute(const std::string& scriptContent, const instance_variables& instanceVars);
script_value execute_file(const std::string& scriptPath, const instance_variables& instanceVars);
```

## Semantics: a temporary overlay scope

When `instanceVars` is non-empty, `engine::execute_parsed` pushes one scope on the backend,
defines each entry in it, runs the script, and pops the scope. That gives:

```
[overlay scope]   <- the instance_variables, per-execution, discarded afterwards
       ↓
[global scope]    <- persistent, shared across executes
```

- **Overlay reads work.** The script sees the supplied variables by name.
- **Shadowing works.** An overlay variable shadows a same-name global for that execution only.
- **Globals stay writable.** Assigning to an existing global mutates it as usual.
- **Top-level declarations do NOT persist.** With locals provided, a root-level `var x = 10;`
  lands in the overlay scope and evaporates when it pops — there is no copy-back to globals.
  (Without locals, no overlay exists and top-level declarations become globals as usual.)

```cpp
auto eng = jai::engine::make();
eng->execute("var counter = 0;");                       // global (no overlay)

jai::instance_variables locals{{"increment", eng->make_value(5)}};
eng->execute("counter = counter + increment;", locals); // reads overlay, writes global
eng->execute("var temp = 1;", locals);                  // temp dies with the overlay

eng->execute("print(counter);");     // 5
eng->execute("print(increment);");   // ERROR — overlay is gone
eng->execute("print(temp);");        // ERROR — declared into the overlay, not globals
```

## Not dynamic scoping

Functions resolve names through their **captured closure chain**, not the caller's scopes. A
function defined in an earlier execute does NOT see a later caller's `instance_variables` —
its free names resolve against the environment it closed over. Only code parsed in the same
execute (which closes over the overlay scope) can see the overlay.

## The real consumer: signal receivers

Script signal receivers are invoked with their parameters delivered as `instance_variables`
(`detail::build_script_locals` in `signals/signal_impl.hpp` builds the map from the C++
arguments and the receiver's parameter names). This is the pattern the feature exists for:
passing per-invocation parameters without polluting the global namespace.

## Known gap

No Foundry test currently exercises `instance_variables` directly — the only coverage is
indirect, through the signal tests. Treat behavioral changes here as untested until that gap
is closed.
