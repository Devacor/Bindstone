#pragma once

#ifndef __JAISCRIPT_DETAIL_BUILTIN_METHODS_HPP__
#define __JAISCRIPT_DETAIL_BUILTIN_METHODS_HPP__

#include <jaiscript/core/value.hpp>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <optional>
#include <string_view>

namespace jai {

    class string_symbolizer;
    class class_definition;

    // shared_ptr HANDLE builtin method names that a user class method now SHADOWS and
    // WINS over. same_as is deliberately EXCLUDED: it is a UNIVERSAL
    // identity primitive available on every value (and excluded from the fast method-call
    // gate), not a per-class-overridable handle method. weak_ptr's lock/expired never
    // collide here - they dispatch on weak_ptr VALUES, which are not user class instances.
    inline constexpr std::array<std::string_view, 4> k_shadowable_handle_method_names = {
        std::string_view{"reset"}, std::string_view{"use_count"},
        std::string_view{"unique"}, std::string_view{"cpp_ref_count"}
    };

    // The string builtins that write their receiver in place and return self for chaining
    // (C++ `string& to_upper()`). ONE list feeds the registry's mutator set and index table;
    // both backends bind the receiver LVALUE for exactly these names
    // (detail/string_mutator_receiver.hpp) - a name missing here mutates a copy and the
    // write is lost, a name wrongly here costs an observer a receiver resolve.
    inline constexpr std::array<std::string_view, 18> k_string_mutator_method_names = {
        std::string_view{"append"}, std::string_view{"to_lower"}, std::string_view{"to_upper"}, std::string_view{"trim"},
        std::string_view{"trim_left"}, std::string_view{"trim_right"}, std::string_view{"pad_left"},
        std::string_view{"pad_right"}, std::string_view{"pad_center"}, std::string_view{"replace_first"},
        std::string_view{"replace_last"}, std::string_view{"replace_all"}, std::string_view{"insert"},
        std::string_view{"erase"}, std::string_view{"remove_prefix"}, std::string_view{"remove_suffix"},
        std::string_view{"reverse"}, std::string_view{"repeat"}
    };

    // The whitespace the string library classifies (trim, split's no-separator form): string::whitespace spells it, and
    // split/explode read exactly this text as "any run of whitespace" rather than a literal separator
    inline constexpr std::string_view k_whitespace_text = " \t\n\v\f\r";
    inline constexpr bool is_string_whitespace(char c) noexcept { return c == ' ' || (c >= '\t' && c <= '\r'); }

    // Backend-neutral invocation context for the builtin container/string/pointer methods
    struct builtin_method_context {
        engine* engine_ref = nullptr;
        string_symbolizer* symbolizer = nullptr;
        // the calling backend's language == and its unwinding flag (a user operator== may throw)
        checked_result<bool> (*equal_fn)(void*, const script_value&, const script_value&) = nullptr;
        bool (*unwinding_fn)(void*) = nullptr;
        void* backend_self = nullptr;
		bool copy_array_result = false; // namespace transforms create a result; dot forms mutate

        engine* get_engine() const { return engine_ref; }
        string_symbolizer* get_string_symbolizer() const { return symbolizer; }
        checked_result<bool> language_equal(const script_value& a, const script_value& b) const {
            if (!equal_fn) { return a == b; }   // a host-built context without a backend: identity
            return equal_fn(backend_self, a, b);
        }
        bool unwinding() const { return unwinding_fn && unwinding_fn(backend_self); }

        script_value make_value() const { return script_value(std::monostate{}, engine_ref); }
        script_value make_value(script_int v) const { return script_value(v, engine_ref); }
        script_value make_value(script_float v) const { return script_value(v, engine_ref); }
        script_value make_value(script_bool v) const { return script_value(v, engine_ref); }
        script_value make_value(script_char v) const { return script_value(v, engine_ref); }
        script_value make_value(const script_string& v) const { return script_value(v, engine_ref); }
        script_value make_value(script_string&& v) const { return script_value(std::move(v), engine_ref); }
    };

    using builtin_method = std::function<checked_result<script_value>(const builtin_method_context&, script_value&, const std::vector<script_value>&)>;

