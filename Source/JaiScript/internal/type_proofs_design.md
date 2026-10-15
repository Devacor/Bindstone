# Parse-time type proofs

The static checker stamps proofs on the AST (checker/AST), and both backends consume them behind
total runtime guards (interpreter, vm). Symbols are the anchors; this note carries no line numbers.

## 0. Contract

Contract (rulings 1-3): (1) enforce at parse time whatever is provable, keep runtime enforcement wherever
var/dynamic values mix in; (2) hot reload never turns a proof into an error — a retyped field
assigns or is ignored through the existing migration/enforcement paths; (3) dynamic users pay
the unproven price, consistently typed users pay once at parse.

Soundness principle (the one sentence every consumer obeys): **a proof selects a lane; a total
runtime guard makes the lane sound.** Every consumed proof sits behind a guard that is TRUE only
when the skipped conversion is provably the identity on the value actually in hand
(`detail::same_type_payload`) or when the class object, its field-shape epoch and a present cell
all match (`detail::proven_field_cell`). Guard failure = the unproven code runs, byte for byte.
There is therefore no path where a proof produces an error the unproven path would not; stale
proofs (hot reload, re-stamp races, frozen vm flags) can only cost the skip.

Byte-parallel contract: for every program, both backends with `type_proofs(true)` and
`type_proofs(false)` produce identical results, identical printed output, identical error text,
identical `type_of`/`type_info` observations. Section 7's differential gate pins it.

Terms. *Certain*: a static type for which the payload guard is total (section 4.2). *same_type*:
target and RHS certain and identical. *Field proof*: `{proven_class, proven_slot, proven_epoch,
proven_field_type, proven_field_write}` on a `member_expr` / `identifier_expr`. *Shape epoch*:
`class_definition::field_shape_epoch()` (section 3.1).

Design points: (a) `proven_epoch` is `field_shape_epoch()`, not `method_epoch()` —
`method_epoch` bumps on every execute of a `class_decl` (identical reload, `store_field_defaults`),
so a `method_epoch` guard would make every field proof in a self-defining script miss forever; the
READ guard also includes the epoch (a reload can add a custom `_get_` or access labels).
(b) Containers are not certain (`ctype` carries no element type for this purpose;
`enforce_container_boundary` is not the identity). (c) Two extra stamps:
`proven_field_type`/`proven_field_write` on field proofs, `proven_callee_body` on `call_expr`.
(d) `static_operand_class` selects the VM compiler's typed row; the row's runtime guard keeps it
sound.

## 1. Shared kernel — `include/jaiscript/detail/type_proofs.hpp` (header-only)

Included by `interpreter.cpp` and `vm_backend.cpp` (not by `ast.hpp`; the proof constants live in
`ast.hpp`). Both backends call these verbatim — never re-implement the guards inline.

- `same_type_payload(v, target)`: TRUE iff the unproven conversion for `target` is the identity on
  `v` (deref-normalized). It mirrors the typed-slot table's identity rows
  (`detail::convert_for_typed_slot_core`: same-base scalars/strings, the exact class-name tag); a
  non-any tag must name the base. any/null/array/map/shared_ptr/weak_ptr/reference/function are
  never identity.
- `retags_on_field_write(declared)`: `detail::field_store_value` stamps `set_type_info(declared)` on
  scalar/string fields and leaves same-class objects untouched; a proven field store repeats that
  retag.
- `proven_field_instance(receiver, cls, epoch)`: the receiver guard — a class_instance wrapper whose
  class object IS `cls` and whose field shape has not moved since the stamp; null otherwise. It
  never dereferences a non-class holder.
- `proven_field_cell(receiver, cls, epoch, slot, inst_out)`: the read guard — the instance guard
  plus a present cell (absent/raw-invalid → null → the ladder runs, which materializes defaults on
  the bare path / reads class defaults on the member path).

The kernel static_asserts `TYPEID_INT==1, TYPEID_FLOAT==2, TYPEID_STRING==3, TYPEID_CHAR==4,
TYPEID_BOOL==5, TYPEID_OBJECT==8`.

## 2. AST fields — `include/jaiscript/detail/ast.hpp`

Constants: `proof_none = 0`, `proof_same_type = 1`, and the operand classes
`operand_class_unknown` (0), `int_int` (1), `float_float` (2), `int_float` (3), `float_int` (4),
`string_string` (5), `bool_bool` (6), `object_element` (7: a subscript whose container the checker
proved an array of class instances) and `vec_element` (8: a subscript whose container the checker
proved a math vector buffer).

Per node (all `mutable` so `infer(expression*)` and const callers may stamp; raw pointers on
purpose — script class_definitions are engine-lifetime, `impl->classes` never erases, hot reload
reuses the object):

| node | fields |
|---|---|
| `identifier_expr` | `proven_class`, `proven_slot`, `proven_epoch`, `proven_field_type`, `proven_field_write` |
| `binary_expr` | `static_operand_class` |
| `assignment_expr` | `static_store_proof`; `static_operand_class` (a compound op on a local: the target's declared kind against the value's) |
| `call_expr` | `static_arg_proofs`, `proof_globals` (the program-scope names an argument proof read), `receiver_global`, `proven_callee_body`, `proven_callee_decl`, `proven_method_decl` |
| `member_expr` | `proven_class`, `proven_slot`, `proven_epoch`, `proven_field_type`, `proven_field_write` (beside `mcache`, a `method_site_cache {cls, epoch, flags, ...}`) |
| `return_stmt` | `static_return_proof` |
| `variable_decl` | `static_init_proof` |

The guard compares typed `class_definition*` pointers (`method_site_cache::cls` is a `const void*`;
proofs do not copy that).

