# Parse-time type proofs

The static checker stamps proofs on the AST (checker/AST), and the VM's compiler reads them to choose
lanes that keep total runtime guards. The interpreter's own evaluation reads none; `parallel_transform`'s
rewritten body, which both backends run, reads the return proofs (section 5). Symbols are the anchors; this
note carries no line numbers.

## 0. Contract

Contract (rulings 1-3): (1) enforce at parse time whatever is provable, keep runtime enforcement wherever
var/dynamic values mix in; (2) hot reload never turns a proof into an error — a retyped field
assigns or is ignored through the existing migration/enforcement paths; (3) dynamic users pay
the unproven price, consistently typed users pay once at parse.

Soundness principle (the one sentence every consumer obeys): **a proof selects a lane; a total
runtime guard makes the lane sound.** Guard failure = the general code runs, byte for byte. There
is therefore no path where a proof produces an error the unproven path would not; stale proofs (hot
reload, re-stamp races, frozen vm flags) can only cost the lane.

Byte-parallel contract: for every program, both backends with `type_proofs(true)` and
`type_proofs(false)` produce identical results, identical printed output, identical error text,
identical `type_of`/`type_info` observations. Section 7's differential gate pins it.

Terms. *Certain*: a static type for which the payload guard is total (section 4.2). *same_type*:
target and RHS certain and identical.

## 1. The identity guard — `include/jaiscript/detail/type_proofs.hpp` (header-only)

`same_type_payload(v, target)`: TRUE iff the conversion for `target` is the identity on `v`
(deref-normalized). It mirrors the typed-slot table's identity rows
(`detail::convert_for_typed_slot_core`: same-base scalars/strings, the exact class-name tag); a
non-any tag must name the base. any/null/array/map/shared_ptr/weak_ptr/reference/function are never
identity. The kernel static_asserts `TYPEID_INT==1, TYPEID_FLOAT==2, TYPEID_STRING==3, TYPEID_CHAR==4,
TYPEID_BOOL==5, TYPEID_OBJECT==8`.

The VM runs it on every value, proof or not, in front of the conversions whose identity rows it
mirrors (section 6.2). It is a guard, not a proof consumer: the conversion it skips is the identity
on the value in hand.

## 2. AST fields — `include/jaiscript/detail/ast.hpp`

Constants: `proof_none = 0`, `proof_same_type = 1`, and the operand classes
`operand_class_unknown` (0), `int_int` (1), `float_float` (2), `int_float` (3), `float_int` (4),
`string_string` (5), `bool_bool` (6), `object_element` (7: a subscript whose container the checker
proved an array of class instances) and `vec_element` (8: a subscript whose container the checker
proved a math vector buffer).

Per node (all `mutable` so `infer(expression*)` and const callers may stamp):

| node | fields |
|---|---|
| `binary_expr` | `static_operand_class` |
| `assignment_expr` | `static_store_proof` (a local target only); `static_operand_class` (a compound op on a local: the target's declared kind against the value's; a plain `=` of a string value: `string_string`) |
| `call_expr` | `static_arg_proofs`, `proof_globals` (the program-scope names an argument proof read), `receiver_global`, `proven_callee_decl`, `proven_method_decl` |
| `return_stmt` | `static_return_proof` |

Rules: `ast_serializer.hpp` does not serialize any of these (jaibite re-derives them at load).
`parser.cpp`'s `ref_escape_marker` / `mark_transient_reads` (`run_ast_marking_passes`) do not touch
them. `structural_node_key` (ast_serializer-based) is unaffected because the serializer ignores
them.

## 3. Engine surface — `engine::note_class_shape_change()`

`++impl->check_surface_epoch_`: the static checker's surface moved, so cached entries re-check
(and re-stamp proofs) at their next execute. It runs on a script class's first registration
(`class_registry`, `first_definition`) and on a non-identical `redefine_class`; identical reloads
do not call it, so they do not re-check every cached entry.

## 4. Proof pass — `source/implementation/type_checker.cpp`