    namespace detail {
        // Same-kind scalars answer without the backend; everything else is the language's == (a user operator==, memberwise)
        inline std::optional<bool> quick_language_equal(const script_value& a, const script_value& b) {
            const size_t ka = a.raw_storage_index(), kb = b.raw_storage_index();
            if (ka == script_value::TYPEID_INT && kb == script_value::TYPEID_INT) { return a.unchecked_as_int() == b.unchecked_as_int(); }
            if (ka == script_value::TYPEID_STRING && kb == script_value::TYPEID_STRING) { return a.unchecked_as_string() == b.unchecked_as_string(); }
            if (ka == script_value::TYPEID_FLOAT && kb == script_value::TYPEID_FLOAT) { return a.unchecked_as_float() == b.unchecked_as_float(); }
            if (ka == script_value::TYPEID_BOOL && kb == script_value::TYPEID_BOOL) { return a.unchecked_as_bool() == b.unchecked_as_bool(); }
            return std::nullopt;
        }
        // The first index whose element == probe (the language's ==), -1 when none; nullopt when a user operator== threw
        inline checked_result<std::optional<script_int>> array_index_of_equal(const builtin_method_context& ctx, const script_value& self, const script_value& probe_in) {
            const script_array* node = self.unchecked_array_node();
            const script_value& probe = probe_in.deref();
            for (size_t i = 0; i < node->size(); ++i) {
                const script_value elem = node->get(i, ctx.get_engine());
                const script_value& e = elem.deref();
                bool equal = false;
                if (auto quick = quick_language_equal(e, probe)) { equal = *quick; }
                else {
                    auto r = ctx.language_equal(e, probe);
                    if (!r) { return r.error_value(); }
                    if (ctx.unwinding()) { return std::optional<script_int>{}; }
                    equal = r.value();
                }
                if (equal) { return std::optional<script_int>{static_cast<script_int>(i)}; }
            }
            return std::optional<script_int>{script_int(-1)};
        }
    }

    struct builtin_method_registries {
        std::unordered_map<uint64_t, builtin_method> array_methods;
        std::unordered_map<uint64_t, builtin_method> map_methods;
        std::unordered_map<uint64_t, builtin_method> string_methods;
        std::unordered_map<uint64_t, builtin_method> utf8_methods;
        std::unordered_map<uint64_t, builtin_method> weak_ptr_methods;
        std::unordered_map<uint64_t, builtin_method> shared_ptr_methods;

        // Dense dispatch tables, NAME-SORTED so indices are identical for every engine
        // of the same binary (symbol ids are engine-local; names are not) — call sites
        // bake indices into chunks that worker backends read through their own
        // registries. The name→index maps are consulted at COMPILE time only; dispatch
        // is one indexed load. Pointers into the maps are node-stable after init.
        static constexpr uint16_t k_no_builtin = 0xFFFF;
        std::vector<const builtin_method*> array_by_index;
        std::vector<const builtin_method*> map_by_index;
        std::vector<const builtin_method*> string_by_index;
        std::vector<const builtin_method*> utf8_by_index;   // utf8_string's methods: the string library in characters
        std::unordered_map<uint64_t, uint16_t> array_index;
        std::unordered_map<uint64_t, uint16_t> map_index;
        std::unordered_map<uint64_t, uint16_t> string_index;
        std::unordered_map<uint64_t, uint16_t> utf8_index;
        // k_string_mutator_method_names by symbol id and by string_by_index position
        std::unordered_set<uint64_t> string_mutator_ids;
        std::vector<uint8_t> string_mutates_by_index;
    };

    // ONE implementation shared by every backend (defined in interpreter.cpp)
    void init_builtin_method_registries(string_symbolizer* symbolizer, builtin_method_registries& out);

    namespace detail {
        // Emits ONE warning per class-defined method (local, not inherited) that shadows a
        // shared_ptr handle builtin (k_shadowable_handle_method_names). Called at class-
        // definition time by BOTH backends after register_script_class - always-on
        // (independent of static checking), parity-by-construction. Defined in interpreter.cpp.
        void warn_shadowed_handle_builtins(engine& eng, const class_definition& def);

        // Builtin args are BY-VALUE. A subscript read in argument position mints an
        // owner-pinned element/map-entry reference (transient_read.hpp leaves call args
        // unmarked so ref params can bind), and script/native parameter binding derefs
        // it - but builtins receive the raw args vector, where a reference-typed key
        // never matches map.find (operator<=> doesn't deref) and unchecked_as_string
        // misreads the holder. Normalize at EVERY builtin dispatch boundary, one shared
        // spelling for both backends, so builtins see what a hoisted temp would.
        inline void deref_builtin_args_in_place(std::vector<script_value>& args) {
            for (auto& arg : args) {
                if (arg.is_reference()) [[unlikely]] {
                    script_value resolved(arg.deref());
                    arg = std::move(resolved);
                }
            }
        }
        inline const std::vector<script_value>& deref_builtin_args(
                const std::vector<script_value>& args, std::vector<script_value>& scratch) {
            bool needs_resolve = false;
            for (const auto& arg : args) {
                if (arg.is_reference()) { needs_resolve = true; break; }
            }
            if (!needs_resolve) [[likely]] {
                return args;
            }
            scratch.reserve(args.size());
            for (const auto& arg : args) {
                scratch.emplace_back(arg.is_reference() ? script_value(arg.deref()) : arg);
            }
            return scratch;
        }
    }

} // namespace jai

#endif // __JAISCRIPT_DETAIL_BUILTIN_METHODS_HPP__