Rules: `ast_serializer.hpp` does not serialize any of these (jaibite re-derives them at load).
`parser.cpp`'s `ref_escape_marker` / `mark_transient_reads` (`run_ast_marking_passes`) do not touch
them. `structural_node_key` (ast_serializer-based) is unaffected because the serializer ignores
them.

## 3. class_definition / engine

### 3.1 `class_definition::field_shape_epoch()` — `include/jaiscript/core/class_definition.hpp`

Beside `method_epoch_` and `bump_method_epoch`:

```cpp
    // Field-shape epoch (type proofs): moves only when a field proof's facts can change - field
    // set, declared types, resolution chain, access labels, custom accessors. Unlike
    // method_epoch it does NOT move on an identical reload. Propagates to derived like
    // bump_method_epoch (same engine-lifetime/no-ABA contract).
    uint64_t field_shape_epoch() const { return field_shape_epoch_; }
    void bump_field_shape_epoch();   // ++field_shape_epoch_, then every live derived class
    ...
    uint64_t field_shape_epoch_ = 1;
```

Bump sites: `add_field` (both overloads); `replace_field_declared_types` when a declared type
changed (regardless of `mark_changes`: "declared type changed ⇒ epoch moved"); `set_parent`,
`add_parent`, `set_parents` when the chain changed; `set_cpp_base_class` when the base changed;
`register_property_setter`, `register_property_read_only`, `register_property_getter`,
`register_property_member`; `set_nonpublic_members` when the labels changed; `add_method_by_id`
and `add_method` when a custom `_get_`/`_set_` or a field-named method changes the field ladder;
`redefine_class` on its full path (NOT on the structurally-identical shortcut). `reclass` needs
nothing (the class pointer changes).

### 3.2 `engine::note_class_shape_change()`

`++impl->check_surface_epoch_`: the static checker's surface moved, so cached entries re-check
(and re-stamp proofs) at their next execute. It runs on a script class's first registration
(`class_registry`, `first_definition`) and on a non-identical `redefine_class`; identical reloads
do not call it, so they do not re-check every cached entry.

## 4. Proof pass — `source/implementation/type_checker.cpp`

### 4.1 `ctype` carries a type_info (`source/implementation/type_checker.cpp`)

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
NOT certain, by construction: `unknown`, `null_k`, `array_k`, `map_k`, `function_k`,
`class_ref_k`, `enum_ref_k`, objects reached through a shared_ptr/weak_ptr/cpp_bound/any
type_info, every `var_entry.dynamic` (var, auto-without-init, reference decls, ref params,
ref/parallel range-for, catch vars, destructured names), global-tier-ambiguous names
(`global_tier_name`), `extra_globals_`, `engine_global_type` results (value-tag derived:
`tentative`, never a proof source), mixed ternaries, include results. `assignable`'s lenient rows are conversions, never identity — `same_type` never consults
`assignable`.

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

`fn_ctx` carries `is_static_method` and `in_ctor_initializers`: `walk_function` sets
`is_static_method` from `fn->is_static` and raises `in_ctor_initializers` around the initializer
loop. Bare-field proofs require `bare_field_context()`:

```cpp
    bool bare_field_context() const {
        return class_ctx_ && !fn_stack_.empty() && fn_stack_.back().sees_fields &&
               !fn_stack_.back().is_static_method && !fn_stack_.back().in_ctor_initializers && !has_includes_;
    }
```
(`sees_fields` is false in `[=]`/`[&]` lambdas and true for `[this]`; field initializers are
inferred in `walk_class` with an EMPTY `fn_stack_` → excluded.)

### 4.4 Field-proof resolution (one function, used by 4.5 rows S2/S4/M1/I1)

```cpp
    struct field_proof { class_definition* cd; uint32_t slot; uint64_t epoch; type_info* declared; bool write_ok; };
    static constexpr std::string_view k_no_proof_names[] = {"same_as", "reset", "use_count", "unique", "cpp_ref_count"};

    // Chain closed = every ancestor is a script class visible to the engine, none carries a C++ base.
    bool script_chain_closed(const class_definition& cd, int depth = 0) {
        if (depth > 32 || !cd.is_script_class() || cd.find_cpp_base_class() || cd.extended_ever()) return false;
        for (const auto& p : cd.get_parent_classes()) { if (!p || !script_chain_closed(*p, depth + 1)) return false; }
        return true;
    }

    std::optional<field_proof> resolve_field_proof(std::string_view cls, std::string_view member, const member_hit& hit) {
        if (!stamping() || !hit.found || hit.methods) return std::nullopt;
        if (hit.field && hit.field->is_static) return std::nullopt;                 // statics live in class storage
        for (auto n : k_no_proof_names) { if (member == n) return std::nullopt; }   // builtin-shadow names
        if (global_tier_name(member)) return std::nullopt;
        auto def = engine_class(cls);                                               // null on a same-unit class's FIRST check (5.0)
        if (!def || def->chain_has_nonpublic() || !script_chain_closed(*def)) return std::nullopt;
        const uint64_t id = eng_.symbolize(member);
        if (!def->get_method(id, false).is_invalid()) return std::nullopt;         // method with the field's name: ladder decides
        if (def->find_field_declaring_class(id) == nullptr) return std::nullopt;
        const uint32_t slot = def->field_slot(id);
        if (slot == class_definition::k_no_field_slot) return std::nullopt;
        const uint64_t getter_id = eng_.get_symbolizer()->get_getter_id_with_view(id).first;
        const uint64_t setter_id = eng_.get_symbolizer()->get_setter_id_with_view(id).first;
        if (def->user_accessor(getter_id) || def->user_accessor(setter_id)) return std::nullopt;   // a user _get_/_set_ in the chain owns the member
        type_info* declared = def->get_field_declared_type(id).get();
        if (hit.field && hit.field->type.get() != declared) return std::nullopt;   // class also declared in THIS unit: spellings must agree (interned pointers)
        bool write_ok = true;
        if (declared) {
            switch (declared->base_type) {
                case script_value_type::jai_int_type: case script_value_type::jai_float_type: case script_value_type::jai_bool_type:
                case script_value_type::jai_char_type: case script_value_type::jai_string_type: case script_value_type::jai_object_type:
                case script_value_type::jai_any_type: break;
                default: write_ok = false;   // array/map/shared_ptr/weak_ptr/reference/function fields: reads only
            }
        }
        return field_proof{def.get(), slot, def->field_shape_epoch(), declared, write_ok};
    }
```
A custom `_get_`/`_set_` anywhere in the chain stamps no proof, for reads or writes.
`lookup_member`'s engine fallback returns `found=true, field=nullptr` for a field declared on an
engine class — `resolve_field_proof` re-derives everything from `def`, so the module pattern (class
defined by an earlier execute) is covered without a `variable_decl`.

