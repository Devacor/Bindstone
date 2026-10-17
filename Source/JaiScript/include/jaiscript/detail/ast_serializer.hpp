#pragma once

#ifndef __JAISCRIPT_DETAIL_AST_SERIALIZER_HPP__
#define __JAISCRIPT_DETAIL_AST_SERIALIZER_HPP__

// Binary AST serializer for jaibite save/load. Interned symbol IDs are per-engine and
// non-deterministic, so every symbol/name is written as a string-table index and
// re-interned into the LOADING engine's symbolizer; type_info is written structurally
// and re-interned via engine::get_type_info. The compiled VM chunk is never written —
// a loaded bite recompiles lazily through the normal path. Fixed-width fields and floats
// are written little-endian on every host; the source hash reads its words in host order.
//
// Format: "JBIT" magic, u32 version, u32 flags, u64 registration fingerprint, u64 source
// size + 1 and u64 source hash (0, 0 = no source), string table (count + length-prefixed strings), declaration count, node tree.
// Node encoding: u8 node_type tag (0xFF = null pointer), source_location, fields.
// Runtime AST patches (identifier slot_index -> SIZE_MAX, outer_slot_plan, cached
// getter_id) are serialized faithfully: patched state is self-consistent in memory on
// both backends, so a faithful round trip preserves behavior (save-after-execute test).

