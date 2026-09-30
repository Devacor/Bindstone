# Simplification audit

This document maps every place where the runtime implements one semantic rule more than once. For each place it names the consolidation that keeps a single general path, the lines that path deletes, the proof the change owes, and the order in which the rows can land. Line anchors refer to commit `de3c271f`. They drift as the tree moves, so re-anchor each row before starting work on it.

Paths are relative to `Source/JaiScript/`. Shorthand used throughout:

| Short | File |
|---|---|
| I | `source/implementation/interpreter.cpp` |
| ID | `source/implementation/interpreter_dispatch.cpp` |
| VB | `source/implementation/vm/vm_backend.cpp` |
| VC | `source/implementation/vm/vm_compiler.cpp` |
| VI | `source/implementation/vm/vm_inline.cpp` |
| VBH | `include/jaiscript/vm/vm_backend.hpp` |
| OP | `include/jaiscript/vm/opcode.hpp` |
| CH | `include/jaiscript/vm/chunk.hpp` |
| CD | `include/jaiscript/core/class_definition.hpp` |
| RL | `include/jaiscript/detail/ref_lvalue.hpp` |
| CE | `include/jaiscript/detail/container_enforce.hpp` |
| TR | `include/jaiscript/detail/transient_read.hpp` |
| PT | `source/implementation/parallel_transform.cpp` |

## The bar

A row qualifies only if it deletes branches or merges paths into one general path, and loses no correctness, performance or diagnostic text in doing so. If the merged path needs a special case to stay fast or to stay correct, the row does not qualify.

Some rows merge copies that behave differently today. Merging them picks one behavior, so those rows wait on a Dev ruling. The ruling fixes the semantics first; the merge then deletes the losing copies.

Two remarks shape the plan:

- **Where the code is.** The code map lives in two files that are under active edit: `vm_compiler.cpp` (6,571 lines) and `vm_backend.cpp` (20,593 lines). The interpreter (15,288 lines), `class_definition.hpp`, the `detail/` kernels, the checker and the archives are free.
- **The reviewer's concern.** `docs/review-2026-09-29/BOUNDARY_FIXES.md` says that too many interacting optimization paths make evaluation order, ownership and exception lifetimes hard to audit. That concern is concrete. All twelve divergences confirmed below sit where one rule has several implementations whose policies differ.

## Proof obligations (gate sets)

Each row names the gate sets it owes.

| Gate | Content |
|---|---|
| **G-suite** | Full Foundry suite, Debug and Release, `--backend=interp` and `--backend=vm`. At de3c271f that is 3,546 tests per backend per configuration. XTHREAD Debug is added when a row touches parallel paths. |
| **G-text** | The error texts do not change. The suite pins most texts; the rest are covered by the `docs/diagnostics.md` catalogue plus the `Spec Conformance` diagnostic group. A row that only moves a text builder must diff every call site's literal. |
| **G-fuzz** | `jaiscript_fuzz` differential: 3,000 seeds default; 1,500 seeds each with `--no-proofs`, `--no-passes`, `--no-param-alias` and `--element-band`; 1,000 seeds with `--order-band --order-args`. In the `x64-Release MISS` build, also `--miss-every=1` and `--miss-every=3`. Allowed: zero new divergences and zero crashes or hangs. |
| **G-manifest** | `jaiscript --manifest=<file>` over the Foundry corpus and the game sources, byte-identical before and after. This is required wherever the row claims that compiled code does not change. `--disasm` listings as well, when mnemonics move. |
| **G-hash** | GLOOM 300 ticks, seed 666, frame and state hashes on both backends with 0 and 8 workers. JaiDOOM timedemo state `1746772898`. crawler, demoreel and OSSUARY scene outputs match the interpreter reference. Gator Quest smoke. |
| **G-perf** | The 1.5% whole-program bar in both layouts (normal and padded) on GLOOM serial, GLOOM 8 workers, JaiDOOM timedemo and OSSUARY. Alternatively, more than 3% on a targeted microbenchmark with no whole-program regression. |

**G-parity-blind.** When both backends share one kernel, differential fuzzing cannot see a wrong result that both produce. Every row that changes a shared kernel adds a spec-derived Foundry test first (`Spec Conformance` or `Spec Attacks`), red before the change where the change fixes something.

## Divergences found during the audit

The audit wrote a repro for each suspected divergence and ran it with the `x64-Release BENCHMARKS` runner, `jaiscript --backend=interp|vm`, built at `de3c271f`. These results decide the rulings the merges need, so they come before the map.

### Confirmed by running

