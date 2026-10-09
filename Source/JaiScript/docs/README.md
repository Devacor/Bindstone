# JaiScript documentation

**Start here:** [`site/index.html`](site/index.html) — the documentation site. A 14-chapter
language guide, a 14-chapter embedding guide, the examples, and a reference index. Open it in a
browser; every JaiScript sample on it is run on both backends under strict checking.

The [project README](../README.md) has the pitch, the build commands, and a taste of the
language; [why_jaiscript.md](why_jaiscript.md) is the longer version of the pitch — the design
pillars, the receipts, and the tradeoffs. [`../CONTRIBUTING.md`](../CONTRIBUTING.md) has the
development workflow.

This directory holds the specifications behind the site. Where a page and one of these files
disagree, the file wins.

## The contract

| document | what it settles |
|---|---|
| [grammar.md](grammar.md) | The full grammar: every statement form, operator precedence, declaration spellings |
| [static_checking.md](static_checking.md) | The checker: modes, the one conversion table, program closure, escape hatches, proofs, the false-positive guarantee |
| [diagnostics.md](diagnostics.md) | Every runtime error text, byte-identical on both backends |
| [char_semantics.md](char_semantics.md) | `char` promotes to int 0..255; `\xNN` literals; conversion only through `cast` |
| [integer_semantics.md](integer_semantics.md) | int64, checked overflow, shifts that wrap by contract |
| [closures_and_captures.md](closures_and_captures.md) | What a lambda captures, and when globals resolve |
| [reference_model.md](reference_model.md) | What a reference is, and the four owner-pinned modes |
| [global_shadowing.md](global_shadowing.md) | Program-scope declarations across executions: units, shadowing by unit, in-place reloads |
| [namespaces.md](namespaces.md) | Namespaces as scopes: flat names, variables as cells, functions, classes, host-registered entries |
| [typed_array_design.md](typed_array_design.md) | Typed arrays and maps: the declared type *is* the representation, and what that means for conversions and the C++ view |
| [reflection.md](reflection.md) | The `reflect::` API |
| [SERIALIZATION.md](SERIALIZATION.md) | The C++ archive framework (JSON and binary) and script `to_json` / `from_json` |
| [script_extensible.md](script_extensible.md) | `extend(obj, Class)`: script layers on C++ objects |
| [BUILD.md](BUILD.md) | Build requirements, targets, generators, and CMake options |
| [portability.md](portability.md) | The compiler and OS surface |
| [../tools/jaiscript/README.md](../tools/jaiscript/README.md) | The runner: file mode, REPL, `--watch` live coding, `--check` and `--backend` |

## Measured reports

- [PERFORMANCE.md](PERFORMANCE.md) — the head-to-head performance report: ladders, scaling, and
  comparisons against other embedded languages.
- [GLOOM_COMPARISON.md](GLOOM_COMPARISON.md) — one complete game written five times, in
  JaiScript and four other languages, compared on speed, conformance, and how it felt to write.

## Internals

How it is built, rather than how to use it: [invariants.md](invariants.md) is the one to read
before touching value layout, `strong_ptr`, or the VM run loop. Alongside it sit the design
notes behind particular features — [type_proofs_design.md](type_proofs_design.md) for the parse-time proofs
both backends consume, [parallel_design.md](parallel_design.md) and
[parallel_prove_or_serial.md](parallel_prove_or_serial.md) for `parallel_transform`,
[thin_value_spec.md](thin_value_spec.md) for the 32-byte value layout,
[aliasing_costs.md](aliasing_costs.md), [flatstack_design.md](flatstack_design.md),
[DEBUGGER_DESIGN.md](DEBUGGER_DESIGN.md), [EXCEPTION_DESIGN.md](EXCEPTION_DESIGN.md),
[TYPE_SYSTEM_DESIGN.md](TYPE_SYSTEM_DESIGN.md), [STRONG_TYPES.md](STRONG_TYPES.md).

[JaiScriptTesting.md](JaiScriptTesting.md) and [JaiScriptVMTesting.md](JaiScriptVMTesting.md)
are the testing notes for the two backends.

These are useful if you are working *on* JaiScript, and unnecessary if you are working *with*
it.

## History

A few files here are planning documents from before the feature they describe shipped —
[REFERENCE_SEMANTICS_PLAN.md](REFERENCE_SEMANTICS_PLAN.md),
[PERSISTENT_INTERPRETER_DESIGN.md](PERSISTENT_INTERPRETER_DESIGN.md),
[JaiScript_FutureDesign.md](JaiScript_FutureDesign.md), [ROADMAP.md](ROADMAP.md) and their
neighbours. They record how a decision was reached, not what the language does today; when one
of them disagrees with a file in the contract table above, the contract is right.

A few more are working notes rather than documentation — [OPEN_NOTES.md](OPEN_NOTES.md),
[DRAGON_PERF.md](DRAGON_PERF.md), [PERF_PASS_2026-09-02.md](PERF_PASS_2026-09-02.md) and
`arch_audit_2026-07.json`: measurement ledgers and unresolved design tensions kept next to the
code they discuss. Nothing in them is a contract, and nothing on the site depends on them.
