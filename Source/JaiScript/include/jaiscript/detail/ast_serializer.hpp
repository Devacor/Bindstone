#pragma once

#ifndef __JAISCRIPT_DETAIL_AST_SERIALIZER_HPP__
#define __JAISCRIPT_DETAIL_AST_SERIALIZER_HPP__

// Binary AST serializer for jaibite save/load. Interned symbol IDs are per-engine and
// non-deterministic, so every symbol/name is written as a string-table index and
// re-interned into the LOADING engine's symbolizer; type_info is written structurally
// and re-interned via engine::get_type_info. The compiled VM chunk is never written —
// a loaded bite recompiles lazily through the normal path. Little-endian, x64 only.
//
// Format v1: "JBIT" magic, u32 version, u32 flags, u64 registration fingerprint,
// string table (count + length-prefixed strings), declaration count, node tree.
// Node encoding: u8 node_type tag (0xFF = null pointer), source_location, fields.
// Runtime AST patches (identifier slot_index -> SIZE_MAX, outer_slot_plan, cached
// getter_id) are serialized faithfully: patched state is self-consistent in memory on
// both backends, so a faithful round trip preserves behavior (save-after-execute test).

#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <jaiscript/core/engine.hpp>
#include <cstring>
#include <unordered_map>

namespace jai::detail {

    inline constexpr uint8_t k_jaibite_magic[4] = { 'J', 'B', 'I', 'T' };
    inline constexpr uint32_t k_jaibite_version = 1;
    // Header flags (u32; readers ignore unknown bits, so no version bump needed):
    // bit 0 = the bite was static-checked clean under the SAVING engine's surface —
    // trusted on load only when the registration fingerprint also matches.
    inline constexpr uint32_t k_jaibite_flag_checked_clean = 0x1;
    inline constexpr int k_jaibite_max_depth = 1000;

    [[noreturn]] inline void jaibite_fail(const std::string& why) {
        throw jai::runtime_error("jaibite: " + why);
    }

    // ---------------------------------------------------------------- writer

    class ast_writer {
    public:
        explicit ast_writer(const string_symbolizer& symbols) : symbols_(symbols) {}