| # | Shape | Interpreter | VM | Spec reading | Where the rule forks |
|---|---|---|---|---|---|
| F1 | `C c; var r = 0; r = (c.n = 3.9);` also `print(to_string(a[0] = 4.9))` | float 3.9 | int 3 | int 3 (the value of an assignment is the stored value, ruling 2026-09-08) | The interpreter's `expression_result_needed_` is one flag per statement (interpreter.hpp:927; cleared at I:6355 and I:12736). A nested store never reads back. |
| F2 | `weak_ptr<Foo> w = sp; auto r = (w = null);` | `Internal error: empty value stack` | null (`(w = sp)` yields the shared handle, typed Foo) | the stored weak handle | The interpreter's env-path handle rows at I:5755-5950 push no value. The VM pushes the right-hand side (VB:3654, 3795). |
| F3 | `a[g()] = b[1]` where `g` writes `b[1]` | 2 | 99 | 2 (the right side's value is taken before the target, invariant 12) | The interpreter dereferences the value at once (I:5570-5582). The VM carries a container+index reference to `op_index_store` (VC:5742-5745, VB:6391). |
| F4 | `[A[0], o + 1]` where `o`'s `operator+` writes `A[0]` | 1 | 100 | 1 (left to right) | `deref_ahead_of` (VC:5261) and the literal loops (VC:2019-2046) trust the *gated* `callout_free`. A class `operator+` does not latch the gate (TR:42). |
| F5 | A method storing `f = i` in a loop, where `f` has class type `V` with `operator=(int)` | `operator=` runs 4 times | `Cannot assign int to field 'f' of H (declared V)` on the second iteration | `operator=` at every plain store (spec CB row) | The VM this-field store IC (VB:3491-3521) calls `field_store_convert` and skips `enforce_field_write`'s `user_assign_operator` row. `this_field_store_arm` (VBH:1586) admits object-typed fields. |
| F6 | `coroutine g() { throw; yield 1; }` resumed twice | second resume: `Coroutine is already running` | `Cannot resume a completed coroutine` | the VM's text; the handle is failed | The interpreter's `resume_coroutine` has no `catch(...)`: status stays running and `active_coroutine_` stays set (I:14775-14815; `prepare_for_execution` keeps it at I:2822). |
| F7 | `sz(G, o + 1)` where `operator+` does `G.push(9)`; `sz(var a, int b)` returns `a.size() + b` | 3 | 3 | 2 (invariant 13: a by-value argument takes its value when evaluated) | The stamp `stamp_arg_captures` (TR:157) uses `callout_free<true>`, which is gated. Both backends share the stamp, so the bug is parity-blind. |
| F8 | `f(Box p) { var q = GQ; q.poke(); return p.n; }` called as `f(G)`, where `poke` writes `G.n` | 5 | 5 (`--no-param-alias`: 1) | 1 (by-value parameter) | `prove_readonly_parameter` (PT:1365-1375) does not walk a method on a non-root local receiver, and never checks user accessors off the parameter chain. |
| F9 | `var n = 7` field, `n /= 2` in a method or as `d.n /= 2`; also an int field `9007199254740993 /= 1` | 3.5; ...992 | 3.5; ...992 | 3 and ...993 (a local gives 3) | The member and implicit-this `/=` rows compute in float (I:5025-5037 and 5237-5253; VB:4415-4423 and 13712-13724). The identifier, element and binary paths divide as integers. |
| F10 | `t.s += 'c'` (member) vs `gs += 'c'` (local); `as[0] += 5` (element) vs `vs += 5` (var local) | member raises, local works; element concatenates, local raises | same | one rule per operator pair | There are 12 compound arithmetic copies across three semantics. The rows are under cluster C. |
| F11 | `auto& e0 = ai[0]; var q = (e0 += 1.5);` over `array<int>` | q = 2.5, stored 2 | same | 2 (the stored value) | `ref_compound_store_constrained` returns the value before conversion (RL:893). |
| F12 | `x %= 0` → `Modulo by zero` with code `division_by_zero`; `x % 0` → `Modulo by zero in integer operation`; `x /= 0` → `Division by zero`; `x %= 0.0` → `Division by zero` | same | same | one text per condition (needs a ruling) | Five spellings (ID binary handlers, `evaluate_arithmetic` I:7119, compound ladders I:4867/5040, VB:4276/4430/6306/10432) |

Divergences between the two backends are F1-F6. F7-F12 are shared by both backends, so parity checks cannot see them. Each of them sits where a rule has several implementations and the copies disagree.

### Suspected, not reproduced or not run

| # | Shape / claim | Status |
|---|---|---|
| S-a | `x + g(t = mutate(x))`: `slot_argument_in` (VC:4434) has no assignment, `new` or literal case, while `names_slot` (VC:5821) has one. | Not reproduced: both backends give 7 in this shape. The textual gap is real. |
| S-b | VM `pending_callees_` survives an uncaught error in the entry frame, so a captured instance's destructor is delayed until engine teardown. `prepare_for_execution` (VB:685-689) clears the other four stacks but not this one. | Not run (needs two executes and a destructor probe) |
| S-c | VM iter, try and pending vectors are destroyed in place while a script destructor can re-enter and grow them (VB:16835, 16429, 19534-19545, 320-328). | Not reproduced in Release; needs a Debug run (0xDD fill) |
| S-d | A raw C++ throw from default-argument binding at the first resume in `run_fiber` leaves `environment_` and `active_coroutine_` unrestored. | Not run |
| S-e | The argument zones catch different exception types: the interpreter catches `runtime_error` (I:7568, 7770), the VM `run()` catches `std::exception`. | Not run |
| S-f | In the implicit-this compound, the VM returns the promoted result before the store (VB:4439-4450); the interpreter returns the stored value (I:5049-5064). | Not run (needs a hybrid C++ property) |
| S-g | An untyped element compound deep-copies a `shared_ptr` result: `.clone()` at VB:6354 and VB:10490, where I:6341 uses `clone_for_assignment`. | Not run |
| S-h | Handle admission: a local `=` compares the static tag (I:5850 / VB:3701); a declaration uses the dynamic class (I:6651). So `shared_ptr<Base>` holding a Derived enters `shared_ptr<Derived>` by declaration but not by `=`. | Not run |
| S-i | A cpp-bound identifier in a container literal: the interpreter detaches it per element (I:9666); the VM detaches it at `op_array` (VB:1624). | Not run (needs a host global) |
| S-j | NaN `<=>` yields 0 while `NaN == x` is false (ID:809, VB:2638). C++ `script_value::operator==` disagrees with `<=>` on -0.0 and NaN (value.cpp:1470-1560), against the CLAUDE.md consistency rule. | Read |
| S-k | The parser's constant fold (parser.cpp:2029-2136) ignores registered global operators. `math::abs(INT64_MIN)` raises under `WRAP_ON_OVERFLOW`. | Read |
| S-l | Implicit-this `f /= 0.0` stores inf because only an int zero is checked (I:5026, VB:4416). | Not reproduced: the `var f = 1.0` field raised `Division by zero` |

### Open ruling questions the audit raised

- **R-q1.** Does "the right side first" include taking the right side's *value*? Today `a[g()] += b[1]` reads `b[1]` after `g` on both backends (99). C++17 takes the value first (2).
- **R-q2.** Should a container-literal element take its value when evaluated, like a by-value argument? This concerns S-i, and heavy values in literals.
- **R-q3.** Should compound targets consult the global operator table uniformly? Today identifiers consult it for `+ - *` only (I:4594, VB:4012), subscripts for all five (I:5438, VB:6253), and members and statics not at all.
- **R-q4.** Choose one semantics for each compound pair: integer `/=` on members (F9), `string += char` (F10), `string += non-string` through `to_string` (F10), `%=` on a cpp-bound variable (D6: unknown_operator today), utf8 `+=` on implicit-this. Also choose one zero-divisor text and code per condition (F12).
- **R-q5.** Should a builtin method, or a coroutine handle's `resume` / `done`, be usable as a value (`var f = h.resume;`)? Both backends implement it (VB:12612-12690, I:8270-8291). The spec pins only the missing-member text.
- **R-q6.** One handle-admission rule, static tag or dynamic class (S-h), and one text family: seven handle texts exist today.
- **R-q7.** Should a store through a field reference use the field's full table? `auto& r = o.f; r = true` raises `Cannot assign 'bool' to field of type 'int'`, while `o.f = true` stores 1. RL:633-704 is a second, narrower field table.
- **R-q8.** NaN `<=>`, and float `operator==` versus `<=>` at the C++ level (S-j).

## The map

### A. Evaluation order and capturing arguments and operands

| Rule | Sites | Differences between the copies |
|---|---|---|
| Capture kernel | `detail/arg_capture.hpp` (one kernel) | Already shared |
| Interpreter capture per call form | math intrinsic I:7269; string builtin I:7361; direct method I:7558-7561; generic I:7762; `new shared_ptr<T>` I:9541-9544; `new T` I:9578-9581 | The two `new` lambdas are identical and each does a string lookup per argument. The script_exception/runtime_error conversion blocks at I:7549-7575 and I:7751-7778 are identical. |
| VM capture | `capture_arg` VB:11184-11217 (probe, stack, method, new, opaque); emitted from VC:6015, 6065, 6170, 6250 and VI:1344 through `emit_arg_capture` (VC:5278) | The VM never reads the parse stamp `call_expr::arg_captures`. It recomputes it at five compile sites, because inliner clones do not carry the field. For constructor initializers, however, both backends read the parse stamp (arg_capture.hpp:175). |
| Getter-shadowed method callee | `member_call_binding` (arg_capture.hpp:91-106) / VB:11198 classify without running `_get_m`. The interpreter classifies the callable the getter returns (I:7712-7762). | Suspected classification divergence (rare) |
| Receiver ahead of arguments for string builtins | Interpreter: node whitelist (I:7342-7350). VM: `callout_free` gate (VC:6078-6083) plus a probe after the arguments (VB:14176-14199). | Two "may run code" classifiers. Both are correct by fallback. |
| Capture ahead of a conversion that may run code | I:14243-14250 and VB:20400-20417; constructors I:13069-13076 and VB:15798-15804; coroutine `coroutine.cpp:28-32` | Five loops, one rule. The constructor gate is spelled `construction_runs_code` in one and `init_env != null` in the other. |
| Compound right side first | Identifier I:4566-4591 and 4881-4891 (the constrained-reference branch at I:4403-4431 carries `varPtr` across the right side); subscript I:5308-5418 / VC:5750-5767 / VB:10394; member I:5090-5116 / VC:5786-5810 / VB:13583 | The order matches. The dereference timing is F3 and R-q1. |
| Token-to-compound-kind maps | VC:5719, 5756, 5799 (while `compound_kind_for` VC:461 exists); VC:2536 and 3540; VB:4015, 4038, 4172, 4328, 6253, 10418, 10453, 13610; I:4414, 4597, 4621, 4758, 4943, 5134, 5312, 5376, 5440 | About 20 switches, one mapping. R-q3 hides inside them: the identifier subset `+ - *`. |
| "May run user code" predicate | Gated `callout_free<false/true>` (TR:84); strict `quiet_read` (VC:4475: "a binary is never quiet"); `env_identifier` (VC:4405: an environment name may run a getter) | Three different answers to one question. F4 and F7 are consumers that got the gated answer where they needed the strict one. |

**Consolidation A1: one "value taken now" rule.** Replace the gated consumers (`stamp_arg_captures`, `deref_ahead_of`, the literal loops) with a *strict* may-run-code bit: any binary or subscript whose operand classes the checker has not proven primitive, any member read with a user accessor, and any environment identifier. The same bit decides whether a plain store or compound value is dereferenced ahead of the target.
- Fixes F3, F4 and F7. F3 becomes the interpreter's behavior; F4 and F7 become the spec's.
- Deletes the ad hoc callout test at `emit_arg_capture`'s callers.
- Adds `op_deref_top` / `op_capture_arg` only where the strict bit fires, which is never on a proven-primitive hot loop.
- Lines: about +20 net. The value is a single rule, not a smaller line count.
- Gates: G-suite, G-fuzz (`--order-band --order-args`), G-parity-blind (F4 and F7 tests), G-manifest diff reviewed (the manifest changes only at non-primitive shapes), G-hash, G-perf.
- Wait: VC and TR stamp semantics.

**Consolidation A2: one token↔kind table.** A `constexpr` table in `detail/compound_ops.hpp` (base op, op slot, method id) replaces about 20 switches, about 140 lines. The identifier `+ - *` subset stays byte-identical until R-q3 is ruled. A table column can represent it, so no special-case code is needed. The interpreter half (about 65 lines) can land now.
- Gates: G-suite, G-text, G-manifest (identical).

**Consolidation A3: one interpreter argument loop.** An `eval_call_arguments` helper covers the direct, generic, string and two `new` loops, including the duplicated conversion blocks: about 45 lines. It lands now, on the interpreter only.
- Gates: G-suite, G-fuzz `--order-args`.

**Consolidation A4: carry `arg_captures` through inliner clones.** The VM then reads the parse stamp like the interpreter, and the five recompute sites go: about 10 lines, plus one vector allocation per compiled call. The risk is that a clone site misses the stamp, so the change needs a restamp-on-clone assert.
- Gates: G-manifest identical.
- Wait: VC and VI.

**Consolidation A5: one `capture_arguments_from(span, from, params)` loop** for the five binder loops, about 12 lines. The interpreter and coroutine halves can land now.

### B. Graft and inlining guards, and the effect walks behind them

About 19 recursive walks answer overlapping effect questions. Each one lists its own child cases and applies its own implicit policy.

| # | Predicate | Question | Differs by |
|---|---|---|---|
| 1-2 | `callout_free<false/true>` TR:84 | Can this run user code? | Gated. A math intrinsic counts as free in the `<true>` form. |
| 3 | `quiet_read` / `quiet_index` VC:4475, 4493 | Can a hoisted read run code? | Strict |
| 4 | `observing_hoist_in` / `hoist_in` VC:4499, 4516 | Is there a hoisted read in the subtree? | Re-walked at every binary level (quadratic) |
| 5 | `env_identifier` VC:4405 (`env_reference_candidate` VC:4507 has no callers) | May an identifier run a getter? | Contradicts #1 |
| 6 | `slot_argument_in` / `read_ahead_of` VC:4434, 4461 | Can a call bind slot S by reference? | No assignment, `new` or literal case (S-a) |
| 7 | `names_slot` VC:5821 | Does the expression mention S? | Same recursion as #6, plus assignment |
| 8 | `private_local` VC:4424 + parser `ref_escape_marker` parser.cpp:31 | Can user code rebind S? | Flow-sensitive |
| 9 | `call_args_need_probe` VC:648, `compound_rhs_inert` VC:5679, `call_partner_ok` VC:4971 | Flat shape gates | Three strictness levels |
| 10 | `deref_ahead_of` VC:5261, literal loops VC:2019-2046 | Must a pushed reference be taken first? | Consumes the gated predicate (F4) |
| 11 | `clone_expr` / `clone_stmt` / `clone_body` VI:468-760 | Graft admission, plus `writes_global/member/ref`, `reads_env` | Records syntactic stores only. Runs 3-4 times per graft (VI:961, 1127, 1301, 1438). |
| 12 | `site_walk` VI:322-463 | Kind fixpoint; `walk_unknown` | Never visits the callee (D6: a stamp miss, not a wrong result) |
| 13 | `call_events` / `find_path` VI:764, 787 | Calls in execution order | Value first for assignment (invariant 12) |
| 14 | `landable` VI:116, 157 | Can a block land this? | Engine alias table, nested memo |
| 15 | `plan_hoist` position rules VI:982-1080 | Spill or keep the operands before the call? | Its own alias rules |
| 16-17 | Parallel admission walk and `proof_mode` PT:971-1362, 507-580, 1365 | Writes to enclosing state; read-only parameter proof | Live environment. Proof mode has no runtime walls (F8). |
| 18 | `warm_expr` PT:718-784 | Intern names | Duplicates `body_walker`'s shape |
| 19 | `raw_call_reaches_program` VC:851 | Can a call reach a frame program? | Conservative |

Two value-kind lattices are duplicated in VI: `value_kind` (VI:207) and `kind_of` (VI:258). The first lacks the logical, `!`, operand-class and replaced-call arms. The name list `weak_from_this` / `shared_from_this` / `__jai_parallel_*` appears at both VI:924 and VC:4968. `compile_lambda` (VC:6290-6341) inlines the collectors defined at VC:6535-6569.

**Consolidation B1: delete `method_body_env_reusable`.** VC:105-285 and VC:293-459 list the same 134 opcodes; the audit diffed the sets. Their dynamic checks are identical too. VC:808 therefore computes `!needs_frame_env || X`, which always equals `!needs_frame_env`. Delete VC:287-459 and the `chunk::method_env_reusable` field (CH:605, read at VB:19244 and 19414), replacing its readers with `!needs_frame_env`. About 175 lines.
- Gates: G-suite, G-manifest identical (the flag values are unchanged).
- Wait (VC and VB), but it is a pure deletion.

**Consolidation B2: one evaluation-order child visitor** (`detail/expr_children.hpp`) folds the recursion boilerplate of walks #4, #6, #7, #13, the expression half of `body_walker` and `warm_expr`. About 100-150 lines. The visitor states once which children each walk skips (a static member's object, callee vs receiver, lambda bodies).
- Gates: G-manifest identical.
- The header and its unit tests can land now; adoption waits on VC and VI.

**Consolidation B3: one effect summary with an explicit policy.**
- Facts:
  - `runs_code_gated`, `runs_code_strict`, `runs_code_intrinsics_ok`
  - `ref_bind_slots` (callee-agnostic) and `ref_bind_slots_proven`
  - `mentions_slots`
  - `stores {slots, global, heap}`
  - `calls_in_order`
- The facts are memoized per compile, not stamped on the AST, because clones are minted per graft.
- It replaces #1, #2, the recursion in #4, #6, #7, #13, `walk_unknown` in #12 and the `writes_*` flags in #11. That is about 300 lines of predicates, replaced by a module of about 200: net about 100 lines. The real gain is that each consumer names the policy it needs; F4, F7 and S-a are all cases of a consumer getting the wrong policy.
- Proof step: land the module with an identity oracle first, a test that asserts `summary.runs_code_gated == callout_free<...>` on every node of the corpus. Then move the consumers over one at a time. A consumer that keeps its policy owes G-manifest identity; a consumer that moves to the strict policy is A1.
- It cannot replace `quiet_read`, `landable`, `plan_hoist`'s path rules or the parallel walk, because those depend on compiler or runtime state.

**Consolidation B4: one kind lattice** (`value_kind` merged into `kind_of`, about 30 lines). Graft verdicts get more permissive, but only soundly, so the manifest changes. Gates: G-fuzz, G-hash, G-perf.

**Consolidation B5: one `graft_verdict` helper** replaces the three hand-built probe contexts (VI:954-962, 1121-1128, 1284-1303): about 40 lines, and fewer clone passes. Gates: G-manifest identical.

**Fix F8, the proof-mode escape rules (PT, free now).** In proof mode, treat any non-builtin method call, any user accessor on any chain, and any binary over a non-primitive operand as an escape. Gates: G-suite, `parameter_alias_differential_gate`, `jaiscript_fuzz --no-param-alias`, G-perf (the proof may lose hits).

### C. Assignment and compound assignment by destination kind

This cluster is about 5,100 lines: about 2,300 in the interpreter (`visit_assignment_expr_t` alone runs I:4375-6300) and about 2,800 in VB. Five kernels are already shared:
- `static_store.hpp`
- `resolve_namespace_store`
- `field_store_convert` / `enforce_field_write` / `enforce_static_write` (CD:2986-3148)
- `user_assign_operator` (CD:3057)
- `member_reads_back` (CD:2575)

| Destination | Interp plain | Interp compound | Interp `++`/`--` | VM plain | VM compound | VM `++`/`--` |
|---|---|---|---|---|---|---|
| local slot | I:5596-5667 | I:4389-4891 | I:4149-4228 | `store_popped_value` VB:3407-3485; `t_store_*` VB:9755, 9780; `binary_fused_store` VB:5480 | `exec_compound_store` VB:3964-4288; `compound_fused` VB:5669-5817; `op_t_*` and cells | `exec_incdec` VB:4454-4526 |
| env/global | I:5719-6030 | same | I:4161 | VB:3570-3847 | VB:3964 | VB:4459 |
| escape cell / ref alias | I:5603-5614, 5726-5751 | RL:816; I:4438, 4885 | I:4166 | VB:3413-3425, 3585-3600 | VB:3972-4001 → RL:816 | VB:4466 |
| bare field in method | I:5679-5708, 6053-6094 | tail I:4893-5066 | tail I:4229-4297 | IC VB:3491-3521; VB:3534, 3849-3888 | tail VB:4290-4451 | tail VB:4528-4587 |
| `this.f` / `obj.f` | I:6166-6262; `assign_member_value` I:4310-4372 | I:5067-5300 | desugared | `store_member` VB:13365; `assign_member` VB:13294 | `exec_member_compound` VB:13577-13770 | desugared |
| `C::s` / bare static | I:6170-6192; I:6095-6118 and 6127-6146 (two copies) | I:5070 | in place | `exec_set_static` VB:13554; VB:3889-3911 and 3920-3941 (two copies) | VB:13538 | in place |
| cpp-bound / hybrid property | I:5752, 6065 | I:4644-4744; tail I:5052 | I:4174-4195 | VB:3611, 3861 | VB:4061-4162, 4442 | VB:4471-4492 |
| array element / map entry | `store_through_subscript_reference` I:6306 | I:5308-5555 | desugared | VB:6180, 6369, 9824, 10015, 10083 | VB:6236, 10393 | desugared |
| `ns::x` | I:6961 | via `store_static_target` | desugared | VB:13496 | VB:13539 | desugared |

Result read-back is implemented four ways: the interpreter's per-statement flag (F1), and in the VM `compound_flag_result_needed` (VC:5727, 1869, 1910), `assignment_result_used` (VBH:645) and `store_flag_no_result` (VB:5491).

Pipelines re-run in full:

1. **Compound arithmetic: 12 copies, about 1,060 lines.**
   - Interpreter: I:4644-4744, 4750-4876, 4979-5047, 5182-5266, 5438-5502, 4449-4563.
   - VM twins: VB:4061-4162, 4164-4287, 4363-4437, 13651-13741, 6251-6315, plus the fused lanes.
   - RL:829-882.
   - These produce F9-F12.
2. **Typed variable store** (proof check, `operator=`, `enforce_type_compatibility`, retag, clone-or-move): about 800 lines, near-verbatim across backends (I:5603-5663, 5724-6029, 6961-6998; VB:3413-3483, 3584-3846, 13496-13535). This includes about 190 lines of weak/shared rows per backend.
3. **Twin pairs:**
   - `enforce_type_compatibility` / `compound_typed_store_back`: I:7001-7077 vs VB:3035-3107.
   - `assign_member_value` vs `assign_member`.
4. **Element store through a reference: 7 copies of about 35 lines** (I:6306-6348; VB:6194-6231, 6323-6358, 6420-6439, 10071, 10340, 10481).
5. **The implicit-this tails** (about 170 compound and 60 `++`/`--` lines per backend) repeat `this.x op= v`. They are reachable only for hybrid properties and access errors.
6. **Dead code:** I:5505-5542, the identifier branch inside the subscript-compound else. Identifier targets return earlier.

**Consolidation C1: four kernels in `detail/`.**
1. `lvalue_view`: stack-only, no holder minted. It has one kind per destination and offers `read()`, `store(v) → stored` and `stored_value()`. Each kind owns its typed rule and its text.
2. `compound_apply(kind, cur, rhs, arith)` with a force-inlined `compound_in_place` for int/int, float/number and string/string.
3. `store_variable`, the typed variable store including the handle rows.
4. `element_store_through`.

   `++`/`--` on identifiers then lowers to the compound path with `old_result`, deleting I:4149-4302 and `exec_incdec`.

   Guarded lanes stay separate code because they exist to avoid `script_value` traffic: raw i64/f64/byte buffer lanes, `op_t_*`, cells, the `type_provable` slot store and the this-field IC. They fall back to `lvalue_view::store`. That makes the F5 fix structural: the IC's miss and arm path is the kernel, and object-typed fields never arm.

   | Part | Lines | Precondition |
   |---|---|---|
   | Variable-store kernel | about 550 | — |
   | Element-store kernel | about 200 | — |
   | Member and namespace twins | about 95 | — |
   | Enforce / store-back | about 70 | — |
   | Compiler helpers | about 60 | — |
   | Implicit-this tails through the member path | about 350 | — |
   | Identifier `++`/`--` via compound | about 270 | — |
   | Compound kernel | about 800 | rulings R-q3 and R-q4 |

   Total about 2,000-2,400 of about 5,100.
   - Gates: G-suite, G-text, G-fuzz (all bands), G-manifest (identical for the kernel moves; changed for `++`/`--` lowering), G-hash, G-perf. Watch `store_popped_value`'s slot early exit and `i++` loops, which must land on `compound_fused` or typed rows.
   - Now: the kernels in `detail/` and the interpreter adoption. That adoption fixes F1 and F2 on the interpreter side by construction, since `store` returns the stored value.
   - Wait: VB and VC adoption.

### D. Arithmetic, comparison, bitwise, unary, truthiness

**True twins:**
- The 17 general handlers: ID:49-888 vs VB:1920-2720, about 1,640 lines together. Each handler runs its numeric body twice, before and after the transparent unwrap.
- `evaluate_arithmetic`: I:7119-7234 vs VB:2925-3016. It has different texts from the binary handlers (F12).
- Unary: I:4054-4147 vs VB:4590-4640.
- `is_truthy`: interpreter.hpp:1197-1236 vs VB:1350-1384, plus three copies of the object `to_bool()` part (typed_slot.hpp:43-61, VB:1310-1348, I:2379).
- `bound_fastpath_zero_divisor`: I:57-82 vs VB:85-110.
- `is_numeric_op` (interpreter.hpp:656) vs `is_numeric_binary_op` (VBH:37).
- The weak-vs-null equality block: four copies.

**Scalar lanes:** "int op int under the overflow policy" is written out 18 times (7 in the interpreter, 11 in the VM). Int `<` appears about 17 times and the zero-divisor check about 30 times.

| Lane | Location |
|---|---|
| Interpreter FP1-3 | I:3313-3660 |
| `typed_binary_head` / `typed_cmp_jump_head` | VB:5153-5233 |
| `binary_fused_compute` | VB:5297-5357 |
| `exec_fused_cmp_jump` (with its own null and string lanes) | VB:5563-5633 |
| `raw_int_arith` | VB:6502-6527 |
| Cell ops | VB:8547-8650 |
| `cell_cmp_any` / `exec_c_arith_any` | VB:8718-9105 |
| `typed_arith_fast` / `typed_cmp_jump_fast` | VB:9625-9699 |
| Cell math re-deriving `math_intrinsics.hpp` rows | VB:9209-9251 |
| Parser fold | parser.cpp:2029-2136 |

**Consolidation D1: `detail/binary_handlers.hpp`.** A CRTP `template<class Host>` kernel with host hooks for value minting, concatenation, object dispatch, unwinding state and the parallel verdict.
- One arithmetic template per operator (a functor plus its overflow text), one comparison template, and `/` and `%` specialized.
- The unwrap duplication folds away: unwrap only when an object or `shared_ptr` is present, run one numeric body, and dispatch to the class method only if an operand started as an object.
- About 1,640 → 400 lines: **about 1,200 deleted**.
- Neutral for performance, because this is the general path behind the lanes.
- Preserves: `==` skips the bound decode (opaque identity), and `!` evaluates truthiness before the decode.
- Gates: G-suite, G-text, G-fuzz, G-hash, G-perf (spot check).
- Now: the header, and the interpreter's ID rewritten as thin calls. Wait: the VM adopts it by deleting its twins, so parity then holds by construction, as `ref_lvalue.hpp` does today.

**Consolidation D2: `detail/scalar_ops.hpp`.**
- API: `arith_ii(op, a, b, r&) -> scalar_fault`, `arith_ff`, `cmp_ii`, `cmp_ff`, `fault_error(op, fault, lane)`, plus a `constexpr` typed-opcode→token map. The typed opcodes already follow token order.
- Each lane keeps its own operand fetch and its own miss policy: raise, `cells_miss`, or false.
- Raw sinks return raw scalars, never `checked_result<script_value>`.
- Lines: interpreter FP1-3 about 300 → 60, VM lanes about 200 → 60, plus the dead `binary_fast_shape` (row X1). **About 560 in total.**
- Cells and typed lanes already have the `ok` shape and are forceinline-neutral. The fused and FP lanes change codegen (a comparison vs arithmetic split), so they owe G-perf.
- Gates: G-suite, G-fuzz (MISS build `--miss-every`), G-manifest identical, G-hash, G-perf.
- Now: the header and interpreter FP1-3. Wait: the VM lanes. The MSVC Debug frame ceiling (invariant 5) applies to anything that moves inside `run_dispatch`.

**Consolidation D3: small kernels**, about 385 lines together. Now for the headers and the interpreter; later for the VM.
- `detail/truthiness.hpp`: primitive switch plus the shared `to_bool` memo, with the VM wrapper kept out of line. About 105 lines together with the `to_bool` copies.
- A zero-divisor helper, the weak-null helper, and one numeric-op predicate. About 80.
- One `normalize_binary_operands(li, ri, policy)` prologue. About 200.

The compound ladders merge only through C1 plus rulings.

### E. Exception, unwind and lifetime

**VM:**
- Three handler searches: `unwind_to_handler` VB:16461, `handle_op_error` VB:16553, `handle_throw_unwind` VB:16572.
- About nine frame-exit implementations:

  | Exit | Location |
  |---|---|
  | `pop_script_frame_core` | VB:19503 |
  | Raw `exec_c_ret` pop | VB:8306-8317 |
  | `call_script_function` cleanup | VB:20530 |
  | `run_program` | VB:18833-18873 |
  | `eval_expression` | VB:12472-12483 |
  | Namespace variable | VB:15504-15513 |
  | Namespace declaration | VB:15613-15631 |
  | `run_fiber` | VB:901-1134 |
  | Push rollbacks | VB:11648, 19038, 19266, 19433, plus six bind-fail pops |

  They store one mark tuple (stack, iter, cfor, pending, try, env), re-captured at five push sites (VB:8209, 11625, 19009, 19211, 19373). Their cleanup orders differ: `call_script_function` erases pending before the stack, `pop_script_frame_core` after it. Four of them never truncate pending, which is S-b.
- 13-15 native-exception conversion pairs, about 12 lines each (VB:11496, 11859, 11947, 11979, 12068, 12097, 14997, 15463, 16271, 16314, 18837, 19102, 19126). They vary in where they truncate first and whether they push. Three catch only `runtime_error`. `raise_script_exception` (VB:12438) and `convert_cpp_exception_in_frame` (VB:19851) exist but have few users.

**Interpreter:**
- The frame cleanup is already one lambda (I:14107). `resume_coroutine` lacks the same treatment (F6).
- Three scope restorers: block I:6453, switch case I:11661, switch default I:11707.
- Five conversion pairs (I:2979, 7551-7574, 7662-7678, 7754-7777, 7799-7819).

**Cross-backend twins:**
- Try entry, catch finish and escape: `exec_try_push` / `exec_catch_end` vs `visit_try_stmt`.
- Coroutine exception-trio park: `fiber_exception_swap` VB:929 vs `coroutine_exception_swap` I:14623.

**Consolidation E1: one native-exception helper per backend.** `raise_native(e, truncate_to, push)` in the VM (about 150 lines) and the interpreter (about 45). The catch type becomes one decision: S-e, a ruling on whether to catch `std::exception` everywhere, as `engine::execute_parsed` does. Cold paths, outside `run_dispatch`.
- Gates: G-suite, G-text.
- Now: the interpreter. Wait: the VM.

**Consolidation E2: one mark tuple, truncate, unwind and entry-scope routine.**
- `stack_marks`, and `truncate_to<FrameSurvives>` in one fixed order with detach-then-destroy for the reentrant vectors (this also closes S-c).
- A shared `retire_record` tail for the hot pop, the raw pop and the rollbacks.
- `unwind(fp, base, mode)` merging `handle_op_error` and `handle_throw_unwind`, with mode flags for the trace and the implicit result.
- An `entry_frame_scope` RAII for the five entry frames, which closes S-b.
- About 150-200 lines. The hot pop and the push sites must compile to the same stores. Unifying the destructor order is observable, so pin the order to the interpreter.
- Gates: G-suite, G-fuzz, a destructor-order test, G-hash, G-perf.
- Wait: VB.

**Consolidation E3: coroutine resume RAII in the interpreter.** This fixes F6, and it makes the raw `pop_back` loop at I:14322 dead. Now. Gates: G-suite, an F6 red test.

**Consolidation E4: a shared `detail/` exception-trio kernel** for try entry, catch finish, escape and fiber park. About 70 lines. Removes two byte-parallel twin sets. Wait: VB.

### F. Typed-slot conversion, element boundary, copies

The scalar table is already one kernel: `convert_for_typed_slot_core` (CD:2609) with its script rows at typed_slot.hpp:37. The duplication sits in the layers around it.

| Rule | Shared kernel | Re-implementations |
|---|---|---|
| float→int checked | `checked_float_to_integer` numeric_conversion.hpp:16 | Inline in RL:695-702. A rival *saturating* policy, `float_to_script_int` (value.hpp:89), serves every host parameter through `checked_as<script_int>` (D-V3, ruling). |
| int/float/bool element rows | CE | Listed five times inside CE (115-126, 262-275, 568-571, 590-594, 660-664). Again in RL:668-673, and in the checker (type_checker.cpp:1535-1538, 1625-1630). |
| Container kind compatibility | CE:141-167 | `ref_container_kind_compatible` RL:593-622, verbatim |
| Map key check | CE:643-650 | Inline with its own text: I:3870-3879, VB:6023-6032, VB:10035-10043 |
| Handle slots | `weak_slot_takes_shared` + `field_handle_rows` (CD:2591, 2745; fields only) | Local `=` I:5755-5950 / VB:3616-3797. Declaration I:6519-6679 / VB:10611-10750. Parameter I:14545-14568 / VB:3155-3178. Assignment I:7045-7054 / VB:3075-3084. `weak_ptr<T>(x)` I:9447-9474. Seven texts. |
| Parameter share rule | — | I:14264-14275, VB:1031-1035, VB:20430-20436 |
| Capture clone | — | `clone_for_capture` I:174-192 = `vm_clone_for_capture` VB:152-170 |
| Type→TYPEID identity map | — | Nine copies (I:7013, 14514; VB:3042, 3123; CD:2990, 2800; return_conv.hpp:62; ast.hpp:803; type_proofs.hpp:34, 47). The utf8 row is missing from four (performance only). |
| Type-name builders | `container_base_type_name` CE:27 | `get_type_name` I:195-215, `vm_type_name_of` VB:112-131, `ref_type_name_of` RL:516-536, plus seven one-line aliases. Three source-spelling policies (CD:2819, CE:61, cast_kernel.hpp:96). |
| Auto-literal homogeneity | — | `validate_container_homogeneous` I:228-380 = VB:187-310 |
| Return classification | `classify_return_conv` return_conv.hpp:30 | VB:20031-20034 re-derives it by string compares |
| Field vs static write | CD:3094-3120 | CD:3122-3148, a twin |
| Element store texts | — | `Cannot assign '{0}' to element of type '{1}'` ×8; `Cannot push '{0}' to array<{1}>` ×4; `Cannot store {0} in {1}` (plus four inline rebuilds of `container_mismatch_error`) |

| Row | Change | Lines | Now or wait |
|---|---|---|---|
| F-1 | `enforce_type_compatibility`, `try_convert_for_parameter` and `compound_typed_store_back` move into typed_slot.hpp | about 120 | Interp + header now; VM later |
| F-2 | Handle-slot kernel (text-preserving first cut; unification is R-q6) | about 430 | Both backends |
| F-3 | `store_element(value, elem_type, eng, text_kind)` at about 12 sites | about 110 | Mostly VB |
| F-4 | Delete the ref field table RL:516-704 (R-q7) | about 150 | Free header, ruling-gated |
| F-5 | One `constexpr scalar_storage_index` replaces the nine identity maps | about 50 | Mostly free |
| F-6 | One type-name builder, and the aliases dropped | about 75 | Free except `vm_type_name_of` |
| F-7 | `clone_for_assignment` becomes `deref().clone_element()`; shared `clone_for_capture`; one parameter-share rule; one lvalue-shape predicate (`is_lvalue_shaped_expr` vs cast_kernel.hpp:42 vs I:6001, I:3851); `deep_copy_holder` factored in value.cpp (closes S-D3: a cpp-bound `shared_ptr` clone throws `non-copyable`) | about 100 | Mixed |
| F-8 | Homogeneity validator and map-key check move into CE | about 150 | Both |
| F-9 | Field/static write merge | about 25 | Free |
| F-10 | `constexpr` element-row table shared by the predicate, the converter, the typed-source pair and the checker's `container_element_issue`; predicate and converter fused into one pass | about 80 | Free |
| F-11 | `bound_array::fit_node` → `convert_container_element` + `byte_range_error` (D-V4: a byte buffer admits char from the host) | about 20 | Free |
| F-12 | VM `convert_return_value` uses `return_conv_of` | about 5 | VB |

Gates for the mechanical rows: G-suite, G-text, G-manifest identical. F-10 also owes a checker-verdict identity run over the corpus.

**Archives.**
- Weak re-wrap and getter-walk blocks duplicated verbatim: json_archive.hpp:208-238 and 306-332 vs binary_archive.hpp:230-258 and 309-335. About 45 lines.
- Unreachable array and map arms in `to_json_impl_fallback` (json_archive.hpp:462-494), and duplicated float formatting. About 45.
- Setter walk and `post_load` load side duplicated: stdlib/json.hpp:488-535 vs binary_archive.hpp:1001-1030. About 40. D-S5 decides whether a field without a setter loads.
- `stdlib::to_json_impl` (stdlib/json.hpp:44-230) is a third JSON writer used only by the CLI (tools/jaiscript/main.cpp:1557). It prints floats with `setprecision(15)` and swallows getter errors. Replacing it with `json_archive_writer` saves about 190 lines and changes the CLI output, so it needs a ruling.

### G. Inline caches, cells, opcode metadata

**Inline caches.** Eight "class → epoch → slot" memo implementations exist:
- `env_lookup_cache_entry` heads CH:235-261
- the this-field read IC VBH:1491-1555 (no epoch, by design)
- the this-store IC VBH:1564-1599
- `member_ic_entry` CH:594-599
- call_site `ic_*` CH:120-123
- call_site `mic_*` / `probe_*` CH:135-146
- `inline_site holds_cd/epoch` CH:534-535
- the cell `site_member_class/slot` VB:7403

On top of these sit the AST proofs (ast.hpp:365-369) and the interpreter's `mcache_*`. The validity rules differ for real reasons, so a single IC structure would need policy flags; that is a special case and is not proposed.

Proposed instead:
- One `class_slot_memo {cd, epoch, slot}` for member IC, `mic`, `inline_site` and the AST proofs. About 30 lines.
- One `member_ic_slot()` helper replacing four verbatim probe-and-arm copies (VB:13140-13169, 13197-13230, 7385-7391, 13448-13480). About 60 lines. Wait: VB.

**Cells vs general forms.** The cell subsystem is VB:7120-9473, about 2,350 lines.
- The "constant or cell → (kind, bits)" read is copied six times. Two of the copies read `f.code->constants`, the others read `cr.consts`.
- Cell math re-derives `math_intrinsics.hpp`.
- A `cell_operand()` helper (about 40 lines) and a raw math kernel shared with `math_intrinsics.hpp` (about 30) consolidate these. The arithmetic itself belongs to D2.
- Gates: G-fuzz MISS build, G-hash, G-perf. Wait.

**Opcode metadata.** At least nine per-opcode switches exist:

| Switch | Location | Lines |
|---|---|---|
| `opcode_name` | OP:372-562 | 190 |
| The two env-safe lists | VC:105-459 | 350 (B1 deletes one) |
| Debugger locals walk | VB:529-586 | 58 |
| Slot census | VB:17390 | — |
| `cell_refusal_of` | CH:491-509 | 19 |
| `straight_op` / `work_op` / `entry_op` | VC:2707-2742 | 36 |
| `exec_extended` grouping | VB:18516-18547 | — |
| Disassembler | disassembler.cpp:40-208 | 169 |

An X-macro row per opcode (name, text, env class, refusal class, straight/work/entry bits, operand roles) generates the name table and the refusal table, and drives one env gate (table lookup plus about 40 lines of dynamic checks), the debugger walk and a generic disassembler. The CLAUDE.md instruction "add to both env safe-lists … and the debugger's locals walk" becomes one row edit.
- About 150 lines from OP and the disassembler; about 300 from the env lists and `straight_op` (after B1).
- Disassembler mnemonics are substring-matched by about 22 tests, so they stay byte-identical.
- Gates: G-suite, `--disasm` identical, G-manifest identical.
- Wait: the raw-frame work adds opcodes to OP, so the table lands between VM batteries.

**Other VM repeats:**

| Repeat | Lines | Precondition |
|---|---|---|
| `exec_call`'s vector tail (VB:11879-11967) re-implements `invoke_callee` (VB:11994-12117) | about 70 | Needs an opaque-callee test first |
| `exec_return_binary` / `_ident` vs their `_entry` twins (VB:19764-19825) | about 25 | — |
| The `typed_lowering` / `ir_passes` / `arm_listing` prologue, repeated six times | about 12 | — |
| `mic_field_shadowed` = `probe_field_shadowed` (VBH:1520-1527) | about 4 | — |

## Dead code: delete or justify

`vm_compiler.cpp` is 96.8% line-covered and `vm_backend.cpp` 85.2% (llvm-cov figures from `docs/spec_conformance.md`, "Execution census"). The entries below come from that census, from counting opcode emissions for all 186 opcodes, and from a caller scan.

| # | Item | Location | Lines | Evidence | Verdict |
|---|---|---|---|---|---|
| X1 | `binary_fast_shape` and the shape operand | Body VB:2742-2923; three guards VB:4652-4661, 19774-19782, 19806-19814; declaration VBH:1292; constants OP:353-355; `shape=` text disassembler.cpp:72, 198 | about 212 | The only `op_binary` emission is VC:5510, which passes `binary_shape_none`. `op_return_binary` is a rewrite of that instruction that keeps `b` (VC:1124-1126). `exec_binary_fused` never reads `ins.b` (VB:5128-5133). Every guard tests `ins.b != binary_shape_none`. | **Delete.** `binary_shape()` becomes a bool fusability test at compile time. The manifest stays identical: `b` is 0 today. |
| X2 | `method_body_env_reusable` + `chunk::method_env_reusable` | VC:287-459, CH:605, readers VB:19244, 19414 | about 175 | Same 134-opcode set (diffed) and the same dynamic checks. VC:808 reduces to `!needs_frame_env`. | **Delete** (row B1). Stale comments at CH:600-604, VC:807 and vm_backend_tests.cpp:490. |
| X3 | `enter_constructor_in_loop` unreachable tail | VB:16294-16333 | 40 | The block VB:16259-16293 returns on every path. | **Delete** |
| X4 | Coroutine-constructor branch | VB:16242-16256 | 15 | parser.cpp:3937 rejects coroutine constructors | **Delete** (assert) |
| X5 | `exec_extended` unreachable labels | VB:17140-17152, 17181-17185 | 15 | The main switch dispatches these ops directly (VB:18516-18548) | **Delete** |
| X6 | `collect_used_identifiers` | VC:6542-6555, vm_compiler.hpp:277 | 16 | No callers | **Delete** |
| X7 | `env_reference_candidate` | VC:4507-4513, vm_compiler.hpp:126 | 8 | No callers | **Delete** |
| X8 | `compile_only` | VB:1157-1164, VBH:336 | 9 | No callers | **Delete** |
| X9 | Non-block branch of `as_block` | VC:28-37 | 10 | parser.cpp:2817-2823 always yields a `block_stmt` for a lambda body | **Delete.** Also make `lambda_expr::body` a `shared_ptr<block_stmt>` (ast.hpp:408), like `function_decl`. |
| X10 | `op_index_assign` / `op_index_compound` opcodes | OP:35-36 and 392-393, CH:495-496, dispatch VB:18393-18394, env lists VC:137-138 and 325-326 | about 12 | Never emitted. Each executor has one caller, its fused twin's slow tail (VB:6468, 10510; VB:10509 builds a fake `vm_instruction`). | **Delete the opcodes.** The executors become the helpers `index_assign_through` / `index_compound_through`. |
| X11 | Interpreter subscript-compound identifier branch | I:5505-5542 | 38 | Identifier targets return earlier | **Delete** (with an assert while the suite runs) |
| X12 | `json_archive` fallback array and map arms | json_archive.hpp:462-494 | about 33 | Called only with object or scalar values (353, 359) | **Delete** |
| X13 | Constant-string map branch of the fused index read | VB:6774-6803 | 30 | The scalar-key branch (VB:6817-6841) admits a superset with the same hit/miss semantics; only the probe differs (`map_string_key_probe` vs `find`) | **Delete after an `m["k"]` microbenchmark**, since the probe differs |
| X14 | `emit_unsupported` | VC:621-627 and four defaults (VC:1188, 1196, 2100, 5162) | 7 | Fail-closed defaults of node switches | **Justify:** the internal-error default. Reword its "stage B2/B3" text. |
| X15 | `T_JLE/JGE/JEQ/JNE_FF`, `T_MOD_FF` | Table entries VC:2349-2401; arms in shared switches | about 10 | Rarely hit, but the lowering and the range checks (`op_t_add_ii..op_t_mod_ff`, `op <= op_t_jne_ii`) depend on the regular matrix | **Justify:** removal would carve a gap, which is a special case |
| X16 | Boxed chain tail of the fused index read | VB:6647-6684 and its twin in `exec_index_store_fused` | about 75 | Reachable: VC:5092-5111 chains unproven operands (`var x = 1.5; a[i + 1 + x]`) | **Needs test.** Alternative: chain only int/int-proven steps, so both tails die. Manifest changes, so G-perf. |
| X17 | `exec_call` opaque vector tail | VB:11879-11967 | about 70 | Reachable (`var fs = [print]; fs[0](1);`) | **Merge** into `invoke_callee` after an opaque-callee test (G-row above) |
| X18 | Builtin and coroutine methods taken as values | VB:12612-12690, I:8270-8291 | about 75 per backend | Reachable from script; not specified | **R-q5:** specify and test, or make it a diagnostic and delete |
| X19 | `enter_constructor_in_loop` catch arms | VB:16271-16283 | 13 | Reachable only if `push_method_frame` throws | **Needs test** (a converting-constructor bind that throws), then fold into E1 |

Pure deletions X1-X12 total **about 580 lines**. None of them changes compiled code. They owe G-suite, G-manifest identity and G-hash, and no G-perf beyond a spot check.

**Stale documentation:**
- `docs/invariants.md` §9 cites `call_site::receiver_cache_slot`, which no longer exists, and says IC entries carry no symbol, while `this_field_sym` exists (CH:252).
- §5's line references (7101, 7203-7226) are stale.
- §13 defines the capture set by the gated `callout_free<true>`; row A1 changes that definition.

## Ranked rows

Ranking is by deletion-to-risk. A row marked **Now** touches only free files: the interpreter, `detail/`, `class_definition.hpp`, value, the checker, the archives, `parallel_transform.cpp` and tests. A row marked **Wait** restructures `vm_compiler.cpp`, `vm_backend.cpp`, `vm_inline.cpp` or `opcode.hpp` and lands between VM batteries. **Coordinate** marks a pure deletion in a contended file: it rebases trivially and lands as a standalone commit whenever the VM agents are between batteries.

| Rank | Row | Lines | Risk | Perf risk | Gates | When | Ruling |
|---|---|---|---|---|---|---|---|
| 1 | **X1-X10 VM dead-code batch** (shape lane, env list twin, constructor tail, extended labels, uncalled helpers, `as_block`, index opcodes) | about 510 | very low | none (identical code) | G-suite, G-manifest, G-hash | Coordinate | — |
| 2 | **X11 + X12 + archive S1/S2/S4**: interpreter dead branch, JSON fallback arms, weak/getter/setter walks shared | about 200 | low | none | G-suite, G-text, archive round-trips | Now | D-S5 only for S4's setterless rule |
| 3 | **D1 `detail/binary_handlers.hpp`**: the 17 general handlers, interpreter first | about 1,200 (600 now + 600 on VM adoption) | low | neutral (general path) | G-suite, G-text, G-fuzz, G-hash, G-perf spot | Now (interp), Wait (VM) | none |
| 4 | **C1 variable-store, element-store and member kernels** (`store_variable`, `element_store_through`, `lvalue_view` minus the compound kernel); fixes F1 and F2 on the interpreter | about 900 | low-medium | typed local and field store: keep inline heads | G-suite, G-text, G-fuzz, G-manifest (VM identical), G-hash, G-perf | Now (kernels + interp), Wait (VM) | none for the text-preserving cut |
| 5 | **F-5, F-6, F-7, F-8, F-9, F-10, F-11 + D3**: identity map, type names, clone helpers, homogeneity and map-key kernels, field/static merge, element-row table, truthiness, zero-divisor/weak-null helpers | about 750 | low | none (same codegen, forceinline) | G-suite, G-text, G-manifest, checker verdict identity | Now (headers + interp), Wait (VM twins) | none |
| 6 | **E1 + E3**: native-exception helpers (both backends) and interpreter coroutine resume RAII; fixes F6 | about 195 | low | none (cold) | G-suite, G-text, F6 red test | Now (interp), Wait (VM) | S-e catch type |
| 7 | **D2 `detail/scalar_ops.hpp`**: the scalar kernel under every lane | about 560 (includes X1's 182 if not already gone) | medium | **hot**: fused and FP lanes change codegen | G-suite, G-fuzz incl. MISS, G-manifest, G-hash, G-perf | Now (interp FP1-3), Wait (VM lanes) | none |
| 8 | **G opcode X-macro table** (+ env gate after X2) | about 450 | low-medium | none (compile time and listings) | G-suite, `--disasm` identical, G-manifest | Wait (OP gains opcodes from raw-frame work) | none |
| 9 | **E2 unwind consolidation** (marks, truncate, retire tail, unified unwind, entry scope); closes S-b and S-c | about 180 | medium | hot push/pop must stay identical | G-suite, G-fuzz, destructor-order test, G-hash, G-perf | Wait | destructor order pinned to the interpreter |
| 10 | **B2 + B3 + B5 effect module and A1 strict policy**: one child visitor, one effect summary with named policies, one graft-verdict helper; fixes F4 and F7 | about 250 net (plus about +20 for A1) | medium | A1 adds captures and derefs only at non-primitive shapes | identity oracle, G-suite, G-fuzz order bands, G-manifest (identical except A1), G-hash, G-perf | Now (headers + oracle test), Wait (adoption) | invariant 13 rewording; R-q1 for compound values |
| 11 | C1 compound kernel + identifier `++`/`--` via compound + implicit-this tails | about 1,400 | medium | compound loops | full set | Wait | **R-q3, R-q4** |
| 12 | F-2 handle-slot kernel (text-preserving first), then unification | about 430 | medium | none | G-suite, G-text | Now (interp), Wait (VM) | R-q6 for unification |
| 13 | F8 proof-mode escapes in PT (correctness) | about +15 | low | may lose parameter-alias hits | `parameter_alias_differential_gate`, `--no-param-alias` fuzz, G-perf | Now | none (spec is clear) |
| 14 | A2 + A3 + A5: token-kind table, interpreter argument loop, binder capture loop | about 200 | low | none | G-suite, G-manifest | Now (interp halves), Wait (VM halves) | R-q3 decides the table's identifier column |
| 15 | G member IC helper + `class_slot_memo`; X17 `exec_call` → `invoke_callee`; return twins | about 190 | low-medium | member read path | G-suite, G-manifest, G-perf | Wait | — |
| 16 | F-4 ref field table deletion; archive `to_json_impl` replacement | about 340 | medium (behavior changes) | none | G-suite, G-text changes reviewed | Now (free files) | **R-q7**, CLI output ruling |
| 17 | X16 chain tail by proof; X18 methods as values | about 225 | medium | fused index | G-perf | Wait | R-q5 |

Totals:
- **About 5,000 lines** deletable with no semantic change (rows 1-10 and 12-15 in their text-preserving first cuts, overlaps such as X1 inside D2 and the element store inside C1/F-3 counted once).
- **About 7,000** once the ruling-gated rows land.

This is against a core of roughly 68,000 lines in `source/implementation` plus about 12,000 in the heavy headers.

### Execution order

**Now, on free files, in this order:**

1. **Red tests** for F1-F12 and for S-b, S-c and S-f in free test files (`Spec Conformance`, `Call Argument Capture`, `Compound Assignment Order`, review regression). Tests that pin spec-derived values the runtime does not meet yet go into `known_bugs` until their row lands.
2. **Row 2** (interpreter dead branch, archives).
3. **Row 13** (F8 proof fix): correctness, contained in PT.
4. **Row 6, interpreter half** (conversion helper, coroutine RAII, F6).
5. **Row 3, interpreter half** (`binary_handlers.hpp` + ID rewrite).
6. **Row 5, headers + interpreter** (identity map, names, clones, CE table, truthiness).
7. **Row 4, kernels + interpreter adoption.** F1 and F2 flip to the VM's answers on the interpreter. Each flip is a spec-derived fix: the VM already gives the spec's value.
8. **Row 7, interpreter FP1-3**, and **row 10's headers** plus the identity-oracle test.

**Between VM batteries, coordinated with the raw-frame and math-lowering agents:**

9. **Row 1** dead-code batch (standalone commit).
10. **VM adoption** of rows 3 and 5 (pure delete-and-call).
11. **Row 4, VM adoption.** This includes the F5 fix: the this-field store IC's arm and miss go through the kernel, and object fields never arm.
12. **Row 6, VM half.**
13. **Row 7, VM lanes** (G-perf).
14. **Row 8** opcode table.
15. **Row 9** unwind.
16. **Row 10** adoption with A1.
17. **Rows 14 and 15.**

**After rulings:** rows 11, 12 (unification), 16 and 17.

Each landing runs its gate set once as a battery. Related rows batch into one landing where they share gates: rows 3 and 5 share every gate; rows 1 and 8 share the manifest and disassembly identity.
