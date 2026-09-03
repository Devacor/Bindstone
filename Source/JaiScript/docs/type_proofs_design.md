# Parse-time type proofs — implementation spec (v1)

Three implementation tracks (checker/AST, interpreter, vm) plus a test track. Follow verbatim;
refinements below are the designer's and are final for v1. Line numbers are from the working
tree on 2026-09-02 (other agents' WIP is present) — **re-grep the named symbol before editing**;
the symbol is the anchor, the line is a hint.

## 0. Contract

Rulings (Dev): (1) enforce at parse time whatever is provable, keep runtime enforcement wherever
var/dynamic values mix in; (2) hot reload never turns a proof into an error — a retyped field
assigns or is ignored through the existing migration/enforcement paths; (3) dynamic users pay
today's price, consistently typed users pay once at parse.

Soundness principle (the one sentence every consumer obeys): **a proof selects a lane; a total
runtime guard makes the lane sound.** Every consumed proof sits behind a guard that is TRUE only
when the skipped conversion is provably the identity on the value actually in hand
(`detail::same_type_payload`) or when the class object, its field-shape epoch and a present cell
all match (`detail::proven_field_cell`). Guard failure = the existing code runs, byte for byte.
There is therefore no path where a proof produces an error the unproven path would not; stale
proofs (hot reload, re-stamp races, frozen vm flags) can only cost the skip.

Byte-parallel contract: for every program, both backends with `type_proofs(true)` and
`type_proofs(false)` produce identical results, identical printed output, identical error text,
identical `type_of`/`type_info` observations. Section 7's differential gate pins it.

Terms. *Certain*: a static type for which the payload guard is total (section 4.2). *same_type*:
target and RHS certain and identical. *Field proof*: `{proven_class, proven_slot, proven_epoch,
proven_field_type, proven_field_write}` on a `member_expr` / `identifier_expr`. *Shape epoch*:
`class_definition::field_shape_epoch()` (new, section 3.1).

Designer refinements of P3 (final): (a) `proven_epoch` is the NEW `field_shape_epoch()`, not
`method_epoch()` — `method_epoch` bumps on every execute of a `class_decl` (identical reload
`class_definition.hpp:1479`, `store_field_defaults` 1809), so a `method_epoch` guard would make
every field proof in a self-defining script miss forever; the READ guard also includes the epoch
(a reload can add a custom `_get_` or access labels). (b) Containers are NOT certain in v1
(`ctype` carries no element type; `enforce_container_boundary` is not the identity; slot vs env
container enforcement already diverges — `interpreter.cpp:5181` skips it, 5506 runs it). (c) Two
extra stamps: `proven_field_type`/`proven_field_write` on field proofs, `proven_callee_body` on
`call_expr`. (d) `static_operand_class` is annotation-only; no consumer in v1.

## 1. Shared kernel — `include/jaiscript/detail/type_proofs.hpp` (NEW, header-only)

Included by `interpreter.cpp` and `vm_backend.cpp` (not by `ast.hpp`; the enum constants live in
`ast.hpp`). Both backends call these verbatim — never re-implement the guards inline.

```cpp
#pragma once
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/class_definition.hpp>
namespace jai::detail {

// TRUE iff the unproven conversion for `target` is the identity on `v` (v deref-normalized):
// mirrors enforce_type_compatibility's same-base fast path (value.type() == base, non-object) and
// its same-class-name object branch, try_convert_for_parameter's exact-name branch, and
// try_convert_field_value's same-base branch (that one RETAGS scalars/strings - see
// retags_on_field_write). tag_agrees reproduces defined_type(): a non-any tag must name the base.
inline bool same_type_payload(const script_value& v, const type_info* target) noexcept {
    if (!target) return false;
    const size_t ri = v.raw_storage_index();
    const type_info* tag = v.get_type_info().get();
    const bool tag_agrees = !tag || tag->base_type == script_value_type::jai_any_type ||
                            tag->base_type == target->base_type;
    switch (target->base_type) {
        case script_value_type::jai_int_type:    return ri == script_value::TYPEID_INT && tag_agrees;
        case script_value_type::jai_float_type:  return ri == script_value::TYPEID_FLOAT && tag_agrees;
        case script_value_type::jai_bool_type:   return ri == script_value::TYPEID_BOOL && tag_agrees;
        case script_value_type::jai_char_type:   return ri == script_value::TYPEID_CHAR && tag_agrees;
        case script_value_type::jai_string_type: return ri == script_value::TYPEID_STRING && tag_agrees;
        case script_value_type::jai_object_type: return ri == script_value::TYPEID_OBJECT &&
                                                        tag == target && !target->type_name.empty();
        default: return false;   // any/null/array/map/shared_ptr/weak_ptr/reference/function: never
    }
}

// try_convert_field_value stamps set_type_info(declared) on same-base scalars/strings and leaves
// same-class objects untouched (class_definition.hpp:2001-2024, 2100-2130).
inline bool retags_on_field_write(const type_info* declared) noexcept {
    switch (declared->base_type) {
        case script_value_type::jai_int_type: case script_value_type::jai_float_type:
        case script_value_type::jai_bool_type: case script_value_type::jai_char_type:
        case script_value_type::jai_string_type: return true;
        default: return false;
    }
}

// Receiver guard: class_instance wrapper whose class object IS `cls` and whose field shape has
// not moved since the stamp. Null otherwise. Never dereferences a non-class holder.
inline class_instance* proven_field_instance(const script_value& receiver_in, const class_definition* cls,
                                             uint64_t epoch) noexcept {
    const script_value& r = receiver_in.is_reference() ? receiver_in.deref() : receiver_in;
    if (r.raw_storage_index() != script_value::TYPEID_OBJECT) return nullptr;
    auto& holder = const_cast<script_value&>(r).unchecked_get_object_storage();
    if (!holder || !holder->is_class_instance_wrapper || !holder->data) return nullptr;
    auto* inst = static_cast<class_instance*>(holder->data.get());
    class_definition* cd = inst->get_class_definition();
    if (cd != cls || cd->field_shape_epoch() != epoch) return nullptr;
    return inst;
}

// Read guard: instance guard + present cell (absent/raw-invalid -> null -> the ladder runs,
// which materializes defaults on the bare path / reads class defaults on the member path).
inline script_value* proven_field_cell(const script_value& receiver, const class_definition* cls,
                                       uint64_t epoch, uint32_t slot, class_instance** inst_out) noexcept {
    class_instance* inst = proven_field_instance(receiver, cls, epoch);
    if (!inst) return nullptr;
    if (inst_out) *inst_out = inst;
    return inst->field_slot_value(slot);
}

} // namespace jai::detail
```

Static asserts the kernel must carry: `TYPEID_INT==1, TYPEID_FLOAT==2, TYPEID_STRING==3,
TYPEID_CHAR==4, TYPEID_BOOL==5, TYPEID_OBJECT==8` (value.hpp:1964-1979).

## 2. AST fields — `include/jaiscript/detail/ast.hpp`

Constants (put once, before `identifier_expr`, ~line 193):

```cpp
    // Parse-time type proofs (docs/type_proofs_design.md). Stamped by the static checker's proof
    // pass, never serialized (jaibite re-derives them at load), consumed behind total runtime
    // guards (detail/type_proofs.hpp). Default = no proof.
    inline constexpr uint8_t proof_none = 0;
    inline constexpr uint8_t proof_same_type = 1;
    inline constexpr uint8_t operand_class_unknown = 0, operand_class_int_int = 1, operand_class_float_float = 2,
        operand_class_int_float = 3, operand_class_float_int = 4, operand_class_string_string = 5, operand_class_bool_bool = 6;
```

Per node (insert after the named existing member; all `mutable` so `infer(expression*)` and
const callers may stamp; raw pointers on purpose — same pinning rationale as `mcache_*` 304-312:
script class_definitions are engine-lifetime, `impl->classes` never erases, hot reload reuses the
object):

| node | after | fields |
|---|---|---|
| `identifier_expr` (194) | `names_value_decl` 207 | `mutable class_definition* proven_class = nullptr; mutable uint32_t proven_slot = UINT32_MAX; mutable uint64_t proven_epoch = 0; mutable type_info* proven_field_type = nullptr; mutable bool proven_field_write = false;` |
| `binary_expr` (214) | `transient_element_read` 223 | `mutable uint8_t static_operand_class = operand_class_unknown;` |
| `assignment_expr` (264) | `typed_store_provable` 273 | `mutable uint8_t static_store_proof = proof_none;` |
| `call_expr` (285) | `arguments` 288 | `mutable std::vector<uint8_t> static_arg_proofs; mutable const block_stmt* proven_callee_body = nullptr;` |
| `member_expr` (294) | `mcache_flags` 319 | `mutable class_definition* proven_class = nullptr; mutable uint32_t proven_slot = UINT32_MAX; mutable uint64_t proven_epoch = 0; mutable type_info* proven_field_type = nullptr; mutable bool proven_field_write = false;` |
| `return_stmt` (568) | `binds_reference` 574 | `mutable uint8_t static_return_proof = proof_none;` |
| `variable_decl` (640) | `decl_fast_flags` 660 | `mutable uint8_t static_init_proof = proof_none;` |

`class_definition` needs a forward declaration in ast.hpp if not already visible (`mcache_cls`
uses `const void*` — do NOT copy that; the guard compares typed pointers).

Rules: `ast_serializer.hpp` MUST NOT serialize any of these (no `k_jaibite_version` bump; the
version-8 field lists at 244-271 stay untouched). `parser.cpp`'s `ref_escape_marker` /
`mark_transient_reads` (`run_ast_marking_passes` 663-671) MUST NOT touch them. `structural_node_key`
(ast_serializer-based) is unaffected because the serializer ignores them.

## 3. class_definition / engine additions (checker/AST track owns these)

### 3.1 `class_definition::field_shape_epoch()` — `include/jaiscript/core/class_definition.hpp`

Beside `method_epoch_` (1750) and `bump_method_epoch` (318-326):

```cpp
    // Field-shape epoch (type proofs): moves only when a field proof's facts can change - field
    // set, declared types, resolution chain, access labels, custom accessors. Unlike
    // method_epoch it does NOT move on an identical reload. Propagates to derived like
    // bump_method_epoch (same engine-lifetime/no-ABA contract, 311-317).
    uint64_t field_shape_epoch() const { return field_shape_epoch_; }
    void bump_field_shape_epoch() {
        ++field_shape_epoch_;
        for (auto& weak : derived_classes_) { if (auto d = weak.lock()) d->bump_field_shape_epoch(); }
    }
    ...
    uint64_t field_shape_epoch_ = 1;
```

Bump sites (add `bump_field_shape_epoch();` next to the existing `bump_method_epoch();`):
`add_field` both overloads (534, 545); `replace_field_declared_types` when `changed` (556-575,
regardless of `mark_changes` — pins the "declared type changed ⇒ epoch moved" convention the
survey found unpinned); `set_parent` 1001, `add_parent` 1027, `set_parents` 1051,
`set_cpp_base_class` 1112; `register_property_setter` 1146; `set_nonpublic_members` 1301;
`add_method_by_id` when `!is_auto_accessor` (350 — a custom `_get_`/`_set_` registered after
definition); `redefine_class` full path once, beside the 1550 bump (NOT on the
`structurally_identical` shortcut 1476-1484). `reclass` needs nothing (class pointer changes).

### 3.2 `engine::note_class_shape_change()` — `engine.hpp` after `bump_class_definition_epoch()` (780)

```cpp
        // Script-class SHAPE change (first definition, non-identical redefinition): the static
        // checker's surface moved, cached entries re-check (and re-stamp proofs) at their next execute.
        void note_class_shape_change();
```
Body (`engine.cpp` beside 2574): `++impl->check_surface_epoch_;`. Call sites (three, all
gated so identical reloads do NOT re-check every cached entry): `interpreter.cpp` after
`eng->bump_class_definition_epoch();` (11418): `if (!is_redefinition) eng->note_class_shape_change();`;
`vm_backend.cpp` twin after 10340 with its `is_redefinition` (9929); `class_definition::redefine_class`
full path beside the 1550 bump: `if (engine_ref) engine_ref->note_class_shape_change();`.

## 4. Proof pass — checker/AST track

### 4.1 `ctype` grows a type_info (`source/implementation/type_checker.cpp:81-94`)

```cpp
struct ctype {
    ...
    kind_t kind = unknown;
    std::string_view cls{};
    const type_info* ti = nullptr;   // the interned type_info this ctype came from (null = kind only)
```
`ctype_from_type_info` (114-138): every returned ctype sets `ti` to the type_info it was derived
from — for `jai_object_type`/`jai_shared_ptr_type` `ti = ti_in` (so `ti->base_type` still tells
value object from handle); for `jai_reference_type` the recursive call's `ti` (the pointee).
Everything else in the file keeps comparing `kind`/`cls` only; `ti` is consulted solely by 4.2.
`infer_call`'s in-script ctor result (1126-1128) and `new_expr` (952) mint the object ctype with
`ti = eng_.get_type_info(type_info(script_value_type::jai_object_type, name))` (same interning the
parser uses, parser.cpp:684-686 → `engine::get_type_info(const type_info&)` engine.cpp:2497). The
engine-class ctor result at 1137-1139 does the same. `walk_variable_decl`'s `auto`+init entry
(1525-1528) inherits `ti` from `init` automatically.

### 4.2 Certainty and same_type (spell out exactly)

```cpp
    // Proof certainty: the payload guard (detail::same_type_payload) is TOTAL for these kinds -
    // every dynamic value the site can hold is either the identity case or falls to the ladder.
    bool known_class(std::string_view name) { return classes_.count(name) || engine_class(name) != nullptr; }
    static bool certain_scalar(const ctype& t) {
        return t.kind == ctype::int_k || t.kind == ctype::float_k || t.kind == ctype::bool_k ||
               t.kind == ctype::char_k || t.kind == ctype::string_k;
    }
    bool certain_object(const ctype& t) {
        return t.kind == ctype::object_k && t.ti && t.ti->base_type == script_value_type::jai_object_type &&
               !t.ti->type_name.empty() && known_class(t.cls);
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
type_info, every `var_entry.dynamic` (var, auto-without-init 1546, reference decls 1521, ref
params 1583, ref/parallel range-for, catch vars, destructured names), global-tier-ambiguous names
(`global_tier_name` 446), `extra_globals_`, `engine_global_type` results (543-553 — value-tag
derived; treat as unknown for proofs: never pass them to `same_type`), mixed ternaries, include
results. `assignable`'s lenient rows are conversions, never identity — `same_type` never consults
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
`diag()` (300-311): first line `if (!collect_diagnostics_) return;`. `undeclared()` goes through
`diag`. Proofs are independent of diagnostics (a stamp never depends on a `diag` having fired).

`fn_ctx` (294-306) gains `bool is_static_method = false; bool in_ctor_initializers = false;`.
`walk_function` (1553) sets `ctx.is_static_method = fn->is_static;` and wraps the initializer loop
(1567-1569) with `fn_stack_.back().in_ctor_initializers = true/false`. Bare-field proofs require
`bare_field_context()`:

```cpp
    bool bare_field_context() const {
        return class_ctx_ && !fn_stack_.empty() && fn_stack_.back().sees_fields &&
               !fn_stack_.back().is_static_method && !fn_stack_.back().in_ctor_initializers && !has_includes_;
    }
```
(`sees_fields` is false in `[=]`/`[&]` lambdas and true for `[this]` — 1693-1695; field
initializers are inferred in `walk_class` 1632-1642 with an EMPTY `fn_stack_` → excluded.)

### 4.4 Field-proof resolution (one function, used by 4.5 rows M1/M2/I1/I2)

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
        if (proofs_ != proof_mode::stamp || !hit.found || hit.methods) return std::nullopt;
        if (hit.field && hit.field->is_static) return std::nullopt;                 // statics live in class storage
        if (member == class_constants::CPP_OBJECT_FIELD) return std::nullopt;
        for (auto n : k_no_proof_names) { if (member == n) return std::nullopt; }   // builtin-shadow names (vm member_read_slot_probe negatives)
        if (global_tier_name(member)) return std::nullopt;
        auto def = engine_class(cls);                                               // null on a same-unit class's FIRST check (5.0)
        if (!def || def->chain_has_nonpublic() || !script_chain_closed(*def)) return std::nullopt;
        const uint64_t id = eng_.symbolize(member);
        if (!def->get_method(id, false).is_invalid()) return std::nullopt;         // method with the field's name: ladder decides
        if (def->find_field_declaring_class(id) == nullptr) return std::nullopt;
        const uint32_t slot = def->field_slot(id);
        if (slot == class_definition::k_no_field_slot) return std::nullopt;
        const auto [getter_id, g_] = eng_.get_symbolizer()->get_getter_id_with_view(id);
        if (!def->is_auto_accessor(getter_id) && !def->get_method(getter_id, false).is_invalid()) return std::nullopt;   // custom _get_
        const auto [setter_id, s_] = eng_.get_symbolizer()->get_setter_id_with_view(id);
        const bool custom_setter = !def->is_auto_accessor(setter_id) && !def->get_method(setter_id, false).is_invalid();
        type_info* declared = def->get_field_declared_type(id).get();
        if (hit.field) {                                                            // class also declared in THIS unit: spellings must agree
            const type_info* here = hit.field->type.get();                          // (the unit's redefinition runs before the proven site)
            if (here != declared) return std::nullopt;                             // interned pointers: identity == spelling
        }
        bool write_ok = !custom_setter;
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
`lookup_member`'s engine fallback (634-667) already returns `found=true, field=nullptr` for a
field declared on an engine class — `resolve_field_proof` re-derives everything from `def`, so the
module pattern (class defined by an earlier execute) is covered without a `variable_decl`.

Stamping a field proof onto a node `n` (identifier_expr or member_expr):
```cpp
    template <class N> void stamp_field(N* n, const std::optional<field_proof>& p) {
        stamp(n->proven_class, p ? p->cd : nullptr, (class_definition*)nullptr);
        stamp(n->proven_slot, p ? p->slot : UINT32_MAX, UINT32_MAX);
        stamp(n->proven_epoch, p ? p->epoch : 0ull, 0ull);
        stamp(n->proven_field_type, p ? p->declared : nullptr, (type_info*)nullptr);
        stamp(n->proven_field_write, p ? p->write_ok : false, false);
    }
```
Always call it (with `nullopt` on every refusal path) so a re-check that loses a proof clears it.

### 4.5 Stamping rules per node

| id | node / method | condition → stamp |
|---|---|---|
| S1 | `assignment_expr`, local target — `infer_assignment` 1189-1195 | `a->op.type == token_type::equal` (compound = `x = T(x op rhs)`, separate opcode; the parser's typed-store precedent 376-386 restricts to `=` too) `&& !local->dynamic && same_type(local->type, rhs)` → `stamp(a->static_store_proof, proof_same_type, proof_none)`. Else stamp `proof_none`. |
| S2 | `assignment_expr`, bare field target — 1197-1213 | `bare_field_context()`; `p = resolve_field_proof(class_ctx_name_, id->name, hit)`; `stamp_field(id, p)`; `static_store_proof = same_type` iff `op == equal && p && p->declared && same_type(ctype_from_type_info(p->declared), rhs)`. |
| S3 | `assignment_expr`, engine global — 1220-1228 | always `proof_none`. |
| S4 | `assignment_expr`, member target — 1236-1248 | `infer_member(..., writing=true, &typed_field, &recv_out, &hit_out)` (new out-params: receiver ctype + member_hit); `p = (recv.kind == object_k && m->object is not super_expr && !m->null_safe && !m->is_static) ? resolve_field_proof(recv.cls, m->member, hit) : nullopt`; `stamp_field(m, p)`; `static_store_proof = same_type` iff `op == equal && p && p->declared && same_type(ctype_from_type_info(p->declared), rhs)`. |
| S5 | `assignment_expr`, subscript/other — 1250 | `proof_none`. incdec desugar nodes (`incdec_desugar`/`postfix_old_result`) are compound → `proof_none` by S1's `op` test. |
| D1 | `variable_decl` — `walk_variable_decl` explicit type 1517-1523 | `has_init && !entry.dynamic && !is_reference_decl && decl->type->base_type not in {shared_ptr, weak_ptr, reference, any} && same_type(entry.type, init)` → `static_init_proof = same_type`; else `proof_none`. `shared_ptr<auto>` (1497-1515), `auto` (1525-1528) and no-init (1546): `proof_none`. Class-body field initializers (`walk_class` 1632-1642) are NOT `walk_variable_decl` and get no stamp (they run through `store_field_defaults`). |
| C1 | `call_expr` — `infer_call` | Candidate overload vector `ov` at exactly these resolution points: method via `lookup_member(class_ctx_name_, ...)` 1113-1118 (`hit.methods`), top-level `functions_` 1120-1124, in-script ctor 1125-1127 (`classes_[name].ctors`), member method 1152-1155 (`hit.methods`). Stamp iff `!has_includes_ && ov.size() == 1 && fn = ov[0]; !fn->is_coroutine && fn->body && arity_range(fn) == {n, n} && n == c->arguments.size() && no param is_reference` and, for the bare-name method row, `!global_tier_name(name)` (already enforced). Then `static_arg_proofs.resize(n)`, per i: `1` iff `pt = ctype_from_type_info(fn->parameters[i].type.get()); pt.is_unknown() /*untyped or var*/ || same_type(pt, args[i])`, else `0`; `proven_callee_body = fn->body.get()`. Every other callee shape (callable local 1106-1110, engine ctor/function, `new_expr`, member on non-object): `static_arg_proofs.clear(); proven_callee_body = nullptr` (also under `clear`). Note `functions_` may hold several decls for one name ("later replaces earlier", 405-421) → `size()==1` refuses those. |
| R1 | `return_stmt` — `check_return` 1386-1414 | `fn = fn_stack_.back(); r->value && !r->binds_reference && !fn.is_coroutine && fn.ret && ret = ctype_from_type_info(fn.ret); ret.kind != null_k && same_type(ret, value)` → `static_return_proof = same_type`; else `proof_none`. Lambdas share `check_return` and the same consumer (`call_function` epilogue) — stamp them too. |
| M1 | `member_expr` read — `infer_member` 1056-1097 | `recv.kind == object_k && m->object not super_expr && !m->null_safe && !m->is_static && hit.found`: `stamp_field(m, resolve_field_proof(recv.cls, m->member, hit))`; every other exit of `infer_member` → `stamp_field(m, nullopt)`. |
| I1 | `identifier_expr` bare field read — `infer_identifier` 1026-1035 | `bare_field_context() && hit.found && !global_tier_name(...)`: `stamp_field(id, resolve_field_proof(class_ctx_name_, id->name, hit))`; every other exit (local hit 1013-1025 included) → `stamp_field(id, nullopt)`. |
| B1 | `binary_expr` — `infer` 917-924 | `op != left_bracket`: from `(l.kind, r.kind)`: int,int→1 float,float→2 int,float→3 float,int→4 string,string→5 bool,bool→6 else 0 (char pairs 0; char is promoted by `binary_result`, not in the enum). Annotation only. |

`infer_member` signature: `ctype infer_member(member_expr* m, bool writing, const variable_decl** typed_field_out = nullptr, ctype* recv_out = nullptr, member_hit* hit_out = nullptr)`; fill the out-params right after `recv`/`hit` are computed (1057, 1078).

Recheck idempotence: every rule writes its field on EVERY visit (stamp or `proof_none`/`nullopt`),
so a later re-check with a moved surface can only add or drop proofs, never leave a stale one
that the pass no longer derives.

### 4.6 `run_static_check` API — `include/jaiscript/detail/type_checker.hpp:84` / `type_checker.cpp:1975`

```cpp
    namespace detail {
        struct check_options {
            bool diagnostics = true;                     // false: report stays empty (trusted-jaibite re-stamp)
            enum class proofs_t : uint8_t { leave, stamp, clear } proofs = proofs_t::stamp;
        };
        check_report run_static_check(engine& eng, const std::vector<declaration_ptr>& decls,
                                      const std::vector<std::string>* extra_globals = nullptr,
                                      check_options opts = {});
    }
```
`static_checker` ctor takes `opts` and sets `proofs_`/`collect_diagnostics_`. Existing callers
compile unchanged.

### 4.7 Engine hook points — `source/implementation/engine.cpp`

State: `implementation` (664-666) gains `bool type_proofs_ = true;`. `script_cache_entry`
(627-637) gains `uint64_t proof_epoch = UINT64_MAX;   // UINT64_MAX = nodes carry no proofs`.
`jaibite` (jaibite.hpp:76-79, friend engine) gains `uint64_t proof_epoch_ = UINT64_MAX;`.
`engine.hpp` after `static_checking()` (209): `void type_proofs(bool on); bool type_proofs() const;`
(setter: `impl->type_proofs_ = on;` — nothing else; the next `checked_report`/`check_bite`
re-runs with `stamp` or `clear` as needed).

`checked_report` (668-675) becomes the ONE cached entry point, mode-independent:
```cpp
    const check_report& checked_report(engine& self, script_cache_entry& entry, bool trusted_clean = false) {
        const uint64_t want_proof_epoch = type_proofs_ ? check_surface_epoch_ : UINT64_MAX;
        const bool need_check = !entry.checked || entry.checked_epoch != check_surface_epoch_;
        const bool need_proofs = entry.proof_epoch != want_proof_epoch;
        if (need_check || need_proofs) {
            detail::check_options o;
            o.diagnostics = need_check && !trusted_clean;
            o.proofs = type_proofs_ ? detail::check_options::proofs_t::stamp : detail::check_options::proofs_t::clear;
            check_report r = detail::run_static_check(self, entry.declarations, nullptr, o);
            if (o.diagnostics) { entry.check_result = std::move(r); }
            entry.checked = true;
            entry.checked_epoch = check_surface_epoch_;
            entry.proof_epoch = want_proof_epoch;
        }
        return entry.check_result;
    }
```
Hook sites (each calls `checked_report` UNCONDITIONALLY — all modes including `off` — and
keeps its mode gate only around `last_check_`/strict):
- `execute_source` 1470-1486: `const check_report& rep = impl->checked_report(*this, *cached);` first; then the existing `if (mode != off)` block uses `rep` for the `instanceVars.empty()` case; the non-empty case keeps its fresh `run_static_check(..., &extra, {diagnostics=true, proofs=leave})` (per-call, uncached, diagnostics only — `leave` so it never disturbs the entry's stamps). Order stays: check → `write_jaibite_sibling` 1491 → `execute_parsed` 1494.
- `execute_cache_hit` 679-698: same shape (checked_report first, mode gate after; instanceVars case `leave`).
- `engine::check` 1659-1672: unchanged call (now also stamps).
- `try_load_jaibite_entry` 856-884: keep the trusted stamp (`checked=true, checked_epoch=...`); `proof_epoch` stays `UINT64_MAX`; the entry's first `checked_report` call passes `trusted_clean = (flags & k_jaibite_flag_checked_clean)` — store that bit on the entry (`bool trusted_clean = false;`) so the proof re-run keeps `check_result` empty exactly as today.
- `jaibite_load_bytes` 1626-1645, `engine::jaibite` 1549-1562, `execute(jaibite&)` 1580-1586: call `check_bite(bite, source)` UNCONDITIONALLY; `check_bite` (1566-1578) becomes:
```cpp
void engine::check_bite(jai::jaibite& bite, const std::string* source) {
    const uint64_t want = impl->type_proofs_ ? impl->check_surface_epoch_ : UINT64_MAX;
    const bool need_check = bite.check_state_ == jaibite::check_state::unchecked;
    const bool need_proofs = bite.proof_epoch_ != want;
    if (need_check || need_proofs) {
        detail::check_options o;
        o.diagnostics = need_check;
        o.proofs = impl->type_proofs_ ? detail::check_options::proofs_t::stamp : detail::check_options::proofs_t::clear;
        check_report r = detail::run_static_check(*this, bite.declarations_, nullptr, o);
        if (need_check) {
            bite.check_report_ = std::move(r);
            bite.check_state_ = bite.check_report_.has_errors() ? jaibite::check_state::flagged : jaibite::check_state::clean;
        }
        bite.proof_epoch_ = want;
    }
    if (impl->static_check_mode_ == check_mode::off) { return; }      // diagnostics surface only under warn/strict
    impl->last_check_ = bite.check_report_;
    if (impl->static_check_mode_ == check_mode::strict && bite.check_report_.has_errors()) { throw static_check_error(...); }
}
```
A trusted-clean loaded bite (`check_state_ == clean` from the flag, 1638-1640) has
`need_check == false` → diagnostics stay empty, proofs stamp. "A bite is checked once" is
preserved for diagnostics; proofs re-stamp on surface moves.

Ordering guarantee (P1): every hook above runs AFTER `run_ast_marking_passes` — parse tail
(parser.cpp:728 → `store_cached_script` 1466 → 1470), jaibite twins (873 → 874 → first
`checked_report`; 1632 → `check_bite` 1641), bite creation (parser 728 → 1558).

What bumps `check_surface_epoch_`: existing 1822 `add_global`, 1862 `register_overload_impl`,
1918 `add_class_impl`, 2110 `register_template_type`, plus NEW `note_class_shape_change` (3.2).
`bump_class_definition_epoch` (2574) itself stays as is.

Cost statement for the docs: one checker walk per unique source per surface epoch, in every mode
(replaces "zero when off"); the instanceVars per-call path never re-stamps.

## 5. Interpreter track — `source/implementation/interpreter.cpp`

Includes `detail/type_proofs.hpp`. Each site: guard → fast statements → **identical fall-through
to the existing code** (never restructure the existing branch; insert the proven branch in front
of the exact call it skips). All field-proof consumers are gated `!parallel_worker_` (workers keep
their walls at 4024-4041 / 7714-7727, which deref on read — a different shape).

### 5.0 First-execute note
A unit that declares its own classes gets no field proofs on its first check (`engine_class` is
null until `visit_class_decl` registers at 11414); its same_type store/decl/return/arg proofs DO
stamp (they use the in-script tables). Registration calls `note_class_shape_change` → the second
execute re-checks and stamps field proofs; identical reloads afterwards leave `field_shape_epoch`
alone so the stamps keep hitting.

### I-S1 Slot store — `visit_assignment_expr` 5180-5198
Replace the enforcement condition only:
```cpp
                        type_info_ptr slot_type = storage->get_type_info();
                        const bool proven_identity = expr->static_store_proof == proof_same_type && slot_type &&
                                                     detail::same_type_payload(value, slot_type.get());
                        if (!proven_identity && slot_type && (slot_type->base_type != value.type() ||
                                          slot_type->base_type == script_value_type::jai_object_type)) {
                            ...existing enforce_type_compatibility + var-tag rule, unchanged...
                        }
                        *storage = clone_for_assignment(value);    // unchanged
```
Existing `typed_store_provable` lane (5169-5177) stays in front, unchanged. `store_through`
(`ref_store_through`) stays unproven.

### I-S2 Env store — 5506
```cpp
                    if (!(expr->static_store_proof == proof_same_type && target_type &&
                          detail::same_type_payload(value, target_type.get()))) {
                        auto enforced = enforce_type_compatibility(std::move(value), target_type, identifier->name);
                        if (!enforced) { return enforced.error_value(); }
                        value = std::move(enforced.value());
                    }
```
Everything before (cell redirect, cpp_bound, weak/shared ladders 5254-5500) and after (any-tag
rules 5521-5532, the three result-push rules 5536-5559) unchanged.

### I-S3 Bare this-field store — 5208-5216 (before `probe_member_scope`)
```cpp
            bool member_wins = false;
            if (!identifier->global_qualified && member_scope_.active() &&
                !detail::own_scope_lookup(environment_.get(), member_scope_.floor, identifier->symbol_id)) {
                if (identifier->proven_class && identifier->proven_field_write && !parallel_worker_ &&
                    member_scope_.state == detail::member_scope_state::instance && member_scope_.this_storage) {
                    if (class_instance* inst = detail::proven_field_instance(*member_scope_.this_storage,
                                                  identifier->proven_class, identifier->proven_epoch)) {
                        // == the this-field landing 5623-5645 (KEEP BYTE-PARALLEL with vm store_popped_value's proven lane)
                        const bool is_lvalue_read = is_lvalue_expression(expr->value.get());   // same predicate as 5580-5588
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
                        else { push_value(inst->get_field(identifier->symbol_id)); }   // stored, declared-tagged value (5643 shape)
                        return {};
                    }
                }
                member_wins = ...existing probe...;
            }
```
`is_lvalue_expression` (2137) must equal the 5580-5588 predicate (identifier/member/subscript);
if it differs, inline the 5580-5588 test. Compound this-field stores (4560-4690) and `++f`
(3966-3990) stay unproven.

### I-D1 Decl — `visit_variable_decl` 6404-6410
```cpp
            if (decl->initializer && !(decl->static_init_proof == proof_same_type &&
                                       detail::same_type_payload(value, effective_decl_type.get()))) {
                auto enforced = enforce_type_compatibility(std::move(value), effective_decl_type, decl->name);
                ...
            }
```
6280-6316 scalar fast path, 6323-6333 clone rule, 6340-6392 homogeneity, 6415-6428
`set_type_info`, 6444 define: unchanged.

### I-C1 Argument binding — `call_function`
`detail::call_site_context` (environment.hpp:460-462) gains `const call_expr* site = nullptr;`.
Both arming sites build `ctx{&expr->arguments, expr}` (7210, 7324). After `pending_call_ctx_` is
consumed (12627-12628):
```cpp
    const uint8_t* arg_proofs = nullptr;
    if (call_ctx && call_ctx->site && call_ctx->site->proven_callee_body == function.body.get() &&
        call_ctx->site->static_arg_proofs.size() == function.parameters().size() &&
        args.size() == function.parameters().size()) {
        arg_proofs = call_ctx->site->static_arg_proofs.data();
    }
```
In the value-param branch, wrap ONLY 12873-12880 (exact-class lane 12861-12869 stays in front):
```cpp
            script_value converted_arg = make_value();
            const bool untyped = !param.type || param.type->base_type == script_value_type::jai_any_type;
            if (arg_proofs && arg_proofs[i] == proof_same_type &&
                (untyped || detail::same_type_payload(arg.deref(), param.type.get()))) {
                // try_convert_for_parameter's identity returns: `arg` (13157/13183), `derefed_arg` for exact-name objects (13170)
                converted_arg = (!untyped && param.type->base_type == script_value_type::jai_object_type)
                                ? script_value(arg.deref()) : script_value(arg);
            } else {
                auto convert_result = try_convert_for_parameter(arg, param.type);
                ...unchanged...
            }
```
`should_share` + `clone()` + `boxed_param` (12882-12905) unchanged; parameters are never
retagged. Defaults (`i >= args.size()`) and reference params never see proofs by construction.
`construct_instance` (11813) routes the ctor body through `call_function` → same code; callee
identity (`function.body` == the single ctor's body) is what makes a wrong-overload stamp miss.

### I-R1 Return — `visit_return_stmt` 10311-10360 + `call_function` 13004-13017
New member `bool return_proven_ = false;` (interpreter.hpp beside `hasReturnValue_`). Written
wherever `hasReturnValue_` is written: `false` at 10334/10345 (ref paths) and at every reset
(12673-12700 cleanup, 12950-12975 yield, 12758/12785 save/restore as `previousReturnProven`,
2714-2743 and 13460 coroutine state twins); `stmt->static_return_proof == proof_same_type` at 10359.
Epilogue:
```cpp
            if (function.return_type && ...concrete test unchanged... ) {
                if (!(return_proven_ && detail::same_type_payload(result, function.return_type.get()))) {
                    auto convert_result = try_convert_for_parameter(result, function.return_type);
                    ...unchanged...
                }
            }
```
`resume_coroutine` 13520-13540 stays unproven (identity anyway).

### I-M1 Member read — `visit_member_expr`, after the null_safe check (7654-7657), before `if (is_super_access)` (7660)
```cpp
    if (expr->proven_class && !parallel_worker_) {
        class_instance* inst = nullptr;
        if (script_value* cell = detail::proven_field_cell(objectValue, expr->proven_class, expr->proven_epoch, expr->proven_slot, &inst)) {
            push_value(*cell);   // == push_value(target.get_field(member_id)) at 8051: cell copy, no deref, no materialize
            return {};
        }
    }
```
(`objectValue` is already deref'd at 7651.) Skipped ladder rungs are all excluded at stamp time or
covered by the epoch: super (stamp), worker wall (gate), coroutine handle / cpp / transparent
wrappers (not the proven class object), shared_ptr builtin names + `same_as` (names), access
check (`chain_has_nonpublic` stamp + epoch), getter probe (stamp + epoch), `has_field` (present
cell). `resolve_member_target`'s extended-layer re-point (2196-2208): a not-yet-re-pointed plain
wrapper has a cpp class object → guard miss → ladder re-points.

### I-M2 Member write — `visit_assignment_expr` member branch, before `assign_member_value(dereferenced, memberExpr, value)` (5758)
```cpp
            if (memberExpr->proven_class && memberExpr->proven_field_write && !parallel_worker_) {
                if (class_instance* inst = detail::proven_field_instance(dereferenced, memberExpr->proven_class, memberExpr->proven_epoch)) {
                    // == assign_member_value's field branch 4066-4072 minus the ladder (KEEP BYTE-PARALLEL with vm exec_set_member's proven lane)
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
                    push_value(std::move(value));   // what the branch pushes after assign_member_value (read the lines after 5758 and match exactly)
                    return {};
                }
            }
```
Setter precedence (4057-4065) is excluded at stamp (custom `_set_` → `proven_field_write=false`;
cpp-base setters → no cpp base allowed), access check by stamp + epoch.

### I-I1 Bare field read — `visit_identifier_expr` 2958-2975, after `own_scope_lookup` misses, before `probe_member_scope`
```cpp
        if (expr->proven_class && !parallel_worker_ &&
            member_scope_.state == detail::member_scope_state::instance && member_scope_.this_storage) {
            class_instance* inst = nullptr;
            if (script_value* cell = detail::proven_field_cell(*member_scope_.this_storage, expr->proven_class, expr->proven_epoch, expr->proven_slot, &inst)) {
                push_value(*cell);   // == push_value(*member.value_ptr) 2973: copy, no deref
                return checked_result<void>();
            }
        }
```
A null cell falls to `probe_member_scope`, which materializes the default (non-const `get_field`)
exactly as today. The non-member-frame this-fallback (3009-3040) stays unproven.

## 6. VM track — `source/implementation/vm/vm_backend.cpp`, `vm_compiler.cpp`, `chunk.hpp`, `opcode.hpp`

**No new opcode.** Carriers, by proof:

| proof | carrier | freshness |
|---|---|---|
| `static_store_proof` (identifier target) | `store_flag_same_type = 32` in `op_store.c` (opcode.hpp after 166), mirrored into `fused_binary_dst_proto.flags` (chunk.hpp:233) at the two peepholes | frozen at first compile; safe via `same_type_payload` |
| `static_init_proof` | node: `op_decl_var.a` → `variable_decl` read at exec | live |
| `static_arg_proofs` / `proven_callee_body` | `call_site.call_node` (NEW `uint32_t call_node = k_invalid_u32;` in `call_site`, chunk.hpp:60) = `add_node(expr)`; read at push time | live |
| `static_return_proof` | `op_return.b` (free); `op_return_ident.c` bit `load_flag_return_proven = 4` (opcode.hpp:152-153; `fused_ident_value` tests flags with `&`, so an extra bit is inert); `op_return_binary.c` (free) | frozen; safe via guard |
| member_expr field proofs | node: `op_get_member.a` / `op_set_member.a` | live |
| identifier_expr field proofs | `chunk::ident_nodes` (NEW `std::vector<uint32_t> ident_nodes;` beside `nodes`, chunk.hpp:284; empty or `code.size()` long, `k_invalid_u32` default) for `op_load`/`op_store`/`op_binary_fused_store`/`op_return_ident` ips, and `fused_operand::ident_node` (NEW `uint32_t ident_node = k_invalid_u32;` chunk.hpp:36-40) | live |

Compiler fills (`vm_compiler.cpp`): every identifier compiled with `identifier_slot_operand(ident)
== k_invalid_u32` (env-resolved — field names are never slotted) records `(ip, add_node(ident))`
into a compiler-local vector at: `op_load` emit 1566, `op_store` emits 1395/1953 (and the
in-place fused rewrite keeps the same ip), `op_return_ident` 780, fused operand builder 1826 (sets
`out.ident_node`), dst-proto fills 1405/1963 (`dp.ident_node` if the dst proto resolves through
`fused_operand`; otherwise the ip entry suffices). At every compile tail (`compile_callable`
580-590 and the top-level compile's tail) materialize `ident_nodes` sized `code.size()` when the
local vector is non-empty. `call_site.call_node = add_node(expr)` in both call-site builders
(2103-2137 method calls, 2169-2189 plain calls). `store_flag_same_type` set beside
`store_flag_type_provable` at 1395/1953 (`if (assign->static_store_proof == proof_same_type)`),
independent of `typed_store_provable`. Return flags at 808 (`emit(op_return, 1, proof)`), 780
(`flags |= load_flag_return_proven`), 799 (`code.back().c = proof` when rewriting the trailing `op_binary` to
`op_return_binary` — `op_binary` uses only `a`/`b`, opcode.hpp:32, so `c` is free).

Gates: every field-proof consumer requires `cached_global_env_` (null on workers; the proven lanes
write no chunk state, but the worker walls in `assign_member` 8850-8867 / `member_access_value`
must stay ahead). Invariant 2b (docs/invariants.md): never hold a raw `stack_`/slot pointer across
`frame_slot_set`; field cells are unique_ptr-stable and the frame's `this` anchor pins the instance.

### V-S1 Slot store — `store_popped_value` 3367-3386
```cpp
			type_info_ptr slot_type = storage->get_type_info();
			const bool proven_identity = (ins.c & store_flag_same_type) && slot_type &&
			                             detail::same_type_payload(value, slot_type.get());
			if (!proven_identity && slot_type && (slot_type->base_type != value.type() ||
			                  slot_type->base_type == script_value_type::jai_object_type)) { ...unchanged... }
			*storage = clone_for_assignment(value);
```
Existing `store_flag_type_provable` lane (3349-3362) stays in front.

### V-S2 Env store — 3726
```cpp
		if (!((ins.c & store_flag_same_type) && target_type && detail::same_type_payload(value, target_type.get()))) {
			auto enforced = enforce_type_compatibility(std::move(value), target_type);
			...unchanged...
		}
```
weak/shared ladders, `operator=` dispatch (3700-3720), var-tag rules, store_back/push: unchanged.

### V-S3 Bare this-field store — inside the member_wins block 3431-3440, after `own_scope_lookup` misses, before `probe_member_scope`
Node: `identifier_expr* idn = (code.ident_nodes.size() > f.ip && code.ident_nodes[f.ip] != k_invalid_u32) ? static_cast<identifier_expr*>(code.nodes[code.ident_nodes[f.ip]].get()) : nullptr;`
```cpp
			if (idn && idn->proven_class && idn->proven_field_write && cached_global_env_ &&
			    frame_scope_state(f) == detail::member_scope_state::instance && f.locals->scope_this) {
				if (class_instance* inst = detail::proven_field_instance(*f.locals->scope_this, idn->proven_class, idn->proven_epoch)) {
					// == the store-side this-field IC hit 3397-3422 with the conversion gated by the proof
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
The existing this-store IC (3391-3422) stays ahead of it (a hit there is already the fast case);
`this_field_store_arm` is not called from the proven lane (unproven sibling sites arm themselves).

### V-D1 Decl — `exec_decl_var` 6912-6924
```cpp
		if (has_init && !(decl->static_init_proof == proof_same_type &&
		                  detail::same_type_payload(value, effective_decl_type.get()))) {
			auto enforced = enforce_type_compatibility(std::move(value), effective_decl_type);
			...unchanged...
		}
```
Fused decl replays (`exec_binary_fused_decl` 4840, `exec_index_fused_decl` 6216) reach the same
statement or the identity-gated `decl_expect` lane — nothing else changes.

### V-C1 Argument binding — `bind_parameters` 13505 (template) + six call sites
Helper (member):
```cpp
	const uint8_t* site_arg_proofs(const call_site* site, const chunk* caller_code, const block_stmt* body, size_t nparams, size_t argc) const {
		if (!site || !caller_code || site->call_node == k_invalid_u32 || argc != nparams) return nullptr;
		auto* ce = static_cast<const call_expr*>(caller_code->nodes[site->call_node].get());
		if (ce->proven_callee_body != body || ce->static_arg_proofs.size() != nparams) return nullptr;
		return ce->static_arg_proofs.data();
	}
```
`bind_parameters` gains a trailing `const uint8_t* arg_proofs = nullptr`. Call sites pass
`site_arg_proofs(site, caller_code, <body>, parameters.size(), argc)` where `<body>` is
`function.body.get()` (7345 push_script_frame_pinned, 7388 tail_splice_frame, 12630
push_script_frame, 13901 call_script_function) or `rec.ast_pin->body.get()` (12853
push_method_frame, 13008 external-args method push). Never feed proofs into the method IC
(`mic_static` already covers all-untyped params).
In the value-param branch, wrap ONLY 13755-13770 (primitive in-place lane 13716-13742 stays in front):
```cpp
			script_value arg_shield(arg);                                   // kept: byte-parallel, cheap
			const bool untyped = !param.type || param.type->base_type == script_value_type::jai_any_type;
			script_value converted_arg = make_null();
			if (arg_proofs && arg_proofs[i] == proof_same_type &&
			    (untyped || detail::same_type_payload(arg_shield.deref(), param.type.get()))) {
				converted_arg = (!untyped && param.type->base_type == script_value_type::jai_object_type)
				                ? script_value(arg_shield.deref()) : script_value(arg_shield);
			} else {
				...existing exact-class lane 13755-13763 + try_convert_for_parameter 13766, unchanged...
			}
			...should_share / clone / boxed_param 13773-13789 unchanged...
```

### V-R1 Return — `return_with_result` 13133
Signature `return_with_result(frame*& fp, script_value result, bool proven = false)`;
`return_from_script_frame` 13125 passes `ins.b != 0`; `exec_return_ident` 13219 passes
`(ins.c & load_flag_return_proven) != 0`; `exec_return_binary` 13249 passes `ins.c != 0`. In the
switch, `case return_conv::check:` (and `unclassified` when `rec.return_type` is concrete):
```cpp
			if (proven) {
				const script_value& probe = result.is_reference() ? result.deref() : result;
				deref_only = detail::same_type_payload(probe, rec.return_type.get());   // full route otherwise
			}
```
`deref_only` tail (deref, anchor, ctor stamp, pop, land) unchanged. The native/fiber epilogue
(13929-13935) stays unproven (identity anyway).

### V-M1 Member read — `exec_get_member` 8784, before the `member_ic` block
```cpp
	if (expr->proven_class && cached_global_env_) {
		class_instance* inst = nullptr;
		if (script_value* v = detail::proven_field_cell(object, expr->proven_class, expr->proven_epoch, expr->proven_slot, &inst)) {
			stack_.push_back(*v);   // raw cell copy, references stay references (== the member_ic hit 8811)
			return {};
		}
	}
```
`member_ic` stays for unproven sites and for guard misses. `fused_member_container` (8743) is not
changed in v1.

### V-M2 Member write — `exec_set_member` 8909, before the `member_ic` block
```cpp
	if (member->proven_class && member->proven_field_write && cached_global_env_) {
		if (class_instance* inst = detail::proven_field_instance(dereferenced, member->proven_class, member->proven_epoch)) {
			script_value incoming = clone_for_assignment(value);
			if ((ins.c & store_flag_same_type) /* see note */ ...
```
Note: `op_set_member` has no store flags; carry the assignment's `static_store_proof` in
`op_set_member.b` (free: the emit at vm_compiler.cpp:2033 sets only `a`): `emit(op_set_member,
add_node(member), expr->static_store_proof)` in `compile_assignment`'s member branch. Then:
```cpp
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

### V-I1 Bare field read — `exec_load` 3211-3226 and `fused_ident_value` 4541-4575, after `own_scope_lookup` misses, before `probe_member_scope`
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
`this_field_ic_probe` (3194 / 4541) stays ahead; the proven lane replaces only the probe's hash
lookups on the first hit and after serial churn. `op_return_ident` builds its operand from `ins`
(13219-13224): set `operand.ident_node = ident_nodes[f.ip]` there.

### Freshness statement
Node-borne proofs (decl, member, call, ident) see re-stamps live even in a chunk compiled earlier.
Instruction-borne flags (`store_flag_same_type`, return bits) never gain proofs after first
compile — toggling `type_proofs(true)` later or a surface move that adds store/return proofs only
benefits fresh chunks; correctness is unaffected (guards). No chunk/compiled-slot invalidation is
added in v1.

## 7. Tests (existing suite files only)

Backend selection: `foundry::make_engine()` honors `--backend=vm` (main_test_runner.cpp:26-32);
run every suite below under both. Where a test needs proofs on AND off in one process, build
engines explicitly (the `quad` pattern, strong_types_tests.cpp:1479-1503).

`source/tests/semantics/static_check_tests.cpp` (suite "Static Type Checking"):
- `proofs_default_on_and_switch_roundtrip` — `type_proofs()` true by default; false/true roundtrip; toggling off then executing a cached source clears stamps (observe via `engine::check` + an inspection hook: expose `const std::vector<declaration_ptr>* engine::debug_cached_declarations(const std::string&)` behind `JAISCRIPT_TESTING` or reuse an existing AST accessor if one exists — otherwise assert behaviorally only).
- `proof_battery_same_type_stores` — for each of int/float/bool/char/string/object: local `=`, env global `=`, `this.f =`, bare `f =`, `o.f =`, decl init, arg bind (typed + var param), typed return; each snippet run 4 ways {interp,vm}×{proofs on,off}, outputs equal; include `type_of(x)` after the store and a subsequent mismatching store to prove tags are unchanged (e.g. `int x = 1; x = 2; x = 2.5; type_of(x)`).
- `proof_battery_not_certain` — var/auto-no-init/shared_ptr<T>/weak_ptr/array<int>/map/reference/ref-param/include/callable-local/coroutine/null RHS/typed-null placeholder (`C a; C b = a;`)/derived-into-base/shared_ptr-into-object: all 4 ways equal, and where the unproven path errors the text is identical (compare `what()` strings).
- `proof_call_arg_identity_guard` — global function reassigned between calls (`f = other;`), later `execute` redefining `f` with a different signature, overloaded methods, defaults, ref params: 4-way equal; error texts equal.
- `proof_member_second_execute` — same source executed 3× on one engine (first: no field proofs; second: stamped; third: identical reload keeps hitting); results equal 4-way; plus module pattern (class in execute 1, users in execute 2).
- `proof_recheck_on_class_shape_change` — `note_class_shape_change` bumps: after a NON-identical redefinition a cached user script re-stamps; after an identical redefinition `check_surface_epoch_` does not move (observe via `engine::check` result caching or a counter of checker runs behind `JAISCRIPT_TESTING`).
- `proof_jaibite_paths` — bite created under `off`, saved, loaded (trusted stamp), executed: results 4-way equal; `checked_clean()` unchanged by the proof re-stamp; `check_diagnostics()` still empty for a trusted-clean load.
- `proof_differential_gate` — extend the `fuzz_corpus_false_positive_gate` loop (780-841): 200 seeds of `fuzz::generate_program(seed, 3)`; per seed build FOUR engines {backend ∈ interp,vm} × {type_proofs ∈ true,false} with identical registrations (`print` capture into a string, `to_string`, budget 0.1 s, memory cap 64 MB); per segment record `"VALUE " + describe` / `"ERROR " + what()` (reuse `fuzz::run_program` by adding `bool proofs = true;` to `run_config`, applied as `eng->type_proofs(cfg.proofs)` right after `set_backend` at fuzz_harness.hpp:153-154); skip a seed when any engine hit the budget (`budget_hit`); otherwise all four `segment_results`, `printed` and `alive` must be identical; first divergence in the message with `seed`, backend and proofs flag. Assert `compared_programs > 0`.

`source/tests/language/hot_reload_correctness_tests.cpp` (suite "Hot Reload Correctness") — all
run on both backends; each executes a method that stores into a typed field via BOTH spellings
(`this.f = v` / `f = v`) and reads it back (`this.f` / `f`), THREE times before the reload (arms
first-execute → stamped → identical-reload-hit), then reloads, then calls again:
- `proof_field_retype_int_to_string_assigns` — `int f` → `string f`: value converts ("42"), store of an int afterwards converts via the ladder, never errors.
- `proof_field_retype_float_to_int_truncates`, `proof_field_retype_string_to_int_resets_default` (unconvertible → new default; subsequent stores enforce as int).
- `proof_field_removed_then_read_errors_like_unproven` — field dropped on reload: read errors with the identical text under proofs on/off.
- `proof_field_gains_custom_getter_on_reload` / `proof_field_gains_custom_setter_on_reload` — accessor added by reload wins after the reload (proof misses via epoch).
- `proof_field_access_label_change_on_reload` — field becomes private on reload; access error identical.
- `proof_base_reload_invalidates_derived_proofs` — base gains a field/retype; derived-method proofs miss and re-stamp.
- `proof_same_source_reexecute_vm_chunk_reuse` — the SAME source string executed repeatedly (vm reuses `entry.compiled`), reload between executes; results equal.
- `proof_extend_reclass_misses` — `extend(obj, Class)` mid-run on a proven receiver: guard misses (class pointer), ladder result identical.
Every test asserts equality against a `type_proofs(false)` twin engine, never a literal alone.

`source/tests/vm/vm_backend_tests.cpp` (suite "VM Backend"):
- `proof_carriers_survive_recompile_and_frozen_flags` — a store/return site compiled with proofs off then engine `type_proofs(true)` + re-execute: results equal (frozen flags never gain proofs, never break).
- `proof_ident_nodes_and_call_node_fill` — bodies with env-resolved identifiers and calls get `ident_nodes`/`call_node`; slot-resolved identifiers do not (disassembler or a `JAISCRIPT_TESTING` accessor).

`jaiscript_fuzz` (`source/tests/fuzz/fuzz_main.cpp`): option `--no-proofs` (parse_args 61-81 →
`opt.proofs=false`, forwarded in `run_isolated` 179-185); `fuzz_main` passes `opt.proofs` through
`run_config`. Cheap, so include it. `fuzz_smoke_tests.cpp` (suite "Differential Fuzz Smoke") gains
`fixed_seeds_no_new_divergence_no_proofs` running its first 40 seeds with `proofs=false` on both
backends (interp-vs-vm parity must hold in both proof states).

Green gates before merge: `jaiscript_tests.exe` and `jaiscript_tests.exe --backend=vm` fully
green; `jaiscript_fuzz --seed-start 0 --iterations 300 --max-depth 4 --timeout-ms 250 --isolate`
with and without `--no-proofs`: no new divergences.

## 8. Docs — `docs/static_checking.md`

Add `## Proofs (2026-09)` after "The false-positive contract" (95-102): what a proof is, the
lane/guard principle, the certain set (= the complement of the lenient rows in the rule table),
`type_proofs(bool)`, "runs in every mode", hot reload (shape epoch), jaibite (re-derived, never
serialized), what is NOT proven in v1. Rewrite "Cost" (114-120): one walk per unique source per
surface epoch in every mode; `type_proofs(false)` restores the old zero-when-off. Amend the jaibite
paragraph (104-112): proofs re-stamp on surface moves while diagnostics stay checked-once. Timeless
snapshot voice; no change narrative.

## 9. Ordered checklists

### Checker/AST track
1. `ast.hpp` constants + fields (section 2); `class_definition` forward decl. Done when: Debug build green, `jaiscript_tests` unchanged.
2. `class_definition.hpp` `field_shape_epoch` + bump sites; `engine::note_class_shape_change` + 3 call sites (section 3). Done when: `hot_reload_correctness_tests` + `hot_reload_tests` green both backends; a temporary assert that `field_shape_epoch` does not move across an identical reload passes.
3. `type_proofs.hpp` kernel (section 1). Done when: compiles standalone (include-what-you-use), static asserts hold.
4. `ctype.ti` + certainty/same_type predicates (4.1-4.2). Done when: existing "Static Type Checking" suite unchanged (no new diagnostics).
5. `check_options`, `proofs_`, `collect_diagnostics_`, `stamp`, `fn_ctx` flags (4.3, 4.6). Done when: `run_static_check` old callers compile, suite unchanged.
6. `resolve_field_proof` + stamping rows S1-S5, D1, C1, R1, M1, I1, B1 (4.4-4.5). Done when: a `JAISCRIPT_TESTING` dump of stamps for the battery snippets matches the table (unit-check the refusal list explicitly: statics, `_cpp_object`, builtin names, custom accessors, cpp base, nonpublic, open chain, spelling disagreement, static methods, ctor initializers, `[=]` lambdas, includes).
7. Engine hooks (4.7): `type_proofs_`, `proof_epoch`, `checked_report`, five hook sites, `check_bite`, `trusted_clean`. Done when: `static_check_tests` incl. the new `proofs_*` tests green on both backends with consumers not yet landed (proofs stamped, nothing consumed — results trivially equal); `off`-mode battery (`off_mode_zero_behavior_change_battery`) still green.
8. Docs (section 8).

### Interpreter track (starts after checker step 3; can run parallel to steps 4-7 using a hand-stamped AST in tests)
1. I-D1, I-S1, I-S2 (scalar/string/object same_type). 2. I-C1 (+ `call_site_context.site`). 3. I-R1 (+ `return_proven_` plumbing). 4. I-M1, I-I1 (reads). 5. I-M2, I-S3 (writes).
Done when, per step: full suite green (interpreter), battery + hot-reload tests green, `jaiscript_fuzz --iterations 300` (interp-vs-vm) unchanged — the vm may lag; parity is judged at the end of both tracks.

### VM track (same prerequisites)
1. Carriers: `store_flag_same_type`, `load_flag_return_proven`, `call_site.call_node`, `chunk::ident_nodes`, `fused_operand::ident_node`, `op_set_member.b`, compiler fills. Done when: disassembler/`get_current_frame_locals` unaffected, suite green `--backend=vm`.
2. V-D1, V-S1, V-S2. 3. V-C1 (+ `site_arg_proofs`, six call sites). 4. V-R1. 5. V-M1, V-I1. 6. V-M2, V-S3.
Done when, per step: `--backend=vm` suite green; interp-vs-vm fuzz (300 seeds, both `--no-proofs` states) no new divergences.

### Test track
1. `run_config.proofs` + `--no-proofs` + smoke test. 2. `proof_differential_gate`. 3. Batteries. 4. Hot-reload tests. 5. VM carrier tests. Done when: everything in section 7 green both backends, Release pass green.

## 10. Out of scope for v1 (explicit)

- Conversion proofs (int→float widening, bool/string coercions, null→object placeholder, derived→base, ctor conversions): `same_type` only.
- Container same_type / covariant `array<Base> = array<Derived>` proofs; typed-map proofs.
- `binary_expr::static_operand_class` consumption (annotation only; `has_custom_numeric_ops_`/operator tables can change after the check).
- Proven callee dispatch (skipping overload resolution / method IC by proof); proofs for callable locals, engine functions, `new_expr`, coroutine callees, defaulted or reference parameters.
- Proofs on compound assignment / `++`/`--` desugars, `store_through` references, `environment::assign`'s method-env fallback, compound this-field stores, static fields, `super.f`, `?.` receivers, engine globals, `_cpp_object`, hybrid (cpp-base) classes, extended layers, transparent wrappers.
- Native-entry / coroutine / fiber return epilogues; `fused_member_container`; worker (parallel) frames.
- Invalidating compiled vm chunks on re-stamp; serializing proofs in `.jaibite`; a per-call re-stamp on the instanceVars path.
- Any change to error texts, `type_of` results, tag stamping, clone/value semantics, or reference normalization.