        std::vector<uint8_t> serialize(const std::vector<declaration_ptr>& decls, uint64_t fingerprint,
                                       uint32_t flags = 0) {
            varint(decls.size());
            for (const auto& d : decls) node(d.get());

            std::vector<uint8_t> out;
            out.reserve(24 + body_.size() + table_.size() * 8);
            out.insert(out.end(), k_jaibite_magic, k_jaibite_magic + 4);
            fixed_u32(out, k_jaibite_version);
            fixed_u32(out, flags);
            fixed_u64(out, fingerprint);
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
            str(loc.filename);
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
            if (depth_ > k_jaibite_max_depth) jaibite_fail("type nesting too deep");
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
            if (depth_ > k_jaibite_max_depth) jaibite_fail("AST nesting too deep");
            u8(uint8_t(n->get_type()));
            location(n->location);
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
                    }
                    parameters(e->parameters);
                    type(e->return_type);
                    node(e->body.get());
                    varint(e->local_count);
                    outer_slot_plan(e->outer_slot_plan, e->outer_slot_plan_built);
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
                    type(s->element_type);
                    str(s->variable_name);
                    symbol(s->variable_name_id, UINT64_MAX);
                    slot(s->variable_slot_index);
                    flag(s->is_reference);
                    flag(s->is_const);
                    node(s->container.get());
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
                    type(d->type);
                    str(d->name);
                    symbol(d->name_id, UINT64_MAX);
                    node(d->initializer.get());
                    flag(d->is_static);
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
                case node_type::import_decl: {
                    auto* d = static_cast<const import_decl*>(n);
                    str(d->path);
                    node(d->path_expr.get());
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
                    node(d->initializer.get());
                    break;
                }
                default:
                    jaibite_fail("unserializable AST node type " + std::to_string(int(n->get_type())));
            }
            --depth_;
        }

        const string_symbolizer& symbols_;
        std::vector<uint8_t> body_;
        std::vector<std::string> table_;
        std::unordered_map<std::string, uint64_t> table_lookup_;
        int depth_ = 0;
    };

    // ---------------------------------------------------------------- reader

    class ast_reader {
    public:
        ast_reader(const uint8_t* data, size_t size, engine* eng)
            : data_(data), size_(size), eng_(eng), symbols_(eng->get_symbolizer()) {}

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
            return decls;
        }

    private:
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
            loc.filename = str();
            loc.line = size_t(varint());
            loc.column = size_t(varint());
            return loc;
        }

        type_info_ptr type() {
            if (!u8()) return type_info_ptr(nullptr);
            uint8_t bt = u8();
            if (bt > uint8_t(script_value_type::jai_invalid_type)) jaibite_fail("corrupt type_info");
            ++depth_;
            if (depth_ > k_jaibite_max_depth) jaibite_fail("type nesting too deep");
            type_info temp(static_cast<script_value_type>(bt));
            temp.type_name = str();
            temp.native_size = size_t(varint());
            temp.is_signed = flag();
            uint64_t n = varint();
            check_count(n);
            for (uint64_t i = 0; i < n; ++i) temp.type_params.push_back(type());
            --depth_;
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
            if (tt > uint8_t(token_type::error)) jaibite_fail("corrupt token type");
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

        ast_node_ptr node() {
            uint8_t tag = u8();
            if (tag == 0xFF) return nullptr;
            ++depth_;
            if (depth_ > k_jaibite_max_depth) jaibite_fail("AST nesting too deep");
            source_location loc = location();
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
                        e->captures.push_back(c);
                    }
                    e->parameters = parameters();
                    e->return_type = type();
                    e->body = stmt_req();
                    e->local_count = size_t(varint());
                    bool built = false;
                    outer_slot_plan(e->outer_slot_plan, built);
                    e->outer_slot_plan_built = built;
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
                    auto try_blk = stmt_req();
                    std::string catch_var = str();
                    auto catch_blk = stmt_req();
                    result = std::make_shared<try_stmt>(loc, try_blk, catch_blk, catch_var);
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
                    d->slot_index = slot_val();
                    result = d;
                    break;
                }
                case node_type::function_decl: {
                    std::string_view name = view();
                    uint64_t nid = symbol(UINT64_MAX);
                    auto d = std::make_shared<function_decl>(loc, name, nid);
                    d->parameters = parameters();
                    d->return_type = type();
                    d->body = block_opt();
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
                case node_type::import_decl: {
                    std::string path = str();
                    auto d = std::make_shared<import_decl>(loc, path);
                    d->path_expr = expr_opt();
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
                    std::vector<size_t> slots;
                    slots.reserve(size_t(ns));
                    for (uint64_t i = 0; i < ns; ++i) slots.push_back(slot_val());
                    auto d = std::make_shared<destructuring_decl>(loc, expr_opt());
                    d->names = std::move(names);
                    d->slot_indices = std::move(slots);
                    result = d;
                    break;
                }
                default:
                    jaibite_fail("corrupt node tag " + std::to_string(int(tag)));
            }
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
        int depth_ = 0;
    };

    inline std::vector<uint8_t> serialize_jaibite(const std::vector<declaration_ptr>& decls,
                                                  const string_symbolizer& symbols, uint64_t fingerprint,
                                                  uint32_t flags = 0) {
        return ast_writer(symbols).serialize(decls, fingerprint, flags);
    }

    inline std::vector<declaration_ptr> deserialize_jaibite(const uint8_t* data, size_t size,
                                                            engine* eng, uint64_t& fingerprint_out,
                                                            uint32_t* flags_out = nullptr) {
        return ast_reader(data, size, eng).deserialize(fingerprint_out, flags_out);
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_AST_SERIALIZER_HPP__
