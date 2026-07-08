# JaiScript Roadmap

Updated 2026-07. Shipped work is listed once with commit refs and then dropped from the live
sections; the old (2025) pillar ordering is dead.

## Shipped (formerly roadmap pillars)

- **Serialization** ✅ — pointer deduplication with cycle handling (`archive_impl.hpp`), binary +
  JSON formats, versioning (see `SERIALIZATION.md`); jaibite binary save/load of parsed scripts
  (`80fd619b`).
- **VM Completion** ✅ — full-parity bytecode VM: exception handling (`94311244`), reference
  semantics via the shared lvalue kernel `detail/ref_lvalue.hpp` (`e43f8a6f`, `b637fa18`).
  Run everything with `--backend=vm`; MV defaults to the VM.
- **shared_ptr<T> auto-unwrap assignment** ✅ — delegates to T's `operator=` per the detection
  rules in `TYPE_SYSTEM_DESIGN.md`.
- **Explicit-truncation strictness** ✅ obviated — runtime keeps the C++-conversion ruling
  (`9e5fea2f`, `5f7482d5`); the strictness surface is opt-in static checking off/warn/strict
  (`2301d5ce`, `static_checking.md`).
- **Test-coverage asks** ✅ exceeded — the differential interpreter-vs-VM fuzzer (`f3cd4206`)
  plus ~1600 Foundry tests green on both backends.

## Live roadmap (in rough order)

1. **CELLS reference-model refactor** — IN FLIGHT (uncommitted on VM-perf): unify the four-mode
   `reference_holder` around cell storage. After it lands: write the replacement
   reference-model doc and re-verify `invariants.md` §3.
2. **`parallel_for` / `parallel_transform`** — designed and ruled (prove-it-or-stay-serial (A), 2026-07-06);
   see `parallel_design.md` + `parallel_prove_or_serial.md`. Step 1 (thread pool,
   `detail/thread_pool.hpp`) has landed.
3. **Flat-stack VM** — move the VM off environment-backed frames toward a flat value stack for
   the remaining interpreter-vs-VM perf gap.
4. **QoL bundle** (Dev rulings):
   - ~~`format()` / template-string hardening~~ DONE 2026-07: `${expr:spec}` format specs
     (std::format subset, lex-time desugar to the engine-core `format_value` builtin) +
     `{:spec}`/`{n:spec}` in `format()`/`print()` — grammar.md "Template Strings"
   - `import` hardening: content-hash-based cache invalidation, cycle detection,
     import-as-namespace
   - "did you mean ...?" suggestions on unknown identifiers/members

## Type system — genuinely open

- **Const declarations** — `const int x = 5;` (today `const` parses only on range-`for`
  bindings).
- **Union types** — `int | null`, `string | int`.
- **Type narrowing** — `if (x is int) { ... }`.
- **Script-side generics** — `function<T> identity(T x) -> T`.
- **`array<auto>` deduced-type push-lock** — homogeneity is validated at declaration, but the
  deduced element type isn't enforced on later `push`/insert.
- **Map key homogeneity** — `auto m = {{"a", 1}, {2, 3}};` (mixed key types) is currently
  allowed; decision needed.
- **Smart-pointer type-parameter validation** — `weak_ptr<Creature> ref = OtherClass();` isn't
  checked at runtime.

## Tooling

The static checker now provides the substrate (`engine::check()`, `check_report`,
per-diagnostic locations — `engine.hpp`):

- Language Server Protocol (autocomplete, go-to-definition, diagnostics)
- Syntax highlighting (VS Code, Sublime)
- Debugger (breakpoints, stepping, variable inspection) — designed in `DEBUGGER_DESIGN.md`
  (DAP over raw sockets, `engine::debugger()` + `debug_connector`, interpreter backend first)

## Documentation needs

- [ ] API reference for stdlib functions
- [ ] C++ integration cookbook with common patterns
- [ ] Reference-model doc (write AFTER cells lands)
- [ ] Performance tuning guide (seed from `thin_value_rebaseline.md` §6)
