# Testing the Bytecode VM Backend

JaiScript has two swappable `execution_backend`s: the tree-walking interpreter (default) and the
bytecode VM (`jai::vm`, `include/jaiscript/vm/`). Both must pass the FULL Foundry suite — the VM
is held to exact parity: semantics, error-message text, stack-trace frames/lines, budget timing,
coroutine drive order, hot-reload behavior.

## Running the suite on a backend

```
jaiscript_tests.exe                    # interpreter (default)
jaiscript_tests.exe --backend=vm       # entire suite on the bytecode VM
jaiscript_tests.exe --backend=vm "Coroutine"   # combine with filters as usual
```

The banner prints the active backend. Tests create engines through `jai::foundry::make_engine()`
(honors the flag); never call `engine::make()` directly in a test. VM-specific suites live in
`source/tests/vm/` and select the backend explicitly (`set_backend(backend_type::vm)`), so they
run the VM even in a default run — including interpreter-vs-VM differential tests.

## Selecting a backend programmatically

```cpp
auto eng = jai::engine::make();
eng->set_backend(jai::backend_type::vm);   // BEFORE the first execute; forbidden after
```

`auto_select` is a legacy alias for `interpreter` (implicit length-based switching was removed).
Injected custom backends report `backend_type::custom`.

## Debugging tools

- `jai::vm::disassembler` renders a compiled chunk (op, operands, resolved symbols/constants,
  statement lines). The `VM Backend` suite has smoke tests using it.
- Chunk encoding invariant: instruction operands reference chunk-local tables (symbols, constants,
  call sites) only — never raw interned ids. Interned ids are engine-local and non-deterministic;
  this invariant is what keeps a future on-disk bytecode cache (.jaibites) possible.

## Structural invariants (enforced by tests)

- run()'s dispatch frame stays flat: extended opcodes go through `exec_extended`; the 10000-deep
  `recursion_depth_error` test guards native-stack headroom in Debug.
- Both backends share ONE builtin-method registry (`detail/builtin_methods.hpp`) and ONE overload
  resolver (`core/overload_resolution.hpp`) — semantics and error text cannot drift.
- Coroutines: interpreter uses AST continuation replay; the VM suspends fibers (saved frame + ip +
  stack slices) behind the opaque `coroutine_backend_state` on the handle.