### 4.1 `ctype` carries a type_info

```cpp
struct ctype {
    ...
    kind_t kind = unknown;
    std::string_view cls{};
    const type_info* ti = nullptr;   // the interned type_info this ctype came from (null = kind only)
    bool tentative = false;          // kind read off a runtime value tag (engine globals): never a proof source
    bool element_proof = false;      // an element read from a declared typed container (operand-class stamps)
    bool alias_proof = false;        // a reference alias of a declared typed container
```
`ctype_from_type_info` sets `ti` to the type_info it was derived from — for
`jai_object_type`/`jai_shared_ptr_type` that type_info (so `ti->base_type` still tells value object
from handle); for `jai_reference_type` the recursive call's `ti` (the pointee). The diagnostics
compare `kind`/`cls`; `ti` is consulted by 4.2. Constructor-call results mint the object ctype
through `object_of(name)` (`ti = eng_.get_type_info_object(name)`, the parser's interned spelling).
`walk_variable_decl`'s `auto`+init entry inherits `ti` from `init`.

### 4.2 Certainty and same_type

```cpp
    // Proof certainty: the payload guard (detail::same_type_payload) is TOTAL for these kinds -
    // every dynamic value the site can hold is either the identity case or falls to the ladder.
    bool known_class(std::string_view name) { return classes_.count(name) || engine_class(name) != nullptr; }
    static bool certain_scalar(const ctype& t) {
        return (!t.tentative || t.element_proof) && (t.kind == ctype::int_k || t.kind == ctype::float_k || t.kind == ctype::bool_k ||
                                t.kind == ctype::char_k || t.kind == ctype::string_k);
    }
    bool certain_object(const ctype& t) {
        return !t.tentative && t.kind == ctype::object_k && t.ti &&
               t.ti->base_type == script_value_type::jai_object_type && !t.ti->type_name.empty() && known_class(t.cls);
    }
    bool certain(const ctype& t) { return certain_scalar(t) || certain_object(t); }
    // same_type: certain on both sides, identical kind, and for objects the SAME interned spelling.
    bool same_type(const ctype& target, const ctype& rhs) {
        if (!certain(target) || !certain(rhs) || target.kind != rhs.kind) return false;
        return target.kind != ctype::object_k || target.ti == rhs.ti;
    }
```
`element_proof` makes an element of a declared typed array certain (an `array<int>` holds ints); a map's
value stays tentative, because a missing key reads null.

NOT certain, by construction: `unknown`, `null_k`, `array_k`, `map_k`, a map's value, `function_k`,
`class_ref_k`, `enum_ref_k`, objects reached through a shared_ptr/weak_ptr/cpp_bound/any
type_info, every `var_entry.dynamic` (var, auto-without-init, reference decls, ref params,
ref/parallel range-for, catch vars, destructured names), global-tier-ambiguous names
(`global_tier_name`), `extra_globals_`, `engine_global_type` results (value-tag derived:
`tentative`, never a proof source), mixed ternaries, include results. `assignable`'s lenient rows are
conversions, never identity — `same_type` never consults `assignable`.

### 4.3 Checker state and the stamp helper

```cpp
    enum class proof_mode : uint8_t { leave, stamp, clear };
    proof_mode proofs_ = proof_mode::stamp;      // from check_options
    bool collect_diagnostics_ = true;            // from check_options; false -> diag() returns early
    template <class F, class V> void stamp(F& field, V value, V def) {
        if (proofs_ == proof_mode::stamp) field = value;
        else if (proofs_ == proof_mode::clear) field = def;
    }
```
`diag()` returns early when `!collect_diagnostics_`; `undeclared()` goes through `diag`. Proofs
are independent of diagnostics (a stamp never depends on a `diag` having fired). `stamping()` is
`proofs_ == proof_mode::stamp`.

### 4.4 Stamping rules per node

| id | node / method | condition → stamp |
|---|---|---|
| S1 | `assignment_expr`, local target — `infer_assignment` | plain `=` (a compound is `x = T(x op rhs)`, a separate opcode) `&& !local->dynamic && same_type(local->type, rhs)` → `static_store_proof = proof_same_type`, else `proof_none`. A compound on a local stamps `static_operand_class = operand_class_of(local->type, rhs)` (the fused compound's typed row). |
| S2 | `assignment_expr`, every other target (a field, an engine global, a subscript) | `proof_none`. incdec desugar nodes (`incdec_desugar`/`postfix_old_result`) are compound → `proof_none` by S1's `op` test. |
| C1 | `call_expr` — `stamp_call_proofs` | One statically resolved same-script callee (`ov.size() == 1`; `fn->body && !fn->is_coroutine && arity_range(fn) == {n, n}`). Per by-value argument `i`: `proof_same_type` iff the parameter is untyped/var or `same_type(ctype_from_type_info(param.type), args[i])`; reference parameters get none. `proof_globals` lists the program names an argument proof read (an includes-free unit proves every argument; otherwise `certain_argument` decides per argument and records its dependencies). `proven_callee_decl` is set for a free callee (the inliner's identity). Every other callee shape (callable local, engine ctor/function, `new_expr`, member on non-object): `static_arg_proofs`, `proof_globals` cleared. `functions_` may hold several decls for one name ("later replaces earlier") → `size()==1` refuses those. |
| R1 | `return_stmt` — `check_return` | `r->value && !r->binds_reference && !fn.is_coroutine && fn.ret && ret = ctype_from_type_info(fn.ret); ret.kind != null_k && same_type(ret, value)` → `static_return_proof = same_type`; else `proof_none`. Lambdas share `check_return` and are stamped too. |
| B1 | `binary_expr` — `infer` | Non-subscript: `operand_class_of(l, r)`: int,int→1 float,float→2 int,float→3 float,int→4 string,string→5 bool,bool→6 else 0 (tentative operands 0; char pairs 0, char is promoted by `binary_result`). Subscript: the proven element kind of a declared container — object elements → 7, math vectors → 8, a proven int/float element → 1/2, else 0. `static_operand_class` selects the VM compiler's typed row; the row's runtime guard keeps it sound. |

Recheck idempotence: every rule writes its field on EVERY visit (stamp or `proof_none`), so a
later re-check with a moved surface can only add or drop proofs, never leave a stale one that the
pass no longer derives.

### 4.5 `run_static_check` API — `include/jaiscript/detail/type_checker.hpp`

`detail::check_options` carries `diagnostics` (false: the report stays empty), `proofs`
(`proofs_t { leave, stamp, clear }`), `stats` (a `proof_stats*` the stamp/clear passes fill:
`stores`, `args`, `returns`, `deferred`), the program closure tables (`program_names`,
`program_classes`, `program_functions`, `program_globals`, `program_enums`), `deferred_classes`
and an `observer`. `run_static_check(eng, decls, extra_globals, opts)` builds the
`static_checker`, which sets `proofs_`/`collect_diagnostics_` from `opts`.

### 4.6 Engine hook points — `source/implementation/engine.cpp`

State: the engine's `type_proofs_` (default true; `engine::type_proofs(bool)` sets it and nothing
else), `script_cache_entry::proof_epoch` and `jaibite::proof_epoch_` (`UINT64_MAX` = the nodes
carry no proofs).

`checked_report` (the cached entry point, through `check_entry`) and `check_bite` run the
diagnostics walk (proofs left alone, `proofs_t::leave`) when diagnostics are wanted and stale, then
a proof pass whenever the switch, the surface epoch, auto typing or the lowering view moved; the
proof pass alone reads the program's globals and types the auto slots. Every execute calls
`checked_report`, and the proof pass rides it. A host's instance-variables call checks fresh for
diagnostics only (`check_with_instance_vars`) and never re-stamps. `check_bite` runs on
`engine::jaibite`, `execute(jaibite&)` and a bite load: "a bite is checked once" holds for
diagnostics (a trusted-clean loaded bite keeps empty diagnostics), while proofs re-stamp on surface
moves.

Every hook runs after `run_ast_marking_passes` (the parse tail, the jaibite load, bite creation).

`check_surface_epoch_` moves on `add_global`, `register_overload_impl`, `add_class_impl`,
`register_template_type` and `note_class_shape_change` (section 3).

Cost: one checker walk per unique source per surface epoch; `type_proofs(false)` clears the stamps.

## 5. Interpreter

The interpreter's own evaluation reads no proof: every store, declaration, argument and return runs its
conversion, whose identity rows return the value unchanged.

`parallel_transform` and `parallel_reduce` graft a read-only function's body into their chunk loop and
rewrite each top-level `return e;` into a store of `cast_expr(e)` whose `return_of` names the function, so
the store converts with the function's own return conversion and text, the text the transform's call form
raises (a worker's symbolizer is frozen, so both spell the slotless `Cannot return null from a function declared
-> int`). The rewrite reads `static_return_proof` and leaves
out the conversion for a proven same-type return; both backends run the rewritten body, so this is the one
proof the interpreter observes. It has no runtime guard: it relies on the proof being certain (section
4.2), and a stale proof of the cached wrapper would land the unconverted value.

## 6. VM — `source/implementation/vm/vm_compiler.cpp`, `vm_inline.cpp`, `vm_backend.cpp`

### 6.1 Proof consumers (the compiler)

| proof | carrier | consumer |
|---|---|---|
| `static_store_proof` | `store_flag_same_type = 32` in `op_store.c` (and `op_concat_store`'s identifier mode, `op_t_store_mov`'s site), mirrored into `fused_binary_dst_proto.flags` at the peepholes | the typed store lowering: a fused binary landing in a frame slot (`op_binary_fused_store`, no result, no ref alias, not `::`) becomes a typed row with a `k_typed_dst_store` destination; `typed_land_int`/`typed_land_float` check the slot's tag and fall back to the general store on a miss |
| `static_operand_class` | `fused_binary_proto::typed_class`, `compound_fused_proto::typed_class` | the typed arithmetic and compare rows, each guarding its operands' storage |
| `static_arg_proofs`, `proof_globals`, `receiver_global`, `proven_callee_decl`, `proven_method_decl`, `static_return_proof` | the AST, read when the inliner grafts | the inliner's admission (a graft's parameters and result take the proven kinds); the grafted code keeps the general guards |

Freshness: instruction-borne flags (`store_flag_same_type`) never gain proofs after first compile —
toggling `type_proofs(true)` later or a surface move that adds store proofs only benefits fresh
chunks; correctness is unaffected (guards). No chunk invalidation happens on a re-stamp.

### 6.2 Identity lanes (no proof)

Each runs on every value; the conversion it skips is the identity on the value in hand.

- Slot store — `store_popped_value_full`: an int/float over a held payload of its own storage under
  the identical tag is a plain copy (a held object, which a var store may assign in place, never
  takes the lane); otherwise `same_type_payload(value, slot_type)` skips `enforce_type_compatibility`.
  The held-object store and a user `operator=` run ahead of that skip.
- Environment store: `same_type_payload(value, target_type)` skips `enforce_type_compatibility`.
- Declaration — `exec_decl_var`: `same_type_payload(value, effective_decl_type)` skips the conversion.
  `op_t_decl_const` lowers only a constant its declaration takes as is (an int into a float
  declaration folds to a float constant at compile time); any other pair keeps `CONST + DECL_VAR`.
- Argument binding — `bind_parameters`: an untyped parameter, or `same_type_payload(arg.deref(),
  param.type)`, takes `try_convert_for_parameter`'s identity returns (the arg; the derefed arg for an
  exact-name object).
- Return — `return_with_result`: the deref-only route takes a payload the declared type names
  (`same_type_payload(result.deref(), rec.return_type)`) as well as `return_mode_converted`;
  `convert_return_at_site` (`op_return_convert`, the native-entry / fiber doors) skips the table the
  same way.

## 7. Tests

`foundry::make_engine()` honors `--backend=vm`, so every suite below runs under both backends.
Tests that need proofs on AND off in one process build their engines explicitly. Every proof test
asserts equality against a `type_proofs(false)` twin, never a literal alone.

- `source/tests/semantics/strong_types_tests.cpp`: the batteries `proof_battery_same_type_stores`,
  `proof_battery_not_certain`, `proof_call_arg_identity_guard`, `proof_member_second_execute`,
  `proof_stats_fire_and_kill_switch` — each snippet 4 ways {interp,vm}×{proofs on,off}, outputs and
  error texts equal.
- `source/tests/semantics/static_check_tests.cpp` (suite "Static Type Checking"):
  `proofs_default_on_and_switch_roundtrip`, `proofs_stamp_with_counters`,
  `proofs_not_stamped_from_uncertain_types`, `proofs_off_strips_cached_entry_and_on_restamps`,
  `proofs_restamp_after_registration_and_hold_across_identical_reload`,
  `proof_jaibite_paths_restamp`, `proof_kernel_same_type_payload`,
  `proof_battery_results_equal_with_proofs_on_and_off`,
  `proof_consumers_retype_on_reload_assigns_never_errors`, and `proof_differential_gate` (fuzz
  seeds run on four engines {interp,vm}×{proofs on,off}; segment results, printed output and
  liveness must be identical).
- `source/tests/language/hot_reload_correctness_tests.cpp` (suite "Hot Reload Correctness"): each
  test stores into a typed field via BOTH spellings (`this.f = v` / `f = v`) and reads it back,
  three times before the reload, then reloads and calls again: `proof_field_retype_int_to_string_assigns`,
  `proof_field_retype_float_to_int_truncates`, `proof_field_retype_string_to_int_resets_default`,
  `proof_field_retype_int_to_var_accepts_anything`, `proof_field_removed_then_read_errors_like_unproven`,
  `proof_field_gains_custom_getter_on_reload`, `proof_field_gains_custom_setter_on_reload`,
  `proof_field_access_label_change_on_reload`, `proof_base_reload_invalidates_derived_proofs`,
  `proof_class_gains_base_on_reload`, `proof_class_becomes_open_on_reload`,
  `proof_same_source_reexecute_vm_chunk_reuse`, `proof_extend_reclass_misses`.
- `source/tests/vm/vm_backend_tests.cpp` (suite "VM Backend"):
  `proof_vm_lanes_equal_with_proofs_on_and_off`, `proof_carriers_survive_recompile_and_frozen_flags`,
  `proof_ident_nodes_and_call_node_fill`.
- `jaiscript_fuzz --no-proofs` runs both backends with `type_proofs(false)` (`run_config::proofs`
  in fuzz_harness.hpp); the "Differential Fuzz Smoke" suite's
  `fixed_seeds_no_new_divergence_no_proofs` holds interp-vs-vm parity in both proof states.

## 8. Not proven

- Conversion proofs (int→float widening, bool/string coercions, null→object placeholder, derived→base, ctor conversions): `same_type` only.
- Container same_type / covariant `array<Base> = array<Derived>` proofs; typed-map proofs.
- Field reads and writes: the member IC, the this-field IC and the frame layout serve them.
- Proven callee dispatch for the identity bind (skipping overload resolution / method IC by proof); argument proofs for callable locals, engine functions, `new_expr`, coroutine callees, defaulted or reference parameters.
- Same-type proofs on compound assignment / `++`/`--` desugars, `store_through` references, `environment::assign`'s method-env fallback, static fields, `super.f`, `?.` receivers, engine globals, hybrid (cpp-base) classes, extended layers, transparent wrappers.
- Worker (parallel) frames.
- Invalidating compiled vm chunks on re-stamp; serializing proofs in `.jaibite`; a per-call re-stamp on the instanceVars path.
- Any change to error texts, `type_of` results, tag stamping, clone/value semantics, or reference normalization.