#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/map_entry_binding.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <jaiscript/core/engine.hpp>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace jai::detail {

    inline constexpr uint8_t k_jaibite_magic[4] = { 'J', 'B', 'I', 'T' };
    // BUMP THIS on ANY change to what a parse produces: serialized bytes AND parse-time
    // semantics baked into the AST — slot assignment, node/token enum values,
    // capture plans. A stale sidecar is silently reparsed + rewritten (the loader's
    // version check feeds the normal corrupt-fallback), so bumping is always safe
    // and never bumping leaves stale caches (a stale slot_index).
    //   v1: original format
    //   v2: traditional-for initializer decls carry slot_index (parser slotting)
    //   v3: \xNN escapes in string/char literals (lexer; literal bytes baked into AST)
    //   v4: literal JSON-style map keys ({1: "a"}, float/char/bool too; parser map-literal
    //       detection - previously-rejected inputs only, but the contract says always bump)
    // v5: identifier_expr carries global_qualified (the ::name escape hatch)
    inline constexpr uint32_t k_jaibite_version = 44;   // 44: a conditional whose arms name storage reads its arms as references where a reference binds it (an argument, a T& declaration, default or return, a cast<T&> operand), so a sibling parsed before it re-parses; 43: a by-value structured range-for binding declares its value by value (the entry's copy), not as a reference to a hidden pair's half; 42: a conditional of lvalues as the container of a store target reads its arms as references, and the checker reports override markers and same-run redefinitions, types a class operand's operator by the overload that takes the right operand and a field over a same-named global, so a sibling parsed or checked clean before them re-parses; 41: const declarations and parameters (internal/const.md): variable_decl::is_const and destructuring_decl::is_const, and a const range-for or parameter is no longer ignored; 40: `function` parses as the callable-or-null slot type (type_info::make_callable), so a sibling that kept it untyped re-parses; 39: consistent namespaces, byte-character results, typed transforms and builtin ordering invalidate prior hints/checks; 38: known builtin method arities invalidate prior checked-clean bites; reserved preview 37 is not reused; 36: hot_reload_migrate at a unit's top level is the unit's reload hook, off the program, so a sibling that defined it as a function re-parses; 35: the JavaScript/C# additions: ?? and ??= read as tokens of their own, => arrow functions, do-while, let, structured range-for bindings and optional chains that skip their rest desugar at parse time, so a sibling parsed before them re-parses; 34: a host template name is no longer a token of its own (token_type::user_template_type removed, so every later token value moved) and its arguments spell canonically (Box<array<int>>, double as float); 33: an anonymous function(...) expression parses as a [&] lambda, so a sibling that recorded it capture-free re-parses; 32: exhaustive capture lists (a parse error for an unlisted enclosing local, a named capture's own scope entry) and the strict required-return and reference-admission checks, so a sibling parsed or checked clean before them re-parses; 31: the header carries the identity of the source the bite was parsed from, so a sibling describes exactly one source text; 30: C++ lvalue targets (a conditional of lvalues, `(a = b) = c`, `++x = v`) read their pieces as references (cast_expr::implicit_ref, assignment_expr::yields_reference); 29: the checker holds a shared_ptr<T> namespace variable store to handle admission, so a bite checked clean before it re-checks; 28: variable_decl::is_inline (`inline T x;`); 27: math::vec2/vec3/vec4/ivec2/ivec3/color read as types, so a sibling that parsed a math declaration as an expression re-parses; 26: import(...) is an expression (import_expr: paths, a group, jai::force); 24: array<uint8> / array<int8> read uint8 / int8 as one-byte element types, so a sibling that parsed them as classes re-parses; 23: a lambda capture carries its init-capture expression; 22: variable_decl::default_initialized (a program-scope declaration without an initializer keeps its value on a same-unit re-run); 21: a typed declaration without an initializer default-initializes, so a sibling parsed before it re-parses; 20: a template string joins no empty parts; 19: utf8_string reads as a type, so a sibling that parsed it as a class re-parses; 18: a literal include inside a function body splices at parse time, so a sibling written before it re-parses; 17: a negated numeric literal is a literal; 16: parallel_for frame captures; 15: variable_decl::function_spelling; 14: own-file location marker; 13: parallel_for zip (extra loop variables + arrays); 12: try_stmt catch var symbol + slot (11: statement::check_mode_attr, 10: extern_decl, 9: cast_expr)
    // Header flags (u32; readers ignore unknown bits, so no version bump needed):
    // bit 0 = the bite was static-checked clean under the SAVING engine's surface —
    // trusted on load only when the registration fingerprint also matches.
    inline constexpr uint32_t k_jaibite_flag_checked_clean = 0x1;
    // BUMP THIS whenever the static checker's rules change (a new diagnostic, a rule that rejects more): a checked-clean
    // stamp records the revision that checked it (flag bits 8..31), and a bite stamped under another revision is checked
    // again on load instead of trusted. A parse change bumps k_jaibite_version instead, which re-parses as well.
    //   1: Base class 'X' not found, an override in a class with no base, a namespace reopening's redefinition
    inline constexpr uint32_t k_jaibite_checker_revision = 1;
    inline constexpr uint32_t jaibite_checked_clean_flags() { return k_jaibite_flag_checked_clean | (k_jaibite_checker_revision << 8); }
    // The stamp is trusted only from this checker revision
    inline constexpr bool jaibite_stamped_clean(uint32_t flags) {
        return (flags & k_jaibite_flag_checked_clean) && (flags >> 8) == k_jaibite_checker_revision;
    }
    // The source a bite was parsed from: its byte count and a 64-bit hash of its bytes. A disk sibling is valid only for
    // the source whose identity equals the one it records (never by an mtime ordering); a bite saved without a source
    // records the empty identity, which no source text has.
    struct jaibite_source_identity {
        uint64_t size = 0;
        uint64_t hash = 0;
        bool operator==(const jaibite_source_identity&) const = default;
    };
    // Each step is a bijection of the running state for a fixed word and of the word for a fixed state, so two texts of
    // one length that differ in a single word never collide.
    inline jaibite_source_identity jaibite_identity_of(std::string_view source) {
        const auto* p = reinterpret_cast<const unsigned char*>(source.data());
        const size_t n = source.size();
        uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
        const auto step = [&h](uint64_t w) { h = (h ^ w) * 0xBF58476D1CE4E5B9ull; h ^= h >> 31; };
        size_t i = 0;
        for (; i + 8 <= n; i += 8) { uint64_t w; std::memcpy(&w, p + i, 8); step(w); }
        uint64_t tail = uint64_t(n - i) << 56;
        for (size_t k = 0; i + k < n; ++k) { tail |= uint64_t(p[i + k]) << (8 * k); }
        step(tail);
        h ^= h >> 30; h *= 0x94D049BB133111EBull; h ^= h >> 31;
        return { uint64_t(n) + 1, h };
    }
    inline constexpr size_t k_jaibite_header_size = 36;   // magic, version, flags, fingerprint, source size, source hash
    // The header of a current-version bite, read without touching the body; false = not one
    inline bool read_jaibite_header(const uint8_t* data, size_t size, uint64_t& fingerprint, jaibite_source_identity& source) {
        if (size < k_jaibite_header_size || std::memcmp(data, k_jaibite_magic, 4) != 0) return false;
        const auto u64 = [data](size_t at) { uint64_t x = 0; for (int i = 0; i < 8; ++i) x |= uint64_t(data[at + i]) << (i * 8); return x; };
        const uint32_t version = uint32_t(data[4]) | uint32_t(data[5]) << 8 | uint32_t(data[6]) << 16 | uint32_t(data[7]) << 24;
        if (version != k_jaibite_version) return false;
        fingerprint = u64(12);
        source = { u64(20), u64(28) };
        return true;
    }
    // Locations naming the unit's own file serialize as this marker and read back as the
    // path the jaibite is loaded under, so a moved folder still names its files
    // (stack traces, breakpoints, engine::current_source_file).
    inline constexpr std::string_view k_jaibite_own_file = "";
    inline constexpr int k_jaibite_max_depth = 1000;

    [[noreturn]] inline void jaibite_fail(const std::string& why) {
        throw jai::runtime_error("jaibite: " + why);
    }

    // ---------------------------------------------------------------- writer

    class ast_writer {
    public:
        // include_locations = false produces a POSITION-INSENSITIVE encoding (structural
        // identity keys, e.g. hot-reload redefinition equality) — never deserialized.
        explicit ast_writer(const string_symbolizer& symbols, bool include_locations = true, std::string own_path = {})
            : symbols_(symbols), include_locations_(include_locations), own_path_(std::move(own_path)) {}

        // Structural key for one subtree: string table + node encoding, a deterministic
        // pure function of AST structure (symbol ids persist as first-seen table indices,
        // so engine-local id values never leak in). Byte equality = structural equality.
        std::vector<uint8_t> node_key(const ast_node* n) {
            node(n);
            std::vector<uint8_t> out;
            out.reserve(8 + body_.size() + table_.size() * 8);
            append_varint(out, table_.size());
            for (const auto& s : table_) {
                append_varint(out, s.size());
                out.insert(out.end(), s.begin(), s.end());
            }
            out.insert(out.end(), body_.begin(), body_.end());
            return out;
        }

        std::vector<uint8_t> serialize(const std::vector<declaration_ptr>& decls, uint64_t fingerprint,
                                       uint32_t flags = 0, jaibite_source_identity source = {}) {
            varint(decls.size());
            for (const auto& d : decls) node(d.get());

            std::vector<uint8_t> out;
            out.reserve(24 + body_.size() + table_.size() * 8);
            out.insert(out.end(), k_jaibite_magic, k_jaibite_magic + 4);
            fixed_u32(out, k_jaibite_version);
            fixed_u32(out, flags);
            fixed_u64(out, fingerprint);
            fixed_u64(out, source.size);
            fixed_u64(out, source.hash);
            append_varint(out, table_.size());
            for (const auto& s : table_) {
                append_varint(out, s.size());
                out.insert(out.end(), s.begin(), s.end());
            }
            out.insert(out.end(), body_.begin(), body_.end());
            return out;
        }

    private:
        // --- primitives ---
        static void fixed_u32(std::vector<uint8_t>& v, uint32_t x) {
            for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (i * 8)));
        }
        static void fixed_u64(std::vector<uint8_t>& v, uint64_t x) {
            for (int i = 0; i < 8; ++i) v.push_back(uint8_t(x >> (i * 8)));
        }
        static void append_varint(std::vector<uint8_t>& v, uint64_t x) {
            while (x >= 0x80) { v.push_back(uint8_t(x) | 0x80); x >>= 7; }
            v.push_back(uint8_t(x));
        }

        void u8(uint8_t x) { body_.push_back(x); }
        void varint(uint64_t x) { append_varint(body_, x); }
        void zigzag(int64_t x) { varint((uint64_t(x) << 1) ^ uint64_t(x >> 63)); }
        void f64(double x) {
            uint64_t bits; std::memcpy(&bits, &x, 8);
            for (int i = 0; i < 8; ++i) body_.push_back(uint8_t(bits >> (i * 8)));
        }
        void flag(bool b) { u8(b ? 1 : 0); }

        uint64_t table_index(std::string_view s) {
            auto it = table_lookup_.find(std::string(s));
            if (it != table_lookup_.end()) return it->second;
            uint64_t idx = table_.size();
            table_.emplace_back(s);
            table_lookup_.emplace(table_.back(), idx);
            return idx;
        }
        void str(std::string_view s) { varint(table_index(s)); }

        // Symbol IDs are engine-local: persist the NAME (table index), never the raw id.
        void symbol(uint64_t id, uint64_t sentinel) {
            if (id == sentinel) { varint(0); return; }
            std::string_view name = symbols_.get_string(id);
            if (name.empty()) jaibite_fail("cannot serialize unresolvable symbol id");
            varint(1 + table_index(name));
        }
        void slot(size_t s) { varint(s == SIZE_MAX ? 0 : uint64_t(s) + 1); }

        void location(const source_location& loc) {
            if (!include_locations_) {
                return;
            }
            str(!own_path_.empty() && loc.filename == own_path_ ? k_jaibite_own_file : std::string_view(loc.filename));
            varint(loc.line);
            varint(loc.column);
        }

        void type(const type_info_ptr& t) {
            if (!t) { u8(0); return; }
            u8(1);
            u8(uint8_t(t->base_type));
            str(t->type_name);
            varint(t->native_size);
            flag(t->is_signed);
            varint(t->type_params.size());
            ++depth_;
            if (depth_ > k_jaibite_max_depth || native_stack_low()) jaibite_fail("type nesting too deep");
            for (const auto& p : t->type_params) type(p);
            --depth_;
        }

        // AST literals are engine-less placeholder values (ast_literal_tag) — scalar payloads only.
        void literal(const script_value& v) {
            if (v.is_null()) { u8(0); }
            else if (v.is_int()) { u8(1); zigzag(v.unchecked_as_int()); }
            else if (v.is_float()) { u8(2); f64(v.unchecked_as_float()); }
            else if (v.is_string()) { u8(3); str(v.unchecked_as_string()); }
            else if (v.is_char()) { u8(4); u8(uint8_t(v.unchecked_as_char())); }
            else if (v.is_bool()) { u8(5); flag(v.unchecked_as_bool()); }
            else jaibite_fail("unsupported literal value type in AST");
        }

        void tok(const token& t) {
            u8(uint8_t(t.type));
            str(t.lexeme);
            symbol(t.symbol_id, 0);  // token uses 0 = not interned
            location(t.location);
            switch (t.type) {
                case token_type::integer_literal: zigzag(t.int_value); break;
                case token_type::float_literal: f64(t.float_value); break;
                case token_type::boolean_literal: flag(t.bool_value); break;
                case token_type::char_literal: u8(uint8_t(t.char_value)); break;
                case token_type::string_literal: str(t.string_value); break;
                default: break;
            }
        }

        void parameters(const std::vector<parameter>& params) {
            varint(params.size());
            for (const auto& p : params) {
                type(p.type);
                str(p.name);
                flag(p.is_reference);
                flag(p.is_const);
                symbol(p.symbol_id, UINT64_MAX);
                slot(p.slot_index);
                node(p.default_value.get());
            }
        }

        void outer_slot_plan(const std::vector<std::pair<uint64_t, size_t>>& plan, bool built) {
            flag(built);
            varint(plan.size());
            for (const auto& [sym, s] : plan) { symbol(sym, UINT64_MAX); slot(s); }
        }

        // --- node dispatch (exhaustive over node_type; unknown = loud failure) ---
        void node(const ast_node* n) {
            if (!n) { u8(0xFF); return; }
            ++depth_;
            if (depth_ > k_jaibite_max_depth || native_stack_low()) jaibite_fail("AST nesting too deep");
            u8(uint8_t(n->get_type()));
            location(n->location);
            if (n->get_type() >= node_type::expression_stmt) u8(static_cast<const statement*>(n)->check_mode_attr);
            switch (n->get_type()) {
                case node_type::literal_expr: {
                    auto* e = static_cast<const literal_expr*>(n);
                    type(e->result_type);
                    literal(e->value);
                    break;
                }
                case node_type::identifier_expr: {
                    auto* e = static_cast<const identifier_expr*>(n);
                    type(e->result_type);
                    str(e->name);
                    symbol(e->symbol_id, UINT64_MAX);
                    slot(e->slot_index);
                    flag(e->global_qualified);   // ::name is pure syntax - not re-derivable
                    break;
                }
                case node_type::binary_expr: {
                    auto* e = static_cast<const binary_expr*>(n);
                    type(e->result_type);
                    node(e->left.get());
                    tok(e->op);
                    node(e->right.get());
                    break;
                }
                case node_type::unary_expr: {
                    auto* e = static_cast<const unary_expr*>(n);
                    type(e->result_type);
                    tok(e->op);
                    node(e->operand.get());
                    flag(e->is_postfix);
                    break;
                }
                case node_type::assignment_expr: {
                    auto* e = static_cast<const assignment_expr*>(n);
                    type(e->result_type);
                    node(e->target.get());
                    tok(e->op);
                    node(e->value.get());
                    u8(static_cast<uint8_t>((e->postfix_old_result ? 1 : 0) | (e->incdec_desugar ? 2 : 0) | (e->yields_reference ? 4 : 0)));
                    break;
                }
                case node_type::call_expr: {
                    auto* e = static_cast<const call_expr*>(n);
                    type(e->result_type);
                    node(e->callee.get());
                    varint(e->arguments.size());
                    for (const auto& a : e->arguments) node(a.get());
                    break;
                }
                case node_type::member_expr: {
                    auto* e = static_cast<const member_expr*>(n);
                    type(e->result_type);
                    node(e->object.get());
                    str(e->member);
                    symbol(e->member_id, UINT64_MAX);
                    symbol(e->getter_id, UINT64_MAX);
                    flag(e->is_arrow);
                    flag(e->is_static);
                    flag(e->null_safe);
                    break;
                }
                case node_type::lambda_expr: {
                    auto* e = static_cast<const lambda_expr*>(n);
                    type(e->result_type);
                    u8(uint8_t(e->default_capture));
                    varint(e->captures.size());
                    for (const auto& c : e->captures) {
                        str(c.name);
                        flag(c.by_reference);
                        symbol(c.symbol_id, UINT64_MAX);
                        slot(c.outer_slot);
                        node(c.initializer.get());
                    }
                    parameters(e->parameters);
                    type(e->return_type);
                    node(e->body.get());
                    varint(e->local_count);
                    outer_slot_plan(e->outer_slot_plan, e->outer_slot_plan_built);
                    varint(e->chained_outer_slots.size());
                    for (const auto& [sym, s] : e->chained_outer_slots) { symbol(sym, UINT64_MAX); slot(s); }
                    varint(e->chained_outer_names.size());
                    for (uint64_t sym : e->chained_outer_names) symbol(sym, UINT64_MAX);
                    break;
                }
                case node_type::new_expr: {
                    auto* e = static_cast<const new_expr*>(n);
                    type(e->result_type);
                    type(e->type);
                    varint(e->arguments.size());
                    for (const auto& a : e->arguments) node(a.get());
                    break;
                }
                case node_type::ternary_expr: {
                    auto* e = static_cast<const ternary_expr*>(n);
                    type(e->result_type);
                    node(e->condition.get());
                    node(e->then_expression.get());
                    node(e->else_expression.get());
                    break;
                }
                case node_type::array_literal_expr: {
                    auto* e = static_cast<const array_literal_expr*>(n);
                    type(e->result_type);
                    varint(e->elements.size());
                    for (const auto& el : e->elements) node(el.get());
                    break;
                }
                case node_type::map_literal_expr: {
                    auto* e = static_cast<const map_literal_expr*>(n);
                    type(e->result_type);
                    varint(e->entries.size());
                    for (const auto& [k, v] : e->entries) { node(k.get()); node(v.get()); }
                    break;
                }
                case node_type::this_expr:
                case node_type::super_expr: {
                    type(static_cast<const expression*>(n)->result_type);
                    break;
                }
                case node_type::throw_expr: {
                    auto* e = static_cast<const throw_expr*>(n);
                    type(e->result_type);
                    node(e->value.get());
                    break;
                }
                case node_type::yield_expr: {
                    auto* e = static_cast<const yield_expr*>(n);
                    type(e->result_type);
                    node(e->value.get());
                    break;
                }
                case node_type::cast_expr: {
                    auto* e = static_cast<const cast_expr*>(n);
                    type(e->result_type);
                    type(e->target);
                    u8(static_cast<uint8_t>((e->by_reference ? 1 : 0) | (e->implicit_ref ? 2 : 0) | (e->binding_ref ? 4 : 0)));
                    node(e->operand.get());
                    break;
                }
                case node_type::expression_stmt: {
                    auto* s = static_cast<const expression_stmt*>(n);
                    node(s->expression.get());
                    break;
                }
                case node_type::block_stmt: {
                    auto* s = static_cast<const block_stmt*>(n);
                    varint(s->declarations.size());
                    for (const auto& d : s->declarations) node(d.get());
                    break;
                }
                case node_type::if_stmt: {
                    auto* s = static_cast<const if_stmt*>(n);
                    node(s->condition.get());
                    node(s->then_statement.get());
                    node(s->else_statement.get());
                    break;
                }
                case node_type::while_stmt: {
                    auto* s = static_cast<const while_stmt*>(n);
                    node(s->condition.get());
                    node(s->body.get());
                    break;
                }
                case node_type::for_stmt: {
                    auto* s = static_cast<const for_stmt*>(n);
                    node(s->initializer.get());
                    node(s->condition.get());
                    node(s->update.get());
                    node(s->body.get());
                    break;
                }
                case node_type::range_for_stmt: {
                    auto* s = static_cast<const range_for_stmt*>(n);
                    type(s->auto_spelled ? type_info_ptr{} : s->element_type);   // auto stays auto: the checker re-types it at load
                    str(s->variable_name);
                    symbol(s->variable_name_id, UINT64_MAX);
                    slot(s->variable_slot_index);
                    flag(s->is_reference);
                    flag(s->is_const);
                    node(s->container.get());
                    node(s->body.get());
                    break;
                }
                case node_type::parallel_for_stmt: {
                    auto* s = static_cast<const parallel_for_stmt*>(n);
                    std::vector<parameter> all{ s->loop_param };
                    all.insert(all.end(), s->extra_params.begin(), s->extra_params.end());
                    parameters(all);
                    varint(s->local_count);
                    varint(s->frame_captures.size());
                    for (const auto& [sym, slot] : s->frame_captures) { symbol(sym, UINT64_MAX); varint(slot); }
                    node(s->container.get());
                    varint(s->extra_containers.size());
                    for (const auto& c : s->extra_containers) node(c.get());
                    node(s->body.get());
                    break;
                }
                case node_type::return_stmt: {
                    auto* s = static_cast<const return_stmt*>(n);
                    node(s->value.get());
                    break;
                }
                case node_type::break_stmt:
                case node_type::continue_stmt:
                case node_type::fallthrough_stmt:
                    break;
                case node_type::try_stmt: {
                    auto* s = static_cast<const try_stmt*>(n);
                    node(s->try_block.get());
                    str(s->catch_var);
                    symbol(s->catch_var_id, UINT64_MAX);
                    slot(s->catch_slot_index);
                    node(s->catch_block.get());
                    break;
                }
                case node_type::switch_stmt: {
                    auto* s = static_cast<const switch_stmt*>(n);
                    node(s->condition.get());
                    varint(s->cases.size());
                    for (const auto& c : s->cases) node(c.get());
                    node(s->default_case.get());
                    break;
                }
                case node_type::case_stmt: {
                    auto* s = static_cast<const case_stmt*>(n);
                    node(s->value.get());
                    varint(s->body.size());
                    for (const auto& b : s->body) node(b.get());
                    flag(s->has_fallthrough);
                    break;
                }
                case node_type::default_stmt: {
                    auto* s = static_cast<const default_stmt*>(n);
                    varint(s->body.size());
                    for (const auto& b : s->body) node(b.get());
                    break;
                }
                case node_type::variable_decl: {
                    auto* d = static_cast<const variable_decl*>(n);
                    type(d->auto_spelled ? type_info_ptr{} : d->type);   // auto stays auto: the checker re-types it at load
                    str(d->name);
                    symbol(d->name_id, UINT64_MAX);
                    node(d->initializer.get());
                    flag(d->is_static);
                    flag(d->function_spelling);
                    flag(d->default_initialized);
                    flag(d->is_inline);
                    flag(d->is_const);
                    slot(d->slot_index);
                    break;
                }
                case node_type::function_decl: {
                    auto* d = static_cast<const function_decl*>(n);
                    str(d->name);
                    symbol(d->name_id, UINT64_MAX);
                    parameters(d->parameters);
                    type(d->return_type);
                    node(d->body.get());
                    varint(d->initializers.size());
                    for (const auto& init : d->initializers) {
                        str(init.target);
                        varint(init.arguments.size());
                        for (const auto& a : init.arguments) node(a.get());
                    }
                    flag(d->is_override);
                    flag(d->is_static);
                    flag(d->is_coroutine);
                    varint(d->local_count);
                    outer_slot_plan(d->outer_slot_plan, d->outer_slot_plan_built);
                    break;
                }
                case node_type::class_decl: {
                    auto* d = static_cast<const class_decl*>(n);
                    str(d->name);
                    symbol(d->name_id, UINT64_MAX);
                    u8(d->nocopy ? 1 : 0);
                    varint(d->base_classes.size());
                    for (const auto& b : d->base_classes) str(b);
                    varint(d->members.size());
                    for (const auto& m : d->members) {
                        u8(uint8_t(m.visibility));
                        node(m.declaration.get());
                    }
                    break;
                }
                case node_type::namespace_decl: {
                    auto* d = static_cast<const namespace_decl*>(n);
                    str(d->name);
                    symbol(d->name_id, UINT64_MAX);
                    varint(d->declarations.size());
                    for (const auto& dd : d->declarations) node(dd.get());
                    break;
                }
                case node_type::expression_decl: {
                    auto* d = static_cast<const expression_decl*>(n);
                    node(d->expression.get());
                    flag(d->implicit_return);
                    break;
                }
                case node_type::statement_decl: {
                    auto* d = static_cast<const statement_decl*>(n);
                    node(d->statement.get());
                    break;
                }
                case node_type::include_decl: {
                    auto* d = static_cast<const include_decl*>(n);
                    str(d->path);
                    node(d->path_expr.get());
                    break;
                }
                case node_type::include_expr: {
                    auto* e = static_cast<const include_expr*>(n);
                    str(e->path);
                    node(e->path_expr.get());
                    break;
                }
                case node_type::import_decl: {
                    auto* d = static_cast<const import_decl*>(n);
                    str(d->path);
                    node(d->path_expr.get());
                    break;
                }
                case node_type::import_expr: {
                    auto* e = static_cast<const import_expr*>(n);
                    varint(e->paths.size());
                    for (const auto& p : e->paths) str(p);
                    varint(e->path_exprs.size());
                    for (const auto& a : e->path_exprs) node(a.get());
                    flag(e->force);
                    break;
                }
                case node_type::extern_decl: {
                    auto* d = static_cast<const extern_decl*>(n);
                    str(d->name);
                    symbol(d->name_id, UINT64_MAX);
                    flag(d->is_function);
                    flag(d->is_const);
                    type(d->type);
                    break;
                }
                case node_type::enum_decl: {
                    auto* d = static_cast<const enum_decl*>(n);
                    str(d->name);
                    symbol(d->name_id, UINT64_MAX);
                    varint(d->values.size());
                    for (const auto& [vname, vid] : d->values) {
                        str(vname);
                        symbol(vid, UINT64_MAX);
                    }
                    break;
                }
                case node_type::destructuring_decl: {
                    auto* d = static_cast<const destructuring_decl*>(n);
                    varint(d->names.size());
                    for (const auto& [nm, id] : d->names) { str(nm); symbol(id, UINT64_MAX); }
                    varint(d->slot_indices.size());
                    for (size_t s : d->slot_indices) slot(s);
                    flag(d->is_const);
                    node(d->initializer.get());
                    break;
                }
                default:
                    jaibite_fail("unserializable AST node type " + std::to_string(int(n->get_type())));
            }
            --depth_;
        }

        const string_symbolizer& symbols_;
        bool include_locations_ = true;
        std::string own_path_;
        std::vector<uint8_t> body_;
        std::vector<std::string> table_;
        std::unordered_map<std::string, uint64_t> table_lookup_;
        int depth_ = 0;
    };

    // ---------------------------------------------------------------- reader

    // Only the trusted in-memory parallel body copier supplies this context. A
    // public saved program always starts without locals or an enclosing frame.
    struct ast_frame_context {
        size_t local_count;
        const std::vector<parameter>& parameters;
    };

    class ast_reader {
    public:
        ast_reader(const uint8_t* data, size_t size, engine* eng, const ast_frame_context* context = nullptr, std::string own_path = {})
            : data_(data), size_(size), eng_(eng), symbols_(eng->get_symbolizer()), own_path_(std::move(own_path)) {
            if (context) {
                // The copied body's own frame is known; its captured parent is
                // not part of this trusted subtree. Never used for file loading.
                frames_[0].local_count = SIZE_MAX;
                enter_frame(context->parameters);
                frames_[current_frame_].local_count = context->local_count;
            }
        }

        std::vector<declaration_ptr> deserialize(uint64_t& fingerprint_out, uint32_t* flags_out = nullptr) {
            if (size_ < 20) jaibite_fail("truncated header");
            if (std::memcmp(data_, k_jaibite_magic, 4) != 0) jaibite_fail("not a jaibite file (bad magic)");
            pos_ = 4;
            uint32_t version = fixed_u32();
            if (version != k_jaibite_version)
                jaibite_fail("unsupported format version " + std::to_string(version) +
                             " (expected " + std::to_string(k_jaibite_version) + ")");
            uint32_t flags = fixed_u32();
            if (flags_out) *flags_out = flags;
            fingerprint_out = fixed_u64();
            if (size_ < k_jaibite_header_size) jaibite_fail("truncated header");
            pos_ += 16;   // the source identity, which read_jaibite_header reads

            uint64_t table_count = varint();
            check_count(table_count);
            table_.reserve(size_t(table_count));
            for (uint64_t i = 0; i < table_count; ++i) {
                uint64_t len = varint();
                if (len > remaining()) jaibite_fail("truncated string table");
                table_.emplace_back(reinterpret_cast<const char*>(data_ + pos_), size_t(len));
                pos_ += size_t(len);
            }
            interned_.assign(table_.size(), { UINT64_MAX, std::string_view{} });

            uint64_t decl_count = varint();
            check_count(decl_count);
            std::vector<declaration_ptr> decls;
            decls.reserve(size_t(decl_count));
            for (uint64_t i = 0; i < decl_count; ++i) decls.push_back(decl_req());
            if (remaining() != 0) jaibite_fail("trailing bytes after declarations");
            const auto valid = validate_program();
            if (!valid) { jaibite_fail(std::string(valid.message())); }
            return decls;
        }

    private:
        struct frame_record {
            size_t parent = 0;
            size_t local_count = 0;
            std::unordered_set<uint64_t> declared;
            bool allows_parent_slots = true;
        };
        struct slot_record {
            size_t index;
            size_t frame;
            uint64_t symbol;
            bool identifier;
        };

        void track_slot(size_t index, uint64_t symbol, bool identifier = false, bool declares = false) {
            if (index == SIZE_MAX) { return; }
            slots_.push_back({index, current_frame_, symbol, identifier});
            if (declares) { frames_[current_frame_].declared.insert(symbol); }
        }

        void enter_frame(const std::vector<parameter>& params, bool allows_parent_slots = true) {
            const size_t parent = current_frame_;
            current_frame_ = frames_.size();
            frames_.push_back({parent, 0, {}, allows_parent_slots});
            for (const auto& p : params) { track_slot(p.slot_index, p.symbol_id, false, true); }
        }

        void leave_frame(size_t local_count) {
            frames_[current_frame_].local_count = local_count;
            current_frame_ = frames_[current_frame_].parent;
        }

        // The decoder's existing complete traversal records fields with their
        // lexical frame. Counts occur after bodies on the wire, so validate the
        // decoded records together before any marking/checking/compilation pass.
        checked_result<void> validate_program() const {
            for (size_t i = 1; i < frames_.size(); ++i) {
                // Each parser-allocated slot requires encoded syntax. This loose
                // byte bound permits synthetic slots but rejects enormous claims
                // before allocation or the VM's narrower operand conversion.
                if (frames_[i].local_count > size_ || frames_[i].local_count >= UINT32_MAX) {
                    return checked_result<void>(make_error_code(runtime_error_code::invalid_operation),
                        "invalid program: local count exceeds encoded storage");
                }
            }
            for (const auto& slot : slots_) {
                size_t frame = slot.frame;
                if (slot.identifier && frames_[frame].declared.find(slot.symbol) == frames_[frame].declared.end()) {
                    // A parallel body crosses the parser's slot lookup boundary:
                    // foreign names use environments, never the caller's slots.
                    if (!frames_[frame].allows_parent_slots) {
                        return checked_result<void>(make_error_code(runtime_error_code::invalid_operation),
                            "invalid program: local slot out of range");
                    }
                    // Match the existing callable/capture classifier: an identifier
                    // not declared in this frame may carry its immediate parent's
                    // slot. Saved SIZE_MAX runtime patches were skipped above.
                    frame = frames_[frame].parent;
                }
                if (slot.index >= frames_[frame].local_count) {
                    return checked_result<void>(make_error_code(runtime_error_code::invalid_operation),
                        "invalid program: local slot out of range");
                }
            }
            return {};
        }

        // --- primitives ---
        size_t remaining() const { return size_ - pos_; }
        void need(size_t n) { if (remaining() < n) jaibite_fail("unexpected end of data"); }
        void check_count(uint64_t n) { if (n > remaining()) jaibite_fail("corrupt count"); }

        uint8_t u8() { need(1); return data_[pos_++]; }
        uint32_t fixed_u32() {
            need(4);
            uint32_t x = 0;
            for (int i = 0; i < 4; ++i) x |= uint32_t(data_[pos_++]) << (i * 8);
            return x;
        }
        uint64_t fixed_u64() {
            need(8);
            uint64_t x = 0;
            for (int i = 0; i < 8; ++i) x |= uint64_t(data_[pos_++]) << (i * 8);
            return x;
        }
        uint64_t varint() {
            uint64_t x = 0;
            for (int shift = 0; shift < 64; shift += 7) {
                uint8_t b = u8();
                if (shift == 63 && (b & 0xFE)) jaibite_fail("corrupt varint");
                x |= uint64_t(b & 0x7F) << shift;
                if (!(b & 0x80)) return x;
            }
            jaibite_fail("corrupt varint");
        }
        int64_t zigzag() {
            uint64_t x = varint();
            return int64_t(x >> 1) ^ -int64_t(x & 1);
        }
        double f64() {
            uint64_t bits = fixed_u64();
            double d; std::memcpy(&d, &bits, 8);
            return d;
        }
        bool flag() { return u8() != 0; }

        const std::string& str() {
            uint64_t idx = varint();
            if (idx >= table_.size()) jaibite_fail("corrupt string index");
            return table_[size_t(idx)];
        }
        // Interned view into the TARGET engine's symbolizer (AST string_views must be permanent).
        std::pair<uint64_t, std::string_view> interned(uint64_t idx) {
            if (idx >= table_.size()) jaibite_fail("corrupt string index");
            auto& slot = interned_[size_t(idx)];
            if (slot.second.data() == nullptr)
                slot = symbols_->intern_with_view(table_[size_t(idx)]);
            return slot;
        }
        std::string_view view() { return interned(varint()).second; }
        uint64_t symbol(uint64_t sentinel) {
            uint64_t v = varint();
            if (v == 0) return sentinel;
            return interned(v - 1).first;
        }
        size_t slot_val() {
            uint64_t v = varint();
            return v == 0 ? SIZE_MAX : size_t(v - 1);
        }

        source_location location() {
            source_location loc;
            const std::string& name = str();
            loc.filename = name == k_jaibite_own_file ? own_path_ : name;
            loc.line = size_t(varint());
            loc.column = size_t(varint());
            return loc;
        }

        type_info_ptr type() {
            if (!u8()) return type_info_ptr(nullptr);
            uint8_t bt = u8();
            if (bt > uint8_t(script_value_type::jai_utf8_string_type)) jaibite_fail("corrupt type_info");
            ++depth_;
            if (depth_ > k_jaibite_max_depth || native_stack_low()) jaibite_fail("type nesting too deep");
            type_info temp(static_cast<script_value_type>(bt));
            temp.type_name = str();
            temp.native_size = size_t(varint());
            temp.is_signed = flag();
            uint64_t n = varint();
            check_count(n);
            for (uint64_t i = 0; i < n; ++i) temp.type_params.push_back(type());
            --depth_;
            // Check the encoded shape before interning: primitive/container keys
            // deliberately ignore surplus parameters, which would hide corruption.
            bool valid_arity = true;
            switch (temp.base_type) {
                case script_value_type::jai_array_type:
                case script_value_type::jai_reference_type:
                case script_value_type::jai_weak_ptr_type:
                case script_value_type::jai_shared_ptr_type:
                    valid_arity = n == 1;
                    break;
                case script_value_type::jai_map_type:
                    valid_arity = n == 2;
                    break;
                case script_value_type::jai_object_type:
                case script_value_type::jai_function_type:
                    break;  // Host template/signature placeholders may be empty.
                case script_value_type::jai_cpp_bound_type:
                    valid_arity = n <= 1;
                    break;
                default:
                    valid_arity = n == 0;
                    break;
            }
            if (!valid_arity) jaibite_fail("invalid program: type parameter count");
            temp.id = eng_->symbolize(temp.canonical_name());
            return type_info_ptr(eng_->get_type_info(temp));
        }

        script_value literal() {
            switch (u8()) {
                case 0: return script_value(script_value::ast_literal_tag{}, std::monostate{});
                case 1: return script_value(script_value::ast_literal_tag{}, script_int(zigzag()));
                case 2: return script_value(script_value::ast_literal_tag{}, script_float(f64()));
                case 3: return script_value(script_value::ast_literal_tag{}, script_string(str()));
                case 4: return script_value(script_value::ast_literal_tag{}, script_char(u8()));
                case 5: return script_value(script_value::ast_literal_tag{}, script_bool(flag()));
                default: jaibite_fail("corrupt literal value");
            }
        }

        token tok() {
            uint8_t tt = u8();
            if (tt > uint8_t(token_type::right_shift_equal)) jaibite_fail("corrupt token type");
            token t;
            t.type = static_cast<token_type>(tt);
            t.lexeme = view();
            t.symbol_id = symbol(0);
            t.location = location();
            switch (t.type) {
                case token_type::integer_literal: t.int_value = zigzag(); break;
                case token_type::float_literal: t.float_value = f64(); break;
                case token_type::boolean_literal: t.bool_value = flag(); break;
                case token_type::char_literal: t.char_value = script_char(u8()); break;
                case token_type::string_literal: t.string_value = str(); break;
                default: break;
            }
            return t;
        }

        std::vector<parameter> parameters() {
            uint64_t n = varint();
            check_count(n);
            std::vector<parameter> params;
            params.reserve(size_t(n));
            for (uint64_t i = 0; i < n; ++i) {
                type_info_ptr t = type();
                std::string name = str();
                bool is_ref = flag();
                bool is_const = flag();
                parameter p(t, name, is_ref, is_const);
                p.symbol_id = symbol(UINT64_MAX);
                p.slot_index = slot_val();
                p.default_value = expr_opt();
                params.push_back(std::move(p));
            }
            return params;
        }

        void outer_slot_plan(std::vector<std::pair<uint64_t, size_t>>& plan, bool& built) {
            built = flag();
            uint64_t n = varint();
            check_count(n);
            plan.reserve(size_t(n));
            for (uint64_t i = 0; i < n; ++i) {
                uint64_t sym = symbol(UINT64_MAX);
                size_t s = slot_val();
                plan.emplace_back(sym, s);
                track_slot(s, sym);
            }
        }

        // --- typed child readers ---
        expression_ptr expr_opt() {
            auto n = node();
            if (!n) return nullptr;
            if (uint8_t(n->get_type()) >= 20) jaibite_fail("expected expression node");
            return std::static_pointer_cast<expression>(n);
        }
        expression_ptr expr_req() {
            auto e = expr_opt();
            if (!e) jaibite_fail("missing required expression node");
            return e;
        }
        statement_ptr stmt_opt() {
            auto n = node();
            if (!n) return nullptr;
            if (uint8_t(n->get_type()) < 20) jaibite_fail("expected statement node");
            return std::static_pointer_cast<statement>(n);
        }
        statement_ptr stmt_req() {
            auto s = stmt_opt();
            if (!s) jaibite_fail("missing required statement node");
            return s;
        }
        declaration_ptr decl_opt() {
            auto n = node();
            if (!n) return nullptr;
            if (uint8_t(n->get_type()) < 40) jaibite_fail("expected declaration node");
            return std::static_pointer_cast<declaration>(n);
        }
        declaration_ptr decl_req() {
            auto d = decl_opt();
            if (!d) jaibite_fail("missing required declaration node");
            return d;
        }
        std::shared_ptr<block_stmt> block_opt() {
            auto n = node();
            if (!n) return nullptr;
            if (n->get_type() != node_type::block_stmt) jaibite_fail("expected block statement node");
            return std::static_pointer_cast<block_stmt>(n);
        }
        std::shared_ptr<block_stmt> block_req() {
            auto result = block_opt();
            if (!result) jaibite_fail("missing required block statement node");
            return result;
        }

        ast_node_ptr node() {
            uint8_t tag = u8();
            if (tag == 0xFF) return nullptr;
            ++depth_;
            if (depth_ > k_jaibite_max_depth || native_stack_low()) jaibite_fail("AST nesting too deep");
            source_location loc = location();
            const uint8_t check_mode_attr = tag >= uint8_t(node_type::expression_stmt) ? u8() : 0;
            ast_node_ptr result;
            switch (static_cast<node_type>(tag)) {
                case node_type::literal_expr: {
                    auto rt = type();
                    auto e = std::make_shared<literal_expr>(loc, literal());
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::identifier_expr: {
                    auto rt = type();
                    std::string_view name = view();
                    uint64_t sym = symbol(UINT64_MAX);
                    auto e = std::make_shared<identifier_expr>(loc, name, sym);
                    e->result_type = rt;
                    e->slot_index = slot_val();
                    e->global_qualified = flag();
                    track_slot(e->slot_index, sym, true);
                    result = e;
                    break;
                }
                case node_type::binary_expr: {
                    auto rt = type();
                    auto l = expr_req();
                    token op = tok();
                    auto r = expr_req();
                    auto e = std::make_shared<binary_expr>(loc, l, op, r);
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::unary_expr: {
                    auto rt = type();
                    token op = tok();
                    auto operand = expr_req();
                    bool postfix = flag();
                    auto e = std::make_shared<unary_expr>(loc, op, operand, postfix);
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::assignment_expr: {
                    auto rt = type();
                    auto t = expr_req();
                    token op = tok();
                    auto v = expr_req();
                    auto e = std::make_shared<assignment_expr>(loc, t, op, v);
                    e->result_type = rt;
                    { const uint8_t fl = u8(); e->postfix_old_result = (fl & 1) != 0; e->incdec_desugar = (fl & 2) != 0; e->yields_reference = (fl & 4) != 0; }
                    result = e;
                    break;
                }
                case node_type::call_expr: {
                    auto rt = type();
                    auto callee = expr_req();
                    auto e = std::make_shared<call_expr>(loc, callee, expr_list());
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::member_expr: {
                    auto rt = type();
                    auto obj = expr_req();
                    std::string_view member = view();
                    uint64_t member_id = symbol(UINT64_MAX);
                    uint64_t getter_id = symbol(UINT64_MAX);
                    bool arrow = flag();
                    bool is_static = flag();
                    auto e = std::make_shared<member_expr>(loc, obj, member, member_id, arrow, is_static);
                    e->getter_id = getter_id;
                    e->null_safe = flag();
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::lambda_expr: {
                    auto rt = type();
                    auto e = std::make_shared<lambda_expr>(loc);
                    e->result_type = rt;
                    uint8_t cd = u8();
                    if (cd > uint8_t(lambda_expr::capture_default::by_reference)) jaibite_fail("corrupt capture default");
                    e->default_capture = static_cast<lambda_expr::capture_default>(cd);
                    uint64_t nc = varint();
                    check_count(nc);
                    e->captures.reserve(size_t(nc));
                    for (uint64_t i = 0; i < nc; ++i) {
                        std::string_view cname = view();
                        bool by_ref = flag();
                        lambda_expr::capture c(cname, by_ref);
                        c.symbol_id = symbol(UINT64_MAX);
                        c.outer_slot = slot_val();
                        track_slot(c.outer_slot, c.symbol_id);
                        c.initializer = expr_opt();
                        e->captures.push_back(std::move(c));
                    }
                    e->parameters = parameters();
                    e->return_type = type();
                    enter_frame(e->parameters);
                    e->body = block_req();
                    e->local_count = size_t(varint());
                    leave_frame(e->local_count);
                    bool built = false;
                    outer_slot_plan(e->outer_slot_plan, built);
                    e->outer_slot_plan_built = built;
                    uint64_t nchain = varint();
                    check_count(nchain);
                    e->chained_outer_slots.reserve(size_t(nchain));
                    for (uint64_t i = 0; i < nchain; ++i) {
                        uint64_t sym = symbol(UINT64_MAX);
                        size_t s = slot_val();
                        e->chained_outer_slots.emplace_back(sym, s);
                        track_slot(s, sym);
                    }
                    uint64_t nnames = varint();
                    check_count(nnames);
                    e->chained_outer_names.reserve(size_t(nnames));
                    for (uint64_t i = 0; i < nnames; ++i) e->chained_outer_names.push_back(symbol(UINT64_MAX));
                    result = e;
                    break;
                }
                case node_type::new_expr: {
                    auto rt = type();
                    auto t = type();
                    auto e = std::make_shared<new_expr>(loc, t, expr_list());
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::ternary_expr: {
                    auto rt = type();
                    auto c = expr_req();
                    auto t = expr_req();
                    auto f = expr_req();
                    auto e = std::make_shared<ternary_expr>(loc, c, t, f);
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::array_literal_expr: {
                    auto rt = type();
                    auto e = std::make_shared<array_literal_expr>(loc, expr_list());
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::map_literal_expr: {
                    auto rt = type();
                    uint64_t n = varint();
                    check_count(n);
                    std::vector<std::pair<expression_ptr, expression_ptr>> entries;
                    entries.reserve(size_t(n));
                    for (uint64_t i = 0; i < n; ++i) {
                        auto k = expr_req();
                        auto v = expr_req();
                        entries.emplace_back(k, v);
                    }
                    auto e = std::make_shared<map_literal_expr>(loc, std::move(entries));
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::this_expr: {
                    auto rt = type();
                    auto e = std::make_shared<this_expr>(loc);
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::super_expr: {
                    auto rt = type();
                    auto e = std::make_shared<super_expr>(loc);
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::throw_expr: {
                    auto rt = type();
                    auto e = std::make_shared<throw_expr>(loc, expr_opt());
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::yield_expr: {
                    auto rt = type();
                    auto e = std::make_shared<yield_expr>(loc, expr_opt());
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::cast_expr: {
                    auto rt = type();
                    auto target = type();
                    const uint8_t cast_flags = u8();
                    auto e = std::make_shared<cast_expr>(loc, target, (cast_flags & 1) != 0, expr_req());
                    e->implicit_ref = (cast_flags & 2) != 0;
                    e->binding_ref = (cast_flags & 4) != 0;
                    e->result_type = rt;
                    result = e;
                    break;
                }
                case node_type::expression_stmt:
                    result = std::make_shared<expression_stmt>(loc, expr_req());
                    break;
                case node_type::block_stmt: {
                    uint64_t n = varint();
                    check_count(n);
                    std::vector<declaration_ptr> decls;
                    decls.reserve(size_t(n));
                    for (uint64_t i = 0; i < n; ++i) decls.push_back(decl_req());
                    result = std::make_shared<block_stmt>(loc, std::move(decls));
                    break;
                }
                case node_type::if_stmt: {
                    auto c = expr_req();
                    auto t = stmt_req();
                    auto e = stmt_opt();
                    result = std::make_shared<if_stmt>(loc, c, t, e);
                    break;
                }
                case node_type::while_stmt: {
                    auto c = expr_req();
                    auto b = stmt_req();
                    result = std::make_shared<while_stmt>(loc, c, b);
                    break;
                }
                case node_type::for_stmt: {
                    auto i = decl_opt();
                    if (i && i->get_type() != node_type::variable_decl &&
                        i->get_type() != node_type::expression_decl) {
                        jaibite_fail("invalid program: for initializer must be a variable or expression declaration");
                    }
                    auto c = expr_opt();
                    auto u = expr_opt();
                    auto b = stmt_req();
                    result = std::make_shared<for_stmt>(loc, i, c, u, b);
                    break;
                }
                case node_type::range_for_stmt: {
                    auto et = type();
                    std::string_view vname = view();
                    uint64_t vid = symbol(UINT64_MAX);
                    size_t vslot = slot_val();
                    bool is_ref = flag();
                    bool is_const = flag();
                    auto cont = expr_req();
                    auto body = stmt_req();
                    auto s = std::make_shared<range_for_stmt>(loc, et, vname, vid, is_ref, is_const, cont, body);
                    s->variable_slot_index = vslot;
                    track_slot(vslot, vid, false, true);
                    stamp_map_entry_bindings(*s, symbols_);
                    result = s;
                    break;
                }
                case node_type::parallel_for_stmt: {
                    auto params = parameters();
                    if (params.empty()) jaibite_fail("parallel_for_stmt parameter count");
                    size_t locals = size_t(varint());
                    const size_t frame_capture_count = size_t(varint());
                    if (frame_capture_count > 65536) jaibite_fail("parallel_for_stmt frame capture count");
                    std::vector<std::pair<uint64_t, size_t>> frame_captures(frame_capture_count);
                    for (auto& [sym, slot] : frame_captures) { sym = symbol(UINT64_MAX); slot = size_t(varint()); }
                    auto cont = expr_req();
                    auto extras = expr_list();
                    if (extras.size() + 1 != params.size()) jaibite_fail("parallel_for_stmt zip arity");
                    for (size_t i = 0; i < params.size(); ++i) {
                        if (params[i].slot_index != i) {
                            jaibite_fail("invalid program: parallel parameter slot does not match its position");
                        }
                    }
                    enter_frame(params, false);
                    auto body = block_req();
                    leave_frame(locals);
                    auto s = std::make_shared<parallel_for_stmt>(loc, std::move(params[0]), cont, body);
                    s->extra_params.assign(params.begin() + 1, params.end());
                    s->extra_containers = std::move(extras);
                    s->local_count = locals;
                    s->frame_captures = std::move(frame_captures);
                    result = s;
                    break;
                }
                case node_type::return_stmt:
                    result = std::make_shared<return_stmt>(loc, expr_opt());
                    break;
                case node_type::break_stmt:
                    result = std::make_shared<break_stmt>(loc);
                    break;
                case node_type::continue_stmt:
                    result = std::make_shared<continue_stmt>(loc);
                    break;
                case node_type::try_stmt: {
                    auto try_blk = block_req();
                    std::string catch_var = str();
                    uint64_t catch_id = symbol(UINT64_MAX);
                    size_t catch_slot = slot_val();
                    auto catch_blk = block_req();
                    auto s = std::make_shared<try_stmt>(loc, try_blk, catch_blk, catch_var);
                    s->catch_var_id = catch_id;
                    s->catch_slot_index = catch_slot;
                    track_slot(catch_slot, catch_id, false, true);
                    result = s;
                    break;
                }
                case node_type::switch_stmt: {
                    auto s = std::make_shared<switch_stmt>(loc, expr_req());
                    uint64_t n = varint();
                    check_count(n);
                    s->cases.reserve(size_t(n));
                    for (uint64_t i = 0; i < n; ++i) {
                        auto c = node();
                        if (!c || c->get_type() != node_type::case_stmt) jaibite_fail("expected case statement node");
                        s->cases.push_back(std::static_pointer_cast<case_stmt>(c));
                    }
                    auto def = node();
                    if (def) {
                        if (def->get_type() != node_type::default_stmt) jaibite_fail("expected default statement node");
                        s->default_case = std::static_pointer_cast<default_stmt>(def);
                    }
                    result = s;
                    break;
                }
                case node_type::case_stmt: {
                    auto s = std::make_shared<case_stmt>(loc, expr_req());
                    uint64_t n = varint();
                    check_count(n);
                    s->body.reserve(size_t(n));
                    for (uint64_t i = 0; i < n; ++i) s->body.push_back(stmt_req());
                    s->has_fallthrough = flag();
                    bool terminal_fallthrough = false;
                    for (size_t i = 0; i < s->body.size(); ++i) {
                        if (s->body[i]->get_type() == node_type::fallthrough_stmt) {
                            if (i + 1 != s->body.size()) {
                                jaibite_fail("invalid program: inconsistent case fallthrough");
                            }
                            terminal_fallthrough = true;
                        }
                    }
                    if (s->has_fallthrough != terminal_fallthrough) {
                        jaibite_fail("invalid program: inconsistent case fallthrough");
                    }
                    result = s;
                    break;
                }
                case node_type::default_stmt: {
                    auto s = std::make_shared<default_stmt>(loc);
                    uint64_t n = varint();
                    check_count(n);
                    s->body.reserve(size_t(n));
                    for (uint64_t i = 0; i < n; ++i) s->body.push_back(stmt_req());
                    result = s;
                    break;
                }
                case node_type::fallthrough_stmt:
                    result = std::make_shared<fallthrough_stmt>(loc);
                    break;
                case node_type::variable_decl: {
                    auto t = type();
                    std::string_view name = view();
                    uint64_t nid = symbol(UINT64_MAX);
                    auto init = expr_opt();
                    auto d = std::make_shared<variable_decl>(loc, t, name, nid, init);
                    d->is_static = flag();
                    d->function_spelling = flag();
                    d->default_initialized = flag();
                    d->is_inline = flag();
                    d->is_const = flag();
                    d->slot_index = slot_val();
                    track_slot(d->slot_index, nid, false, true);
                    result = d;
                    break;
                }
                case node_type::function_decl: {
                    std::string_view name = view();
                    uint64_t nid = symbol(UINT64_MAX);
                    auto d = std::make_shared<function_decl>(loc, name, nid);
                    d->parameters = parameters();
                    d->return_type = type();
                    enter_frame(d->parameters);
                    d->body = block_req();
                    // Constructor initializers, like parameter defaults, are parsed
                    // in the surrounding scope, not in the function body frame.
                    const size_t body_frame = current_frame_;
                    current_frame_ = frames_[body_frame].parent;
                    uint64_t ni = varint();
                    check_count(ni);
                    d->initializers.reserve(size_t(ni));
                    for (uint64_t i = 0; i < ni; ++i) {
                        std::string target = str();
                        d->initializers.emplace_back(target, expr_list());
                    }
                    d->is_override = flag();
                    d->is_static = flag();
                    d->is_coroutine = flag();
                    d->local_count = size_t(varint());
                    frames_[body_frame].local_count = d->local_count;
                    bool built = false;
                    outer_slot_plan(d->outer_slot_plan, built);
                    d->outer_slot_plan_built = built;
                    result = d;
                    break;
                }
                case node_type::class_decl: {
                    std::string_view name = view();
                    uint64_t nid = symbol(UINT64_MAX);
                    auto d = std::make_shared<class_decl>(loc, name, nid);
                    d->nocopy = u8() != 0;
                    uint64_t nb = varint();
                    check_count(nb);
                    d->base_classes.reserve(size_t(nb));
                    for (uint64_t i = 0; i < nb; ++i) d->base_classes.push_back(view());
                    uint64_t nm = varint();
                    check_count(nm);
                    d->members.reserve(size_t(nm));
                    for (uint64_t i = 0; i < nm; ++i) {
                        uint8_t vis = u8();
                        if (vis > uint8_t(class_decl::Protected)) jaibite_fail("corrupt member visibility");
                        auto member_decl = decl_req();
                        if (member_decl->get_type() != node_type::variable_decl &&
                            member_decl->get_type() != node_type::function_decl) {
                            jaibite_fail("invalid program: class member must be a variable or function declaration");
                        }
                        d->members.push_back({ static_cast<class_decl::member_visibility>(vis), member_decl });
                    }
                    result = d;
                    break;
                }
                case node_type::namespace_decl: {
                    std::string_view name = view();
                    uint64_t nid = symbol(UINT64_MAX);
                    auto d = std::make_shared<namespace_decl>(loc, name, nid);
                    uint64_t n = varint();
                    check_count(n);
                    d->declarations.reserve(size_t(n));
                    for (uint64_t i = 0; i < n; ++i) d->declarations.push_back(decl_req());
                    result = d;
                    break;
                }
                case node_type::expression_decl: {
                    auto e = expr_req();
                    bool implicit = flag();
                    result = std::make_shared<expression_decl>(loc, e, implicit);
                    break;
                }
                case node_type::statement_decl:
                    result = std::make_shared<statement_decl>(loc, stmt_req());
                    break;
                case node_type::include_decl: {
                    std::string path = str();
                    auto d = std::make_shared<include_decl>(loc, path);
                    d->path_expr = expr_opt();
                    result = d;
                    break;
                }
                case node_type::include_expr: {
                    std::string path = str();
                    auto e = std::make_shared<include_expr>(loc, path);
                    e->path_expr = expr_opt();
                    result = e;
                    break;
                }
                case node_type::import_decl: {
                    std::string path = str();
                    auto d = std::make_shared<import_decl>(loc, path);
                    d->path_expr = expr_opt();
                    result = d;
                    break;
                }
                case node_type::import_expr: {
                    auto e = std::make_shared<import_expr>(loc);
                    const uint64_t n_paths = varint();
                    check_count(n_paths);
                    for (uint64_t i = 0; i < n_paths; ++i) { e->paths.push_back(str()); }
                    e->path_exprs = expr_list();
                    e->force = flag();
                    result = e;
                    break;
                }
                case node_type::extern_decl: {
                    std::string_view name = view();
                    uint64_t nid = symbol(UINT64_MAX);
                    bool is_function = flag();
                    bool is_const = flag();
                    type_info_ptr t = type();
                    auto d = std::make_shared<extern_decl>(loc, t, is_function, name, nid);
                    d->is_const = is_const;
                    result = d;
                    break;
                }
                case node_type::enum_decl: {
                    std::string_view name = view();
                    uint64_t nid = symbol(UINT64_MAX);
                    auto d = std::make_shared<enum_decl>(loc, name, nid);
                    uint64_t n = varint();
                    check_count(n);
                    d->values.reserve(size_t(n));
                    for (uint64_t i = 0; i < n; ++i) {
                        std::string_view vname = view();
                        uint64_t vid = symbol(UINT64_MAX);
                        d->values.emplace_back(vname, vid);
                    }
                    result = d;
                    break;
                }
				case node_type::destructuring_decl: {
					std::vector<std::pair<std::string_view, uint64_t>> names;
					uint64_t n = varint();
					check_count(n);
					names.reserve(size_t(n));
					for (uint64_t i = 0; i < n; ++i) {
						std::string_view nm = view();
						uint64_t id = symbol(UINT64_MAX);
						names.emplace_back(nm, id);
					}
					uint64_t ns = varint();
					check_count(ns);
					if (ns != n) {
						jaibite_fail("destructuring name/slot count mismatch");
					}
					std::vector<size_t> slots;
					slots.reserve(size_t(ns));
					for (uint64_t i = 0; i < ns; ++i) slots.push_back(slot_val());
					const bool is_const = flag();
					auto d = std::make_shared<destructuring_decl>(loc, expr_req());
					d->is_const = is_const;
					d->names = std::move(names);
					d->slot_indices = std::move(slots);
					for (size_t i = 0; i < d->slot_indices.size(); ++i) {
						track_slot(d->slot_indices[i], d->names[i].second, false, true);
					}
					result = d;
					break;
				}
                default:
                    jaibite_fail("corrupt node tag " + std::to_string(int(tag)));
            }
            if (check_mode_attr && result) static_cast<statement*>(result.get())->check_mode_attr = check_mode_attr;
            --depth_;
            return result;
        }

        std::vector<expression_ptr> expr_list() {
            uint64_t n = varint();
            check_count(n);
            std::vector<expression_ptr> list;
            list.reserve(size_t(n));
            for (uint64_t i = 0; i < n; ++i) list.push_back(expr_req());
            return list;
        }

        const uint8_t* data_;
        size_t size_;
        size_t pos_ = 0;
        engine* eng_;
        string_symbolizer* symbols_;
        std::vector<std::string> table_;
        std::vector<std::pair<uint64_t, std::string_view>> interned_;  // lazy per-table-entry target ids
        std::vector<frame_record> frames_{frame_record{}};
        std::vector<slot_record> slots_;
        size_t current_frame_ = 0;
        int depth_ = 0;
        std::string own_path_;
    };

    // own_path: the unit's source path as its nodes are stamped; pass the same spelling to
    // deserialize_jaibite (the path the bite is loaded under) and the file may move.
    inline std::vector<uint8_t> serialize_jaibite(const std::vector<declaration_ptr>& decls,
                                                  const string_symbolizer& symbols, uint64_t fingerprint,
                                                  uint32_t flags = 0, std::string own_path = {},
                                                  jaibite_source_identity source = {}) {
        return ast_writer(symbols, /*include_locations=*/true, std::move(own_path)).serialize(decls, fingerprint, flags, source);
    }

    inline std::vector<declaration_ptr> deserialize_jaibite(const uint8_t* data, size_t size,
                                                            engine* eng, uint64_t& fingerprint_out,
                                                            uint32_t* flags_out = nullptr,
                                                            const ast_frame_context* context = nullptr,
                                                            std::string own_path = {}) {
        return ast_reader(data, size, eng, context, std::move(own_path)).deserialize(fingerprint_out, flags_out);
    }

    // Position-insensitive structural identity key for one AST subtree (hot-reload
    // redefinition equality): byte-equal keys = structurally identical code, regardless
    // of line/column shifts or the surrounding source. Throws (jaibite_fail) on
    // unserializable nodes — callers treat that as "not identical".
    inline std::vector<uint8_t> structural_node_key(const ast_node* n, const string_symbolizer& symbols) {
        return ast_writer(symbols, /*include_locations=*/false).node_key(n);
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_AST_SERIALIZER_HPP__