Stamping a field proof onto a node `n` (identifier_expr or member_expr):
```cpp
    template <class N> void stamp_field(N* n, const std::optional<field_proof>& p) {
        stamp(n->proven_class, p ? p->cd : nullptr, static_cast<class_definition*>(nullptr));
        stamp(n->proven_slot, p ? p->slot : uint32_t(UINT32_MAX), uint32_t(UINT32_MAX));
        stamp(n->proven_epoch, p ? p->epoch : uint64_t(0), uint64_t(0));
        stamp(n->proven_field_type, p ? p->declared : nullptr, static_cast<type_info*>(nullptr));
        stamp(n->proven_field_write, p ? p->write_ok : false, false);
        if (p && stamping()) { ++counts_.members; }
    }
```
Always call it (with `nullopt` on every refusal path) so a re-check that loses a proof clears it.

### 4.5 Stamping rules per node

| id | node / method | condition → stamp |
|---|---|---|
| S1 | `assignment_expr`, local target — `infer_assignment` | plain `=` (a compound is `x = T(x op rhs)`, a separate opcode) `&& !local->dynamic && same_type(local->type, rhs)` → `static_store_proof = proof_same_type`, else `proof_none`. A compound on a local stamps `static_operand_class = operand_class_of(local->type, rhs)` (the fused compound's typed row). |
| S2 | `assignment_expr`, bare field target | `bare_field_context()`; `p = resolve_field_proof(class_ctx_name_, id->name, hit)`; `stamp_field(id, p)`; `static_store_proof = same_type` iff `field_store_same_type(p, op, rhs)` (`op == equal && p && p->declared && same_type(ctype_from_type_info(p->declared), rhs)`). |
| S3 | `assignment_expr`, engine global | always `proof_none`. |
| S4 | `assignment_expr`, member target | `p = (recv.kind == object_k && m->object is not super_expr && !m->null_safe && !m->is_static) ? resolve_field_proof(recv.cls, m->member, hit) : nullopt`; `stamp_field(m, p)`; `static_store_proof = same_type` iff `field_store_same_type(p, op, rhs)`. |
| S5 | `assignment_expr`, subscript/other | `proof_none`. incdec desugar nodes (`incdec_desugar`/`postfix_old_result`) are compound → `proof_none` by S1's `op` test. |
| D1 | `variable_decl` — `walk_variable_decl` explicit type | `has_init && !entry.dynamic && !is_reference_decl && decl->type->base_type not in {shared_ptr, weak_ptr, reference, any} && same_type(entry.type, init)` → `static_init_proof = same_type`; else `proof_none`. `shared_ptr<auto>`, `auto` and no-init: `proof_none`. Class-body field initializers (`walk_class`) are NOT `walk_variable_decl` and get no stamp (they run through `store_field_defaults`). |
| C1 | `call_expr` — `stamp_call_proofs` | One statically resolved same-script callee (`ov.size() == 1`; `fn->body && !fn->is_coroutine && arity_range(fn) == {n, n}`). Per by-value argument `i`: `proof_same_type` iff the parameter is untyped/var or `same_type(ctype_from_type_info(param.type), args[i])`; reference parameters get none. `proven_callee_body = fn->body` only when every parameter is by value and the unit has no includes; `proof_globals` lists the program names an argument proof read (an includes-free unit proves every argument; otherwise `certain_argument` decides per argument and records its dependencies). `proven_callee_decl` is set for a free callee either way (the inliner's identity). Every other callee shape (callable local, engine ctor/function, `new_expr`, member on non-object): `static_arg_proofs`, `proof_globals` cleared, `proven_callee_body = nullptr`. `functions_` may hold several decls for one name ("later replaces earlier") → `size()==1` refuses those. |
| R1 | `return_stmt` — `check_return` | `r->value && !r->binds_reference && !fn.is_coroutine && fn.ret && ret = ctype_from_type_info(fn.ret); ret.kind != null_k && same_type(ret, value)` → `static_return_proof = same_type`; else `proof_none`. Lambdas share `check_return` and the same consumer — stamp them too. |
| M1 | `member_expr` read — `infer_member` | `recv.kind == object_k && m->object not super_expr && !m->null_safe && !m->is_static && hit.found`: `stamp_field(m, resolve_field_proof(recv.cls, m->member, hit))`; every other exit of `infer_member` → `stamp_field(m, nullopt)`. |
| I1 | `identifier_expr` bare field read — `infer_identifier` | `bare_field_context() && hit.found && !global_tier_name(...)`: `stamp_field(id, resolve_field_proof(class_ctx_name_, id->name, hit))`; every other exit (a local hit included) → `stamp_field(id, nullopt)`. |
| B1 | `binary_expr` — `infer` | Non-subscript: `operand_class_of(l, r)`: int,int→1 float,float→2 int,float→3 float,int→4 string,string→5 bool,bool→6 else 0 (tentative operands 0; char pairs 0, char is promoted by `binary_result`). Subscript: the proven element kind of a declared container — object elements → 7, math vectors → 8, a proven int/float element → 1/2, else 0. `static_operand_class` selects the VM compiler's typed row; the row's runtime guard keeps it sound. |

Recheck idempotence: every rule writes its field on EVERY visit (stamp or `proof_none`/`nullopt`),
so a later re-check with a moved surface can only add or drop proofs, never leave a stale one
that the pass no longer derives.

### 4.6 `run_static_check` API — `include/jaiscript/detail/type_checker.hpp`

`detail::check_options` carries `diagnostics` (false: the report stays empty), `proofs`
(`proofs_t { leave, stamp, clear }`), `stats` (a `proof_stats*` the stamp/clear passes fill), the
program closure tables (`program_names`, `program_classes`, `program_functions`,
`program_globals`, `program_enums`), `deferred_classes` and an `observer`.
`run_static_check(eng, decls, extra_globals, opts)` builds the `static_checker`, which sets
`proofs_`/`collect_diagnostics_` from `opts`.

### 4.7 Engine hook points — `source/implementation/engine.cpp`

State: the engine's `type_proofs_` (default true; `engine::type_proofs(bool)` sets it and nothing
else), `script_cache_entry::proof_epoch` and `jaibite::proof_epoch_` (`UINT64_MAX` = the nodes
carry no proofs).

`checked_report` (the cached entry point, through `check_entry`) and `check_bite` run the
diagnostics walk (proofs left alone, `proofs_t::leave`) when diagnostics are wanted and stale, then
a proof pass whenever the switch, the surface epoch, auto typing or the lowering view moved; the
proof pass alone reads the program's globals and types the auto slots. Every execute calls
`checked_report` in every mode (`off` included): diagnostics are wanted only under warn/strict, and
the proof pass rides it. A host's instance-variables call checks fresh for diagnostics only
(`check_with_instance_vars`) and never re-stamps. `check_bite` runs on `engine::jaibite`,
`execute(jaibite&)` and a bite load: "a bite is checked once" holds for diagnostics (a trusted-clean
loaded bite keeps empty diagnostics), while proofs re-stamp on surface moves.

Every hook runs after `run_ast_marking_passes` (the parse tail, the jaibite load, bite creation).

`check_surface_epoch_` moves on `add_global`, `register_overload_impl`, `add_class_impl`,
`register_template_type` and `note_class_shape_change` (3.2).

Cost: one checker walk per unique source per surface epoch, in every mode; `type_proofs(false)`
clears the stamps.

## 5. Interpreter — `source/implementation/interpreter.cpp`

Includes `detail/type_proofs.hpp`. Each site: guard → fast statements → **identical fall-through
to the unproven code** (the proven branch sits in front of the exact call it skips). The snippets
in sections 5 and 6 show each lane's shape; the code is the reference. All field-proof consumers
are gated `!parallel_worker_` (workers keep their own walls, which deref on read — a different
shape).

### 5.0 First-execute note
A unit that declares its own classes gets no field proofs on its first check (`engine_class` is
null until `visit_class_decl` registers the class); its same_type store/decl/return/arg proofs DO
stamp (they use the in-script tables). Registration calls `note_class_shape_change` → the second
execute re-checks and stamps field proofs; identical reloads afterwards leave `field_shape_epoch`
alone so the stamps keep hitting.

### I-S1 Slot store — `visit_assignment_expr`
The proof gates only the enforcement condition:
```cpp
                        type_info_ptr slot_type = storage->get_type_info();
                        const bool proven_identity = expr->static_store_proof == proof_same_type && slot_type &&
                                                     detail::same_type_payload(value, slot_type.get());
                        if (!proven_identity && slot_type && (slot_type->base_type != value.type() ||
                                          slot_type->base_type == script_value_type::jai_object_type)) {
                            ...enforce_type_compatibility + var-tag rule...
                        }
                        *storage = clone_for_assignment(value);
```
The `typed_store_provable` lane stays in front. `store_through` (`ref_store_through`) stays
unproven.

### I-S2 Env store
```cpp
                    if (!(expr->static_store_proof == proof_same_type && target_type &&
                          detail::same_type_payload(value, target_type.get()))) {
                        auto enforced = enforce_type_compatibility(std::move(value), target_type, identifier->name);
                        if (!enforced) { return enforced.error_value(); }
                        value = std::move(enforced.value());
                    }
```
Everything before (cell redirect, cpp_bound, weak/shared ladders) and after (any-tag rules, the
result-push rules) is the unproven path.

### I-S3 Bare this-field store — before `probe_member_scope`
```cpp
            bool member_wins = false;
            if (!identifier->global_qualified && member_scope_.active() &&
                !detail::own_scope_lookup(environment_.get(), member_scope_.floor, identifier->symbol_id)) {
                if (identifier->proven_class && identifier->proven_field_write && !parallel_worker_ &&
                    member_scope_.state == detail::member_scope_state::instance && member_scope_.this_storage) {
                    if (class_instance* inst = detail::proven_field_instance(*member_scope_.this_storage,
                                                  identifier->proven_class, identifier->proven_epoch)) {
                        // == the this-field landing (KEEP BYTE-PARALLEL with vm store_popped_value's proven lane)
                        const bool is_lvalue_read = is_lvalue_expression(expr->value.get());
                        if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] { value = value.detached_for_store(); }
                        script_value incoming = is_lvalue_read ? clone_for_assignment(value) : std::move(value);
                        if (expr->static_store_proof == proof_same_type && identifier->proven_field_type &&
                            detail::same_type_payload(incoming, identifier->proven_field_type)) {
                            if (detail::retags_on_field_write(identifier->proven_field_type)) { incoming.set_type_info(type_info_ptr{identifier->proven_field_type}); }
                        } else {
                            auto enforced = inst->enforce_field_write(identifier->symbol_id, std::move(incoming));
                            if (!enforced) { return enforced.error_value(); }
                            incoming = std::move(enforced).value();
                        }
                        inst->field_slot_write(identifier->proven_slot, std::move(incoming));
                        if (is_lvalue_read) { push_value(std::move(value)); }
                        else { push_value(inst->get_field(identifier->symbol_id)); }   // stored, declared-tagged value
                        return {};
                    }
                }
                member_wins = ...the probe...;
            }
```
`is_lvalue_expression` is the unproven landing's predicate (identifier/member/subscript).
Compound this-field stores and `++f` stay unproven.

### I-D1 Decl — `visit_variable_decl`
```cpp
            if (decl->initializer && !(decl->static_init_proof == proof_same_type &&
                                       detail::same_type_payload(value, effective_decl_type.get()))) {
                auto enforced = enforce_type_compatibility(std::move(value), effective_decl_type, decl->name);
                ...
            }
```
The scalar fast path, clone rule, homogeneity check, `set_type_info` and define are the unproven
path.

### I-C1 Argument binding — `call_function`
`detail::call_site_context` carries `const call_expr* site`; the call sites arm it with the
`call_expr`. After `pending_call_ctx_` is consumed:
```cpp
    const uint8_t* arg_proofs = nullptr;
    if (call_ctx && call_ctx->site && call_ctx->site->proven_callee_body == function.body.get() &&
        call_ctx->site->static_arg_proofs.size() == function.parameters().size() &&
        args.size() == function.parameters().size()) {
        arg_proofs = call_ctx->site->static_arg_proofs.data();
    }
```
In the value-param branch the proof gates only the conversion (the exact-class lane stays in
front):
```cpp
            script_value converted_arg = make_value();
            const bool untyped = !param.type || param.type->base_type == script_value_type::jai_any_type;
            if (arg_proofs && arg_proofs[i] == proof_same_type &&
                (untyped || detail::same_type_payload(arg.deref(), param.type.get()))) {
                // try_convert_for_parameter's identity returns: `arg`, or the deref for exact-name objects
                converted_arg = (!untyped && param.type->base_type == script_value_type::jai_object_type)
                                ? script_value(arg.deref()) : script_value(arg);
            } else {
                auto convert_result = try_convert_for_parameter(arg, param.type);
                ...
            }
```
`should_share` + `clone()` + `boxed_param` follow either way; parameters are never retagged.
Defaults (`i >= args.size()`) and reference params never see proofs by construction.
`construct_instance` routes the ctor body through `call_function` → same code; callee identity
(`function.body` == the single ctor's body) is what makes a wrong-overload stamp miss.

### I-R1 Return — `visit_return_stmt`
The conversion happens at the return site: `visit_return_stmt` converts against the enclosing
`call_frame::function`'s declared type (`detail::return_conv_of`, detail/return_conv.hpp) and
consumes the proof there — `stmt->static_return_proof == proof_same_type &&
same_type_payload(out, return_type)` skips the table; the `call_function` / `resume_coroutine`
epilogues only deref.

### I-M1 Member read — `visit_member_expr`, after the null_safe check, before `if (is_super_access)`
```cpp
    if (expr->proven_class && !parallel_worker_) {
        class_instance* inst = nullptr;
        if (script_value* cell = detail::proven_field_cell(objectValue, expr->proven_class, expr->proven_epoch, expr->proven_slot, &inst)) {
            push_value(*cell);   // == push_value(target.get_field(member_id)): cell copy, no deref, no materialize
            return {};
        }
    }
```
(`objectValue` is already deref'd.) Skipped ladder rungs are all excluded at stamp time or
covered by the epoch: super (stamp), worker wall (gate), coroutine handle / cpp / transparent
wrappers (not the proven class object), shared_ptr builtin names + `same_as` (names), access
check (`chain_has_nonpublic` stamp + epoch), getter probe (stamp + epoch), `has_field` (present
cell). `resolve_member_target`'s extended-layer re-point: a not-yet-re-pointed plain wrapper has
a cpp class object → guard miss → ladder re-points.

### I-M2 Member write — `visit_assignment_expr` member branch, before `assign_member_value(dereferenced, memberExpr, value)`
```cpp
            if (memberExpr->proven_class && memberExpr->proven_field_write && !parallel_worker_) {
                if (class_instance* inst = detail::proven_field_instance(dereferenced, memberExpr->proven_class, memberExpr->proven_epoch)) {
                    // == assign_member_value's field branch minus the ladder (KEEP BYTE-PARALLEL with vm exec_set_member's proven lane)
                    script_value incoming = clone_for_assignment(value);
                    if (expr->static_store_proof == proof_same_type && memberExpr->proven_field_type &&
                        detail::same_type_payload(incoming, memberExpr->proven_field_type)) {
                        if (detail::retags_on_field_write(memberExpr->proven_field_type)) { incoming.set_type_info(type_info_ptr{memberExpr->proven_field_type}); }
                    } else {
                        auto enforced = inst->enforce_field_write(memberExpr->member_id, std::move(incoming));
                        if (!enforced) { return enforced.error_value(); }
                        incoming = std::move(enforced).value();
                    }
                    inst->field_slot_write(memberExpr->proven_slot, std::move(incoming));
                    push_value(std::move(value));   // what the branch pushes after assign_member_value
                    return {};
                }
            }
```
Setter precedence is excluded at stamp (a custom `_get_`/`_set_` → no proof; cpp-base setters →
no cpp base allowed), access check by stamp + epoch.

### I-I1 Bare field read — `visit_identifier_expr`, after `own_scope_lookup` misses, before `probe_member_scope`
```cpp
        if (expr->proven_class && !parallel_worker_ &&
            member_scope_.state == detail::member_scope_state::instance && member_scope_.this_storage) {
            class_instance* inst = nullptr;
            if (script_value* cell = detail::proven_field_cell(*member_scope_.this_storage, expr->proven_class, expr->proven_epoch, expr->proven_slot, &inst)) {
                push_value(*cell);   // == push_value(*member.value_ptr): copy, no deref
                return checked_result<void>();
            }
        }
```
A null cell falls to `probe_member_scope`, which materializes the default (non-const
`get_field`). The non-member-frame this-fallback stays unproven.

## 6. VM — `source/implementation/vm/vm_backend.cpp`, `vm_compiler.cpp`, `chunk.hpp`, `opcode.hpp`

Proofs ride existing instructions and side tables. Carriers, by proof:

| proof | carrier | freshness |
|---|---|---|
| `static_store_proof` (identifier target) | `store_flag_same_type = 32` in `op_store.c`, mirrored into `fused_binary_dst_proto.flags` at the peepholes | frozen at first compile; safe via `same_type_payload` |
| `static_init_proof` | node: `op_decl_var.a` → `variable_decl` read at exec | live |
| `static_arg_proofs` / `proven_callee_body` | `call_site.call_node` (`chunk::nodes` index of the `call_expr`); read at push time | live |
| `static_return_proof` | `op_return.b` (the return mode); `op_return_ident.c` bit `load_flag_return_proven = 4` (`fused_ident_value` tests flags with `&`, so an extra bit is inert); `op_return_binary.c`; `op_return_convert.a` | frozen; safe via guard |
| member_expr field proofs | node: `op_get_member.a` / `op_set_member.a` | live |
| identifier_expr field proofs | `chunk::ident_nodes` (empty or `code.size()` long, `k_invalid_u32` default) for `op_load`/`op_store`/`op_binary_fused_store`/`op_return_ident` ips, and `fused_operand::ident_node` | live |

Compiler fills (`vm_compiler.cpp`): every identifier compiled with `identifier_slot_operand(ident)
== k_invalid_u32` (env-resolved — field names are never slotted) records `(ip, add_node(ident))`
at its `op_load`, `op_store` (the in-place fused rewrite keeps the same ip) and `op_return_ident`
emits, and the fused operand builder sets `out.ident_node` (a dst proto that resolves through
`fused_operand` sets `dp.ident_node`; otherwise the ip entry suffices). Each compile tail
materializes `ident_nodes` sized `code.size()` when any entry was recorded. Both call-site builders
set `call_site.call_node = add_node(expr)`. `store_flag_same_type` is set beside
`store_flag_type_provable` when `assign->static_store_proof == proof_same_type`, independent of
`typed_store_provable`. Return carriers: `op_return.b` takes the proof (or
`return_mode_converted`), `op_return_ident` sets `load_flag_return_proven`, and a trailing
`op_binary` rewritten to `op_return_binary` takes the proof in `c` (`op_binary` uses only `a`/`b`).

Gates: every field-proof consumer requires `cached_global_env_` (null on workers; the proven lanes
write no chunk state, but the worker walls in `assign_member` / `member_access_value` stay ahead).
Invariant 2b (internal/invariants.md): never hold a raw `stack_`/slot pointer across
`frame_slot_set`; field cells are unique_ptr-stable and the frame's `this` anchor pins the instance.

### V-S1 Slot store — `store_popped_value`
```cpp
			type_info_ptr slot_type = storage->get_type_info();
			const bool proven_identity = (ins.c & store_flag_same_type) && slot_type &&
			                             detail::same_type_payload(value, slot_type.get());
			if (!proven_identity && slot_type && (slot_type->base_type != value.type() ||
			                  slot_type->base_type == script_value_type::jai_object_type)) { ... }
			*storage = clone_for_assignment(value);
```
The `store_flag_type_provable` lane stays in front.

### V-S2 Env store
```cpp
		if (!((ins.c & store_flag_same_type) && target_type && detail::same_type_payload(value, target_type.get()))) {
			auto enforced = enforce_type_compatibility(std::move(value), target_type);
			...
		}
```
weak/shared ladders, `operator=` dispatch, var-tag rules, store_back/push: the unproven path.

### V-S3 Bare this-field store — inside the member_wins block, after `own_scope_lookup` misses, before `probe_member_scope`
Node: `identifier_expr* idn = (code.ident_nodes.size() > f.ip && code.ident_nodes[f.ip] != k_invalid_u32) ? static_cast<identifier_expr*>(code.nodes[code.ident_nodes[f.ip]].get()) : nullptr;`
```cpp
			if (idn && idn->proven_class && idn->proven_field_write && cached_global_env_ &&
			    frame_scope_state(f) == detail::member_scope_state::instance && f.locals->scope_this) {
				if (class_instance* inst = detail::proven_field_instance(*f.locals->scope_this, idn->proven_class, idn->proven_epoch)) {
					// == the store-side this-field IC hit with the conversion gated by the proof
					if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] { value = value.detached_for_store(); }
					script_value incoming = rhs_lvalue ? clone_for_assignment(value) : std::move(value);
					if ((ins.c & store_flag_same_type) && idn->proven_field_type &&
					    detail::same_type_payload(incoming, idn->proven_field_type)) {
						if (detail::retags_on_field_write(idn->proven_field_type)) { incoming.set_type_info(type_info_ptr{idn->proven_field_type}); }
					} else {
						auto enforced = inst->enforce_field_write(sym, std::move(incoming));
						if (!enforced) { return raise_from(enforced); }
						incoming = std::move(enforced).value();
					}
					inst->field_slot_write(idn->proven_slot, std::move(incoming));
					stack_.push_back(rhs_lvalue ? std::move(value) : script_value(inst->get_field(sym)));
					return {};
				}
			}
```
The this-store IC stays ahead of it (a hit there is already the fast case);
`this_field_store_arm` is not called from the proven lane (unproven sibling sites arm themselves).

### V-D1 Decl — `exec_decl_var`
```cpp
		if (has_init && !(decl->static_init_proof == proof_same_type &&
		                  detail::same_type_payload(value, effective_decl_type.get()))) {
			auto enforced = enforce_type_compatibility(std::move(value), effective_decl_type);
			...
		}
```
Fused decl replays (`exec_binary_fused_decl`, `exec_index_fused_decl`) reach the same statement or
the identity-gated `decl_expect` lane.

### V-C1 Argument binding — `bind_parameters` and its call sites
Helper (member):
```cpp
	const uint8_t* site_arg_proofs(const call_site* site, const chunk* caller_code, const block_stmt* body, size_t nparams, size_t argc) const {
		if (!site || !caller_code || site->call_node == k_invalid_u32 || argc != nparams) return nullptr;
		auto* ce = static_cast<const call_expr*>(caller_code->nodes[site->call_node].get());
		if (ce->proven_callee_body != body || ce->static_arg_proofs.size() != nparams) return nullptr;
		return ce->static_arg_proofs.data();
	}
```
`bind_parameters` takes the `arg_proofs` pointer. Call sites pass
`site_arg_proofs(site, caller_code, <body>, parameters.size(), argc)` where `<body>` is the script
function's `body` (the plain, pinned, tail-splice and host-called frame pushes) or the method
declaration's `body` (`rec.ast_pin`, the method pushes). Proofs never feed the method IC
(`mic_static` already covers all-untyped params).
In the value-param branch the proof gates only the conversion (the primitive in-place lane stays
in front):
```cpp
			script_value arg_shield(arg);                                   // kept: byte-parallel, cheap
			const bool untyped = !param.type || param.type->base_type == script_value_type::jai_any_type;
			script_value converted_arg = make_null();
			if (arg_proofs && arg_proofs[i] == proof_same_type &&
			    (untyped || detail::same_type_payload(arg_shield.deref(), param.type.get()))) {
				converted_arg = (!untyped && param.type->base_type == script_value_type::jai_object_type)
				                ? script_value(arg_shield.deref()) : script_value(arg_shield);
			} else {
				...exact-class lane + try_convert_for_parameter...
			}
			...should_share / clone / boxed_param...
```

### V-R1 Return — `return_with_result`
`return_with_result(frame*& fp, script_value result, uint32_t mode)`: `op_return.b` carries the
mode (0 plain, 1 = the proof, `return_mode_converted`). `exec_return_ident` passes the
`load_flag_return_proven` bit and `exec_return_binary` its `c`. With the proof, the
`return_conv::check` row (and `unclassified` when `rec.return_type` is concrete) takes the
`deref_only` tail when `same_type_payload(result.deref(), rec.return_type)` holds, the full route
otherwise. A `return` inside a try/catch region compiles to `op_return_convert` (a = the proof),
which converts BEFORE the region exits and lands with `return_mode_converted` (identity in
`return_with_result`, the ref handle passed through); the native-entry / fiber doors convert in
`entry_return_finish` and honour the proof behind the same guard (`convert_return_at_site`).

### V-M1 Member read — `exec_get_member`, before the `member_ic` block
```cpp
	if (expr->proven_class && cached_global_env_) {
		class_instance* inst = nullptr;
		if (script_value* v = detail::proven_field_cell(object, expr->proven_class, expr->proven_epoch, expr->proven_slot, &inst)) {
			stack_.push_back(*v);   // raw cell copy, references stay references (== the member_ic hit)
			return {};
		}
	}
```
`member_ic` stays for unproven sites and for guard misses. `fused_member_container` takes no
proof.

### V-M2 Member write — `exec_set_member`, before the `member_ic` block
`op_set_member` has no store flags; the assignment's `static_store_proof` rides `op_set_member.b`
(`emit(op_set_member, node, expr->static_store_proof)` in the member branch of the assignment
compile):
```cpp
	if (member->proven_class && member->proven_field_write && cached_global_env_) {
		if (class_instance* inst = detail::proven_field_instance(dereferenced, member->proven_class, member->proven_epoch)) {
			script_value incoming = clone_for_assignment(value);
			if (ins.b == proof_same_type && member->proven_field_type &&
			    detail::same_type_payload(incoming, member->proven_field_type)) {
				if (detail::retags_on_field_write(member->proven_field_type)) { incoming.set_type_info(type_info_ptr{member->proven_field_type}); }
			} else {
				auto enforced = inst->enforce_field_write(member->member_id, std::move(incoming));
				if (!enforced) { return raise_from(enforced); }
				incoming = std::move(enforced).value();
			}
			inst->field_slot_write(member->proven_slot, std::move(incoming));
			stack_.push_back(std::move(value));
			return {};
		}
	}
```

### V-I1 Bare field read — `exec_load` and `fused_ident_value`, after `own_scope_lookup` misses, before `probe_member_scope`
Node: `exec_load` → `ident_nodes[f.ip]`; `fused_ident_value` → `operand.ident_node`.
```cpp
			if (idn && idn->proven_class && cached_global_env_ &&
			    frame_scope_state(f) == detail::member_scope_state::instance && f.locals->scope_this) {
				class_instance* inst = nullptr;
				if (script_value* cell = detail::proven_field_cell(*f.locals->scope_this, idn->proven_class, idn->proven_epoch, idn->proven_slot, &inst)) {
					stack_.push_back(*cell);          // exec_load; fused_ident_value returns `cell` (== *member.value_ptr)
					return {};
				}
			}
```
`this_field_ic_probe` stays ahead; the proven lane replaces only the probe's hash lookups on the
first hit and after serial churn. `op_return_ident` builds its operand from `ins` with
`operand.ident_node = ident_nodes[f.ip]`.

### Freshness statement
Node-borne proofs (decl, member, call, ident) see re-stamps live even in a chunk compiled earlier.
Instruction-borne flags (`store_flag_same_type`, return bits) never gain proofs after first
compile — toggling `type_proofs(true)` later or a surface move that adds store/return proofs only
benefits fresh chunks; correctness is unaffected (guards). No chunk/compiled-slot invalidation
happens on a re-stamp.

## 7. Tests

`foundry::make_engine()` honors `--backend=vm`, so every suite below runs under both backends.
Tests that need proofs on AND off in one process build their engines explicitly. Every proof test
asserts equality against a `type_proofs(false)` twin, never a literal alone.

- `source/tests/semantics/strong_types_tests.cpp`: the batteries `proof_battery_same_type_stores`,
  `proof_battery_not_certain`, `proof_call_arg_identity_guard`, `proof_member_second_execute`,
  `proof_stats_fire_and_kill_switch` — each snippet 4 ways {interp,vm}×{proofs on,off}, outputs and
  error texts equal.
- `source/tests/semantics/static_check_tests.cpp` (suite "Static Type Checking"):
  `proofs_default_on_and_switch_roundtrip`, `proofs_stamp_in_off_mode_with_counters`,
  `proofs_not_stamped_from_uncertain_types`, `proofs_off_strips_cached_entry_and_on_restamps`,
  `proof_field_stamps_on_second_execute_and_hold_across_identical_reload`, `proof_field_refusals`,
  `proof_jaibite_paths_restamp_without_touching_diagnostics`, `proof_kernel_same_type_payload`,
  `proof_battery_results_equal_with_proofs_on_and_off`,
  `proof_consumers_retype_on_reload_assigns_never_errors`, and `proof_differential_gate` (fuzz
  seeds run on four engines {interp,vm}×{proofs on,off}; segment results, printed output and
  liveness must be identical).
- `source/tests/language/hot_reload_correctness_tests.cpp` (suite "Hot Reload Correctness"): each
  test stores into a typed field via BOTH spellings (`this.f = v` / `f = v`) and reads it back,
  three times before the reload (first-execute → stamped → identical-reload-hit), then reloads and
  calls again: `proof_field_retype_int_to_string_assigns`, `proof_field_retype_float_to_int_truncates`,
  `proof_field_retype_string_to_int_resets_default`, `proof_field_retype_int_to_var_accepts_anything`,
  `proof_field_removed_then_read_errors_like_unproven`, `proof_field_gains_custom_getter_on_reload`,
  `proof_field_gains_custom_setter_on_reload`, `proof_field_access_label_change_on_reload`,
  `proof_base_reload_invalidates_derived_proofs`, `proof_class_gains_base_on_reload`,
  `proof_class_becomes_open_on_reload`, `proof_same_source_reexecute_vm_chunk_reuse`,
  `proof_extend_reclass_misses`.
- `source/tests/vm/vm_backend_tests.cpp` (suite "VM Backend"):
  `proof_vm_lanes_equal_with_proofs_on_and_off`, `proof_carriers_survive_recompile_and_frozen_flags`,
  `proof_ident_nodes_and_call_node_fill`.
- `jaiscript_fuzz --no-proofs` runs both backends with `type_proofs(false)` (`run_config::proofs`
  in fuzz_harness.hpp); the "Differential Fuzz Smoke" suite's
  `fixed_seeds_no_new_divergence_no_proofs` holds interp-vs-vm parity in both proof states.

## 8. Not proven

- Conversion proofs (int→float widening, bool/string coercions, null→object placeholder, derived→base, ctor conversions): `same_type` only.
- Container same_type / covariant `array<Base> = array<Derived>` proofs; typed-map proofs.
- Proven callee dispatch for the identity bind (skipping overload resolution / method IC by proof); argument proofs for callable locals, engine functions, `new_expr`, coroutine callees, defaulted or reference parameters.
- Same-type proofs on compound assignment / `++`/`--` desugars, `store_through` references, `environment::assign`'s method-env fallback, compound this-field stores, static fields, `super.f`, `?.` receivers, engine globals, hybrid (cpp-base) classes, extended layers, transparent wrappers.
- `fused_member_container`; worker (parallel) frames.
- Invalidating compiled vm chunks on re-stamp; serializing proofs in `.jaibite`; a per-call re-stamp on the instanceVars path.
- Any change to error texts, `type_of` results, tag stamping, clone/value semantics, or reference normalization.
