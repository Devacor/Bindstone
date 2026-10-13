> **AS-BUILT ADDENDUM (archived 2026-07).** Mandatory re-baseline pass for
> `thin_value_spec.md`, executed 2026-07-04 before implementation began; the fold landed as
> commits `2c596175..6871f057`. Anchors here supersede the spec's but still describe the
> PRE-FOLD tree at `86e9c909` — use them to read the fold's diffs, not the current source.

# thin_value_rebaseline.md — mandatory re-baseline addendum (D13 / spec §7 audit step 0 / §15 step 0.5)

Executed 2026-07-04 against HEAD **86e9c909** ("sol2/Lua comparison benchmarks ... 1454 green both backends").
This addendum SUPERSEDES the spec's line anchors and site counts. The spec's semantic rulings (D-table, S-rules, §12 deltas) remain binding.

## 0. Landing-commit state

- HEAD: `86e9c909`; prior two: `8d198351` (by-ref BST benchmarks, 1435), `d0076424` (reentrant exception scrub, 1412). The spec's snapshot commit was `94311244` (1394) — THREE commits of drift, and the concurrent ref-binding workflow's uncommitted edits observed at spec time have all LANDED (ref_lvalue.hpp is now tracked, added in `ded92fe0`).
- Working tree clean except pre-existing noise: `Assets/Atlases/*.png` (7 files) + untracked `UpgradeLog.htm`. No JaiScript source is dirty — the "moving baseline" has stopped moving; this is a good landing commit.
- Test counts (per HEAD commit message + suite structure): Debug **1412/1412** (76 suites), Release BENCHMARKS **1454/1454** (80 suites incl. Squirrel + Lua comparison suites), both backends. Spec's "~1385/1394" is stale.
- Files created after 2026-07-04 spec snapshot, all audited: `include/jaiscript/detail/ref_lvalue.hpp` (ded92fe0), `source/tests/performance/sol2_comparison.cpp` (86e9c909), `include/jaiscript/detail/body_walker.hpp` (0c7cd4f9). sol2_comparison.cpp and body_walker.hpp have ZERO hits for raw_storage_index / TYPEID / cpp_bound / std::get / current_type — public-API only, no change-list impact. ref_lvalue.hpp: see §3.

## 1. Grep re-counts (audit closure population at 86e9c909)

| Grep | Spec snapshot | Now | Files |
|---|---|---|---|
| #1 `unchecked_as_int_ref\|unchecked_as_float_ref` | — | 43 | vm_backend 18, interpreter 21 (incl. range-for :7959/:7973/:8016), value.hpp 2 (defs :406/:420) |
| #2 `unchecked_as_string_ref` | — | 21 | interpreter 19 (17 = string builtin methods :1398-1768, receiver-dispatched; 2 = compound in-place :3878 + vm :2852), value.hpp def :429 |
| #3 `raw_storage_index` | 169 / 11 files | **169 / 11 files** (same total, sites moved) | interpreter 40, interpreter_dispatch 38, vm_backend 61, value.hpp 17, ref_lvalue.hpp 4, vm_compiler 3, interpreter.hpp 2, value.cpp 1, disassembler 1, + non-production: test_fallthrough_debug.cpp 1, docs/REFERENCE_SEMANTICS_PLAN.md 1 |
| #4 `std::get_if<TYPEID` | 21 | **19** (value.hpp 13, value.cpp 6) |
| #4b `std::get<` | 59 | 69 raw hits (value.hpp 50, value.cpp 9, interpreter 5, vm_backend 5, ref_lvalue 0) — includes non-storage_ gets; re-filter at audit time |
| #5 `current_type()\|storage_type()` | — | 36 / 7 files; NEW consumers: class_definition.hpp:82/:1021 (deref().current_type() equality — S3-safe, no get pairing), ref_lvalue.hpp:318 (type-name for error text — S3-safe) |

## 2. Corrected anchors for the spec's named change-list items (spec → HEAD 86e9c909)

**value.hpp**: members to delete :1523-1524 (was :1511-1512); move ctor/assign member lines :142-162 (was :137-164); `current_type()` :227 (was :222); is_* family :273-307 (was :268-282), `is_reference` :287, `is_cpp_bound` :288, `is_non_owning_object` :290-296, `get_cpp_bound_ptr` :299, `get_cpp_bound_as` :302-305; unchecked accessors :376-435 (bool :378, int :385-401, int_ref :406, float :411-415, float_ref :420, string_ref :429, char :434); as_*_ref family **:501-539 EXACT match to spec** (int :501, float :509, bool :517, char :525, string :533 — S9 lands here); `as<T&>` object extraction :587-598; checked_as_* :640-735; checked_as<T> fast paths :760-860; shared_ptr-extraction refusal :1242-1244; by-value copy from bound ptr :1304-1308; TYPEID constants :1484-1497; variant `using storage` :1505; `set_shared_ptr` :1659, `get_shared_ptr_value` :1668.
**value_impl.hpp**: `make_cpp_bound` :30 (unchanged).
**value.cpp**: clone :316 (was :294); assign_through :628 copy / :704 move (was :612-684); operator== :740 (was :686); <=> :766 (was :712); identity lambda :797-813, its raw-index switch :798 (was :743-753) — S7 unchanged.
**function_binder.hpp**: :290 `as_int_ref`, :294 `as_string_ref` — EXACT match, §7.26 stands.
**interpreter.hpp**: is_truthy :874 (switch :878), to_numeric :900 — unchanged (§7.13).
**interpreter.cpp**: visit_literal_expr :2704 (spec :2701, unchanged item); generic unary switch oi :3477, `-` else ~:3491, `~` gate :3502-3504 (§7.23a); ++/-- env-var fast path :3519-3550, type()-switch :3525 (§7.23); compound ultra-fast raw-index-gated paths :3688/:3691/:3732/:3773/:3778 (safe, S1); identifier-compound in-place switch leftIdx/rightIdx :3863-3864, switch :3866-3942, type_mismatch elses :3880/:3896/:3912/:3935 (see NEW-D); implicit-this class-field compound ci/ri :3992-3993 (§7.24a; spec :3985); member-compound ci/ri :4098-4099 (§7.24a); general compound path + div/mod-zero checks :4227-4260, ri checks :4240/:4250 (see NEW-C); evaluate_arithmetic def :5419, li/ri :5426-5427 (§7.23b); same_as member-fallback lambda :6428-6480, idx :6452-6453, early-out :6455, switch :6460, `default → make_value(false)` :6472-6475 (§7.24b); range-for/cfor gates :7844-8057 (raw-index, safe).
**interpreter_dispatch.cpp**: 9 handlers :43-526 essentially as spec (add li_raw :44, subtract :104, multiply :154, divide :204, modulo :266, less :324, less_equal :375, greater :426, greater_equal :477); spaceship raw-index int fast path :661 (safe).
**vm_backend.cpp**: is_truthy def :938, switch :939 (§7.15); 9 handlers li_raw at :1170/:1226/:1275/:1324/:1385/:1442/:1490/:1538/:1586 (§7.20); spaceship fast path :1733 (safe); evaluate_arithmetic def :1984, li/ri :1988-1989 (§7.16a); exec_compound_store def :2751, env-var in-place switch leftIdx/rightIdx :2837-2838 with unchecked_*_ref bodies :2843-2905 (§7.16); exec_incdec def :3025, type()-gated env-var fast path :3033-3060, this-field fallback ti :3069 (§7.17); exec_unary def :3091, oi :3095 (§7.15a); exec_index_compound def :3577, ri zero-checks :3624/:3633 (see NEW-C); same_as lambda ~:4573-4626, idx :4601-4602, early-out :4603, switch :4607, `default → script_value(false, eng)` ~:4619 (§7.15b); exec_member_compound def :5022, ci/ri :5061-5062 (§7.20a); param-bind arg index :7907 (was :7769, guarded, safe); cfor int-specialization gates :3314/:3321/:3337/:3382/:3395 — :3321 and :3337 carry explicit `is_cpp_bound()` exclusions (keep, §7.19).
**strong_ptr.hpp**: untouched by the drift; converting ctors still :119-134; rung 2a plan unaffected.
**ref_lvalue.hpp (§7.27)**: the bound-int index gate is at **:245-248** (spec said :263-266): `index_derefed.value().raw_storage_index() != TYPEID_INT → ref_non_lvalue_error()`. Fix as spec prescribes: gate on `is_int()` (value already deref'd at :243), decode at :248 works via S5. The literal-index path :236-237 and the literal shape check :44 read literal_expr values — never bound, no edit.

## 3. NEW change-list items the spec lacks (all bucket 3, all get the spec's own S8/`bound_decoded_temp()` conventions)

- **NEW-A (VM implicit-this field compound)** — vm_backend.cpp **:2955-2956** (ci/ri reads inside exec_compound_store's implicit-this.member fallback, :2918-3020; type_mismatch defaults :2972 et seq.). The VM twin of §7.24a's interpreter implicit-this switch; spec's H6 round only named the interpreter side. Today a bound-alias class field decode-reads live via the shadow and set_field stores detached (green); post-fold ci==14 falls to type_mismatch. Fix: S8 in-place normalization of `currentValue`/`rightValue` immediately after the custom-op block (:2953), before the ci/ri reads; `set_field` keeps §12.5 detach-and-replace. Byte-parallel with §7.24a.
- **NEW-B (interpreter implicit-this field ++/--)** — interpreter.cpp **:3553-3586** (incdec identifier fallback: ti raw-index switch :3564-3580, erroring else :3579 `invalid_numeric_operand`). The interpreter twin of §7.17's VM this-field fallback; spec named only the VM side. Fix: S8 in-place normalization of `currentVal` before the ti read; set_field keeps §12.5. Byte-parallel with §7.17's fallback.
- **NEW-C (div/mod-by-zero raw-index fast checks — error-surface hazard)** — three sites where `/=` and `%=` pre-check the rhs by raw index before delegating to evaluate_arithmetic: interpreter general compound **:4240-4252** ("Division by zero" / code division_by_zero + "Modulo by zero" / code division_by_zero), VM exec_index_compound **:3624-3633** (same texts/codes), ref_lvalue.hpp shared ref-compound helper **:604-616** (same texts/codes; `arith` = the backend's evaluate_arithmetic, so its §7.16a/§7.23b prologue reaches these callers). A bound-int-zero rhs passes these checks TODAY (shadow index INT + live decode); post-fold index 14 skips them and the error surfaces inside evaluate_arithmetic instead — for `%=` that is code **modulo_by_zero** text **"Division by zero"** (interpreter :5461-5464) vs today's code **division_by_zero** text **"Modulo by zero"**: different code AND text, breaking byte-identical error parity. Fix: S8 in-place normalization of `rightValue` immediately before each op switch (3 sites; the interpreter/VM pair byte-parallel; ref_lvalue.hpp is shared by both backends so one edit covers both).
- **NEW-D (interpreter identifier-compound in-place switch — firm anchor for §7.23b's NOTE)** — interpreter.cpp **:3859-3944**: the env-slot compound in-place mutation switch (leftIdx/rightIdx :3863-3864, unchecked_*_ref writes, type_mismatch elses). This is the interpreter twin of VM §7.16 and the site where `boundInt += 5` mutates the dead shadow today. The spec's §7.23b NOTE anticipated it without an anchor; treat it as a named item: explicit `is_cpp_bound()` bound branch BEFORE the switch → decode-read, `ints::try_*`, `assign_through` (write-through §12.1), push per expression_result_needed_. Byte-parallel with §7.16.

## 4. Vanished / reclassified / confirmed-safe

- **Dead code (bucket 1, no edit):** `interpreter::evaluate_comparison` (:5525, raw-index string gate :5583-5585), `evaluate_logical` (:5653), `evaluate_bitwise` (:5678, raw-index gate :5680-5686) have **zero call sites** (declared interpreter.hpp:815-817, never invoked). Live comparison/bitwise all route through interpreter_dispatch/VM handlers as the spec says. Do not add prologues to corpses; optionally delete in a separate cleanup.
- **binary_fast_shape** (vm_backend.cpp:1854, li/ri :1856-1857) and the **fused compare fast path** (:3231-3232): index-gated, return false / fall through to `binary_general` → the S8-prologued handlers — bucket 1, no edit (new functions vs spec's snapshot, both safe by construction).
- **Two additional same_as implementations** exist in the shared builtin-method registries (interpreter.cpp:1893 weak_ptr variant, :1997 shared_ptr variant): is_*-gated + holder identity, NO raw-index switch, no wrong-constant default reachable by bound primitives — bucket 1. The spec's §7.15b/§7.24b pair (VM :4601, interpreter :6452 member-fallback lambdas) remains the complete bucket-3 same_as population.
- **visit_binary fast paths** interpreter.cpp :2870-2871/:3000-3001/:3103-3104: index-gated, fall through to pre_fetched slow path → dispatch handlers — bucket 1.
- **vm_compiler.cpp** :590/:609/:630 literal-index gates — literals never bound, bucket 1.
- Grep #1 closure: every unchecked_as_int_ref/float_ref caller is either raw-index-dominated (safe) or one of the two type()-gated incdec fast paths (§7.17/§7.23 bound branches) or the two in-place compound switches (§7.16/NEW-D).
- Grep #2 closure: string builtin methods (:1398-1768) receive `self` via receiver dispatch; post-S5 `unchecked_as_string_ref` decodes the LIVE bound string — the sanctioned §12.2 write-through. Verify at implementation time that string-method receiver dispatch selects the string registry via type()/is_string (S2) rather than raw index.

## 5. Bucket-3 census at HEAD (complete list an implementer must touch)

2× is_truthy (§7.13/§7.15) · 18 arithmetic/ordering handlers (§7.20/§7.22) · 2× evaluate_arithmetic (§7.16a/§7.23b) · 2× generic unary (§7.15a/§7.23a) · 2× same_as member-fallback lambdas (§7.15b/§7.24b, wrong-constant defaults) · VM exec_compound_store env-var switch (§7.16) + interpreter twin (NEW-D) · VM exec_incdec env fast path + this-field fallback (§7.17) + interpreter twins (§7.23 + NEW-B) · VM implicit-this compound (NEW-A) + interpreter implicit-this compound (§7.24a) · 2× member-compound (§7.20a/§7.24a) · 3× div/mod-zero pre-checks (NEW-C) · ref_lvalue index gate :245 (§7.27). Everything else in grep #3 is bucket 1 or a named safe guard.

## 6. Pre-refactor performance baseline (Release BENCHMARKS, HEAD 86e9c909, min-of-3, integer µs/iter ±50%)

Suite: "Performance Benchmarks". Build fresh (exe 2026-07-04 19:29, no newer sources). Raw logs: scratchpad/bench_{interp,vm}_{1,2,3}.txt.

| Benchmark | interp | vm |
|---|---|---|
| Integer Addition | 0 | 0 |
| Float Multiplication | 0 | 0 |
| Variable Operations | 1 | 0 |
| Function Calls | 2 | 1 |
| Array Push/Pop | 6 | 5 |
| Map Insert/Lookup | 2 | 2 |
| Class Creation | 14 | 13 |
| Method Invocation | 12 | 9 |
| For Loop (100) | 13 | 9 |
| String Concatenation | 1 | 1 |
| Engine Creation | 68 | 67 |
| Stdlib Registration | 290 | 284 |
| Complex Expression | 0 | 0 |
| Class Inheritance | 271 | 265 |
| Hot Loop (1000) | 134 | 117 |
| Simple Compound Assignment (x100) | 3 | 1 |
| Variable Lookup Heavy | 2 | 1 |
| auto: Simple Array [10 ints] | 2 | 1 |
| var: Simple Array [10 ints] | 1 | 1 |
| auto: 2D Array [[5x5]] | 4 | 3 |
| var: 2D Array [[5x5]] | 3 | 2 |
| auto: 3D Array [[[2x2x2]]] | 3 | 2 |
| var: 3D Array [[[2x2x2]]] | 2 | 2 |
| auto: Homogeneous Map {5} | 2 | 2 |
| var: Heterogeneous Map {5} | 2 | 2 |
| auto: Nested Map 2 levels | 4 | 3 |
| var: Nested Map 2 levels | 3 | 2 |
| auto: Mixed Array+Map 3 levels | 5 | 4 |
| var: Mixed Array+Map 3 levels | 4 | 3 |
| Integer Addition [jaibite] | 0 | 0 |
| Function Calls [jaibite] | 2 | 1 |
| Method Invocation [jaibite] | 11 | 8 |
| For Loop (100) [jaibite] | 12 | 8 |
| Hot Loop (1000) [jaibite] | 134 | 111 |
| Fibonacci(15) [recursion] | 2035 | 840 |
| Recurse 10 Locals (depth=15) | 61 | 41 |
| BST insert/sum/rotate (15 nodes) | 953 | 895 |
| BST shared_ptr insert/sum (15) | 366 | 331 |
| BST by-ref insert/sum (15) | 291 | 249 |
| Ref Param Pass-Through relay->inc (x100) | 166 | 89 |
| String Copy (Long String) | 2 | 1 |
| String Passing to Function | 7 | 5 |
| String Method Chaining | 3 | 2 |

Harness quirk: filtering to "Performance Benchmarks" exits 1 with "No tests matched filter" because the suite contains benchmark entries, not check-based tests — the benchmark block still runs and prints. Post-rung comparisons must use min-of-3 the same way; the real gate stays the ns micro-bench (spec §11).
