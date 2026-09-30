#include <jaiscript/jaiscript.hpp>
#include <jaiscript/detail/null_member_diagnostic.hpp>
#include <jaiscript/detail/inline_variable.hpp>
#include <jaiscript/stdlib/containers.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/detail/integer_ops.hpp>   // kCheckedOverflow + jai::ints overflow policy
#include <jaiscript/detail/body_walker.hpp>   // nested-coroutine capture analysis
#include <jaiscript/detail/math_intrinsics.hpp>   // math:: language intrinsics (shared kernel)
#include <jaiscript/detail/aggregate_lowering.hpp>
#include <jaiscript/detail/redeclaration.hpp>
#include <jaiscript/detail/math_component_store.hpp>   // math-value component stores (vm parity)
#include <jaiscript/detail/ref_lvalue.hpp>    // shared ref-param lvalue binding (vm parity)
#include <jaiscript/detail/method_value.hpp>
#include <jaiscript/detail/string_mutator_receiver.hpp>
#include <jaiscript/detail/string_element.hpp>
#include <jaiscript/detail/import_paths.hpp>
#include <jaiscript/detail/capture_ladder.hpp> // shared capture-source ladder (vm parity)
#include <jaiscript/detail/cast_kernel.hpp>   // cast<T>/cast<T&>/cast<shared_ptr<T>> kernel (vm parity)
#include <jaiscript/detail/static_store.hpp>  // class-qualified static stores (vm parity)
#include <jaiscript/detail/namespace_scope.hpp>  // a namespace's variable cells and the ns::x store target (shared with the vm)
#include <jaiscript/detail/transient_read.hpp>   // callout_free (subscript-compound fast path)
#include <jaiscript/detail/binary_handlers.hpp>
#include <jaiscript/detail/arg_capture.hpp>   // call arguments take their value when evaluated (vm parity)
#include <jaiscript/detail/char_promotion.hpp>   // char operands promote to int64 0..255
#include <jaiscript/detail/utf8_operands.hpp>
#include <jaiscript/detail/utf8_text.hpp>
#include <jaiscript/detail/container_enforce.hpp> // typed-container boundary kernel (vm parity)
#include <jaiscript/detail/parallel_transform.hpp>   // parallel_borrow_subscript_read (captured reads)
#include <jaiscript/detail/ast_serializer.hpp>   // structural_node_key (hot-reload identity)
#include <jaiscript/detail/type_proofs.hpp>   // parse-time type proof guards (vm parity)
#include <jaiscript/detail/typed_slot.hpp>   // the one typed-slot conversion table (vm parity)
#include <jaiscript/detail/range_for_diagnostic.hpp>   // range-for texts (vm parity)
#include <jaiscript/detail/float_text.hpp>
#include <jaiscript/debug/controller.hpp>     // step-debugger statement hook
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <filesystem>
#include <jaiscript/detail/string_methods_more.hpp>
#include <jaiscript/detail/utf8_methods.hpp>
#include <jaiscript/detail/binary_data.hpp>
#include <fstream>

namespace jai {

namespace {
    using detail::capture_call_argument;

    // Local helper: error result for an overflowing integer op in the inlined
    // fast paths below (mirrors interpreter_dispatch.cpp's handlers).
    inline checked_result<void> int_overflow_v(const char* msg) {
        return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand), msg);
    }
    inline checked_result<script_value> int_overflow_sv(const char* msg) {
        return checked_result<script_value>(make_error_code(runtime_error_code::invalid_numeric_operand), msg);
    }

    // Decl-ref (auto&/T& x = y) aliasing of a value that is already a reference: SHARE
    // the holder, constraint included. RULED (2026-07-06): element/field/map-entry refs
    // keep their bind-time type constraint through a ref decl — auto& over an
    // array<int> element enforces int on store exactly like direct element assignment
    // (reverses the e43f8a6f constraint-dropping pin). Sharing also keeps container
    // re-resolution (realloc/shrink safe). KEEP BYTE-PARALLEL with vm_decl_ref_alias.
    checked_result<script_value> decl_ref_alias(const script_value& source, engine* eng) {
        (void)eng;
        auto refHolder = source.get_reference_holder();
        if (!refHolder) {
            return checked_result<script_value>(make_error_code(runtime_error_code::invalid_reference), "Reference target is null");
        }
        return script_value(source);
    }

    // Box-in-place + share: non-ref storage moves into a cell and the storage becomes
    // the handle (all read/store paths handle the boxed form); ref storage just shares
    // its holder. The one way env-variable storage is ever bound by reference — decl
    // refs, [&] captures, ref returns. KEEP BYTE-PARALLEL with vm_backend::share_env_ref
    // (the vm twin also demotes active counted-for fast states).
    script_value share_boxed_env_storage(script_value& storage, engine* eng) {
        if (!storage.is_reference()) {
            script_value inner = std::move(storage);
            storage = script_value::make_cell_reference(std::move(inner), eng);
        }
        return script_value(storage);
    }

    // A handle's backend_state is always interpreter-owned here: an engine's backend
    // is fixed before its first execute, so no other backend can have populated it.
    interpreter_coroutine_state& coroutine_state(coroutine_handle& handle) {
        auto* state = static_cast<interpreter_coroutine_state*>(handle.backend_state());
        if (!state) {
            auto owned = std::make_unique<interpreter_coroutine_state>();
            state = owned.get();
            handle.set_backend_state(std::move(owned));
        }
        return *state;
    }

    // Replay parking for the generic call lane: the callee ahead of the arguments
    std::vector<script_value> parked_with_head(const script_value& head, std::vector<script_value> rest) {
        rest.insert(rest.begin(), head);
        return rest;
    }
}

// An operand site inside an expression visitor: on the yield signal, park what this node
// had produced (evaluation order) and the suspended child's ordinal, then keep unwinding
#define JAISCRIPT_TRY_OPERAND(expr, node, phase, ordinal, partial) \
    do { \
        auto __result = (expr); \
        if (!__result) [[unlikely]] { \
            if (is_yield_signal(__result)) park_operands(node, phase, ordinal, partial); \
            return __result.error_value(); \
        } \
    } while(0)

// A statement's one expression: the signal makes the statement the replay root
#define JAISCRIPT_TRY_ROOT(stmt, root) \
    do { \
        auto __result = eval_root<Replaying>(root); \
        if (!__result) [[unlikely]] { \
            if (is_yield_signal(__result)) { note_statement_yield(stmt, root); return {}; } \
            return __result.error_value(); \
        } \
    } while(0)

// Initialize built-in method registries with interned method names for O(1) lookup
void init_builtin_method_registries(string_symbolizer* symbolizer, builtin_method_registries& out) {
    // Array methods
    out.array_methods = {
        {symbolizer->intern("size"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "size() takes no arguments");
        }
        return ctx.make_value(static_cast<script_int>(self.unchecked_array_node()->size()));
    }},

        {symbolizer->intern("push"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "push() takes exactly one argument");
        }

        // Get element type from array's type_info
        auto array_type_info = self.get_type_info();
        type_info_ptr element_type = array_type_info ? array_type_info->element_type() : nullptr;

        // Validate element type compatibility
        if (!detail::container_element_compatible(args[0], element_type, self.get_engine())) {
            // Get type names for error message
            std::string value_type = detail::container_value_type_name(args[0]);
            std::string expected_type = detail::type_info_display_name(element_type);
            // Intern the type names for the error message
            uint64_t value_type_id = ctx.get_string_symbolizer()->intern(value_type);
            uint64_t expected_type_id = ctx.get_string_symbolizer()->intern(expected_type);
            return checked_result<script_value>(
                make_error_code(runtime_error_code::array_element_type_mismatch),
                "Cannot push '{0}' to array<{1}>",
                value_type_id, expected_type_id);
        }

        // Convert element if needed (e.g., int -> float for array<float>)
        auto converted_result = detail::convert_container_element(ctx.get_engine(), args[0], element_type);
        if (!converted_result) { return converted_result.error_value(); }
        script_value converted = std::move(converted_result.value());

        // engine::memory_cap chokepoint: deny the growth before it exists
        if (engine* eng = ctx.get_engine()) {
            auto& limits = eng->execution_limits();
            const size_t bytes = sizeof(script_value) + (converted.is_string() ? converted.unchecked_as_string().size() : 0);
            if (!limits.memory_charge(bytes)) [[unlikely]] {
                return detail::raise_memory_cap(limits);
            }
        }

        self.unchecked_get_array_storage()->push(std::move(converted));
        return ctx.make_value();
    }},

        {symbolizer->intern("pop"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "pop() takes no arguments");
        }
        auto& node = *self.unchecked_get_array_storage();
        if (node.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::array_empty), "Cannot pop from empty array");
        }
        script_value last = node.get(node.size() - 1, ctx.get_engine());
        node.pop_back();
        return last;
    }},

        {symbolizer->intern("empty"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "empty() takes no arguments");
        }
        return ctx.make_value(self.unchecked_array_node()->empty());
    }},

        {symbolizer->intern("clear"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "clear() takes no arguments");
        }
        self.unchecked_get_array_storage()->clear();
        return ctx.make_value();
    }},

        {symbolizer->intern("front"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "front() takes no arguments");
        }
        const script_array* node = self.unchecked_array_node();
        if (node->empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::array_empty), "Cannot get front of empty array");
        }
        return node->get(0, ctx.get_engine());
    }},

        {symbolizer->intern("back"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "back() takes no arguments");
        }
        const script_array* node = self.unchecked_array_node();
        if (node->empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::array_empty), "Cannot get back of empty array");
        }
        return node->get(node->size() - 1, ctx.get_engine());
    }},

        {symbolizer->intern("index_of"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "index_of() takes exactly one argument");
        }
        auto found = detail::array_index_of_equal(ctx, self, args[0]);   // the language's ==
        if (!found) { return found.error_value(); }
        if (!found.value()) { return ctx.make_value(); }
        return ctx.make_value(*found.value());
    }},

        {symbolizer->intern("has"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "has() takes exactly one argument");
        }
        auto found = detail::array_index_of_equal(ctx, self, args[0]);   // the language's ==
        if (!found) { return found.error_value(); }
        if (!found.value()) { return ctx.make_value(); }
        return ctx.make_value(*found.value() >= 0);
    }},

        {symbolizer->intern("contains"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "contains() takes exactly one argument");
        }
        auto found = detail::array_index_of_equal(ctx, self, args[0]);   // the language's ==
        if (!found) { return found.error_value(); }
        if (!found.value()) { return ctx.make_value(); }
        return ctx.make_value(*found.value() >= 0);
    }},

        {symbolizer->intern("first"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "first() takes no arguments");
        }
        const script_array* node = self.unchecked_array_node();
        if (node->empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::array_empty), "Cannot get first of empty array");
        }
        return node->get(0, ctx.get_engine());
    }},

        {symbolizer->intern("last"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "last() takes no arguments");
        }
        const script_array* node = self.unchecked_array_node();
        if (node->empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::array_empty), "Cannot get last of empty array");
        }
        return node->get(node->size() - 1, ctx.get_engine());
    }},

        {symbolizer->intern("length"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "length() takes no arguments");
        }
        return ctx.make_value(static_cast<script_int>(self.unchecked_array_node()->size()));
    }},

        {symbolizer->intern("slice"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 2) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "slice() takes exactly two arguments");
        }
        if (!args[0].is_int() || !args[1].is_int()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "slice() arguments must be integers");
        }
        const script_array* node = self.unchecked_array_node();
        script_int start = args[0].unchecked_as_int();
        script_int end = args[1].unchecked_as_int();

        // Handle negative indices
        if (start < 0) start = static_cast<script_int>(node->size()) + start;
        if (end < 0) end = static_cast<script_int>(node->size()) + end;

        // Clamp to valid range
        start = std::max<script_int>(0, std::min<script_int>(start, static_cast<script_int>(node->size())));
        end = std::max<script_int>(0, std::min<script_int>(end, static_cast<script_int>(node->size())));

        if (start > end) start = end;

        // the slice has the receiver's element type (a typed array's slice is typed)
        auto self_type = self.get_type_info();
        type_info_ptr element_type = self_type && self_type->base_type == script_value_type::jai_array_type ? self_type->element_type() : nullptr;
        script_value result = script_value::make_array(element_type, ctx.get_engine());
        auto& result_node = *result.unchecked_get_array_storage();
        for (script_int i = start; i < end; ++i) {
            result_node.push(node->get(static_cast<size_t>(i), ctx.get_engine()).clone());
        }
        return result;
    }},

        {symbolizer->intern("filter"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "filter() takes exactly one argument");
        }
        if (!args[0].is_function()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "filter() requires a function argument");
        }

        // Snapshot first: func is user script that may mutate THIS array (shared
        // storage), reallocating the buffer and invalidating the iterator.
        const script_array* self_node = self.unchecked_array_node();
        const std::vector<script_value> arr = self_node->is_typed()
            ? self_node->materialize_values(ctx.get_engine()) : self_node->values();
        const auto& func = args[0].unchecked_as_function();
        script_value result = script_value::make_array(nullptr, ctx.get_engine());
        auto& resultPtr = result.unchecked_get_array_storage()->values();

        for (const auto& elem : arr) {
            auto call_result = func({elem});
            if (!call_result) {
                return call_result.error_value();
            }
            if (call_result.value().is_bool() && call_result.value().unchecked_as_bool()) {
                resultPtr.push_back(elem.clone());
            }
        }
        return result;
    }},

        {symbolizer->intern("sort"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() > 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "sort() takes zero or one argument");
        }

        auto& node = *self.unchecked_get_array_storage();
        if (node.is_typed() && args.empty() && node.kind() != script_array::kind_t::obj && !script_array::is_vec_kind(node.kind())) {
            // raw buffer sort: no comparator, so no user script can mutate mid-sort
            switch (node.kind()) {
                case script_array::kind_t::i64: std::sort(node.ints().begin(), node.ints().end()); break;
                case script_array::kind_t::f64: std::sort(node.floats().begin(), node.floats().end()); break;
                case script_array::kind_t::c8:
                    // chars order as script_char (signed), exactly like the boxed compare
                    std::sort(node.bytes().begin(), node.bytes().end(), [](uint8_t a, uint8_t b) {
                        return static_cast<script_char>(a) < static_cast<script_char>(b);
                    });
                    break;
                case script_array::kind_t::i8:
                    std::sort(node.bytes().begin(), node.bytes().end(), [](uint8_t a, uint8_t b) {
                        return static_cast<int8_t>(a) < static_cast<int8_t>(b);
                    });
                    break;
                case script_array::kind_t::str:
                    std::sort(node.strs().begin(), node.strs().end(),
                              [](const strong_ptr<script_string>& a, const strong_ptr<script_string>& b) { return *a < *b; });
                    break;
                default: std::sort(node.bytes().begin(), node.bytes().end()); break;   // b8: false < true; u8: 0..255
            }
            return ctx.make_value();
        }

        // Sort a SNAPSHOT, then write it back. The comparator (default or custom) can
        // run user script that mutates THIS array; sorting the live buffer while it
        // reallocates is undefined behavior / a crash. (A user-supplied comparator
        // that isn't a strict weak ordering is still the caller's responsibility, as
        // in C++ — but it can no longer corrupt the container.)
        std::vector<script_value> tmp = node.is_typed()
            ? node.materialize_values(ctx.get_engine()) : node.values();

        if (args.empty()) {
            // Default order: numerics by value (int/float mixed too), other primitives by script_value's total <=> order.
            // An instance, a container or a callable has no built-in order (CLAUDE.md: ordering stays off containers and
            // instances): it sorts by the language's `<` - a user operator< on the left element's class - and without one
            // raises the < operator's own rejection; never by storage address, which differs between runs and backends.
            engine* eng = ctx.get_engine();
            auto* backend = eng->get_execution_backend();
            const uint64_t less_id = ctx.get_string_symbolizer()->intern("<");
            std::optional<error_propagator> order_error;
            bool order_threw = false;
            auto unordered = [](const script_value& v) {
                const size_t k = v.raw_storage_index();
                return k == script_value::TYPEID_OBJECT || k == script_value::TYPEID_SHARED_PTR || k == script_value::TYPEID_ARRAY ||
                       k == script_value::TYPEID_MAP || k == script_value::TYPEID_FUNCTION || k == script_value::TYPEID_WEAK_PTR;
            };
            std::sort(tmp.begin(), tmp.end(), [&](const script_value& a_in, const script_value& b_in) {
                if (order_error || order_threw) return false;   // the first failure ends the calls; the array stays as it was
                const script_value& a = a_in.is_reference() ? a_in.deref() : a_in;
                const script_value& b = b_in.is_reference() ? b_in.deref() : b_in;
                const bool an = a.is_int() || a.is_float();
                const bool bn = b.is_int() || b.is_float();
                if (an && bn) {
                    double av = a.is_int() ? static_cast<double>(a.unchecked_as_int()) : a.unchecked_as_float();
                    double bv = b.is_int() ? static_cast<double>(b.unchecked_as_int()) : b.unchecked_as_float();
                    return av < bv;
                }
                if (script_value::is_math_index(a.raw_storage_index()) || script_value::is_math_index(b.raw_storage_index())) {
                    order_error = detail::math::ordering_error(a, b).error_value();   // math values have no script order (design 5.2)
                    return false;
                }
                if (!unordered(a) && !unordered(b)) {
                    return a < b;  // total order via operator<=> (numerics have lowest type ids)
                }
                auto view = detail::resolve_slot_class(a, eng);
                script_value method(std::monostate{}, eng);
                if (view.def) { method = view.def->get_method(less_id, false); }
                if (!method.is_invalid() && method.is_function()) {
                    script_value bound = make_bound_method(a, method);
                    auto result = bound.as_function()({b});
                    if (!result) {
                        order_error = result.error_value();
                        return false;
                    }
                    if (backend && backend->is_unwinding()) {
                        order_threw = true;
                        return false;
                    }
                    if (result.value().is_bool()) return result.value().unchecked_as_bool();
                }
                order_error = error_propagator{make_error_code(runtime_error_code::type_mismatch), "Invalid operands for < operator"};
                return false;
            });
            if (order_error) {
                return *order_error;
            }
            if (order_threw) {
                return ctx.make_value();
            }
        } else {
            // Custom comparator
            if (!args[0].is_function()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "sort() comparator must be a function");
            }
            const auto& comparator = args[0].unchecked_as_function();
            auto* backend = ctx.get_engine()->get_execution_backend();
            std::optional<error_propagator> comparator_error;
            bool comparator_threw = false;
            std::sort(tmp.begin(), tmp.end(), [&](const script_value& a, const script_value& b) {
                if (comparator_error || comparator_threw) return false;   // the first failure ends the calls; the array stays as it was
                auto result = comparator({a, b});
                if (!result) {
                    comparator_error = result.error_value();
                    return false;
                }
                if (backend && backend->is_unwinding()) {
                    comparator_threw = true;
                    return false;
                }
                return result.value().is_bool() && result.value().unchecked_as_bool();
            });
            if (comparator_error) {
                return *comparator_error;
            }
            if (comparator_threw) {
                return ctx.make_value();
            }
        }
        if (node.is_typed()) {
            // snapshot wins wholesale (matches the hetero write-back below)
            node.clear();
            for (auto& v : tmp) { node.push(std::move(v)); }
        } else {
            node.values() = std::move(tmp);
        }
        return ctx.make_value();
    }},

        {symbolizer->intern("reverse"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "reverse() takes no arguments");
        }
        self.unchecked_get_array_storage()->reverse();
        return ctx.make_value();
    }},

        {symbolizer->intern("remove"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "remove() takes exactly one argument");
        }
        if (!args[0].is_int()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "remove() argument must be an integer");
        }
        auto& node = *self.unchecked_get_array_storage();
        script_int index = args[0].unchecked_as_int();

        if (index < 0 || index >= static_cast<script_int>(node.size())) {
            return ctx.make_value(false);
        }

        node.erase_at(static_cast<size_t>(index));
        return ctx.make_value(true);
    }},

        {symbolizer->intern("remove_if"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "remove_if() takes exactly one argument");
        }
        if (!args[0].is_function()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "remove_if() requires a function argument");
        }

        auto& node = *self.unchecked_get_array_storage();
        const auto& predicate = args[0].unchecked_as_function();
        script_int removed_count = 0;

        // Snapshot the elements before iterating: the predicate is user script that
        // can mutate THIS array (arrays share strong_ptr storage), and a push/clear
        // would reallocate the buffer and dangle a live iterator. Evaluate against the
        // snapshot, collect survivors, then write them back.
        std::vector<script_value> snapshot = node.is_typed()
            ? node.materialize_values(ctx.get_engine()) : node.values();
        std::vector<script_value> kept;
        kept.reserve(snapshot.size());
        for (auto& elem : snapshot) {
            auto call_result = predicate({elem});
            if (!call_result) {
                return call_result.error_value();
            }
            if (call_result.value().is_bool() && call_result.value().unchecked_as_bool()) {
                ++removed_count;          // dropped
            } else {
                kept.push_back(elem);     // survivor
            }
        }
        if (node.is_typed()) {
            node.clear();
            for (auto& v : kept) { node.push(std::move(v)); }
        } else {
            node.values() = std::move(kept);
        }
        return ctx.make_value(removed_count);
    }},

        {symbolizer->intern("join"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() > 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "join() takes 0 or 1 argument (separator)");
        }

        std::string_view sep;
        if (args.size() > 0) {
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "join() argument must be a string");
            }
            sep = args[0].unchecked_as_string();
        }

        const script_array* node = self.unchecked_array_node();
        engine* eng = ctx.get_engine();
        // one buffer: string elements by their length, everything else by a short estimate;
        // hetero elements read in place, typed kinds box once per element
        size_t total = node->size() > 1 ? sep.size() * (node->size() - 1) : 0;
        node->for_each(eng, [&](const script_value& e) { total += e.is_string() ? e.unchecked_as_string().size() : 8; });
        std::string result;
        result.reserve(total);

        bool first = true;
        node->for_each(eng, [&](const script_value& elem) {
            if (!first) {
                result += sep;
            }
            first = false;
            if (elem.is_string()) {
                result += elem.unchecked_as_string();
            } else if (elem.is_int()) {
                result += std::to_string(elem.unchecked_as_int());
            } else if (elem.is_float()) {
                result += script_value::float_text(elem.unchecked_as_float());
            } else if (elem.is_bool()) {
                result += elem.unchecked_as_bool() ? "true" : "false";
            } else if (elem.is_char()) {
                result += elem.unchecked_as_char();
            } else if (elem.is_null()) {
                result += "null";
            } else if (script_value::is_math_index(elem.raw_storage_index())) {
                result += elem.to_string();
            } else {
                result += "[object]";
            }
        });

        return ctx.make_value(std::move(result));
    }}
    };

    // Map methods
    out.map_methods = {
        {symbolizer->intern("size"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "size() takes no arguments");
        }
        return ctx.make_value(static_cast<script_int>(self.unchecked_as_map().size()));
    }},

        {symbolizer->intern("empty"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "empty() takes no arguments");
        }
        return ctx.make_value(self.unchecked_as_map().empty());
    }},

        {symbolizer->intern("clear"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "clear() takes no arguments");
        }
        auto& mapPtr = self.unchecked_get_map_storage();
        mapPtr->clear();
        return ctx.make_value();
    }},

        {symbolizer->intern("contains"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "contains() takes exactly one argument");
        }
        const auto& map = self.unchecked_as_map();
        return ctx.make_value(map.find(args[0]) != map.end());
    }},

        {symbolizer->intern("erase"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "erase() takes exactly one argument");
        }
        auto& mapPtr = self.unchecked_get_map_storage();
        mapPtr->erase(args[0]);
        return ctx.make_value();
    }},

        {symbolizer->intern("keys"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "keys() takes no arguments");
        }
        const auto& map = self.unchecked_as_map();
        script_value result = script_value::make_array(nullptr, ctx.get_engine());
        auto& arrayPtr = result.unchecked_get_array_storage()->values();
        arrayPtr.reserve(map.size());
        for (const auto& [key, value] : map) {
            arrayPtr.push_back(key.clone());
        }
        return result;
    }},

        {symbolizer->intern("values"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "values() takes no arguments");
        }
        const auto& map = self.unchecked_as_map();
        script_value result = script_value::make_array(nullptr, ctx.get_engine());
        auto& arrayPtr = result.unchecked_get_array_storage()->values();
        arrayPtr.reserve(map.size());
        for (const auto& [key, value] : map) {
            arrayPtr.push_back(value.clone());
        }
        return result;
    }},

        {symbolizer->intern("has"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "has() takes exactly one argument");
        }
        const auto& map = self.unchecked_as_map();
        return ctx.make_value(map.find(args[0]) != map.end());
    }},

        {symbolizer->intern("get"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 2) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "get() takes exactly two arguments (key, default)");
        }
        const auto& map = self.unchecked_as_map();
        auto it = map.find(args[0]);
        if (it != map.end()) {
            return it->second;
        }
        return args[1];  // Return default value
    }},

        {symbolizer->intern("length"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "length() takes no arguments");
        }
        return ctx.make_value(static_cast<script_int>(self.unchecked_as_map().size()));
    }},

        {symbolizer->intern("remove"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "remove() takes exactly one argument");
        }
        auto& mapPtr = self.unchecked_get_map_storage();
        auto it = mapPtr->find(args[0]);
        if (it != mapPtr->end()) {
            mapPtr->erase(it);
            return ctx.make_value(true);
        }
        return ctx.make_value(false);
    }},

        {symbolizer->intern("remove_if"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "remove_if() takes exactly one argument");
        }
        if (!args[0].is_function()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "remove_if() requires a function argument");
        }

        auto& mapPtr = self.unchecked_get_map_storage();
        const auto& predicate = args[0].unchecked_as_function();
        script_int removed_count = 0;

        for (auto it = mapPtr->begin(); it != mapPtr->end(); ) {
            auto call_result = predicate({it->first, it->second});
            if (!call_result) {
                return call_result.error_value();
            }
            const auto& val = call_result.value();
            if (val.is_bool() && val.unchecked_as_bool()) {
                it = mapPtr->erase(it);
                ++removed_count;
            } else {
                ++it;
            }
        }
        return ctx.make_value(removed_count);
    }},

        {symbolizer->intern("filter"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "filter() takes exactly one argument");
        }
        if (!args[0].is_function()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "filter() requires a function argument");
        }

        const auto& map = self.unchecked_as_map();
        const auto& predicate = args[0].unchecked_as_function();
        script_value result = script_value::make_map(nullptr, nullptr, ctx.get_engine());
        auto& resultPtr = result.unchecked_get_map_storage();

        for (const auto& [key, value] : map) {
            auto call_result = predicate({key, value});
            if (!call_result) {
                return call_result.error_value();
            }
            const auto& val = call_result.value();
            if (val.is_bool() && val.unchecked_as_bool()) {
                (*resultPtr)[key.clone()] = value.clone();
            }
        }
        return result;
    }},

        {symbolizer->intern("to_array"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "to_array() takes no arguments");
        }
        const auto& map = self.unchecked_as_map();
        script_value result = script_value::make_array(nullptr, ctx.get_engine());
        auto& arrayPtr = result.unchecked_get_array_storage()->values();
        arrayPtr.reserve(map.size());

        // Return array of [key, value] pairs
        for (const auto& [key, value] : map) {
            script_value pair = script_value::make_array(nullptr, ctx.get_engine());
            auto& pairPtr = pair.unchecked_get_array_storage()->values();
            pairPtr.push_back(key.clone());
            pairPtr.push_back(value.clone());
            arrayPtr.push_back(std::move(pair));
        }
        return result;
    }}
    };

    // String methods - enable str.length(), str.substr(), etc.
    // Observer methods return values; mutating methods modify in place and return self
    out.string_methods = {
        // ============================================================
        // Observer methods (non-mutating)
        // ============================================================

        {symbolizer->intern("length"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "length() takes no arguments");
            }
            return ctx.make_value(static_cast<script_int>(self.unchecked_text().size()));
        }},

        {symbolizer->intern("size"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "size() takes no arguments");
            }
            return ctx.make_value(static_cast<script_int>(self.unchecked_text().size()));
        }},

        {symbolizer->intern("empty"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "empty() takes no arguments");
            }
            return ctx.make_value(self.unchecked_text().empty());
        }},

        {symbolizer->intern("at"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "at() takes exactly one argument");
            }
            if (!args[0].is_int()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "at() argument must be an integer");
            }
            const auto& str = self.unchecked_text();
            script_int idx = args[0].unchecked_as_int();
            script_int len = static_cast<script_int>(str.size());

            // Normalize negative index
            if (idx < 0) idx += len;

            // Throw if out of bounds
            if (idx < 0 || idx >= len) {
                return checked_result<script_value>(make_error_code(runtime_error_code::index_out_of_bounds), "at() index out of bounds");
            }

            // Return single character as string
            return ctx.make_value(std::string(1, str[static_cast<size_t>(idx)]));
        }},

        {symbolizer->intern("front"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "front() takes no arguments");
            }
            const auto& str = self.unchecked_text();
            if (str.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::index_out_of_bounds), "front() called on empty string");
            }
            return ctx.make_value(std::string(1, str.front()));
        }},

        {symbolizer->intern("back"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "back() takes no arguments");
            }
            const auto& str = self.unchecked_text();
            if (str.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::index_out_of_bounds), "back() called on empty string");
            }
            return ctx.make_value(std::string(1, str.back()));
        }},

        {symbolizer->intern("substr"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1 || args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "substr() takes 1 or 2 arguments (start, [length])");
            }
            if (!args[0].is_int()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "substr() start argument must be an integer");
            }
            const auto& str = self.unchecked_text();
            script_int start = args[0].unchecked_as_int();
            script_int len = static_cast<script_int>(str.size());

            // Handle negative start index
            if (start < 0) start += len;
            if (start < 0) start = 0;
            if (start >= len) {
                return ctx.make_value(std::string(""));
            }

            if (args.size() == 2) {
                if (!args[1].is_int()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "substr() length argument must be an integer");
                }
                script_int sub_len = args[1].unchecked_as_int();
                if (sub_len < 0) sub_len = 0;
                return ctx.make_value(str.substr(static_cast<size_t>(start), static_cast<size_t>(sub_len)));
            }
            return ctx.make_value(str.substr(static_cast<size_t>(start)));
        }},

        // ============================================================
        // Search methods
        // ============================================================

        {symbolizer->intern("find"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1 || args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "find() takes 1 or 2 arguments (substr, [start])");
            }
            if (!args[0].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find() first argument must be a string");
            }
            const auto& str = self.unchecked_text();
            const auto& search = args[0].unchecked_text();
            script_int len = static_cast<script_int>(str.size());

            size_t start_pos = 0;
            if (args.size() == 2) {
                if (!args[1].is_int()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find() second argument must be an integer");
                }
                script_int start = args[1].unchecked_as_int();
                if (start < 0) start += len;
                if (start < 0) start = 0;
                start_pos = static_cast<size_t>(start);
            }

            auto pos = str.find(search, start_pos);
            if (pos == std::string::npos) {
                return ctx.make_value(static_cast<script_int>(-1));
            }
            return ctx.make_value(static_cast<script_int>(pos));
        }},

        {symbolizer->intern("rfind"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1 || args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "rfind() takes 1 or 2 arguments (substr, [start])");
            }
            if (!args[0].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "rfind() first argument must be a string");
            }
            const auto& str = self.unchecked_text();
            const auto& search = args[0].unchecked_text();
            script_int len = static_cast<script_int>(str.size());

            size_t start_pos = std::string::npos;
            if (args.size() == 2) {
                if (!args[1].is_int()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "rfind() second argument must be an integer");
                }
                script_int start = args[1].unchecked_as_int();
                if (start < 0) start += len;
                if (start < 0) {
                    return ctx.make_value(static_cast<script_int>(-1));
                }
                start_pos = static_cast<size_t>(start);
            }

            auto pos = str.rfind(search, start_pos);
            if (pos == std::string::npos) {
                return ctx.make_value(static_cast<script_int>(-1));
            }
            return ctx.make_value(static_cast<script_int>(pos));
        }},

        {symbolizer->intern("find_first_of"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1 || args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "find_first_of() takes 1 or 2 arguments (chars, [start])");
            }
            if (!args[0].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_first_of() first argument must be a string");
            }
            const auto& str = self.unchecked_text();
            const auto& chars = args[0].unchecked_text();
            script_int len = static_cast<script_int>(str.size());

            size_t start_pos = 0;
            if (args.size() == 2) {
                if (!args[1].is_int()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_first_of() second argument must be an integer");
                }
                script_int start = args[1].unchecked_as_int();
                if (start < 0) start += len;
                if (start < 0) start = 0;
                start_pos = static_cast<size_t>(start);
            }

            auto pos = str.find_first_of(chars, start_pos);
            if (pos == std::string::npos) {
                return ctx.make_value(static_cast<script_int>(-1));
            }
            return ctx.make_value(static_cast<script_int>(pos));
        }},

        {symbolizer->intern("find_last_of"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1 || args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "find_last_of() takes 1 or 2 arguments (chars, [start])");
            }
            if (!args[0].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_last_of() first argument must be a string");
            }
            const auto& str = self.unchecked_text();
            const auto& chars = args[0].unchecked_text();
            script_int len = static_cast<script_int>(str.size());

            size_t start_pos = std::string::npos;
            if (args.size() == 2) {
                if (!args[1].is_int()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_last_of() second argument must be an integer");
                }
                script_int start = args[1].unchecked_as_int();
                if (start < 0) start += len;
                if (start < 0) {
                    return ctx.make_value(static_cast<script_int>(-1));
                }
                start_pos = static_cast<size_t>(start);
            }

            auto pos = str.find_last_of(chars, start_pos);
            if (pos == std::string::npos) {
                return ctx.make_value(static_cast<script_int>(-1));
            }
            return ctx.make_value(static_cast<script_int>(pos));
        }},

        {symbolizer->intern("find_first_not_of"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1 || args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "find_first_not_of() takes 1 or 2 arguments (chars, [start])");
            }
            if (!args[0].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_first_not_of() first argument must be a string");
            }
            const auto& str = self.unchecked_text();
            const auto& chars = args[0].unchecked_text();
            script_int len = static_cast<script_int>(str.size());

            size_t start_pos = 0;
            if (args.size() == 2) {
                if (!args[1].is_int()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_first_not_of() second argument must be an integer");
                }
                script_int start = args[1].unchecked_as_int();
                if (start < 0) start += len;
                if (start < 0) start = 0;
                start_pos = static_cast<size_t>(start);
            }

            auto pos = str.find_first_not_of(chars, start_pos);
            if (pos == std::string::npos) {
                return ctx.make_value(static_cast<script_int>(-1));
            }
            return ctx.make_value(static_cast<script_int>(pos));
        }},

        {symbolizer->intern("find_last_not_of"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1 || args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "find_last_not_of() takes 1 or 2 arguments (chars, [start])");
            }
            if (!args[0].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_last_not_of() first argument must be a string");
            }
            const auto& str = self.unchecked_text();
            const auto& chars = args[0].unchecked_text();
            script_int len = static_cast<script_int>(str.size());

            size_t start_pos = std::string::npos;
            if (args.size() == 2) {
                if (!args[1].is_int()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_last_not_of() second argument must be an integer");
                }
                script_int start = args[1].unchecked_as_int();
                if (start < 0) start += len;
                if (start < 0) {
                    return ctx.make_value(static_cast<script_int>(-1));
                }
                start_pos = static_cast<size_t>(start);
            }

            auto pos = str.find_last_not_of(chars, start_pos);
            if (pos == std::string::npos) {
                return ctx.make_value(static_cast<script_int>(-1));
            }
            return ctx.make_value(static_cast<script_int>(pos));
        }},

        {symbolizer->intern("contains"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "contains() takes exactly one argument");
            }
            if (!args[0].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "contains() argument must be a string");
            }
            const auto& str = self.unchecked_text();
            const auto& search = args[0].unchecked_text();
            return ctx.make_value(str.find(search) != std::string::npos);
        }},

        {symbolizer->intern("starts_with"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "starts_with() takes exactly one argument");
            }
            if (!args[0].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "starts_with() argument must be a string");
            }
            const auto& str = self.unchecked_text();
            const auto& prefix = args[0].unchecked_text();
            if (prefix.size() > str.size()) {
                return ctx.make_value(false);
            }
            return ctx.make_value(str.compare(0, prefix.size(), prefix) == 0);
        }},

        {symbolizer->intern("ends_with"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "ends_with() takes exactly one argument");
            }
            if (!args[0].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "ends_with() argument must be a string");
            }
            const auto& str = self.unchecked_text();
            const auto& suffix = args[0].unchecked_text();
            if (suffix.size() > str.size()) {
                return ctx.make_value(false);
            }
            return ctx.make_value(str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0);
        }},

        {symbolizer->intern("count"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "count() takes exactly one argument");
            }
            if (!args[0].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "count() argument must be a string");
            }
            const auto& str = self.unchecked_text();
            const auto& search = args[0].unchecked_text();

            if (search.empty()) {
                return ctx.make_value(static_cast<script_int>(0));
            }

            script_int count = 0;
            size_t pos = 0;
            while ((pos = str.find(search, pos)) != std::string::npos) {
                ++count;
                pos += search.size();  // Non-overlapping
            }
            return ctx.make_value(count);
        }},

        // ============================================================
        // Parsing methods
        // ============================================================

        {symbolizer->intern("to_int"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            // to_int(default=null, base=10)
            script_value default_val(std::monostate{}, ctx.get_engine());
            int base = 10;

            if (args.size() > 0) {
                default_val = args[0];
            }
            if (args.size() > 1) {
                if (!args[1].is_int()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "to_int() base argument must be an integer");
                }
                base = static_cast<int>(args[1].unchecked_as_int());
                if (base != 0 && (base < 2 || base > 36)) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "to_int() base must be 0 or between 2 and 36");
                }
            }
            if (args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "to_int() takes at most 2 arguments (default, base)");
            }

            const auto& str = self.unchecked_text();
            try {
                size_t pos = 0;
                long long val = std::stoll(str, &pos, base);
                // Check if entire string was consumed (ignoring trailing whitespace).
                // isspace requires an unsigned-char value (or EOF); a negative char
                // (bytes >= 0x80 in UTF-8) is undefined behavior.
                while (pos < str.size() && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                if (pos != str.size()) {
                    return default_val;
                }
                return ctx.make_value(static_cast<script_int>(val));
            } catch (...) {
                return default_val;
            }
        }},

        {symbolizer->intern("to_float"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            // to_float(default=null)
            script_value default_val(std::monostate{}, ctx.get_engine());

            if (args.size() > 0) {
                default_val = args[0];
            }
            if (args.size() > 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "to_float() takes at most 1 argument (default)");
            }

            const auto& str = self.unchecked_text();
            const auto parsed = detail::parse_float_text(str.c_str());
            if (parsed.end == str.c_str() || parsed.out_of_range) {
                return default_val;
            }
            size_t pos = static_cast<size_t>(parsed.end - str.c_str());
            // Check if entire string was consumed (ignoring trailing whitespace).
            // isspace requires an unsigned-char value; a negative char is UB.
            while (pos < str.size() && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
            if (pos != str.size()) {
                return default_val;
            }
            return ctx.make_value(parsed.value);
        }},

        // ============================================================
        // Mutating methods (modify in place, return self for chaining)
        // ============================================================

        {symbolizer->intern("to_lower"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "to_lower() takes no arguments");
            }
            auto& str = self.unchecked_text_ref();
            for (auto& c : str) {
                if (c >= 'A' && c <= 'Z') {
                    c = c + ('a' - 'A');
                }
            }
            return self;
        }},

        {symbolizer->intern("to_upper"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "to_upper() takes no arguments");
            }
            auto& str = self.unchecked_text_ref();
            for (auto& c : str) {
                if (c >= 'a' && c <= 'z') {
                    c = c - ('a' - 'A');
                }
            }
            return self;
        }},

        {symbolizer->intern("trim"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            std::string chars = " \t\r\n";
            if (args.size() > 0) {
                if (!args[0].is_text()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "trim() argument must be a string");
                }
                chars = args[0].unchecked_text();
            }
            if (args.size() > 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "trim() takes at most 1 argument");
            }

            auto& str = self.unchecked_text_ref();
            size_t start = str.find_first_not_of(chars);
            if (start == std::string::npos) {
                str.clear();
            } else {
                size_t end = str.find_last_not_of(chars);
                str = str.substr(start, end - start + 1);
            }
            return self;
        }},

        {symbolizer->intern("trim_left"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            std::string chars = " \t\r\n";
            if (args.size() > 0) {
                if (!args[0].is_text()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "trim_left() argument must be a string");
                }
                chars = args[0].unchecked_text();
            }
            if (args.size() > 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "trim_left() takes at most 1 argument");
            }

            auto& str = self.unchecked_text_ref();
            size_t start = str.find_first_not_of(chars);
            if (start == std::string::npos) {
                str.clear();
            } else {
                str.erase(0, start);
            }
            return self;
        }},

        {symbolizer->intern("trim_right"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            std::string chars = " \t\r\n";
            if (args.size() > 0) {
                if (!args[0].is_text()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "trim_right() argument must be a string");
                }
                chars = args[0].unchecked_text();
            }
            if (args.size() > 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "trim_right() takes at most 1 argument");
            }

            auto& str = self.unchecked_text_ref();
            size_t end = str.find_last_not_of(chars);
            if (end == std::string::npos) {
                str.clear();
            } else {
                str.erase(end + 1);
            }
            return self;
        }},

        {symbolizer->intern("pad_left"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1 || args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "pad_left() takes 1 or 2 arguments (target_len, [char])");
            }
            if (!args[0].is_int()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "pad_left() first argument must be an integer");
            }
            script_int target_len = args[0].unchecked_as_int();

            char pad_char = ' ';
            if (args.size() > 1) {
                if (args[1].is_char()) {
                    pad_char = args[1].unchecked_as_char();
                } else if (args[1].is_text()) {
                    const auto& pad_str = args[1].unchecked_text();
                    if (pad_str.empty()) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "pad_left() padding character cannot be empty");
                    }
                    pad_char = pad_str[0];
                } else {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "pad_left() second argument must be a char or string");
                }
            }

            auto& str = self.unchecked_text_ref();
            if (target_len > static_cast<script_int>(str.size())) {
                str.insert(0, static_cast<size_t>(target_len) - str.size(), pad_char);
            }
            return self;
        }},

        {symbolizer->intern("pad_right"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1 || args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "pad_right() takes 1 or 2 arguments (target_len, [char])");
            }
            if (!args[0].is_int()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "pad_right() first argument must be an integer");
            }
            script_int target_len = args[0].unchecked_as_int();

            char pad_char = ' ';
            if (args.size() > 1) {
                if (args[1].is_char()) {
                    pad_char = args[1].unchecked_as_char();
                } else if (args[1].is_text()) {
                    const auto& pad_str = args[1].unchecked_text();
                    if (pad_str.empty()) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "pad_right() padding character cannot be empty");
                    }
                    pad_char = pad_str[0];
                } else {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "pad_right() second argument must be a char or string");
                }
            }

            auto& str = self.unchecked_text_ref();
            if (target_len > static_cast<script_int>(str.size())) {
                str.append(static_cast<size_t>(target_len) - str.size(), pad_char);
            }
            return self;
        }},

        {symbolizer->intern("pad_center"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1 || args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "pad_center() takes 1 or 2 arguments (target_len, [char])");
            }
            if (!args[0].is_int()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "pad_center() first argument must be an integer");
            }
            script_int target_len = args[0].unchecked_as_int();

            char pad_char = ' ';
            if (args.size() > 1) {
                if (args[1].is_char()) {
                    pad_char = args[1].unchecked_as_char();
                } else if (args[1].is_text()) {
                    const auto& pad_str = args[1].unchecked_text();
                    if (pad_str.empty()) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "pad_center() padding character cannot be empty");
                    }
                    pad_char = pad_str[0];
                } else {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "pad_center() second argument must be a char or string");
                }
            }

            auto& str = self.unchecked_text_ref();
            if (target_len > static_cast<script_int>(str.size())) {
                size_t total_pad = static_cast<size_t>(target_len) - str.size();
                size_t left_pad = total_pad / 2;
                size_t right_pad = total_pad - left_pad;
                str.insert(0, left_pad, pad_char);
                str.append(right_pad, pad_char);
            }
            return self;
        }},

        {symbolizer->intern("replace_first"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "replace_first() takes exactly 2 arguments (old, new)");
            }
            if (!args[0].is_text() || !args[1].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "replace_first() arguments must be strings");
            }
            const auto& old_str = args[0].unchecked_text();
            const auto& new_str = args[1].unchecked_text();

            if (old_str.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "replace_first() search string cannot be empty");
            }

            auto& str = self.unchecked_text_ref();
            size_t pos = str.find(old_str);
            if (pos != std::string::npos) {
                str.replace(pos, old_str.size(), new_str);
            }
            return self;
        }},

        {symbolizer->intern("replace_last"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "replace_last() takes exactly 2 arguments (old, new)");
            }
            if (!args[0].is_text() || !args[1].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "replace_last() arguments must be strings");
            }
            const auto& old_str = args[0].unchecked_text();
            const auto& new_str = args[1].unchecked_text();

            if (old_str.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "replace_last() search string cannot be empty");
            }

            auto& str = self.unchecked_text_ref();
            size_t pos = str.rfind(old_str);
            if (pos != std::string::npos) {
                str.replace(pos, old_str.size(), new_str);
            }
            return self;
        }},

        {symbolizer->intern("replace_all"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "replace_all() takes exactly 2 arguments (old, new)");
            }
            if (!args[0].is_text() || !args[1].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "replace_all() arguments must be strings");
            }
            const auto& old_str = args[0].unchecked_text();
            const auto& new_str = args[1].unchecked_text();

            if (old_str.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "replace_all() search string cannot be empty");
            }

            auto& str = self.unchecked_text_ref();
            size_t pos = 0;
            while ((pos = str.find(old_str, pos)) != std::string::npos) {
                str.replace(pos, old_str.size(), new_str);
                pos += new_str.size();  // Move past the replacement to avoid infinite loop
            }
            return self;
        }},

        {symbolizer->intern("insert"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "insert() takes exactly 2 arguments (pos, text)");
            }
            if (!args[0].is_int()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "insert() first argument must be an integer");
            }
            if (!args[1].is_text()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "insert() second argument must be a string");
            }

            auto& str = self.unchecked_text_ref();
            script_int pos = args[0].unchecked_as_int();
            const auto& text = args[1].unchecked_text();
            script_int len = static_cast<script_int>(str.size());

            // Normalize negative index
            if (pos < 0) pos += len;
            // Clamp to valid range
            if (pos < 0) pos = 0;
            if (pos > len) pos = len;

            // engine::memory_cap chokepoint: deny the growth before it exists
            if (engine* eng = ctx.get_engine()) {
                auto& limits = eng->execution_limits();
                if (!limits.memory_charge(text.size())) [[unlikely]] {
                    return detail::raise_memory_cap(limits);
                }
            }

            str.insert(static_cast<size_t>(pos), text);
            return self;
        }},

        {symbolizer->intern("erase"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1 || args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "erase() takes 1 or 2 arguments (pos, [count])");
            }
            if (!args[0].is_int()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "erase() first argument must be an integer");
            }

            auto& str = self.unchecked_text_ref();
            script_int pos = args[0].unchecked_as_int();
            script_int len = static_cast<script_int>(str.size());

            // Normalize negative index
            if (pos < 0) pos += len;
            if (pos < 0 || pos >= len) {
                return self;  // Out of bounds, no change
            }

            size_t count = std::string::npos;
            if (args.size() > 1) {
                if (!args[1].is_int()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "erase() second argument must be an integer");
                }
                script_int cnt = args[1].unchecked_as_int();
                if (cnt < 0) cnt = 0;
                count = static_cast<size_t>(cnt);
            }

            str.erase(static_cast<size_t>(pos), count);
            return self;
        }},

        {symbolizer->intern("remove_prefix"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "remove_prefix() takes exactly one argument");
            }
            if (!args[0].is_int()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "remove_prefix() argument must be an integer");
            }

            auto& str = self.unchecked_text_ref();
            script_int n = args[0].unchecked_as_int();
            if (n < 0) n = 0;
            if (n > 0) {
                size_t to_remove = std::min(static_cast<size_t>(n), str.size());
                str.erase(0, to_remove);
            }
            return self;
        }},

        {symbolizer->intern("remove_suffix"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "remove_suffix() takes exactly one argument");
            }
            if (!args[0].is_int()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "remove_suffix() argument must be an integer");
            }

            auto& str = self.unchecked_text_ref();
            script_int n = args[0].unchecked_as_int();
            if (n < 0) n = 0;
            if (n > 0) {
                size_t to_remove = std::min(static_cast<size_t>(n), str.size());
                str.erase(str.size() - to_remove);
            }
            return self;
        }},

        {symbolizer->intern("reverse"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "reverse() takes no arguments");
            }
            auto& str = self.unchecked_text_ref();
            std::reverse(str.begin(), str.end());
            return self;
        }},

        {symbolizer->intern("repeat"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "repeat() takes exactly one argument");
            }
            if (!args[0].is_int()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "repeat() argument must be an integer");
            }

            script_int count = args[0].unchecked_as_int();
            if (count <= 0) {
                self.unchecked_text_ref().clear();
                return self;
            }

            auto& str = self.unchecked_text_ref();
            // engine::memory_cap chokepoint: deny the growth before it exists
            if (count > 1) {
                if (engine* eng = ctx.get_engine()) {
                    auto& limits = eng->execution_limits();
                    if (!limits.memory_charge(str.size() * static_cast<size_t>(count - 1))) [[unlikely]] {
                        return detail::raise_memory_cap(limits);
                    }
                }
            }
            std::string original = str;
            str.clear();
            str.reserve(original.size() * static_cast<size_t>(count));
            for (script_int i = 0; i < count; ++i) {
                str += original;
            }
            return self;
        }},

        // ============================================================
        // Split method (returns array)
        // ============================================================

        {symbolizer->intern("split"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() > 2) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "split() takes 0 to 2 arguments (delimiter, [max_splits])");
            }
            script_int max_splits = -1;   // -1: no limit; the last element keeps the rest
            if (args.size() > 1) {
                if (!args[1].is_int()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "split() second argument must be an integer");
                }
                max_splits = args[1].unchecked_as_int();
            }

            std::string delim = "";
            bool whitespace = args.empty();
            if (args.size() > 0) {
                if (!args[0].is_text()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "split() argument must be a string");
                }
                delim = args[0].unchecked_text();
                whitespace = delim == k_whitespace_text;
            }

            const auto& str = self.unchecked_text();

            // Create array with string element type
            auto eng = ctx.get_engine();
            if (!eng) {
                return checked_result<script_value>(make_error_code(runtime_error_code::internal_error), "Engine reference expired");
            }

            script_value array_val = script_value::make_array(eng->get_type_info_string(), eng);
            script_array& result = *array_val.unchecked_get_array_storage();   // array<string> is a handle buffer: push through the node

            if (whitespace) {
                // runs of whitespace separate the pieces and no piece is empty; past max_splits the rest is the last piece
                size_t at = 0;
                script_int splits = 0;
                while (true) {
                    while (at < str.size() && is_string_whitespace(str[at])) { ++at; }
                    if (at == str.size()) { break; }
                    if (max_splits >= 0 && splits == max_splits) { result.push(ctx.make_value(str.substr(at))); break; }
                    size_t end = at;
                    while (end < str.size() && !is_string_whitespace(str[end])) { ++end; }
                    result.push(ctx.make_value(str.substr(at, end - at)));
                    at = end;
                    ++splits;
                }
            } else if (delim.empty()) {
                // Split into individual characters
                for (char c : str) {
                    result.push(ctx.make_value(std::string(1, c)));
                }
            } else {
                size_t start = 0;
                size_t end;
                script_int splits = 0;
                while ((max_splits < 0 || splits < max_splits) && (end = str.find(delim, start)) != std::string::npos) {
                    result.push(ctx.make_value(str.substr(start, end - start)));
                    start = end + delim.size();
                    ++splits;
                }
                result.push(ctx.make_value(str.substr(start)));
            }

            return array_val;
        }}
    };

    // Weak pointer methods
    out.weak_ptr_methods = {
        {symbolizer->intern("lock"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "lock() takes no arguments");
        }

        if (!self.is_weak_ptr()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "lock() can only be called on weak_ptr");
        }

        auto weak_ptr = self.get_weak_ptr();
        if (auto locked = weak_ptr.lock()) {
            // Reconstruct a script_value from the locked object_holder
            // IMPORTANT: Reuse the same std::shared_ptr<object_holder> to maintain reference semantics
            script_value result(std::monostate{}, ctx.get_engine());

            // Preserve the original type info (including shared_ptr marker if present)
            auto weak_type_info = self.get_type_info();
            if (weak_type_info && weak_type_info->element_type()) {
                result.set_type_info(weak_type_info->element_type());
            } else {
                // Fallback: use the object type
                if (auto eng = ctx.get_engine()) {
                    result.set_type_info(eng->get_type_info_object(locked->type_id));
                }
            }

            // Directly assign the locked shared_ptr
            // Works for both regular objects and shared_ptr<T> since they use the same storage
            result.set_object_holder(locked);

            return result;
        } else {
            // weak_ptr is expired, return null
            return ctx.make_value();
        }
    }},

        {symbolizer->intern("expired"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "expired() takes no arguments");
        }

        if (!self.is_weak_ptr()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "expired() can only be called on weak_ptr");
        }

        auto weak_ptr = self.get_weak_ptr();
        return ctx.make_value(weak_ptr.expired());
    }},

        {symbolizer->intern("reset"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "reset() takes no arguments");
        }

        if (!self.is_weak_ptr()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "reset() can only be called on weak_ptr");
        }

        // Reset the weak_ptr to null
        auto& weak_storage = self.get_weak_ptr_storage();
        weak_storage.reset();

        return self; // Return the reset weak_ptr
    }},

        {symbolizer->intern("same_as"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        // Pointer identity comparison for weak_ptr: do they reference the same object?
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "same_as() takes exactly 1 argument");
        }

        const script_value& other = args[0];

        // Both expired -> same (both point to nothing)
        auto self_weak = self.get_weak_ptr();
        bool self_expired = self_weak.expired();

        // Handle other being weak_ptr
        if (other.is_weak_ptr()) {
            auto other_weak = other.get_weak_ptr();
            bool other_expired = other_weak.expired();

            // Both expired -> same
            if (self_expired && other_expired) {
                return ctx.make_value(true);
            }
            // One expired, one not -> not same
            if (self_expired || other_expired) {
                return ctx.make_value(false);
            }

            // Both valid - compare locked pointers
            auto self_locked = self_weak.lock();
            auto other_locked = other_weak.lock();
            return ctx.make_value(self_locked.get() == other_locked.get());
        }

        // Handle other being shared_ptr or null
        if (other.is_null()) {
            return ctx.make_value(self_expired);
        }

        if (!other.is_object()) {
            return ctx.make_value(false);
        }

        // Compare weak_ptr with shared_ptr/object
        if (self_expired) {
            return ctx.make_value(false);
        }

        auto self_locked = self_weak.lock();
        auto other_holder = const_cast<script_value&>(other).get_object_holder();
        return ctx.make_value(self_locked.get() == other_holder.get());
    }}
    };

    // Shared pointer methods
    out.shared_ptr_methods = {
        {symbolizer->intern("reset"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "reset() takes no arguments");
        }

        // In JaiScript, all objects are internally shared_ptr<object_holder>
        // Reset it to null while preserving the shared_ptr type
        auto current_type_info = self.get_type_info();
        self = ctx.make_value();
        self.set_type_info(current_type_info); // Preserve the shared_ptr<T> type

        return self; // Return the reset shared_ptr
    }},

        // RULED (2026-07, open question #9, option C): use_count() reports the OUTER
        // strong_ptr<object_holder> count — the layer script alias copies bump and the
        // layer script weak_ptr expiry follows — so `shared_ptr<T> a = T()` reports 1
        // and `var b = a;` makes it 2. The builtin's own call path holds transient
        // receiver copies (bound-method capture + this local), so the raw count is
        // aliases + K with K constant per backend (interp binds one fewer stack copy
        // than the vm); the offset below is pinned by the alias-ladder parity tests.
        // Today's inner std::shared_ptr count (C++-side shares) moved to cpp_ref_count().
        {symbolizer->intern("use_count"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "use_count() takes no arguments");
        }

        if (self.is_object()) {
            auto obj_holder = self.get_object_holder();
            if (obj_holder && obj_holder->data) {
                const long transient_k =
                    (ctx.engine_ref && ctx.engine_ref->get_backend_type() == backend_type::vm) ? 3 : 2;
                long count = static_cast<long>(obj_holder.use_count()) - transient_k;
                if (count < 1) count = 1;   // the receiver itself always counts
                return ctx.make_value(static_cast<script_int>(count));
            }
        }

        // Not a valid shared_ptr
        return ctx.make_value(static_cast<script_int>(0));
    }},

        // Preserves the pre-ruling use_count() number: how many C++-side owners share
        // the object (as<std::shared_ptr<T>>() / get_class_instance() extractions)
        {symbolizer->intern("cpp_ref_count"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "cpp_ref_count() takes no arguments");
        }

        if (self.is_object()) {
            auto obj_holder = self.get_object_holder();
            if (obj_holder && obj_holder->data) {
                long count = obj_holder->data.use_count();
                return ctx.make_value(static_cast<script_int>(count));
            }
        }

        return ctx.make_value(static_cast<script_int>(0));
    }},

        {symbolizer->intern("unique"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "unique() takes no arguments");
        }

        if (self.is_object()) {
            auto obj_holder = self.get_object_holder();
            if (obj_holder && obj_holder->data) {
                // unique = exactly one script handle (mirrors the ruled use_count layer)
                const long transient_k =
                    (ctx.engine_ref && ctx.engine_ref->get_backend_type() == backend_type::vm) ? 3 : 2;
                long count = static_cast<long>(obj_holder.use_count()) - transient_k;
                return ctx.make_value(count <= 1);
            }
        }

        // Not a valid shared_ptr
        return ctx.make_value(false);
    }},

        {symbolizer->intern("same_as"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        // Pointer identity comparison: are these the same underlying object?
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "same_as() takes exactly 1 argument");
        }

        const script_value& other = args[0];

        // Both null -> same
        if (self.is_null() && other.is_null()) {
            return ctx.make_value(true);
        }

        // One null, one not -> not same
        if (self.is_null() || other.is_null()) {
            return ctx.make_value(false);
        }

        // Both must be objects
        if (!self.is_object() || !other.is_object()) {
            return ctx.make_value(false);
        }

        auto self_holder = self.get_object_holder();
        auto other_holder = const_cast<script_value&>(other).get_object_holder();

        // Compare the underlying object_holder pointers (pointer identity)
        bool same = (self_holder.get() == other_holder.get());
        return ctx.make_value(same);
    }}
    };

    auto build_index = [symbolizer](const std::unordered_map<uint64_t, builtin_method>& methods,
                                    std::vector<const builtin_method*>& by_index,
                                    std::unordered_map<uint64_t, uint16_t>& index) {
        by_index.clear();
        index.clear();
        std::vector<std::pair<std::string_view, uint64_t>> names;
        names.reserve(methods.size());
        for (const auto& [id, fn] : methods) {
            names.emplace_back(symbolizer->get_string(id), id);
        }
        std::sort(names.begin(), names.end());
        by_index.reserve(names.size());
        for (const auto& [name, id] : names) {
            index.emplace(id, static_cast<uint16_t>(by_index.size()));
            by_index.push_back(&methods.at(id));
        }
    };
    detail::add_more_string_methods(out.string_methods, symbolizer);
    detail::add_binary_string_methods(out.string_methods, symbolizer);
    out.string_methods[symbolizer->intern("explode")] = out.string_methods[symbolizer->intern("split")];
    out.string_methods[symbolizer->intern("to_utf8")] = [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "to_utf8() takes no arguments");
        return script_value::make_utf8(self.unchecked_text(), ctx.get_engine());
    };
    out.utf8_methods = detail::make_utf8_methods(out.string_methods, symbolizer);
    out.utf8_methods.erase(symbolizer->intern("to_utf8"));
    build_index(out.array_methods, out.array_by_index, out.array_index);
    build_index(out.map_methods, out.map_by_index, out.map_index);
    build_index(out.string_methods, out.string_by_index, out.string_index);
    out.utf8_by_index.assign(out.string_by_index.size(), nullptr);   // aligned: a baked bi_string serves both kinds
    out.utf8_index.clear();
    for (const auto& [id, at] : out.string_index) {
        auto it = out.utf8_methods.find(id);
        if (it != out.utf8_methods.end()) { out.utf8_by_index[at] = &it->second; out.utf8_index.emplace(id, at); }
    }
    out.string_mutator_ids.clear();
    out.string_mutates_by_index.assign(out.string_by_index.size(), 0);
    for (std::string_view name : k_string_mutator_method_names) {
        const uint64_t id = symbolizer->intern(name);
        assert(out.string_methods.count(id) && "k_string_mutator_method_names names a builtin the registry lacks");
        out.string_mutator_ids.insert(id);
        out.string_mutates_by_index[out.string_index.at(id)] = 1;
    }
}

namespace detail {
void warn_shadowed_handle_builtins(engine& eng, const class_definition& def) {
    auto* sym = eng.get_symbolizer();
    if (!sym) { return; }
    // LOCAL methods only (has_method, not defines_method): warn where the shadow is
    // introduced, once per colliding method per class definition - subclasses that merely
    // inherit it don't re-warn.
    for (std::string_view name : k_shadowable_handle_method_names) {
        if (def.has_method(sym->intern(name))) {
            std::string m(name);
            eng.report_script_warning(
                "class method '" + m + "' shadows the builtin shared_ptr " + m +
                "() - the builtin is unreachable on " + def.get_name() +
                " instances; rename the class method to reach it");
        }
    }
}
} // namespace detail

void interpreter::init_builtin_methods() {
    builtin_method_registries registries;
    init_builtin_method_registries(string_symbolizer_, registries);
    array_methods_ = std::move(registries.array_methods);
    map_methods_ = std::move(registries.map_methods);
    string_methods_ = std::move(registries.string_methods);
    utf8_methods_ = std::move(registries.utf8_methods);
    string_mutator_ids_ = std::move(registries.string_mutator_ids);
    weak_ptr_methods_ = std::move(registries.weak_ptr_methods);
    shared_ptr_methods_ = std::move(registries.shared_ptr_methods);
}

// interpreter implementation

// The one file-resolution ladder: include, import, the static checker's closure gather and
// the resolve_path builtin all walk it. An absolute path stands as given; a relative one is
// tried against the folder of the file doing the asking (from_file), then each include path
// in order, then the working directory. The answer is the canonical spelling, which is the
// key the unit caches and executes under.
checked_result<std::string> resolve_include_path(const std::string& path, engine* engine_ptr, const std::string& from_file) {
    if (engine_ptr) {
        auto allowed = engine_ptr->require_privilege(privilege::filesystem);
        if (!allowed) { return allowed.error_value(); }
    }
    namespace fs = std::filesystem;
    auto found = [](const fs::path& candidate, std::string& out) {
        std::error_code ec;
        if (!fs::exists(candidate, ec) || ec) return false;
        fs::path canon = fs::canonical(candidate, ec);
        if (ec) { ec.clear(); canon = fs::absolute(candidate, ec); }
        out = (ec ? candidate : canon).string();
        return true;
    };
    std::string out;
    fs::path p(path);
    std::string key;
    if (engine_ptr) {
        key = engine_ptr->resolution_key(path, from_file);
        if (const std::string* remembered = engine_ptr->remembered_resolution(key)) { return *remembered; }
    }
    std::vector<std::string> misses;
    auto rung = [&](const fs::path& candidate) {
        if (found(candidate, out)) {
            if (engine_ptr) { engine_ptr->remember_resolution(key, std::move(misses), candidate.string(), out); }
            return true;
        }
        misses.push_back(candidate.string());
        return false;
    };
    if (p.is_absolute()) {
        if (rung(p)) return out;
    } else {
        if (!from_file.empty() && from_file != "<script>" && rung(fs::path(from_file).parent_path() / p)) return out;
        if (engine_ptr) {
            for (const auto& include_path : engine_ptr->get_include_paths()) {
                if (rung(fs::path(include_path) / p)) return out;
            }
        }
        if (rung(p)) return out;
    }
    return checked_result<std::string>(make_error_code(runtime_error_code::file_not_found), "Could not find include/import file");
}

// The statement forms ask from the file whose code is executing
checked_result<std::string> resolve_include_path(const std::string& path, engine* engine_ptr) {
    return resolve_include_path(path, engine_ptr, engine_ptr ? engine_ptr->current_source_file() : std::string());
}

// Note: Direct engine implementation access removed
// Import tracking will be handled by the engine's public API

// Helper to create a bound method - binds 'this' as the first argument
script_value interpreter::create_bound_method(const script_value& this_obj, const script_value& method) {
    return make_bound_method(this_obj, method);
}

bool interpreter::is_lvalue_expression(expression* e) const {
    return is_lvalue_shaped_expr(e);
}

script_value interpreter::member_target::method(uint64_t id) const {
    if (class_def) {
        return class_def->get_method(id, false);
    }
    return script_value::make_invalid(nullptr);
}

script_value interpreter::member_target::user_accessor(uint64_t id) const {
    if (class_def) {
        return class_def->find_user_accessor(id);
    }
    return script_value::make_invalid(nullptr);
}

bool interpreter::member_target::has_field(uint64_t id) const {
    return instance && instance->has_field(id);
}

const script_value& interpreter::member_target::get_field(uint64_t id) const {
    return instance->get_field(id);
}

std::string_view interpreter::member_target::class_name() const {
    if (class_def) {
        return class_def->get_name();
    }
    if (instance) {
        return instance->get_class_name();
    }
    static const std::string no_name;
    return no_name;
}

interpreter::member_target interpreter::resolve_member_target(const script_value& objectValue) const {
    member_target target;
    auto holder = objectValue.get_object_holder();
    if (!holder) {
        return target;
    }
    if (holder->is_class_instance_wrapper) {
        target.instance = std::static_pointer_cast<class_instance>(holder->data);
        if (target.instance) {
            target.class_def = target.instance->get_class_definition();
            // A plain wrapper of an extended object IS the object's layer (docs/script_extensible.md
            // S1): re-point the handle itself, so every copy of it - and every receiver site,
            // getters, setters, bound methods, the vm's caches - sees the layer from now on.
            if (target.class_def && !target.class_def->is_script_class() && target.class_def->extended_ever()) {
                if (auto hooks = target.class_def->slot_hooks()) {
                    auto layer = hooks->read(target.instance->cpp_object_pointer());
                    if (layer && layer != target.instance && layer->get_engine() == engine_) {
                        holder->data = layer;
                        target.instance = std::move(layer);
                        target.class_def = target.instance->get_class_definition();
                    }
                }
            }
        }
    } else if (engine_) {
        target.engine_def = holder->type_id != UINT64_MAX
            ? engine_->get_class_definition(holder->type_id)
            : engine_->get_class_definition(std::string(holder->type_name));
        target.class_def = target.engine_def.get();
    }
    return target;
}

// Helper for is_truthy() to check for to_bool() method on objects
bool interpreter::object_to_bool_via_method(const script_value& value) {
    // KEEP BYTE-PARALLEL with vm_backend::object_to_bool_via_method (class to_bool memo) and
    // the typed-slot table's bool row (detail::convert_typed_slot_script_rows_impl)
    if (const auto* h = value.get_storage().get_if<script_value::TYPEID_OBJECT>(); h && *h) {
        auto* oh = h->get();
        if (oh->is_class_instance_wrapper && oh->data) {
            if (auto* cd = static_cast<class_instance*>(oh->data.get())->get_class_definition();
                cd && (cd->is_script_class() || !cd->extended_ever()) && !cd->may_define_to_bool()) {
                return true;
            }
        }
    }
    auto target = resolve_member_target(value);
    if (!target) {
        return true;  // Not a class object - treat as truthy
    }

    auto method_id = string_symbolizer_->intern("to_bool");
    auto method_val = target.method(method_id);
    if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
        return true;  // No to_bool() method - objects are truthy by default
    }

    script_value bound = create_bound_method(value, method_val);
    const script_function& method = bound.as_function();
    std::vector<script_value> no_args;
    auto result = method(no_args);
    if (!result) {
        // truthiness has no error channel: the runtime error travels as the script exception its text would throw (a
        // terminal error keeps its latch) (KEEP BYTE-PARALLEL with vm_backend::object_to_bool_via_method)
        if (!is_unwinding_ && !limits_->terminal_error) {
            const std::string message = format_error(result, *string_symbolizer_);
            active_exception_value_ = make_value(message);
            current_exception_ = script_exception(message);
            is_unwinding_ = true;
        }
        return false;
    }
    if (result.value().is_bool()) {
        return result.value().unchecked_as_bool();
    }
    return true;  // Method didn't return a valid bool - treat as truthy
}

// Helper for handle_equal() to check for operator== or equals() method on objects
// Returns: nullopt if no custom equality method, true/false if method found and returned valid result
bool interpreter::object_defines_custom_equality(const script_value& v) const {
    const script_value& d = v.is_reference() ? v.deref() : v;
    if (!d.is_object()) { return false; }
    auto holder = const_cast<script_value&>(d).get_object_holder();
    if (!holder || !holder->is_class_instance_wrapper || !holder->data) { return false; }
    auto* cd = static_cast<class_instance*>(holder->data.get())->get_class_definition();
    return cd && cd->defines_method(eq_method_id_);
}

// The language's ==: a global operator== first, then the value's own (a class operator==, memberwise, numeric across kinds)
checked_result<bool> interpreter::language_equal(const script_value& left, const script_value& right) {
    const script_value& l = left.deref();
    const script_value& r = right.deref();
    if (operator_table_ && operator_table_->any()) {
        if (const script_value* opFunc = operator_table_->entry(detail::binary_op_slot(token_type::equal_equal))) {
            auto result = opFunc->as_function()({l, r});
            if (!result) { return result.error_value(); }
            return is_truthy(result.value());
        }
    }
    auto equal = handle_equal(l, r);
    if (!equal) { return equal.error_value(); }
    return is_truthy(equal.value());
}

std::optional<checked_result<bool>> interpreter::object_equality_via_method(const script_value& left, const script_value& right) {
    // Safe-mode workers never dispatch operator methods (increment B twin of the vm's
    // gate; handle_equal raises the verdict before the structural fallback diverges)
    if (parallel_worker_) [[unlikely]] { return std::nullopt; }
    auto target = resolve_member_target(left);
    if (!target) {
        return std::nullopt;
    }

    // Look for "==" method - used by both script classes (operator==) and dynamic_binder
    auto eq_id = string_symbolizer_->intern("==");
    auto method_val = target.method(eq_id);
    if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
        // Check if this is a transparent wrapper - if so, unwrap and retry
        if (target.class_def && target.class_def->is_transparent_wrapper()) {
            script_value mutable_left = left;  // Need mutable copy for unwrap
            script_value unwrapped = target.class_def->unwrap(mutable_left);
            if (!unwrapped.is_null()) {
                return object_equality_via_method(unwrapped, right);
            }
        }
        return std::nullopt;  // No == method - use default reference comparison
    }

    // Create a bound method and call it with the right operand
    script_value bound = create_bound_method(left, method_val);
    const script_function& method = bound.as_function();
    std::vector<script_value> args;
    args.push_back(right);  // Push by copy (const ref)

    auto result = method(args);
    // the method's runtime error is the comparison's (a script throw in flight keeps travelling on its own)
    if (!result && !is_unwinding_) { return checked_result<bool>(result.error_value()); }
    if (result.has_value() && result.value().is_bool()) {
        return checked_result<bool>(result.value().unchecked_as_bool());
    }
    // Method didn't return a valid bool - fall back to reference comparison
    return std::nullopt;
}

// Generic helper for comparison operators (<, <=, >, >=) via custom methods
std::optional<checked_result<bool>> interpreter::object_comparison_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id) {
    // Safe-mode workers: no operator dispatch (increment B; type-mismatch fallback is loud)
    if (parallel_worker_) [[unlikely]] { return std::nullopt; }
    auto target = resolve_member_target(left);
    if (!target) {
        return std::nullopt;
    }

    // Look for the operator method by symbol ID
    auto method_val = target.method(op_symbol_id);
    if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
        // Check if this is a transparent wrapper - if so, unwrap and retry
        if (target.class_def && target.class_def->is_transparent_wrapper()) {
            script_value mutable_left = left;  // Need mutable copy for unwrap
            script_value unwrapped = target.class_def->unwrap(mutable_left);
            if (!unwrapped.is_null()) {
                return object_comparison_via_method(unwrapped, right, op_symbol_id);
            }
        }
        return std::nullopt;  // No custom method - use default comparison
    }

    // Create a bound method and call it with the right operand
    script_value bound = create_bound_method(left, method_val);
    const script_function& method = bound.as_function();
    std::vector<script_value> args;
    args.push_back(right);

    auto result = method(args);
    if (!result && !is_unwinding_) { return checked_result<bool>(result.error_value()); }
    if (result.has_value() && result.value().is_bool()) {
        return checked_result<bool>(result.value().unchecked_as_bool());
    }
    return std::nullopt;
}

// Generic helper for arithmetic operators (+, -, *, /, %) via custom methods
std::optional<checked_result<script_value>> interpreter::object_arithmetic_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id) {
    // Safe-mode workers: no operator dispatch (increment B; type-mismatch fallback is loud)
    if (parallel_worker_) [[unlikely]] { return std::nullopt; }
    auto target = resolve_member_target(left);
    if (!target) {
        return std::nullopt;
    }

    // Look for the operator method by symbol ID
    auto method_val = target.method(op_symbol_id);
    if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
        // Check if this is a transparent wrapper - if so, unwrap and retry
        if (target.class_def && target.class_def->is_transparent_wrapper()) {
            script_value mutable_left = left;  // Need mutable copy for unwrap
            script_value unwrapped = target.class_def->unwrap(mutable_left);
            if (!unwrapped.is_null()) {
                return object_arithmetic_via_method(unwrapped, right, op_symbol_id);
            }
        }
        return std::nullopt;  // No custom method - use default arithmetic
    }

    // Create a bound method and call it with the right operand
    script_value bound = create_bound_method(left, method_val);
    const script_function& method = bound.as_function();
    std::vector<script_value> args;
    args.push_back(right);

    auto result = method(args);
    if (!result && !is_unwinding_) { return checked_result<script_value>(result.error_value()); }
    if (result.has_value()) {
        return checked_result<script_value>(std::move(result.value()));
    }
    return std::nullopt;
}

// A class's unary operator (Dev ruling 2026-09-30): the chain's overload taking no argument, called on the operand; nullopt
// when the class declares none (the builtin meaning and text follow). KEEP BYTE-PARALLEL with vm_backend::object_unary_via_method
std::optional<checked_result<script_value>> interpreter::object_unary_via_method(const script_value& operand, token_type op) {
    if (parallel_worker_) [[unlikely]] { return std::nullopt; }
    const uint64_t id = op == token_type::minus ? op_minus_id_ : op == token_type::plus ? op_plus_id_ : op_tilde_id_;
    auto target = resolve_member_target(operand);
    if (!target || !declares_unary_operator(target.class_def, id)) { return std::nullopt; }
    script_value method_val = target.method(id);
    if (!method_val.is_function()) { return std::nullopt; }
    script_value bound = create_bound_method(operand, method_val);
    auto result = bound.as_function()(std::vector<script_value>{});
    if (!result) {
        if (is_unwinding_) { return checked_result<script_value>(make_value()); }
        return checked_result<script_value>(result.error_value());
    }
    return checked_result<script_value>(std::move(result.value()));
}

// A callable object (Dev ruling 2026-09-30): only a call that would raise not callable asks for it, so ordinary calls never
// pay for it. KEEP BYTE-PARALLEL with vm_backend::call_operator_of
script_value interpreter::call_operator_of(const script_value& callee) {
    const script_value& v = callee.deref();
    if (parallel_worker_ || (v.type() != script_value_type::jai_object_type && v.type() != script_value_type::jai_shared_ptr_type)) { return make_value(); }
    auto target = resolve_member_target(v);
    if (!target) { return make_value(); }
    script_value method_val = target.method(op_call_id_);
    return method_val.is_function() ? create_bound_method(v, method_val) : make_value();
}

interpreter::interpreter()
    : ownedSymbolizer_(std::make_unique<string_symbolizer>()),
      string_symbolizer_(ownedSymbolizer_.get()),
      environment_(std::make_shared<environment>(string_symbolizer_)),
      hasReturnValue_(false),
      current_method_this_(std::monostate{}, nullptr) {
    env_symbolizer_ = string_symbolizer_;
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls
    call_stack_.reserve(128);    // Avoid reallocation during deep recursion (e.g., Fibonacci)

    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.emplace_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }

    // Initialize cached type IDs for fast object type comparison
    class_definition_type_id_ = string_symbolizer_->intern("class_definition");
    weak_ptr_holder_type_id_ = string_symbolizer_->intern("weak_ptr_holder");
    shared_ptr_holder_type_id_ = string_symbolizer_->intern("shared_ptr_holder");
    weak_from_this_id_ = string_symbolizer_->intern("weak_from_this");
    shared_from_this_id_ = string_symbolizer_->intern("shared_from_this");
    coroutine_handle_type_id_ = string_symbolizer_->intern("coroutine_handle");
    resume_id_ = string_symbolizer_->intern("resume");
    done_id_ = string_symbolizer_->intern("done");
    same_as_id_ = string_symbolizer_->intern("same_as");
    eq_method_id_ = string_symbolizer_->intern("==");
    base_type_ids_.intern(*string_symbolizer_);
    arity_count_ids_.intern(*string_symbolizer_);

    // Initialize cached operator symbol IDs for fast operator overload lookup
    op_plus_id_ = string_symbolizer_->intern("+");
    op_minus_id_ = string_symbolizer_->intern("-");
    op_star_id_ = string_symbolizer_->intern("*");
    op_slash_id_ = string_symbolizer_->intern("/");
    op_percent_id_ = string_symbolizer_->intern("%");
    op_less_id_ = string_symbolizer_->intern("<");
    op_less_equal_id_ = string_symbolizer_->intern("<=");
    op_greater_id_ = string_symbolizer_->intern(">");
    op_greater_equal_id_ = string_symbolizer_->intern(">=");
    op_equal_equal_id_ = string_symbolizer_->intern("==");
    op_bang_equal_id_ = string_symbolizer_->intern("!=");
    op_spaceship_id_ = string_symbolizer_->intern("<=>");
    op_ampersand_id_ = string_symbolizer_->intern("&");
    op_pipe_id_ = string_symbolizer_->intern("|");
    op_caret_id_ = string_symbolizer_->intern("^");
    op_left_shift_id_ = string_symbolizer_->intern("<<");
    op_right_shift_id_ = string_symbolizer_->intern(">>");
    op_tilde_id_ = string_symbolizer_->intern("~");
    op_call_id_ = string_symbolizer_->intern("()");
    subscript_op_id_ = string_symbolizer_->intern("[]");
    assign_operator_id_ = string_symbolizer_->intern("=");

	// Initialize cached keyword symbol IDs for fast keyword checks
	this_id_ = string_symbolizer_->intern("this");
	super_id_ = string_symbolizer_->intern("super");

	// Verify this_id_ matches symbolizer's cached ID
	if(this_id_ != string_symbolizer_->get_this_id()){
        throw std::runtime_error("this_id_ mismatch with symbolizer");
    }
	getValue_id_ = string_symbolizer_->intern("getValue");
	cpp_object_field_id_ = string_symbolizer_->intern(class_constants::CPP_OBJECT_FIELD);

    // Initialize built-in method registries with interned method names
    init_builtin_methods();

    // Initialize binary operator dispatch table
    init_dispatch_table();
}

interpreter::interpreter(string_symbolizer* external_symbolizer)
    : ownedSymbolizer_(nullptr),
      string_symbolizer_(external_symbolizer),
      environment_(std::make_shared<environment>(string_symbolizer_)),
      hasReturnValue_(false),
      current_method_this_(std::monostate{}, nullptr) {
    env_symbolizer_ = string_symbolizer_;
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls

    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.emplace_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }

    // Initialize cached type IDs for fast object type comparison
    class_definition_type_id_ = string_symbolizer_->intern("class_definition");
    weak_ptr_holder_type_id_ = string_symbolizer_->intern("weak_ptr_holder");
    shared_ptr_holder_type_id_ = string_symbolizer_->intern("shared_ptr_holder");
    weak_from_this_id_ = string_symbolizer_->intern("weak_from_this");
    shared_from_this_id_ = string_symbolizer_->intern("shared_from_this");
    coroutine_handle_type_id_ = string_symbolizer_->intern("coroutine_handle");
    resume_id_ = string_symbolizer_->intern("resume");
    done_id_ = string_symbolizer_->intern("done");
    same_as_id_ = string_symbolizer_->intern("same_as");
    eq_method_id_ = string_symbolizer_->intern("==");
    base_type_ids_.intern(*string_symbolizer_);
    arity_count_ids_.intern(*string_symbolizer_);

    // Initialize cached operator symbol IDs for fast operator overload lookup
    op_plus_id_ = string_symbolizer_->intern("+");
    op_minus_id_ = string_symbolizer_->intern("-");
    op_star_id_ = string_symbolizer_->intern("*");
    op_slash_id_ = string_symbolizer_->intern("/");
    op_percent_id_ = string_symbolizer_->intern("%");
    op_less_id_ = string_symbolizer_->intern("<");
    op_less_equal_id_ = string_symbolizer_->intern("<=");
    op_greater_id_ = string_symbolizer_->intern(">");
    op_greater_equal_id_ = string_symbolizer_->intern(">=");
    op_equal_equal_id_ = string_symbolizer_->intern("==");
    op_bang_equal_id_ = string_symbolizer_->intern("!=");
    op_spaceship_id_ = string_symbolizer_->intern("<=>");
    op_ampersand_id_ = string_symbolizer_->intern("&");
    op_pipe_id_ = string_symbolizer_->intern("|");
    op_caret_id_ = string_symbolizer_->intern("^");
    op_left_shift_id_ = string_symbolizer_->intern("<<");
    op_right_shift_id_ = string_symbolizer_->intern(">>");
    op_tilde_id_ = string_symbolizer_->intern("~");
    op_call_id_ = string_symbolizer_->intern("()");
    subscript_op_id_ = string_symbolizer_->intern("[]");
    assign_operator_id_ = string_symbolizer_->intern("=");

	// Initialize cached keyword symbol IDs for fast keyword checks
	this_id_ = string_symbolizer_->intern("this");
	super_id_ = string_symbolizer_->intern("super");

	// Verify this_id_ matches symbolizer's cached ID
	if (this_id_ != string_symbolizer_->get_this_id()) {
		throw std::runtime_error("this_id_ mismatch with symbolizer");
	}
	getValue_id_ = string_symbolizer_->intern("getValue");
	cpp_object_field_id_ = string_symbolizer_->intern(class_constants::CPP_OBJECT_FIELD);

    // Initialize built-in method registries with interned method names
    init_builtin_methods();

    // Initialize binary operator dispatch table
    init_dispatch_table();
}

interpreter::interpreter(string_symbolizer* external_symbolizer, std::shared_ptr<environment> global_env)
    : ownedSymbolizer_(nullptr),
      string_symbolizer_(external_symbolizer),
      environment_(global_env),
      hasReturnValue_(false),
      current_method_this_(std::monostate{}, nullptr) {
    env_symbolizer_ = string_symbolizer_;
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls

    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.emplace_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }

    // Initialize cached type IDs for fast object type comparison
    class_definition_type_id_ = string_symbolizer_->intern("class_definition");
    weak_ptr_holder_type_id_ = string_symbolizer_->intern("weak_ptr_holder");
    shared_ptr_holder_type_id_ = string_symbolizer_->intern("shared_ptr_holder");
    weak_from_this_id_ = string_symbolizer_->intern("weak_from_this");
    shared_from_this_id_ = string_symbolizer_->intern("shared_from_this");
    coroutine_handle_type_id_ = string_symbolizer_->intern("coroutine_handle");
    resume_id_ = string_symbolizer_->intern("resume");
    done_id_ = string_symbolizer_->intern("done");
    same_as_id_ = string_symbolizer_->intern("same_as");
    eq_method_id_ = string_symbolizer_->intern("==");
    base_type_ids_.intern(*string_symbolizer_);
    arity_count_ids_.intern(*string_symbolizer_);

    // Initialize cached operator symbol IDs for fast operator overload lookup
    op_plus_id_ = string_symbolizer_->intern("+");
    op_minus_id_ = string_symbolizer_->intern("-");
    op_star_id_ = string_symbolizer_->intern("*");
    op_slash_id_ = string_symbolizer_->intern("/");
    op_percent_id_ = string_symbolizer_->intern("%");
    op_less_id_ = string_symbolizer_->intern("<");
    op_less_equal_id_ = string_symbolizer_->intern("<=");
    op_greater_id_ = string_symbolizer_->intern(">");
    op_greater_equal_id_ = string_symbolizer_->intern(">=");
    op_equal_equal_id_ = string_symbolizer_->intern("==");
    op_bang_equal_id_ = string_symbolizer_->intern("!=");
    op_spaceship_id_ = string_symbolizer_->intern("<=>");
    op_ampersand_id_ = string_symbolizer_->intern("&");
    op_pipe_id_ = string_symbolizer_->intern("|");
    op_caret_id_ = string_symbolizer_->intern("^");
    op_left_shift_id_ = string_symbolizer_->intern("<<");
    op_right_shift_id_ = string_symbolizer_->intern(">>");
    op_tilde_id_ = string_symbolizer_->intern("~");
    op_call_id_ = string_symbolizer_->intern("()");
    subscript_op_id_ = string_symbolizer_->intern("[]");
    assign_operator_id_ = string_symbolizer_->intern("=");

    // Initialize built-in method registries with interned method names
    init_builtin_methods();

    // Initialize binary operator dispatch table
    init_dispatch_table();
}

void interpreter::add_globals(const std::unordered_map<std::string, script_value>& globals) {
    for (const auto& [name, value] : globals) {
        environment_->define(name, value);
    }
}

void interpreter::add_global(const std::string& name, const script_value& value) {
    environment_->define(name, value);
}

std::shared_ptr<environment> interpreter::get_global_environment() const {
    // Get the global environment directly from the engine
    // This avoids issues with closures/methods capturing stale environment references
    // from different execute() calls that don't chain to the same root
    if (auto eng = engine_) {
        return eng->get_global_environment();
    }
    // Fallback: walk up the parent chain (shouldn't happen if engine is alive)
    auto global_env = environment_;
    while (global_env && global_env->get_parent()) {
        global_env = global_env->get_parent();
    }
    return global_env ? global_env : environment_;
}

// Debug sync point: execute entry + every 1024 budget ticks, always on the script
// thread. Mirrors the transport-writable controller state into the plain per-statement
// gate (debug_hook_) and the controller's hot cache; a session that just ended gets a
// fresh budget deadline (the armed one is long past after any pause).
void interpreter::sync_debug_hook() {
    auto* dbg = debugger_.load(std::memory_order_acquire);
    debug::controller* next = (dbg && dbg->sync_hot_state()) ? dbg : nullptr;
    if (debug_hook_ && !next) { arm_execution_deadline(); }
    debug_hook_ = next;
}

void interpreter::begin_host_call() {
    if (engine_) { limits_ = &engine_->execution_limits(); }
    arm_execution_deadline();
    limits_->reset_for_execute();
}

void interpreter::prepare_for_execution() {
    sync_debug_hook();
    arm_execution_deadline();

    // Share the engine's limit state: reentrant executes see the outer run's terminal
    // latch, so a terminal error crosses the reentrant boundary uncaught
    if (auto eng = engine_) {
        limits_ = &eng->execution_limits();
    }

    // Clear exception state even when re-entered: the engine's catch path relies on
    // this scrub to strip a nested run's uncaught throw before the suspended outer
    // run resumes (vm backend ordering — clears before its reentrancy early-return).
    current_exception_.reset();
    is_unwinding_ = false; trace_captured_ = false;
    active_exception_value_.reset();  // No need to create a value here either
    current_catch_var_id_ = 0;

    // Reentrant execute (include/import or a host callback calling execute() mid-run):
    // the outer run's live state - value stack, return value, environment - must
    // survive; interpreter::execute isolates the nested program at top level
    // itself. Clearing here used to hand the outer frame the GLOBAL env to release
    // into the pool, which later recycled (and wiped) it. Mirrors the vm backend's
    // frames_-based reentrancy guard.
    if (is_executing()) {
        return;
    }

    // Terminal latch (and memory allowance) reset only at a NON-reentrant entry: a
    // nested execute must not grant the outer run's unwinding error a second life
    limits_->reset_for_execute();

    // Clear execution state
    valueStack_.clear();
    returnValue_.reset();  // No need to create a value - value_or() will create it if needed
    hasReturnValue_ = false;

    // Clear coroutine state — unless re-entered from inside a RUNNING coroutine
    // (include/import, or a host callback calling execute() mid-resume): clearing
    // then would sever the live coroutine's yield machinery and its next `yield`
    // would fail while the caller received the previous value as a bogus success.
    if (!active_coroutine_ || active_coroutine_->get_status() != coroutine_handle::status::running) {
        hasYieldRequest_ = false;
        active_coroutine_ = nullptr;
    }

    // Reset to the engine's global environment directly
    // This fixes issues where the interpreter's environment_ can become disconnected
    // from the engine's actual global (e.g., due to pooled environments or exception unwinding)
    if (execute_depth_ == 0) {
        instance_scope_saved_.clear();
        pending_instance_env_.reset();
    }
    auto eng_global = get_global_environment();
    if (eng_global) {
        environment_ = eng_global;
    } else {
        // Fallback: walk up the parent chain (shouldn't happen if engine is alive)
        while (environment_->get_parent()) {
            environment_ = environment_->get_parent();
        }
    }
    // Note: We don't clear the global environment, so variables persist between executions
}

void interpreter::push_scope() {
    environment_ = get_block_environment(environment_);
}

void interpreter::pop_scope() {
    if (environment_->get_parent()) {
        // Clear local values to trigger destructors before popping scope
        // This is crucial for script class destructors to run at scope exit
        auto current_env = environment_;
        environment_ = environment_->get_parent();
        release_environment(current_env);
    }
}

void interpreter::define_variable(const std::string& name, const script_value& value) {
    environment_->define(name, value);
}

void interpreter::push_instance_scope() {
    instance_scope_saved_.push_back(environment_);
    environment_ = get_pooled_environment(get_global_environment());
    pending_instance_env_ = environment_;
}

void interpreter::pop_instance_scope() {
    pending_instance_env_.reset();
    if (instance_scope_saved_.empty()) { pop_scope(); return; }
    auto instance_env = environment_;
    environment_ = std::move(instance_scope_saved_.back());
    instance_scope_saved_.pop_back();
    if (instance_env != environment_) { release_environment(instance_env); }
}

script_value interpreter::execute(const std::vector<declaration_ptr>& declarations, bool textual) {
    std::optional<detail::call_depth_guard> reentry_depth;
    checked_result<void> entry_result;
    if (is_executing()) {
        if (current_call_depth_ >= limits_->interpreter_call_depth_limit || detail::native_stack_low()) {
            entry_result = detail::raise_call_depth();
        } else {
            reentry_depth.emplace(current_call_depth_);
        }
    }

    // Reentrant execute (a host callback firing mid-script, e.g. hot reload): the
    // nested program must run at TOP LEVEL - global environment, fresh call/value
    // stacks - and restore the in-flight run's state afterwards. Without this,
    // top-level definitions land in the running frame's pooled env (and evaporate
    // with it), the outer run's value stack gets cleared, and the pool reset below
    // recycles environments the outer frames still hold.
    struct reentry_isolation {
        interpreter* self;
        bool reentrant;
        bool textual;
        std::shared_ptr<environment> saved_env;
        std::vector<call_frame> saved_call_stack;
        script_valueStack saved_values;
        std::optional<script_value> saved_return;
        bool saved_has_return = false;
        detail::member_scope_ctx saved_member_scope{};
        unit_stamp saved_run_unit{};
        const ast_node* saved_top_level_node = nullptr;

        reentry_isolation(interpreter* interp, bool textual_run) : self(interp), reentrant(interp->is_executing()), textual(textual_run) {
            saved_run_unit = self->run_unit_;
            self->run_unit_ = self->string_symbolizer_->current_unit();   // the run's stamp: the derived unit of its top-level code
            if (reentrant) {
                saved_top_level_node = self->top_level_node_;
                saved_env = self->environment_;
                saved_call_stack = std::move(self->call_stack_);
                self->call_stack_.clear();
                saved_values = std::move(self->valueStack_);
                self->valueStack_.clear();
                saved_return = std::move(self->returnValue_);
                saved_has_return = self->hasReturnValue_;
                if (!self->pending_instance_env_ && !textual) {   // an include runs where it is written
                    self->environment_ = self->get_global_environment();
                }
                saved_member_scope = self->member_scope_;
                if (!textual) { self->member_scope_ = {}; }
            }
            // Counted, not a flag (Dev ruling): a nested execute's exit decrements
            // symmetrically and can never clear the outer run's in-flight state
            self->pending_instance_env_.reset();
            ++self->execute_depth_;
        }
        ~reentry_isolation() {
            self->run_unit_ = saved_run_unit;
            if (reentrant) {
                self->top_level_node_ = saved_top_level_node;
                self->environment_ = std::move(saved_env);
                self->call_stack_ = std::move(saved_call_stack);
                self->valueStack_ = std::move(saved_values);
                self->returnValue_ = std::move(saved_return);
                self->hasReturnValue_ = saved_has_return;
                self->member_scope_ = saved_member_scope;
            }
            --self->execute_depth_;
        }
    } isolation(this, textual);

    script_value last_script_value = make_value();
    hasReturnValue_ = false;  // Reset return value state
    captured_trace_.clear();
    trace_captured_ = false;
    top_level_node_ = nullptr;

    // A denied entry runs only the existing error boundary, even for an empty
    // program; no declaration is dispatched and its exception text stays identical.
    const size_t execution_count = entry_result ? declarations.size() : 1;
    for (size_t i = 0; i < execution_count; i++) {
        auto* decl = entry_result ? declarations[i].get() : nullptr;
        // std::cerr << "  Declaration " << i << " type: " << typeid(*decl).name() << "\n";

        // Execute declaration with exception handling
        run_error_.reset();
        try {
            // std::cerr << "  About to visit declaration " << i << "\n";
            auto result = entry_result ? dispatch_decl(decl) : entry_result;
            if (!result) [[unlikely]] {
                run_error_ = result.error_value();
                // Convert error code to exception at boundary
                // Include the custom error message if available, formatted with symbol resolution
                if (!result.message().empty()) {
                    auto formatted = format_error_message(result.message(),
                        string_symbolizer_->get_string(result.symbol_id()),
                        string_symbolizer_->get_string(result.symbol_id2()));
                    throw std::system_error(result.error(), formatted);
                } else {
                    throw std::system_error(result.error());
                }
            }
            // std::cerr << "  Finished visiting declaration " << i << "\n";
        } catch (const script_exception& e) {
            // Convert to interpreter exception state
            run_error_.reset();
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = e;
            is_unwinding_ = true;
        } catch (const std::runtime_error& e) {
            // Convert runtime errors to script exceptions
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = script_exception(e.what());
            is_unwinding_ = true;
        }

        // Check if we're unwinding due to an uncaught exception
        if (is_unwinding_) {
            if (!trace_captured_) capture_stack_trace();
            // Stop executing further declarations
            break;
        }
        
        // Check if this is an implicit return expression
        if (decl->get_type() == node_type::expression_decl) {
            auto* expr_decl = static_cast<expression_decl*>(decl);
            if (expr_decl->implicit_return && !valueStack_.empty()) {
                last_script_value = pop_value();
                // Dereference in case it's a reference (for expressions like m["key"] that return references)
                last_script_value = last_script_value.deref();
            }
        }
        
        // Clear any remaining values on the stack (from non-implicit expressions)
        while (!valueStack_.empty()) {
            pop_value();
        }
        
        // If we hit a return statement, break out of execution
        if (hasReturnValue_) {
            if (!isolation.reentrant) {
                reset_environment_pool();  // Reset pool for next execution
            }
            return returnValue_.value();
        }
    }



    if (!isolation.reentrant) {
        reset_environment_pool();  // Reset pool for next execution
    }
    return last_script_value;
}

script_value interpreter::evaluate(expression_ptr expr) {
    auto result = dispatch_expr(expr.get());
    if (!result) {
        // Return null on error
        return make_value();
    }
    return pop_value();
}

// Variable access methods
script_value interpreter::get_variable(const std::string& name) const {
    auto result = environment_->get(name);
    if (!result) {
        if (!result.message().empty()) {
            auto formatted = format_error_message(result.message(),
                string_symbolizer_->get_string(result.symbol_id()),
                string_symbolizer_->get_string(result.symbol_id2()));
            throw runtime_error(formatted);
        }
        throw runtime_error(result.error().message());
    }
    return result.value().deref();
}

bool interpreter::has_variable(const std::string& name) const {
    return environment_->contains(name);
}

std::unordered_map<std::string_view, script_value> interpreter::get_all_variables() const {
    // Since we should be at root scope after execution, just return local variables
    return environment_->get_local_variables();
}


// expression visitors
checked_result<void> interpreter::visit_literal_expr(literal_expr* expr) {
    // Literals are created at parse time without engine references and type_info set to nullptr
    // Extract raw values from storage and recreate with proper type_info
    // We can't use as_int()/as_string() etc because they check type() which returns jai_null_type for AST literals

    // Access the raw storage variant directly
    const auto& storage = expr->value.get_storage();

    // Determine type from variant index and extract + recreate value
    switch (storage.index()) {
        case 1:  // script_int
            push_value(make_value(storage.get<script_int>()));
            break;
        case 2:  // script_float
            push_value(make_value(storage.get<script_float>()));
            break;
        case 3:  // script_string (wrapped in strong_ptr)
            push_value(literal_string_value(storage.get<strong_ptr<script_string>>()));
            break;
        case 4:  // script_char
            push_value(make_value(storage.get<script_char>()));
            break;
        case 5:  // script_bool
            push_value(make_value(storage.get<script_bool>()));
            break;
        case 0:  // std::monostate (null)
            push_value(make_value());
            break;
        default:
            // For other types, try to set engine ref (though this shouldn't happen with literals)
            expr->value.set_engine(engine_);
            push_value(expr->value);
            break;
    }
    return checked_result<void>();
}

checked_result<void> interpreter::visit_identifier_expr(identifier_expr* expr) {
    // ::name - GLOBAL env only (locals, captures, member scope all skipped).
    // KEEP BYTE-PARALLEL with vm exec_load's global-only door.
    if (expr->global_qualified) {
        if (script_value* g = get_global_environment()->get_value_ptr(expr->symbol_id)) {
            push_value(g->deref());
            return checked_result<void>();
        }
        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
            environment_->undefined_text(expr->symbol_id), expr->symbol_id);
    }
    if (expr->symbol_id == getValue_id_) {
    }

    // ============================================================
    // SLOT-BASED LOCAL ACCESS: O(1) array indexing
    // ============================================================
    // Parser assigns slot indices to local variables and parameters.
    // This avoids hash map lookup overhead for the most common variable accesses.
    if (expr->slot_index != SIZE_MAX && !call_stack_.empty()) {
        script_value* local = call_stack_.back().get_local(expr->slot_index);
        if (local) {
            push_value(local->deref());  // Automatically handles references
            return checked_result<void>();
        }
    }

    // Special handling for type constructors like weak_ptr<T>, shared_ptr<T>.
    // This is reached on every non-local (global/function/member) identifier access,
    // so gate the two prefix scans on the first character: any name not starting
    // with 'w'/'s' (the overwhelming majority) can't match and skips both find()s.
    if (!expr->name.empty() && (expr->name.front() == 'w' || expr->name.front() == 's') &&
        (expr->name.find("weak_ptr<") == 0 || expr->name.find("shared_ptr<") == 0)) {
        // This is a type constructor being used as a function
        // Extract the base type name (weak_ptr or shared_ptr)
        size_t pos = expr->name.find('<');
        std::string base_type(expr->name.substr(0, pos));
        
        // Look up the constructor function for this type (nullable-pointer probe,
        // checked_result stage 1: same ladder as get(), null miss - KEEP BYTE-PARALLEL
        // with vm_backend::exec_load)
        if (script_value* ctor = environment_->get_value_ptr(string_symbolizer_->intern(base_type));
            ctor && ctor->is_function()) {
            push_value(*ctor);
            return checked_result<void>();
        }
        // Fall through to normal error handling if not found
    }
    
    // Member scope (detail/member_scope.hpp): C++ unqualified-lookup order - the
    // function's own scopes, then the class's members, then the definition/global
    // chain. Field/static pushes copy WITHOUT deref (the old fallback's exact shape);
    // method hits mint the same bound method the old post-walk fallback minted.
    if (member_scope_.active()) {
        if (script_value* own = detail::own_scope_lookup(environment_.get(), member_scope_.floor, expr->symbol_id)) {
            push_value(own->deref());
            return checked_result<void>();
        }
        // Proven this-field read (docs/type_proofs_design.md I-I1): class object + shape
        // epoch + present cell -> the cell copy the probe's field rung pushes; any miss
        // runs the probe (which materializes defaults) unchanged
        if (expr->proven_class && !parallel_worker_ &&
            member_scope_.state == detail::member_scope_state::instance && member_scope_.this_storage) {
            if (script_value* cell = detail::proven_field_cell(*member_scope_.this_storage, expr->proven_class,
                                                               expr->proven_epoch, expr->proven_slot, nullptr)) {
                push_value(*cell);
                return checked_result<void>();
            }
        }
        auto member = detail::probe_member_scope(member_scope_.state, member_scope_.this_storage,
                                                 member_scope_.statics, expr->symbol_id, /*want_methods*/ true);
        if (member) {
            JAISCRIPT_TRY(detail::bare_member_access(member, expr->symbol_id, environment_->find_access_context()));
            if (member.what == detail::member_probe_hit::kind::method) {
                if (member.instance && member_scope_.this_storage) {
                    if (!identifier_callee_lookup_) { return bare_method_value_error(expr, *member_scope_.this_storage); }
                    push_value(create_bound_method(*member_scope_.this_storage, member.method_value));
                } else {
                    push_value(std::move(member.method_value));   // static method: unbound callable
                }
            } else if (member.what == detail::member_probe_hit::kind::cpp_property) {
                auto read = detail::read_bare_cpp_property(*member_scope_.this_storage, member.instance,
                                                           member.method_value, member.getter_id, expr->symbol_id);
                if (!read) {
                    return read.error_value();
                }
                push_value(std::move(read.value()));
            } else {
                push_value(*member.value_ptr);
            }
            return checked_result<void>();
        }
        if (script_value* beyond = member_scope_.floor ? member_scope_.floor->get_value_ptr(expr->symbol_id)
                                                       : environment_->get_value_ptr(expr->symbol_id)) {
            push_value(beyond->deref());
            return checked_result<void>();
        }
        if (current_class_context_ && current_class_context_->in_method) {
            current_class_context_->unresolved_identifiers.insert(expr->symbol_id);
            push_value(make_value());
            return checked_result<void>();
        }
        uint64_t scoped_name_id = string_symbolizer_->intern(expr->name);
        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
            "Undefined variable '{0}'", scoped_name_id);
    }

    // Use parser's pre-computed symbol ID (always set by parser)
    // With lazy caching, environment_ will cache lookups automatically.
    // get_value_ptr IS get_ref's exact resolution ladder minus the error wrapper.
    if (script_value* env_val = environment_->get_value_ptr(expr->symbol_id)) {
        push_value(env_val->deref());  // Automatically handles references
    } else {
        // If we're in a class method context, collect unresolved identifier
        if (current_class_context_ && current_class_context_->in_method) {
            // Add to unresolved identifiers for later validation (using pre-computed ID)
            current_class_context_->unresolved_identifiers.insert(expr->symbol_id);
            // Push a placeholder value to continue parsing
            push_value(make_value());
            return checked_result<void>();
        }


        // Variable not found - check if it's a member of 'this' (pointer probe: no
        // per-probe copy of the live this value)
        if (script_value* this_ptr = environment_->get_value_ptr(string_symbolizer_->get_this_id())) {
            script_value& this_val = *this_ptr;
            if (this_val.is_object()) {
                // Try to access as a member of 'this'
                std::shared_ptr<class_instance> instance = this_val.get_class_instance();

                if (instance) {
                    // Intern the name to ID
                    uint64_t name_id = string_symbolizer_->intern(expr->name);

                    // A hybrid's C++ property reads through its getter (never a placeholder cell)
                    if (uint64_t getter_id = detail::cpp_property_getter_id(instance.get(), name_id)) {
                        auto read = detail::read_bare_cpp_property(this_val, instance.get(),
                                                                   instance->get_method(getter_id, false), getter_id, name_id);
                        if (!read) {
                            return read.error_value();
                        }
                        push_value(std::move(read.value()));
                        return checked_result<void>();
                    }
                    // Check instance fields first
                    if (instance->has_field(name_id)) {
                        push_value(instance->get_field(name_id));
                        return checked_result<void>();
                    }

                    // Check for methods (returns bound method)
                    script_value method = instance->get_method(name_id, false);
                    if (!method.is_invalid()) {
                        if (!identifier_callee_lookup_) { return bare_method_value_error(expr, this_val); }
                        script_value bound_method = create_bound_method(this_val, method);
                        push_value(std::move(bound_method));
                        return checked_result<void>();
                    }

                    // Check for static fields of the class
                    auto class_def = instance->get_class_definition();
                    if (class_def && class_def->has_static_field(expr->symbol_id)) {
                        push_value(class_def->get_static_field(expr->symbol_id));
                        return checked_result<void>();
                    }
                }
            }
        }

        // Use error code instead of exception state - include variable name for debugging
        uint64_t name_id = string_symbolizer_->intern(expr->name);
        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
            environment_->undefined_text(name_id), name_id);
    }
    return checked_result<void>();
}

// Gate on STORAGE, not declared type: a typed-but-uninitialized variable (`int x;`) holds monostate
checked_result<bool> interpreter::scalar_lane(token_type op, const script_value& left, const script_value& right, bool concat) {
    if (!detail::scalar_arith_op(op) && !detail::scalar_compare_op(op)) { return false; }
    const size_t li = left.raw_storage_index();
    const size_t ri = right.raw_storage_index();
    if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
        const script_int a = left.unchecked_as_int(), b = right.unchecked_as_int();
        if (detail::scalar_compare_op(op)) {
            push_value(make_bool_fast(detail::scalar_compare(op, a, b)));
            return true;
        }
        script_int out;
        if (const auto fault = detail::int_arith(op, a, b, out); fault != detail::scalar_fault::none) [[unlikely]] {
            return detail::scalar_fault_error<checked_result<bool>>(op, fault);
        }
        push_value(make_int_fast(out));
        return true;
    }
    if (detail::numeric_storage(li) && detail::numeric_storage(ri)) {
        const script_float a = detail::as_script_float(left, li), b = detail::as_script_float(right, ri);
        if (detail::scalar_compare_op(op)) {
            push_value(make_bool_fast(detail::scalar_compare(op, a, b)));
            return true;
        }
        script_float out;
        if (const auto fault = detail::float_arith(op, a, b, out); fault != detail::scalar_fault::none) [[unlikely]] {
            return detail::scalar_fault_error<checked_result<bool>>(op, fault);
        }
        push_value(make_float_fast(out));
        return true;
    }
    if (concat && op == token_type::plus && li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
        const script_string& left_text = left.unchecked_as_string();
        const script_string& right_text = right.unchecked_as_string();
        // engine::memory_cap chokepoint: deny the concat result before it exists
        if (!limits_->memory_charge(sizeof(script_value) + left_text.size() + right_text.size())) [[unlikely]] {
            return detail::raise_memory_cap(*limits_);
        }
        push_value(make_value(left_text + right_text));
        return true;
    }
    // a cpp-bound operand (index 14) skips the gates above: its zero divisor keeps this lane's error text
    if (li == script_value::TYPEID_CPP_BOUND || ri == script_value::TYPEID_CPP_BOUND) {
        if (auto zero = detail::bound_fastpath_zero_divisor(op, left, right, true, true)) {
            return checked_result<bool>(make_error_code(zero->code), zero->text);
        }
    }
    return false;
}

template<bool Replaying>
checked_result<void> interpreter::visit_binary_expr_t(binary_expr* expr, const replay_point* rs) {
    // Track if we've pre-fetched values to avoid duplicate lookups
    bool already_have_values = false;
    std::optional<script_value> pre_fetched_left, pre_fetched_right;
    // Replay: an operand evaluated before the yield is parked (left as partial[0]); the
    // identifier/identifier fast head cannot hold a yield, so replay skips it
    if constexpr (Replaying) {
        if (rs && rs->index == 1) { pre_fetched_left = rs->partial[0]; }
    }

	// FAST PATH: identifier + identifier (e.g., "a + b", "sum + i") - eliminates 2 virtual calls
	// Skip logical operators - they need short-circuit evaluation
	if (!Replaying && expr->op.type != token_type::ampersand_ampersand && expr->op.type != token_type::pipe_pipe) {
		if (expr->left->get_type() == node_type::identifier_expr) {
			auto* leftId = static_cast<identifier_expr*>(expr->left.get());
			if (expr->right->get_type() == node_type::identifier_expr) {
				auto* rightId = static_cast<identifier_expr*>(expr->right.get());
				// Both operands are simple identifiers - direct variable lookup without AST
				// traversal (a null resolution - undefined name, member-method or C++-property
				// shadow - takes the generic path below, which raises the same texts)
				script_value* leftPtr = resolve_local_or_env(leftId->slot_index, leftId->symbol_id, leftId->global_qualified);
				script_value* rightPtr = leftPtr ? resolve_local_or_env(rightId->slot_index, rightId->symbol_id, rightId->global_qualified) : nullptr;
				if (leftPtr && rightPtr) {
					const script_value& leftVal = leftPtr->deref();
					const script_value& rightVal = rightPtr->deref();

					// Fast path for integer arithmetic (most common in loops)
					if (can_use_fast_path(expr->op.type)) {
						auto lane = scalar_lane(expr->op.type, leftVal, rightVal, true);
						if (!lane) { return lane.error_value(); }
						if (lane.value()) { return {}; }
					}

					// Fast path didn't handle it - copy values for fallback dispatch
					pre_fetched_left = leftVal;
					pre_fetched_right = rightVal;
					already_have_values = true;
				}
			}
			// FAST PATH 2: identifier + literal (e.g., "i < 100", "x + 5") - most common loop condition!
			else if (expr->right->get_type() == node_type::literal_expr) {
				auto* rightLit = static_cast<literal_expr*>(expr->right.get());
				if (script_value* leftPtr = resolve_local_or_env(leftId->slot_index, leftId->symbol_id, leftId->global_qualified)) {
					const script_value& leftVal = leftPtr->deref();
					const script_value& rightVal = rightLit->value;  // Direct access - no lookup!

					if (can_use_fast_path(expr->op.type)) {
						auto lane = scalar_lane(expr->op.type, leftVal, rightVal, false);
						if (!lane) { return lane.error_value(); }
						if (lane.value()) { return {}; }
					}
					// Fast path didn't fully handle - copy for slow path (literal needs hydration)
					pre_fetched_left = leftVal;
					pre_fetched_right = rightVal.has_valid_engine() ? rightVal : hydrate_literal(rightVal);
					already_have_values = true;
				}
			}
		}
		// FAST PATH 3: literal + identifier (e.g., "100 > i", "5 + x")
		else if (expr->left->get_type() == node_type::literal_expr) {
			auto* leftLit = static_cast<literal_expr*>(expr->left.get());
			if (expr->right->get_type() == node_type::identifier_expr) {
				auto* rightId = static_cast<identifier_expr*>(expr->right.get());
				const script_value& leftVal = leftLit->value;  // Direct access!
				if (script_value* rightPtr = resolve_local_or_env(rightId->slot_index, rightId->symbol_id, rightId->global_qualified)) {
					script_value rightVal = rightPtr->deref();

					if (can_use_fast_path(expr->op.type)) {
						auto lane = scalar_lane(expr->op.type, leftVal, rightVal, false);
						if (!lane) { return lane.error_value(); }
						if (lane.value()) { return {}; }
					}
					// Fast path didn't fully handle - save for slow path (literal needs hydration)
					pre_fetched_left = leftVal.has_valid_engine() ? leftVal : hydrate_literal(leftVal);
					pre_fetched_right = std::move(rightVal);
					already_have_values = true;
				}
			}
		}
	}

    // Handle logical operators specially for short-circuit evaluation
    // Return proper boolean values (not JavaScript-style operand values)
    if (expr->op.type == token_type::ampersand_ampersand || expr->op.type == token_type::pipe_pipe) {
        if (!parked<Replaying>(rs, 0)) {
            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->left.get(), rs, 0), expr, rp_operand, 0, {});
            if (is_unwinding_) { return {}; }   // the left operand raised: no short-circuit decision, no right operand
        }
        script_value left = parked<Replaying>(rs, 0) ? *pre_fetched_left : pop_value();

        bool leftTruthy = is_truthy(left);

        if (expr->op.type == token_type::ampersand_ampersand) {
            if (!leftTruthy) {
                push_value(make_value(false));  // Short-circuit: left is falsy, return false
                return {};
            }
            // Left is truthy, evaluate right and return its boolean value
            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->right.get(), rs, 1), expr, rp_operand, 1, (std::vector<script_value>{left}));
            if (is_unwinding_) { return {}; }
            script_value right = pop_value();
            push_value(make_value(is_truthy(right)));
            return {};
        } else { // pipe_pipe
            if (leftTruthy) {
                push_value(make_value(true));  // Short-circuit: left is truthy, return true
                return {};
            }
            // Left is falsy, evaluate right and return its boolean value
            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->right.get(), rs, 1), expr, rp_operand, 1, (std::vector<script_value>{left}));
            if (is_unwinding_) { return {}; }
            script_value right = pop_value();
            push_value(make_value(is_truthy(right)));
            return {};
        }
    }

    // Capture whether THIS binary expression is the outermost assignment-target
    // subscript, then clear the flag so any nested subscripts evaluated while
    // computing the operands below are treated as reads (no map auto-insert).
    const bool want_lvalue_write = lvalue_target_context_;
    const bool want_string_target = want_lvalue_write || string_target_context_;
    lvalue_target_context_ = false;
    string_target_context_ = false;

    // Evaluate operands once and use them throughout (or use pre-fetched values)
    std::optional<script_value> left_raw_opt, left_opt, right_opt;

    if (already_have_values) {
        // Use pre-fetched values from fast path (already dereferenced)
        left_opt = std::move(*pre_fetched_left);
        left_raw_opt = *left_opt;  // Already dereferenced
        right_opt = std::move(*pre_fetched_right);
    } else {
        // Normal path: evaluate via AST traversal
        if (parked<Replaying>(rs, 0)) {
            left_raw_opt = *pre_fetched_left;   // evaluated before the yield, never re-run
        } else {
            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->left.get(), rs, 0), expr, rp_operand, 0, {});
            if (is_unwinding_) { return {}; }   // the left operand raised: the right one never runs (the vm's order)
            left_raw_opt = pop_value();  // Keep raw value for subscript handling
        }
        left_opt = left_raw_opt->deref();  // Dereferenced version for most operations

        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->right.get(), rs, 1), expr, rp_operand, 1, (std::vector<script_value>{*left_raw_opt}));
        // Check if we're unwinding due to an exception in the right expression
        if (is_unwinding_) {
            // Don't try to pop a value that wasn't pushed due to the exception
            return {};
        }
        right_opt = pop_value().deref();  // Handle references safely
    }

    // Extract values from optional (guaranteed to have values at this point)
    script_value& left_raw = *left_raw_opt;
    script_value& left = *left_opt;
    script_value& right = *right_opt;
    
    // Check for custom operator functions first using cached symbol IDs (eliminates string construction)
    uint64_t op_symbol_id = 0;
    switch (expr->op.type) {
        case token_type::plus: op_symbol_id = op_plus_id_; break;
        case token_type::minus: op_symbol_id = op_minus_id_; break;
        case token_type::star: op_symbol_id = op_star_id_; break;
        case token_type::slash: op_symbol_id = op_slash_id_; break;
        case token_type::percent: op_symbol_id = op_percent_id_; break;
        case token_type::less: op_symbol_id = op_less_id_; break;
        case token_type::less_equal: op_symbol_id = op_less_equal_id_; break;
        case token_type::greater: op_symbol_id = op_greater_id_; break;
        case token_type::greater_equal: op_symbol_id = op_greater_equal_id_; break;
        case token_type::equal_equal: op_symbol_id = op_equal_equal_id_; break;
        case token_type::bang_equal: op_symbol_id = op_bang_equal_id_; break;
        case token_type::spaceship: op_symbol_id = op_spaceship_id_; break;
        case token_type::ampersand: op_symbol_id = op_ampersand_id_; break;
        case token_type::pipe: op_symbol_id = op_pipe_id_; break;
        case token_type::caret: op_symbol_id = op_caret_id_; break;
        case token_type::left_shift: op_symbol_id = op_left_shift_id_; break;
        case token_type::right_shift: op_symbol_id = op_right_shift_id_; break;
        default: break;
    }

    // Check for custom operator function (excluding subscript) through the engine's
    // flat operator table (detail/operator_table.hpp): one mask test + array read,
    // no environment probe on any operator path.
    if (op_symbol_id != 0 && operator_table_ && operator_table_->any()) {
        if (const script_value* opFunc = operator_table_->entry(detail::binary_op_slot(expr->op.type))) {
            const script_function& func = opFunc->as_function();
            std::vector<script_value> args = {left, right};
            auto result = func(args);
            if (!result) {
                // Function returned error - propagate it up
                return result.error_value();
            }
            push_value(std::move(result.value()));
            return {};
        }
    }

    // Handle subscript operation specially
    if (expr->op.type == token_type::left_bracket) {
        if (left.raw_storage_index() == script_value::TYPEID_PARALLEL_BORROW) {
            // Parallel captured-read borrow: element reads go through the shared kernel
            // (detail/parallel_transform.hpp - parity with the vm twin by construction);
            // writes hit the runtime write wall inside it. No element reference is ever
            // minted into a borrowed container.
            auto elem = detail::parallel_borrow_subscript_read(left, right, engine_, want_lvalue_write);
            if (!elem) { return checked_result<void>(elem.error(), elem.static_message()); }
            push_value(std::move(elem).value());
            return {};
        }
        if (left.is_array()) {
            if (!right.is_int()) {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_index_type), "Array index must be an integer");
            }
            script_int index = right.unchecked_as_int();
            const script_array* node = left.unchecked_array_node();

            if (index < 0 || index >= static_cast<script_int>(node->size())) {
                // Numbers intern as symbols: the {0}/{1} machinery resolves symbol ids,
                // so raw counts printed garbage names ("Array index bool out of bounds
                // for array of size class_definition")
                return checked_result<void>(make_error_code(runtime_error_code::index_out_of_bounds),
                    "Array index {0} out of bounds for array of size {1}",
                    string_symbolizer_->intern(std::to_string(index)),
                    string_symbolizer_->intern(std::to_string(node->size())));
            }

            // Check if the left side is an lvalue (variable, member access, or subscript)
            // These should allow modification, even if use_count == 1
            bool is_lvalue = expr->left->get_type() == node_type::identifier_expr ||
                            expr->left->get_type() == node_type::member_expr ||
                            (expr->left->get_type() == node_type::binary_expr &&
                             static_cast<binary_expr*>(expr->left.get())->op.type == token_type::left_bracket);

            // Transient read (transient_read.hpp): the consumer provably discards
            // identity, so skip the reference mint and push a shallow element copy.
            // Overrides re-enable the mint at runtime - operand copies would be
            // observable via use_count inside them. (KEEP BYTE-PARALLEL with
            // vm_backend::exec_index)
            const bool transient_read = expr->transient_element_read && !want_lvalue_write && !has_custom_binary_ops_;

            if (is_lvalue && !transient_read) {
                // Reallocation-safe reference to the element (container+index, not a raw
                // pointer into the vector buffer): holding `arr[i]` as an lvalue across a
                // push that reallocates would otherwise dangle -> heap corruption (#41).
                auto array_type_info = left.get_type_info();
                type_info_ptr element_type = array_type_info ? array_type_info->element_type() : nullptr;
                script_value ref_value = script_value::make_element_reference(
                    left.get_array_storage(), static_cast<size_t>(index), engine_, element_type);
                push_value(std::move(ref_value));
            } else if (node->is_typed()) {
                push_value(node->get(static_cast<size_t>(index), engine_));   // raw buffer read
            } else {
                // True temporary (e.g., function return), read-only access
                push_value(node->values()[index]);
            }
        } else if (left.is_map()) {
            // For maps, we need to handle both assignment and read access
            // Try to get a mutable reference if possible
            try {
                auto& map = const_cast<script_map&>(left.as_map());

                // Check if the left side is an lvalue (variable, member access, or subscript)
                bool is_lvalue = expr->left->get_type() == node_type::identifier_expr ||
                                expr->left->get_type() == node_type::member_expr ||
                                (expr->left->get_type() == node_type::binary_expr &&
                                 static_cast<binary_expr*>(expr->left.get())->op.type == token_type::left_bracket);

                // Transient map read: copy branch below (never inserts, never mints)
                const bool transient_read = expr->transient_element_read && !want_lvalue_write && !has_custom_binary_ops_;

                if (is_lvalue && want_lvalue_write) {
                    // Assignment target (m[k] = v): auto-insert a slot so the
                    // assignment has somewhere to write through. The key is COPIED into
                    // the map; an engine-less key (a raw AST literal reaching here via the
                    // binary fast path) would poison the whole map for clone().
                    script_value key = right;
                    if (!key.has_valid_engine()) {
                        key.set_engine(left.has_valid_engine() ? left.get_engine() : engine_);
                    }
                    // Declared key type: keys never coerce (Dev ruling 2026-09-02), so the
                    // check runs BEFORE the auto-insert (KEEP BYTE-PARALLEL with the vm)
                    if (auto map_tag = left.get_type_info()) {
                        if (type_info_ptr key_type = map_tag->key_type();
                            key_type && key_type->base_type != script_value_type::jai_any_type &&
                            key.deref().type() != key_type->base_type) {
                            uint64_t got_id = string_symbolizer_->intern(detail::container_value_type_name(key.deref()));
                            uint64_t want_id = string_symbolizer_->intern(detail::type_info_display_name(key_type));
                            return checked_result<void>(make_error_code(runtime_error_code::map_key_type_mismatch),
                                "Cannot use '{0}' as a key of type '{1}'", got_id, want_id);
                        }
                    }
                    const size_t prior_map_size = map.size();
                    script_value& value_ref = map[key];
                    // engine::memory_cap: count NEW-entry growth (raised at the next back-edge)
                    if (map.size() != prior_map_size) {
                        limits_->memory_charge_deferred(2 * sizeof(script_value) + (key.is_string() ? key.unchecked_as_string().size() : 0));
                    }

                    // If this created a new entry with default constructor, it has invalid engine reference
                    if (!value_ref.has_valid_engine()) {
                        if (!left.has_valid_engine()) {
                            return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                                "Invalid script_value: both map and new entry missing engine reference");
                        }
                        value_ref.set_engine(left.get_engine());
                    }

                    // Get value type constraint from the map's type_info for validation on assignment
                    auto map_type_info = left.get_type_info();
                    type_info_ptr value_type = map_type_info ? map_type_info->value_type() : nullptr;
                    // Map-entry mode: pins the map + re-resolves by key (temporaries/erase safe)
                    script_value ref_value = script_value::make_map_entry_reference(left.get_map_storage(), key, engine_, value_type);
                    push_value(std::move(ref_value));
                } else if (is_lvalue && !transient_read) {
                    // lvalue-shaped but a READ (e.g. x = m[k], or the inner m[k]
                    // of m[k][i] / m[k].field): return a reference to the EXISTING
                    // entry so nested in-place mutation still works, but NEVER
                    // insert a missing key — reading must not grow the map. A compound's
                    // own target on a typed-value map inserts the value type's default first
                    // (C++ operator[]; KEEP BYTE-PARALLEL with vm_backend::exec_index_compound_fused)
                    if (expr == compound_map_target_) {
                        auto map_tag = left.get_type_info();
                        type_info_ptr key_type = map_tag ? map_tag->key_type() : nullptr;
                        if (key_type && key_type->base_type != script_value_type::jai_any_type && right.deref().type() != key_type->base_type) {
                            uint64_t got_id = string_symbolizer_->intern(detail::container_value_type_name(right.deref()));
                            uint64_t want_id = string_symbolizer_->intern(detail::type_info_display_name(key_type));
                            return checked_result<void>(make_error_code(runtime_error_code::map_key_type_mismatch),
                                "Cannot use '{0}' as a key of type '{1}'", got_id, want_id);
                        }
                        (void)detail::map_compound_insert_default(left, right, engine_);
                    }
                    auto it = map.find(right);
                    if (it != map.end()) {
                        script_value& value_ref = const_cast<script_value&>(it->second);
                        if (!value_ref.has_valid_engine()) {
                            value_ref.set_engine(left.has_valid_engine() ? left.get_engine() : engine_);
                        }
                        auto map_type_info = left.get_type_info();
                        type_info_ptr value_type = map_type_info ? map_type_info->value_type() : nullptr;
                        push_value(script_value::make_map_entry_reference(left.get_map_storage(), right, engine_, value_type));
                    } else {
                        // Missing key on a read: yield null WITHOUT inserting.
                        push_value(script_value(std::monostate{}, engine_));
                    }
                } else {
                    // This is a true temporary (e.g., function return), read-only access
                    auto it = map.find(right);
                    if (it != map.end()) {
                        // Ensure the value has an engine ref before pushing
                        script_value val = it->second;
                        if (!val.has_valid_engine()) {
                            val.set_engine(engine_);
                        }
                        push_value(std::move(val));
                    } else {
                        push_value(script_value(std::monostate{}, engine_));
                    }
                }
            } catch (...) {
                // Fallback for any edge cases
                const auto& map = left.as_map();
                auto it = map.find(right);
                if (it != map.end()) {
                    // Ensure the value has an engine ref before pushing
                    script_value val = it->second;
                    if (!val.has_valid_engine()) {
                        val.set_engine(engine_);
                    }
                    push_value(std::move(val));
                } else {
                    push_value(script_value(std::monostate{}, engine_));
                }
            }
        } else if (left.is_string()) {
            // s[i]: byte i as a char; a store target mints a STRING-ELEMENT reference
            // (detail/string_element.hpp; KEEP BYTE-PARALLEL with vm_backend::exec_index)
            if (!right.is_int()) {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_index_type), "String index must be an integer");
            }
            const auto& str = left.unchecked_as_string();
            script_int index = right.unchecked_as_int();
            if (index < 0 || index >= static_cast<script_int>(str.size())) {
                return checked_result<void>(make_error_code(runtime_error_code::index_out_of_bounds),
                    "String index {0} out of bounds for string of size {1}",
                    string_symbolizer_->intern(std::to_string(index)),
                    string_symbolizer_->intern(std::to_string(str.size())));
            }
            const script_char byte = static_cast<script_char>(str[static_cast<size_t>(index)]);
            if (want_string_target) {
                script_value pin = string_element_pin(left_raw, expr->left.get());
                if (pin.is_reference()) {
                    push_value(script_value::make_string_element_reference(pin, static_cast<size_t>(index), engine_));
                    return {};
                }
            }
            push_value(script_value(byte, engine_));
        } else if (left.is_utf8_string()) {
            // u[i]: character i as a utf8_string, a store target like s[i] (KEEP BYTE-PARALLEL with vm_backend::exec_index)
            if (!right.is_int()) {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_index_type), "String index must be an integer");
            }
            const script_utf8& text = left.unchecked_as_utf8();
            const bool cache = !engine_->parallel_region_active();
            const size_t len = detail::utf8::length(text, cache);
            script_int index = right.unchecked_as_int();
            if (index < 0 || index >= static_cast<script_int>(len)) {
                return checked_result<void>(make_error_code(runtime_error_code::index_out_of_bounds),
                    "String index {0} out of bounds for string of size {1}",
                    string_symbolizer_->intern(std::to_string(index)),
                    string_symbolizer_->intern(std::to_string(len)));
            }
            if (want_string_target) {
                script_value pin = string_element_pin(left_raw, expr->left.get());
                if (pin.is_reference()) {
                    push_value(script_value::make_string_element_reference(pin, static_cast<size_t>(index), engine_));
                    return {};
                }
            }
            push_value(detail::utf8::char_value(text, static_cast<size_t>(index), engine_, cache));
        } else {
            if (left.is_object()) {
                // Safe-mode workers: operator[]/global-operator dispatch copies shared
                // method values — verdict (increment B; KEEP BYTE-PARALLEL with the vm)
                if (parallel_worker_) [[unlikely]] {
                    return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                        "custom operator dispatch on class instances is not admitted in a parallel body");
                }
                // First, try to find operator[] as a method on the object (for class instances)
                auto instance_result = left.checked_as<std::shared_ptr<class_instance>>();
                if (instance_result) {
                    auto instance = instance_result.value();
                    // Use cached subscript operator ID
                    script_value method = instance->get_method(subscript_op_id_, false);
                    if (method.is_function()) {
                        const script_function& func = method.as_function();
                        std::vector<script_value> args = {left, right};
                        auto result = func(args);
                        if (!result) {
                            return result.error_value();
                        }
                        push_value(std::move(result.value()));
                        return {};
                    }
                }

                // Fall back to the global [] operator (flat table - no env probe)
                if (operator_table_) {
                    if (const script_value* getMethod = operator_table_->entry(detail::op_slot::subscript)) {
                        const script_function& func = getMethod->as_function();
                        std::vector<script_value> args = {left, right};
                        auto result = func(args);
                        if (!result) {
                            // Function returned error - propagate it up
                            return result.error_value();
                        }
                        push_value(std::move(result.value()));
                        return {};
                    }
                }
            }
            return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                "Subscript can only be used on arrays, maps, or types with [] operator");
        }
        return {};
    }

    // Flat handler dispatch for built-in operators with already-evaluated operands
    // (direct op_slot index - no hash on the generic path)
    const detail::op_slot handler_slot = detail::binary_op_slot(expr->op.type);
    binary_op_handler handler_fn = handler_slot != detail::op_slot::none
        ? binary_handlers_[static_cast<size_t>(handler_slot)] : nullptr;
    if (handler_fn) {
        auto result = (this->*handler_fn)(left, right);
        if (!result) [[unlikely]] {
            return result.error_value();
        }
        push_value(std::move(result.value()));
    } else {
        return checked_result<void>(make_error_code(runtime_error_code::unknown_operator), "Unknown binary operator");
    }
    return {};
}
template<bool Replaying>
checked_result<void> interpreter::visit_unary_expr_t(unary_expr* expr, const replay_point* rs) {
    // Fast path for literal unary operations
    if (expr->operand->get_type() == node_type::literal_expr) {
        auto* literal = static_cast<literal_expr*>(expr->operand.get());
        const script_value& val = literal->value;

        switch (expr->op.type) {
            case token_type::minus:
                if (val.is_int()) {
                    // -INT64_MIN is UB; apply the overflow policy exactly like the generic
                    // path below (a parse-folded literal INT64_MIN reaches here; vm parity)
                    script_int neg;
                    if (!ints::try_neg(val.unchecked_as_int(), neg)) {
                        return int_overflow_v("Integer overflow in unary '-'");
                    }
                    push_value(make_value(neg));
                    return {};
                } else if (val.is_float()) {
                    push_value(make_value(-val.unchecked_as_float()));
                    return {};
                }
                break;
            case token_type::bang:
                push_value(make_value(!is_truthy(val)));
                return {};
            case token_type::tilde:
                if (val.is_int()) {
                    push_value(make_value(~val.unchecked_as_int()));
                    return {};
                }
                break;
            default:
                break; // Fall through to generic path for increment/decrement
        }
    }

    // Generic path - evaluate operand and use existing logic
    JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->operand.get(), rs, 0), expr, rp_operand, 0, {});
    // Operand raised a script exception (e.g. ++obj.missing member read): propagate it
    // instead of clobbering it with invalid_assignment_target below (vm parity)
    if (is_unwinding_) {
        return {};
    }
    script_value operand = pop_value();
    // an element read arrives as a reference wrapper (the rhs-lvalue shape): the unary consumes the VALUE like every
    // other consumer (KEEP BYTE-PARALLEL with vm_backend::exec_unary); copy out through a temp, the holder owns the target
    if (operand.is_reference()) {
        script_value derefed = operand.deref();
        operand = std::move(derefed);
    }

    // a class instance's unary operator answers where the builtin rows miss (KEEP BYTE-PARALLEL with vm_backend::exec_unary)
    auto user_unary = [&](token_type uop) -> std::optional<checked_result<void>> {
        auto user = object_unary_via_method(operand, uop);
        if (!user) { return std::nullopt; }
        if (!*user) { return checked_result<void>(user->error_value()); }
        push_value(std::move(user->value()));
        return checked_result<void>{};
    };

    // `!` consults truthiness BEFORE the S8 decode: is_truthy handles bound values in
    // full, and decoding an OPAQUE bound to null would lie about its truthiness (§13,
    // 2026-07). Identical result for bound primitives. Byte-parallel with exec_unary.
    if (expr->op.type == token_type::bang) {
        push_value(make_value(!is_truthy(operand)));
        return {};
    }

    size_t oi = operand.raw_storage_index();
    // S8: bound operand decodes in place to a detached temp (byte-parallel with vm_backend::exec_unary)
    if (oi == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
        operand = operand.bound_decoded_temp();
        oi = operand.raw_storage_index();
    }
    switch (expr->op.type) {
        case token_type::minus: {
            if (oi == script_value::TYPEID_INT) {
                // -INT64_MIN is undefined behavior; ints::try_neg applies the overflow
                // policy (raise when checked, wrap to INT64_MIN otherwise).
                script_int neg;
                if (!ints::try_neg(operand.unchecked_as_int(), neg)) {
                    return int_overflow_v("Integer overflow in unary '-'");
                }
                push_value(make_value(neg));
            } else if (oi == script_value::TYPEID_FLOAT) {
                push_value(make_value(-operand.unchecked_as_float()));
            } else if (detail::math::is_kind(oi)) {   // KEEP BYTE-PARALLEL with vm_backend::exec_unary
                auto negated = detail::math::negate(operand, engine_);
                if (!negated) {
                    return negated.error_value();
                }
                push_value(std::move(negated.value()));
            } else {
                if (auto landed = user_unary(token_type::minus)) { return std::move(*landed); }
                return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand), "Unary minus requires numeric operand");
            }
            break;
        }

        case token_type::bang:
            push_value(make_value(!is_truthy(operand)));
            break;

        case token_type::plus:
            // C++'s unary plus: a number is itself, a char promotes to its int
            if (oi == script_value::TYPEID_INT || oi == script_value::TYPEID_FLOAT) { push_value(std::move(operand)); break; }
            if (oi == script_value::TYPEID_CHAR) { push_value(detail::char_promoted(operand, engine_)); break; }
            if (auto landed = user_unary(token_type::plus)) { return std::move(*landed); }
            return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand), "Unary plus requires numeric operand");

        case token_type::tilde:
            // Bitwise NOT
            if (oi != script_value::TYPEID_INT) {
                if (auto landed = user_unary(token_type::tilde)) { return std::move(*landed); }
                return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand), "Bitwise NOT requires integer operand");
            }
            push_value(make_value(~operand.unchecked_as_int()));
            break;
            
        case token_type::plus_plus:
        case token_type::minus_minus: {
            // Handle increment/decrement with in-place mutation (ChaiScript-style)
            if (expr->operand->get_type() == node_type::identifier_expr) {
                auto* identifier = static_cast<identifier_expr*>(expr->operand.get());
                // Cache symbol ID if not already cached
                if (identifier->symbol_id == UINT64_MAX) {
                    identifier->symbol_id = string_symbolizer_->intern(identifier->name);
                }

                // Try fast path: slot-based locals first (function params and typed locals
                // live in call-frame slots, not the environment), then environment
                script_value* varPtr = resolve_local_or_env(identifier->slot_index, identifier->symbol_id, identifier->global_qualified);
                if (varPtr) {
                    // Scratch-backed ref (typed element, C++ property): deref hands back the
                    // holder scratch - the in-place mutation below must commit it (buffer
                    // store / setter) (KEEP BYTE-PARALLEL with the VM exec_incdec twin)
                    auto* scratch_ref = varPtr->scratch_ref_holder();
                    // Fast path: direct in-place mutation for local variables
                    script_value& target = varPtr->deref();

                    const bool isIncrement = (expr->op.type == token_type::plus_plus);
                    // §12.1 write-through: ++/-- on a bound VARIABLE decodes the live value and
                    // stores back through assign_through (the type() switch below would mutate a
                    // dead shadow). KEEP BYTE-PARALLEL with the VM exec_incdec bound branch.
                    if (target.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
                        if (target.is_int()) {
                            const script_int oldVal = target.unchecked_as_int();
                            script_int newVal;
                            if (isIncrement) {
                                if (!ints::try_add(oldVal, 1, newVal)) return int_overflow_v("Integer overflow in '++'");
                            } else {
                                if (!ints::try_sub(oldVal, 1, newVal)) return int_overflow_v("Integer overflow in '--'");
                            }
                            target.assign_through(make_value(newVal));
                            push_value(make_value(expr->is_postfix ? oldVal : newVal));
                            return {};
                        }
                        if (target.is_float()) {
                            const script_float oldVal = target.unchecked_as_float();
                            const script_float newVal = isIncrement ? oldVal + 1.0 : oldVal - 1.0;
                            target.assign_through(make_value(newVal));
                            push_value(make_value(expr->is_postfix ? oldVal : newVal));
                            return {};
                        }
                        return detail::incdec_mismatch(isIncrement, target, engine_);
                    }
                    switch (target.type()) {
                        case script_value_type::jai_int_type: {
                            // Overflow policy applies to plain ++/-- like every other int
                            // op (INCDEC-WRAPS-SILENTLY); wrap builds fold the check away.
                            script_int& tref = target.unchecked_as_int_ref();
                            const script_int oldVal = tref;
                            script_int newVal;
                            if (isIncrement) {
                                if (!ints::try_add(oldVal, 1, newVal)) return int_overflow_v("Integer overflow in '++'");
                            } else {
                                if (!ints::try_sub(oldVal, 1, newVal)) return int_overflow_v("Integer overflow in '--'");
                            }
                            tref = newVal;
                            if (scratch_ref) [[unlikely]] { scratch_ref->commit_scratch(); }
                            push_value(make_value(expr->is_postfix ? oldVal : newVal));
                            return {};
                        }
                        case script_value_type::jai_float_type: {
                            if (expr->is_postfix) {
                                push_value(make_value(target.unchecked_as_float()));
                                if (isIncrement) target.unchecked_as_float_ref() += 1.0;
                                else target.unchecked_as_float_ref() -= 1.0;
                            } else {
                                if (isIncrement) target.unchecked_as_float_ref() += 1.0;
                                else target.unchecked_as_float_ref() -= 1.0;
                                push_value(make_value(target.unchecked_as_float()));
                            }
                            if (scratch_ref) [[unlikely]] { scratch_ref->commit_scratch(); }
                            return {};
                        }
                        default:
                            return detail::incdec_mismatch(isIncrement, target, engine_);
                    }
                } else {
                    // ::name targets never reach the implicit this.member fallback
                    if (identifier->global_qualified) {
                        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                            environment_->undefined_text(identifier->symbol_id), identifier->symbol_id);
                    }
                    // Fallback: identifier is an implicit this.member access
                    // Check if 'this' exists and has this field
                    script_value* this_ptr = environment_->get_value_ptr(string_symbolizer_->get_this_id());
                    if (this_ptr && this_ptr->is_object()) {
                        script_value& this_val = *this_ptr;
                        std::shared_ptr<class_instance> instance = this_val.get_class_instance();

                        if (instance && instance->has_field(identifier->symbol_id)) {
                            if (class_definition* field_class = instance->get_class_definition(); field_class && field_class->chain_has_nonpublic()) [[unlikely]] {
                                JAISCRIPT_TRY(detail::enforce_member_access(field_class, identifier->symbol_id, environment_->find_access_context()));
                            }
                            // A hybrid's C++ property reads through its getter and stores through
                            // its setter (KEEP BYTE-PARALLEL with the VM exec_incdec this-tail)
                            const uint64_t prop_getter = detail::cpp_property_getter_id(instance.get(), identifier->symbol_id);
                            const uint64_t prop_setter = prop_getter ? detail::cpp_property_setter_id(instance.get(), identifier->symbol_id) : 0;
                            script_value currentVal = make_value();
                            if (prop_getter) {
                                JAISCRIPT_TRY_ASSIGN(currentVal, detail::read_bare_cpp_property(this_val, instance.get(),
                                    instance->get_method(prop_getter, false), prop_getter, identifier->symbol_id));
                            } else {
                                currentVal = instance->get_field(identifier->symbol_id);
                            }
                            const bool isIncrement = (expr->op.type == token_type::plus_plus);
                            // S8: a bound-alias field decodes live; set_field stores the detached
                            // result (§12.5). KEEP BYTE-PARALLEL with the VM this-field fallback.
                            if (currentVal.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
                                currentVal = currentVal.bound_decoded_temp();
                            const size_t ti = currentVal.raw_storage_index();

                            if (ti == script_value::TYPEID_INT) {
                                script_int oldVal = currentVal.unchecked_as_int();
                                script_int newVal;
                                if (isIncrement) {
                                    if (!ints::try_add(oldVal, 1, newVal)) return int_overflow_v("Integer overflow in '++'");
                                } else {
                                    if (!ints::try_sub(oldVal, 1, newVal)) return int_overflow_v("Integer overflow in '--'");
                                }
                                if (prop_setter) {
                                    JAISCRIPT_TRY(detail::write_cpp_property(this_val, instance->get_class_definition(), prop_setter, make_value(newVal)));
                                } else {
                                    instance->set_field(identifier->symbol_id, make_value(newVal));
                                }
                                push_value(make_value(expr->is_postfix ? oldVal : newVal));
                                return {};
                            } else if (ti == script_value::TYPEID_FLOAT) {
                                script_float oldVal = currentVal.unchecked_as_float();
                                script_float newVal = isIncrement ? oldVal + 1.0 : oldVal - 1.0;
                                if (prop_setter) {
                                    JAISCRIPT_TRY(detail::write_cpp_property(this_val, instance->get_class_definition(), prop_setter, make_value(newVal)));
                                } else {
                                    instance->set_field(identifier->symbol_id, make_value(newVal));
                                }
                                push_value(make_value(expr->is_postfix ? oldVal : newVal));
                                return {};
                            } else {
                                return detail::incdec_mismatch(isIncrement, currentVal, engine_);
                            }
                        }
                    }
                    uint64_t name_id = string_symbolizer_->intern(identifier->name);
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        environment_->undefined_text(name_id), name_id);
                }
            } else {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_assignment_target));
            }
            break;
        }

        default:
            return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));  // [ErrorText] Unknown operator
    }
    return {};
}

checked_result<script_value> interpreter::assign_math_component(script_value& base, member_expr* memberExpr, const script_value& value) {
    return detail::math::assign_component(base, memberExpr, value,
        [this](const identifier_expr* id) { return resolve_local_or_env(id->slot_index, id->symbol_id, id->global_qualified); },
        [this](const member_expr* m) {
            return detail::math::static_component_cell(m, *engine_, *environment_, *string_symbolizer_, environment_->find_access_context());
        },
        detail::caller_frame_view{call_stack_.empty() ? nullptr : &call_stack_.back()}, environment_.get(), engine_, string_symbolizer_);
}

checked_result<void> interpreter::assign_member_value(const script_value& objectValue, member_expr* memberExpr, const script_value& value) {
    // Worker direct-field store (parallel_for v1): the setter probe below COPIES a
    // shared method value (cross-worker count race); a backing field's enforce+set is
    // exactly what the auto-setter does, on exclusively-owned instance storage. Any
    // non-field member store needs method machinery - wall it.
    if (parallel_worker_ && objectValue.raw_storage_index() == script_value::TYPEID_OBJECT) [[unlikely]] {
        auto& holder = const_cast<script_value&>(objectValue).unchecked_get_object_storage();
        if (holder && holder->is_class_instance_wrapper && holder->data) {
            auto* inst = static_cast<class_instance*>(holder->data.get());
            if (memberExpr->member_id != UINT64_MAX && inst->find_field_value(memberExpr->member_id)) {
                auto enforced = inst->enforce_field_write(memberExpr->member_id, clone_for_assignment(value));
                if (!enforced) {
                    return enforced.error_value();
                }
                inst->set_field_unchecked(memberExpr->member_id, std::move(enforced).value());
                return {};
            }
            return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                "only direct field stores on class instances are allowed in a parallel body");
        }
    }
    auto target = resolve_member_target(objectValue);
    if (!target) {
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
            "Cannot assign property to non-object value");
    }

    uint64_t member_id = memberExpr->member_id != UINT64_MAX
        ? memberExpr->member_id
        : string_symbolizer_->intern(memberExpr->member);

    if (target.class_def && target.class_def->chain_has_nonpublic()) [[unlikely]] {
        JAISCRIPT_TRY(detail::enforce_member_access(target.class_def, member_id,
                                                    environment_->find_access_context()));
    }

    auto [setter_id, _] = string_symbolizer_->get_setter_id_with_view(member_id);
    script_value setter = target.user_accessor(setter_id);   // a synthesized setter takes the field rung
    if (!setter.is_null() && !setter.is_invalid() && setter.is_function()) {
        const script_function& func = setter.as_function();
        std::vector<script_value> args = {objectValue, clone_for_assignment(value)};
        auto result = func(args);
        if (!result) {
            return result.error_value();
        }
    } else if (target.has_field(member_id)) {
        // Direct field assignment (deep copy for value types, share for shared_ptr);
        // typed fields enforce like locals
        auto enforced = target.instance->enforce_field_write(member_id, clone_for_assignment(value));
        if (!enforced) {
            return enforced.error_value();
        }
        target.instance->set_field_unchecked(member_id, enforced.value());
    } else {
        const std::string_view cls = target.class_name();
        std::string text = "Cannot assign to '" + std::string(memberExpr->member) + "': " + (cls.empty() ? std::string("object") : std::string(cls)) + " has no such member";
        active_exception_value_ = make_value(text);
        current_exception_ = script_exception(text, memberExpr->location);
        is_unwinding_ = true;
        return {};
    }
    return {};
}

template<bool Replaying>
checked_result<void> interpreter::visit_assignment_expr_t(assignment_expr* expr, const replay_point* rs) {
    // Non-lvalue targets fail before ANY evaluation (the vm rejects them at compile time)
    {
        const node_type target_kind = expr->target->get_type();
        if (target_kind != node_type::identifier_expr && target_kind != node_type::member_expr &&
            !(target_kind == node_type::binary_expr &&
              static_cast<binary_expr*>(expr->target.get())->op.type == token_type::left_bracket)) {
            return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation), "Complex assignment targets not yet implemented");
        }
    }
    // Only the statement's own root discards its value; a nested assignment's stored value is consumed
    struct result_needed_scope { bool& flag; bool saved; ~result_needed_scope() { flag = saved; } } result_needed{ expression_result_needed_, expression_result_needed_ };
    expression_result_needed_ = expr != unconsumed_root_;

    // For compound assignment operators, we need the current value
    if (expr->op.type != token_type::equal) {
        // Get current value of the target
        if (expr->target->get_type() == node_type::identifier_expr) {
            auto* identifier = static_cast<identifier_expr*>(expr->target.get());
            // Cache symbol ID if not already cached
            if (identifier->symbol_id == UINT64_MAX) {
                identifier->symbol_id = string_symbolizer_->intern(identifier->name);
            }

            // Slot-based O(1) first, then environment fallback
            script_value* varPtr = resolve_local_or_env(identifier->slot_index, identifier->symbol_id, identifier->global_qualified);
            if (varPtr) {
                // Constrained element/field ref (Tier 1 bind): route through the shared
                // helper so the compound result honors the constraint like subscript
                // compounds do (vm parity). A C++ PROPERTY ref takes the same route: its
                // deref is a scratch, the write must reach the setter.
                if (varPtr->is_reference()) {
                    const auto* refHolder = varPtr->get_reference_holder();
                    if (refHolder && (refHolder->container_element_type || refHolder->property_ref())) {
                        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->value.get(), rs, 0), expr, rp_asg_rhs, 0, {});
                        if (is_unwinding_) {
                            push_value(make_value());
                            return {};
                        }
                        script_value rightValue = pop_value();
                        const token_type op = detail::compound_base(expr->op.type);
                        const char* opName = detail::binary_op_name(op);
                        auto result = detail::ref_compound_store_constrained(*varPtr, rightValue, op, opName,
                            operator_table_, engine_, string_symbolizer_,
                            [this](const script_value& l, token_type o, const script_value& r) {
                                return evaluate_arithmetic(l, o, r, detail::compound_index(o));
                            });
                        if (!result) {
                            return result.error_value();
                        }
                        push_value(std::move(result.value()));
                        return {};
                    }
                }
                // an element, map-entry or property reference is dereferenced only after the right-hand side has run: the
                // language's order reads the target after the right side (a reference to a removed element raises when the
                // store resolves, not before the right side's calls; KEEP BYTE-PARALLEL with the VM's compound store, whose
                // right value is already on the stack); a plain cell or a value slot cannot raise on a dereference, so the
                // fast paths keep reading it first
                const auto* target_holder = varPtr->is_reference() ? varPtr->get_reference_holder() : nullptr;
                const bool deferred_target = target_holder != nullptr && !target_holder->plain_cell();
                if (!deferred_target) {
                // Fast path: direct in-place mutation for local variables
                script_value& target = varPtr->deref();
                auto leftType = target.type();

                // === ULTRA FAST PATH: int += int literal (e.g., sum += 1) ===
                // Skip dispatch_expr entirely for int literal RHS.
                // Gated on STORAGE (a typed-null target must not reach unchecked_*); ops
                // route through jai::ints so the overflow policy covers compound assigns.
                if (target.raw_storage_index() == script_value::TYPEID_INT && !has_custom_numeric_ops_) [[likely]] {
                    if (expr->value->get_type() == node_type::literal_expr) {
                        auto* rhs_lit = static_cast<literal_expr*>(expr->value.get());
                        if (rhs_lit->value.raw_storage_index() == script_value::TYPEID_INT) {  // int literal
                            script_int rhs_val = rhs_lit->value.unchecked_as_int();
                            script_int& tref = target.unchecked_as_int_ref();
                            script_int rr;
                            switch (expr->op.type) {
                                case token_type::plus_equal:
                                    if (!ints::try_add(tref, rhs_val, rr)) return int_overflow_v("Integer overflow in '+='");
                                    tref = rr;
                                    push_value(expression_result_needed_ ? target.clone() : target);
                                    return {};
                                case token_type::minus_equal:
                                    if (!ints::try_sub(tref, rhs_val, rr)) return int_overflow_v("Integer overflow in '-='");
                                    tref = rr;
                                    push_value(expression_result_needed_ ? target.clone() : target);
                                    return {};
                                case token_type::star_equal:
                                    if (!ints::try_mul(tref, rhs_val, rr)) return int_overflow_v("Integer overflow in '*='");
                                    tref = rr;
                                    push_value(expression_result_needed_ ? target.clone() : target);
                                    return {};
                                case token_type::slash_equal:
                                    if (rhs_val == 0) {
                                        return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                                    }
                                    if (!ints::try_div(tref, rhs_val, rr)) return int_overflow_v("Integer overflow in '/='");
                                    tref = rr;
                                    push_value(expression_result_needed_ ? target.clone() : target);
                                    return {};
                                default:
                                    break;  // Fall through to general path
                            }
                        }
                    }
                    // === ULTRA FAST PATH: int += int variable (e.g., sum += i) ===
                    // Skip dispatch_expr for simple identifier RHS
                    else if (expr->value->get_type() == node_type::identifier_expr) {
                        auto* rhs_id = static_cast<identifier_expr*>(expr->value.get());
                        if (rhs_id->symbol_id == UINT64_MAX) {
                            rhs_id->symbol_id = string_symbolizer_->intern(rhs_id->name);
                        }
                        script_value* rhs_ptr = resolve_local_or_env(rhs_id->slot_index, rhs_id->symbol_id, rhs_id->global_qualified);
                        if (rhs_ptr && rhs_ptr->raw_storage_index() == script_value::TYPEID_INT) {  // int value
                            script_int rhs_val = rhs_ptr->unchecked_as_int();
                            script_int& tref = target.unchecked_as_int_ref();
                            script_int rr;
                            switch (expr->op.type) {
                                case token_type::plus_equal:
                                    if (!ints::try_add(tref, rhs_val, rr)) return int_overflow_v("Integer overflow in '+='");
                                    tref = rr;
                                    push_value(expression_result_needed_ ? target.clone() : target);
                                    return {};
                                case token_type::minus_equal:
                                    if (!ints::try_sub(tref, rhs_val, rr)) return int_overflow_v("Integer overflow in '-='");
                                    tref = rr;
                                    push_value(expression_result_needed_ ? target.clone() : target);
                                    return {};
                                case token_type::star_equal:
                                    if (!ints::try_mul(tref, rhs_val, rr)) return int_overflow_v("Integer overflow in '*='");
                                    tref = rr;
                                    push_value(expression_result_needed_ ? target.clone() : target);
                                    return {};
                                case token_type::slash_equal:
                                    if (rhs_val == 0) {
                                        return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                                    }
                                    if (!ints::try_div(tref, rhs_val, rr)) return int_overflow_v("Integer overflow in '/='");
                                    tref = rr;
                                    push_value(expression_result_needed_ ? target.clone() : target);
                                    return {};
                                default:
                                    break;  // Fall through to general path
                            }
                        }
                    }
                    // === FAST PATH: int += simple binary expr (e.g., sum += i * 2) ===
                    else if (expr->value->get_type() == node_type::binary_expr) {
                        auto* rhs_binary = static_cast<binary_expr*>(expr->value.get());
                        // Handle identifier * literal pattern (most common in loops)
                        if (rhs_binary->left->get_type() == node_type::identifier_expr) {
                            auto* left_id = static_cast<identifier_expr*>(rhs_binary->left.get());
                            if (rhs_binary->right->get_type() == node_type::literal_expr) {
                                auto* right_lit = static_cast<literal_expr*>(rhs_binary->right.get());
                                if (right_lit->value.raw_storage_index() == script_value::TYPEID_INT) {  // int literal
                                    if (left_id->symbol_id == UINT64_MAX) {
                                        left_id->symbol_id = string_symbolizer_->intern(left_id->name);
                                    }
                                    script_value* left_ptr = resolve_local_or_env(left_id->slot_index, left_id->symbol_id, left_id->global_qualified);
                                    if (left_ptr && left_ptr->raw_storage_index() == script_value::TYPEID_INT) {
                                        script_int left_val = left_ptr->unchecked_as_int();
                                        script_int right_val = right_lit->value.unchecked_as_int();
                                        script_int binary_result = 0;
                                        bool handled = true;
                                        switch (rhs_binary->op.type) {
                                            case token_type::star: if (!ints::try_mul(left_val, right_val, binary_result)) return int_overflow_v("Integer overflow in '*'"); break;
                                            case token_type::plus: if (!ints::try_add(left_val, right_val, binary_result)) return int_overflow_v("Integer overflow in '+'"); break;
                                            case token_type::minus: if (!ints::try_sub(left_val, right_val, binary_result)) return int_overflow_v("Integer overflow in '-'"); break;
                                            default: handled = false; break;
                                        }
                                        if (handled && expr->op.type == token_type::plus_equal) {
                                            script_int& tref = target.unchecked_as_int_ref();
                                            script_int rr;
                                            if (!ints::try_add(tref, binary_result, rr)) return int_overflow_v("Integer overflow in '+='");
                                            tref = rr;
                                            push_value(expression_result_needed_ ? target.clone() : target);
                                            return {};
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                }   // !deferred_target: the fast paths over a target that cannot raise on its dereference

                // Evaluate the right-hand side (general path)
                JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->value.get(), rs, 0), expr, rp_asg_rhs, 0, {});
                if (is_unwinding_) {
                    // The RHS raised: surface the ORIGINAL exception instead of running the
                    // compound arithmetic on the placeholder, whose follow-on type error
                    // clobbered the catch value (KEEP BYTE-PARALLEL with the constrained-ref
                    // branch above and the vm's compound store)
                    push_value(make_value());
                    return {};
                }
                script_value rightValue = pop_value();
                // S8: a bound rhs decodes to a detached temp (the old shadow-index operand behavior)
                if (rightValue.deref().raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
                    script_value decoded = rightValue.deref().bound_decoded_temp();
                    rightValue = std::move(decoded);
                }
                auto& derefRight = rightValue.deref();

                // Get type for RHS
                auto rightType = derefRight.type();

                // The right side ran user code: a by-reference argument may have boxed the variable into a cell in
                // place, so the slot's representation changed under the reference taken above (the wrapper read as a
                // Type mismatch). The rest of the path runs against the binding read again HERE, where the language's
                // order puts the target's read (the right operand before the left, as for every compound assignment;
                // KEEP BYTE-PARALLEL with the VM's compound store, which reads the slot after the right side)
                auto apply = [&](script_value& target, script_value_type leftType) -> checked_result<void> {
                // Check for custom operators first (rare path; flat table, no env probe)
                if (has_custom_numeric_ops_) [[unlikely]] {
                    detail::op_slot op_slot = detail::op_slot::none;
                    switch (expr->op.type) {
                        case token_type::plus_equal: op_slot = detail::op_slot::plus; break;
                        case token_type::minus_equal: op_slot = detail::op_slot::minus; break;
                        case token_type::star_equal: op_slot = detail::op_slot::star; break;
                        default: if (detail::compound_is_bitwise(expr->op.type)) { op_slot = detail::binary_op_slot(detail::compound_base(expr->op.type)); } break;
                    }
                    if (operator_table_) {
                        if (const script_value* opFunc = operator_table_->entry(op_slot)) {
                            const script_function& func = opFunc->as_function();
                            std::vector<script_value> args = {target.clone(), rightValue};
                            auto result = func(args);
                            if (!result) {
                                return result.error_value();
                            }
                            target = std::move(result.value());
                            push_value(expression_result_needed_ ? target.clone() : target);
                            return {};
                        }
                    }
                }

                // Check for custom arithmetic operators on class instances
                if (leftType == script_value_type::jai_object_type) {
                    uint64_t op_symbol_id = 0;
                    op_symbol_id = compound_op_symbol(expr->op.type);
                    if (op_symbol_id != 0) {
                        auto custom_outcome = object_arithmetic_via_method(target, rightValue, op_symbol_id);
                        if (custom_outcome.has_value() && !*custom_outcome) { return custom_outcome->error_value(); }
                        std::optional<script_value> custom_result;
                        if (custom_outcome.has_value()) { custom_result = std::move(custom_outcome->value()); }
                        if (custom_result.has_value()) {
                            target = std::move(custom_result.value());
                            push_value(expression_result_needed_ ? target.clone() : target);
                            return {};
                        }
                    }
                }

                // &= |= ^= <<= >>=: the binary operator's kernel, stored back through the slot's own table
                // (KEEP BYTE-PARALLEL with the VM exec_compound_store)
                if (detail::compound_is_bitwise(expr->op.type)) [[unlikely]] {
                    const token_type baseOp = detail::compound_base(expr->op.type);
                    JAISCRIPT_TRY_ASSIGN(script_value promoted, evaluate_arithmetic(target, baseOp, derefRight, detail::compound_index(baseOp)));
                    if (target.raw_storage_index() == script_value::TYPEID_CPP_BOUND) { target.assign_through(std::move(promoted)); }
                    else { JAISCRIPT_TRY(compound_typed_store_back(target, std::move(promoted), identifier->name)); }
                    push_value(expression_result_needed_ ? target.clone() : target);
                    return {};
                }

                // §12.1 write-through: compound on a bound VARIABLE reads the live value, computes,
                // and stores back through assign_through (was a silent dead-shadow no-op).
                // KEEP BYTE-PARALLEL with the VM exec_compound_store bound branch.
                if (target.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
                    const size_t rIdx = derefRight.raw_storage_index();
                    switch (expr->op.type) {
                        case token_type::plus_equal: {
                            if (target.is_int()) {
                                if (rIdx == script_value::TYPEID_INT) {
                                    script_int rr;
                                    if (!ints::try_add(target.unchecked_as_int(), derefRight.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '+='");
                                    target.assign_through(make_value(rr));
                                } else if (rIdx == script_value::TYPEID_FLOAT) {
                                    target.assign_through(make_value(target.unchecked_as_int() + derefRight.unchecked_as_float()));
                                } else {
                                    return detail::compound_mismatch(0, target, derefRight, engine_);
                                }
                            } else if (target.is_float()) {
                                const auto rf = derefRight.checked_as_float();
                                if (!rf) { return detail::compound_mismatch(0, target, derefRight, engine_); }
                                target.assign_through(make_value(target.unchecked_as_float() + rf.value()));
                            } else if (target.is_string() && rIdx == script_value::TYPEID_STRING) {
                                // engine::memory_cap chokepoint: deny the append before it exists
                                if (!limits_->memory_charge(derefRight.unchecked_as_string().size())) [[unlikely]] {
                                    return detail::raise_memory_cap(*limits_);
                                }
                                target.unchecked_as_string_ref() += derefRight.unchecked_as_string();
                            } else {
                                return detail::compound_mismatch(0, target, derefRight, engine_);
                            }
                            break;
                        }
                        case token_type::minus_equal: {
                            if (target.is_int()) {
                                if (rIdx == script_value::TYPEID_INT) {
                                    script_int rr;
                                    if (!ints::try_sub(target.unchecked_as_int(), derefRight.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '-='");
                                    target.assign_through(make_value(rr));
                                } else if (rIdx == script_value::TYPEID_FLOAT) {
                                    target.assign_through(make_value(target.unchecked_as_int() - derefRight.unchecked_as_float()));
                                } else {
                                    return detail::compound_mismatch(1, target, derefRight, engine_);
                                }
                            } else if (target.is_float()) {
                                const auto rf = derefRight.checked_as_float();
                                if (!rf) { return detail::compound_mismatch(1, target, derefRight, engine_); }
                                target.assign_through(make_value(target.unchecked_as_float() - rf.value()));
                            } else {
                                return detail::compound_mismatch(1, target, derefRight, engine_);
                            }
                            break;
                        }
                        case token_type::star_equal: {
                            if (target.is_int()) {
                                if (rIdx == script_value::TYPEID_INT) {
                                    script_int rr;
                                    if (!ints::try_mul(target.unchecked_as_int(), derefRight.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '*='");
                                    target.assign_through(make_value(rr));
                                } else if (rIdx == script_value::TYPEID_FLOAT) {
                                    target.assign_through(make_value(target.unchecked_as_int() * derefRight.unchecked_as_float()));
                                } else {
                                    return detail::compound_mismatch(2, target, derefRight, engine_);
                                }
                            } else if (target.is_float()) {
                                const auto rf = derefRight.checked_as_float();
                                if (!rf) { return detail::compound_mismatch(2, target, derefRight, engine_); }
                                target.assign_through(make_value(target.unchecked_as_float() * rf.value()));
                            } else {
                                return detail::compound_mismatch(2, target, derefRight, engine_);
                            }
                            break;
                        }
                        case token_type::slash_equal: {
                            if (rIdx == script_value::TYPEID_INT && derefRight.unchecked_as_int() == 0) {
                                return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                            }
                            if (rIdx == script_value::TYPEID_FLOAT && derefRight.unchecked_as_float() == 0.0) {
                                return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                            }
                            if (target.is_int()) {
                                if (rIdx == script_value::TYPEID_INT) {
                                    script_int rr;
                                    if (!ints::try_div(target.unchecked_as_int(), derefRight.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '/='");
                                    target.assign_through(make_value(rr));
                                } else if (rIdx == script_value::TYPEID_FLOAT) {
                                    target.assign_through(make_value(target.unchecked_as_int() / derefRight.unchecked_as_float()));
                                } else {
                                    return detail::compound_mismatch(3, target, derefRight, engine_);
                                }
                            } else if (target.is_float()) {
                                const auto rf = derefRight.checked_as_float();
                                if (!rf) { return detail::compound_mismatch(3, target, derefRight, engine_); }
                                target.assign_through(make_value(target.unchecked_as_float() / rf.value()));
                            } else {
                                return detail::compound_mismatch(3, target, derefRight, engine_);
                            }
                            break;
                        }
                        default:
                            return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));
                    }
                    push_value(expression_result_needed_ ? target.clone() : target);
                    return {};
                }

                // === IN-PLACE MUTATION (ChaiScript-style) ===
                // Modify the value directly in storage, avoiding make_value() allocations.
                // Gated on STORAGE (typed-null must not reach unchecked_*); integer ops route
                // through jai::ints so the checked-overflow policy covers compound assignment.
                const size_t leftIdx = target.raw_storage_index();
                const size_t rightIdx = derefRight.raw_storage_index();
                if (expr->op.type == token_type::plus_equal && detail::compound_plus_text(target, derefRight) == detail::compound_text::mismatch) {
                    return detail::compound_mismatch(0, target, derefRight, engine_);   // one '+=' table for every destination
                }
                // Integral promotion (char_promotion.hpp): a char operand leaves the in-place
                // ladder and takes the general arithmetic path as int64 0..255, storing back
                // like the %= case does. KEEP BYTE-PARALLEL with the VM exec_compound_store.
                if (detail::char_operands_promote(leftIdx, rightIdx) || detail::utf8_operands(leftIdx, rightIdx) || detail::math::owns_arithmetic(leftIdx, rightIdx)) [[unlikely]] {
                    token_type baseOp = token_type::plus;
                    switch (expr->op.type) {
                        case token_type::plus_equal: baseOp = token_type::plus; break;
                        case token_type::minus_equal: baseOp = token_type::minus; break;
                        case token_type::star_equal: baseOp = token_type::star; break;
                        case token_type::slash_equal: baseOp = token_type::slash; break;
                        case token_type::percent_equal: baseOp = token_type::percent; break;
                        default: return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));
                    }
                    JAISCRIPT_TRY_ASSIGN(script_value promoted, evaluate_arithmetic(target, baseOp, derefRight, detail::compound_index(baseOp)));
                    JAISCRIPT_TRY(compound_typed_store_back(target, std::move(promoted), identifier->name));
                    push_value(expression_result_needed_ ? target.clone() : target);
                    return {};
                }
                const bool bothInt = leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_INT;
                switch (expr->op.type) {
                    case token_type::plus_equal: {
                        if (bothInt) {
                            script_int& tref = target.unchecked_as_int_ref();
                            script_int rr;
                            if (!ints::try_add(tref, derefRight.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '+='");
                            tref = rr;
                        } else if (leftIdx == script_value::TYPEID_FLOAT) {
                            const auto rf = derefRight.checked_as_float();
                            if (!rf) { return detail::compound_mismatch(0, target, derefRight, engine_); }
                            target.unchecked_as_float_ref() += rf.value();
                        } else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
                            JAISCRIPT_TRY(compound_typed_store_back(target, make_value(target.unchecked_as_int() + derefRight.unchecked_as_float()), identifier->name));
                        } else if (leftIdx == script_value::TYPEID_STRING && rightIdx == script_value::TYPEID_STRING) {
                            // engine::memory_cap chokepoint: deny the append before it exists
                            if (!limits_->memory_charge(derefRight.unchecked_as_string().size())) [[unlikely]] {
                                return detail::raise_memory_cap(*limits_);
                            }
                            target.unchecked_as_string_ref() += derefRight.unchecked_as_string();
                        } else if (leftIdx == script_value::TYPEID_STRING && rightIdx == script_value::TYPEID_CHAR) {
                            // string += char appends the char as TEXT (out += to_char(b),
                            // the binary-writer shape) — a char is a text unit. Non-char
                            // rhs keeps the Strong Types raise (string += int stays an
                            // error, unlike binary +, by pinned design). KEEP
                            // BYTE-PARALLEL with the VM exec_compound_store.
                            JAISCRIPT_TRY_ASSIGN(script_value joined, evaluate_arithmetic(target, token_type::plus, derefRight, 0));
                            JAISCRIPT_TRY(compound_typed_store_back(target, std::move(joined), identifier->name));
                        } else {
                            return detail::compound_mismatch(0, target, derefRight, engine_);
                        }
                        break;
                    }

                    case token_type::minus_equal: {
                        if (bothInt) {
                            script_int& tref = target.unchecked_as_int_ref();
                            script_int rr;
                            if (!ints::try_sub(tref, derefRight.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '-='");
                            tref = rr;
                        } else if (leftIdx == script_value::TYPEID_FLOAT) {
                            const auto rf = derefRight.checked_as_float();
                            if (!rf) { return detail::compound_mismatch(1, target, derefRight, engine_); }
                            target.unchecked_as_float_ref() -= rf.value();
                        } else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
                            JAISCRIPT_TRY(compound_typed_store_back(target, make_value(target.unchecked_as_int() - derefRight.unchecked_as_float()), identifier->name));
                        } else {
                            return detail::compound_mismatch(1, target, derefRight, engine_);
                        }
                        break;
                    }

                    case token_type::star_equal: {
                        if (bothInt) {
                            script_int& tref = target.unchecked_as_int_ref();
                            script_int rr;
                            if (!ints::try_mul(tref, derefRight.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '*='");
                            tref = rr;
                        } else if (leftIdx == script_value::TYPEID_FLOAT) {
                            const auto rf = derefRight.checked_as_float();
                            if (!rf) { return detail::compound_mismatch(2, target, derefRight, engine_); }
                            target.unchecked_as_float_ref() *= rf.value();
                        } else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
                            JAISCRIPT_TRY(compound_typed_store_back(target, make_value(target.unchecked_as_int() * derefRight.unchecked_as_float()), identifier->name));
                        } else {
                            return detail::compound_mismatch(2, target, derefRight, engine_);
                        }
                        break;
                    }

                    case token_type::slash_equal: {
                        if (rightIdx == script_value::TYPEID_INT && derefRight.unchecked_as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                        }
                        if (rightIdx == script_value::TYPEID_FLOAT && derefRight.unchecked_as_float() == 0.0) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                        }

                        if (bothInt) {
                            script_int& tref = target.unchecked_as_int_ref();
                            script_int rr;
                            if (!ints::try_div(tref, derefRight.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '/='");
                            tref = rr;
                        } else if (leftIdx == script_value::TYPEID_FLOAT) {
                            const auto rf = derefRight.checked_as_float();
                            if (!rf) { return detail::compound_mismatch(3, target, derefRight, engine_); }
                            target.unchecked_as_float_ref() /= rf.value();
                        } else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
                            JAISCRIPT_TRY(compound_typed_store_back(target, make_value(target.unchecked_as_int() / derefRight.unchecked_as_float()), identifier->name));
                        } else {
                            return detail::compound_mismatch(3, target, derefRight, engine_);
                        }
                        break;
                    }

                    case token_type::percent_equal: {
                        if (rightIdx == script_value::TYPEID_INT && derefRight.unchecked_as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero");
                        }
                        JAISCRIPT_TRY_ASSIGN(script_value promoted, evaluate_arithmetic(target, token_type::percent, derefRight, 4));
                        JAISCRIPT_TRY(compound_typed_store_back(target, std::move(promoted), identifier->name));
                        break;
                    }

                    default:
                        return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));
                }

                push_value(expression_result_needed_ ? target.clone() : target);
                return {};
                };
                varPtr = resolve_local_or_env(identifier->slot_index, identifier->symbol_id, identifier->global_qualified);
                if (!varPtr) [[unlikely]] {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable), environment_->undefined_text(identifier->symbol_id), identifier->symbol_id);
                }
                if (deferred_target) {
                    // the target's reference is read here, after the right side: a removed element or map entry is the
                    // language's error at this point (the checked twin of deref, so the error takes the ordinary flow)
                    auto alive = detail::ref_checked_deref(*varPtr);
                    if (!alive) [[unlikely]] { return alive.error_value(); }
                }
                return apply(varPtr->deref(), varPtr->deref().type());
            } else {
                // VM parity (C++17 sequencing): the rhs of a compound assignment is
                // evaluated BEFORE the target resolves, so rhs errors and side effects
                // (e.g. an undefined rhs identifier) surface first
                JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->value.get(), rs, 0), expr, rp_asg_rhs, 0, {});
                if (is_unwinding_) {
                    push_value(make_value());
                    return {};
                }
                script_value rightValue = pop_value();

                // ::name targets never reach the implicit this.member fallback
                if (identifier->global_qualified) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        environment_->undefined_text(identifier->symbol_id), identifier->symbol_id);
                }
                // Fallback: identifier is an implicit this.member access
                // Check if 'this' exists and has this field
                script_value* this_ptr = environment_->get_value_ptr(string_symbolizer_->get_this_id());
                if (!this_ptr || !this_ptr->is_object()) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        environment_->undefined_text(identifier->symbol_id, "Undefined variable '{0}' (no 'this' in scope)"), identifier->symbol_id);
                }

                script_value& this_val = *this_ptr;
                std::shared_ptr<class_instance> instance = this_val.get_class_instance();

                if (!instance || !instance->has_field(identifier->symbol_id)) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        environment_->undefined_text(identifier->symbol_id, "Undefined variable '{0}' (not a field of 'this')"), identifier->symbol_id);
                }
                if (class_definition* field_class = instance->get_class_definition(); field_class && field_class->chain_has_nonpublic()) [[unlikely]] {
                    JAISCRIPT_TRY(detail::enforce_member_access(field_class, identifier->symbol_id, environment_->find_access_context()));
                }

                // Get current field value. A hybrid's C++ property reads through its getter and
                // stores through its setter (KEEP BYTE-PARALLEL with the VM exec_compound_store this-tail)
                const uint64_t prop_getter = detail::cpp_property_getter_id(instance.get(), identifier->symbol_id);
                const uint64_t prop_setter = prop_getter ? detail::cpp_property_setter_id(instance.get(), identifier->symbol_id) : 0;
                script_value currentValue = make_value();
                if (prop_getter) {
                    JAISCRIPT_TRY_ASSIGN(currentValue, detail::read_bare_cpp_property(this_val, instance.get(),
                        instance->get_method(prop_getter, false), prop_getter, identifier->symbol_id));
                } else {
                    currentValue = instance->get_field(identifier->symbol_id);
                }

                // Check for custom arithmetic operators on class instances
                if (currentValue.is_object()) {
                    const uint64_t op_symbol_id = compound_op_symbol(expr->op.type);
                    if (op_symbol_id != 0) {
                        auto custom_outcome = object_arithmetic_via_method(currentValue, rightValue, op_symbol_id);
                        if (custom_outcome.has_value() && !*custom_outcome) { return custom_outcome->error_value(); }
                        std::optional<script_value> custom_result;
                        if (custom_outcome.has_value()) { custom_result = std::move(custom_outcome->value()); }
                        if (custom_result.has_value()) {
                            if (prop_setter) {
                                JAISCRIPT_TRY(detail::write_cpp_property(this_val, instance->get_class_definition(), prop_setter, custom_result.value()));
                            } else {
                                auto enforced = instance->enforce_field_write(identifier->symbol_id, clone_for_assignment(custom_result.value()));
                                if (!enforced) {
                                    return enforced.error_value();
                                }
                                instance->set_field_unchecked(identifier->symbol_id, enforced.value());
                            }
                            push_value(std::move(custom_result.value()));
                            return {};
                        }
                    }
                }

                // Perform the compound operation (using standard path, no in-place mutation for fields)
                // S8: bound-alias class fields decode-read LIVE; set_field stores the detached
                // result (§12.5). KEEP BYTE-PARALLEL with the VM implicit-this compound normalization.
                if (currentValue.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
                    currentValue = currentValue.bound_decoded_temp();
                if (rightValue.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
                    rightValue = rightValue.bound_decoded_temp();
                // Cache type indices for fast checking
                const size_t ci = currentValue.raw_storage_index();
                const size_t ri = rightValue.raw_storage_index();
                script_value resultValue = make_value();
                const detail::compound_text text_pair = expr->op.type == token_type::plus_equal ? detail::compound_plus_text(currentValue, rightValue) : detail::compound_text::none;
                if (text_pair == detail::compound_text::mismatch) { return detail::compound_mismatch(0, currentValue, rightValue, engine_); }
                if (text_pair == detail::compound_text::append) {   // one '+=' table for every destination (KEEP BYTE-PARALLEL with the VM)
                    JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::plus, rightValue, 0));
                } else
                switch (expr->op.type) {
                    case token_type::plus_equal:
                        if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
                            { script_int rr; if (!ints::try_add(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '+='"); resultValue = make_value(rr); }
                        } else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
                                   (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
                            script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
                            script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
                            resultValue = make_value(cf + rf);
                        } else if (ci == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
                            // engine::memory_cap chokepoint: deny the concat result before it exists
                            if (!limits_->memory_charge(sizeof(script_value) + currentValue.unchecked_as_string().size() + rightValue.unchecked_as_string().size())) [[unlikely]] {
                                return detail::raise_memory_cap(*limits_);
                            }
                            resultValue = make_value(currentValue.unchecked_as_string() + rightValue.unchecked_as_string());
                        } else {
                            return detail::compound_mismatch(0, currentValue, rightValue, engine_);
                        }
                        break;
                    case token_type::minus_equal:
                        if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
                            { script_int rr; if (!ints::try_sub(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '-='"); resultValue = make_value(rr); }
                        } else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
                                   (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
                            script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
                            script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
                            resultValue = make_value(cf - rf);
                        } else {
                            return detail::compound_mismatch(1, currentValue, rightValue, engine_);
                        }
                        break;
                    case token_type::star_equal:
                        if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
                            { script_int rr; if (!ints::try_mul(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '*='"); resultValue = make_value(rr); }
                        } else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
                                   (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
                            script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
                            script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
                            resultValue = make_value(cf * rf);
                        } else {
                            return detail::compound_mismatch(2, currentValue, rightValue, engine_);
                        }
                        break;
                    case token_type::slash_equal: {
                        // the local's semantics: int / int divides as integers (C++), a zero divisor and the operand text come from the kernel
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::slash, rightValue, 3));
                        break;
                    }
                    case token_type::percent_equal: {
                        if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero");
                        }
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::percent, rightValue, 4));
                        break;
                    }
                    default:
                        if (!detail::compound_is_bitwise(expr->op.type)) { return checked_result<void>(make_error_code(runtime_error_code::unknown_operator)); }
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, detail::compound_base(expr->op.type), rightValue,
                                                                              detail::compound_index(detail::compound_base(expr->op.type))));
                        break;
                }

                // Store the enforced (declared-type converted) value; a consumed expression's
                // value is the stored one (Dev ruling 2026-09-08), read back through a property getter
                if (prop_setter) {
                    JAISCRIPT_TRY(detail::write_cpp_property(this_val, instance->get_class_definition(), prop_setter, resultValue));
                    if (expression_result_needed_) {
                        JAISCRIPT_TRY_ASSIGN(resultValue, detail::read_bare_cpp_property(this_val, instance.get(),
                            instance->get_method(prop_getter, false), prop_getter, identifier->symbol_id));
                    }
                } else {
                    auto enforced = instance->enforce_field_write(identifier->symbol_id, clone_for_assignment(resultValue));
                    if (!enforced) {
                        return enforced.error_value();
                    }
                    if (expression_result_needed_) { resultValue = clone_for_assignment(enforced.value()); }
                    instance->set_field_unchecked(identifier->symbol_id, enforced.value());
                }
                push_value(std::move(resultValue));
            }
        } else if (expr->target->get_type() == node_type::member_expr) {
            auto* memberExpr = static_cast<member_expr*>(expr->target.get());
            // C::s op= v writes back through the shared static-store kernel (vm parity)
            auto store_static_target = [&](const script_value& result) -> checked_result<script_value> {
                if (auto ns_target = detail::resolve_namespace_store(*engine_, memberExpr, *string_symbolizer_)) {
                    return store_namespace_cell(*ns_target, memberExpr, clone_for_assignment(result));
                }
                auto cls = detail::resolve_static_store_class(memberExpr, *environment_, *string_symbolizer_,
                                                              environment_->find_access_context());
                if (!cls) {
                    return cls.error_value();
                }
                auto stored = detail::store_static_member(*cls.value(), memberExpr->member_id, clone_for_assignment(result));
                if (!stored) {
                    return stored.error_value();
                }
                return std::move(stored.value());
            };
            // Handle compound assignment to member expression (e.g., obj.value += 10)
            // C++17: the right-hand side is sequenced before the target (Dev ruling 2026-09-27), then the
            // receiver is evaluated ONCE and held raw (a reference receiver re-resolves by mode at the store)
            // across the read and the store-back; a :: target has no receiver (replay: rhs 0, receiver 1,
            // partial {rhs}). KEEP BYTE-PARALLEL with the vm's value-first member compound.
            script_value rightValue;
            script_value objectValue;
            script_value currentValue;
            script_value postfix_old;
            if (parked<Replaying>(rs, 0)) {
                rightValue = rs->partial[0];
            } else {
                JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->value.get(), rs, 0), expr, rp_asg_member_compound, 0, {});
                if (is_unwinding_) {
                    // RHS raised: surface the original exception, never touch the target
                    push_value(make_value());
                    return {};
                }
                rightValue = pop_value();
            }
            if (memberExpr->is_static) {
                JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(memberExpr, rs, 1), expr, rp_asg_member_compound, 1, (std::vector<script_value>{rightValue}));
            } else {
                JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(memberExpr->object.get(), rs, 1), expr, rp_asg_member_compound, 1, (std::vector<script_value>{rightValue}));
                objectValue = pop_value();
                if (is_unwinding_) {
                    push_value(make_value());
                    return {};
                }
                JAISCRIPT_TRY(member_read_on(memberExpr, objectValue, false));
            }
            currentValue = pop_value().deref();
            if (is_unwinding_) {
                // Target read raised: surface it, not the ++/-- gate's error on the placeholder
                push_value(make_value());
                return {};
            }
            postfix_old = expr->postfix_old_result ? currentValue : make_value();
            if (expr->incdec_desugar) {
                const size_t ci = currentValue.raw_storage_index();
                if (ci != script_value::TYPEID_INT && ci != script_value::TYPEID_FLOAT && ci != script_value::TYPEID_CPP_BOUND) {
                    return detail::incdec_mismatch(expr->op.type == token_type::plus_equal, currentValue, engine_);
                }
            }

            // Check for custom arithmetic operators on class instances
            if (currentValue.is_object()) {
                const uint64_t op_symbol_id = compound_op_symbol(expr->op.type);
                if (op_symbol_id != 0) {
                    auto custom_outcome = object_arithmetic_via_method(currentValue, rightValue, op_symbol_id);
                    if (custom_outcome.has_value() && !*custom_outcome) { return custom_outcome->error_value(); }
                    std::optional<script_value> custom_result;
                    if (custom_outcome.has_value()) { custom_result = std::move(custom_outcome->value()); }
                    if (custom_result.has_value()) {
                        if (memberExpr->is_static) {
                            script_value stored_static;
                            JAISCRIPT_TRY_ASSIGN(stored_static, store_static_target(custom_result.value()));
                            push_value(expression_result_needed_ ? clone_for_assignment(stored_static) : std::move(custom_result.value()));
                            return {};
                        }
                        // Assign the result back to the property
                        script_value target_obj = objectValue.deref();
                        if (target_obj.is_object()) {
                            JAISCRIPT_TRY(assign_member_value(target_obj, memberExpr, custom_result.value()));
                            if (is_unwinding_) {
                                push_value(make_value());
                                return {};
                            }
                            if (expression_result_needed_ &&
                                detail::member_reads_back(target_obj, memberExpr->member_id, string_symbolizer_->get_getter_id_with_view(memberExpr->member_id).first, engine_)) {
                                JAISCRIPT_TRY(member_read_on(memberExpr, target_obj, false));
                                if (!is_unwinding_) { script_value got = pop_value(); push_value(clone_for_assignment(got.deref())); }
                                return {};
                            }
                            push_value(std::move(custom_result.value()));
                            return {};
                        }
                    }
                }
            }

            // Perform the compound operation
            // S8: a bound-alias member decode-reads LIVE and assign_member_value stores the
            // detached result (§12.5). KEEP BYTE-PARALLEL with the VM exec_member_compound.
            if (currentValue.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
                currentValue = currentValue.bound_decoded_temp();
            if (rightValue.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
                rightValue = rightValue.bound_decoded_temp();
            // Cache type indices for fast checking
            const size_t ci = currentValue.raw_storage_index();
            const size_t ri = rightValue.raw_storage_index();
            script_value resultValue = make_value();
            const detail::compound_text text_pair = expr->op.type == token_type::plus_equal ? detail::compound_plus_text(currentValue, rightValue) : detail::compound_text::none;
            if (text_pair == detail::compound_text::mismatch) { return detail::compound_mismatch(0, currentValue, rightValue, engine_); }
            if (text_pair == detail::compound_text::append) {   // one '+=' table for every destination (KEEP BYTE-PARALLEL with the VM)
                JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::plus, rightValue, 0));
            } else if (detail::math::owns_arithmetic(ci, ri)) [[unlikely]] {   // a math field or component operand takes the math rows (KEEP BYTE-PARALLEL with vm_backend::exec_member_compound)
                auto math_result = detail::math::binary(detail::math::bop_of(expr->op.type), currentValue, rightValue, engine_);
                if (!math_result) {
                    return math_result.error_value();
                }
                resultValue = std::move(math_result.value());
            } else
            switch (expr->op.type) {
                case token_type::plus_equal: {
                    if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
                        { script_int rr; if (!ints::try_add(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '+='"); resultValue = make_value(rr); }
                    } else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
                               (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
                        script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
                        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
                        resultValue = make_value(cf + rf);
                    } else if (ci == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
                        // engine::memory_cap chokepoint: deny the concat result before it exists
                        if (!limits_->memory_charge(sizeof(script_value) + currentValue.unchecked_as_string().size() + rightValue.unchecked_as_string().size())) [[unlikely]] {
                            return detail::raise_memory_cap(*limits_);
                        }
                        resultValue = make_value(currentValue.unchecked_as_string() + rightValue.unchecked_as_string());
                    } else if (detail::utf8_operands(ci, ri) && currentValue.is_text() && rightValue.is_text()) {
                        if (!limits_->memory_charge(sizeof(script_value) + currentValue.unchecked_text().size() + rightValue.unchecked_text().size())) [[unlikely]] {
                            return detail::raise_memory_cap(*limits_);
                        }
                        resultValue = script_value::make_utf8(currentValue.unchecked_text() + rightValue.unchecked_text(), engine_);
                    } else {
                        return detail::compound_mismatch(0, currentValue, rightValue, engine_);
                    }
                    break;
                }
                case token_type::minus_equal: {
                    if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
                        { script_int rr; if (!ints::try_sub(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '-='"); resultValue = make_value(rr); }
                    } else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
                               (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
                        script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
                        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
                        resultValue = make_value(cf - rf);
                    } else {
                        return detail::compound_mismatch(1, currentValue, rightValue, engine_);
                    }
                    break;
                }
                case token_type::star_equal: {
                    if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
                        { script_int rr; if (!ints::try_mul(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return int_overflow_v("Integer overflow in '*='"); resultValue = make_value(rr); }
                    } else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
                               (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
                        script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
                        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
                        resultValue = make_value(cf * rf);
                    } else {
                        return detail::compound_mismatch(2, currentValue, rightValue, engine_);
                    }
                    break;
                }
                case token_type::slash_equal: {
                    // the local's semantics: int / int divides as integers (C++), a zero divisor and the operand text come from the kernel
                    JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::slash, rightValue, 3));
                    break;
                }
                case token_type::percent_equal: {
                    if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
                        return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero),
                            "Modulo by zero");
                    }
                    JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::percent, rightValue, 4));
                    break;
                }
                default:
                    if (!detail::compound_is_bitwise(expr->op.type)) {
                        return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                            "Unsupported compound assignment operator");
                    }
                    JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, detail::compound_base(expr->op.type), rightValue,
                                                                          detail::compound_index(detail::compound_base(expr->op.type))));
                    break;
            }
            
            if (memberExpr->is_static) {
                script_value stored_static;
                JAISCRIPT_TRY_ASSIGN(stored_static, store_static_target(resultValue));
                push_value(expr->postfix_old_result ? std::move(postfix_old)
                           : expression_result_needed_ ? clone_for_assignment(stored_static) : std::move(resultValue));
                return {};
            }

            // Now assign the result back to the property (the receiver evaluated once above)
            script_value target_obj = objectValue.deref();

            // A math value's component (KEEP BYTE-PARALLEL with vm_backend::exec_member_compound)
            if (detail::math::is_kind(target_obj.raw_storage_index())) {
                auto stored = assign_math_component(objectValue, memberExpr, resultValue);
                if (!stored) {
                    return stored.error_value();
                }
                push_value(expr->postfix_old_result ? std::move(postfix_old)
                           : expression_result_needed_ ? std::move(stored.value()) : std::move(resultValue));
                return {};
            }

            // Check if it's an object
            if (!target_obj.is_object()) {
                // Set exception state instead of throwing
                active_exception_value_ = make_value(std::string("Cannot assign to member of non-object type"));
                current_exception_ = script_exception("Cannot assign to member of non-object type", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return {};
            }

            JAISCRIPT_TRY(assign_member_value(target_obj, memberExpr, resultValue));
            if (is_unwinding_) {
                push_value(make_value());
                return {};
            }
            if (expression_result_needed_ && !expr->postfix_old_result &&
                detail::member_reads_back(target_obj, memberExpr->member_id, string_symbolizer_->get_getter_id_with_view(memberExpr->member_id).first, engine_)) {
                JAISCRIPT_TRY(member_read_on(memberExpr, target_obj, false));   // the stored value, through a user _get_
                if (!is_unwinding_) { script_value got = pop_value(); push_value(clone_for_assignment(got.deref())); }
                return {};
            }
            push_value(expr->postfix_old_result ? std::move(postfix_old) : std::move(resultValue));
        } else {
            // Numeric subscript-compound fast path (a[i] op= v, the GLOOM store shape):
            // compute + write in place - no reference mint, no synthetic assignment
            // replay. Bails to the general path below for ANY shape it can't prove
            // identical (semantics + error text stay the general path's). Sound to bail
            // AFTER probing because every probed expression is side-effect-free: base
            // ident, index ident/int-literal, and a callout_free RHS re-evaluate safely.
            if (expr->target->get_type() == node_type::binary_expr && !has_custom_numeric_ops_ && !expr->postfix_old_result) {
                auto* sub = static_cast<binary_expr*>(expr->target.get());
                detail::op_slot cslot = detail::op_slot::none;
                switch (expr->op.type) {
                    case token_type::plus_equal: cslot = detail::op_slot::plus; break;
                    case token_type::minus_equal: cslot = detail::op_slot::minus; break;
                    case token_type::star_equal: cslot = detail::op_slot::star; break;
                    case token_type::slash_equal: cslot = detail::op_slot::slash; break;
                    case token_type::percent_equal: cslot = detail::op_slot::percent; break;
                    default: break;
                }
                const bool op_overridden = operator_table_ && operator_table_->entry(cslot) != nullptr;
                if (sub->op.type == token_type::left_bracket && cslot != detail::op_slot::none && !op_overridden &&
                    sub->left->get_type() == node_type::identifier_expr &&
                    detail::transient_read_marker::callout_free(expr->value.get())) {
                    auto* base_ident = static_cast<identifier_expr*>(sub->left.get());
                    if (base_ident->symbol_id == UINT64_MAX) {
                        base_ident->symbol_id = string_symbolizer_->intern(base_ident->name);
                    }
                    script_value* base_ptr = resolve_local_or_env(base_ident->slot_index, base_ident->symbol_id, base_ident->global_qualified);
                    script_value* array_val = base_ptr ? &base_ptr->deref() : nullptr;
                    if (array_val && array_val->raw_storage_index() == script_value::TYPEID_ARRAY) {
                        // Index: ident or int literal only (re-evaluation-safe)
                        std::optional<script_int> index;
                        if (sub->right->get_type() == node_type::literal_expr) {
                            const auto& lit = static_cast<literal_expr*>(sub->right.get())->value;
                            if (lit.raw_storage_index() == script_value::TYPEID_INT) {
                                index = lit.unchecked_as_int();
                            }
                        } else if (sub->right->get_type() == node_type::identifier_expr) {
                            auto* idx_ident = static_cast<identifier_expr*>(sub->right.get());
                            if (idx_ident->symbol_id == UINT64_MAX) {
                                idx_ident->symbol_id = string_symbolizer_->intern(idx_ident->name);
                            }
                            if (script_value* idx_ptr = resolve_local_or_env(idx_ident->slot_index, idx_ident->symbol_id, idx_ident->global_qualified)) {
                                const script_value& idx_v = idx_ptr->deref();
                                if (idx_v.raw_storage_index() == script_value::TYPEID_INT) {
                                    index = idx_v.unchecked_as_int();
                                }
                            }
                        }
                        auto storage = array_val->get_array_storage();
                        // typed nodes take the general path (holder scratch + buffer
                        // commit); 2b gives them a raw load-op-store fast path
                        if (index && storage && !storage->is_typed() && *index >= 0 && *index < static_cast<script_int>(storage->size())) {
                            // RHS after the probes: they read no user code and cannot raise, so this observes the value-first order
                            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->value.get(), rs, 0), expr, rp_asg_rhs, 0, {});
                            if (is_unwinding_) {
                                push_value(make_value());
                                return {};
                            }
                            const script_value rhs_holder = pop_value();
                            const script_value& rhs = rhs_holder.deref();
                            script_value* target_ptr = &storage->values()[static_cast<size_t>(*index)];
                            const size_t ei = target_ptr->raw_storage_index();
                            const size_t ri = rhs.raw_storage_index();
                            const bool numeric = (ei == script_value::TYPEID_INT || ei == script_value::TYPEID_FLOAT) &&
                                                 (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT);
                            // Zero divisors bail: the general path owns their error text
                            const bool zero_div =
                                (expr->op.type == token_type::slash_equal &&
                                 ((ri == script_value::TYPEID_INT && rhs.unchecked_as_int() == 0) ||
                                  (ri == script_value::TYPEID_FLOAT && rhs.unchecked_as_float() == 0.0))) ||
                                (expr->op.type == token_type::percent_equal &&
                                 ri == script_value::TYPEID_INT && rhs.unchecked_as_int() == 0);
                            if (numeric && !zero_div) {
                                token_type op;
                                switch (expr->op.type) {
                                    case token_type::minus_equal: op = token_type::minus; break;
                                    case token_type::star_equal: op = token_type::star; break;
                                    case token_type::slash_equal: op = token_type::slash; break;
                                    case token_type::percent_equal: op = token_type::percent; break;
                                    default: op = token_type::plus; break;
                                }
                                script_value resultValue = make_value();
                                JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(*target_ptr, op, rhs, detail::compound_index(op)));
                                // Typed elements: only the identity case (same raw type) is
                                // provably conversion-free; anything else bails
                                auto array_type_info = array_val->get_type_info();
                                type_info_ptr element_type = array_type_info ? array_type_info->element_type() : nullptr;
                                if (!element_type || resultValue.raw_storage_index() == ei) {
                                    *target_ptr = resultValue;
                                    push_value(std::move(resultValue));
                                    return {};
                                }
                            }
                        }
                    }
                }
            }

            // General compound assignment for any expression
            // This handles subscripts, function calls that return references, etc.

            // C++17: the right-hand side is sequenced before the target (Dev ruling 2026-09-27): the value first, then the
            // subscript's container and index and the element's read (replay: rhs 0, target 1, partial {rhs}). KEEP
            // BYTE-PARALLEL with the vm's value-first index compound.
            script_value rightValue;
            if (parked<Replaying>(rs, 0)) {
                rightValue = rs->partial[0];
            } else {
                JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->value.get(), rs, 0), expr, rp_asg_general, 0, {});
                if (is_unwinding_) {
                    // RHS raised: surface the original exception, never touch the target
                    push_value(make_value());
                    return {};
                }
                rightValue = pop_value();
            }
            string_target_context_ = expr->target->get_type() == node_type::binary_expr &&
                                     static_cast<binary_expr*>(expr->target.get())->op.type == token_type::left_bracket;
            compound_map_target_ = expr->target.get();   // a missing key of a typed-value map takes its default (visit_binary)
            auto target_read = eval_child<Replaying>(expr->target.get(), rs, 1);
            compound_map_target_ = nullptr;
            string_target_context_ = false;
            JAISCRIPT_TRY_OPERAND(std::move(target_read), expr, rp_asg_general, 1, (std::vector<script_value>{rightValue}));
            script_value currentValue = pop_value();
            if (is_unwinding_) {
                push_value(make_value());
                return {};
            }
            script_value postfix_old = expr->postfix_old_result ? script_value(currentValue.deref()) : make_value();
            if (expr->incdec_desugar) {
                // ++/-- gate: numeric targets only, the identifier path's exact error
                const size_t ci = currentValue.deref().raw_storage_index();
                if (ci != script_value::TYPEID_INT && ci != script_value::TYPEID_FLOAT && ci != script_value::TYPEID_CPP_BOUND) {
                    return detail::incdec_mismatch(expr->op.type == token_type::plus_equal, currentValue.deref(), engine_);
                }
            }

            // Perform the compound operation
            script_value resultValue = make_value();
            
            // Custom operator consult through the flat table (was a per-store
            // string-construction + env-chain walk; KEEP BYTE-PARALLEL with
            // vm_backend::exec_index_compound)
            detail::op_slot compound_slot = detail::op_slot::none;
            switch (expr->op.type) {
                case token_type::plus_equal: compound_slot = detail::op_slot::plus; break;
                case token_type::minus_equal: compound_slot = detail::op_slot::minus; break;
                case token_type::star_equal: compound_slot = detail::op_slot::star; break;
                case token_type::slash_equal: compound_slot = detail::op_slot::slash; break;
                case token_type::percent_equal: compound_slot = detail::op_slot::percent; break;
                default: if (detail::compound_is_bitwise(expr->op.type)) { compound_slot = detail::binary_op_slot(detail::compound_base(expr->op.type)); } break;
            }
            const script_value* compound_op_fn = operator_table_ ? operator_table_->entry(compound_slot) : nullptr;
            if (compound_op_fn) {
                const script_function& func = compound_op_fn->as_function();
                std::vector<script_value> args = {currentValue, rightValue};
                auto result = func(args);
                if (!result) {
                    // Function returned error - propagate it up
                    return result.error_value();
                }
                resultValue = std::move(result.value());
            } else {
                // S8 (NEW-C): a bound rhs decodes before the zero pre-checks so /= and %= keep
                // today's error codes/texts (byte-parallel with the VM exec_index_compound)
                if (rightValue.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
                    rightValue = rightValue.bound_decoded_temp();
                // Fall back to built-in operators
                switch (expr->op.type) {
                    case token_type::plus_equal: {
                        // one '+=' table for every destination: a text target appends text, a text operand never stringifies
                        const bool text_slot = currentValue.is_reference() && currentValue.get_reference_holder() && currentValue.get_reference_holder()->string_element();
                        if (detail::compound_plus_text(currentValue.deref(), rightValue.deref(), text_slot) == detail::compound_text::mismatch) {
                            return detail::compound_mismatch(0, currentValue.deref(), rightValue.deref(), engine_);
                        }
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::plus, rightValue, 0));
                        break;
                    }
                    case token_type::minus_equal: {
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::minus, rightValue, 1));
                        break;
                    }
                    case token_type::star_equal: {
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::star, rightValue, 2));
                        break;
                    }
                    case token_type::slash_equal: {
                        const size_t ri = rightValue.raw_storage_index();
                        if ((ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) ||
                            (ri == script_value::TYPEID_FLOAT && rightValue.unchecked_as_float() == 0.0)) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                                "Division by zero");
                        }
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::slash, rightValue, 3));
                        break;
                    }
                    case token_type::percent_equal: {
                        if (rightValue.raw_storage_index() == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero),
                                "Modulo by zero");
                        }
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::percent, rightValue, 4));
                        break;
                    }
                    default:
                        if (!detail::compound_is_bitwise(expr->op.type)) {
                            return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                                "Unknown compound assignment operator");
                        }
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, detail::compound_base(expr->op.type), rightValue,
                                                                              detail::compound_index(detail::compound_base(expr->op.type))));
                        break;
                }
            }
            
            if (currentValue.is_reference()) {
                // The target read minted the element reference: store through it, never
                // re-evaluate the subscript (its side effects, and any yield, ran once)
                JAISCRIPT_TRY(store_through_subscript_reference(currentValue, std::move(resultValue)));
                if (expr->postfix_old_result) {
                    valueStack_.discard();
                    push_value(std::move(postfix_old));
                }
            } else {
                // No lvalue came back (rvalue base, string subscript, missing map key): the
                // vm's exec_index_compound position and text (KEEP BYTE-PARALLEL)
                return checked_result<void>(make_error_code(runtime_error_code::invalid_assignment_target), "Cannot assign to rvalue expression");
            }
        }
    } else {
        // Regular assignment

        script_value value;
        if (parked<Replaying>(rs, 0)) {
            value = rs->partial[0];   // evaluated before the yield: the rhs runs once
        } else {
            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->value.get(), rs, 0), expr, rp_asg_regular, 0, {});
            // Check if we're unwinding due to an exception in the value expression
            if (is_unwinding_) {
                // Don't try to pop a value that wasn't pushed due to the exception
                return {};
            }
            value = pop_value();
        }

        // Element/subscript reads arrive as reference wrappers (rhs-lvalue read shape);
        // assignment consumes the VALUE - normalize like every other consumer, or the
        // typed-store check below sees 'reference' and rejects `int x; x = a[0]`
        // (KEEP BYTE-PARALLEL with vm_backend::exec_store). Copy out through a temp:
        // assigning value.deref() straight into value destroys the holder that OWNS
        // the deref target while the copy is still reading it.
        if (value.is_reference()) {
            script_value derefed = value.deref();
            value = std::move(derefed);
        }

        // Check if target is an identifier
        if (expr->target->get_type() == node_type::identifier_expr) {
            auto* identifier = static_cast<identifier_expr*>(expr->target.get());
            // Cache symbol ID if not already cached
            if (identifier->symbol_id == UINT64_MAX) {
                identifier->symbol_id = string_symbolizer_->intern(identifier->name);
            }

            // ============================================================
            // SLOT-BASED FAST PATH: O(1) array indexing for locals
            // ============================================================
            // If the variable is a local with a slot, assign directly
            if (identifier->slot_index != SIZE_MAX && !call_stack_.empty()) {
                script_value* frameLocal = call_stack_.back().get_local(identifier->slot_index);
                if (frameLocal) {
                    // A cell in the slot IS the variable's storage when the name is the
                    // value decl itself (escape-boxed local): write the cell with the same
                    // typed enforcement a plain slot gets. Ref-alias names (ref params /
                    // auto& decls) and non-cell references keep store-through semantics.
                    script_value* storage = frameLocal;
                    bool store_through = false;
                    if (frameLocal->is_reference()) {
                        auto* holder = frameLocal->get_reference_holder();
                        if (holder && holder->has_cell && identifier->names_value_decl) {
                            storage = holder->cell();
                        } else {
                            store_through = true;
                        }
                    }
                    if (store_through) {
                        JAISCRIPT_TRY(detail::ref_store_through(*frameLocal, value, engine_, string_symbolizer_));
                    } else {
                        // Parse-proven typed store: an IDENTICAL interned type tag (raw
                        // pointer compare - int/float tags are engine-canonical) makes
                        // enforcement a provable no-op, and a plain int/float payload makes
                        // the assignment kernel a plain copy. Guarded at runtime, so a
                        // mis-stamp just falls through. (KEEP BYTE-PARALLEL with
                        // vm_backend::exec_store.)
                        if (expr->typed_store_provable) {
                            const size_t vi = value.raw_storage_index();
                            if ((vi == script_value::TYPEID_INT || vi == script_value::TYPEID_FLOAT) &&
                                storage->get_type_info().get() == value.get_type_info().get()) {
                                *storage = value;
                                push_value(std::move(value));
                                return {};
                            }
                        }
                        // Slot locals enforce their locked type like the environment path does
                        // (same-type fast guard keeps the hot store path call-free)
                        type_info_ptr slot_type = storage->get_type_info();
                        const bool proven_identity = expr->static_store_proof == proof_same_type && slot_type &&
                                                     detail::same_type_payload(value, slot_type.get());
                        // A user operator= writes the held instance (KEEP BYTE-PARALLEL with vm_backend::store_popped_value)
                        if (!proven_identity && slot_type && slot_type->base_type == script_value_type::jai_object_type) {
                            if (auto assigned = detail::user_assign_operator(*storage, value, engine_)) {
                                if (!*assigned) { return assigned->error_value(); }
                                push_value(clone_for_assignment(*storage));   // the expression's value is the destination's
                                return {};
                            }
                        }
                        if (!proven_identity && slot_type && (slot_type->base_type != value.type() ||
                                          slot_type->base_type == script_value_type::jai_object_type ||
                                          slot_type->base_type == script_value_type::jai_array_type ||
                                          slot_type->base_type == script_value_type::jai_map_type ||
                                          (slot_type->base_type == script_value_type::jai_shared_ptr_type && value.get_type_info().get() != slot_type.get()))) {
                            auto enforced = enforce_type_compatibility(std::move(value), slot_type, identifier->name);
                            if (!enforced) {
                                return enforced.error_value();
                            }
                            value = std::move(enforced.value());
                            detail::keep_handle_slot_type(value, slot_type);
                            // var slots keep a shared_ptr-tagged rhs's marker (decl
                            // parity; flattening to 'any' would detach at the copy)
                            if (detail::var_slot(slot_type.get()) &&
                                (!value.get_type_info() ||
                                 value.get_type_info()->base_type != script_value_type::jai_shared_ptr_type)) {
                                detail::var_stamp(value, slot_type, engine_);
                            }
                        }
                        // Direct assignment to call frame local (or its cell); the
                        // kernel deep-copies values and shares shared_ptr handles
                        *storage = clone_for_assignment(value);
                    }
                    push_value(std::move(value));
                    return {};
                }
            }

            // Member scope: a member (field, static, or method - shadowing is by NAME)
            // outranks everything past the function's own scopes, so an env hit that
            // could only be a global must yield to the member-store kernel below.
            bool member_wins = false;
            if (!identifier->global_qualified && member_scope_.active() &&
                !detail::own_scope_lookup(environment_.get(), member_scope_.floor, identifier->symbol_id)) {
                // Proven this-field store (docs/type_proofs_design.md I-S3): the implicit-this
                // landing below with its conversion gated by the proof (KEEP BYTE-PARALLEL with
                // vm_backend store_popped_value's proven lane)
                if (identifier->proven_class && identifier->proven_field_write && !parallel_worker_ &&
                    member_scope_.state == detail::member_scope_state::instance && member_scope_.this_storage) {
                    if (class_instance* inst = detail::proven_field_instance(*member_scope_.this_storage,
                                                                             identifier->proven_class, identifier->proven_epoch)) {
                        const bool is_lvalue_read = is_lvalue_expression(expr->value.get());
                        if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
                            value = value.detached_for_store();
                        }
                        script_value incoming = is_lvalue_read ? clone_for_assignment(value) : std::move(value);
                        if (expr->static_store_proof == proof_same_type && identifier->proven_field_type &&
                            detail::same_type_payload(incoming, identifier->proven_field_type)) {
                            if (detail::retags_on_field_write(identifier->proven_field_type)) {
                                incoming.set_type_info(type_info_ptr{identifier->proven_field_type});
                            }
                        } else {
                            auto enforced = inst->enforce_field_write(identifier->symbol_id, std::move(incoming));
                            if (!enforced) {
                                return enforced.error_value();
                            }
                            incoming = std::move(enforced.value());
                        }
                        inst->field_slot_write(identifier->proven_slot, std::move(incoming));
                        if (is_lvalue_read) {
                            push_value(std::move(value));
                        } else {
                            push_value(inst->get_field(identifier->symbol_id));
                        }
                        return {};
                    }
                }
                const auto store_member = detail::probe_member_scope(member_scope_.state, member_scope_.this_storage,
                                                                     member_scope_.statics, identifier->symbol_id, /*want_methods*/ false);
                if (store_member) {
                    JAISCRIPT_TRY(detail::bare_member_access(store_member, identifier->symbol_id, environment_->find_access_context()));
                }
                member_wins = store_member.what != detail::member_probe_hit::kind::miss ||
                              detail::member_scope_has_method(member_scope_.state, member_scope_.this_storage,
                                                              member_scope_.statics, identifier->symbol_id);
            }

            // ::name resolves and stores against the GLOBAL env only
            environment* ident_env = identifier->global_qualified
                ? get_global_environment().get() : environment_.get();

            // Get the current value to check if it's a reference (environment path)
            if (!member_wins && ident_env->contains(identifier->symbol_id)) {
                script_value* currentVal = ident_env->get_value_ptr(identifier->symbol_id);
                bool boxed_cell = false;
                bool store_through = false;
                if (currentVal && currentVal->is_reference()) {
                    // Escape-boxed variable (cell) named as itself: redirect to the cell
                    // inner and run the full assignment semantics on it; anything else
                    // stores through
                    auto* holder = currentVal->get_reference_holder();
                    if (holder && holder->has_cell && identifier->names_value_decl) {
                        currentVal = holder->cell();
                        boxed_cell = true;
                    } else {
                        store_through = true;
                    }
                }
                // Boxed storage writes the cell inner directly (an env assign would
                // replace the handle and detach every alias)
                auto store_back = [&](script_value&& v) -> checked_result<void> {
                    if (boxed_cell) {
                        *currentVal = std::move(v);
                        return {};
                    }
                    return ident_env->assign(identifier->symbol_id, std::move(v));
                };
                // The value of a store through an alias, a bound variable or a handle: a consumed one is the destination's
                // stored value; the statement root hands the host its right-hand side
                const bool handle_rows = store_through || (currentVal && (currentVal->is_cpp_bound() || currentVal->is_weak_ptr() ||
                    (currentVal->get_type_info() && currentVal->get_type_info()->base_type == script_value_type::jai_shared_ptr_type)));
                std::optional<script_value> host_result;
                if (handle_rows && !expression_result_needed_) { host_result = value; }
                script_value* const stored_cell = boxed_cell ? currentVal : nullptr;
                auto push_stored = [&]() {
                    if (host_result) { push_value(std::move(*host_result)); return; }
                    const script_value* stored = stored_cell ? stored_cell : ident_env->get_value_ptr(identifier->symbol_id);
                    push_value(stored ? clone_for_assignment(stored->deref()) : make_value());
                };
                if (store_through) {
                    // This is a reference - assign through it (deep copy the value)
                    JAISCRIPT_TRY(detail::ref_store_through(*currentVal, value, engine_, string_symbolizer_));
                } else if (currentVal && currentVal->is_cpp_bound()) {
                    // This is a C++ bound value - use assign_through
                    currentVal->assign_through(value);
                } else if (currentVal && currentVal->is_weak_ptr()) {
                    // Special handling for weak_ptr assignment
                    if (value.is_null()) {
                        // Assign null - create empty weak_ptr
                        auto type_info = currentVal->get_type_info();
                        JAISCRIPT_TRY(store_back(script_value::make_empty_weak_ptr(type_info, engine_)));
                    } else if (value.is_weak_ptr()) {
                        // Assign another weak_ptr
                        JAISCRIPT_TRY(store_back(std::move(value)));
                    } else if (value.type() == script_value_type::jai_shared_ptr_type) {
                        // Validate type parameter - weak_ptr<T> should only accept shared_ptr<T> or subclass
                        auto weak_type_info = currentVal->get_type_info();
                        auto expected_type = weak_type_info ? weak_type_info->element_type() : nullptr;
                        auto value_type_info = value.get_type_info();
                        if (expected_type && value_type_info &&
                            expected_type->base_type != script_value_type::jai_any_type) {
                            std::string expected_class = expected_type->type_name;
                            std::string actual_class = value_type_info->element_type()
                                ? value_type_info->element_type()->type_name
                                : value_type_info->type_name;

                            if (expected_class != actual_class) {
                                auto eng = engine_;
                                if (eng) {
                                    auto actual_def = eng->get_class_definition(actual_class);
                                    if (!actual_def || !actual_def->is_subtype_of(expected_class)) {
                                        return detail::typed_slot_rejection(value, *weak_type_info,
                                            detail::slot_site{detail::slot_kind::assignment, identifier->name, {}}, engine_);
                                    }
                                }
                            }
                        }

                        // Convert shared_ptr to weak_ptr
                        auto weak_result = script_value::make_weak_ptr(value, engine_);
                        if (!weak_result) {
                            return weak_result.error_value();
                        }
                        JAISCRIPT_TRY(store_back(std::move(weak_result.value())));
                    } else {
                        // A value-semantic object or a primitive: family D's assignment shape
                        // (KEEP BYTE-PARALLEL with vm_backend::exec_store)
                        const type_info* weak_tag = currentVal->get_type_info().get();
                        if (!weak_tag) { weak_tag = engine_->get_type_info_weak_ptr(nullptr); }
                        return detail::typed_slot_rejection(value, *weak_tag,
                            detail::slot_site{detail::slot_kind::assignment, identifier->name, {}}, engine_);
                    }
                } else if (currentVal && currentVal->get_type_info() &&
                          currentVal->get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
                    // Special handling for shared_ptr<T> assignment with auto-unwrap semantics
                    // See TYPE_SYSTEM_DESIGN.md "Assignment Semantics" section
                    auto ptr_type_info = currentVal->get_type_info();
                    auto expected_type = ptr_type_info->element_type();
                    std::string expected_type_name = expected_type ? expected_type->type_name : "";

                    // Monotonic-ladder rule (Dev ruling 2026-07, refines c81c9812): a
                    // var-held (dynamic_pointee) holder behaves IDENTICALLY to a typed
                    // holder wherever typed is legal - a VALUE rhs of a COMPATIBLE class
                    // (same class or subclass of the held pointee) assigns INTO the shared
                    // pointee (aliases see it). var's ONLY extra power is rebinding on an
                    // INCOMPATIBLE value or a primitive.
                    bool dynamic_value_compatible = false;
                    if (ptr_type_info->dynamic_pointee &&
                        value.type() == script_value_type::jai_object_type) {
                        auto src_ti = value.get_type_info();
                        std::string src_name = src_ti ? src_ti->type_name : "";
                        if (!src_name.empty()) {
                            dynamic_value_compatible = (src_name == expected_type_name);
                            if (!dynamic_value_compatible && engine_) {
                                auto src_class = engine_->get_class_definition(src_name);
                                dynamic_value_compatible = src_class && src_class->is_subtype_of(expected_type_name);
                            }
                        }
                    }

                    if (value.is_null()) {
                        // POINTER OP: Nullify pointer
                        JAISCRIPT_TRY(store_back(std::move(value)));
                    } else if (value.is_weak_ptr()) {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign weak_ptr to shared_ptr - use weak.lock() instead");
                    } else if (value.get_type_info() &&
                              value.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
                        // POINTER OP: shared_ptr<U> to shared_ptr<T> - polymorphic reassignment
                        auto value_type_info = value.get_type_info();
                        auto value_element = value_type_info->element_type();
                        if (ptr_type_info->dynamic_pointee) {
                            // var-declared holder: '=' with a handle rhs REBINDS unchecked
                            // (Dev ruling 2026-07; enforcement guards copy-assign-to-
                            // underlying, not re-pointing) - carry the rebindable marker
                            if (engine_ && value_element && !value_type_info->dynamic_pointee) {
                                value.set_type_info(engine_->get_type_info_shared_ptr_dynamic(value_element.get()));
                            }
                            JAISCRIPT_TRY(store_back(std::move(value)));
                        } else {
                            // R-q6: a typed source by its static pointee, a var-held one by its object's class
                            // (KEEP BYTE-PARALLEL with vm_backend::store_popped_value's environment row)
                            if (!detail::shared_handle_admits(value, ptr_type_info, engine_)) {
                                uint64_t expected_id = expected_type ? expected_type->id : 0;
                                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                                    "Cannot assign shared_ptr<{0}> to shared_ptr<{1}>",
                                    string_symbolizer_->intern(detail::shared_handle_judged_class(value, engine_)), expected_id);
                            }

                            // Compatible - reassign pointer (keep target's type info for polymorphism)
                            value.set_type_info(ptr_type_info);
                            JAISCRIPT_TRY(store_back(std::move(value)));
                        }
                    } else if (ptr_type_info->dynamic_pointee && !dynamic_value_compatible) {
                        // var-held handle receiving an INCOMPATIBLE value or a primitive:
                        // REBIND (var's dynamic power where typed would error). A COMPATIBLE
                        // value falls through to the auto-unwrap path below, IDENTICAL to a
                        // typed holder (Dev ruling 2026-07, refines c81c9812). Replace the
                        // stored handle with a value-copy of the rhs and keep the 'any'
                        // marker so the variable stays dynamic for the next '='.
                        script_value rebound = clone_for_assignment(value);
                        if (engine_) {
                            detail::var_stamp(rebound, nullptr, engine_);
                        }
                        JAISCRIPT_TRY(store_back(std::move(rebound)));
                    } else {
                        // VALUE OP: Auto-unwrap - delegate to underlying object
                        // Equivalent to: *currentVal = value (like C++ dereference + assign)

                        // Get the underlying class instance
                        auto holder = currentVal->get_object_holder();
                        if (!holder || !holder->data) {
                            return checked_result<void>(make_error_code(runtime_error_code::invalid_reference),
                                "Cannot assign to null shared_ptr");
                        }

                        auto instance = currentVal->get_class_instance();
                        if (!instance) {
                            // Not a class instance wrapper (e.g., raw C++ object not registered via dynamic_binder)
                            auto type_info = value.get_type_info();
                            uint64_t type_id = type_info ? type_info->id : 0;
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                                "Cannot assign {0} to shared_ptr - object is not a class instance", type_id);
                        }

                        // Get source type info
                        auto source_type_info = value.get_type_info();
                        std::string source_type_name = source_type_info ? source_type_info->type_name : "unknown";

                        // Case 1: Same underlying type (T = T) - copy the object's fields
                        if (source_type_name == expected_type_name) {
                            if (value.type() == script_value_type::jai_object_type ||
                                value.type() == script_value_type::jai_shared_ptr_type) {
                                // Copy fields from source to target (deep copy semantics for the contents)
                                auto source_holder = const_cast<script_value&>(value).get_object_holder();
                                if (source_holder && source_holder->data && source_holder->is_class_instance_wrapper) {
                                    auto source_instance = std::static_pointer_cast<class_instance>(source_holder->data);
                                    if (source_instance) {
                                        instance->copy_fields_from(*source_instance);
                                        push_stored();
                                        return checked_result<void>();
                                    }
                                }
                            }
                            // Fallthrough to operator= check
                        }

                        // Case 2: Different type - try operator=
                        script_value method = instance->get_method(assign_operator_id_, false);
                        if (method.is_function()) {
                            // Found operator= method - call it with the source value
                            const script_function& func = method.as_function();
                            std::vector<script_value> args;
                            args.push_back(*currentVal);  // 'this' - the shared_ptr (transparent access)
                            args.push_back(std::move(value));  // the value being assigned
                            auto result = func(args);
                            if (result) {
                                if (is_unwinding_) { push_value(make_value()); } else { push_stored(); }
                                return checked_result<void>();
                            }
                            return result.error_value();   // the operator's own error, never a stand-in text
                        }

                        // No operator=: family D's assignment shape names the handle variable
                        // (KEEP BYTE-PARALLEL with vm_backend::exec_store)
                        return detail::typed_slot_rejection(value, *ptr_type_info,
                            detail::slot_site{detail::slot_kind::assignment, identifier->name, {}}, engine_);
                    }
                } else {
                    // Regular variable assignment with STRONG TYPES enforcement
                    type_info_ptr target_type = currentVal->get_type_info();

                    // A user operator= writes the held instance (KEEP BYTE-PARALLEL with vm_backend::store_popped_value)
                    if (target_type && target_type->base_type == script_value_type::jai_object_type) {
                        if (auto assigned = detail::user_assign_operator(*currentVal, value, engine_)) {
                            if (!*assigned) { return assigned->error_value(); }
                            push_value(clone_for_assignment(*currentVal));   // the expression's value is the destination's
                            return {};
                        }
                    }

                    // Enforce type compatibility (a proven store whose payload guard holds is
                    // the identity - docs/type_proofs_design.md I-S2)
                    if (!(expr->static_store_proof == proof_same_type && target_type &&
                          detail::same_type_payload(value, target_type.get()))) {
                        auto enforced = enforce_type_compatibility(std::move(value), target_type, identifier->name);
                        if (!enforced) {
                            return enforced.error_value();
                        }
                        value = std::move(enforced.value());
                    }

                    // Handle first assignment for uninitialized auto variables
                    if (!target_type) {
                        // Lock the variable's type to the assigned value's type
                        // The value keeps its own type_info
                    }
                    // For any_type (var), keep the any_type on the variable. Exception
                    // (mirrors the decl path, Dev ruling 2026-07): a shared_ptr-tagged
                    // rhs keeps its marker - flattening to 'any' would silently detach
                    // the handle at the copy below
                    else if (detail::var_slot(target_type.get()) &&
                             (!value.get_type_info() ||
                              value.get_type_info()->base_type != script_value_type::jai_shared_ptr_type)) {
                        detail::var_stamp(value, target_type, engine_);   // the var marker (a typed array keeps its element type)
                    }
                    else if (detail::var_slot(target_type.get()) &&
                             engine_ && value.get_type_info()->element_type() &&
                             !value.get_type_info()->dynamic_pointee) {
                        // var target adopting a handle: mark it rebindable (Dev ruling 2026-07)
                        value.set_type_info(engine_->get_type_info_shared_ptr_dynamic(
                            value.get_type_info()->element_type().get()));
                    }
                    // For locked types, the value already has correct type from enforce_type_compatibility

                    // C++ style move/copy semantics:
                    // - If RHS is a variable/field/index read (lvalue), clone for value semantics
                    // - If RHS is a temporary (function result, constructor, literal, operator), move
                    bool is_lvalue_read = (expr->value->get_type() == node_type::identifier_expr ||
                                          expr->value->get_type() == node_type::member_expr);
                    // Also check for subscript access (binary_expr with left_bracket operator)
                    if (!is_lvalue_read && expr->value->get_type() == node_type::binary_expr) {
                        auto* bin = static_cast<binary_expr*>(expr->value.get());
                        if (bin->op.type == token_type::left_bracket) {
                            is_lvalue_read = true;
                        }
                    }

                    if (is_lvalue_read) {
                        // Reading from a storage location - the kernel deep-copies
                        // values and shares shared_ptr handles
                        script_value assignValue = clone_for_assignment(value);
                        JAISCRIPT_TRY(store_back(std::move(assignValue)));
                        // value is still valid for push_value below
                        push_value(std::move(value));
                    } else if (boxed_cell) {
                        JAISCRIPT_TRY(store_back(std::move(value)));
                        push_value(*currentVal);   // the cell inner IS the stored value
                    } else {
                        // Temporary value - move directly (like C++ RVO/move semantics)
                        // No clone needed - the temporary becomes the stored value
                        JAISCRIPT_TRY(ident_env->assign(identifier->symbol_id, std::move(value)));
                        // Get the stored value back for the expression result
                        // Use shallow copy (shares object_holder) - like C++ returning lvalue reference
                        script_value* stored = ident_env->get_value_ptr(identifier->symbol_id);
                        push_value(stored ? *stored : make_value());  // Shallow copy, not clone
                    }
                }
                if (handle_rows) {
                    if (is_unwinding_) { push_value(make_value()); } else { push_stored(); }
                }
            } else {
                // ::name targets never reach members - a global miss is an error
                if (identifier->global_qualified) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        environment_->undefined_text(identifier->symbol_id), identifier->symbol_id);
                }
                // Variable doesn't exist in environment
                // Try static_method_environment's assign (which handles static fields)
                // or instance method's 'this' field assignment
                bool assigned_to_member = false;

                // Determine if RHS is lvalue (needs clone) or temporary (can move)
                bool is_lvalue_read = (expr->value->get_type() == node_type::identifier_expr ||
                                      expr->value->get_type() == node_type::member_expr);
                // Also check for subscript access (binary_expr with left_bracket operator)
                if (!is_lvalue_read && expr->value->get_type() == node_type::binary_expr) {
                    auto* bin = static_cast<binary_expr*>(expr->value.get());
                    if (bin->op.type == token_type::left_bracket) {
                        is_lvalue_read = true;
                    }
                }

                script_value* this_ptr = environment_->get_value_ptr(string_symbolizer_->get_this_id());
                if (this_ptr) {
                    script_value& this_val = *this_ptr;
                    if (this_val.is_object()) {
                        auto obj_holder = this_val.get_object_holder();
                        if (obj_holder->is_class_instance_wrapper) {
                            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                            // First try instance fields
                            if (instance->has_field(identifier->symbol_id)) {
                                // A hybrid's C++ property stores through its setter, exactly like
                                // this.name = v (a same-named script field shadows it)
                                if (uint64_t setter_id = detail::cpp_property_setter_id(instance.get(), identifier->symbol_id)) {
                                    JAISCRIPT_TRY(detail::write_cpp_property(this_val, instance->get_class_definition(), setter_id, value));
                                    assigned_to_member = true;
                                }
                                // Not a C++ property or no setter - use regular field assignment
                                // (typed fields enforce like locals)
                                if (!assigned_to_member) {
                                    // All-detach ruling (2026-07, #12): the implicit-this
                                    // MOVE path stores a detached snapshot like the
                                    // explicit o.f = spelling (was the one live-member hole)
                                    if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
                                        value = value.detached_for_store();
                                    }
                                    if (is_lvalue_read) {
                                        auto enforced = instance->enforce_field_write(identifier->symbol_id, clone_for_assignment(value));
                                        if (!enforced) {
                                            return enforced.error_value();
                                        }
                                        instance->set_field_unchecked(identifier->symbol_id, enforced.value());
                                    } else {
                                        auto enforced = instance->enforce_field_write(identifier->symbol_id, std::move(value));
                                        if (!enforced) {
                                            return enforced.error_value();
                                        }
                                        instance->set_field_unchecked(identifier->symbol_id, enforced.value());
                                        value = instance->get_field(identifier->symbol_id);  // Shallow copy for return
                                    }
                                    assigned_to_member = true;
                                }
                            }
                            // Then try static fields (typed statics enforce like fields)
                            else {
                                auto class_def = instance->get_class_definition();
                                if (class_def && class_def->has_static_field(identifier->symbol_id)) {
                                    // All-detach (#12): implicit static store snapshots too
                                    if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
                                        value = value.detached_for_store();
                                    }
                                    if (is_lvalue_read) {
                                        auto stored = detail::store_static_member(*class_def, identifier->symbol_id, clone_for_assignment(value));
                                        if (!stored) {
                                            return stored.error_value();
                                        }
                                        if (expression_result_needed_) { value = clone_for_assignment(stored.value()); }   // the stored value is the expression's
                                    } else {
                                        auto stored = detail::store_static_member(*class_def, identifier->symbol_id, std::move(value));
                                        if (!stored) {
                                            return stored.error_value();
                                        }
                                        value = std::move(stored.value());
                                    }
                                    assigned_to_member = true;
                                }
                            }
                        }
                    }
                }

                // Static-method scope: no 'this' to route through, and a nested block's env
                // assign would raw-write the static cell it finds through the scope env - land
                // the store on the scope's class (KEEP BYTE-PARALLEL with vm_backend
                // store_popped_value's statics tail)
                if (!assigned_to_member && member_scope_.state == detail::member_scope_state::statics &&
                    member_scope_.statics && member_scope_.statics->has_static_field(identifier->symbol_id)) {
                    if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
                        value = value.detached_for_store();
                    }
                    if (is_lvalue_read) {
                        auto stored = detail::store_static_member(*member_scope_.statics, identifier->symbol_id, clone_for_assignment(value));
                        if (!stored) {
                            return stored.error_value();
                        }
                        if (expression_result_needed_) { value = clone_for_assignment(stored.value()); }   // the stored value is the expression's
                    } else {
                        auto stored = detail::store_static_member(*member_scope_.statics, identifier->symbol_id, std::move(value));
                        if (!stored) {
                            return stored.error_value();
                        }
                        value = std::move(stored.value());
                    }
                    assigned_to_member = true;
                }

                if (!assigned_to_member) {
                    // Use environment->assign() which will:
                    // - For static_method_environment: check static fields, then return error if not found
                    // - For method_environment: check 'this' fields, then define locally if not found
                    // - For regular environment: return error if variable doesn't exist
                    if (is_lvalue_read) {
                        JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, clone_for_assignment(value)));
                    } else {
                        JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, std::move(value)));
                        // Get back the stored value for the return (shallow copy)
                        script_value* stored = environment_->get_value_ptr(identifier->symbol_id);
                        value = stored ? *stored : make_value();  // Shallow copy, not clone
                    }
                }
                push_value(std::move(value));
            }
        }
        // Check if target is a member expression (property assignment)
        else if (expr->target->get_type() == node_type::member_expr) {
            auto* memberExpr = static_cast<member_expr*>(expr->target.get());
            // Static member assignment: the shared kernel resolves the class and runs the
            // field-store table over the static's declared type (vm parity)
            if (memberExpr->is_static) {
                if (auto ns_target = detail::resolve_namespace_store(*engine_, memberExpr, *string_symbolizer_)) {
                    auto stored = store_namespace_cell(*ns_target, memberExpr, clone_for_assignment(value));
                    if (!stored) {
                        return stored.error_value();
                    }
                    push_value(expression_result_needed_ ? clone_for_assignment(stored.value()) : std::move(stored.value()));
                    return {};
                }
                auto cls = detail::resolve_static_store_class(memberExpr, *environment_, *string_symbolizer_,
                                                              environment_->find_access_context());
                if (!cls) {
                    return cls.error_value();
                }
                // The rhs is the one evaluated at the top of this branch: re-dispatching it
                // here ran every side effect twice (and fired a yield twice)
                auto stored = detail::store_static_member(*cls.value(), memberExpr->member_id, clone_for_assignment(value));
                if (!stored) {
                    return stored.error_value();
                }
                push_value(expression_result_needed_ ? clone_for_assignment(stored.value()) : std::move(stored.value()));   // a consumed static store is a copy of the stored value
                return {};
            }

            // Regular member assignment - evaluate the object (replay ordinal 1: the rhs is parked as partial[0])
            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(memberExpr->object.get(), rs, 1), expr, rp_asg_regular, 1, (std::vector<script_value>{value}));
            script_value objectValue = pop_value();
            if (is_unwinding_) {
                push_value(make_value());
                return {};
            }

            // Dereference if it's a reference (e.g., from array[index])
            script_value dereferenced = objectValue.deref();

            // A math value's component lands in the storage the base names (detail/math_component_store.hpp;
            // KEEP BYTE-PARALLEL with vm_backend::store_member)
            if (detail::math::is_kind(dereferenced.raw_storage_index())) {
                auto stored = assign_math_component(objectValue, memberExpr, value);
                if (!stored) {
                    return stored.error_value();
                }
                push_value(expression_result_needed_ ? std::move(stored.value()) : std::move(value));
                return {};
            }

            // Unwrap shared_ptr if needed
            // After refactor: shared_ptr<T> uses same storage, no unwrapping needed

            // Check if it's an object
            if (!dereferenced.is_object()) {
                // Set exception state instead of throwing
                active_exception_value_ = make_value(std::string("Cannot assign to member of non-object type"));
                current_exception_ = script_exception("Cannot assign to member of non-object type", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return {};
            }

            // Proven field store (docs/type_proofs_design.md I-M2): assign_member_value's field
            // branch minus the ladder (KEEP BYTE-PARALLEL with vm_backend exec_set_member's
            // proven lane)
            if (memberExpr->proven_class && memberExpr->proven_field_write && !parallel_worker_) {
                if (class_instance* inst = detail::proven_field_instance(dereferenced, memberExpr->proven_class,
                                                                         memberExpr->proven_epoch)) {
                    const uint64_t member_id = memberExpr->member_id != UINT64_MAX
                        ? memberExpr->member_id : string_symbolizer_->intern(memberExpr->member);
                    script_value incoming = clone_for_assignment(value);
                    if (expr->static_store_proof == proof_same_type && memberExpr->proven_field_type &&
                        detail::same_type_payload(incoming, memberExpr->proven_field_type)) {
                        if (detail::retags_on_field_write(memberExpr->proven_field_type)) {
                            incoming.set_type_info(type_info_ptr{memberExpr->proven_field_type});
                        }
                    } else {
                        auto enforced = inst->enforce_field_write(member_id, std::move(incoming));
                        if (!enforced) {
                            return enforced.error_value();
                        }
                        incoming = std::move(enforced.value());
                    }
                    if (expression_result_needed_) {
                        script_value stored = clone_for_assignment(incoming);   // the expression's value is a copy of the stored one (Dev ruling 2026-09-08)
                        inst->field_slot_write(memberExpr->proven_slot, std::move(incoming));
                        push_value(std::move(stored));
                    } else {
                        inst->field_slot_write(memberExpr->proven_slot, std::move(incoming));
                        push_value(std::move(value));
                    }
                    return {};
                }
            }
            JAISCRIPT_TRY(assign_member_value(dereferenced, memberExpr, value));
            if (is_unwinding_) {
                push_value(make_value());
                return {};
            }
            if (expression_result_needed_ &&
                detail::member_reads_back(dereferenced, memberExpr->member_id, string_symbolizer_->get_getter_id_with_view(memberExpr->member_id).first, engine_)) {
                JAISCRIPT_TRY(member_read_on(memberExpr, dereferenced, false));   // the stored value, through a user _get_
                if (!is_unwinding_) { script_value got = pop_value(); push_value(clone_for_assignment(got.deref())); }
                return {};
            }
            push_value(std::move(value));  // discarded: nothing observes it
        }
        // Check if target is a subscript expression (array[index] or map[key])
        else if (expr->target->get_type() == node_type::binary_expr) {
            auto* binaryExpr = static_cast<binary_expr*>(expr->target.get());
            if (binaryExpr->op.type == token_type::left_bracket) {
                // Evaluate the entire target expression (e.g., nested["nums"][1])
                // This should return a reference if it's a valid lvalue.
                // Mark this as an assignment-target subscript so a map key is
                // auto-inserted (visit_binary clears the flag for nested reads).
                lvalue_target_context_ = true;
                auto target_eval = eval_child<Replaying>(expr->target.get(), rs, 1);
                lvalue_target_context_ = false;
                if (!target_eval) [[unlikely]] {
                    if (is_yield_signal(target_eval)) park_operands(expr, rp_asg_regular, 1, {value});
                    return target_eval.error_value();
                }
                script_value target_ref = pop_value();
                if (is_unwinding_) {
                    push_value(make_value());
                    return {};
                }
                
                // Check if we got a reference
                if (target_ref.is_reference()) {
                    JAISCRIPT_TRY(store_through_subscript_reference(target_ref, std::move(value)));
                } else {
                    // Not a reference - this means the subscript expression didn't
                    // return an lvalue (e.g., trying to assign to a function call result)
                    return checked_result<void>(make_error_code(runtime_error_code::invalid_assignment_target), "Cannot assign to rvalue expression");
                }
            } else {
                return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation), "Complex assignment targets not yet implemented");
            }
        } else {
            return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation), "Complex assignment targets not yet implemented");
        }
    }
    return {};
}

// Store through the reference a subscript target read minted. Mode-based re-resolution
// (never a cached address): realloc/erase between mint and write can't corrupt the heap.
// TYPED elements: scratch target + buffer commit (KEEP BYTE-PARALLEL with
// vm_backend::exec_index_assign). Leaves the assigned value on the stack.
checked_result<void> interpreter::store_through_subscript_reference(script_value& target_ref, script_value value) {
    auto refHolder = target_ref.get_reference_holder();
    if (refHolder && refHolder->string_element()) {
        JAISCRIPT_TRY(detail::store_string_element(*refHolder, value, engine_));
        push_value(expression_result_needed_ ? clone_for_assignment(value.deref()) : std::move(value));
        return {};
    }
    const bool typed_element = refHolder && refHolder->typed_element();
    script_value* target_ptr;
    if (typed_element) {
        if (refHolder->container_index >= refHolder->container->size()) {
            return checked_result<void>(make_error_code(runtime_error_code::invalid_reference), "Invalid reference in assignment");
        }
        target_ptr = const_cast<script_value*>(&refHolder->materialize_typed_element(engine_));
    } else {
        target_ptr = refHolder->resolve_target();
        if (!target_ptr) {
            return checked_result<void>(make_error_code(runtime_error_code::invalid_reference), "Invalid reference in assignment");
        }
    }

    // Validate element type if this is a container subscript reference
    type_info_ptr element_type = refHolder->container_element_type;
    if (element_type) {
        if (!detail::container_element_compatible(value, element_type, target_ptr->get_engine())) {
            std::string value_type = detail::container_value_type_name(value);
            std::string expected_type = detail::type_info_display_name(element_type);
            uint64_t value_type_id = string_symbolizer_->intern(value_type);
            uint64_t expected_type_id = string_symbolizer_->intern(expected_type);
            return checked_result<void>(
                make_error_code(runtime_error_code::array_element_type_mismatch),
                "Cannot assign '{0}' to element of type '{1}'",
                value_type_id, expected_type_id);
        }
        // Convert element if needed (e.g., int -> float for array<float>)
        auto converted = detail::convert_container_element(engine_, value, element_type);
        if (!converted) { return converted.error_value(); }
        *target_ptr = std::move(converted.value());
    } else {
        // No element type constraint - values deep-copy, shared_ptr handles share
        *target_ptr = clone_for_assignment(value);
    }
    if (typed_element) {
        refHolder->container->set(refHolder->container_index, *target_ptr);
    }
    push_value(expression_result_needed_ ? clone_for_assignment(*target_ptr) : std::move(value));   // a copy of the stored element is the expression's value  // Assignment expressions return the assigned value
    return {};
}

// The pin a string subscript store target writes through (detail/string_element.hpp): the
// base's own reference, its field / property reference, or its variable cell boxed like a
// reference binding boxes it; null for a temporary base. KEEP BYTE-PARALLEL with
// vm_backend::string_element_pin.
script_value interpreter::string_element_pin(script_value& base_raw, const expression* base_expr) {
    if (base_raw.is_reference()) {
        return base_raw;
    }
    if (base_expr->get_type() == node_type::identifier_expr) {
        if (script_value* cell = string_receiver_cell(static_cast<const identifier_expr*>(base_expr))) {
            return share_boxed_env_storage(*cell, engine_);
        }
        return make_value();
    }
    if (detail::is_member_final_ref_lvalue(base_expr)) {
        auto resolved = detail::resolve_ref_lvalue(base_expr,
            detail::caller_frame_view{call_stack_.empty() ? nullptr : &call_stack_.back()},
            environment_.get(), engine_, string_symbolizer_);
        if (resolved && resolved.value().is_reference()) {
            return std::move(resolved.value());
        }
    }
    return make_value();
}

// statement visitors
template<bool Replaying>
checked_result<void> interpreter::visit_expression_stmt_t(expression_stmt* stmt) {
    const expression* prev = std::exchange(unconsumed_root_, stmt->expression.get());
    auto result = eval_root<Replaying>(stmt->expression.get());
    unconsumed_root_ = prev;
    if (!result) [[unlikely]] {
        if (is_yield_signal(result)) {
            note_statement_yield(stmt, stmt->expression.get());
            return {};
        }
        return result.error_value();
    }

    // Early exit if exception is propagating
    if (is_unwinding_) return {};

    // Pop the result - expression statements don't produce values
    pop_value();
    return {};
}

checked_result<void> interpreter::visit_block_stmt(block_stmt* stmt) {
    auto previous = environment_;

    // Check if we're resuming into this block from a coroutine yield
    size_t start_index = 0;
    bool resuming = false;
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        auto* cont = coro_state.peek_continuation(stmt);
        if (cont) {
            start_index = cont->index;
            // Restore the environment that was saved when this block yielded.
            // This includes any inner scopes (for-loop, nested blocks) that
            // were active at the time of yield.
            if (cont->saved_env) {
                environment_ = cont->saved_env;
            }
            coro_state.pop_continuation();
            resuming = true;
            // DON'T create new scope - the saved environment already has it
        }
    }

    if (!resuming) {
        // Create a new child scope for the block
        // With lazy caching, this is O(1) - no flat_lookup_ copy needed
        environment_ = get_block_environment(environment_);
    }

    try {
        // The resumed statement consumes its own frames (expression replay); the rest run cold
        bool resume_entry = resuming;
        for (size_t i = start_index; i < stmt->declarations.size(); ++i) {
            auto result = resume_entry ? dispatch_decl_resuming(stmt->declarations[i].get())
                                       : dispatch_decl(stmt->declarations[i].get());
            resume_entry = false;

            // IMPORTANT: Clear value stack after each declaration to prevent accumulation
            // This ensures objects are destroyed at statement boundaries, not just at block exit
            // Variable declarations pop their values, but some other declarations (like expression_decl)
            // may leave values on the stack
            if (valueStack_.size() > 0) {
                valueStack_.clear();
            }

            if (!result) {
                // Restore scope before returning
                if (!resuming || i > start_index) {
                    release_environment(environment_);
                }
                environment_ = previous;
                return result;
            }

            // Check for control flow: break, continue, return, or exceptions
            if (is_unwinding_ || hasBreakRequest_ || hasContinueRequest_ || hasReturnValue_) {
                break;
            }

            // On yield: record continuation and return without releasing environment
            if (hasYieldRequest_) {
                if (active_coroutine_) {
                    auto& coro_state = coroutine_state(*active_coroutine_);
                    // If inner constructs (if/while/for/block) pushed continuations,
                    // we need to re-enter this statement on resume (save i).
                    // If no inner continuations exist, the yield was a direct child
                    // statement, so skip it on resume (save i + 1).
                    size_t resume_index = coro_state.has_continuations() ? i : i + 1;
                    // Save the current environment (may be deeper than this block's scope
                    // due to inner constructs like for-loops that didn't restore).
                    // On resume, we'll restore this to get back to the right depth.
                    coro_state.push_continuation(stmt, resume_index, environment_);
                }
                // Restore environment_ to previous so parent constructs see correct scope.
                environment_ = previous;
                return {};
            }
        }
    } catch (...) {
        // Restore scope even if an error occurs
        if (!resuming) {
            release_environment(environment_);
        }
        environment_ = previous;
        throw;
    }

    // Clear the value stack before releasing scope
    // This ensures any lingering object references on the stack are released
    // Block statements don't produce a value, so the stack should be cleared
    valueStack_.clear();

    // Restore the previous scope - only release if we created it
    if (!resuming) {
        release_environment(environment_);
    }
    environment_ = previous;
    return {};
}

template<bool Replaying>
checked_result<void> interpreter::visit_variable_decl_t(variable_decl* decl) {
    // a same-unit re-run of a program-scope declaration written without an initializer keeps the variable, an inline
    // declaration adopts the variable it finds (the vm's op_decl_keep)
    if ((decl->default_initialized || decl->is_inline) && decl->slot_index == SIZE_MAX) [[unlikely]] {
        if (decl->is_inline) {
            uint32_t owner = 0;
            const auto verdict = environment_->inline_declare(decl->name_id, owner);
            if (verdict == environment::inline_verdict::conflict) {
                return detail::inline_plain_conflict(*string_symbolizer_, decl->name_id, engine_->unit_label(owner));
            }
            if (verdict == environment::inline_verdict::adopt) { return {}; }
        } else if (!environment_->get_parent()) {
            if (const script_value* cell = environment_->same_unit_cell(decl->name_id); cell && detail::redeclaration_keeps_value(*decl, *cell)) { return {}; }
        }
    }
    // Helper to store value in slot (local) or environment (global)
    auto define_variable = [&](script_value value) -> checked_result<void> {
        // Escape-marked declarations box into a cell (reference_holder cell mode):
        // reference binding then shares the handle. Reference values pass through
        // untouched (ref decls alias; a cell never wraps a reference).
        if (decl->ref_escaping && !value.is_reference()) {
            value = script_value::make_cell_reference(std::move(value), engine_);
        }
        if (decl->slot_index != SIZE_MAX && !call_stack_.empty()) {
            // Local variable with slot - use O(1) slot-based storage
            call_stack_.back().set_local(decl->slot_index, std::move(value));
        } else {
            // Global variable or no slot assigned - use environment
            if (!environment_->define_variable(decl->name_id, std::move(value))) [[unlikely]] { return detail::inline_redefinition(*string_symbolizer_, decl->name_id, decl); }
            if (decl->is_inline) [[unlikely]] { environment_->share(decl->name_id); }
        }
        return {};
    };

    // The type the declaration binds to: explicit, or inferred from a uniform literal
    type_info_ptr effective_decl_type = decl->type;

    // Check if this is a reference variable declaration
    bool is_reference = false;
    if (decl->type && decl->type->base_type == script_value_type::jai_reference_type) {
        is_reference = true;
    }
    
    // Check if this is a weak_ptr declaration
    bool is_weak_ptr = false;
    if (decl->type && decl->type->base_type == script_value_type::jai_weak_ptr_type) {
        is_weak_ptr = true;
    }
    
    // Check if this is a shared_ptr declaration
    bool is_shared_ptr = false;
    if (decl->type && decl->type->base_type == script_value_type::jai_shared_ptr_type) {
        is_shared_ptr = true;
    }
    
    if (is_weak_ptr) {
        // weak_ptr<T> variable - handle initialization
        if (!decl->initializer) {
            // No initializer - create empty weak_ptr
            script_value weak = script_value::make_empty_weak_ptr(decl->type, engine_);
            JAISCRIPT_TRY(define_variable(std::move(weak)));
        } else {
            // Evaluate initializer
            JAISCRIPT_TRY_ROOT(decl, decl->initializer.get());
            script_value value = pop_value();
            if (is_unwinding_) { return {}; }   // a raising initializer (missing member) wins over the declared-type check; the vm's run loop sees the flag before exec_decl_var

            // Element/subscript reads arrive as reference wrappers (rhs-lvalue read
            // shape); declarations consume the VALUE - normalize like assignment does
            // (KEEP BYTE-PARALLEL with vm_backend::exec_decl_var)
            if (value.is_reference()) {
                script_value derefed = value.deref();
                value = std::move(derefed);
            }

            // Handle different initialization cases
            if (value.is_null()) {
                // Initialize with null - create empty weak_ptr
                script_value weak = script_value::make_empty_weak_ptr(decl->type, engine_);
                JAISCRIPT_TRY(define_variable(std::move(weak)));
            } else if (value.is_weak_ptr()) {
                // Initialize with another weak_ptr - copy it
                JAISCRIPT_TRY(define_variable(std::move(value)));
            } else if (value.type() == script_value_type::jai_shared_ptr_type) {
                // Validate type parameter - weak_ptr<T> should only accept shared_ptr<T> or subclass
                auto expected_type = decl->type ? decl->type->element_type() : nullptr;
                auto value_type_info = value.get_type_info();
                if (expected_type && value_type_info &&
                    expected_type->base_type != script_value_type::jai_any_type) {
                    std::string expected_class = expected_type->type_name;
                    std::string actual_class = value_type_info->element_type()
                        ? value_type_info->element_type()->type_name
                        : value_type_info->type_name;

                    if (expected_class != actual_class) {
                        auto eng = engine_;
                        if (eng) {
                            auto actual_def = eng->get_class_definition(actual_class);
                            if (!actual_def || !actual_def->is_subtype_of(expected_class)) {
                                return detail::typed_slot_rejection(value, *decl->type,
                                    detail::slot_site{detail::slot_kind::declaration, decl->name, {}}, engine_);
                            }
                        }
                    }
                }

                // Initialize with shared_ptr - create weak_ptr from it
                auto weak_result = script_value::make_weak_ptr(value, engine_);
                if (!weak_result) {
                    return weak_result.error_value();
                }
                JAISCRIPT_TRY(define_variable(std::move(weak_result.value())));
            } else {
                // A value-semantic object or a primitive: family D's declaration shape
                // (KEEP BYTE-PARALLEL with vm_backend::exec_decl_var)
                return detail::typed_slot_rejection(value, *decl->type,
                    detail::slot_site{detail::slot_kind::declaration, decl->name, {}}, engine_);
            }
        }
    } else if (is_shared_ptr) {
        // shared_ptr<T> variable - handle initialization.
        // shared_ptr<auto> (element_type null) INFERS the pointee from the initializer's
        // exact class, then enforces it exactly like the explicit spelling (Dev ruling
        // 2026-07); a null/missing initializer has nothing to infer.
        const bool infer_pointee = decl->type && !decl->type->element_type();
        if (!decl->initializer) {
            if (infer_pointee) {
                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                    "Cannot infer shared_ptr<auto> pointee without an initializer - use var, or an explicit shared_ptr<T>");
            }
            // No initializer - create null shared_ptr
            script_value null_ptr = make_value();
            null_ptr.set_type_info(decl->type);  // Mark as shared_ptr type
            JAISCRIPT_TRY(define_variable(std::move(null_ptr)));
        } else {
            // Evaluate initializer
            JAISCRIPT_TRY_ROOT(decl, decl->initializer.get());
            script_value value = pop_value();
            if (is_unwinding_) { return {}; }   // a raising initializer (missing member) wins over the declared-type check; the vm's run loop sees the flag before exec_decl_var

            // Element/subscript reads arrive as reference wrappers (rhs-lvalue read
            // shape); `shared_ptr<T> t = arr[i]` must see the element, not 'reference'
            // (KEEP BYTE-PARALLEL with vm_backend::exec_decl_var)
            if (value.is_reference()) {
                script_value derefed = value.deref();
                value = std::move(derefed);
            }

            // Handle different initialization cases
            if (value.is_null()) {
                if (infer_pointee) {
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                        "Cannot infer shared_ptr<auto> pointee from null - use var, or an explicit shared_ptr<T>");
                }
                // Initialize with null - that's fine
                value.set_type_info(decl->type);  // Mark as shared_ptr type
                JAISCRIPT_TRY(define_variable(std::move(value)));
            } else if (value.is_weak_ptr()) {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_weak_ptr_conversion), "Cannot initialize shared_ptr directly from weak_ptr");
            } else if (value.type() == script_value_type::jai_object_type ||
                      value.type() == script_value_type::jai_shared_ptr_type) {
                if (infer_pointee) {
                    // Take the initializer's exact class; thereafter the tag behaves
                    // exactly like the explicit spelling (reassignment enforcement
                    // reads it) - construct-and-share included.
                    std::string actual_class;
                    if (auto instance = value.get_class_instance()) {
                        actual_class = instance->get_class_name();
                    }
                    type_info* inferred = nullptr;
                    if (!actual_class.empty() && engine_) {
                        if (auto* pointee_ti = engine_->get_type_info_object(string_symbolizer_->intern(actual_class))) {
                            inferred = engine_->get_type_info_shared_ptr(pointee_ti);
                        }
                    }
                    if (!inferred) {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot infer shared_ptr<auto> pointee from this initializer - use var, or an explicit shared_ptr<T>");
                    }
                    value.set_type_info(inferred);
                    JAISCRIPT_TRY(define_variable(std::move(value)));
                    return {};
                }
                // Typed shared_ptr declarations enforce their pointee class (R-q6: a typed source by its
                // static pointee, a var-held one by its object's class; detail::shared_handle_admits)
                // (KEEP BYTE-PARALLEL with vm_backend::exec_decl_var)
                if (!detail::shared_handle_admits(value, decl->type, engine_)) {
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                        "Cannot initialize shared_ptr<{0}> with {1}: type must match or be a subclass",
                        decl->type->element_type()->id, string_symbolizer_->intern(detail::shared_handle_judged_class(value, engine_)));
                }
                // Initialize with object/shared_ptr - that's fine, objects are already shared_ptr
                // Mark the type as shared_ptr to ensure reference semantics
                value.set_type_info(decl->type);
                JAISCRIPT_TRY(define_variable(std::move(value)));
            } else {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_shared_ptr_conversion), "Cannot initialize shared_ptr with this type");
            }
        }
    } else if (is_reference) {
        // Reference variable - must have initializer
        if (!decl->initializer) {
            return checked_result<void>(make_error_code(runtime_error_code::uninitialized_reference), "Reference variable must be initialized", decl->name_id);
        }
        
        // Check if initializer is an identifier (can take reference)
        if (decl->initializer->get_type() == node_type::identifier_expr) {
            auto* identExpr = static_cast<identifier_expr*>(decl->initializer.get());
            // Get the target variable's address
            uint64_t targetSymbolId = string_symbolizer_->intern(identExpr->name);

            // SLOT-aware resolution (Dev ruling 2026-07-09): plain function locals live
            // in frame slots, not the environment - the escape marker cell-boxes the
            // SLOT, so the binder must look there first or `auto& r = x` errors
            // "undefined variable" for every slot-resident local. Env storage is
            // pointer-stable (unordered_map); slots are stable within the frame and
            // the share below boxes into an escape-legal cell immediately.
            script_value* targetPtr = resolve_local_or_env(identExpr->slot_index, targetSymbolId, identExpr->global_qualified);
            if (!targetPtr) {
                // A hybrid's bare C++ property: the member itself / its accessors
                // (KEEP BYTE-PARALLEL with the vm's exec_decl_ref_ident)
                if (auto minted = bind_bare_cpp_property_ref(targetSymbolId, identExpr->global_qualified)) {
                    if (!*minted) {
                        return minted->error_value();
                    }
                    JAISCRIPT_TRY(define_variable(std::move(minted->value())));
                    return {};
                }
                JAISCRIPT_TRY(bare_reference_access(targetSymbolId));
                if (auto method = bare_method_reference_error(identExpr)) { return *method; }
                return checked_result<void>(make_error_code(runtime_error_code::undefined_variable), environment_->undefined_text(targetSymbolId, "Cannot take reference of undefined variable"), targetSymbolId);
            }
            
            // Check if the target is itself a reference
            if (targetPtr->is_reference()) {
                auto aliased = decl_ref_alias(*targetPtr, engine_);
                if (!aliased) {
                    return aliased.error_value();
                }
                JAISCRIPT_TRY(define_variable(std::move(aliased.value())));
            } else {
                // Box the variable's storage into a cell and alias it (escape-legal:
                // the ref keeps the cell alive after the env dies)
                JAISCRIPT_TRY(define_variable(share_boxed_env_storage(*targetPtr, engine_)));
            }
        } else {
            // C::s and ns::x are lvalues: the reference binds their storage (KEEP BYTE-PARALLEL with the vm's exec_decl_ref_value)
            if (decl->initializer->get_type() == node_type::member_expr && static_cast<const member_expr*>(decl->initializer.get())->is_static) {
                if (auto cell = detail::static_member_lvalue(*engine_, static_cast<const member_expr*>(decl->initializer.get()), *environment_,
                                                             *string_symbolizer_, environment_->find_access_context())) {
                    JAISCRIPT_TRY_ROOT(decl, decl->initializer.get());   // the read's effects and errors come first, as the vm evaluates it
                    (void)pop_value();
                    if (is_unwinding_) { return {}; }
                    if (!*cell) { return cell->error_value(); }
                    script_value& storage = *cell->value();
                    if (storage.is_reference() && !storage.get_reference_holder()) {
                        return checked_result<void>(make_error_code(runtime_error_code::invalid_reference), "Reference target is null");
                    }
                    JAISCRIPT_TRY(define_variable(share_boxed_env_storage(storage, engine_)));
                    return {};
                }
            }
            // For other expressions, evaluate them and check if they return a reference
            JAISCRIPT_TRY_ROOT(decl, decl->initializer.get());
            script_value result = pop_value();
            if (is_unwinding_) { return {}; }   // a raising initializer (missing member) wins over the declared-type check; the vm's run loop sees the flag before exec_decl_var
            
            // If the result is a reference, alias it (share the holder)
            if (result.is_reference()) {
                auto aliased = decl_ref_alias(result, engine_);
                if (!aliased) {
                    return aliased.error_value();
                }
                JAISCRIPT_TRY(define_variable(std::move(aliased.value())));
            } else if (detail::is_member_final_ref_lvalue(decl->initializer.get())) {
                // Member-final initializer (auto& p = G.player): field reads evaluate to
                // COPIES, so the evaluated result can't alias - resolve the chain through
                // the shared kernel into an owner-pinned FIELD reference instead (route-
                // independence with ref params/returns; Dev ruling 2026-07-12). Access/
                // removed-field errors surface as-is; the kernel's generic non-lvalue
                // verdict (computed script property, non-object receiver) keeps the
                // decl's own error text below.
                // KEEP BYTE-PARALLEL with the vm's exec_decl_ref_value
                auto resolved = detail::resolve_ref_lvalue(decl->initializer.get(),
                    detail::caller_frame_view{call_stack_.empty() ? nullptr : &call_stack_.back()},
                    environment_.get(), engine_, string_symbolizer_);
                if (!resolved && resolved.error() != make_error_code(runtime_error_code::invalid_reference)) {
                    return resolved.error_value();
                }
                if (resolved) {
                    auto aliased = decl_ref_alias(resolved.value(), engine_);
                    if (!aliased) {
                        return aliased.error_value();
                    }
                    JAISCRIPT_TRY(define_variable(std::move(aliased.value())));
                } else {
                    return checked_result<void>(make_error_code(runtime_error_code::invalid_reference),
                        "Cannot take reference of non-lvalue expression (var&/auto& declarations bind locals, "
                        "subscript chains like arr[i] or grid[y][x], map entries, object fields like obj.field and C++ properties; "
                        "computed script properties are not ref-bindable)");
                }
            } else {
                // KEEP BYTE-PARALLEL with the vm's exec_decl_ref_value
                return checked_result<void>(make_error_code(runtime_error_code::invalid_reference),
                    "Cannot take reference of non-lvalue expression (var&/auto& declarations bind locals, "
                    "subscript chains like arr[i] or grid[y][x], map entries, object fields like obj.field and C++ properties; "
                    "computed script properties are not ref-bindable)");
            }
        }
    } else {
        // Regular variable declaration
        script_value value = make_value();
        if (decl->initializer) {
            JAISCRIPT_TRY_ROOT(decl, decl->initializer.get());
            value = std::move(pop_value());
            if (is_unwinding_) { return {}; }   // a raising initializer (missing member) wins over the declared-type check; the vm's run loop sees the flag before exec_decl_var

            // Decl fast path (construction-stamped flags): a scalar or string payload
            // whose storage index already matches the declared type stores straight into
            // the slot - enforce/clone/homogeneity are provably identity there (a scalar
            // clones as a copy, a string as its shared node, so is_lvalue_expression is
            // irrelevant). Anything else - references, mismatched payloads, env-resident
            // decls, escape-boxed decls - falls through to the untouched full path below.
            // (KEEP BYTE-PARALLEL with vm_backend::exec_decl_var)
            if ((decl->decl_fast_flags & variable_decl::decl_fast_slot_store) == variable_decl::decl_fast_slot_store &&
                !decl->ref_escaping && decl->slot_index != SIZE_MAX && !call_stack_.empty()) {
                size_t vi = value.raw_storage_index();
                // Subscript-initialized decls arrive as rhs-lvalue reference wrappers
                // (the dominant profiled shape): the decl consumes the VALUE, so a
                // matching pointee takes the same identity store - copy out BEFORE
                // overwriting `value` (its holder OWNS the deref target).
                const script_value* seen = &value;
                if (vi == script_value::TYPEID_REFERENCE) {
                    seen = &value.deref();
                    vi = seen->raw_storage_index();
                }
                if (variable_decl::identity_payload(vi, decl->type.get())) {
                    if (seen != &value) {
                        script_value pointee(*seen);
                        value = std::move(pointee);
                    }
                    if (decl->type) {
                        value.set_type_info(decl->type);
                    }
                    call_stack_.back().set_local(decl->slot_index, std::move(value));
                    return {};
                }
                if (variable_decl::widening_payload(vi, decl->type.get())) {
                    script_value widened(static_cast<script_float>(seen->unchecked_as_int()), engine_);
                    widened.set_type_info(decl->type);
                    call_stack_.back().set_local(decl->slot_index, std::move(widened));
                    return {};
                }
            }

            // Element/subscript reads arrive as reference wrappers (rhs-lvalue read
            // shape) - and so do ternaries over them, which is_lvalue_expression can't
            // see. Normalize to the VALUE and treat the read as an lvalue read, or the
            // typed enforcement below sees 'reference' and the stored local silently
            // ALIASES the element (KEEP BYTE-PARALLEL with vm_backend::exec_decl_var)
            const bool reference_init = value.is_reference();
            if (reference_init) {
                script_value derefed = value.deref();
                value = std::move(derefed);
            }

            // Only copy if initializing from an lvalue (existing object); temporaries
            // move. The kernel deep-copies values and shares shared_ptr handles.
            if (reference_init || is_lvalue_expression(decl->initializer.get())) {
                value = clone_for_assignment(value);
            }
            // else: Initializing from a temporary - use move semantics (no copy)

            // === HOMOGENEOUS CONTAINER VALIDATION ===
            // For auto x = [...] and array<auto> x = [...], all elements must be the same type
            // For var x = [...] and array<var> x = [...], heterogeneous elements are allowed
            // Also validates maps: auto m = {...} requires homogeneous values
            // Recursively validates nested containers to arbitrary depth
            if (value.is_array() || value.is_map()) {
                bool requires_homogeneity = false;

                if (!decl->type) {
                    // auto x = [...] or auto m = {...} - requires homogeneous elements
                    requires_homogeneity = true;
                } else if (decl->type->is_array()) {
                    // array<...> declaration
                    auto element_type = decl->type->element_type();
                    if (!element_type) {
                        // array<auto> - requires homogeneous elements
                        requires_homogeneity = true;
                    } else if (element_type->base_type == script_value_type::jai_any_type) {
                        // array<var> - heterogeneous allowed
                        requires_homogeneity = false;
                    }
                    // else: array<T> - type validation handled elsewhere
                } else if (decl->type->is_map()) {
                    // map<...> declaration
                    auto value_type = decl->type->value_type();
                    if (!value_type) {
                        // map<K, auto> - requires homogeneous values
                        requires_homogeneity = true;
                    } else if (value_type->base_type == script_value_type::jai_any_type) {
                        // map<K, var> - heterogeneous values allowed
                        requires_homogeneity = false;
                    }
                    // else: map<K, V> - type validation handled elsewhere
                } else if (decl->type->base_type == script_value_type::jai_any_type) {
                    // var x = [...] or var m = {...} - heterogeneous allowed
                    requires_homogeneity = false;
                }

                if (requires_homogeneity) {
                    JAISCRIPT_TRY(detail::validate_container_homogeneous(value, arity_count_ids_, *string_symbolizer_));
                    // A uniform primitive literal infers the element type and takes its
                    // representation (KEEP BYTE-PARALLEL with vm_backend::exec_decl_var)
                    if (value.is_array()) {
                        if (type_info_ptr inferred = detail::infer_uniform_array_type(value, engine_)) {
                            effective_decl_type = inferred;
                        }
                    }
                }
                // Map twin: `auto` key/value slots infer from a uniform literal; mixed keys
                // raise (KEEP BYTE-PARALLEL with vm_backend::exec_decl_var)
                if (value.is_map()) {
                    int mixed_part = 0;
                    if (type_info_ptr inferred = detail::infer_uniform_map_type(value, decl->type, engine_, mixed_part)) {
                        effective_decl_type = inferred;
                    } else if (mixed_part != 0) {
                        return checked_result<void>(
                            make_error_code(mixed_part == 1 ? runtime_error_code::map_key_type_mismatch : runtime_error_code::map_value_type_mismatch),
                            mixed_part == 1 ? "Map 'auto' requires homogeneous keys" : "Map 'auto' requires homogeneous values");
                    }
                }
            }
        }
        // If no initializer, value remains null

        // === STRONG TYPES: Set type_info based on declaration ===
        // - Explicit type (int, float, var, etc.): Use declared type (locks the variable)
        // - auto with initializer: Infer type from value (locks to inferred type)
        // - auto without initializer: Keep nullptr (first assignment will lock type)
        if (effective_decl_type) {
            // Explicit type declaration (int x, float y, var z, etc.), or the element
            // type a uniform literal inferred for auto / array<auto>
            // Typed declarations convert their initializer exactly like assignment does
            // (int d = 4.7 truncates instead of storing a mistagged float payload)
            if (decl->initializer && !(decl->static_init_proof == proof_same_type &&
                                       detail::same_type_payload(value, effective_decl_type.get()))) {
                auto enforced = enforce_type_compatibility(std::move(value), effective_decl_type, decl->name, detail::slot_kind::declaration);
                if (!enforced) {
                    return enforced.error_value();
                }
                value = std::move(enforced.value());
            }
            // Set declared type - this locks the variable's type. Exception: a 'var' decl
            // keeps a shared_ptr-tagged initializer's marker — `var p = new P()` IS
            // `shared_ptr<P> p = P()` (Dev ruling 2026-07), so the reference-semantics
            // tag must survive the decl instead of flattening to 'any'
            if (effective_decl_type->base_type == script_value_type::jai_any_type &&
                (!value.get_type_info() || value.get_type_info()->base_type != script_value_type::jai_shared_ptr_type)) {
                detail::var_stamp(value, effective_decl_type, engine_);   // a typed array keeps its element type (Dev ruling 2026-09-28)
            } else if (effective_decl_type->base_type != script_value_type::jai_any_type ||
                !value.get_type_info() ||
                value.get_type_info()->base_type != script_value_type::jai_shared_ptr_type) {
                value.set_type_info(effective_decl_type);
            } else if (engine_ && value.get_type_info()->element_type() &&
                       !value.get_type_info()->dynamic_pointee) {
                // var-held handle: '=' with a handle rhs REBINDS unchecked (Dev ruling
                // 2026-07; enforcement guards copy-assign-to-underlying, not re-pointing)
                value.set_type_info(engine_->get_type_info_shared_ptr_dynamic(
                    value.get_type_info()->element_type().get()));
            }
        } else if (decl->initializer && value.get_type_info()) {
            // auto with initializer - type is inferred from value and locked
            // Value already has type_info from make_value() or evaluation
            // Keep the value's type_info (already set). Exception: a var-held handle's
            // rebindable marker does NOT transfer - auto is infer-then-enforce
            if (value.get_type_info()->base_type == script_value_type::jai_shared_ptr_type &&
                value.get_type_info()->dynamic_pointee && engine_ &&
                value.get_type_info()->element_type()) {
                value.set_type_info(engine_->get_type_info_shared_ptr(
                    value.get_type_info()->element_type().get()));
            }
            detail::drop_var_slot(value, engine_);   // nor does a var-held array's
        }
        // else: auto without initializer - type_info remains nullptr (uninitialized)
        // First assignment will lock the type

        // Define in slot (local) or environment (global)
        JAISCRIPT_TRY(define_variable(std::move(value)));
        // After move, value is in moved-from state, so don't access it
    }
    return {};
}

// === STRONG TYPES: Type enforcement for assignment ===
// The one typed-slot table (detail::convert_for_typed_slot); this site owns the assignment
// texts. KEEP BYTE-PARALLEL with vm_backend::enforce_type_compatibility.
// A namespace variable's store (ns::x = v, op=, ++): the scope's cell takes the value under the same typed
// enforcement an environment variable gets, a reference-declared variable stores through, a host-registered
// entry is read-only. KEEP BYTE-PARALLEL with vm_backend::store_namespace_cell.
checked_result<script_value> interpreter::store_namespace_cell(const detail::namespace_store_target& target, const member_expr* member, script_value incoming) {
    if (!target.cell) {
        return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch),
            "Cannot assign to '{0}': it is read-only", member->member_id);
    }
    script_value* storage = target.cell;
    if (storage->is_reference()) {
        auto* holder = storage->get_reference_holder();
        if (holder && holder->has_cell) {
            storage = holder->cell();
        } else {
            JAISCRIPT_TRY(detail::ref_store_through(*storage, incoming, engine_, string_symbolizer_));
            return incoming;
        }
    }
    type_info_ptr slot_type = storage->get_type_info();
    if (slot_type && slot_type->base_type == script_value_type::jai_object_type) {
        if (auto assigned = detail::user_assign_operator(*storage, incoming, engine_)) {
            if (!*assigned) { return assigned->error_value(); }
            return clone_for_assignment(*storage);   // the operator wrote the held instance
        }
    }
    if (slot_type && (slot_type->base_type != incoming.type() ||
                      slot_type->base_type == script_value_type::jai_object_type ||
                      slot_type->base_type == script_value_type::jai_array_type ||
                      slot_type->base_type == script_value_type::jai_map_type ||
                      (slot_type->base_type == script_value_type::jai_shared_ptr_type && incoming.get_type_info().get() != slot_type.get()))) {
        auto enforced = enforce_type_compatibility(std::move(incoming), slot_type, member->member);
        if (!enforced) {
            return enforced.error_value();
        }
        incoming = std::move(enforced.value());
        detail::keep_handle_slot_type(incoming, slot_type);
        if (detail::var_slot(slot_type.get()) &&
            (!incoming.get_type_info() || incoming.get_type_info()->base_type != script_value_type::jai_shared_ptr_type)) {
            detail::var_stamp(incoming, slot_type, engine_);
        }
    }
    *storage = std::move(incoming);
    return *storage;
}

checked_result<script_value> interpreter::enforce_type_compatibility(
    script_value value,
    type_info_ptr target_type,
    std::string_view var_name,
    detail::slot_kind kind
) {
    // nullptr target (auto x;) locks to the source type; any (var x) takes anything
    if (!target_type || detail::var_slot(target_type.get())) {
        return std::move(value);
    }
    {   // same-storage scalars are the table's identity row
        const script_value& seen = value.is_reference() ? value.deref() : value;
        size_t want = script_value::TYPEID_INVALID;
        switch (target_type->base_type) {
            case script_value_type::jai_int_type: want = script_value::TYPEID_INT; break;
            case script_value_type::jai_float_type: want = script_value::TYPEID_FLOAT; break;
            case script_value_type::jai_string_type: want = script_value::TYPEID_STRING; break;
            case script_value_type::jai_bool_type: want = script_value::TYPEID_BOOL; break;
            case script_value_type::jai_char_type: want = script_value::TYPEID_CHAR; break;
            default: break;
        }
        if (want != script_value::TYPEID_INVALID && seen.raw_storage_index() == want) {
            return std::move(value);
        }
    }
    auto outcome = detail::convert_for_typed_slot(value, target_type, engine_);
    if (!outcome) {
        return outcome.error_value();
    }
    using verdict = detail::slot_conversion::verdict;
    detail::slot_conversion& out = outcome.value();
    switch (out.v) {
        case verdict::identity:
        case verdict::identity_exact:
            return std::move(value);
        case verdict::converted:
            return std::move(*out.value);
        case verdict::container_mismatch: {
            auto* sym = engine_->get_symbolizer();
            return checked_result<script_value>(
                make_error_code(runtime_error_code::array_element_type_mismatch),
                detail::container_boundary_mismatch_text,
                sym->intern(out.mismatch->first), sym->intern(out.mismatch->second));
        }
        case verdict::handle_target:
            // shared_ptr<T> / weak_ptr<T> `=`: a same-kind tag stores as-is when a shared handle's judged class is T or a
            // subclass (R-q6); a shared handle downgrades into a weak_ptr<T> slot exactly as the declaration form does (outside the table)
            if (value.type() == target_type->base_type &&
                (target_type->base_type != script_value_type::jai_shared_ptr_type || detail::shared_handle_admits(value, target_type, engine_))) {
                return std::move(value);
            }
            if (detail::weak_slot_takes_shared(value, target_type, engine_)) {
                return script_value::make_weak_ptr(value, engine_);
            }
            [[fallthrough]];
        default:
            return detail::typed_slot_rejection(value, *target_type, detail::slot_site{kind, var_name, {}}, engine_);
    }
}

// Compound store-back: locked targets convert the promoted result like '=' does; var
// targets keep the promoted result and re-tag it 'any' so the variable stays dynamic.
// KEEP BYTE-PARALLEL with vm_backend::compound_typed_store_back.
checked_result<void> interpreter::compound_typed_store_back(script_value& target, script_value promoted, std::string_view var_name) {
    type_info_ptr tag = target.get_type_info();
    if (tag && !detail::var_slot(tag.get())) {
        auto enforced = enforce_type_compatibility(std::move(promoted), tag, var_name);
        if (!enforced) {
            return enforced.error_value();
        }
        promoted = std::move(enforced.value());
        promoted.set_type_info(tag);
    } else if (tag) {
        detail::var_stamp(promoted, tag, engine_);
    }
    target = std::move(promoted);
    return {};
}

// Helper to convert value to string, checking for to_string() method on objects
checked_result<std::string> interpreter::value_to_string_with_method(const script_value& val) {
    if (val.type() == script_value_type::jai_object_type) {
        auto target = resolve_member_target(val);
        if (target) {
            auto method_id = string_symbolizer_->intern("to_string");
            auto method_val = target.method(method_id);
            if (!method_val.is_null() && !method_val.is_invalid() && method_val.is_function()) {
                script_value bound = create_bound_method(val, method_val);
                const script_function& method = bound.as_function();
                std::vector<script_value> no_args;
                auto result = method(no_args);
                if (!result) {
                    return result.error_value();   // KEEP BYTE-PARALLEL with vm_backend::value_to_string_with_method
                }
                if (result.value().is_string()) {
                    return std::string(result.value().unchecked_as_string());
                }
            }
        }
    }
    return val.to_string();
}

// String + x: each operand's text, the left operand's to_string() first; a failing to_string() is the expression's error
checked_result<script_value> interpreter::concat_with_methods(const script_value& left, const script_value& right) {
    auto left_text = value_to_string_with_method(left);
    if (!left_text) { return left_text.error_value(); }
    auto right_text = value_to_string_with_method(right);
    if (!right_text) { return right_text.error_value(); }
    script_string joined = std::move(left_text.value());
    joined += right_text.value();
    // engine::memory_cap chokepoint: deny the concat result before it exists
    if (!limits_->memory_charge(sizeof(script_value) + joined.size())) [[unlikely]] {
        return detail::raise_memory_cap(*limits_);
    }
    return make_value(std::move(joined));
}

// Binary operation helpers
checked_result<script_value> interpreter::evaluate_arithmetic(const script_value& left_in, token_type op, const script_value& right_in, int compound) {
    // References must be transparent here: compound assignment through a subscript
    // (m["k"] -= dt) evaluates its target to a reference value.
    const script_value& left = left_in.deref();
    const script_value& right = right_in.deref();

    // Cache type indices for fast checking
    const size_t li = left.raw_storage_index();
    const size_t ri = right.raw_storage_index();

    // S8: bound operands normalize to detached temps (byte-parallel with vm_backend::evaluate_arithmetic)
    if (li == script_value::TYPEID_CPP_BOUND || ri == script_value::TYPEID_CPP_BOUND) [[unlikely]]
        return evaluate_arithmetic(li == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left, op,
                                   ri == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right, compound);

    // &= |= ^= <<= >>=: the binary operator's kernel; an operand that is not an integer (a char promotes) takes the
    // compound's text (KEEP BYTE-PARALLEL with vm_backend::evaluate_arithmetic)
    if (detail::bitwise_token(op)) [[unlikely]] {
        const auto integral = [](size_t idx) { return idx == script_value::TYPEID_INT || idx == script_value::TYPEID_CHAR; };
        if (compound >= 0 && (!integral(li) || !integral(ri))) { return detail::compound_mismatch(static_cast<size_t>(compound), left, right, engine_); }
        switch (op) {
            case token_type::ampersand: return handle_bitwise_and(left, right);
            case token_type::pipe: return handle_bitwise_or(left, right);
            case token_type::caret: return handle_bitwise_xor(left, right);
            case token_type::left_shift: return handle_left_shift(left, right);
            default: return handle_right_shift(left, right);
        }
    }

    // Integral promotion: char operands enter arithmetic as int64 0..255 (char_promotion.hpp)
    if (detail::utf8_operands(li, ri)) [[unlikely]]
        return detail::utf8_rewrapped(evaluate_arithmetic(detail::utf8_unwrapped(left, engine_), op, detail::utf8_unwrapped(right, engine_), compound), engine_);
    if (detail::char_operands_promote(li, ri)) [[unlikely]]
        return evaluate_arithmetic(detail::char_promoted(left, engine_), op, detail::char_promoted(right, engine_), compound);

    // Special case for string concatenation (use move to avoid copying the temporary)
    // Check for to_string() method on objects before falling back to default
    if (op == token_type::plus && (li == script_value::TYPEID_STRING || ri == script_value::TYPEID_STRING)) {
        return concat_with_methods(left, right);
    }

    // Fast path for pure integer arithmetic (avoid float conversion)
    if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
        script_int leftInt = left.unchecked_as_int();
        script_int rightInt = right.unchecked_as_int();
        script_int rr;

        switch (op) {
            case token_type::plus:
                if (!ints::try_add(leftInt, rightInt, rr)) return int_overflow_sv("Integer overflow in '+'");
                return make_value(rr);
            case token_type::minus:
                if (!ints::try_sub(leftInt, rightInt, rr)) return int_overflow_sv("Integer overflow in '-'");
                return make_value(rr);
            case token_type::star:
                if (!ints::try_mul(leftInt, rightInt, rr)) return int_overflow_sv("Integer overflow in '*'");
                return make_value(rr);
            case token_type::slash:
                if (rightInt == 0) {
                    return checked_result<script_value>(
                        make_error_code(runtime_error_code::division_by_zero),
                        "Division by zero");
                }
                // Integer division returns integer (C++ semantics)
                if (!ints::try_div(leftInt, rightInt, rr)) return int_overflow_sv("Integer overflow in '/'");
                return make_value(rr);
            case token_type::percent:
                if (rightInt == 0) {
                    return checked_result<script_value>(
                        make_error_code(runtime_error_code::modulo_by_zero),
                        "Modulo by zero");
                }
                return make_value(ints::mod(leftInt, rightInt));
            default:
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::unknown_operator),
                    "Unknown arithmetic operator");
        }
    }

    // Mixed or floating point arithmetic path
    script_float leftNum, rightNum;

    if (li == script_value::TYPEID_INT) {
        leftNum = static_cast<script_float>(left.unchecked_as_int());
    } else if (li == script_value::TYPEID_FLOAT) {
        leftNum = left.unchecked_as_float();
    } else {
        if (detail::math::owns_arithmetic(li, ri)) [[unlikely]]
            return detail::math::binary(detail::math::bop_of(op), left, right, engine_);
        if (compound >= 0) { return detail::compound_mismatch(static_cast<size_t>(compound), left, right, engine_); }
        return checked_result<script_value>(
            make_error_code(runtime_error_code::invalid_numeric_operand),
            "Left operand must be numeric");
    }

    if (ri == script_value::TYPEID_INT) {
        rightNum = static_cast<script_float>(right.unchecked_as_int());
    } else if (ri == script_value::TYPEID_FLOAT) {
        rightNum = right.unchecked_as_float();
    } else {
        if (detail::math::owns_arithmetic(li, ri)) [[unlikely]]
            return detail::math::binary(detail::math::bop_of(op), left, right, engine_);
        if (compound >= 0) { return detail::compound_mismatch(static_cast<size_t>(compound), left, right, engine_); }
        return checked_result<script_value>(
            make_error_code(runtime_error_code::invalid_numeric_operand),
            "Right operand must be numeric");
    }

    switch (op) {
        case token_type::plus:
            return make_value(leftNum + rightNum);
        case token_type::minus:
            return make_value(leftNum - rightNum);
        case token_type::star:
            return make_value(leftNum * rightNum);
        case token_type::slash:
            if (rightNum == 0.0) {
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::division_by_zero),
                    "Division by zero");
            }
            return make_value(leftNum / rightNum);
        case token_type::percent:
            if (rightNum == 0.0) {
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::modulo_by_zero),
                    "Modulo by zero");
            }
            return make_value(std::fmod(leftNum, rightNum));
        default:
            return checked_result<script_value>(
                make_error_code(runtime_error_code::unknown_operator),
                "Unknown arithmetic operator");
    }
}


// Placeholder implementations for remaining visitors
template<bool Replaying>
checked_result<void> interpreter::visit_call_expr_t(call_expr* expr, const replay_point* rs) {
    // math:: language intrinsics (detail/math_intrinsics.hpp): evaluate args, one
    // shared-kernel call - no namespace lookup, no call machinery (KEEP BYTE-PARALLEL
    // with vm_backend::exec_math; parity by the shared kernel).
    if (expr->callee->get_type() == node_type::member_expr) {
        auto* member_callee = static_cast<member_expr*>(expr->callee.get());
        if (member_callee->is_static) {
            const detail::math_fn fn = detail::math_intrinsic_for_call(member_callee);
            if (fn != detail::math_fn::none && expr->arguments.size() <= 8) {
                const size_t argc = expr->arguments.size();
                // the operands leave the value stack as they are evaluated: a later operand's call runs statements, which
                // drop what the stack holds at their boundaries
                std::optional<script_value> margs[8];
                size_t math_start = 0;
                if constexpr (Replaying) {
                    // args evaluated before the yield are restored in order, never re-run
                    if (rs) { for (const auto& v : rs->partial) margs[math_start++].emplace(v); }
                }
                for (size_t ai = math_start; ai < argc; ++ai) {
                    auto stacked = [&]() {   // the args already evaluated, in evaluation order (cold: park only)
                        std::vector<script_value> done;
                        for (size_t k = 0; k < ai; ++k) done.push_back(*margs[k]);
                        return done;
                    };
                    JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->arguments[ai].get(), rs, ai), expr, rp_call_math, ai, stacked());
                    margs[ai].emplace(pop_value());
                    if (is_unwinding_) {
                        push_value(make_value());
                        return {};
                    }
                    capture_call_argument(expr->arg_captures, ai, *margs[ai], [] { return detail::arg_binding::opaque; });
                }
                const script_value* argp[8] = {};
                for (size_t i = 0; i < argc; ++i) {
                    argp[i] = &margs[i]->deref();
                }
                auto result = detail::eval_math_intrinsic(fn, argp, argc, engine_, engine_->math_rng());
                if (!result) {
                    return result.error_value();
                }
                push_value(std::move(result.value()));
                return {};
            }
        }
    }

    // Special handling for weak_from_this() and shared_from_this()
    if (expr->callee->get_type() == node_type::identifier_expr) {
        auto* ident_expr = static_cast<identifier_expr*>(expr->callee.get());
        // Use interned symbol IDs for fast comparison
        if (ident_expr->symbol_id == weak_from_this_id_ || ident_expr->symbol_id == shared_from_this_id_) {
            // These functions take no arguments
            if (!expr->arguments.empty()) {
                return checked_result<void>(make_error_code(runtime_error_code::argument_count_mismatch),
                    "{0}() takes no arguments", ident_expr->symbol_id);
            }

            // Get 'this' from the current environment
            script_value* this_ptr = environment_->get_value_ptr(string_symbolizer_->get_this_id());
            if (!this_ptr) {
                return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                    "{0}() can only be called from within a method", ident_expr->symbol_id);
            }
            script_value this_val = *this_ptr;
            if (!this_val.is_object()) {
                return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                    "{0}() can only be called from within a method", ident_expr->symbol_id);
            }

            if (ident_expr->symbol_id == weak_from_this_id_) {
                // Create a weak_ptr from the 'this' object
                auto weak_result = script_value::make_weak_ptr(this_val, engine_);
                if (!weak_result) {
                    return weak_result.error_value();
                }
                push_value(std::move(weak_result.value()));
                return {};
            } else {  // shared_from_this
                // Just return the shared_ptr (which is already 'this')
                push_value(std::move(this_val));
                return {};
            }
        }
    }

    // Identifier-receiver string builtins mutate the VARIABLE in place: the receiver
    // resolves through the identifier ladder (locals -> members -> globals, ::global)
    // and the builtin lands on that cell.
    if (expr->callee->get_type() == node_type::member_expr) {
        auto* member = static_cast<member_expr*>(expr->callee.get());
        if (!member->is_static && member->object->get_type() == node_type::identifier_expr) {
            auto* ident = static_cast<identifier_expr*>(member->object.get());
            script_value* recv_cell = string_receiver_cell(ident);
            const bool recv_utf8 = recv_cell && recv_cell->is_utf8_string();
            if (recv_cell && (recv_utf8 || recv_cell->is_string())) {
                const auto& table = recv_utf8 ? utf8_methods_ : string_methods_;
                auto methodIt = table.find(member->member_id);
                if (methodIt != table.end()) {
                    // C++17 [expr.call]: the receiver resolves before the arguments. An
                    // argument that runs code may rebind or retype the variable, so the
                    // cell is re-resolved after them; a cell that no longer holds a string
                    // runs the builtin on the pre-args value (KEEP BYTE-PARALLEL with
                    // vm_backend::exec_call_method's post-args probe + stack-copy fallback)
                    std::optional<script_value> pre_args;
                    for (const auto& arg : expr->arguments) {
                        const auto arg_type = arg->get_type();
                        if (arg_type != node_type::literal_expr && arg_type != node_type::identifier_expr &&
                            arg_type != node_type::this_expr) {
                            pre_args.emplace(recv_cell->deref());
                            break;
                        }
                    }
                    std::vector<script_value> arguments;
                    arguments.reserve(expr->arguments.size());
                    if constexpr (Replaying) { if (rs) arguments = rs->partial; }
                    for (size_t ai = arguments.size(); ai < expr->arguments.size(); ++ai) {
                        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->arguments[ai].get(), rs, ai), expr, rp_call_string, ai, arguments);
                        arguments.emplace_back(std::move(pop_value()));
                        if (is_unwinding_) {
                            push_value(make_value());
                            return {};
                        }
                        capture_call_argument(expr->arg_captures, ai, arguments.back(), [] { return detail::arg_binding::opaque; });
                    }
                    if (pre_args) {
                        recv_cell = string_receiver_cell(ident);
                        if (recv_cell && (recv_utf8 ? recv_cell->is_utf8_string() : recv_cell->is_string())) {
                            pre_args.reset();   // a live copy would only make the write gate copy the node
                        } else {
                            recv_cell = &*pre_args;
                        }
                    }

                    // Resolve through references (ref decls/cells): string builtins read
                    // self's storage RAW, and in-place mutation must land in the target; a
                    // scratch-backed alias (typed element, C++ property) writes back after
                    // (KEEP BYTE-PARALLEL with vm_backend::exec_call_method)
                    script_value& var_ref = recv_cell->deref();
                    detail::deref_builtin_args_in_place(arguments);
                    auto result = methodIt->second(builtin_ctx(), var_ref, arguments);
                    if (!result) {
                        return result.error_value();
                    }
                    if (auto* scratch_ref = recv_cell->scratch_ref_holder()) [[unlikely]] { scratch_ref->commit_scratch(); }
                    push_value(std::move(result.value()));
                    return {};
                }
            }
        }
    }

    // Direct script-method dispatch: callee is obj.method on a pure receiver expression
    // (identifier/this - re-evaluable without side effects, so a gate miss can fall to
    // the generic path below). Mirrors vm exec_call_method's fast-entry precedence gate
    // (same_as/super/coroutine/sp-builtin/access/getter-probe/field), skipping the
    // bound-method mint, its this+args re-copies, and the carrier method env. A per-site
    // monomorphic inline cache (member_expr::mcache_*, validated by class identity +
    // method_epoch) skips lookup + overload resolution for the hot monomorphic shape.
    if (expr->callee->get_type() == node_type::member_expr) {
        auto* member = static_cast<member_expr*>(expr->callee.get());
        const auto recv_type = member->object->get_type();
        if (!member->is_static && member->member_id != same_as_id_ &&
            (recv_type == node_type::identifier_expr || recv_type == node_type::this_expr)) {
            JAISCRIPT_TRY(dispatch_expr(member->object.get()));
            script_value objectValue = pop_value();
            if (is_unwinding_) {
                push_value(make_value());
                return {};
            }
            objectValue = objectValue.deref();

            const script_method_dispatch* dispatch = nullptr;
            std::shared_ptr<environment> def_env;
            std::shared_ptr<class_definition> cls_pin;
            std::shared_ptr<function_decl> parallel_pin_decl;
            class_definition* cls_raw = nullptr;
            bool ic_route = false;

            if (objectValue.is_object()) {
                auto holder = objectValue.get_object_holder();
                if (holder && holder->type_id != coroutine_handle_type_id_) {
                    class_instance* inst_raw = nullptr;
                    if (holder->is_class_instance_wrapper && holder->data) {
                        inst_raw = static_cast<class_instance*>(holder->data.get());
                        cls_raw = inst_raw->get_class_definition();
                    }
                    // Worker method wall → admission verdict (parallel-method-admission
                    // ruling): barrier-admitted methods dispatch through pinned
                    // worker-private state (fresh dispatcher value, pre-resolved overload —
                    // no shared count, cache, or IC is ever touched); anything unpinned
                    // keeps the wall error. KEEP the verdict text BYTE-PARALLEL with vm
                    // exec_call_method's twin.
                    const detail::parallel_method_pin* worker_pin = (parallel_worker_ && cls_raw && parallel_method_pins_)
                        ? parallel_method_pins_->find(cls_raw, member->member_id) : nullptr;
                    if (parallel_worker_ && cls_raw && !worker_pin) [[unlikely]] {
                        return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                            "method '{0}' was not admitted in this parallel body (admitted: single-overload "
                            "public methods on the element's own class touching only element, local, or "
                            "captured-read state)", member->member_id);
                    }
                    if (worker_pin) [[unlikely]] {
                        const detail::parallel_method_pin* pin = worker_pin;
                        dispatch = pin->method_value.as_function().target<script_method_dispatch>();
                        def_env = dispatch->definition_env;
                        cls_pin = dispatch->cls;
                        parallel_pin_decl = pin->resolved;
                    } else if (cls_raw && member->mcache_cls == cls_raw &&
                        member->mcache_epoch == cls_raw->method_epoch() &&
                        !((member->mcache_flags & 2) && objectValue.get_type_info() &&
                          objectValue.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) &&
                        !inst_raw->has_field(member->member_id)) {
                        // IC hit: the per-call precedence checks that stay dynamic ran
                        // (sp-builtin shadowing, field); access enforcement stays per call
                        if (member->mcache_flags & 1) [[unlikely]] {
                            JAISCRIPT_TRY(detail::enforce_member_access(cls_raw, member->member_id,
                                                                        environment_->find_access_context()));
                        }
                        dispatch = static_cast<const script_method_dispatch*>(member->mcache_dispatch);
                        // Owning copies BEFORE argument evaluation: an argument expression
                        // can hot reload the class and replace the dispatcher under us
                        def_env = dispatch->definition_env;
                        cls_pin = dispatch->cls;
                        ic_route = true;
                    } else {
                        // Full gate (fills the IC on success). A user class method WINS
                        // over a same-named builtin handle method (Dev ruling 2026-07): the
                        // sp-builtin only applies when the class does NOT define the method.
                        const bool sp_builtin = objectValue.get_type_info() &&
                            objectValue.get_type_info()->base_type == script_value_type::jai_shared_ptr_type &&
                            shared_ptr_methods_.find(member->member_id) != shared_ptr_methods_.end() &&
                            !(cls_raw && cls_raw->defines_method(member->member_id));
                        if (!sp_builtin) {
                            auto target = resolve_member_target(objectValue);
                            if (target && target.class_def) {
                                if (target.class_def->chain_has_nonpublic()) [[unlikely]] {
                                    JAISCRIPT_TRY(detail::enforce_member_access(target.class_def, member->member_id,
                                                                                environment_->find_access_context()));
                                }
                                bool getter_shadows = false;
                                if (target.class_def->has_property_getters()) {
                                    uint64_t getter_id = member->getter_id;
                                    if (getter_id == UINT64_MAX) {
                                        auto [id, _] = string_symbolizer_->get_getter_id_with_view(member->member_id);
                                        getter_id = id;
                                        member->getter_id = getter_id;
                                    }
                                    script_value getter_probe = target.method(getter_id);
                                    getter_shadows = !getter_probe.is_null() && !getter_probe.is_invalid() && getter_probe.is_function();
                                }
                                if (!getter_shadows && !target.has_field(member->member_id)) {
                                    script_value method_val = target.method(member->member_id);
                                    if (method_val.is_function()) {
                                        const auto* d = method_val.as_function().target<script_method_dispatch>();
                                        if (d && d->eng == engine_) {
                                            dispatch = d;
                                            def_env = d->definition_env;
                                            cls_pin = d->cls;
                                            // Fill the IC: single-overload methods with an
                                            // all-primitive/any exact-argc signature
                                            if (cls_raw && target.class_def == cls_raw) {
                                                if (function_decl* lone = cls_pin->single_method_overload(d->name_id)) {
                                                    const auto& params = lone->parameters;
                                                    bool eligible = params.size() <= 8 && params.size() == expr->arguments.size() &&
                                                                    lone->body && !lone->is_coroutine;
                                                    uint8_t sig[8] = {};
                                                    for (size_t i = 0; eligible && i < params.size(); ++i) {
                                                        const auto& p = params[i];
                                                        if (p.is_reference || p.ref_escaping) { eligible = false; break; }
                                                        const bool untyped = !p.type || p.type->type_name.empty() ||
                                                                             p.type->type_name == "any" ||
                                                                             p.type->base_type == script_value_type::jai_any_type;
                                                        if (untyped) { sig[i] = 0xFF; continue; }
                                                        switch (p.type->base_type) {
                                                            case script_value_type::jai_int_type:    sig[i] = script_value::TYPEID_INT; break;
                                                            case script_value_type::jai_float_type:  sig[i] = script_value::TYPEID_FLOAT; break;
                                                            case script_value_type::jai_bool_type:   sig[i] = script_value::TYPEID_BOOL; break;
                                                            case script_value_type::jai_string_type: sig[i] = script_value::TYPEID_STRING; break;
                                                            case script_value_type::jai_char_type:   sig[i] = script_value::TYPEID_CHAR; break;
                                                            default: eligible = false; break;
                                                        }
                                                    }
                                                    if (eligible) {
                                                        member->mcache_cls = cls_raw;
                                                        member->mcache_epoch = cls_raw->method_epoch();
                                                        member->mcache_dispatch = d;
                                                        member->mcache_decl = lone;
                                                        for (size_t i = 0; i < 8; ++i) member->mcache_sig[i] = sig[i];
                                                        member->mcache_argc = static_cast<uint8_t>(params.size());
                                                        member->mcache_flags = static_cast<uint8_t>(
                                                            (cls_raw->chain_has_nonpublic() ? 1 : 0) |
                                                            (shared_ptr_methods_.find(member->member_id) != shared_ptr_methods_.end() ? 2 : 0));
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (dispatch) {
                // Evaluate arguments (same order/unwind semantics as the generic loop)
                std::vector<script_value> arguments;
                arguments.reserve(expr->arguments.size());
                // Replay: the receiver and method resolution above are pure (identifier/this
                // receiver), so they re-run; the args evaluated before the yield are parked
                if constexpr (Replaying) { if (rs) arguments = rs->partial; }
                for (size_t ai = arguments.size(); ai < expr->arguments.size(); ++ai) {
                    const auto& argExpr = expr->arguments[ai];
                    try {
                        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(argExpr.get(), rs, ai), expr, rp_call_direct, ai, arguments);
                        arguments.emplace_back(std::move(pop_value()));
                        if (is_unwinding_) {
                            push_value(make_value());
                            return {};
                        }
                        capture_call_argument(expr->arg_captures, ai, arguments.back(), [&] {
                            return parallel_pin_decl ? detail::parameters_binding(parallel_pin_decl->parameters, ai)
                                                     : detail::method_binding(cls_pin.get(), dispatch->name_id, ai, expr->arguments.size());
                        });
                    } catch (const std::runtime_error& e) {
                        return raise_native_exception(e);
                    }
                }

                function_decl* direct_ast = nullptr;
                std::shared_ptr<function_decl> resolved_pin;
                if (parallel_pin_decl) {
                    // Barrier-resolved: no overload resolution, no shared-cache touch
                    resolved_pin = parallel_pin_decl;
                    direct_ast = resolved_pin.get();
                }
                if (!direct_ast && ic_route && cls_raw && member->mcache_epoch == cls_raw->method_epoch()) {
                    // Post-args revalidation passed (an argument expression could have hot
                    // reloaded the class). The signature check replaces overload resolution:
                    // exact primitive/any match on a lone overload is what the scorer picks.
                    bool sig_ok = arguments.size() == member->mcache_argc;
                    for (size_t i = 0; sig_ok && i < arguments.size(); ++i) {
                        const uint8_t want = member->mcache_sig[i];
                        if (want != 0xFF && arguments[i].raw_storage_index() != want) {
                            sig_ok = false;
                        }
                    }
                    if (sig_ok) {
                        direct_ast = static_cast<function_decl*>(member->mcache_decl);
                    }
                }
                if (!direct_ast) {
                    if (ic_route && cls_raw && member->mcache_epoch != cls_raw->method_epoch()) {
                        // Class changed under the arguments: refetch the live dispatcher
                        auto target = resolve_member_target(objectValue);
                        script_value method_val = target ? target.method(member->member_id)
                                                         : script_value::make_invalid(engine_);
                        const script_method_dispatch* d = method_val.is_function()
                            ? method_val.as_function().target<script_method_dispatch>() : nullptr;
                        if (!d || d->eng != engine_) {
                            const std::string_view cls = target.class_name();
                            std::string text = (cls.empty() ? std::string("object") : std::string(cls)) + " has no member '" + std::string(member->member) + "'";
                            active_exception_value_ = make_value(text);
                            current_exception_ = script_exception(text, member->location);
                            is_unwinding_ = true;
                            push_value(make_value());
                            return {};
                        }
                        dispatch = d;
                        def_env = d->definition_env;
                        cls_pin = d->cls;
                    }
                    // Slow resolution: exactly what the dispatcher would do, with the
                    // dispatcher's identical error object on failure (and no second
                    // evaluation of side-effecting arguments)
                    auto resolved = cls_pin->resolve_method_overload(dispatch->name_id, arguments);
                    if (!resolved) {
                        return resolved.error_value();
                    }
                    resolved_pin = resolved.value();
                    if (resolved_pin->body && resolved_pin->is_coroutine) {
                        // Coroutine method: mint the handle through the same callable
                        // seam the dispatcher uses (args already evaluated once)
                        script_callable payload;
                        payload.kind = script_callable::kind_type::method;
                        payload.ast = resolved_pin;
                        payload.cls = cls_pin;
                        payload.definition_env = def_env;
                        payload.this_obj = objectValue;
                        auto coro_result = execute_callable(payload, arguments);
                        if (!coro_result) {
                            return coro_result.error_value();
                        }
                        push_value(std::move(coro_result.value()));
                        return {};
                    }
                    if (resolved_pin->body) {
                        direct_ast = resolved_pin.get();
                    }
                }
                if (direct_ast) {
                    script_defined_function script_func(
                        direct_ast->name,
                        shared_parameters_for(direct_ast),
                        direct_ast->return_type,
                        direct_ast->body,
                        nullptr,
                        direct_ast->local_count);
                    script_func.unit = direct_ast->unit;
                    method_call_override mctx{&def_env, &objectValue, cls_pin.get()};
                    detail::call_site_context ctx{&expr->arguments, expr};
                    const detail::call_site_context* saved_ctx = pending_call_ctx_;
                    pending_call_ctx_ = expr->arguments.empty() ? saved_ctx : &ctx;
                    std::optional<checked_result<script_value>> callOutcome;
                    try {
                        callOutcome.emplace(call_function(script_func, arguments, &mctx));
                    } catch (const std::exception& e) {
                        pending_call_ctx_ = saved_ctx;
                        return raise_native_exception(e);
                    }
                    pending_call_ctx_ = saved_ctx;
                    checked_result<script_value>& result_checked = *callOutcome;
                    if (!result_checked) {
                        return result_checked.error_value();
                    }
                    // Non-owning method results (C++ chaining surfacing through a script
                    // method) pin the receiver, as make_bound_method does
                    script_value& rv = result_checked.value();
                    if (objectValue.is_object() && rv.is_non_owning_object()) {
                        auto rv_holder = rv.get_object_holder();
                        auto recv_holder = objectValue.get_object_holder();
                        if (rv_holder && recv_holder) {
                            rv_holder->keep_alive = recv_holder->data ? recv_holder->data
                                                                      : recv_holder->keep_alive;
                        }
                    }
                    push_value(std::move(result_checked.value()));
                    return {};
                }
                // body-less resolution: fall through to the generic path
            }
            // Gate miss: fall through to the generic path (receiver expression is pure,
            // so re-evaluating the full callee is side-effect-free)
        }
    }

    // Evaluate the callee expression (a member callee's lookup errors take the call shape).
    // Replay: partial[0] is the callee resolved BEFORE the yield - the parked callee is the
    // one invoked, exactly like the vm's pending_callee (callee_first_rebind_in_args).
    script_value callee;
    if (parked<Replaying>(rs, 0)) {
        callee = rs->partial[0];
    } else {
        member_callee_lookup_ = expr->callee->get_type() == node_type::member_expr;
        identifier_callee_lookup_ = expr->callee->get_type() == node_type::identifier_expr;
        auto callee_result = eval_child<Replaying>(expr->callee.get(), rs, 0);
        member_callee_lookup_ = false;
        identifier_callee_lookup_ = false;
        if (!callee_result) [[unlikely]] {
            if (is_yield_signal(callee_result)) park_operands(expr, rp_call_generic, 0, {});
            return callee_result.error_value();
        }
        callee = pop_value();
        if (is_unwinding_) {
            push_value(make_value());
            return {};
        }
    }

    // Handle null-safe method calls: obj?.method() returns null if obj was null
    if (callee.is_null() && expr->callee->get_type() == node_type::member_expr) {
        auto* member = static_cast<member_expr*>(expr->callee.get());
        if (member->null_safe) {
            push_value(make_value());  // push null
            return {};
        }
    }

    // Check if the callee is a function
    if (!callee.is_function()) {
        if (script_value call_operator = call_operator_of(callee); call_operator.is_function()) {
            callee = std::move(call_operator);
        } else {
            const script_value& callee_value = callee.deref();
            auto callee_target = resolve_member_target(callee_value);
            auto text = not_callable_diagnostic(callee_name_symbol(expr->callee.get()), callee_value.type(), base_type_ids_[callee_value.type()],
                                                callee_target.class_def ? std::string_view(callee_target.class_def->get_name()) : std::string_view{},
                                                callee_target.class_def ? callee_target.class_def->get_type_id() : 0);
            return checked_result<void>(make_error_code(runtime_error_code::not_a_function), text.message, text.sym0, text.sym1);
        }
    }
    
    // Use a local vector for arguments to avoid issues with nested calls
    std::vector<script_value> arguments;
    arguments.reserve(expr->arguments.size());
    // Replay ordinals: 0 is the callee, argument i is i+1 (partial[0] is the parked callee)
    if constexpr (Replaying) { if (rs && rs->index >= 1) arguments.assign(rs->partial.begin() + 1, rs->partial.end()); }

    for (size_t ai = arguments.size(); ai < expr->arguments.size(); ++ai) {
        const auto& argExpr = expr->arguments[ai];
        // Evaluate argument with exception handling
        try {
            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(argExpr.get(), rs, ai + 1), expr, rp_call_generic, ai + 1,
                                  parked_with_head(callee, arguments));
            arguments.emplace_back(std::move(pop_value()));
            if (is_unwinding_) {
                push_value(make_value());
                return {};
            }
            capture_call_argument(expr->arg_captures, ai, arguments.back(), [&] { return detail::callee_binding(callee, ai, false, expr->arguments.size()); });
        } catch (const std::runtime_error& e) {
            return raise_native_exception(e);
        }
    }

    // Call the function - now returns checked_result instead of throwing. C++ callables
    // may still throw (conversion failures, game-side exceptions) - convert those to
    // script exceptions here so nothing propagates raw through the interpreter.
    // The call-site context rides a single save/restored pointer: the next call_function
    // entered consumes it to bind reference parameters against the caller's variables
    // (the arg expressions outlive the call - they live in this frame).
    // Own-trampoline fast path (vm invoke_callee parity): a plain script-function thunk
    // dispatches straight into THIS backend's call machinery - skips std::function +
    // the engine-backend hop, and keeps parallel-worker interpreters on their own
    // instance (the thunk's operator() routes to the ENGINE's backend, which is the
    // wrong one inside a provisioned worker).
    const script_function& func = callee.as_function();
    const auto* callee_thunk = func.target<script_callable_thunk>();
    const bool direct_script_call = callee_thunk && callee_thunk->eng == engine_ &&
        callee_thunk->payload.kind == script_callable::kind_type::function && callee_thunk->payload.fn;
    detail::call_site_context ctx{&expr->arguments, expr};
    const detail::call_site_context* saved_ctx = pending_call_ctx_;
    pending_call_ctx_ = expr->arguments.empty() ? saved_ctx : &ctx;
    std::optional<checked_result<script_value>> callOutcome;
    try {
        if (direct_script_call) {
            callOutcome.emplace(call_function(*callee_thunk->payload.fn, arguments));
        } else {
            callOutcome.emplace(func(arguments));
        }
    } catch (const std::exception& e) {
        pending_call_ctx_ = saved_ctx;
        return raise_native_exception(e);
    }
    pending_call_ctx_ = saved_ctx;
    checked_result<script_value>& result_checked = *callOutcome;

    // Check if function call succeeded
    if (!result_checked) {
        // Function returned an error via checked_result - propagate it
        return result_checked.error_value();
    }

    // Push successful result onto the stack
    push_value(std::move(result_checked.value()));
    return {};
}

template<bool Replaying>
checked_result<void> interpreter::visit_member_expr_t(member_expr* expr, const replay_point* rs) {
    // Consumed at entry: a nested receiver's own member reads are accesses
    const bool as_callee = member_callee_lookup_;
    member_callee_lookup_ = false;
    // Check if this is a static member access (::)
    if (expr->is_static) {
        // For static access, the object should be an identifier (class or namespace name)
        identifier_expr* ident_expr = nullptr;
        if (expr->object->get_type() == node_type::identifier_expr) {
            ident_expr = static_cast<identifier_expr*>(expr->object.get());
        }
        // Handle nested namespace access: outer::inner::getValue()
        // Build the full namespace path
        std::string name;
        uint64_t name_id;

        if (ident_expr) {
            // Simple case: just an identifier
            name = ident_expr->name;
            name_id = ident_expr->symbol_id;

            // Cache symbol ID if not already done
            if (name_id == UINT64_MAX) {
                name_id = string_symbolizer_->intern(name);
                ident_expr->symbol_id = name_id;
            }
        } else if (expr->object->get_type() == node_type::member_expr) {
            auto* member_expr_obj = static_cast<member_expr*>(expr->object.get());
            // Nested namespace: outer::inner where the object is "outer::inner" (a member_expr)
            // Recursively build the full path
            std::function<std::string(expression*)> build_namespace_path = [&](expression* e) -> std::string {
                if (e->get_type() == node_type::identifier_expr) {
                    return std::string(static_cast<identifier_expr*>(e)->name);
                } else if (e->get_type() == node_type::member_expr) {
                    auto* member = static_cast<member_expr*>(e);
                    if (member->is_static) {
                        // This is a :: access, continue building the path
                        return build_namespace_path(member->object.get()) + "::" + std::string(member->member);
                    }
                }
                return "";
            };

            name = build_namespace_path(expr->object.get());
            if (name.empty()) {
                return checked_result<void>(make_error_code(jai::runtime_error_code::type_mismatch));  // [ErrorText] Invalid namespace path
            }
            name_id = string_symbolizer_->intern(name);
        } else {
            // Static member access requires an identifier or namespace path
            return checked_result<void>(make_error_code(jai::runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        // PRIORITY 1: Check for namespace with this name FIRST
        // Namespaces can override class static methods
        auto& namespaces = engine_->script_namespaces();
        auto ns_it = namespaces.find(name_id);
        bool is_namespace = (ns_it != namespaces.end());

        // PRIORITY 1.5: Special case for namespace::class::static_member
        // ONLY if name is NOT a namespace itself
        // For "my::nested::cat::meow()", name would be "my::nested::cat" which is NOT a namespace
        // We need to detect this pattern and split it into namespace="my::nested" and class="cat"
        if (!is_namespace && name.find("::") != std::string::npos) {
            size_t last_colon = name.rfind("::");
            std::string potential_ns = name.substr(0, last_colon);
            std::string potential_class = name.substr(last_colon + 2);

            uint64_t ns_id = string_symbolizer_->intern(potential_ns);
            uint64_t class_id = string_symbolizer_->intern(potential_class);

            auto ns_check = namespaces.find(ns_id);
            if (ns_check != namespaces.end()) {
                auto class_check = ns_check->second->classes.find(class_id);
                if (class_check != ns_check->second->classes.end()) {
                    // Found! This is namespace::class, and we're accessing a static member
                    auto class_def = class_check->second;

                    // Try to get the static method (use pre-computed member_id from parser)
                    script_value static_method = class_def->get_static_method(expr->member_id, false);
                    if (!static_method.is_null()) {
                        push_value(std::move(static_method));
                        return {};
                    }

                    // Method not found - might be trying to access the class constructor
                    // Fall through to handle namespace::class access
                }
            }
        }

        // PRIORITY 2: Handle namespace members
        if (is_namespace) {
            auto& ns_data = ns_it->second;

            // Use parser's pre-computed member ID (always set by parser)
            // Look for function in namespace (handles overloads by arity)
            // This will be called later with arguments, so we need to return a callable
            auto func_it = ns_data->functions.find(expr->member_id);
            if (func_it != ns_data->functions.end()) {
                // Create a namespace function wrapper
                // Store all overloads for arity-based dispatch
                auto overloads = func_it->second;

                // Check if there's also a class with the same name (for fallback)
                std::shared_ptr<class_definition> fallback_class;
                auto [fallback_class_var_id, fallback_class_var_name] = string_symbolizer_->get_class_var_id_with_view(name_id);
                auto class_var_result = environment_->get(fallback_class_var_id);
                if (class_var_result) {
                    script_value class_var = std::move(class_var_result.value());
                    if (class_var.is_object()) {
                        auto objHolder = class_var.get_object_holder();
                        if (objHolder && objHolder->type_name == "class_definition") {
                            fallback_class = std::static_pointer_cast<class_definition>(objHolder->data);
                        }
                    }
                }
                // No class with this name - that's fine, fallback_class remains null

                // Create a script_function that dispatches based on arity.
                // Captures engine* + payload only; the namespace registry is read through the
                // engine at call time and the body runs through the live backend.
                uint64_t namespace_id = name_id;
                uint64_t member_id = expr->member_id;
                engine* eng = engine_;
                script_function namespace_func = [eng, overloads, namespace_id, fallback_class, member_id](const std::vector<script_value>& args) -> checked_result<script_value> {
                    execution_backend* backend = eng ? eng->get_execution_backend() : nullptr;
                    if (!backend) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::engine_destroyed), "Engine backend unavailable");
                    }

                    // Find matching overload by arity in namespace (trailing defaults
                    // open the window; the candidate using fewest defaults wins)
                    std::shared_ptr<function_decl> ns_pick;
                    size_t ns_defaults = SIZE_MAX;
                    for (const auto& fd : overloads) {
                        if (arity_accepts(fd->parameters, args.size()) &&
                            fd->parameters.size() - args.size() < ns_defaults) {
                            ns_pick = fd;
                            ns_defaults = fd->parameters.size() - args.size();
                        }
                    }
                    {
                        const auto& func_decl = ns_pick;
                        if (func_decl) {
                            // The namespace's scope is the body's environment (its cells, then the globals);
                            // host-registered entries stay visible by their bare names through a layer above it
                            auto& namespaces = eng->script_namespaces();
                            auto ns_it = namespaces.find(namespace_id);
                            std::shared_ptr<environment> ns_env = ns_it != namespaces.end() && ns_it->second
                                ? detail::namespace_scope(*eng, *ns_it->second)
                                : std::make_shared<environment>(eng->get_global_environment(), eng->get_symbolizer());
                            if (ns_it != namespaces.end() && ns_it->second && !ns_it->second->variables.empty()) {
                                auto host_layer = std::make_shared<environment>(ns_env, eng->get_symbolizer());
                                for (const auto& [var_id, var_value] : ns_it->second->variables) {
                                    host_layer->define(var_id, var_value);
                                }
                                ns_env = std::move(host_layer);
                            }

                            // Found matching arity - create script_defined_function with namespace environment
                            script_callable payload;
                            payload.kind = script_callable::kind_type::function;
                            payload.fn = std::make_shared<script_defined_function>(
                                func_decl->name,
                                shared_parameters_for(func_decl.get()),
                                func_decl->return_type,
                                func_decl->body,
                                ns_env,  // Environment with namespace variables
                                func_decl->local_count
                            );
                            payload.fn->unit = func_decl->unit;
                            return backend->execute_callable(payload, args);
                        }
                    }

                    // No matching overload found in namespace - try fallback to class static method
                    if (fallback_class) {
                        // get_static_method with false doesn't throw - returns null if not found
                        script_value static_method = fallback_class->get_static_method(member_id, false);
                        if (!static_method.is_null() && static_method.is_function()) {
                            // Call the class static method
                            auto func = static_method.as_function();
                            return func(args);
                        }
                    }

                    // Neither namespace nor class has matching method
                    (void)namespace_id;
                    return detail::overload_miss(make_error_code(runtime_error_code::not_a_function),
                        member_id, args.data(), args.size(), eng);
                };

                push_value(script_value::make_function(namespace_func, engine_));
                return {};
            }

            // Look for variable in namespace
            if (ns_data->scope) {
                if (script_value* cell = ns_data->scope->get_own_value_ptr(expr->member_id)) {
                    push_value(*cell);
                    return {};
                }
            }
            auto var_it = ns_data->variables.find(expr->member_id);
            if (var_it != ns_data->variables.end()) {
                push_value(var_it->second);
                return {};
            }

            // Look for class in namespace
            // Note: namespace::class::static_member is handled earlier (before namespace lookup)
            auto class_it = ns_data->classes.find(expr->member_id);
            if (class_it != ns_data->classes.end()) {
                // Found a class in the namespace - return the constructor
                auto ctor_result = environment_->get(expr->member_id);
                if (!ctor_result) {
                    return ctor_result.error_value();
                }
                push_value(std::move(ctor_result.value()));
                return {};
            }

            // Member not found in namespace - fall through to check class static methods
        }

        // PRIORITY 2: Look up the class definition (use cached ID)
        auto [class_var_id, class_var_view] = string_symbolizer_->get_class_var_id_with_view(name_id);
        auto class_var_result = environment_->get(class_var_id);
        if (!class_var_result) {
            if (engine_->script_namespaces().count(name_id)) {   // a namespace without that member: name the member, not the class fallback
                return checked_result<void>(make_error_code(runtime_error_code::undefined_variable), "Undefined variable '{0}::{1}'", name_id, expr->member_id);
            }
            // no namespace and no class of that name: the class is what `C::m` names (docs/diagnostics.md, undefined names)
            return checked_result<void>(make_error_code(runtime_error_code::class_not_found), "Class '{0}' not found", name_id);
        }
        script_value class_var = std::move(class_var_result.value());

        if (!class_var.is_object()) {
            // Not a class
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        // Extract the class definition
        auto objHolder = class_var.get_object_holder();
        if (!objHolder || objHolder->type_name != "class_definition") {
            // Not a valid class
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        auto class_def = std::static_pointer_cast<class_definition>(objHolder->data);

        if (class_def->chain_has_nonpublic()) [[unlikely]] {
            JAISCRIPT_TRY(detail::enforce_member_access(class_def.get(), expr->member_id,
                                                        environment_->find_access_context()));
        }

        // Try static method first (most common case for :: access)
        // get_static_method with false doesn't throw - returns null if not found
        script_value static_method = class_def->get_static_method(expr->member_id, false);
        if (!static_method.is_null()) {
            push_value(std::move(static_method));
            return {};
        }

        // Try static field access (presence, not null-ness: a typed null static reads null)
        if (const script_value* static_value = class_def->get_static_field_ptr(expr->member_id)) {
            push_value(*static_value);
            return {};
        }

        // Try getter method as fallback (for C++ bound properties)
        auto [getter_id, _] = string_symbolizer_->get_getter_id_with_view(expr->member_id);
        // get_static_method with false doesn't throw - returns null if not found
        script_value getter_method = class_def->get_static_method(getter_id, false);
        if (!getter_method.is_null() && getter_method.is_function()) {
            // Call the getter with no arguments to get the live C++ value
            auto func = getter_method.as_function();
            std::vector<script_value> no_args;
            auto result = func(no_args);
            if (!result) {
                // Function returned error - propagate it up
                return result.error_value();
            }
            script_value static_value = std::move(result.value());
            push_value(std::move(static_value));
            return {};
        }

        return checked_result<void>(make_error_code(runtime_error_code::static_member_not_found),
            "class {0} has no static member '{1}'", name_id, expr->member_id);
    }

    // Evaluate the object expression
    JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->object.get(), rs, 0), expr, rp_operand, 0, {});
    script_value objectValue = pop_value();
    if (is_unwinding_) {
        push_value(make_value());
        return {};
    }
    return member_read_on(expr, std::move(objectValue), as_callee);
}

// The member read on an already-evaluated receiver: the compound-member store evaluates the
// receiver once and holds it across the read, the rhs and the store-back
// A reference bound to a bare name that lands on an instance method (auto& r = heal, cast<T&>(heal)): the same violation, never
// an undefined variable. KEEP BYTE-PARALLEL with vm_backend::bare_method_reference
std::optional<checked_result<void>> interpreter::bare_method_reference_error(const identifier_expr* expr) {
    if (!member_scope_.active() || !member_scope_.this_storage) { return std::nullopt; }
    auto member = detail::probe_member_scope(member_scope_.state, member_scope_.this_storage, member_scope_.statics, expr->symbol_id,
                                             /*want_methods*/ true);
    if (member && member.what == detail::member_probe_hit::kind::method && member.instance) {
        return bare_method_value_error(expr, *member_scope_.this_storage);
    }
    return std::nullopt;
}

// A bare method name read without a call inside a method (Dev ruling 2026-09-30); KEEP BYTE-PARALLEL with
// vm_backend::raise_bare_method_value
checked_result<void> interpreter::bare_method_value_error(const identifier_expr* expr, const script_value& self) {
    if (parallel_worker_) [[unlikely]] {
        return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
            "'{0}' is a method: call it or bind it with reflect::bind", expr->symbol_id);
    }
    auto target = resolve_member_target(self.deref());
    const std::string owner = target.class_def ? target.class_def->get_name() : std::string("this");
    return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation), "{0}",
        string_symbolizer_->intern(detail::bare_method_value_text(expr->name, owner)));
}

// obj.method without a call (Dev ruling 2026-09-30): a method is called or bound, never read bare. KEEP BYTE-PARALLEL with
// vm_backend::raise_method_value
checked_result<void> interpreter::method_value_error(const member_expr* expr, const script_value& object, bool bindable) {
    if (parallel_worker_) [[unlikely]] {
        return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
            "'{0}' is a method: call it or bind it with reflect::bind", expr->member_id);
    }
    const script_value& v = object.deref();
    std::string owner;
    bool value_type = true;
    if (v.is_utf8_string()) { owner = "utf8_string"; }
    else if (v.is_string()) { owner = "string"; }
    else if (v.is_array()) { owner = "array"; }
    else if (v.is_map()) { owner = "map"; }
    else if (v.is_object() && v.get_object_holder() && v.get_object_holder()->type_id == coroutine_handle_type_id_) { owner = "coroutine"; }
    else {
        value_type = false;
        auto target = resolve_member_target(v);
        owner = target.class_def ? target.class_def->get_name() : std::string(base_type_spelling(v.type()));
    }
    return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation), "{0}",
        string_symbolizer_->intern(detail::method_value_text(expr->member, owner, detail::method_value_receiver(expr), value_type || !bindable)));
}

checked_result<void> interpreter::member_read_on(member_expr* expr, script_value objectValue, bool as_callee) {
    const bool is_super_access = expr->object->get_type() == node_type::super_expr;

    // A string MUTATOR called on a non-identifier receiver binds the receiver LVALUE
    // (detail/string_mutator_receiver.hpp; C++ `string& to_upper()`): the popped reference,
    // the resolved field / property, or the chain root whose node the receiver still is.
    // An identifier chain root re-resolves at call time, after the arguments, like the
    // identifier fast path. Unbound = a temporary: the builtin mutates the copy alone.
    // (KEEP BYTE-PARALLEL with vm_backend::exec_call_method's builtin-direct string branch)
    if (as_callee && (objectValue.deref().raw_storage_index() == script_value::TYPEID_STRING ||
                      objectValue.deref().raw_storage_index() == script_value::TYPEID_UTF8_STRING) &&
        string_mutator_ids_.count(expr->member_id)) {
        const builtin_method& method = (objectValue.deref().raw_storage_index() == script_value::TYPEID_UTF8_STRING
            ? utf8_methods_ : string_methods_).find(expr->member_id)->second;
        script_value copy(objectValue.deref());
        detail::string_mutator_binding binding = detail::bind_string_mutator_receiver(objectValue, expr->object.get(),
            detail::caller_frame_view{call_stack_.empty() ? nullptr : &call_stack_.back()},
            environment_.get(), engine_, string_symbolizer_);
        script_function boundMethod = [this, ctx = builtin_ctx(), method,
                                       binding = std::move(binding), copy = std::move(copy)]
                                      (const std::vector<script_value>& args) mutable -> checked_result<script_value> {
            script_value* root_cell = binding.root_ident ? string_receiver_cell(binding.root_ident) : nullptr;
            script_value* self = detail::string_mutator_self(binding, root_cell, copy);
            if (self) {
                copy = script_value(std::monostate{}, engine_);   // the copy's count would make the gate copy the node
            } else {
                self = &copy;
            }
            std::vector<script_value> scratch;
            auto result = method(ctx, *self, detail::deref_builtin_args(args, scratch));
            binding.commit();
            return result;
        };
        push_value(script_value::make_function(boundMethod, engine_));
        return {};
    }

    // Dereference if needed - subscript access returns references
    objectValue = objectValue.deref();

    // Handle null-safe member access (?.)
    if (expr->null_safe && objectValue.is_null()) {
        push_value(make_value());  // push null
        return {};
    }

    // A math value's component (x / width, y / height, z / depth; detail/math_values.hpp; KEEP BYTE-PARALLEL with
    // vm_backend::member_access_value and exec_probe_member)
    if (detail::math::is_kind(objectValue.raw_storage_index())) {
        const script_value_type kind = objectValue.unchecked_vec_kind();
        const int component = detail::math::component_of(kind, expr->member);
        if (component < 0) {
            if (const detail::math_fn method = detail::vec_method_fn(kind, expr->member); method != detail::math_fn::none) {
                push_value(detail::vec_bound_method(method, objectValue, engine_));
                return {};
            }
            return checked_result<void>(make_error_code(runtime_error_code::member_not_found), detail::math::no_member_text(kind), expr->member_id);
        }
        push_value(detail::math::component_value(objectValue, static_cast<size_t>(component), engine_));
        return {};
    }

    // Proven field read (docs/type_proofs_design.md I-M1): class object + shape epoch +
    // present cell -> the cell copy the field rung pushes; any miss runs the ladder
    if (expr->proven_class && !parallel_worker_) {
        if (script_value* cell = detail::proven_field_cell(objectValue, expr->proven_class, expr->proven_epoch,
                                                           expr->proven_slot, nullptr)) {
            push_value(*cell);
            return {};
        }
    }

    // Handle super:: member access specially
    if (is_super_access) {
        // objectValue is 'this' from visit_super_expr
        if (!objectValue.is_object()) {
            // super:: used on non-object
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        // Get the class instance
        auto objHolder = objectValue.get_object_holder();
        if (!objHolder || !objHolder->data) {
            // super:: used on non-class object
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        // Both script and C++ classes store class_instance in data
        auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
        if (!instance) {
            // super:: used on non-class object
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        // Get the class definition and its parent
        auto class_def = instance->get_class_definition();
        if (!class_def) {
            // Class definition not found for super:: access
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        auto parent_def = class_def->get_parent();
        if (!parent_def) {
            // super:: used in class with no parent
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        if (parent_def->chain_has_nonpublic()) [[unlikely]] {
            JAISCRIPT_TRY(detail::enforce_member_access(parent_def.get(), expr->member_id,
                                                        environment_->find_access_context()));
        }

        // Look for the method in the parent class (use pre-computed member_id from parser)
        script_value method = parent_def->get_method(expr->member_id, false);
        if (method.is_invalid() || method.is_null()) {
            return checked_result<void>(make_error_code(runtime_error_code::member_not_found),
                "parent class {0} has no member '{1}'", parent_def->get_type_id(), expr->member_id);
        }

        // Return a bound method that calls the parent's implementation
        if (!as_callee) { return method_value_error(expr, objectValue, true); }
        push_value(create_bound_method(objectValue, method));
        return {};
    }

    // Worker direct-field read (parallel_for v1): probing getters/methods below
    // COPIES shared method values out of class_definition state - a cross-worker
    // count race on the non-atomic strong_ptr. Fields live on the (exclusively
    // owned) instance; anything else needs method machinery - wall it.
    if (parallel_worker_ && objectValue.raw_storage_index() == script_value::TYPEID_OBJECT) [[unlikely]] {
        auto& holder = objectValue.unchecked_get_object_storage();
        if (holder && holder->is_class_instance_wrapper && holder->data) {
            auto* inst = static_cast<class_instance*>(holder->data.get());
            if (script_value* field = inst->find_field_value(expr->member_id)) {
                push_value(field->is_reference() ? field->deref() : *field);
                return {};
            }
            return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                "only direct field access on class instances is allowed in a parallel body");
        }
    }

    // Handle coroutine_handle methods
    if (objectValue.is_object()) {
        auto objHolder = objectValue.get_object_holder();
        if (objHolder && objHolder->type_id == coroutine_handle_type_id_) {
            auto handle = std::static_pointer_cast<coroutine_handle>(objHolder->data);
            if (expr->member_id == resume_id_) {
	            if (!as_callee) { return method_value_error(expr, objectValue, false); }
                // Return a bound "resume" method
                auto captured_handle = handle;
                auto* eng = engine_;
                script_function resume_method = [captured_handle, eng](const std::vector<script_value>& /*args*/) -> checked_result<script_value> {
                    return captured_handle->resume(eng);
                };
                push_value(script_value::make_function(resume_method, engine_));
                return {};
            }
            if (expr->member_id == done_id_) {
	            if (!as_callee) { return method_value_error(expr, objectValue, false); }
                // Return a bound "done" method
                auto captured_handle = handle;
                auto* eng = engine_;
                script_function done_method = [captured_handle, eng](const std::vector<script_value>& /*args*/) -> checked_result<script_value> {
                    return script_value(captured_handle->done(), eng);
                };
                push_value(script_value::make_function(done_method, engine_));
                return {};
            }
            return checked_result<void>(make_error_code(runtime_error_code::member_not_found),
                "coroutine_handle has no member '{0}'", expr->member_id);
        }
    }

    // Handle string methods
    if (objectValue.is_string() || objectValue.is_utf8_string()) {
        if (!as_callee) {   // a caught error's own members and a thrown object's fields before the string methods (KEEP BYTE-PARALLEL with vm_backend::member_access_value)
            if (auto error_member = engine_->caught_error_member(objectValue, expr->member_id)) {
                push_value(std::move(*error_member));
                return {};
            }
        }
        const auto& table = objectValue.is_utf8_string() ? utf8_methods_ : string_methods_;
        auto methodIt = table.find(expr->member_id);
        if (methodIt != table.end()) {
            // Found the method in the registry
            if (!as_callee) { return method_value_error(expr, objectValue, false); }
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the string value by moving it
            script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                std::vector<script_value> scratch;
                return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
            };

            push_value(script_value::make_function(boundMethod, engine_));
            return {};
        }
        else {
            if (auto error_member = engine_->caught_error_member(objectValue, expr->member_id)) {   // e.file, e.line, ... on a caught error
                push_value(std::move(*error_member));
                return {};
            }
            // Set exception state instead of throwing
            std::string text = std::string(objectValue.is_utf8_string() ? "utf8_string" : "string") + " has no member '" + std::string(expr->member) + "'";
            active_exception_value_ = make_value(text);
            current_exception_ = script_exception(text, expr->location);
            is_unwinding_ = true;
            push_value(make_value());
            return {};
        }
    }

    // Handle array methods
    if (objectValue.is_array()) {
        auto methodIt = array_methods_.find(expr->member_id);
        if (methodIt != array_methods_.end()) {
            // Found the method in the registry
            if (!as_callee) { return method_value_error(expr, objectValue, false); }
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the array value by moving it
            script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                std::vector<script_value> scratch;
                return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
            };

            push_value(script_value::make_function(boundMethod, engine_));
            return {};
        }
        else {
            // Set exception state instead of throwing
            std::string text = "array has no member '" + std::string(expr->member) + "'";
            active_exception_value_ = make_value(text);
            current_exception_ = script_exception(text, expr->location);
            is_unwinding_ = true;
            push_value(make_value());
            return {};
        }
    }

    // Handle map methods
    if (objectValue.is_map()) {
        auto methodIt = map_methods_.find(expr->member_id);
        if (methodIt != map_methods_.end()) {
            // Found the method in the registry
            if (!as_callee) { return method_value_error(expr, objectValue, false); }
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the map value by moving it
            script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                std::vector<script_value> scratch;
                return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
            };

            push_value(script_value::make_function(boundMethod, engine_));
            return {};
        }
        else {
            // No built-in method found - try member name as a map key (for enums: Direction.north)
            auto key = script_value(std::string(expr->member), engine_);
            auto& map_storage = const_cast<script_value&>(objectValue).get_map_storage();
            if (map_storage) {
                auto it = map_storage->find(key);
                if (it != map_storage->end()) {
                    push_value(it->second);
                    return {};
                }
            }
            return checked_result<void>(make_error_code(runtime_error_code::member_not_found),
                "map has no member or key '{0}'", expr->member_id);
        }
    }

    // Handle weak_ptr methods
    if (objectValue.is_weak_ptr()) {
        auto methodIt = weak_ptr_methods_.find(expr->member_id);
        if (methodIt != weak_ptr_methods_.end()) {
            // Found the method in the registry
            if (!as_callee) { return method_value_error(expr, objectValue, false); }
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the weak_ptr value by moving it
            script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                std::vector<script_value> scratch;
                return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
            };

            push_value(script_value::make_function(boundMethod, engine_));
            return {};
        }
        else {
            return checked_result<void>(make_error_code(runtime_error_code::member_not_found),
                "weak_ptr has no member '{0}'", expr->member_id);
        }
    }

    // Handle shared_ptr methods (shared_ptr<T> explicitly marked objects)
    // After refactor: check type_info marker instead of storage type
    if (objectValue.get_type_info() &&
        objectValue.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
        // This is an explicitly marked shared_ptr<T> object
        auto methodIt = shared_ptr_methods_.find(expr->member_id);
        if (methodIt != shared_ptr_methods_.end()) {
            // A user class method WINS over a same-named builtin handle method (Dev ruling
            // 2026-07): only mint the builtin when the underlying class does NOT define it.
            bool user_shadows = false;
            if (auto ci = objectValue.get_class_instance()) {
                if (auto* cd = ci->get_class_definition()) {
                    user_shadows = cd->defines_method(expr->member_id);
                }
            }
            if (!user_shadows) {
                // Found the method in the registry (reset, use_count, unique)
                if (!as_callee) { return method_value_error(expr, objectValue, false); }
                const builtin_method& method = methodIt->second;

                // Create a wrapper function that captures the shared_ptr value by moving it
                script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                    std::vector<script_value> scratch;
                    return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
                };

                push_value(script_value::make_function(boundMethod, engine_));
                return {};
            }
        }
        // Method not found in shared_ptr built-ins (or shadowed) - forward to the object
    }

    // After refactor: shared_ptr<T> uses same storage as regular objects
    // No unwrapping needed - just access the object_holder directly

    // Built-in same_as() for all values (identity/equality comparison)
    // same_as_id_ is a per-interpreter member (symbol ids are per-engine; a static here
    // cached the first engine's id and broke same_as on every later engine).
    if (expr->member_id == same_as_id_) {
	    if (!as_callee) { return method_value_error(expr, objectValue, false); }
        // Create a bound same_as method for this value
        script_function same_as_method = [this, capturedValue = objectValue](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "same_as() takes exactly 1 argument");
            }

            const script_value& other_raw = args[0];

            // Deref both values to follow reference chains to the root object
            script_value self_val = capturedValue.deref();
            script_value other_val = const_cast<script_value&>(other_raw).deref();

            // S8: bound values decode-compare like the old shadow indices (two aliases of one
            // C++ int stay same_as-true). KEEP BYTE-PARALLEL with the VM same_as lambda.
            if (self_val.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
                self_val = self_val.bound_decoded_temp();
            if (other_val.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
                other_val = other_val.bound_decoded_temp();

            // Both null -> same
            if (self_val.is_null() && other_val.is_null()) {
                return make_value(true);
            }

            // One null, one not -> not same
            if (self_val.is_null() || other_val.is_null()) {
                return make_value(false);
            }

            // Fast path: compare raw type indices first
            auto self_idx = self_val.raw_storage_index();
            auto other_idx = other_val.raw_storage_index();

            // Different types -> not same
            if (self_idx != other_idx) {
                return make_value(false);
            }

            // Same type: compare values based on type
            switch (self_idx) {
                case script_value::TYPEID_INT:
                    return make_value(self_val.unchecked_as_int() == other_val.unchecked_as_int());
                case script_value::TYPEID_FLOAT:
                    return make_value(self_val.unchecked_as_float() == other_val.unchecked_as_float());
                case script_value::TYPEID_BOOL:
                    return make_value(self_val.unchecked_as_bool() == other_val.unchecked_as_bool());
                case script_value::TYPEID_STRING:
                    return make_value(self_val.unchecked_as_string() == other_val.unchecked_as_string());
                case script_value::TYPEID_OBJECT:
                case script_value::TYPEID_SHARED_PTR:
                    break; // Fall through to pointer comparison below
                default:
                    // Other types (arrays, maps, functions, etc.) - not same unless same object
                    return make_value(false);
            }

            // For objects: pointer identity comparison
            auto self_holder = self_val.get_object_holder();
            auto other_holder = other_val.get_object_holder();

            // Compare the underlying object_holder pointers (pointer identity)
            bool same = (self_holder.get() == other_holder.get());
            return make_value(same);
        };

        push_value(script_value::make_function(same_as_method, engine_));
        return {};
    }

    if (!objectValue.is_object()) {
        if (objectValue.is_null()) {
            return checked_result<void>(make_error_code(runtime_error_code::member_access_on_null), null_member_message(expr->object.get(), as_callee), expr->member_id, null_member_receiver_symbol(expr->object.get()));
        }
        return checked_result<void>(make_error_code(runtime_error_code::member_not_found),
            scalar_member_message, base_type_ids_[objectValue.type()], expr->member_id);
    }

    // One path for every object shape (class_instance wrapper, cpp_bound T&, raw holder)
    auto target = resolve_member_target(objectValue);
    if (!target) {
        // Cannot access member on non-class object
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    if (target.class_def && target.class_def->chain_has_nonpublic()) [[unlikely]] {
        JAISCRIPT_TRY(detail::enforce_member_access(target.class_def, expr->member_id,
                                                    environment_->find_access_context()));
    }

    // Use pre-computed member_id from parser
    uint64_t member_id = expr->member_id;

    // The nearest USER _get_ in the chain (script or C++) wins over the field; the synthesized
    // accessor is skipped - its body IS the field rung below (KEEP BYTE-PARALLEL with the vm's
    // member_access_value). getter_id is cached on the node.
    if (target.class_def && target.class_def->has_property_getters()) {
        try {
            uint64_t getter_id = expr->getter_id;
            if (getter_id == UINT64_MAX) {
                auto [id, _] = string_symbolizer_->get_getter_id_with_view(member_id);
                getter_id = id;
                expr->getter_id = getter_id;
            }
            script_value getter = target.user_accessor(getter_id);
            if (!getter.is_null() && !getter.is_invalid() && getter.is_function()) {
                // The shared read (retired-layer report, receiver pin): the bare-name
                // rung of detail/member_scope.hpp runs the identical kernel
                auto result = detail::read_cpp_property(objectValue, target.class_def, target.instance.get(),
                                                        getter, getter_id);
                if (!result) {
                    return result.error_value();
                }
                push_value(std::move(result.value()));
                return {};
            }
        } catch (const std::exception&) {
            // If the getter fails (e.g., class definition expired), fall back to field access
            if (target.instance && target.instance->script_layer_retired()) { throw; }
        }
    }

    // Check if it's a field (script class fields without getters, or C++ fields)
    if (target.has_field(member_id)) {
        push_value(target.get_field(member_id));
        return {};
    }

    // Look for a method (bound to the receiver)
    script_value method = target.method(member_id);
    if (!method.is_null() && !method.is_invalid()) {
        // Return a bound method (function that has 'this' pre-bound)
        if (!as_callee) { return method_value_error(expr, objectValue, true); }
        push_value(create_bound_method(objectValue, method));
        return {};
    }

    // Check if this is a transparent wrapper - if so, unwrap and retry member access
    if (target.class_def && target.class_def->is_transparent_wrapper()) {
        script_value mutable_obj = objectValue;  // Need mutable copy for unwrap
        script_value unwrapped = target.class_def->unwrap(mutable_obj);
        // For primitive types (int, float, string, etc.), the value itself doesn't have
        // members (their operations are handled in binary operators); objects retry here
        if (!unwrapped.is_null() && unwrapped.is_object()) {
            auto unwrapped_target = resolve_member_target(unwrapped);
            if (unwrapped_target) {
                if (unwrapped_target.has_field(member_id)) {
                    push_value(unwrapped_target.get_field(member_id));
                    return {};
                }
                script_value unwrapped_method = unwrapped_target.method(member_id);
                if (!unwrapped_method.is_null() && !unwrapped_method.is_invalid()) {
                    if (!as_callee) { return method_value_error(expr, objectValue, true); }
                    push_value(create_bound_method(unwrapped, unwrapped_method));
                    return {};
                }
            }
        }
    }

    const std::string_view cls = target.class_name();
    std::string text = (cls.empty() ? std::string("object") : std::string(cls)) + " has no member '" + std::string(expr->member) + "'";
    active_exception_value_ = make_value(text);
    current_exception_ = script_exception(text, expr->location);
    is_unwinding_ = true;
    push_value(make_value());  // Push null for failed member access
    return {};
}

checked_result<void> interpreter::visit_lambda_expr(lambda_expr* expr) {

    // Capture current environment for closure
    auto closure_env = environment_;

    // Check if we need a capture environment
    bool has_explicit_captures = !expr->captures.empty();
    bool has_default_capture = (expr->default_capture != lambda_expr::capture_default::none);
    
    
    // For default captures, analyze the lambda body to find which variables are actually used
    std::unordered_set<std::string> used_variables;
    if (has_default_capture) {
        // Helper to recursively find all identifiers in an expression
        std::function<void(expression*)> find_identifiers;
        find_identifiers = [&](expression* e) {
            if (e->get_type() == node_type::identifier_expr) {
                auto* ident = static_cast<identifier_expr*>(e);
                // Skip parameter names
                bool is_param = false;
                for (const auto& param : expr->parameters) {
                    if (param.name == ident->name) {
                        is_param = true;
                        break;
                    }
                }
                if (!is_param) {
                    used_variables.insert(std::string(ident->name));
                }
            } else if (e->get_type() == node_type::binary_expr) {
                auto* binary = static_cast<binary_expr*>(e);
                find_identifiers(binary->left.get());
                find_identifiers(binary->right.get());
            } else if (e->get_type() == node_type::unary_expr) {
                auto* unary = static_cast<unary_expr*>(e);
                find_identifiers(unary->operand.get());
            } else if (e->get_type() == node_type::call_expr) {
                auto* call = static_cast<call_expr*>(e);
                find_identifiers(call->callee.get());
                for (const auto& arg : call->arguments) {
                    find_identifiers(arg.get());
                }
            } else if (e->get_type() == node_type::member_expr) {
                auto* member = static_cast<member_expr*>(e);
                find_identifiers(member->object.get());
            } else if (e->get_type() == node_type::assignment_expr) {
                auto* assign = static_cast<assignment_expr*>(e);
                find_identifiers(assign->target.get());
                find_identifiers(assign->value.get());
            } else if (e->get_type() == node_type::ternary_expr) {
                auto* ternary = static_cast<ternary_expr*>(e);
                find_identifiers(ternary->condition.get());
                find_identifiers(ternary->then_expression.get());
                find_identifiers(ternary->else_expression.get());
            } else if (e->get_type() == node_type::array_literal_expr) {
                auto* arr = static_cast<array_literal_expr*>(e);
                for (const auto& el : arr->elements) find_identifiers(el.get());
            } else if (e->get_type() == node_type::map_literal_expr) {
                auto* m = static_cast<map_literal_expr*>(e);
                for (const auto& kv : m->entries) { find_identifiers(kv.first.get()); find_identifiers(kv.second.get()); }
            } else if (e->get_type() == node_type::new_expr) {
                auto* ne = static_cast<new_expr*>(e);
                for (const auto& a : ne->arguments) find_identifiers(a.get());
            } else if (e->get_type() == node_type::throw_expr) {
                auto* te = static_cast<throw_expr*>(e);
                if (te->value) find_identifiers(te->value.get());
            } else if (e->get_type() == node_type::yield_expr) {
                auto* ye = static_cast<yield_expr*>(e);
                if (ye->value) find_identifiers(ye->value.get());
            } else if (e->get_type() == node_type::include_expr) {
                auto* ie = static_cast<include_expr*>(e);
                if (ie->path_expr) find_identifiers(ie->path_expr.get());
            } else if (e->get_type() == node_type::import_expr) {
                for (const auto& a : static_cast<import_expr*>(e)->path_exprs) find_identifiers(a.get());
            } else if (e->get_type() == node_type::cast_expr) {
                find_identifiers(static_cast<cast_expr*>(e)->operand.get());
            }
            // Add more expression types as needed
        };
        
        // Helper to find identifiers in statements
        std::function<void(statement*)> find_in_statement;
        find_in_statement = [&](statement* s) {
            if (s->get_type() == node_type::expression_stmt) {
                auto* expr_stmt = static_cast<expression_stmt*>(s);
                find_identifiers(expr_stmt->expression.get());
            } else if (s->get_type() == node_type::block_stmt) {
                auto* block = static_cast<block_stmt*>(s);
                for (const auto& decl : block->declarations) {
                    if (decl->get_type() == node_type::expression_decl) {
                        auto* expr_decl = static_cast<expression_decl*>(decl.get());
                        find_identifiers(expr_decl->expression.get());
                    } else if (decl->get_type() == node_type::statement_decl) {
                        auto* stmt_decl = static_cast<statement_decl*>(decl.get());
                        find_in_statement(stmt_decl->statement.get());
                    }
                }
            } else if (s->get_type() == node_type::if_stmt) {
                auto* if_s = static_cast<if_stmt*>(s);
                find_identifiers(if_s->condition.get());
                find_in_statement(if_s->then_statement.get());
                if (if_s->else_statement) {
                    find_in_statement(if_s->else_statement.get());
                }
            } else if (s->get_type() == node_type::while_stmt) {
                auto* while_s = static_cast<while_stmt*>(s);
                find_identifiers(while_s->condition.get());
                find_in_statement(while_s->body.get());
            } else if (s->get_type() == node_type::return_stmt) {
                auto* return_s = static_cast<return_stmt*>(s);
                if (return_s->value) {
                    find_identifiers(return_s->value.get());
                }
            } else if (s->get_type() == node_type::for_stmt) {
                auto* for_s = static_cast<for_stmt*>(s);
                if (for_s->initializer) find_in_statement(for_s->initializer.get());
                if (for_s->condition) find_identifiers(for_s->condition.get());
                if (for_s->update) find_identifiers(for_s->update.get());
                find_in_statement(for_s->body.get());
            } else if (s->get_type() == node_type::range_for_stmt) {
                auto* rf = static_cast<range_for_stmt*>(s);
                if (rf->container) find_identifiers(rf->container.get());
                find_in_statement(rf->body.get());
            } else if (s->get_type() == node_type::parallel_for_stmt) {
                auto* pf = static_cast<parallel_for_stmt*>(s);
                if (pf->container) find_identifiers(pf->container.get());
                find_in_statement(pf->body.get());
            } else if (s->get_type() == node_type::switch_stmt) {
                auto* sw = static_cast<switch_stmt*>(s);
                if (sw->condition) find_identifiers(sw->condition.get());
                for (const auto& c : sw->cases) {
                    if (c->value) find_identifiers(c->value.get());
                    for (const auto& st : c->body) find_in_statement(st.get());
                }
                if (sw->default_case) {
                    for (const auto& st : sw->default_case->body) find_in_statement(st.get());
                }
            } else if (s->get_type() == node_type::try_stmt) {
                auto* tr = static_cast<try_stmt*>(s);
                if (tr->try_block) find_in_statement(tr->try_block.get());
                if (tr->catch_block) find_in_statement(tr->catch_block.get());
            } else if (s->get_type() == node_type::variable_decl) {
                auto* vd = static_cast<variable_decl*>(s);
                if (vd->initializer) find_identifiers(vd->initializer.get());
            } else if (s->get_type() == node_type::expression_decl) {
                auto* ed = static_cast<expression_decl*>(s);
                if (ed->expression) find_identifiers(ed->expression.get());
            } else if (s->get_type() == node_type::statement_decl) {
                auto* sd = static_cast<statement_decl*>(s);
                if (sd->statement) find_in_statement(sd->statement.get());
            }
            // Add more statement types as needed
        };
        
        // Analyze the lambda body
        find_in_statement(expr->body.get());

    }

    // ----------------------------------------------------------------
    // OUTER-LOCAL SLOT CAPTURE
    //
    // When the parser assigned slot indices, it walked the outer function
    // scope stack — so identifier_expr nodes in this lambda body that
    // refer to OUTER locals have their slot_index set to the outer
    // function's slot (e.g. slot 0 of the enclosing `auto f()`) rather
    // than SIZE_MAX. At runtime the lambda runs in its OWN call frame
    // (slot 0 = its first param/local), so a baked-in outer slot resolves
    // to completely wrong data or "Undefined variable".
    //
    // Fix: scan the lambda body for identifier nodes whose slot_index falls
    // within the outer call frame's live slot range. For each, read the
    // value from the outer frame (which IS alive at lambda creation time),
    // materialise it into the capture environment, then zero the baked
    // slot_index to SIZE_MAX so the runtime lookup falls through to the env
    // path on every subsequent call.
    //
    // Outer-ness is decided by SYMBOL identity, not slot ranges: outer-function
    // slots and the lambda's own slots are independent numbering spaces that both
    // start at 0, so an outer local can collide with the lambda's own params/locals
    // (e.g. the enclosing function's first param captured by a one-param lambda).
    // Any body identifier whose symbol the lambda does not itself declare is outer.
    //
    // This is purely a runtime operation — zero parser changes; re-parsing
    // (hot reload) regenerates the lambda AST fresh.
    //
    // Performance: one AST walk at closure-creation time, O(1) env lookup
    // per captured local on every call — identical cost to the existing
    // [=] path. No overhead for lambdas that don't reference outer locals.
    // ----------------------------------------------------------------
    struct outer_slot_ref {
        identifier_expr* node;       // the AST node (we'll zero its slot_index)
        uint64_t symbol_id;
        size_t outer_slot;           // the outer frame's slot index
    };
    std::vector<outer_slot_ref> outer_slot_refs;

    // Build the set of outer-slot-index identifiers we've already handled
    // (avoid duplicates for the same symbol used multiple times in the body)
    std::unordered_set<uint64_t> outer_slots_captured;

    if (!call_stack_.empty() && expr->outer_slot_plan_built) {
        // Later creations from this AST: the first creation already patched the body
        // identifiers (a re-scan would find nothing), so replay its capture plan.
        const size_t outer_slot_count = call_stack_.back().local_count();
        for (const auto& [sym, slot] : expr->outer_slot_plan) {
            if (slot < outer_slot_count && outer_slots_captured.insert(sym).second) {
                outer_slot_refs.push_back({nullptr, sym, slot});
            }
        }
    } else if (!call_stack_.empty()) {
        const size_t outer_slot_count = call_stack_.back().local_count();

        // Symbols the lambda itself declares (params + body locals + loop variables).
        // Slot ranges cannot distinguish own locals from outer ones — both scopes
        // number from 0 — so classification below is by symbol identity.
        std::unordered_set<uint64_t> lambda_declared;
        for (const auto& p : expr->parameters) {
            if (p.symbol_id == UINT64_MAX) {
                p.symbol_id = string_symbolizer_->intern(p.name);
            }
            lambda_declared.insert(p.symbol_id);
        }
        std::vector<identifier_expr*> outer_slot_candidates;

        // Reuse the existing AST-walk infrastructure but collect identifier_expr*
        // nodes whose slot_index is in [0, outer_slot_count) — those came from the
        // outer function scope.  We walk even for no-capture lambdas ([]) because the
        // parser still bakes outer slot indices whenever the body references them.
        std::function<void(expression*)> collect_outer_slots_expr;
        std::function<void(statement*)>  collect_outer_slots_stmt;

        collect_outer_slots_expr = [&](expression* e) {
            if (!e) return;
            if (e->get_type() == node_type::identifier_expr) {
                auto* ident = static_cast<identifier_expr*>(e);
                if (ident->slot_index != SIZE_MAX && ident->slot_index < outer_slot_count) {
                    // Candidate only — whether it is outer is decided after the walk,
                    // once every symbol the lambda declares has been collected.
                    outer_slot_candidates.push_back(ident);
                }
                return;
            }
            // Recurse into sub-expressions (mirrors find_identifiers above)
            switch (e->get_type()) {
            case node_type::binary_expr: {
                auto* b = static_cast<binary_expr*>(e);
                collect_outer_slots_expr(b->left.get());
                collect_outer_slots_expr(b->right.get());
                break;
            }
            case node_type::unary_expr:
                collect_outer_slots_expr(static_cast<unary_expr*>(e)->operand.get()); break;
            case node_type::call_expr: {
                auto* c = static_cast<call_expr*>(e);
                collect_outer_slots_expr(c->callee.get());
                for (const auto& a : c->arguments) collect_outer_slots_expr(a.get());
                break;
            }
            case node_type::member_expr:
                collect_outer_slots_expr(static_cast<member_expr*>(e)->object.get()); break;
            case node_type::assignment_expr: {
                auto* a = static_cast<assignment_expr*>(e);
                collect_outer_slots_expr(a->target.get());
                collect_outer_slots_expr(a->value.get());
                break;
            }
            case node_type::ternary_expr: {
                auto* t = static_cast<ternary_expr*>(e);
                collect_outer_slots_expr(t->condition.get());
                collect_outer_slots_expr(t->then_expression.get());
                collect_outer_slots_expr(t->else_expression.get());
                break;
            }
            case node_type::array_literal_expr:
                for (const auto& el : static_cast<array_literal_expr*>(e)->elements)
                    collect_outer_slots_expr(el.get());
                break;
            case node_type::map_literal_expr:
                for (const auto& kv : static_cast<map_literal_expr*>(e)->entries) {
                    collect_outer_slots_expr(kv.first.get());
                    collect_outer_slots_expr(kv.second.get());
                }
                break;
            case node_type::new_expr:
                for (const auto& a : static_cast<new_expr*>(e)->arguments)
                    collect_outer_slots_expr(a.get());
                break;
            case node_type::throw_expr:
                if (static_cast<throw_expr*>(e)->value)
                    collect_outer_slots_expr(static_cast<throw_expr*>(e)->value.get());
                break;
            case node_type::yield_expr:
                if (static_cast<yield_expr*>(e)->value)
                    collect_outer_slots_expr(static_cast<yield_expr*>(e)->value.get());
                break;
            case node_type::include_expr:
                if (static_cast<include_expr*>(e)->path_expr)
                    collect_outer_slots_expr(static_cast<include_expr*>(e)->path_expr.get());
                break;
            case node_type::import_expr:
                for (const auto& a : static_cast<import_expr*>(e)->path_exprs) collect_outer_slots_expr(a.get());
                break;
            case node_type::cast_expr:
                collect_outer_slots_expr(static_cast<cast_expr*>(e)->operand.get());
                break;
            default: break;
            }
        };

        collect_outer_slots_stmt = [&](statement* s) {
            if (!s) return;
            switch (s->get_type()) {
            case node_type::expression_stmt:
                collect_outer_slots_expr(static_cast<expression_stmt*>(s)->expression.get()); break;
            case node_type::expression_decl:
                collect_outer_slots_expr(static_cast<expression_decl*>(s)->expression.get()); break;
            case node_type::statement_decl:
                collect_outer_slots_stmt(static_cast<statement_decl*>(s)->statement.get()); break;
            case node_type::block_stmt:
                for (const auto& d : static_cast<block_stmt*>(s)->declarations)
                    collect_outer_slots_stmt(d.get());
                break;
            case node_type::if_stmt: {
                auto* is = static_cast<if_stmt*>(s);
                collect_outer_slots_expr(is->condition.get());
                collect_outer_slots_stmt(is->then_statement.get());
                if (is->else_statement) collect_outer_slots_stmt(is->else_statement.get());
                break;
            }
            case node_type::while_stmt: {
                auto* ws = static_cast<while_stmt*>(s);
                collect_outer_slots_expr(ws->condition.get());
                collect_outer_slots_stmt(ws->body.get());
                break;
            }
            case node_type::for_stmt: {
                auto* fs = static_cast<for_stmt*>(s);
                if (fs->initializer) collect_outer_slots_stmt(fs->initializer.get());
                if (fs->condition)   collect_outer_slots_expr(fs->condition.get());
                if (fs->update)      collect_outer_slots_expr(fs->update.get());
                collect_outer_slots_stmt(fs->body.get());
                break;
            }
            case node_type::range_for_stmt: {
                auto* rf = static_cast<range_for_stmt*>(s);
                if (rf->variable_name_id != UINT64_MAX) {
                    lambda_declared.insert(rf->variable_name_id);
                }
                if (rf->container) collect_outer_slots_expr(rf->container.get());
                collect_outer_slots_stmt(rf->body.get());
                break;
            }
            case node_type::parallel_for_stmt: {
                // Body identifiers carry the WORKER function's slot numbering (the
                // parser's boundary keeps outer slots off them), so only the container
                // can reference outer slots; the loop variable is body-declared.
                auto* pf = static_cast<parallel_for_stmt*>(s);
                if (pf->loop_param.symbol_id != UINT64_MAX) {
                    lambda_declared.insert(pf->loop_param.symbol_id);
                }
                for (const auto& extra : pf->extra_params) {
                    if (extra.symbol_id != UINT64_MAX) { lambda_declared.insert(extra.symbol_id); }
                }
                if (pf->container) collect_outer_slots_expr(pf->container.get());
                break;
            }
            case node_type::return_stmt:
                if (static_cast<return_stmt*>(s)->value)
                    collect_outer_slots_expr(static_cast<return_stmt*>(s)->value.get());
                break;
            case node_type::switch_stmt: {
                auto* sw = static_cast<switch_stmt*>(s);
                if (sw->condition) collect_outer_slots_expr(sw->condition.get());
                for (const auto& c : sw->cases) {
                    if (c->value) collect_outer_slots_expr(c->value.get());
                    for (const auto& st : c->body) collect_outer_slots_stmt(st.get());
                }
                if (sw->default_case)
                    for (const auto& st : sw->default_case->body) collect_outer_slots_stmt(st.get());
                break;
            }
            case node_type::try_stmt: {
                auto* tr = static_cast<try_stmt*>(s);
                if (tr->try_block) collect_outer_slots_stmt(tr->try_block.get());
                if (tr->catch_var_id != UINT64_MAX) {
                    lambda_declared.insert(tr->catch_var_id);
                }
                if (tr->catch_block) collect_outer_slots_stmt(tr->catch_block.get());
                break;
            }
            case node_type::variable_decl: {
                auto* vd = static_cast<variable_decl*>(s);
                if (vd->name_id == UINT64_MAX) {
                    vd->name_id = string_symbolizer_->intern(std::string(vd->name));
                }
                lambda_declared.insert(vd->name_id);
                if (vd->initializer)
                    collect_outer_slots_expr(vd->initializer.get());
                break;
            }
            default: break;
            }
        };

        collect_outer_slots_stmt(expr->body.get());

        // Classify: anything the lambda itself declares resolves in its own frame;
        // every other slot-carrying identifier references the outer frame.
        for (auto* ident : outer_slot_candidates) {
            if (lambda_declared.count(ident->symbol_id)) continue;
            if (outer_slots_captured.insert(ident->symbol_id).second) {
                outer_slot_refs.push_back({ident, ident->symbol_id, ident->slot_index});
            } else {
                // Same symbol, different node — patch now (firsts are patched after capture)
                ident->slot_index = SIZE_MAX;
            }
        }

        for (const auto& [sym, slot] : expr->chained_outer_slots) {
            if (slot < outer_slot_count && outer_slots_captured.insert(sym).second) {
                outer_slot_refs.push_back({nullptr, sym, slot});
            }
        }

        // Persist the plan so later closure creations from this AST (whose identifier
        // slots are patched below) can replay the same captures.
        expr->outer_slot_plan.reserve(outer_slot_refs.size());
        for (const auto& ref : outer_slot_refs) {
            expr->outer_slot_plan.emplace_back(ref.symbol_id, ref.outer_slot);
        }
        expr->outer_slot_plan_built = true;
    }

    // If we found any outer-slot references, we need a capture env even for a
    // bare [] lambda (automatic local capture, a stated JaiScript feature).
    if (!outer_slot_refs.empty()) {
        has_explicit_captures = true;  // forces needs_capture_env = true below
    }

    if (has_default_capture) {
        for (uint64_t chained : expr->chained_outer_names) {
            used_variables.insert(std::string(string_symbolizer_->get_string(chained)));
        }
    }

    // Determine if we actually need a capture environment
    bool needs_capture_env = has_explicit_captures || (has_default_capture && !used_variables.empty());

    // Pre-cache capture symbol IDs for optimization
    uint64_t this_id = string_symbolizer_->get_this_id();
    for (auto& capture : expr->captures) {
        if (capture.symbol_id == UINT64_MAX) {
            capture.symbol_id = string_symbolizer_->intern(capture.name);
        }
    }

    // Check if [this] is captured - we'll need special handling
    bool captures_this = false;
    for (const auto& capture : expr->captures) {
        if (capture.symbol_id == this_id) {
            captures_this = true;
            break;
        }
    }

    std::shared_ptr<environment> final_closure_env;

    if (needs_capture_env) {
        // Create captured variables in the closure environment.
        // Parent = global environment (NOT the local-function's pool-env).
        //
        // Using the local function's closure_env as parent causes a use-after-free
        // / ownership cycle for ESCAPING closures: the outer function's pooled env
        // gets released when it returns, then get_pooled_environment() re-allocates
        // that same slot with itself (or a descendant) as parent → infinite loop.
        //
        // The capture env is self-contained: all needed outer variables are copied
        // into it (env-variables above, and outer-slot locals below). The only thing
        // a parent provides beyond that is global-scope fallback, so pointing at the
        // global env gives correct semantics without the lifecycle hazard.
        auto captureEnv = std::make_shared<environment>(get_global_environment(), string_symbolizer_);
        
        // Process default captures first ([=] or [&])
        if (has_default_capture && !used_variables.empty()) {
            bool capture_by_ref = (expr->default_capture == lambda_expr::capture_default::by_reference);
            
            for (const auto& varName : used_variables) {
                // Check if this variable is explicitly overridden in the capture list
                bool is_overridden = false;
                uint64_t var_id = string_symbolizer_->intern(varName);
                for (const auto& capture : expr->captures) {
                    if (capture.symbol_id == var_id) {
                        is_overridden = true;
                        break;
                    }
                }

                if (!is_overridden) {
                    // The capture source in the runtime's order (detail/capture_ladder.hpp):
                    // the outer frame's slot, the member scope, then the env chain - a
                    // same-named global never wins over a local or a field. [&] cell-shares
                    // the source (escape-legal: the cell outlives the frame), [=] snapshots
                    // it; a hybrid's C++ property follows the field rule through its
                    // accessors (reference_model.md CPP-MEMBER / PROPERTY).
                    script_value* slot_val = nullptr;
                    if (!call_stack_.empty()) {
                        for (const auto& ref : outer_slot_refs) {
                            if (ref.symbol_id == var_id) {
                                slot_val = call_stack_.back().get_local(ref.outer_slot);
                                break;
                            }
                        }
                    }
                    auto src = detail::resolve_capture_source(slot_val, environment_.get(), member_scope_, var_id);
                    if (src.what == detail::capture_source::kind::denied) [[unlikely]] {
                        return *src.access_error;
                    }
                    if (src.global) { continue; }   // a global resolves when the lambda runs (KEEP BYTE-PARALLEL with vm exec_closure)
                    if (src.cell) {
                        if (capture_by_ref) {
                            captureEnv->define(var_id, share_boxed_env_storage(*src.cell, engine_));
                        } else {
                            captureEnv->define(var_id, detail::clone_for_capture(src.cell->deref(), string_symbolizer_));
                        }
                    } else if (src.what == detail::capture_source::kind::cpp_property) {
                        auto read = capture_by_ref
                            ? detail::bind_cpp_property_ref(*member_scope_.this_storage, var_id, engine_, string_symbolizer_)
                            : detail::read_bare_cpp_property(*member_scope_.this_storage, src.property.instance,
                                                             src.property.method_value, src.property.getter_id, var_id);
                        if (!read) {
                            return read.error_value();
                        }
                        captureEnv->define(var_id, capture_by_ref ? std::move(read.value())
                                                                  : detail::clone_for_capture(read.value(), string_symbolizer_));
                    }
                }
            }
        }
        
        // Process explicit captures
        for (const auto& capture : expr->captures) {
            script_value* slot_val = nullptr;
            if (!call_stack_.empty()) {
                if (capture.outer_slot != SIZE_MAX && capture.outer_slot < call_stack_.back().local_count()) {
                    slot_val = call_stack_.back().get_local(capture.outer_slot);
                }
                if (!slot_val) {
                    for (const auto& ref : outer_slot_refs) {
                        if (ref.symbol_id == capture.symbol_id) {
                            slot_val = call_stack_.back().get_local(ref.outer_slot);
                            break;
                        }
                    }
                }
            }
            if (!slot_val && capture.symbol_id == this_id) {
                // 'this' - method_environment provides it via get() / get_value_ptr()
                // even though contains() might return false
                if (!environment_->contains(this_id) && !environment_->get_value_ptr(this_id)) {
                    return checked_result<void>(make_error_code(runtime_error_code::capture_undefined_variable),
                        environment_->undefined_text(capture.symbol_id, "Cannot capture undefined variable '{0}'"), capture.symbol_id);
                }
                if (capture.by_reference) {
                    script_value* targetPtr = environment_->get_value_ptr(this_id);
                    if (!targetPtr) {
                        return checked_result<void>(make_error_code(runtime_error_code::capture_reference_failed),
                            "Cannot capture variable '{0}' by reference", capture.symbol_id);
                    }
                    captureEnv->define(this_id, share_boxed_env_storage(*targetPtr, engine_));
                } else {
                    // [this] captures the instance itself, as C++ captures the this pointer (KEEP BYTE-PARALLEL with vm_backend::exec_closure)
                    auto capture_result = environment_->get(this_id);
                    if (!capture_result) return capture_result.error_value();
                    captureEnv->define(this_id, script_value(capture_result.value().deref()));
                }
                continue;
            }
            // An init-capture [x = e]: e evaluated here, in the enclosing scope, copied into the lambda's own x
            if (capture.initializer) {
                JAISCRIPT_TRY(dispatch_expr(capture.initializer.get()));
                script_value init = pop_value();
                if (is_unwinding_) {
                    push_value(make_value());
                    return {};
                }
                captureEnv->define(capture.symbol_id, clone_for_assignment(init.deref()));
                continue;
            }
            // The capture source in the runtime's order (detail/capture_ladder.hpp): the
            // outer frame's slot, the function's own scopes, then the env chain - a member
            // shadows the chain and is not a capturable variable
            auto src = detail::resolve_capture_source(slot_val, environment_.get(), member_scope_, capture.symbol_id);
            if (!src.is_variable()) {
                return checked_result<void>(make_error_code(runtime_error_code::capture_undefined_variable),
                    environment_->undefined_text(capture.symbol_id, "Cannot capture undefined variable '{0}'"), capture.symbol_id);
            }
            if (capture.by_reference && src.global) { continue; }   // [&g] of a global: the lambda reads it when it runs
            if (capture.by_reference) {
                // Cell share (escape-legal, C++ [&x] semantics): the storage boxes in
                // place; the closure and the original share the cell
                captureEnv->define(capture.symbol_id, share_boxed_env_storage(*src.cell, engine_));
            } else {
                captureEnv->define(capture.symbol_id, detail::clone_for_capture(src.cell->deref(), string_symbolizer_));
            }
        }

        // Materialise outer-frame locals into the capture env and patch their
        // slot_index to SIZE_MAX so every subsequent call resolves via the env
        // (O(1) hash lookup) rather than the wrong call-frame slot.
        if (!call_stack_.empty()) {
            for (auto& ref : outer_slot_refs) {
                // Skip if already captured explicitly / by default-capture above - OWN
                // storage only, a same-named global behind the parent is not a capture
                // (ref.node is null when replaying a stored plan — already patched then)
                if (captureEnv->get_own_value_ptr(ref.symbol_id)) {
                    if (ref.node) { ref.node->slot_index = SIZE_MAX; }
                    continue;
                }
                script_value* slot_val = call_stack_.back().get_local(ref.outer_slot);
                if (slot_val) {
                    if (expr->default_capture == lambda_expr::capture_default::by_reference) {
                        // C++ [&] semantics: share the cell (the slot boxes in place;
                        // escape-legal - the cell outlives the frame)
                        captureEnv->define(ref.symbol_id, share_boxed_env_storage(*slot_val, engine_));
                    } else {
                        // Automatic / [=] capture of a local: by value
                        captureEnv->define(ref.symbol_id, detail::clone_for_capture(slot_val->deref(), string_symbolizer_));
                    }
                }
                // Patch the AST node so future lookups go through the env path
                if (ref.node) { ref.node->slot_index = SIZE_MAX; }
            }
        }

        // If [this] was captured, we need to use a method_environment instead
        // so that member variables can be accessed without "this."
        if (captures_this) {
            // Get the 'this' object that was captured
            auto this_result = captureEnv->get(this_id);
            if (!this_result) {
                return checked_result<void>(make_error_code(runtime_error_code::capture_reference_failed),
                    "Failed to capture 'this' reference");
            }
            script_value this_obj = std::move(this_result.value());

            // Create a method_environment with the captured 'this' object
            // The parent is closure_env (the environment where the lambda was defined)
            // Use pooled environment to avoid creating infinite parent chains
            auto method_env = get_pooled_method_environment(
                closure_env,
                this_obj
            );
            method_env->define(this_id, this_obj);

            // Copy all captured variables (except 'this') into the method_environment -
            // the capture env's OWN storage only, never a global behind its parent
            for (const auto& capture : expr->captures) {
                if (capture.symbol_id == this_id) continue;
                if (script_value* captured = captureEnv->get_own_value_ptr(capture.symbol_id)) {
                    method_env->define(capture.symbol_id, *captured);
                }
            }

            // Also copy default-captured variables
            if (has_default_capture) {
                for (const auto& varName : used_variables) {
                    uint64_t var_id = string_symbolizer_->intern(varName);
                    if (var_id == this_id) continue;
                    if (script_value* captured = captureEnv->get_own_value_ptr(var_id)) {
                        method_env->define(var_id, *captured);
                    }
                }
            }

            final_closure_env = method_env;
        } else {
            final_closure_env = captureEnv;
        }
    } else {
        // No captures needed - use current environment directly (fast path)
        final_closure_env = closure_env;
        
    }
    
    // Note: Parameter symbol IDs are pre-interned by the parser (parse_parameter_list())

    // Create the script function. Capture-free lambdas mint ONCE per node (closure_env
    // is nullptr, so every creation is identical - body wrap, parameter storage and the
    // function object all reuse); capturing lambdas share the node's parameter storage
    // but carry their per-creation capture environment.
    const unit_stamp decl_unit = current_unit();
    std::shared_ptr<script_defined_function> lambdaFunc = needs_capture_env || (expr->mint_cache && expr->mint_cache->unit.id != decl_unit.id) ? nullptr : expr->mint_cache;
    if (!lambdaFunc) {
        // Convert the lambda body to a block_stmt if it's not already
        std::shared_ptr<block_stmt> lambdaBody;
        if (auto blockStmt = std::dynamic_pointer_cast<block_stmt>(expr->body)) {
            lambdaBody = blockStmt;
        } else {
            // Wrap single statement in a block
            std::vector<declaration_ptr> stmts;
            if (auto stmt = std::dynamic_pointer_cast<statement>(expr->body)) {
                auto stmtDecl = std::make_shared<statement_decl>(expr->location, stmt);
                stmts.push_back(stmtDecl);
            }
            lambdaBody = std::make_shared<block_stmt>(expr->location, std::move(stmts));
        }
        lambdaFunc = std::make_shared<script_defined_function>(
            "<lambda>",  // Anonymous function name
            shared_parameters_for(expr),
            expr->return_type,
            lambdaBody,
            needs_capture_env ? final_closure_env : nullptr,  // Only use closure env if we have captures
            expr->local_count   // the frame reserves every slot up front, as a named function's does
        );
        lambdaFunc->unit = decl_unit;
        if (!needs_capture_env) { expr->mint_cache = lambdaFunc; }
    }

    // Create a script_function wrapper resolving the live backend at call time.
    // Named thunk (not an anonymous lambda) so callers can recover the payload via
    // std::function::target<script_callable_thunk>() - vm parity (exec_closure).
    script_callable payload;
    payload.kind = script_callable::kind_type::function;
    payload.fn = lambdaFunc;
    script_function funcWrapper = script_callable_thunk{engine_, std::move(payload)};

    // Push the lambda as a function value
    push_value(script_value::make_function(funcWrapper, engine_));
    return {};
}

template<bool Replaying>
checked_result<void> interpreter::visit_new_expr_t(new_expr* expr, const replay_point* rs) {
    // A new expression is a call boundary too: preserve its source expressions for reference parameters.
    auto invoke_constructor = [&](const script_function& function, const std::vector<script_value>& args) {
        detail::call_site_context ctx{&expr->arguments, nullptr};
        struct restore_context {
            const detail::call_site_context*& slot;
            const detail::call_site_context* previous;
            ~restore_context() { slot = previous; }
        } restore{pending_call_ctx_, pending_call_ctx_};
        pending_call_ctx_ = &ctx;
        return function(args);
    };

    // This handles expressions like: new Point(), new Point(3.0, 4.0), etc.
    // The new_expr contains a type and arguments

    // std::cerr << "DEBUG: visit_new_expr called for type: " << (expr->type ? expr->type->type_name : "NULL") << std::endl;

    if (!expr->type) {
        // New expression missing type information
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    // Handle built-in types specially
    if (expr->type->base_type == script_value_type::jai_array_type) {
        // array<T>{} constructor
        if (!expr->arguments.empty()) {
            // array{} constructor does not take arguments
            return checked_result<void>(make_error_code(runtime_error_code::argument_count_mismatch));  // [ErrorText] Invalid argument count
        }

        // Create empty array with the specified element type
        auto element_type = expr->type->element_type();
        if (!element_type) {
            if (auto eng = engine_) {
                element_type = eng->get_type_info_int(); // Default to int if no type specified
            }
        }
        push_value(script_value::make_array(element_type, engine_));
        return {};
    }

    if (expr->type->base_type == script_value_type::jai_map_type) {
        // map<K,V>{} constructor
        if (!expr->arguments.empty()) {
            // map{} constructor does not take arguments
            return checked_result<void>(make_error_code(runtime_error_code::argument_count_mismatch));  // [ErrorText] Invalid argument count
        }

        // Create empty map with the specified key/value types
        auto key_type = expr->type->key_type();
        auto value_type = expr->type->value_type();
        if (auto eng = engine_) {
            if (!key_type) key_type = eng->get_type_info_string();
            if (!value_type) value_type = eng->get_type_info_int();
        }
        push_value(script_value::make_map(key_type, value_type, engine_));
        return {};
    }
    
    if (expr->type->base_type == script_value_type::jai_weak_ptr_type) {
        // weak_ptr<T>() or weak_ptr<T>(obj) constructor
        if (expr->arguments.empty()) {
            // No arguments - create empty weak_ptr
            push_value(script_value::make_empty_weak_ptr(expr->type, engine_));
        } else if (expr->arguments.size() == 1) {
            // One argument - create weak_ptr from object
            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->arguments[0].get(), rs, 0), expr, rp_new_weak, 0, {});
            script_value obj = pop_value();

            // Handle null objects
            if (obj.is_null()) {
                push_value(script_value::make_empty_weak_ptr(expr->type, engine_));
                return {};
            }

            // Allow creating weak_ptr from:
            // 1. Another weak_ptr (copy constructor)
            // 2. shared_ptr<T> (jai_shared_ptr_type)
            // NOTE: Regular objects have value semantics and cannot be used with weak_ptr

            if (obj.is_weak_ptr()) {
                // Copy constructor - just return the weak_ptr as-is
                push_value(std::move(obj));
                return {};
            }

            // Check if obj is a shared_ptr (required for weak_ptr)
            if (obj.type() != script_value_type::jai_shared_ptr_type) {
                // Get expected type ID for error context
                uint64_t expected_id = (expr->type && !expr->type->type_params.empty())
                    ? expr->type->type_params[0]->id : 0;

                if (obj.type() == script_value_type::jai_object_type) {
                    // Helpful error for value-semantic objects
                    return checked_result<void>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Cannot create weak_ptr from value-semantic object. Use shared_ptr<T>.",
                        expected_id);
                } else {
                    auto type_info = obj.get_type_info();
                    uint64_t actual_id = type_info ? type_info->id : 0;
                    return checked_result<void>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Cannot create weak_ptr from non-shared_ptr type. Use shared_ptr<T>.",
                        expected_id, actual_id);
                }
            }

            // Validate type parameter - weak_ptr<T> should only accept shared_ptr<T> or subclass
            // Skip validation for weak_ptr<var> (any type)
            auto expected_type = expr->type->element_type();
            auto obj_type_info = obj.get_type_info();
            if (expected_type && obj_type_info &&
                expected_type->base_type != script_value_type::jai_any_type) {
                // Get the actual class name from the shared_ptr's inner type
                std::string expected_class = expected_type->type_name;
                std::string actual_class = obj_type_info->element_type()
                    ? obj_type_info->element_type()->type_name
                    : obj_type_info->type_name;

                // Check if types match or if actual is a subclass of expected
                if (expected_class != actual_class) {
                    auto eng = engine_;
                    if (eng) {
                        auto actual_def = eng->get_class_definition(actual_class);
                        if (!actual_def || !actual_def->is_subtype_of(expected_class)) {
                            uint64_t expected_id = expected_type->id;
                            uint64_t actual_id = obj_type_info->element_type()
                                ? obj_type_info->element_type()->id
                                : obj_type_info->id;
                            return checked_result<void>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "weak_ptr type mismatch: type must match or be a subclass",
                                expected_id, actual_id);
                        }
                    }
                }
            }

            // Create weak_ptr from the shared_ptr
            auto weak_result = script_value::make_weak_ptr(obj, engine_);
            if (!weak_result) {
                return weak_result.error_value();
            }
            push_value(std::move(weak_result.value()));
        } else {
            // weak_ptr() expects 0 or 1 arguments
            return checked_result<void>(make_error_code(runtime_error_code::argument_count_mismatch));  // [ErrorText] Invalid argument count
        }
        return {};
    }
    
    if (expr->type->base_type == script_value_type::jai_shared_ptr_type) {
        // shared_ptr<T>(args...) - forward args to T's constructor, then mark as shared_ptr
        // This is like C++ make_shared - args go directly to T's constructor
        // shared_ptr is a TYPE MARKER only - it affects cloning behavior, not storage

        if (expr->arguments.empty()) {
            // No arguments - call T's default constructor
            auto inner_type = expr->type->element_type();
            if (!inner_type) {
                // shared_ptr with no type parameter and no args - create null
                push_value(make_value());
                return {};
            }
            std::string innerTypeName = inner_type->type_name;

            // Look up T's constructor
            auto ctor_result = environment_->get(innerTypeName);
            if (!ctor_result || !ctor_result.value().is_function()) {
                return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                    "No constructor found for class '{0}'", inner_type->id);
            }

            script_value constructorFunc = std::move(ctor_result.value());
            const script_function& func = constructorFunc.as_function();
            auto result = func({});  // Call with no args
            if (!result) {
                return result.error_value();
            }

            script_value value = std::move(result.value());
            // Mark as shared_ptr type
            if (value.type() == script_value_type::jai_object_type) {
                value.set_type_info(expr->type);
            }
            push_value(std::move(value));
            return {};
        }

        // Has arguments - forward them to T's constructor
        auto inner_type = expr->type->element_type();
        if (!inner_type) {
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                "shared_ptr requires a type parameter when called with arguments");
        }
        std::string innerTypeName = inner_type->type_name;

        // Evaluate all arguments
        std::vector<script_value> args;
        if constexpr (Replaying) { if (rs) args = rs->partial; }
        for (size_t ai = args.size(); ai < expr->arguments.size(); ++ai) {
            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->arguments[ai].get(), rs, ai), expr, rp_new_shared, ai, args);
            args.push_back(std::move(pop_value()));
            capture_call_argument(expr->arg_captures, ai, args.back(), [&] {
                auto ctor = environment_->get(innerTypeName);
                return ctor ? detail::callee_binding(ctor.value(), ai, false, expr->arguments.size()) : detail::arg_binding::opaque;
            });
        }

        // Look up T's constructor
        auto ctor_result = environment_->get(innerTypeName);
        if (!ctor_result || !ctor_result.value().is_function()) {
            return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                "No constructor found for class '{0}'", inner_type->id);
        }

        script_value constructorFunc = std::move(ctor_result.value());
        const script_function& func = constructorFunc.as_function();
        auto result = invoke_constructor(func, args);
        if (!result) {
            return result.error_value();
        }

        script_value value = std::move(result.value());
        // Mark as shared_ptr type
        if (value.type() == script_value_type::jai_object_type) {
            value.set_type_info(expr->type);
        }
        push_value(std::move(value));
        return {};
    }
    
    std::string className = expr->type->type_name;

    // Evaluate all arguments
    std::vector<script_value> args;
    if constexpr (Replaying) { if (rs) args = rs->partial; }
    for (size_t ai = args.size(); ai < expr->arguments.size(); ++ai) {
        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->arguments[ai].get(), rs, ai), expr, rp_new_class, ai, args);
        args.push_back(std::move(pop_value()));
        capture_call_argument(expr->arg_captures, ai, args.back(), [&] {
            auto ctor = environment_->get(className);
            return ctor ? detail::callee_binding(ctor.value(), ai, false, expr->arguments.size()) : detail::arg_binding::opaque;
        });
    }

    // Look for a constructor function registered with this class name
    // The class builder registers constructors as overloaded functions
    auto ctor_result = environment_->get(className);
    if (ctor_result && ctor_result.value().is_function()) {
        script_value constructorFunc = std::move(ctor_result.value());
        const script_function& func = constructorFunc.as_function();
        auto result = invoke_constructor(func, args);
        if (!result) {
            // Function returned error - propagate it up
            return result.error_value();
        }
        push_value(std::move(result.value()));
        return {};
    }

    // No constructor found for class
    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
        "No constructor found for class '{0}'", expr->type->id);
}

template<bool Replaying>
checked_result<void> interpreter::visit_ternary_expr_t(ternary_expr* expr, const replay_point* rs) {
    // Evaluate the condition (a replay of the chosen arm carries it as partial[0])
    if (!parked<Replaying>(rs, 0)) {
        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->condition.get(), rs, 0), expr, rp_operand, 0, {});
    }
    script_value conditionValue = parked<Replaying>(rs, 0) ? rs->partial[0] : pop_value();

    // A script throw unwinding out of the condition leaves a null PLACEHOLDER, not a
    // bool: the unchecked read below would deref get_if<BOOL>()'s nullptr (fuzz seed
    // 285 AV), and neither branch may run. Placeholder result, unwind propagates.
    if (is_unwinding_) {
        push_value(make_value());
        return {};
    }

    // OPTIMIZATION: If condition is guaranteed to return bool, skip type dispatch
    bool conditionIsTruthy;
    if (expression_returns_bool(expr->condition.get())) {
        conditionIsTruthy = conditionValue.unchecked_as_bool();
    } else {
        conditionIsTruthy = is_truthy(conditionValue);
    }

    // Evaluate only the selected branch (short-circuit evaluation)
    if (conditionIsTruthy) {
        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->then_expression.get(), rs, 1), expr, rp_operand, 1, (std::vector<script_value>{conditionValue}));
    } else {
        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->else_expression.get(), rs, 1), expr, rp_operand, 1, (std::vector<script_value>{conditionValue}));
    }
    return {};
}

template<bool Replaying>
checked_result<void> interpreter::visit_array_literal_expr_t(array_literal_expr* expr, const replay_point* rs) {
    // Create array script_value with no element type constraint
    // Untyped array literals (e.g., [1, 2, 3]) allow any element type
    // The type will be set by the variable declaration if one exists (e.g., array<int> arr = [...])
    type_info_ptr element_type = nullptr;  // No constraint - allows any type
    script_value arrayValue = script_value::make_array(element_type, engine_);
    size_t element_start = 0;
    if constexpr (Replaying) {
        // the literal under construction rides the frame as partial[0]
        if (rs) { arrayValue = rs->partial[0]; element_start = rs->index; }
    }

    // Get the internal vector to populate
    auto& array = const_cast<std::vector<script_value>&>(arrayValue.as_array());

    // Reserve capacity to avoid reallocations (optimization)
    array.reserve(expr->elements.size());

    // Evaluate each element and add to array. All-detach ruling (2026-07, open
    // question #12): literal construction is a store boundary - bound primitives
    // snapshot like every assignment-shaped store, and element/subscript reads that
    // arrive as reference wrappers normalize to VALUES exactly like assignment
    // (a literal must never hold a live reference into its source; to_json emitted
    // null for them). Copy out through a temp: assigning deref() straight into elem
    // destroys the holder that owns the deref target. (KEEP BYTE-PARALLEL with
    // vm_backend::exec_array)
    for (size_t ei = element_start; ei < expr->elements.size(); ++ei) {
        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->elements[ei].get(), rs, ei), expr, rp_operand, ei, (std::vector<script_value>{arrayValue}));
        script_value elem = pop_value();
        if (elem.is_reference()) [[unlikely]] {
            script_value derefed = elem.deref();
            elem = std::move(derefed);
        }
        if (elem.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
            elem = elem.detached_for_store();
        }
        array.push_back(std::move(elem));
    }

    push_value(std::move(arrayValue));
    return {};
}

template<bool Replaying>
checked_result<void> interpreter::visit_map_literal_expr_t(map_literal_expr* expr, const replay_point* rs) {
    // Create map script_value with no key/value type constraints
    // Untyped map literals (e.g., {"a": 1}) allow any key/value types
    // The type will be set by the variable declaration if one exists (e.g., map<string,int> m = {...})
    type_info_ptr keyType = nullptr;    // No constraint - allows any key type
    type_info_ptr valueType = nullptr;  // No constraint - allows any value type
    script_value mapValue = script_value::make_map(keyType, valueType, engine_);
    // Replay: the map under construction rides the frame as partial[0]; a suspended value
    // expression also parks its evaluated key as partial[1] (ordinals: 2i key, 2i+1 value)
    size_t entry_start = 0;
    std::optional<script_value> parked_key;
    if constexpr (Replaying) {
        if (rs) {
            mapValue = rs->partial[0];
            entry_start = rs->index / 2;
            if (rs->index % 2 == 1) parked_key = rs->partial[1];
        }
    }

    // Get the internal map to populate
    auto& map = const_cast<script_map&>(mapValue.as_map());

    // Evaluate each key-value pair and add to map
    for (size_t ei = entry_start; ei < expr->entries.size(); ++ei) {
        const auto& entry = expr->entries[ei];
        // Evaluate key
        bool key_parked = false;
        if constexpr (Replaying) { key_parked = parked_key.has_value() && ei == entry_start; }
        if (!key_parked) {
            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(entry.first.get(), rs, 2 * ei), expr, rp_operand, 2 * ei, (std::vector<script_value>{mapValue}));
        }
        script_value key = key_parked ? std::move(*parked_key) : pop_value();

        // Evaluate value
        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(entry.second.get(), rs, 2 * ei + 1), expr, rp_operand, 2 * ei + 1, (std::vector<script_value>{mapValue, key}));
        script_value value = pop_value();

        // All-detach ruling (2026-07, #12): literal construction is a store boundary -
        // bound primitives snapshot, and reference-wrapper reads normalize to VALUES
        // like assignment (see the array-literal twin for the argument; KEEP
        // BYTE-PARALLEL with vm_backend::exec_map)
        if (key.is_reference()) [[unlikely]] {
            script_value derefed = key.deref();
            key = std::move(derefed);
        }
        if (value.is_reference()) [[unlikely]] {
            script_value derefed = value.deref();
            value = std::move(derefed);
        }
        if (key.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
            key = key.detached_for_store();
        }
        if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
            value = value.detached_for_store();
        }

        // Insert into map
        map.insert_or_assign(std::move(key), std::move(value));
    }

    push_value(std::move(mapValue));
    return {};
}

checked_result<void> interpreter::visit_this_expr(this_expr* expr) {
    // If we're in a class method context during parsing, allow 'this'
    if (current_class_context_ && current_class_context_->in_method) {
        // Push a placeholder value to continue parsing
        push_value(make_value());
        return {};
    }

    // Try to get 'this' from the current environment
    // Use the symbolizer's cached ID (not interpreter's this_id_ which may be stale)
    script_value* this_ptr = environment_->get_value_ptr(string_symbolizer_->get_this_id());
    if (!this_ptr) {
        // 'this' can only be used inside methods
        return checked_result<void>(make_error_code(runtime_error_code::this_outside_method),
            "'this' can only be used inside methods");
    }
    push_value(*this_ptr);
    return {};
}

checked_result<void> interpreter::visit_super_expr(super_expr* expr) {
    // Super expression is used for accessing parent class methods: super::method()
    // Constructor delegation (Enemy() : super()) is handled in the parser/constructor

    // Get 'this' from the environment - super only makes sense in instance methods
    // Use the symbolizer's cached ID (not interpreter's this_id_ which may be stale)
    script_value* this_ptr = environment_->get_value_ptr(string_symbolizer_->get_this_id());
    if (!this_ptr) {
        // 'this' not found - super used outside of class method
        return checked_result<void>(make_error_code(runtime_error_code::super_outside_method),
            "'super' can only be used inside methods");
    }
    script_value this_value = *this_ptr;
    if (this_value.is_null()) {
        // 'this' is null - super used outside of class method
        return checked_result<void>(make_error_code(runtime_error_code::super_outside_method),
            "'super' can only be used inside methods");
    }

    // Push 'this' onto the stack - visit_member_expr will handle the parent lookup
    // when it detects that the object expression is a super_expr
    push_value(std::move(this_value));
    return {};
}

// cast<T>(e) / cast<T&>(e) / cast<shared_ptr<T>>(e): operand shape, class verdict and
// class-target outcomes come from detail/cast_kernel.hpp; primitive/container targets
// convert through the assignment table. KEEP BYTE-PARALLEL with vm_backend::exec_cast.
template<bool Replaying>
checked_result<void> interpreter::visit_cast_expr_t(cast_expr* expr, const replay_point* rs) {
    script_value operand;
    switch (detail::cast_operand_mode_of(expr)) {
        case detail::cast_operand_mode::identifier: {
            // The ref-decl identifier arm: slot-first, then env
            auto* ident = static_cast<identifier_expr*>(expr->operand.get());
            const uint64_t targetSymbolId = string_symbolizer_->intern(ident->name);
            script_value* targetPtr = resolve_local_or_env(ident->slot_index, targetSymbolId, ident->global_qualified);
            if (!targetPtr) {
                auto minted = bind_bare_cpp_property_ref(targetSymbolId, ident->global_qualified);
                if (!minted) {
                    JAISCRIPT_TRY(bare_reference_access(targetSymbolId));
                    if (auto method = bare_method_reference_error(ident)) { return *method; }
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable), environment_->undefined_text(targetSymbolId, "Cannot take reference of undefined variable"), targetSymbolId);
                }
                if (!*minted) {
                    return minted->error_value();
                }
                operand = std::move(minted->value());
                break;
            }
            if (targetPtr->is_reference()) {
                auto aliased = decl_ref_alias(*targetPtr, engine_);
                if (!aliased) {
                    return aliased.error_value();
                }
                operand = std::move(aliased.value());
            } else {
                operand = share_boxed_env_storage(*targetPtr, engine_);
            }
            break;
        }
        case detail::cast_operand_mode::lvalue_chain: {
            auto resolved = detail::resolve_ref_lvalue(expr->operand.get(),
                detail::caller_frame_view{call_stack_.empty() ? nullptr : &call_stack_.back()},
                environment_.get(), engine_, string_symbolizer_);
            if (!resolved) {
                if (resolved.error() != make_error_code(runtime_error_code::invalid_reference)) {
                    return resolved.error_value();
                }
                return checked_result<void>(make_error_code(runtime_error_code::invalid_reference), detail::cast_non_lvalue_text);
            }
            operand = std::move(resolved.value());
            break;
        }
        case detail::cast_operand_mode::value:
            JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->operand.get(), rs, 0), expr, rp_operand, 0, {});
            operand = pop_value();
            if (is_unwinding_) {
                push_value(make_value());
                return {};
            }
            break;
    }

    if (expr->by_reference) {
        auto out = detail::cast_reference(detail::cast_pin_temporary(std::move(operand), engine_), *expr, engine_, string_symbolizer_);
        if (!out) {
            return out.error_value();
        }
        push_value(std::move(out.value()));
        return {};
    }

    const bool lvalue_source = operand.is_reference() || detail::cast_lvalue_source(expr->operand.get());
    if (operand.is_reference()) {
        script_value derefed = operand.deref();
        operand = std::move(derefed);
    }
    if (detail::cast_target_is_class(*expr->target)) {
        auto out = detail::cast_class_value(std::move(operand), *expr, lvalue_source, engine_, string_symbolizer_);
        if (!out) {
            return out.error_value();
        }
        push_value(std::move(out.value()));
        return {};
    }
    if (detail::cast_char_rule_applies(operand, *expr->target)) {
        auto out = detail::cast_char_value(operand, *expr, engine_, string_symbolizer_);
        if (!out) {
            return out.error_value();
        }
        script_value converted = std::move(out.value());
        converted.set_type_info(expr->target);
        push_value(std::move(converted));
        return {};
    }
    if (lvalue_source) {
        script_value copy = clone_for_assignment(operand);
        operand = std::move(copy);
    }
    auto enforced = enforce_type_compatibility(operand, expr->target, "cast");
    if (!enforced) {
		if (enforced.error() == make_error_code(runtime_error_code::invalid_numeric_operand)) {
			return enforced.error_value();
		}
        return detail::cast_error<void>(operand, *expr, string_symbolizer_);
    }
    script_value out = std::move(enforced.value());
    out.set_type_info(expr->target);
    push_value(std::move(out));
    return {};
}

template<bool Replaying>
checked_result<void> interpreter::visit_throw_expr_t(throw_expr* expr, const replay_point* rs) {
    if (expr->value) {
        // Evaluate the expression to throw
        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->value.get(), rs, 0), expr, rp_operand, 0, {});
        script_value val = pop_value();

        // Store the exception value and convert to string for exception message
        active_exception_value_ = val;
        std::string message = val.to_string();
        current_exception_ = script_exception(message, expr->location);
        current_exception_->thrown = true;
    } else {
        // Re-throw current exception; with none active it is a catchable runtime error (KEEP BYTE-PARALLEL with vm_backend::exec_throw)
        if (!current_exception_) {
            return checked_result<void>(make_error_code(runtime_error_code::invalid_operation), detail::bare_throw_text);
        }
        // Keep the existing active_exception_value_
    }

    is_unwinding_ = true;
    // throw is an expression (docs/grammar.md): it yields the unwinding placeholder every operand consumer pops
    push_value(make_value());
    return {};
}

template<bool Replaying>
checked_result<void> interpreter::visit_yield_expr_t(yield_expr* expr, const replay_point* rs) {
    if (!active_coroutine_) {
        return checked_result<void>(make_error_code(runtime_error_code::evaluation_failed),
            "yield used outside of coroutine");
    }

    // Evaluate the yield value (if any)
    script_value value = make_value();
    if (expr->value) {
        JAISCRIPT_TRY_OPERAND(eval_child<Replaying>(expr->value.get(), rs, 0), expr, rp_operand, 0, {});
        value = pop_value();
    }

    // Publish the value and suspend HERE: the signal unwinds the enclosing expression (each
    // visitor parks its operands), the owning statement consumes it, and the resume
    // substitutes null for this yield (vm exec_yield parity)
    active_coroutine_->do_yield(std::move(value));
    hasYieldRequest_ = true;
    return checked_result<void>(make_error_code(runtime_error_code::coroutine_suspended));
}

checked_result<void> interpreter::visit_if_stmt(if_stmt* stmt) {
    bool is_true;
    bool resuming = false;
    bool resume_cond = false;

    // Check if we're resuming into a branch from a coroutine yield
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        auto* cont = coro_state.peek_continuation(stmt);
        if (cont) {
            resume_cond = cont->phase == rp_cond;   // suspended in the condition itself
            is_true = (cont->index == 0);  // 0=then branch, 1=else branch
            coro_state.pop_continuation();
            resuming = !resume_cond;
        }
    }

    if (!resuming) {
        // Evaluate the condition
        auto cond_result = resume_cond ? replay_expression(stmt->condition.get())
                                       : dispatch_expr(stmt->condition.get());
        if (!cond_result) [[unlikely]] {
            if (is_yield_signal(cond_result)) { park_operands(stmt, rp_cond, 0, {}); return {}; }
            return cond_result.error_value();
        }
        script_value conditionValue = pop_value();

        // Throw unwinding out of the condition: the popped value is a null
        // placeholder (unchecked_as_bool would deref nullptr - fuzz seed 285 AV)
        // and neither branch may run
        if (is_unwinding_) {
            return {};
        }

        // OPTIMIZATION: If condition is guaranteed to return bool, skip type dispatch
        if (expression_returns_bool(stmt->condition.get())) {
            is_true = conditionValue.unchecked_as_bool();
        } else {
            is_true = is_truthy(conditionValue);
        }
    }

    // Execute appropriate branch based on truthiness. On yield the branch owns the frames;
    // a brace-less branch that completed AT the yield left none, and the if is done too.
    statement* branch = is_true ? stmt->then_statement.get() : stmt->else_statement.get();
    if (branch) {
        JAISCRIPT_TRY(resuming ? dispatch_stmt_resuming(branch) : dispatch_stmt(branch));
        if (hasYieldRequest_ && active_coroutine_) {
            auto& coro_state = coroutine_state(*active_coroutine_);
            if (coro_state.has_continuations()) {
                coro_state.push_continuation(stmt, is_true ? 0 : 1);
            }
        }
    }
    return {};
}

checked_result<void> interpreter::visit_while_stmt(while_stmt* stmt) {
    // OPTIMIZATION: Pre-check if condition is guaranteed to return bool
    const bool condition_returns_bool = expression_returns_bool(stmt->condition.get());

    // Check if we're resuming into this while loop from a coroutine yield
    bool skip_first_condition = false;
    bool replay_condition = false;
    bool resume_body = false;
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        auto* cont = coro_state.peek_continuation(stmt);
        if (cont) {
            replay_condition = cont->phase == rp_cond;   // suspended in the condition itself
            // a brace-less body that completed at the yield: the next step is the condition
            skip_first_condition = cont->phase == rp_none;
            resume_body = skip_first_condition;
            coro_state.pop_continuation();
        }
    }

    while (true) {
        if (execution_limit_exhausted()) [[unlikely]] {
            return execution_limit_failure();
        }

        if (skip_first_condition) {
            skip_first_condition = false;  // Only skip once
        } else {
            // Evaluate the condition
            auto cond_result = replay_condition ? replay_expression(stmt->condition.get())
                                                : dispatch_expr(stmt->condition.get());
            replay_condition = false;
            if (!cond_result) [[unlikely]] {
                if (is_yield_signal(cond_result)) { park_operands(stmt, rp_cond, 0, {}); return {}; }
                return cond_result.error_value();
            }

            // Throw unwinding out of the condition: placeholder on the stack, not a
            // bool (fuzz seed 285 AV) - stop the loop, the unwind propagates
            if (is_unwinding_) {
                valueStack_.discard();
                break;
            }

            const auto& val = valueStack_.top();
            bool is_true;

            // FAST PATH: If we KNOW the condition returns bool, skip type dispatch
            if (condition_returns_bool) {
                is_true = val.unchecked_as_bool();
            } else {
                is_true = is_truthy(val);
            }

            valueStack_.discard();

            if (!is_true) {
                break;
            }
        }

        // Execute the loop body
        auto result = resume_body ? dispatch_stmt_resuming(stmt->body.get()) : dispatch_stmt(stmt->body.get());
        resume_body = false;
        if (!result) return result;

        // A script `throw` sets is_unwinding_ and the body short-circuits
        // without advancing any loop state. Stop the loop so the exception can
        // propagate; otherwise we re-evaluate the condition forever (condition
        // evaluation no-ops while unwinding) and hang.
        if (is_unwinding_) {
            break;
        }

        // Check for control flow changes (break/continue/return)
        if (hasBreakRequest_) {
            hasBreakRequest_ = false;  // Clear the flag
            break;
        }

        if (hasContinueRequest_) {
            hasContinueRequest_ = false;  // Clear the flag
            continue;
        }

        if (hasReturnValue_) {
            break;
        }

        // On yield: record continuation and return
        if (hasYieldRequest_) {
            if (active_coroutine_) {
                auto& coro_state = coroutine_state(*active_coroutine_);
                if (coro_state.has_continuations()) coro_state.push_continuation(stmt, 0);
                else coro_state.push_replay(stmt, rp_body_done, 0);
            }
            return {};
        }
    }
    return {};
}

checked_result<void> interpreter::visit_for_stmt(for_stmt* stmt) {
    // FAST PATH: Detect and optimize integer counting loops
    // Pattern: for (int/auto/var i = START; i < END; ++i) { body }
    //
    // Unified fast path for all declaration types (auto/int/var):
    // - Native C++ loop with cached pointers for counter, end, and step
    // - Per-iteration type validation (~0.1ns) handles edge cases
    // - Falls back to slow path if type changes mid-loop
    //
    // IMPORTANT: Skip fast path when inside a coroutine - the fast path doesn't
    // support yield/resume since it uses native C++ loop counters that can't be saved.
    // The slow path handles yield correctly via continuation points.
    //
    if (stmt->initializer && stmt->condition && stmt->update && !active_coroutine_) {
        auto* init_var = stmt->initializer->get_type() == node_type::variable_decl
            ? static_cast<variable_decl*>(stmt->initializer.get()) : nullptr;
        auto* cond_binary = stmt->condition->get_type() == node_type::binary_expr
            ? static_cast<binary_expr*>(stmt->condition.get()) : nullptr;

        // Try to match update patterns: ++i, i++, i += step, i = i + step
        auto* update_unary = stmt->update->get_type() == node_type::unary_expr
            ? static_cast<unary_expr*>(stmt->update.get()) : nullptr;
        auto* update_assign = stmt->update->get_type() == node_type::assignment_expr
            ? static_cast<assignment_expr*>(stmt->update.get()) : nullptr;

        // Extract update info: which variable, what step
        uint64_t update_var_id = UINT64_MAX;
        script_int step_value = 1;  // Default for ++i
        uint64_t step_var_id = UINT64_MAX;  // For dynamic step (i += j)
        size_t step_var_slot = SIZE_MAX;
        bool valid_update = false;
        // Overflow attribution: the update error names the SOURCE operator (vm cfor parity)
        const char* update_overflow_msg = "Integer overflow in '+='";

        if (update_unary && update_unary->op.type == token_type::plus_plus) {
            // Pattern: ++i or i++
            if (update_unary->operand->get_type() == node_type::identifier_expr) {
                auto* update_id = static_cast<identifier_expr*>(update_unary->operand.get());
                update_var_id = update_id->symbol_id;
                step_value = 1;
                valid_update = true;
                update_overflow_msg = "Integer overflow in '++'";
            }
        } else if (update_unary && update_unary->op.type == token_type::minus_minus) {
            // Pattern: --i or i-- (step = -1, but this is unusual for counting up)
            if (update_unary->operand->get_type() == node_type::identifier_expr) {
                auto* update_id = static_cast<identifier_expr*>(update_unary->operand.get());
                update_var_id = update_id->symbol_id;
                step_value = -1;
                valid_update = true;
                update_overflow_msg = "Integer overflow in '--'";
            }
        } else if (update_assign && update_assign->op.type == token_type::plus_equal) {
            // Pattern: i += step (literal or variable)
            if (update_assign->target->get_type() == node_type::identifier_expr) {
                auto* update_id = static_cast<identifier_expr*>(update_assign->target.get());
                update_var_id = update_id->symbol_id;
                // Check if step is an int literal
                if (update_assign->value->get_type() == node_type::literal_expr) {
                    auto* step_lit = static_cast<literal_expr*>(update_assign->value.get());
                    if (step_lit->value.raw_storage_index() == script_value::TYPEID_INT) {  // int literal
                        step_value = step_lit->value.unchecked_as_int();
                        valid_update = true;
                    }
                }
                // Check if step is an identifier (i += j where j is int)
                else if (update_assign->value->get_type() == node_type::identifier_expr &&
                         !static_cast<identifier_expr*>(update_assign->value.get())->global_qualified) {
                    auto* step_id = static_cast<identifier_expr*>(update_assign->value.get());
                    script_value* step_ptr = resolve_local_or_env(step_id->slot_index, step_id->symbol_id);
                    if (step_ptr && step_ptr->raw_storage_index() == script_value::TYPEID_INT) {  // int value
                        step_var_id = step_id->symbol_id;
                        step_var_slot = step_id->slot_index;
                        step_value = step_ptr->unchecked_as_int();  // Initial value
                        valid_update = true;
                    }
                }
            }
        } else if (update_assign && update_assign->op.type == token_type::minus_equal) {
            // Pattern: i -= step (literal or variable)
            update_overflow_msg = "Integer overflow in '-='";
            if (update_assign->target->get_type() == node_type::identifier_expr) {
                auto* update_id = static_cast<identifier_expr*>(update_assign->target.get());
                update_var_id = update_id->symbol_id;
                if (update_assign->value->get_type() == node_type::literal_expr) {
                    auto* step_lit = static_cast<literal_expr*>(update_assign->value.get());
                    if (step_lit->value.raw_storage_index() == script_value::TYPEID_INT) {
                        // Direction comes from step_subtract below; negating here too made
                        // `i -= <literal>` ASCEND (i -= -step), an infinite counting loop
                        step_value = step_lit->value.unchecked_as_int();
                        valid_update = true;
                    }
                }
                // Check if step is an identifier (i -= j where j is int)
                else if (update_assign->value->get_type() == node_type::identifier_expr &&
                         !static_cast<identifier_expr*>(update_assign->value.get())->global_qualified) {
                    auto* step_id = static_cast<identifier_expr*>(update_assign->value.get());
                    script_value* step_ptr = resolve_local_or_env(step_id->slot_index, step_id->symbol_id);
                    if (step_ptr && step_ptr->raw_storage_index() == script_value::TYPEID_INT) {  // int value
                        step_var_id = step_id->symbol_id;
                        step_var_slot = step_id->slot_index;
                        step_value = step_ptr->unchecked_as_int();  // Initial value
                        valid_update = true;
                    }
                }
            }
        }

        if (init_var && cond_binary && valid_update) {
            // Select comparison function based on operator type
            // Supports: <, <=, >, >=, ==, !=
            using compare_fn = bool(*)(script_int, script_int);
            compare_fn cmp = nullptr;
            switch (cond_binary->op.type) {
                case token_type::less:          cmp = [](script_int a, script_int b) { return a < b; }; break;
                case token_type::less_equal:    cmp = [](script_int a, script_int b) { return a <= b; }; break;
                case token_type::greater:       cmp = [](script_int a, script_int b) { return a > b; }; break;
                case token_type::greater_equal: cmp = [](script_int a, script_int b) { return a >= b; }; break;
                case token_type::equal_equal:   cmp = [](script_int a, script_int b) { return a == b; }; break;
                case token_type::bang_equal:    cmp = [](script_int a, script_int b) { return a != b; }; break;
                default: break;
            }

            if (cmp) {
                auto* cond_id = cond_binary->left->get_type() == node_type::identifier_expr
                    ? static_cast<identifier_expr*>(cond_binary->left.get()) : nullptr;

                // End value can be literal OR variable
                script_int end_literal = 0;
                uint64_t end_var_id = UINT64_MAX;
                size_t end_var_slot = SIZE_MAX;
                bool has_end = false;

                if (cond_binary->right->get_type() == node_type::literal_expr) {
                    auto* cond_lit = static_cast<literal_expr*>(cond_binary->right.get());
                    if (cond_lit->value.raw_storage_index() == script_value::TYPEID_INT) {  // int literal
                        end_literal = cond_lit->value.unchecked_as_int();
                        has_end = true;
                    }
                } else if (cond_binary->right->get_type() == node_type::identifier_expr &&
                           !static_cast<identifier_expr*>(cond_binary->right.get())->global_qualified) {
                    auto* cond_end_id = static_cast<identifier_expr*>(cond_binary->right.get());
                    // End is a variable - we'll bind to its pointer (::name bounds stay
                    // on the generic loop: the cached slot/id pair can't carry the flag)
                    end_var_id = cond_end_id->symbol_id;
                    end_var_slot = cond_end_id->slot_index;
                    has_end = true;
                }

                if (cond_id && has_end) {
                    // Check: update variable matches condition variable
                    if (update_var_id == cond_id->symbol_id) {
                        // Check: init is variable i = literal (same identifier, int literal)
                        auto* init_lit = init_var->initializer && init_var->initializer->get_type() == node_type::literal_expr
                            ? static_cast<literal_expr*>(init_var->initializer.get()) : nullptr;

                        if (init_lit && init_lit->value.raw_storage_index() == script_value::TYPEID_INT &&
                            init_var->name_id == cond_id->symbol_id) {

                            // === PATTERN MATCHED! Run optimized native loop ===
                            // Unified fast path for all integer counting loops (auto/int/var)
                            // Type validation per-iteration is negligible (~0.1ns) vs body dispatch
                            script_int i = init_lit->value.unchecked_as_int();
                            uint64_t var_id = init_var->name_id;

                            // Setup environment once (from pool)
                            auto previous = environment_;
                            auto loop_env = get_block_environment(previous);
                            environment_ = loop_env;

                            // Create value preserving declared type (var = any_type, auto = nullptr, int = int_type)
                            script_value init_val = make_value(i);
                            if (init_var->type) {
                                init_val.set_type_info(init_var->type);
                            }
                            // Slotted counters (function scope) live in the frame local, not
                            // the loop env — body identifiers resolve slot-first to the same
                            // storage. Env define stays for top-level counters.
                            script_value* var_ptr = nullptr;
                            if (init_var->slot_index != SIZE_MAX && !call_stack_.empty()) {
                                call_stack_.back().set_local(init_var->slot_index, std::move(init_val));
                                var_ptr = call_stack_.back().get_local(init_var->slot_index);
                            } else {
                                environment_->define(var_id, std::move(init_val));
                                var_ptr = environment_->get_value_ptr(var_id);
                            }

                            // Bind end value pointer (if variable) AFTER environment setup.
                            // Resolve through references at prep (escape-boxed vars: the
                            // cell inner is heap-stable) and keep the script_value* so the
                            // per-iteration guard can re-check the storage kind - a mid-loop
                            // box-on-demand of the variable demotes gracefully.
                            script_value* end_target = nullptr;
                            script_int end_val = end_literal;
                            if (end_var_id != UINT64_MAX) {
                                script_value* end_sv = resolve_local_or_env(end_var_slot, end_var_id);
                                if (end_sv) { end_target = &end_sv->deref(); }
                                if (!end_target || end_target->raw_storage_index() != script_value::TYPEID_INT ||
                                    end_target->is_cpp_bound()) {
                                    // End variable not int - fall back to slow path
                                    release_environment(loop_env);
                                    environment_ = previous;
                                    goto slow_path;
                                }
                            }

                            // Bind step value pointer (if variable)
                            script_value* step_target = nullptr;
                            if (step_var_id != UINT64_MAX) {
                                script_value* step_sv = resolve_local_or_env(step_var_slot, step_var_id);
                                if (step_sv) { step_target = &step_sv->deref(); }
                                if (!step_target || step_target->raw_storage_index() != script_value::TYPEID_INT ||
                                    step_target->is_cpp_bound()) {
                                    // Step variable not int - fall back to slow path
                                    release_environment(loop_env);
                                    environment_ = previous;
                                    goto slow_path;
                                }
                            }

                            // Determine step operation: += or -=
                            const bool step_subtract = update_assign &&
                                update_assign->op.type == token_type::minus_equal;

                            // === UNIFIED FAST PATH ===
                            // Native C++ loop with cached pointers for end and step
                            // Per-iteration type check is cheap (~0.1ns) and handles edge cases
                            bool fell_through = false;

                            // Optimization: If body is a block, pre-allocate its environment
                            // and reuse across iterations (define() overwrites existing vars in place)
                            auto* body_block = stmt->body->get_type() == node_type::block_stmt
                                ? static_cast<block_stmt*>(stmt->body.get()) : nullptr;
                            std::shared_ptr<environment> body_env = nullptr;
                            if (body_block) {
                                body_env = get_block_environment(loop_env);
                            }

                            while (true) {
                                if (execution_limit_exhausted()) [[unlikely]] {
                                    if (body_env) { release_environment(body_env); }
                                    release_environment(loop_env);
                                    environment_ = previous;
                                    return execution_limit_failure();
                                }

                                // Storage-kind guard on cached targets (box-on-demand safety)
                                if ((end_target && end_target->raw_storage_index() != 1) ||
                                    (step_target && step_target->raw_storage_index() != 1)) [[unlikely]] {
                                    fell_through = true;
                                    break;
                                }
                                script_int current_end = end_target ? end_target->unchecked_as_int() : end_val;
                                if (!cmp(i, current_end)) break;

                                // Type validation - if type changed, fall back to slow path
                                if (var_ptr->raw_storage_index() != 1) [[unlikely]] {
                                    fell_through = true;
                                    break;
                                }
                                var_ptr->unchecked_as_int_ref() = i;

                                // Execute body - use pre-allocated environment for blocks
                                if (body_block) {
                                    // Reuse body_env - it's already a child of loop_env
                                    environment_ = body_env;
                                    for (const auto& decl : body_block->declarations) {
                                        auto result = dispatch_decl(decl.get());
                                        if (valueStack_.size() > 0) {
                                            valueStack_.clear();
                                        }
                                        if (!result) {
                                            release_environment(body_env);
                                            release_environment(loop_env);
                                            environment_ = previous;
                                            return result;
                                        }
                                        if (is_unwinding_ || hasBreakRequest_ || hasContinueRequest_ || hasReturnValue_ || hasYieldRequest_) {
                                            break;
                                        }
                                    }
                                    valueStack_.clear();
                                    // Fresh body scope per iteration (vm/while parity: body shadow decls must
                                    // not persist); clear_values early-outs when the body defined nothing
                                    body_env->clear_values();
                                    environment_ = loop_env;
                                } else {
                                    auto result = dispatch_stmt(stmt->body.get());
                                    if (!result) {
                                        release_environment(loop_env);
                                        environment_ = previous;
                                        return result;
                                    }
                                }

                                if (is_unwinding_) break;  // script throw unwinding: stop the loop
                                if (hasBreakRequest_) { hasBreakRequest_ = false; break; }
                                if (hasReturnValue_ || hasYieldRequest_) break;
                                if (hasContinueRequest_) hasContinueRequest_ = false;

                                // The body can move the frame's locals (a declaration at a new slot grows the vector, a
                                // callee's frame reallocates the call stack): the cached counter, bound and step pointers
                                // are re-resolved before they are read again (a use-after-free otherwise; Debug's fill
                                // tripped the kind guard into the general path, Release read the stale bytes)
                                if (init_var->slot_index != SIZE_MAX && !call_stack_.empty()) { var_ptr = call_stack_.back().get_local(init_var->slot_index); }
                                else { var_ptr = environment_->get_value_ptr(var_id); }
                                if (!var_ptr) [[unlikely]] { fell_through = true; break; }
                                if (end_var_id != UINT64_MAX) {
                                    script_value* end_sv = resolve_local_or_env(end_var_slot, end_var_id);
                                    end_target = end_sv ? &end_sv->deref() : nullptr;
                                    if (!end_target) [[unlikely]] { fell_through = true; break; }
                                }
                                if (step_var_id != UINT64_MAX) {
                                    script_value* step_sv = resolve_local_or_env(step_var_slot, step_var_id);
                                    step_target = step_sv ? &step_sv->deref() : nullptr;
                                    if (!step_target) [[unlikely]] { fell_through = true; break; }
                                }

                                // General-path parity: honor body writes to the loop variable —
                                // adopt int writes into the native counter; on a type change run
                                // the real update once, then continue via the fallthrough loop
                                if (var_ptr->raw_storage_index() != 1) [[unlikely]] {
                                    if (stmt->update) {
                                        auto update_result = dispatch_expr(stmt->update.get());
                                        if (!update_result) {
                                            if (body_env) { release_environment(body_env); }
                                            release_environment(loop_env);
                                            environment_ = previous;
                                            return update_result;
                                        }
                                        pop_value();
                                    }
                                    fell_through = true;
                                    break;
                                }
                                i = var_ptr->unchecked_as_int();

                                script_int step = step_target ? step_target->unchecked_as_int() : step_value;
                                script_int next;
                                const bool step_overflow = step_subtract
                                    ? !ints::try_sub(i, step, next)
                                    : !ints::try_add(i, step, next);
                                if (step_overflow) [[unlikely]] {
                                    // vm cfor parity: the update applies the overflow policy and
                                    // names the source operator
                                    if (body_env) { release_environment(body_env); }
                                    release_environment(loop_env);
                                    environment_ = previous;
                                    return int_overflow_v(update_overflow_msg);
                                }
                                i = next;
                            }

                            // Release body environment if we allocated it
                            if (body_env) {
                                release_environment(body_env);
                            }

                            // If type changed mid-loop, continue with slow path dispatch
                            if (fell_through) [[unlikely]] {
                                while (true) {
                                    // Same back-edge limit check as every other loop path: a counter
                                    // boxed mid-loop (bound by reference once) used to land here
                                    // unbudgeted and spin forever (fuzz seed 33294)
                                    if (execution_limit_exhausted()) [[unlikely]] {
                                        release_environment(loop_env);
                                        environment_ = previous;
                                        return execution_limit_failure();
                                    }
                                    auto cond_result = dispatch_expr(stmt->condition.get());
                                    if (!cond_result) {
                                        release_environment(loop_env);
                                        environment_ = previous;
                                        return cond_result;
                                    }
                                    if (!is_truthy(pop_value())) break;

                                    auto body_result = dispatch_stmt(stmt->body.get());
                                    if (!body_result) {
                                        release_environment(loop_env);
                                        environment_ = previous;
                                        return body_result;
                                    }

                                    if (is_unwinding_) break;
                                    if (hasBreakRequest_) { hasBreakRequest_ = false; break; }
                                    if (hasReturnValue_ || hasYieldRequest_) break;
                                    if (hasContinueRequest_) hasContinueRequest_ = false;

                                    if (stmt->update) {
                                        auto update_result = dispatch_expr(stmt->update.get());
                                        if (!update_result) {
                                            release_environment(loop_env);
                                            environment_ = previous;
                                            return update_result;
                                        }
                                        pop_value();
                                    }
                                }
                            }

                            release_environment(loop_env);
                            environment_ = previous;
                            return {};
                        }
                    }
                }
            }
        }
    slow_path: ;  // Empty statement for goto target
    }

    // === GENERAL PATH: Standard for-loop handling ===

    // Check if we're resuming into this for-loop from a coroutine yield
    bool resuming_loop = false;
    uint32_t resume_phase = rp_none;
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        auto* cont = coro_state.peek_continuation(stmt);
        if (cont) {
            resuming_loop = true;
            resume_phase = cont->phase;
            // Restore the for-loop's environment (includes loop variable scope)
            if (cont->saved_env) {
                environment_ = cont->saved_env;
            }
            coro_state.pop_continuation();
            // DON'T create new scope or execute initializer - saved environment has them
        }
    }

    // For normal execution, previous is the current environment.
    // For resume, previous should be the parent of the restored for-loop scope,
    // so we correctly restore the outer scope on exit.
    auto previous = resuming_loop ? environment_->get_parent() : environment_;
    bool owns_scope = false;

    if (!resuming_loop) {
        // Create new scope for the for loop (initialization variables should be scoped)
        auto loop_env = get_block_environment(environment_);
        environment_ = loop_env;
        owns_scope = true;

        // Execute initialization (if present)
        if (stmt->initializer) {
            auto result = dispatch_decl(stmt->initializer.get());
            if (!result) {
                release_environment(loop_env);
                environment_ = previous;
                return result;
            }
            if (hasYieldRequest_) [[unlikely]] {
                // the initializer suspended: its own frames are parked, keep the loop scope
                if (active_coroutine_) {
                    coroutine_state(*active_coroutine_).push_replay(stmt, rp_init, 0, {}, environment_);
                }
                environment_ = previous;
                return {};
            }
        }
    } else if (resume_phase == rp_init && stmt->initializer) {
        auto result = dispatch_decl_resuming(stmt->initializer.get());
        if (!result) {
            environment_ = previous;
            return result;
        }
        if (hasYieldRequest_) [[unlikely]] {
            if (active_coroutine_) {
                coroutine_state(*active_coroutine_).push_replay(stmt, rp_init, 0, {}, environment_);
            }
            environment_ = previous;
            return {};
        }
    }

    // Error capture for lambdas (avoid throwing from hot path)
    std::optional<checked_result<void>> error;
    // A condition/update suspension keeps the loop scope alive for the resume
    bool park_loop_env = false;

    // OPTIMIZATION: Pre-check if condition is guaranteed to return bool
    // This allows us to skip is_truthy() type dispatch entirely
    const bool condition_returns_bool = stmt->condition && expression_returns_bool(stmt->condition.get());

    // Lambda helpers (ChaiScript-style) for cleaner, more optimizable code
    bool replay_condition = resume_phase == rp_cond;
    bool replay_update = resume_phase == rp_update;
    auto eval_condition = [&]() -> bool {
        if (!stmt->condition) return true;  // No condition = infinite loop

        auto result = replay_condition ? replay_expression(stmt->condition.get())
                                       : dispatch_expr(stmt->condition.get());
        replay_condition = false;
        if (!result) {
            if (is_yield_signal(result)) {
                if (active_coroutine_) {
                    coroutine_state(*active_coroutine_).push_replay(stmt, rp_cond, 0, {}, environment_);
                }
                park_loop_env = true;
                return false;
            }
            error = result;  // Capture error, signal loop termination
            return false;
        }

        // Throw unwinding out of the condition: placeholder on the stack, not a
        // bool (fuzz seed 285 AV) - exit the loop, the unwind propagates
        if (is_unwinding_) {
            valueStack_.discard();
            return false;
        }

        const auto& val = valueStack_.top();
        bool is_true;

        // FAST PATH: If we KNOW the condition returns bool (comparisons, logical ops),
        // use unchecked direct access - no type dispatch needed!
        if (condition_returns_bool) {
            is_true = val.unchecked_as_bool();
        } else {
            // SLOW PATH: General case - need to check type and convert to bool
            is_true = is_truthy(val);
        }

        valueStack_.discard();
        return is_true;
    };

    auto eval_update = [&]() {
        if (!stmt->update) return;

        auto result = replay_update ? replay_expression(stmt->update.get())
                                    : dispatch_expr(stmt->update.get());
        replay_update = false;
        if (!result) {
            if (is_yield_signal(result)) {
                if (active_coroutine_) {
                    coroutine_state(*active_coroutine_).push_replay(stmt, rp_update, 0, {}, environment_);
                }
                park_loop_env = true;
                return;
            }
            error = result;  // Capture error, loop will terminate on next condition check
            return;
        }

        // Discard the update result if it leaves a value on the stack (optimization)
        if (!valueStack_.empty()) {
            valueStack_.discard();
        }
    };

    // Resuming inside the update (or after a brace-less body that completed at the yield):
    // finish the update before this iteration's condition
    if (replay_update || resume_phase == rp_body_done) {
        eval_update();
        if (park_loop_env || error) {
            environment_ = previous;
            return error ? *error : checked_result<void>{};
        }
    }

    // Skip first condition if resuming (we were mid-iteration when yield happened)
    bool skip_condition = resuming_loop && resume_phase == rp_none;
    bool resume_body = skip_condition;

    // Native C++ for-loop structure
    while (true) {
        if (execution_limit_exhausted()) [[unlikely]] {
            if (owns_scope) {
                release_environment(environment_);
            }
            environment_ = previous;
            return execution_limit_failure();
        }

        if (skip_condition) {
            skip_condition = false;  // Only skip on first resumed iteration
        } else {
            if (!eval_condition()) break;
        }
        if (error || park_loop_env) break;

        // Execute the loop body
        auto result = resume_body ? dispatch_stmt_resuming(stmt->body.get()) : dispatch_stmt(stmt->body.get());
        resume_body = false;
        if (!result) {
            if (owns_scope) {
                release_environment(environment_);
            }
            environment_ = previous;
            return result;
        }

        // A script `throw` unwinds: stop the loop (otherwise the condition/update
        // no-op while unwinding and we spin forever).
        if (is_unwinding_) {
            break;
        }

        // Check for control flow changes (break/continue/return)
        if (hasBreakRequest_) {
            hasBreakRequest_ = false;
            break;
        }

        if (hasReturnValue_) {
            break;
        }

        // On yield: record continuation and return without releasing environment
        if (hasYieldRequest_) {
            if (active_coroutine_) {
                // Save the current environment (for-loop scope or deeper) so we can
                // restore it on resume. For-loop variables are environment-based
                // (not slot-based) and must be accessible.
                auto& coro_state = coroutine_state(*active_coroutine_);
                if (coro_state.has_continuations()) coro_state.push_continuation(stmt, 0, environment_);
                else coro_state.push_replay(stmt, rp_body_done, 0, {}, environment_);
            }
            // Restore environment_ to previous so parent scope is correct
            environment_ = previous;
            return {};
        }

        if (hasContinueRequest_) {
            hasContinueRequest_ = false;
        }

        eval_update();
    }

    // Release the loop environment to destroy all loop variables (only if we created it).
    // A parked condition/update keeps it: the resume runs in that same scope.
    if (owns_scope && !park_loop_env) {
        release_environment(environment_);
    }

    // Restore previous environment
    environment_ = previous;

    // If lambda captured an error, propagate it
    if (error) {
        return *error;
    }

    return {};
}

checked_result<void> interpreter::visit_parallel_for_stmt(parallel_for_stmt* stmt) {
    // The containers evaluate in the enclosing scope on this thread, left to right like the
    // declarators; the region kernel owns everything else (admission, element proof, capture
    // provisioning, fork-join).
    std::vector<script_value> containers;
    containers.reserve(stmt->arity());
    JAISCRIPT_TRY(dispatch_expr(stmt->container.get()));
    containers.push_back(pop_value());
    for (const auto& extra : stmt->extra_containers) {
        JAISCRIPT_TRY(dispatch_expr(extra.get()));
        containers.push_back(pop_value());
    }
    const std::function<const script_value*(size_t)> read_frame_slot = [this](size_t slot) -> const script_value* {
        return call_stack_.empty() ? nullptr : call_stack_.back().get_local(slot);
    };
    return detail::run_parallel_for(*engine_, stmt, containers, environment_, read_frame_slot);
}

checked_result<void> interpreter::visit_range_for_stmt(range_for_stmt* stmt) {
    // Coroutine resume replay: continue the SUSPENDED iteration - restoring the
    // evaluated container and position - instead of re-evaluating the container
    // (which would re-mint an inner coroutine or restart the sequence).
    bool replaying = false;          // mid-iteration: the loop variable is already bound
    bool have_container = false;     // the evaluated container rides the frame
    bool replay_container = false;
    size_t resume_index = 0;
    script_value container = make_value();
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        if (auto* cont = coro_state.peek_continuation(stmt)) {
            replay_container = cont->phase == rp_container;   // suspended in the source expression
            if (cont->saved_container) {
                have_container = true;
                // a brace-less body that completed at the yield: bind the next element normally
                replaying = cont->phase != rp_body_done;
                resume_index = cont->index;
                container = *cont->saved_container;
            }
            coro_state.pop_continuation();
        }
    }
    if (!have_container) {
        // Evaluate the container expression
        auto container_result = replay_container ? replay_expression(stmt->container.get())
                                                 : dispatch_expr(stmt->container.get());
        if (!container_result) [[unlikely]] {
            if (is_yield_signal(container_result)) { park_operands(stmt, rp_container, 0, {}); return {}; }
            return container_result.error_value();
        }
        container = pop_value();
        // A script exception here (e.g. a reload-removed method as the container) must
        // unwind the statement with the ORIGINAL exception, not fall into the
        // not-iterable ladder below (which clobbered it with 'Type mismatch')
        if (is_unwinding_) {
            return {};
        }
        // Nested-read sources (for (x : m["a"])) arrive as reference wrappers -
        // iterate the TARGET; the deref'd handle shares the live node so by-ref
        // mutation lands in the original. Temp-copy discipline as everywhere.
        // (KEEP BYTE-PARALLEL with vm_backend::exec_iter_init)
        if (container.is_reference()) [[unlikely]] {
            script_value derefed = container.deref();
            container = std::move(derefed);
        }
    }

    // Determine if we can use slot-based access (inside a function with an assigned slot)
    const bool use_slot = stmt->variable_slot_index != SIZE_MAX && !call_stack_.empty();
    // The loop variable's reference dies with the loop: a slotted by-reference loop clears
    // its slot on every exit but a yield, so no element cursor outlives the loop (a later
    // parallel region over the same array must not see a live reference the program can no
    // longer name). KEEP BYTE-PARALLEL with vm_backend::release_iter_slots
    struct slot_reference_release {
        interpreter* self; size_t slot; bool armed;
        ~slot_reference_release() {
            if (!armed || self->hasYieldRequest_ || self->call_stack_.empty()) { return; }
            if (script_value* cell = self->call_stack_.back().get_local(slot); cell && cell->is_reference()) {
                *cell = self->make_value();
            }
        }
    } release_loop_reference{this, stmt->variable_slot_index, use_slot && stmt->is_reference};

    // Create a new scope for the loop variable
    push_scope();

    if (container.is_array()) {
        // Iterate over array
        auto& array_storage = container.get_array_storage();
        const size_t array_size = array_storage->size();

        if (array_size > 0) {
            script_value* loop_var_ptr = nullptr;

            if (use_slot) {
                // Slot-based O(1) access: define the variable in the call frame's local slot
                // (on replay the restored frame already holds the current element - don't clobber)
                if (!replaying || !call_stack_.back().get_local(stmt->variable_slot_index)) {
                    call_stack_.back().set_local(stmt->variable_slot_index, make_value());
                }
                loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index);
            } else {
                // OPTIMIZATION: Define loop variable ONCE, then use pointer for direct assignment
                // Use pre-interned symbol ID from parser - no runtime string interning needed
                if (!replaying || !environment_->get_value_ptr(stmt->variable_name_id)) {
                    environment_->define(stmt->variable_name_id, make_value());
                }
                loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id);
            }

            // Re-check the LIVE size each iteration: the loop body can alias the
            // same array (arrays are shared strong_ptr storage) and shrink it
            // (clear/pop/erase). Trusting the cached array_size would index past
            // the end with unchecked operator[] -> OOB read / crash.
            for (size_t i = resume_index; i < array_storage->size(); ++i) {
                if (execution_limit_exhausted()) [[unlikely]] {
                    pop_scope();
                    return execution_limit_failure();
                }

                const bool resume_body = replaying;
                // The body can move the frame's locals (a declaration at a new slot, a callee's frame): the loop
                // variable's pointer is re-resolved before it is written again (a use-after-free write otherwise)
                if (use_slot) { loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index); }
                else { loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id); }
                if (!loop_var_ptr) [[unlikely]] {
                    if (use_slot) { call_stack_.back().set_local(stmt->variable_slot_index, make_value()); loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index); }
                    else { environment_->define(stmt->variable_name_id, make_value()); loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id); }
                }
                if (replaying) {
                    // First replayed iteration: the loop variable is already restored;
                    // the body dispatch below fast-forwards to the yield point
                    replaying = false;
                } else if (stmt->is_reference) {
                    // Reallocation-safe reference (container+index, not a raw element
                    // pointer): a push in the loop body can reallocate the vector, and a
                    // raw pointer would dangle -> heap corruption on write-through (#41).
                    // RULED (2026-07-06): the ref carries the container's declared element
                    // type — for (auto& x : intArr) x = "s" errors like intArr[i] = "s";
                    // var (any-tagged) containers stay unconstrained.
                    type_info_ptr element_constraint = nullptr;
                    if (auto tag = container.get_type_info(); tag && tag->base_type == script_value_type::jai_array_type) {
                        type_info_ptr et = tag->element_type();
                        if (et && et->base_type != script_value_type::jai_any_type &&
                            et->base_type != script_value_type::jai_null_type) {
                            element_constraint = et;
                        }
                    }
                    // Cursor (frame-slot loops only, like the vm twin - top-level loops keep the
                    // per-iteration mint so destructor timing stays byte-parallel): the previous
                    // iteration's holder, when nothing retained it, moves to slot i
                    if (auto* cursor = use_slot ? loop_var_ptr->element_cursor_holder() : nullptr;
                        cursor && cursor->container.get() == array_storage.get() &&
                        cursor->container_element_type == element_constraint) {
                        cursor->container_index = i;
                    } else {
                        *loop_var_ptr = script_value::make_element_reference(array_storage, i, engine_, element_constraint);
                    }
                    if (stmt->element_type) {   // a T& binds an element holding a T (KEEP BYTE-PARALLEL with vm exec_iter_next)
                        auto bound = detail::range_for_bind(*loop_var_ptr, stmt->element_type, true, stmt->variable_name, engine_);
                        if (!bound) { pop_scope(); return bound.error_value(); }
                    }
                } else {
                    // Copy binding: values deep-copy per iteration, shared_ptr
                    // elements share (for (auto e : registry) e.hurt() mutates)
                    script_value element = array_storage->kind() == script_array::kind_t::obj
                        ? clone_for_assignment(array_storage->get(i, engine_))   // object slots deep-copy like boxed elements
                        : array_storage->is_typed()
                        ? array_storage->get(i, engine_)   // raw buffer read
                        : clone_for_assignment(array_storage->values()[i]);
                    if (stmt->element_type) {   // T x : takes each element as T x = element; would
                        auto bound = detail::range_for_bind(std::move(element), stmt->element_type, false, stmt->variable_name, engine_);
                        if (!bound) { pop_scope(); return bound.error_value(); }
                        element = std::move(bound.value());
                    }
                    *loop_var_ptr = std::move(element);
                }

                // Execute loop body
                auto body_result = resume_body ? dispatch_stmt_resuming(stmt->body.get()) : dispatch_stmt(stmt->body.get());
                if (!body_result) {
                    pop_scope();
                    return body_result;
                }

                // A script `throw` unwinds: stop iterating so it can propagate.
                if (is_unwinding_) {
                    break;
                }

                // Check for control flow changes (break/continue/return) - mirror while/for loops
                if (hasBreakRequest_) {
                    hasBreakRequest_ = false;  // Clear the flag
                    break;
                }

                if (hasContinueRequest_) {
                    hasContinueRequest_ = false;  // Clear the flag
                    continue;  // Skip to next iteration
                }

                if (hasReturnValue_) {
                    break;
                }

                // On yield: don't pop scope, let environment stay for coroutine to save.
                // Record the container and position so the resume replay continues THIS
                // iteration instead of re-evaluating the container from scratch.
                if (hasYieldRequest_) {
                    if (active_coroutine_) {
                        auto& coro_state = coroutine_state(*active_coroutine_);
                        if (coro_state.has_continuations()) coro_state.push_continuation(stmt, i, nullptr, container);
                        else coro_state.push_replay(stmt, rp_body_done, i + 1, {}, nullptr, container);
                    }
                    return {};
                }
            }
        }

    } else if (container.is_string() || container.is_utf8_string()) {
        // Iterate a string by value, one char per element like s[i] (a utf8_string: one character, like u[i]); a char is not a
        // cell, so the by-reference binding raises (KEEP BYTE-PARALLEL with vm_backend::exec_iter_init)
        const bool utf8 = container.is_utf8_string();
        if (stmt->is_reference) {
            pop_scope();
            return checked_result<void>(make_error_code(runtime_error_code::invalid_reference),
                utf8 ? detail::utf8_range_for_by_ref_text : detail::string_range_for_by_ref_text);
        }
        const bool cache = !engine_->parallel_region_active();
        auto text_size = [&]() { return utf8 ? detail::utf8::length(container.unchecked_as_utf8(), cache) : container.unchecked_as_string().size(); };
        if (text_size() != 0) {
            script_value* loop_var_ptr = nullptr;
            if (use_slot) {
                if (!replaying || !call_stack_.back().get_local(stmt->variable_slot_index)) {
                    call_stack_.back().set_local(stmt->variable_slot_index, make_value());
                }
                loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index);
            } else {
                if (!replaying || !environment_->get_value_ptr(stmt->variable_name_id)) {
                    environment_->define(stmt->variable_name_id, make_value());
                }
                loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id);
            }

            // Live size each iteration: the body can alias and shrink the shared string
            for (size_t i = resume_index; i < text_size(); ++i) {
                if (execution_limit_exhausted()) [[unlikely]] {
                    pop_scope();
                    return execution_limit_failure();
                }

                const bool resume_body = replaying;
                // The body can move the frame's locals (a declaration at a new slot, a callee's frame): the loop
                // variable's pointer is re-resolved before it is written again (a use-after-free write otherwise)
                if (use_slot) { loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index); }
                else { loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id); }
                if (!loop_var_ptr) [[unlikely]] {
                    if (use_slot) { call_stack_.back().set_local(stmt->variable_slot_index, make_value()); loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index); }
                    else { environment_->define(stmt->variable_name_id, make_value()); loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id); }
                }
                if (replaying) {
                    replaying = false;
                } else {
                    script_value element = utf8 ? detail::utf8::char_value(container.unchecked_as_utf8(), i, engine_, cache)
                                                : script_value(static_cast<script_char>(container.unchecked_as_string()[i]), engine_);
                    if (stmt->element_type) {   // KEEP BYTE-PARALLEL with vm exec_iter_next
                        auto bound = detail::range_for_bind(std::move(element), stmt->element_type, false, stmt->variable_name, engine_);
                        if (!bound) { pop_scope(); return bound.error_value(); }
                        element = std::move(bound.value());
                    }
                    *loop_var_ptr = std::move(element);
                }

                auto body_result = resume_body ? dispatch_stmt_resuming(stmt->body.get()) : dispatch_stmt(stmt->body.get());
                if (!body_result) {
                    pop_scope();
                    return body_result;
                }

                if (is_unwinding_) {
                    break;
                }
                if (hasBreakRequest_) {
                    hasBreakRequest_ = false;
                    break;
                }
                if (hasContinueRequest_) {
                    hasContinueRequest_ = false;
                    continue;
                }
                if (hasReturnValue_) {
                    break;
                }
                if (hasYieldRequest_) {
                    if (active_coroutine_) {
                        auto& coro_state = coroutine_state(*active_coroutine_);
                        if (coro_state.has_continuations()) coro_state.push_continuation(stmt, i, nullptr, container);
                        else coro_state.push_replay(stmt, rp_body_done, i + 1, {}, nullptr, container);
                    }
                    return {};
                }
            }
        }

    } else if (container.is_map()) {
        // Iterate over map - return key-value pairs with first/second access
        auto& map_storage = container.get_map_storage();

        if (!map_storage->empty()) {
            // OPTIMIZATION: Look up pair constructor ONCE before the loop
            uint64_t pair_symbol_id = string_symbolizer_->intern("pair");
            auto pair_result = environment_->get_ref(pair_symbol_id);
            if (!pair_result) {
                pop_scope();
                return pair_result.error_value();
            }
            const script_value& pairConstructor = pair_result.value().get();
            if (!pairConstructor.is_function()) {
                pop_scope();
                return checked_result<void>(make_error_code(runtime_error_code::stdlib_not_loaded),
                    "'pair' type not registered - make sure stdlib is loaded");
            }
            const script_function& pair_func = pairConstructor.as_function();

            script_value* loop_var_ptr = nullptr;

            if (use_slot) {
                // Slot-based O(1) access: define the variable in the call frame's local slot
                // (on replay the restored frame already holds the current element - don't clobber)
                if (!replaying || !call_stack_.back().get_local(stmt->variable_slot_index)) {
                    call_stack_.back().set_local(stmt->variable_slot_index, make_value());
                }
                loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index);
            } else {
                // OPTIMIZATION: Define loop variable ONCE, then use pointer for direct assignment
                // Use pre-interned symbol ID from parser - no runtime string interning needed
                if (!replaying || !environment_->get_value_ptr(stmt->variable_name_id)) {
                    environment_->define(stmt->variable_name_id, make_value());
                }
                loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id);
            }

            size_t entry_index = 0;
            for (auto it = map_storage->begin(); it != map_storage->end(); ++it, ++entry_index) {
                if (entry_index < resume_index) {
                    continue;   // fast-forward to the suspended entry
                }
                if (execution_limit_exhausted()) [[unlikely]] {
                    pop_scope();
                    return execution_limit_failure();
                }

                const bool resume_body = replaying;
                // The body can move the frame's locals (a declaration at a new slot, a callee's frame): the loop
                // variable's pointer is re-resolved before it is written again (a use-after-free write otherwise)
                if (use_slot) { loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index); }
                else { loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id); }
                if (!loop_var_ptr) [[unlikely]] {
                    if (use_slot) { call_stack_.back().set_local(stmt->variable_slot_index, make_value()); loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index); }
                    else { environment_->define(stmt->variable_name_id, make_value()); loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id); }
                }
                if (replaying) {
                    // First replayed iteration: the loop variable is already restored;
                    // the body dispatch below fast-forwards to the yield point
                    replaying = false;
                } else {
                    // Create pair args
                    std::vector<script_value> args;

                    if (stmt->is_reference) {
                        // For references, create a pair with a map-entry reference to the value
                        args.push_back(it->first);  // Don't clone - just pass the key
                        args.push_back(script_value::make_map_entry_reference(map_storage, it->first, engine_, nullptr));
                    } else {
                        // Copy binding: string keys share their node (the copy-on-write
                        // gate detaches any write - invariants.md 11), other key kinds
                        // snapshot; values deep-copy, shared_ptr values share
                        args.push_back(it->first.is_string() ? it->first : it->first.clone());
                        args.push_back(clone_for_assignment(it->second));
                    }

                    auto result = pair_func(args);
                    if (!result) {
                        pop_scope();
                        return result.error_value();
                    }
                    *loop_var_ptr = std::move(result.value());
                }

                // Execute loop body
                auto body_result = resume_body ? dispatch_stmt_resuming(stmt->body.get()) : dispatch_stmt(stmt->body.get());
                if (!body_result) {
                    pop_scope();
                    return body_result;
                }

                // A script `throw` unwinds: stop iterating so it can propagate.
                if (is_unwinding_) {
                    break;
                }

                // Check for control flow changes (break/continue/return) - mirror while/for loops
                if (hasBreakRequest_) {
                    hasBreakRequest_ = false;  // Clear the flag
                    break;
                }

                if (hasContinueRequest_) {
                    hasContinueRequest_ = false;  // Clear the flag
                    continue;  // Skip to next iteration
                }

                if (hasReturnValue_) {
                    break;
                }

                // On yield: don't pop scope, let environment stay for coroutine to save
                if (hasYieldRequest_) {
                    if (active_coroutine_) {
                        auto& coro_state = coroutine_state(*active_coroutine_);
                        if (coro_state.has_continuations()) coro_state.push_continuation(stmt, entry_index, nullptr, container);
                        else coro_state.push_replay(stmt, rp_body_done, entry_index + 1, {}, nullptr, container);
                    }
                    return {};
                }
            }
        }

    } else if (container.is_object()) {
        // Check for coroutine/generator iteration
        auto objHolder = container.get_object_holder();
        if (objHolder && objHolder->type_id == coroutine_handle_type_id_) {
            auto handle = std::static_pointer_cast<coroutine_handle>(objHolder->data);

            script_value* loop_var_ptr = nullptr;
            if (use_slot) {
                if (!replaying || !call_stack_.back().get_local(stmt->variable_slot_index)) {
                    call_stack_.back().set_local(stmt->variable_slot_index, make_value());
                }
                loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index);
            } else {
                if (!replaying || !environment_->get_value_ptr(stmt->variable_name_id)) {
                    environment_->define(stmt->variable_name_id, make_value());
                }
                loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id);
            }

            while (replaying || !handle->done()) {
                const bool resume_body = replaying;
                // The body can move the frame's locals (a declaration at a new slot, a callee's frame): the loop
                // variable's pointer is re-resolved before it is written again (a use-after-free write otherwise)
                if (use_slot) { loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index); }
                else { loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id); }
                if (!loop_var_ptr) [[unlikely]] {
                    if (use_slot) { call_stack_.back().set_local(stmt->variable_slot_index, make_value()); loop_var_ptr = call_stack_.back().get_local(stmt->variable_slot_index); }
                    else { environment_->define(stmt->variable_name_id, make_value()); loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id); }
                }
                if (replaying) {
                    // First replayed iteration: the current element is already restored;
                    // the body dispatch below fast-forwards to the yield point
                    replaying = false;
                } else {
                    auto resume_result = handle->resume(engine_);
                    if (!resume_result) {
                        pop_scope();
                        return resume_result.error_value();
                    }
                    if (handle->done()) break;

                    script_value element = std::move(resume_result.value());
                    if (stmt->element_type) {   // KEEP BYTE-PARALLEL with vm exec_iter_next
                        auto bound = detail::range_for_bind(std::move(element), stmt->element_type, false, stmt->variable_name, engine_);
                        if (!bound) { pop_scope(); return bound.error_value(); }
                        element = std::move(bound.value());
                    }
                    *loop_var_ptr = std::move(element);
                }

                auto body_result = resume_body ? dispatch_stmt_resuming(stmt->body.get()) : dispatch_stmt(stmt->body.get());
                if (!body_result) {
                    pop_scope();
                    return body_result;
                }

                if (is_unwinding_) break;  // script throw unwinding: stop iterating
                if (hasBreakRequest_) { hasBreakRequest_ = false; break; }
                if (hasContinueRequest_) { hasContinueRequest_ = false; continue; }
                if (hasReturnValue_) break;
                if (hasYieldRequest_) {
                    if (active_coroutine_) {
                        auto& coro_state = coroutine_state(*active_coroutine_);
                        if (coro_state.has_continuations()) coro_state.push_continuation(stmt, 0, nullptr, container);
                        else coro_state.push_replay(stmt, rp_body_done, 0, {}, nullptr, container);
                    }
                    return {};
                }
            }
        } else {
            pop_scope();
            return detail::not_iterable(container, engine_);
        }
    } else {
        pop_scope();
        return detail::not_iterable(container, engine_);
    }

    // Pop the loop scope (only on normal exit, not yield)
    pop_scope();
    return {};
}

template<bool Replaying>
checked_result<void> interpreter::visit_return_stmt_t(return_stmt* stmt) {
    std::optional<script_value> out;
    if (stmt->value) {
        if (stmt->binds_reference) {
            // Ref-return producer: bind the operand as a reference instead of
            // evaluating a copy. KEEP semantics parallel with the vm's
            // exec_ref_return_bind/exec_ref_return_lvalue.
            if (stmt->value->get_type() == node_type::identifier_expr) {
                auto* ident = static_cast<identifier_expr*>(stmt->value.get());
                if (ident->symbol_id == UINT64_MAX) {
                    ident->symbol_id = string_symbolizer_->intern(ident->name);
                }
                script_value* storage = nullptr;
                if (ident->global_qualified) {
                    // ::name - GLOBAL env only (slots, captures, member scope all skipped)
                    storage = get_global_environment()->get_value_ptr(ident->symbol_id);
                } else {
                    if (ident->slot_index != SIZE_MAX && !call_stack_.empty()) {
                        storage = call_stack_.back().get_local(ident->slot_index);
                    }
                    if (!storage) {
                        storage = environment_->get_value_ptr(ident->symbol_id);
                    }
                }
                if (!storage) {
                    // A hybrid's bare C++ property (KEEP BYTE-PARALLEL with the vm's exec_ref_return_bind)
                    if (auto minted = bind_bare_cpp_property_ref(ident->symbol_id, ident->global_qualified)) {
                        if (!*minted) {
                            return minted->error_value();
                        }
                        out.emplace(std::move(minted->value()));
                    } else {
                        if (auto method = bare_method_reference_error(ident)) { return *method; }
                        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                            environment_->undefined_text(ident->symbol_id, "Cannot take reference of undefined variable"), ident->symbol_id);
                    }
                } else {
                    out.emplace(share_boxed_env_storage(*storage, engine_));
                }
            } else if (detail::is_ref_bindable_lvalue(stmt->value.get())) {
                auto resolved = detail::resolve_ref_lvalue(stmt->value.get(),
                    detail::caller_frame_view{call_stack_.empty() ? nullptr : &call_stack_.back()},
                    environment_.get(), engine_, string_symbolizer_);
                if (!resolved) {
                    return resolved.error_value();
                }
                out.emplace(std::move(resolved.value()));
            }
            // Fall through: calls that already yield a reference pass; anything else
            // rejects in the ref-return conversion below
        }
        if (!out) {
            JAISCRIPT_TRY_ROOT(stmt, stmt->value.get());
            out.emplace(pop_value());
        }
    } else {
        out.emplace(make_value());
    }

    if (is_unwinding_) [[unlikely]] {
        // The operand threw: the return never completes (vm parity - op_return does not
        // run); the flag unwinds the body and a catch resumes past the statement
        return {};
    }

    // By-value return materialization (detail/return_conv.hpp), before the typed
    // conversion so a converted value is never copied twice: the callee's own slot moves
    // out, every other lvalue read (and a reference result) copies, a temporary moves.
    // KEEP BYTE-PARALLEL with the vm's return doors (exec_return_ident / exec_return_convert /
    // return_from_script_frame).
    if (!detail::return_value_plain(*out) && !stmt->binds_reference && !call_stack_.empty()) {
        uint8_t operand_class = stmt->return_operand_class;
        if (operand_class == detail::return_operand_unclassified) {
            operand_class = stmt->return_operand_class = detail::classify_return_operand(stmt->value.get());
        }
        bool moved = false;
        if (operand_class == detail::return_operand_ident) {
            auto* ident = static_cast<identifier_expr*>(stmt->value.get());
            if (!ident->global_qualified && ident->slot_index != SIZE_MAX) {
                if (script_value* payload = detail::return_own_slot_payload(call_stack_.back().get_local(ident->slot_index), *out)) {
                    *payload = make_value();
                    moved = true;
                }
            }
        }
        if (!moved) {
            detail::materialize_return_value(*out, operand_class != detail::return_operand_temporary);
        }
    }

    // The typed-return conversion is part of the statement (C++ semantics): it runs here,
    // inside the callee's try regions, so a catch around the `return` sees its failure.
    // KEEP BYTE-PARALLEL with the vm's convert_return_at_site.
    if (!call_stack_.empty()) {
        if (const script_defined_function* fn = call_stack_.back().function) {
            const auto conv = detail::return_conv_of(*fn);
            if (conv == detail::return_conv::ref) {
                auto passed = detail::ref_return_pass_through(std::move(*out), fn->return_type, string_symbolizer_);
                if (!passed) {
                    return passed.error_value();
                }
                out = std::move(passed.value());
            } else if (conv != detail::return_conv::none) {
                if (out->is_reference()) {
                    *out = out->deref();
                }
                // A storage-matching primitive or a proven same-type payload is the identity (I-R1)
                if (!detail::return_conv_identity(conv, *out) &&
                    !(stmt->value && stmt->static_return_proof == proof_same_type &&
                      detail::same_type_payload(*out, fn->return_type.get()))) {
                    auto converted = try_convert_for_parameter(*out, fn->return_type,
                        detail::slot_site{detail::slot_kind::return_value, {}, fn->name});
                    if (!converted) {
                        return converted.error_value();
                    }
                    if (is_unwinding_) [[unlikely]] {
                        return {};   // a converting ctor / to_X() threw: the throw wins
                    }
                    out = std::move(converted.value());
                }
            }
        }
    }

    returnValue_ = std::move(*out);
    hasReturnValue_ = true;
    return {};
}

checked_result<void> interpreter::visit_break_stmt(break_stmt* stmt) {
    hasBreakRequest_ = true;
    return {};
}

checked_result<void> interpreter::visit_continue_stmt(continue_stmt* stmt) {
    hasContinueRequest_ = true;
    return {};
}

checked_result<void> interpreter::visit_try_stmt(try_stmt* stmt) {
    // Save exception state
    auto saved_exception = current_exception_;
    auto saved_unwinding = is_unwinding_;
    auto saved_exception_value = active_exception_value_;
    auto saved_catch_var_id = current_catch_var_id_;

    // Coroutine resume inside the CATCH block: re-enter the catch with its exception
    // restored - the try block already ran and must not run again
    bool resuming_catch = false;
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        if (auto* cont = coro_state.peek_continuation(stmt)) {
            if (cont->phase == rp_catch) {
                resuming_catch = true;
                active_exception_value_ = cont->saved_container ? *cont->saved_container : make_value();
                current_exception_ = script_exception(active_exception_value_->to_string(), stmt->location);
            }
            coro_state.pop_continuation();
        }
    }

    // Reset state for try block
    // Don't reset exception state if we're in a catch block (allows re-throw); a try block inside a handler is still
    // inside that handler (C++), so the in-catch marker carries into it and a nested try keeps the handled exception
    if (!resuming_catch) {
        if (current_catch_var_id_ == 0) {
            current_exception_.reset();
            active_exception_value_ = make_value();
        }
        is_unwinding_ = false; trace_captured_ = false;
    }

    // Execute try block (a catch resume skips it: it completed before the suspension). A native exception an instruction of the
    // block raises is this try's exception, as in C++ (docs/EXCEPTION_DESIGN.md; KEEP BYTE-PARALLEL with vm_backend::run)
    checked_result<void> try_result;
    if (!resuming_catch) {
        const size_t try_stack_mark = valueStack_.size();
        try {
            try_result = dispatch_stmt(stmt->try_block.get());
        } catch (const std::exception& e) {
            while (valueStack_.size() > try_stack_mark) { (void)valueStack_.pop(); }
            if (auto* se = dynamic_cast<const script_exception*>(&e)) { current_exception_ = *se; }
            else { current_exception_ = script_exception(e.what()); }
            active_exception_value_ = make_value(std::string(e.what()));
            is_unwinding_ = true;
        }
    }
    if (!resuming_catch && hasYieldRequest_) [[unlikely]] {
        // The try block suspended: its own frames carry the position, this statement adds none
        current_catch_var_id_ = saved_catch_var_id;
        return {};
    }

    // Terminal errors (budget overrun, escalated memory cap) can NEVER be caught from
    // script: restore the catch-var and let the failure keep unwinding to the host
    // execute() boundary (KEEP BYTE-PARALLEL with vm_backend::unwind_to_handler).
    if (limits_->terminal_error && (!try_result || (is_unwinding_ && current_exception_))) [[unlikely]] {
        current_catch_var_id_ = saved_catch_var_id;
        if (!try_result) {
            return try_result;
        }
        return {};
    }

    // Check if an error occurred (either checked_result error or exception unwinding)
    bool caught_error = resuming_catch;

    if (!try_result) {
        // Checked_result error - treat as catchable exception
        std::string error_msg = format_error(try_result, *string_symbolizer_);
        active_exception_value_ = make_value(error_msg);
        current_exception_ = script_exception(error_msg);
        caught_error = true;
    } else if (is_unwinding_ && current_exception_) {
        // Old-style exception unwinding (for script throw statements)
        caught_error = true;
    }

    if (caught_error) {
        // the stack at the error, for the caught value (a callee's frame exit captured it already)
        if (!resuming_catch && !trace_captured_) { capture_stack_trace(); }
        // Reset unwinding flag
        is_unwinding_ = false; trace_captured_ = false;

        // In-catch marker of the rethrow protocol (symbolize once here)
        if (auto eng = engine_) {
            current_catch_var_id_ = eng->symbolize(stmt->catch_var);
        }

        // Bind the catch variable as a local of the catch body (KEEP BYTE-PARALLEL with
        // vm_backend::unwind_to_handler): frame slot, or a scope env popped on every exit
        // of the block. A catch resume skips the bind - the restored frame holds it.
        bool catch_env_pushed = false;
        if (stmt->catch_var_id != UINT64_MAX && !resuming_catch) {
            const bool by_throw = current_exception_ && current_exception_->thrown;
            script_value caught = engine_->make_caught_error(active_exception_value_ ? *active_exception_value_ : make_value(), by_throw,
                                                             by_throw ? &current_exception_->location() : nullptr, captured_trace_);
            if (stmt->catch_slot_index != SIZE_MAX && !call_stack_.empty()) {
                call_stack_.back().set_local(stmt->catch_slot_index, std::move(caught));
            } else {
                push_scope();
                environment_->define(stmt->catch_var_id, std::move(caught));
                catch_env_pushed = true;
            }
        }

        // Execute catch block
        auto catch_result = resuming_catch ? dispatch_stmt_resuming(stmt->catch_block.get())
                                           : dispatch_stmt(stmt->catch_block.get());
        if (catch_env_pushed) {
            pop_scope();
        }
        // Propagate errors from catch block
        if (!catch_result) {
            current_catch_var_id_ = saved_catch_var_id;
            return catch_result;
        }

        // The catch suspended: park the caught value so the resume re-enters the catch
        // instead of re-running the try block
        if (hasYieldRequest_) [[unlikely]] {
            if (active_coroutine_) {
                coroutine_state(*active_coroutine_).push_replay(stmt, rp_catch, 0, {}, nullptr,
                    active_exception_value_ ? *active_exception_value_ : make_value());
            }
            current_catch_var_id_ = saved_catch_var_id;
            return {};
        }

        // Clear the in-catch marker
        current_catch_var_id_ = 0;

        // A completed handler hands the current exception back to the enclosing handler when this try sits inside one (C++:
        // the handled exception reverts, so a later bare `throw;` rethrows it); otherwise none is current
        // (KEEP BYTE-PARALLEL with vm_backend::exec_catch_end)
        if (!is_unwinding_) {
            if (saved_catch_var_id != 0) {
                current_exception_ = saved_exception;
                active_exception_value_ = saved_exception_value;
            } else {
                current_exception_.reset();
                active_exception_value_ = make_value();
            }
        }
    }

    // If still unwinding after catch, we need to be careful about state restoration
    // Don't restore if a new exception was thrown in the catch block
    if (is_unwinding_ && saved_unwinding) {
        // We were already unwinding before this try/catch, restore that state
        current_exception_ = saved_exception;
        active_exception_value_ = saved_exception_value;
    }
    // If is_unwinding_ is true but saved_unwinding was false,
    // it means a new exception was thrown in the catch block - keep it

    // Always restore the catch variable state
    current_catch_var_id_ = saved_catch_var_id;
    return {};
}

checked_result<void> interpreter::visit_switch_stmt(switch_stmt* stmt) {
    // Coroutine resume: re-enter the matched body (or the suspended condition / case value)
    // instead of re-evaluating the condition and the case values (each runs once)
    uint32_t resume_phase = rp_none;
    size_t resume_case = 0;
    script_value switch_value;
    std::shared_ptr<environment> resumed_case_env;
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        if (auto* cont = coro_state.peek_continuation(stmt)) {
            resume_phase = cont->phase;
            resume_case = cont->index;
            if (!cont->partial.empty()) switch_value = cont->partial[0];
            resumed_case_env = cont->saved_env;
            coro_state.pop_continuation();
        }
    }

    // Evaluate the switch condition
    if (resume_phase == rp_none || resume_phase == rp_cond) {
        auto cond_result = resume_phase == rp_cond ? replay_expression(stmt->condition.get())
                                                   : dispatch_expr(stmt->condition.get());
        if (!cond_result) [[unlikely]] {
            if (is_yield_signal(cond_result)) { park_operands(stmt, rp_cond, 0, {}); return {}; }
            return cond_result.error_value();
        }
        switch_value = pop_value();
        resume_phase = rp_none;
    }

    // Save and set switch state
    bool old_in_switch = in_switch_;
    bool old_should_fallthrough = should_fallthrough_;
    in_switch_ = true;
    should_fallthrough_ = false;

    try {
        bool matched = false;
        bool executed_case = false;

        // Check each case. A resume into a case body enters it directly: neither the
        // condition nor any case value is evaluated a second time.
        const bool resuming_body = resume_phase == rp_case_body;
        const size_t case_start = (resume_phase == rp_default_body) ? stmt->cases.size()
                                : (resume_phase == rp_none) ? 0 : resume_case;
        for (size_t case_idx = case_start; case_idx < stmt->cases.size(); ++case_idx) {
            const auto& case_stmt = stmt->cases[case_idx];
            bool case_matches = false;
            if (resuming_body && case_idx == resume_case) {
                case_matches = true;   // the suspended case: straight back into its body
            } else {
                // Evaluate case value
                auto result = (resume_phase == rp_case_value && case_idx == resume_case)
                    ? replay_expression(case_stmt->value.get())
                    : dispatch_expr(case_stmt->value.get());
                if (!result) {
                    in_switch_ = old_in_switch;
                    should_fallthrough_ = old_should_fallthrough;
                    if (is_yield_signal(result)) {
                        park_operands(stmt, rp_case_value, case_idx, {switch_value});
                        return {};
                    }
                    return result;
                }
                script_value case_value = pop_value();

                // A case matches when the language's == holds (KEEP BYTE-PARALLEL with vm_backend::exec_case_eq)
                auto equal = language_equal(switch_value, case_value);
                if (!equal) {
                    in_switch_ = old_in_switch;
                    should_fallthrough_ = old_should_fallthrough;
                    return equal.error_value();
                }
                case_matches = equal.value();
            }

            // Execute case if it matches OR if we're falling through from a previous case
            if (case_matches || (executed_case && should_fallthrough_)) {
                matched = true;
                executed_case = true;

                // Reset fallthrough flag for this case (will be set again if case contains fallthrough statement)
                should_fallthrough_ = false;

                // Create a new scope for the case body (like an if statement); a resume
                // re-enters the scope its suspension parked
                auto previous = environment_;
                auto case_env = (resuming_body && case_idx == resume_case && resumed_case_env)
                    ? resumed_case_env : get_block_environment(environment_);
                environment_ = case_env;

                try {
                    // Execute case body
                    auto case_result = dispatch_stmt(case_stmt.get());
                    if (!case_result) {
                        release_environment(case_env);
                        environment_ = previous;
                        in_switch_ = old_in_switch;
                        should_fallthrough_ = old_should_fallthrough;
                        return case_result;
                    }

                    // Release the case scope (runs case-local destructors) and restore.
                    // On yield the scope must survive for coroutine resume — skip the release.
                    if (hasYieldRequest_) {
                        if (active_coroutine_) {
                            coroutine_state(*active_coroutine_).push_replay(stmt, rp_case_body, case_idx, {switch_value}, case_env);
                        }
                        environment_ = previous;
                        in_switch_ = old_in_switch;
                        should_fallthrough_ = old_should_fallthrough;
                        return {};
                    }
                    release_environment(case_env);
                    environment_ = previous;

                    // An explicit `break;` inside a case terminates the switch
                    // (break-by-default already does this), so consume the request
                    // here — it must NOT leak to an enclosing loop. A `continue;`
                    // (and return/unwind) is left set so it propagates outward.
                    if (hasBreakRequest_) {
                        hasBreakRequest_ = false;
                        break;  // Terminate the switch
                    }

                    // Propagate continue/return/unwind out of the switch immediately.
                    if (hasContinueRequest_ || hasReturnValue_ || is_unwinding_) {
                        break;
                    }

                    // Check if we should continue to next case (implicit break by default)
                    if (!should_fallthrough_) {
                        break;  // Stop executing further cases
                    }
                    // If should_fallthrough_ is true, continue to next iteration
                } catch (...) {
                    // Release and restore environment before re-throwing
                    release_environment(case_env);
                    environment_ = previous;
                    throw;
                }
            }
        }

        // Execute default if no case matched OR if we're falling through from the last case
        if ((!matched || (executed_case && should_fallthrough_)) && stmt->default_case) {
            // Create a new scope for the default body
            auto previous = environment_;
            auto default_env = (resume_phase == rp_default_body && resumed_case_env)
                ? resumed_case_env : get_block_environment(environment_);
            environment_ = default_env;

            try {
                auto default_result = dispatch_stmt(stmt->default_case.get());
                if (!default_result) {
                    release_environment(default_env);
                    environment_ = previous;
                    in_switch_ = old_in_switch;
                    should_fallthrough_ = old_should_fallthrough;
                    return default_result;
                }

                // Release the default scope (runs its destructors) and restore.
                // On yield the scope must survive for coroutine resume — skip the release.
                if (hasYieldRequest_) {
                    if (active_coroutine_) {
                        coroutine_state(*active_coroutine_).push_replay(stmt, rp_default_body, 0, {switch_value}, default_env);
                    }
                    environment_ = previous;
                    in_switch_ = old_in_switch;
                    should_fallthrough_ = old_should_fallthrough;
                    return {};
                }
                release_environment(default_env);
                environment_ = previous;

                // Consume an explicit `break;` in default so it does not leak to
                // an enclosing loop (continue/return/unwind still propagate).
                if (hasBreakRequest_) {
                    hasBreakRequest_ = false;
                }
            } catch (...) {
                // Release and restore environment before re-throwing
                release_environment(default_env);
                environment_ = previous;
                throw;
            }
        }
    } catch (const break_exception&) {
        // Break out of switch - this is expected behavior
    }

    // Restore switch state
    in_switch_ = old_in_switch;
    should_fallthrough_ = old_should_fallthrough;
    return {};
}

// Case/default bodies: statement lists in the scope visit_switch_stmt created; a yield
// records the statement index exactly like a block (re-enter when an inner frame exists,
// skip a completed statement otherwise)
checked_result<void> interpreter::run_case_body(ast_node* self, const std::vector<statement_ptr>& body) {
    size_t start_index = 0;
    bool resume_entry = false;
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        if (auto* cont = coro_state.peek_continuation(self)) {
            start_index = cont->index;
            coro_state.pop_continuation();
            resume_entry = true;
        }
    }
    for (size_t i = start_index; i < body.size(); ++i) {
        auto result = resume_entry ? dispatch_stmt_resuming(body[i].get()) : dispatch_stmt(body[i].get());
        resume_entry = false;
        if (!result) return result;

        // Stop the case body on return, exception unwind, OR a break/continue request:
        // break terminates the switch, continue targets an enclosing loop; either way
        // the rest of the case body must not run.
        if (hasReturnValue_ || is_unwinding_ || hasBreakRequest_ || hasContinueRequest_) {
            break;
        }
        if (hasYieldRequest_) {
            if (active_coroutine_) {
                auto& coro_state = coroutine_state(*active_coroutine_);
                coro_state.push_continuation(self, coro_state.has_continuations() ? i : i + 1);
            }
            return {};
        }
    }
    return {};
}

checked_result<void> interpreter::visit_case_stmt(case_stmt* stmt) {
    return run_case_body(stmt, stmt->body);
}

checked_result<void> interpreter::visit_default_stmt(default_stmt* stmt) {
    return run_case_body(stmt, stmt->body);
}

checked_result<void> interpreter::visit_fallthrough_stmt(fallthrough_stmt* stmt) {
    // Set flag to continue to next case
    should_fallthrough_ = true;
    return {};
}

checked_result<void> interpreter::visit_function_decl(function_decl* decl) {
    // Note: name_id and parameter symbol_ids are pre-interned by the parser
    // (see parse_function_body() and parse_parameter_list())

    if (decl->is_coroutine) {
        // Coroutine function - calling it returns a coroutine_handle instead of executing.
        // The shared function_decl copy the handle pins is identical on every execution
        // of this declaration - mint it once per node.
        auto& func_decl_ptr = decl->coroutine_mint_cache;
        const unit_stamp decl_unit = current_unit();
        if (!func_decl_ptr || func_decl_ptr->unit.id != decl_unit.id) {
            func_decl_ptr = std::make_shared<function_decl>(decl->location, decl->name, decl->name_id);
            func_decl_ptr->parameters = decl->parameters;
            func_decl_ptr->return_type = decl->return_type;
            func_decl_ptr->body = decl->body;
            func_decl_ptr->is_coroutine = true;
            func_decl_ptr->local_count = decl->local_count;
        }

        auto closure_env = environment_;

        // Nested (function-local) coroutine: the resumed body runs on its own call
        // frame, so enclosing-frame slot locals are invisible to it. Snapshot them by
        // value into a capture env, lambda-style: the first declaration builds the plan
        // and patches those body identifiers to env lookups; later declarations replay it.
        if (!call_stack_.empty()) {
            if (!decl->outer_slot_plan_built) {
                std::unordered_set<uint64_t> declared;
                for (const auto& p : decl->parameters) {
                    if (p.symbol_id == UINT64_MAX) {
                        p.symbol_id = string_symbolizer_->intern(p.name);
                    }
                    declared.insert(p.symbol_id);
                }
                {
                    detail::body_walker walker;
                    walker.symbolizer = string_symbolizer_;
                    walker.on_declared = [&declared](uint64_t id) { declared.insert(id); };
                    walker.stmt(decl->body);
                }
                std::vector<identifier_expr*> to_patch;
                std::unordered_set<uint64_t> seen;
                detail::body_walker walker;
                walker.symbolizer = string_symbolizer_;
                walker.on_identifier = [&](identifier_expr* ident) {
                    if (ident->slot_index == SIZE_MAX) return;
                    if (declared.count(ident->symbol_id)) return;
                    if (seen.insert(ident->symbol_id).second) {
                        decl->outer_slot_plan.emplace_back(ident->symbol_id, ident->slot_index);
                    }
                    to_patch.push_back(ident);
                };
                walker.stmt(decl->body);
                for (auto* ident : to_patch) {
                    ident->slot_index = SIZE_MAX;
                }
                decl->outer_slot_plan_built = true;
            }
            if (!decl->outer_slot_plan.empty()) {
                const size_t outer_slot_count = call_stack_.back().local_count();
                std::shared_ptr<environment> captureEnv;
                for (const auto& [sym, slot] : decl->outer_slot_plan) {
                    if (slot < outer_slot_count) {
                        if (script_value* slot_val = call_stack_.back().get_local(slot)) {
                            if (!captureEnv) {
                                // Parent = global, NOT the enclosing pooled env: an escaping
                                // handle would otherwise recycle its own ancestor into an env
                                // cycle (same lifecycle hazard visit_lambda_expr documents)
                                captureEnv = std::make_shared<environment>(get_global_environment(), string_symbolizer_);
                            }
                            captureEnv->define(sym, detail::clone_for_capture(slot_val->deref(), string_symbolizer_));
                        }
                    }
                }
                if (captureEnv) {
                    closure_env = captureEnv;
                }
            }
        }
        engine* eng = engine_;
        func_decl_ptr->unit = decl_unit;
        uint64_t coroutine_type_id = coroutine_handle_type_id_;
        script_value functionValue = script_value::make_function(
            [eng, coroutine_type_id, func_decl_ptr, closure_env](const std::vector<script_value>& args) -> checked_result<script_value> {
                // Create a new coroutine handle; resume() resolves the live backend itself
                auto handle = std::make_shared<coroutine_handle>(eng);
                handle->set_function(func_decl_ptr, args, closure_env);

                // Wrap as a script object
                return make_coroutine_object(eng, coroutine_type_id, handle);
            }, engine_);

        environment_->define(decl->name_id, functionValue);
        return {};
    }

    // Don't capture any environment in the closure - just use nullptr (which also makes
    // the mint node-cacheable: every execution of this declaration under one unit is identical;
    // another unit executing the node, an includer of the same file, mints its own)
    auto& scriptFunc = decl->mint_cache;
    const unit_stamp decl_unit = current_unit();
    if (!scriptFunc || scriptFunc->unit.id != decl_unit.id) {
        scriptFunc = std::make_shared<script_defined_function>(
            decl->name,
            shared_parameters_for(decl),
            decl->return_type,
            decl->body,
            nullptr,  // No closure needed - environment stack handles everything
            decl->local_count  // Slot count for stack allocation
        );
    }
    scriptFunc->unit = decl_unit;
    scriptFunc->block_defined = environment_->block_scope();

    // Create wrapper function resolving the live backend at call time. Named thunk so
    // callers can recover the payload via target<script_callable_thunk>() - vm parity.
    scriptFunc->alias_candidate = true;
    script_callable payload;
    payload.kind = script_callable::kind_type::function;
    payload.fn = scriptFunc;
    script_value functionValue = script_value::make_function(
        script_function(script_callable_thunk{engine_, std::move(payload)}), engine_);
    engine_->bump_definition_epoch();

    // Define the function in current environment
    environment_->define(decl->name_id, functionValue);
    return {};
}

checked_result<void> interpreter::visit_class_decl(class_decl* decl) {
    // Set up class parsing context
    class_context prev_context;
    bool had_context = false;
    if (current_class_context_) {
        prev_context = *current_class_context_;
        had_context = true;
    }
    
    // Create new context for this class
    current_class_context_ = class_context{std::string(decl->name), {}, false};
    
    // Restore previous context on exit
    auto context_guard = std::shared_ptr<void>(nullptr, [this, prev_context, had_context](void*) {
        if (had_context) {
            current_class_context_ = prev_context;
        } else {
            current_class_context_.reset();
        }
    });
    
    // Check if class already exists (for hot reloading)
    std::shared_ptr<script_class_definition> class_def = nullptr;
    bool is_redefinition = false;

    // Use cached class variable ID helper (avoids string allocation on repeated access)
    auto [class_var_id, class_var_name_view] = string_symbolizer_->get_class_var_id_with_view(decl->name_id);

    // Look for existing class in GLOBAL environment (hot reload support)
    // get_global_environment() now uses engine's global directly (not parent chain walking)
    auto global_env = get_global_environment();
    if (!global_env) {
        return checked_result<void>(make_error_code(runtime_error_code::engine_destroyed));
    }
    auto existing_result = global_env->get(class_var_id);
    if (existing_result) {
        script_value existing = std::move(existing_result.value());
        if (!existing.is_null() && existing.is_object()) {
            // Class already exists - extract from object holder
            auto objHolder = existing.get_object_holder();
            if (objHolder && objHolder->type_id == class_definition_type_id_) {
                class_def = std::static_pointer_cast<script_class_definition>(objHolder->data);
                is_redefinition = true;
            }
        }
    }
    // If result failed, class doesn't exist yet - that's fine
    
    if (!class_def) {
        // Create a new script class definition
        // Use cached name_id if available, otherwise intern the name
        uint64_t type_id = (decl->name_id != UINT64_MAX) ? decl->name_id : string_symbolizer_->intern(decl->name);
        class_def = std::make_shared<script_class_definition>(decl->name, type_id, engine_);
    } else if (is_redefinition) {
        // Clear old ASTs for hot reload
        class_def->clear_asts();
        // Rebuild method overload sets from scratch so they don't accumulate across reloads.
        class_def->clear_instance_method_overloads();
        class_def->clear_static_method_overloads();
    }
    
    class_def->nocopy(decl->nocopy);
    class_def->set_unit(current_unit());

    // Collect new field defaults and methods (using uint64_t IDs for performance)
    std::unordered_map<uint64_t, script_value> new_field_defaults;
    std::unordered_map<uint64_t, type_info_ptr> new_field_types; // declared types (typed fields enforce like locals)
    std::unordered_map<uint64_t, type_info_ptr> new_static_types; // declared static types ('auto' = null entry)
    std::unordered_map<uint64_t, script_value> new_methods;
    std::unordered_map<uint64_t, script_value> new_static_methods;
    std::set<uint64_t> new_static_field_ids; // statics declared in this (re)definition, for reload purge
    
    // Reserve capacity based on member count for efficiency
    if (!decl->members.empty()) {
        new_field_defaults.reserve(decl->members.size());
        new_methods.reserve(decl->members.size());
        new_static_methods.reserve(decl->members.size());
    }
    
    // Debug output
    // std::cerr << "DEBUG: Processing class declaration: " << decl->name << std::endl;
    
    // Handle base classes (now supports multiple inheritance)
    if (!decl->base_classes.empty()) {
        std::vector<std::shared_ptr<class_definition>> parent_defs;
        parent_defs.reserve(decl->base_classes.size());

        // Look up each base class definition
        for (std::string_view base_name : decl->base_classes) {
            // First try to find a script class (intern base_name and use cached __class_ lookup)
            uint64_t base_name_id = string_symbolizer_->intern(base_name);
            auto [base_class_var_id, base_class_var_name] = string_symbolizer_->get_class_var_id_with_view(base_name_id);
            script_value base_class_var = make_value();
            auto base_result = environment_->get(base_class_var_id);
            if (base_result) {
                base_class_var = std::move(base_result.value());
            }

            std::shared_ptr<class_definition> base_class_def;

            if (!base_class_var.is_null() && base_class_var.is_object()) {
                // Found a class definition in __class_<name> - extract from object holder
                // This could be either a C++ class or a script class
                auto objHolder = base_class_var.get_object_holder();
                if (objHolder && objHolder->type_id == class_definition_type_id_) {
                    base_class_def = std::static_pointer_cast<class_definition>(objHolder->data);
                } else {
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Base class is not a valid class definition
                }
            } else {
                // Try to find a C++ class using the class lookup callback
                if (class_lookup_callback_) {
                    // Callback API requires std::string - convert at boundary
                    auto cpp_class_def = class_lookup_callback_(std::string(base_name));
                    if (cpp_class_def) {
                        // Found a C++ class!
                        base_class_def = cpp_class_def;
                    } else if (environment_->contains(base_name_id)) {
                        // Constructor exists but no class definition found
                        // This shouldn't happen with proper engine integration
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Constructor found for '{0}' but no class definition available", base_name_id);
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::class_not_found),
                            "Base class '{0}' not found", base_name_id);
                    }
                } else {
                    // No class lookup callback set - check if constructor exists
                    if (environment_->contains(base_name_id)) {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Script class inheriting from C++ class '{0}' requires engine integration", base_name_id);
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::class_not_found),
                            "Base class '{0}' not found", base_name_id);
                    }
                }
            }

            if (base_class_def) {
                parent_defs.push_back(base_class_def);

                // Check if this is a C++ class (not a script_class_definition)
                // C++ classes don't have script_class_definition type, so dynamic_pointer_cast fails
                auto script_class = std::dynamic_pointer_cast<script_class_definition>(base_class_def);
                if (!script_class && parent_defs.size() == 1) {
                    // This is a C++ class and it's the first parent - set as cpp_base_class
                    class_def->set_cpp_base_class(base_class_def);
                }
            }
        }

        // Set all parent classes at once
        if (!parent_defs.empty()) {
            // set_parents() now checks for diamond inheritance internally
            if (!class_def->set_parents(parent_defs)) {
                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Diamond inheritance not supported
            }
        }
    }

    // Record private:/protected: labels for runtime access enforcement
    apply_member_access_labels(*class_def, *decl, *string_symbolizer_);

    // Collect field IDs defined in this class (before evaluating them)
    // This is needed for multiple inheritance conflict detection
    std::unordered_set<uint64_t> derived_field_names;
    for (const auto& member : decl->members) {
        if (member.declaration->get_type() == node_type::variable_decl) {
            auto* var_decl = static_cast<variable_decl*>(member.declaration.get());
            if (!var_decl->is_static) {
                derived_field_names.insert(var_decl->name_id);
            }
        }
    }

    // Check for field name conflicts in multiple inheritance (only for fields NOT redefined in derived class)
    // C++ doesn't allow ambiguous field access - we follow the same semantics
    // But if the derived class defines its own version, it shadows the parent fields (allowed)
    if (!decl->base_classes.empty() && decl->base_classes.size() > 1) {
        const auto& parent_classes = class_def->get_parent_classes();
        std::unordered_map<uint64_t, std::vector<std::string>> field_sources;

        // Collect all fields from each parent (including inherited ones)
        for (const auto& parent : parent_classes) {
            const auto& parent_fields = parent->get_all_field_defaults();
            for (const auto& [field_id, _] : parent_fields) {
                // Skip fields that are redefined in the derived class (shadowing is allowed)
                if (derived_field_names.find(field_id) == derived_field_names.end()) {
                    field_sources[field_id].push_back(parent->get_name());
                }
            }
        }

        // Check for conflicts (field appears in multiple parents and NOT redefined in derived)
        for (const auto& [field_id, sources] : field_sources) {
            if (sources.size() > 1) {
                // Return error with field_id and class name_id for context
                return checked_result<void>(make_error_code(runtime_error_code::multiple_inheritance),
                    "Field inherited from multiple parents", field_id, decl->name_id);
            }
        }
    }

    // Track whether we found an explicit constructor
    bool found_constructor = false;
    
    // Process class members
    // Reflection determinism (twin: vm_backend class-decl builder, same commit): field
    // DECLARATION order retained per class — instance fields then statics, source order
    std::vector<uint64_t> reflect_field_order_instance;
    std::vector<uint64_t> reflect_field_order_static;
    for (const auto& member : decl->members) {
        // Extract the actual declaration from the member
        auto* var_decl = member.declaration->get_type() == node_type::variable_decl
            ? static_cast<variable_decl*>(member.declaration.get()) : nullptr;
        auto* func_decl = member.declaration->get_type() == node_type::function_decl
            ? static_cast<function_decl*>(member.declaration.get()) : nullptr;

        if (var_decl) {
            // Field declaration
            script_value default_val(std::monostate{}, engine_);  // Ensure engine reference
            std::string_view field_name = var_decl->name;
            // Use pre-computed ID from parser (already interned during parsing)
            uint64_t field_id = var_decl->name_id;
            expression_ptr initializer_ast = nullptr;

            if (var_decl->initializer) {
                // Check if the initializer is an assignment expression
                // This happens when the parser sees "x = 0" and creates assignment_expr
                auto* assign_expr = var_decl->initializer->get_type() == node_type::assignment_expr
                    ? static_cast<assignment_expr*>(var_decl->initializer.get()) : nullptr;
                if (assign_expr) {
                    // For field declarations like "x = 0", we need to get the field name from the assignment
                    if (assign_expr->target->get_type() == node_type::identifier_expr) {
                        auto* ident_expr = static_cast<identifier_expr*>(assign_expr->target.get());
                        field_name = ident_expr->name;
                        field_id = ident_expr->symbol_id;  // FIX: Also update the ID to match the field name
                    }
                    // Store the RHS AST for later evaluation during instance construction
                    initializer_ast = assign_expr->value;
                } else {
                    // Normal initializer expression - store the AST
                    initializer_ast = var_decl->initializer;
                }
            }

            // Check if field is static
            if (var_decl->is_static) {
                if (field_id != 0) {
                    new_static_field_ids.insert(field_id);
                }
                // A reload preserves a static's runtime value (statics are state, not code);
                // only a static that is NEW in this definition runs its initializer.
                bool preserve_existing = is_redefinition && field_id != 0 && class_def->has_static_field(field_id);

                // Static fields must be evaluated immediately (they're shared across all instances)
                if (initializer_ast && !preserve_existing) {
                    JAISCRIPT_TRY(dispatch_expr(initializer_ast.get()));
                    default_val = initializer_store_value(pop_value(), initializer_ast.get());

                    // Ensure the default value has an engine reference
                    if (!default_val.has_valid_engine() && engine_) {
                        default_val.set_engine(engine_);
                    }
                }
                // Declared type ('auto' parses as null and infers; 'var' stores the any tag);
                // the initializer converts through the field table like an instance initializer
                if (field_id != 0) {
                    new_static_types[field_id] = var_decl->type;
                }
                if (!preserve_existing && var_decl->type && var_decl->type->base_type != script_value_type::jai_any_type) {
                    if (default_val.is_null()) {
                        default_val.set_type_info(var_decl->type);
                    } else {
                        auto converted = detail::field_store_convert(default_val, var_decl->type, engine_, field_id, class_def.get(), true);
                        if (!converted) {
                            return converted.error_value();
                        }
                        default_val = std::move(converted.value());
                    }
                }

                // Add static field directly to the class
                if (field_id != 0) {
                    reflect_field_order_static.push_back(field_id);
                }
                if (field_id != 0 && !preserve_existing) {
                    class_def->add_static_field(field_id, default_val);
                }
            } else {
                // Instance field - store initializer AST for evaluation at construction time
                if (field_id != 0) {
                    reflect_field_order_instance.push_back(field_id);
                    if (initializer_ast) {
                        // Store the initializer AST in the script class definition (using ID for efficiency)
                        class_def->add_field_initializer_ast(field_id, initializer_ast);
                    }
                    // Also add a null default value to the field_defaults map (using ID for performance)
                    // This ensures the field exists but will be properly initialized later
                    new_field_defaults[field_id] = default_val;
                    // Declared type ('auto' parses as null and infers; 'var' stores the any tag)
                    if (var_decl->type) {
                        new_field_types[field_id] = var_decl->type;
                    }
                }
            }

        } else if (func_decl) {
            // Method declaration
            auto method_name = func_decl->name;
            // Use pre-computed ID from parser (already interned during parsing)
            uint64_t method_id = func_decl->name_id;

            // Check for constructor
            if (method_name == decl->name) {
                // Constructor
                found_constructor = true;

                // Note: Parameter symbol IDs are pre-interned by the parser (parse_parameter_list())

                // Set in_method flag while processing constructor body (for static field access)
                if (current_class_context_) {
                    current_class_context_->in_method = true;
                }

                try {
                    class_def->add_constructor_from_ast(
                        std::static_pointer_cast<function_decl>(member.declaration)
                    );
                } catch (const runtime_error&) {
                    // Reset in_method flag on error
                    if (current_class_context_) {
                        current_class_context_->in_method = false;
                    }
                    throw;
                }
                
                // Reset in_method flag
                if (current_class_context_) {
                    current_class_context_->in_method = false;
                }
                
                // Constructor will be registered after all members are processed
                
            } else if (method_name.size() > 1 && method_name[0] == '~') {   // "~" alone is operator~
                // Destructor
                // Set in_method flag while processing destructor body
                if (current_class_context_) {
                    current_class_context_->in_method = true;
                }
                
                try {
                    class_def->add_destructor_from_ast(
                        std::static_pointer_cast<function_decl>(member.declaration),
                        environment_
                    );
                } catch (const runtime_error&) {
                    // Reset in_method flag on error
                    if (current_class_context_) {
                        current_class_context_->in_method = false;
                    }
                    throw;
                }
                
                // Reset in_method flag
                if (current_class_context_) {
                    current_class_context_->in_method = false;
                }
                
            } else {
                // Regular method or static method
                auto method_ast = std::static_pointer_cast<function_decl>(member.declaration);

                // Capture the current definition environment (namespace or global) for method environments
                // This is the stable environment where the class is defined, not an execution environment
                // Methods need access to this scope for static members, namespace variables, etc.
                auto definition_env = environment_;

                if (is_redefinition) {
                    // For redefinition, just collect the method function
                    // We'll add it to the class via redefine_class later

                    if (method_ast->is_static) {
                        // Static method: rebuild through the same machinery a fresh class uses
                        // (appends to static_method_overloads_ and installs the dispatching thunk).
                        class_def->add_static_script_method(method_name, method_ast, definition_env);
                        new_static_methods[method_id] = class_def->get_static_method(method_id, false);
                    } else {
                        // Instance method: rebuild the overload set + resolving dispatcher with
                        // the SAME machinery a fresh class uses (add_script_method appends to
                        // method_overloads_ and installs the type/arity dispatcher), so reloaded
                        // methods overload correctly instead of collapsing to the last-declared.
                        class_def->add_script_method(method_name, method_ast, definition_env);
                        new_methods[method_id] = class_def->get_method(method_id, false);
                    }
                } else {
                    // For new classes, add method normally
                    try {
                        // Set in_method flag while processing the method
                        if (current_class_context_) {
                            current_class_context_->in_method = true;
                        }

                        if (method_ast->is_static) {
                            // Add static method - pass current environment as definition environment
                            class_def->add_static_script_method(method_name, method_ast, environment_);
                        } else {
                            // Add instance method
                            class_def->add_method_from_ast(method_name, method_ast, environment_, is_redefinition);
                        }
                        
                        // Reset in_method flag
                        if (current_class_context_) {
                            current_class_context_->in_method = false;
                        }
                    } catch (const runtime_error& e) {
                        // Reset in_method flag on error
                        if (current_class_context_) {
                            current_class_context_->in_method = false;
                        }
                        // Don't re-throw "Undefined variable" errors - they'll be validated later
                        std::string error_msg = e.what();
                        if (error_msg.find("Undefined variable") == std::string::npos) {
                            // Re-throw other errors
                            throw;
                        }
                        // For undefined variable errors, we've already collected them in unresolved_identifiers
                    }
                }
            }
        }
    }
    
    // After processing all members, create a dispatcher for constructors if any were found
    if (found_constructor) {
        // Capture the global environment for constructor execution
        // This ensures constructor body has access to global definitions (classes, functions)
        auto definition_env = get_global_environment();

        // Create a constructor dispatcher resolving the live backend at call time
        script_callable ctor_payload;
        ctor_payload.kind = script_callable::kind_type::constructor;
        ctor_payload.cls = class_def;
        ctor_payload.definition_env = definition_env;
        // Named thunk (vm parity): extend(obj, ClassName) recovers the class from the
        // constructor value through target<script_callable_thunk>()
        get_global_environment()->define(decl->name_id, script_value::make_function(
            script_function(script_callable_thunk{engine_, std::move(ctor_payload)}), engine_));
    }

    // If no constructor was found, create a default constructor
    else {
        // Create a default constructor that just initializes the instance
        script_callable ctor_payload;
        ctor_payload.kind = script_callable::kind_type::constructor;
        ctor_payload.cls = class_def;
        // Register default constructor in global environment (constructors are always global)
        get_global_environment()->define(decl->name_id, script_value::make_function(
            script_function(script_callable_thunk{engine_, std::move(ctor_payload)}), engine_));
    }
    
    reflect_field_order_instance.insert(reflect_field_order_instance.end(),
                                        reflect_field_order_static.begin(),
                                        reflect_field_order_static.end());
    class_def->set_field_order(std::move(reflect_field_order_instance));

    // If this is a redefinition, we need to call redefine_class to update all instances
    if (is_redefinition) {
        // Evaluate field initializer ASTs to get actual default values for hot reload
        std::unordered_map<uint64_t, script_value> field_defaults_with_engine;
        field_defaults_with_engine.reserve(new_field_defaults.size());

        for (const auto& [field_id, value] : new_field_defaults) {
            // Get the field initializer AST from the class definition (using ID directly)
            auto initializer_ast = class_def->get_field_initializer_ast(field_id);
            script_value evaluated_value = value;

            if (initializer_ast) {
                // Evaluate the initializer AST in the current (definition) environment
                // to get the actual default value
                JAISCRIPT_TRY(dispatch_expr(initializer_ast.get()));
                evaluated_value = initializer_store_value(pop_value(), initializer_ast.get());
            }

            // Ensure the value has an engine reference
            if (!evaluated_value.has_valid_engine() && engine_) {
                evaluated_value.set_engine(engine_);
            }

            field_defaults_with_engine[field_id] = evaluated_value;
        }

        // Synthesized accessors for every field without a user _get_/_set_ (a user accessor
        // keeps its id; the set replaces auto_accessor_ids_ after redefine_class)
        std::unordered_set<uint64_t> synthesized_accessor_ids;
        for (const auto& [field_id, default_val] : field_defaults_with_engine) {
            // Add getter method
            auto getter = [field_id](const std::vector<script_value>& args) -> checked_result<script_value> {
                if (args.empty()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                        "Property getter requires 'this' object");
                }

                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();

                // Get the field value using the cached ID
                return instance->get_field(field_id);
            };
            auto [getter_id, _1] = string_symbolizer_->get_getter_id_with_view(field_id);
            if (!new_methods.count(getter_id)) {
                new_methods[getter_id] = script_value::make_function(getter, engine_);
                synthesized_accessor_ids.insert(getter_id);
            }

            // Add setter method
            auto setter = [field_id](const std::vector<script_value>& args) -> checked_result<script_value> {
                if (args.size() != 2) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                        "Property setter requires 'this' and value");
                }

                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();

                // Typed fields enforce like locals (declared type; auto infers from the
                // initialized value); the converted value is what gets stored and returned
                auto enforced = instance->enforce_field_write(field_id, args[1]);
                if (!enforced) {
                    return enforced;
                }
                instance->set_field_unchecked(field_id, enforced.value());
                return std::move(enforced.value());
            };
            auto [setter_id, _2] = string_symbolizer_->get_setter_id_with_view(field_id);
            if (!new_methods.count(setter_id)) {
                new_methods[setter_id] = script_value::make_function(setter, engine_);
                synthesized_accessor_ids.insert(setter_id);
            }
        }
        
        
        // Declared types install first so redefine_class flags a retype as fields_changed
        // and migrate_fields converts against the NEW types (retype ruling)
        class_def->replace_field_declared_types(std::move(new_field_types), true);
        class_def->replace_static_declared_types(std::move(new_static_types));

        // Structural identity (position-insensitive AST key): an identical reload adopts
        // the freshly minted ASTs (line info stays current) but skips all per-instance
        // migration work. Shape counts gate the O(nodes) walk; serializer failure = not
        // identical. The key stores lazily, so the first redefinition always compares
        // false and seeds it.
        std::vector<uint8_t> structural_key;
        bool structurally_identical = false;
        if (class_def->structural_key_shape_matches(decl->members.size(), decl->base_classes.size())) {
            try {
                structural_key = detail::structural_node_key(decl, *string_symbolizer_);
                structurally_identical = (structural_key == class_def->structural_key());
            } catch (...) {
                structural_key.clear();
            }
        }

        class_def->redefine_class(field_defaults_with_engine, new_methods, new_static_methods, engine_,
                                  structurally_identical);
        class_def->set_auto_accessor_ids(std::move(synthesized_accessor_ids));

        if (structural_key.empty()) {
            try {
                structural_key = detail::structural_node_key(decl, *string_symbolizer_);
            } catch (...) {
                structural_key.clear();
            }
        }
        if (!structural_key.empty()) {
            class_def->set_structural_key(std::move(structural_key), decl->members.size(), decl->base_classes.size());
        }

        // Statics dropped from the new definition must no longer be accessible.
        class_def->retain_static_fields(new_static_field_ids);

        // A hot_reload_migrate hook that errored left the interpreter unwinding. That error
        // belongs to the migration, not to the class declaration — clear it so the reload is
        // atomic and doesn't propagate a per-instance hook failure as the statement's result.
        if (is_unwinding_) {
            is_unwinding_ = false; trace_captured_ = false;
            current_exception_.reset();
            active_exception_value_.reset();
        }

        // Invalidate cached field pointers - they may point to stale storage after migration
        environment_->clear_all_parent_caches();
    } else {
        // For new classes, add the fields normally
        class_def->replace_field_declared_types(std::move(new_field_types), false);
        class_def->replace_static_declared_types(std::move(new_static_types));
        for (const auto& [field_id, default_val] : new_field_defaults) {
            // Convert ID back to string for add_field (legacy API)
            std::string field_name(string_symbolizer_->get_string(field_id));
            class_def->add_field(field_name, default_val);

            // Generate getter and setter methods for script class fields
            // This enables property-style access (obj.field) to work properly

            // Add getter method - capture field_id for performance
            auto getter = [field_id](const std::vector<script_value>& args) -> checked_result<script_value> {
                if (args.empty()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                        "Property getter requires 'this' object");
                }

                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();

                // Get the field value using ID
                return instance->get_field(field_id);
            };
            auto [getter_id, _1] = string_symbolizer_->get_getter_id_with_view(field_id);
            if (!class_def->has_method(getter_id)) { class_def->add_method_by_id(getter_id, getter, true); }   // a user _get_ keeps its id

            // Add setter method - capture field_id for performance
            auto setter = [field_id](const std::vector<script_value>& args) -> checked_result<script_value> {
                if (args.size() != 2) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                        "Property setter requires 'this' and value");
                }

                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();

                // Typed fields enforce like locals (declared type; auto infers from the
                // initialized value); the converted value is what gets stored and returned
                auto enforced = instance->enforce_field_write(field_id, args[1]);
                if (!enforced) {
                    return enforced;
                }
                instance->set_field_unchecked(field_id, enforced.value());
                return std::move(enforced.value());
            };
            auto [setter_id, _2] = string_symbolizer_->get_setter_id_with_view(field_id);
            if (!class_def->has_method(setter_id)) { class_def->add_method_by_id(setter_id, setter, true); }
        }
    }
    
    // Validate unresolved identifiers before finalizing the class
    if (current_class_context_ && !current_class_context_->unresolved_identifiers.empty()) {
        // Get all fields including inherited ones (already efficient)
        auto all_fields = class_def->get_all_field_defaults();
        
        // Check each unresolved identifier (already stored as IDs)
        std::vector<std::string> undefined_identifiers;
        for (const auto& identifier_id : current_class_context_->unresolved_identifiers) {
            // Skip special keywords that are always valid in methods
            if (identifier_id == this_id_ || identifier_id == super_id_) continue;

            // Check if it's a field (all_fields uses ID-based keys)
            if (all_fields.find(identifier_id) != all_fields.end()) continue;

            // Convert ID back to string for find_method (which still uses strings)
            std::string identifier = std::string(string_symbolizer_->get_string(identifier_id));

            // Check if it's a method (including inherited)
            if (class_def->find_method(identifier).owner_class != nullptr) continue;

            // Not found as field or method
            undefined_identifiers.push_back(identifier);
        }
        
        // If there are still undefined identifiers, throw an error
        if (!undefined_identifiers.empty()) {
            // Use class name_id and first undefined identifier for error context
            uint64_t class_id = (decl->name_id != UINT64_MAX) ? decl->name_id : string_symbolizer_->intern(decl->name);
            uint64_t first_undef_id = string_symbolizer_->intern(undefined_identifiers[0]);
            return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                "Undefined identifier '{0}' in class '{1}'", first_undef_id, class_id);
        }
    }

    // CRITICAL: Register the script class in the class registry
    // This makes it available for type lookups and prevents "unregistered class" errors
    auto eng = engine_;
    if (!eng) {
        return checked_result<void>(make_error_code(runtime_error_code::engine_destroyed));
    }
    auto register_result = eng->get_class_registry().register_script_class(class_def);
    if (!register_result) {
        return register_result;
    }
    eng->bump_class_definition_epoch();
    detail::warn_shadowed_handle_builtins(*eng, *class_def);

    // Store the class definition in a special variable for later retrieval
    // This allows inheritance and other features to work
    // IMPORTANT: Define in GLOBAL environment so it's visible across execute() calls (hot reload)
    global_env->define(class_var_id, script_value::make_object("class_definition", class_definition_type_id_, class_def, engine_, false));

    // The constructor function is already registered in the environment
    // which allows "new ClassName()" syntax to work
    return {};
}

checked_result<void> interpreter::visit_namespace_decl(namespace_decl* decl) {
    // Namespaces are FLAT - "my::nested::deep" is a single namespace name
    // Members are stored in a registry and accessed via qualified names (ns::member)

    // Intern the namespace name if not already done
    if (decl->name_id == UINT64_MAX) {
        decl->name_id = string_symbolizer_->intern(decl->name);
    }

    // Engine-owned namespaces cannot be supplied or reopened by script.
    if (is_reserved_namespace_name(decl->name)) {
        return reserved_namespace_error(decl->name_id);
    }

    // Get or create namespace data
    if (!engine_) {
        return checked_result<void>(make_error_code(runtime_error_code::engine_destroyed));
    }
    auto& ns_data = engine_->script_namespaces()[decl->name_id];
    if (!ns_data) {
        ns_data = std::make_shared<script_namespace_data>();
    }

    // Process all declarations within the namespace
    // Functions, variables, and classes are stored in the namespace_data registry
    for (const auto& member_decl : decl->declarations) {
        // Check what kind of declaration this is
        if (member_decl->get_type() == node_type::function_decl) {
            auto* func_decl = static_cast<function_decl*>(member_decl.get());
            // Intern the function name if not already done
            if (func_decl->name_id == UINT64_MAX) {
                func_decl->name_id = string_symbolizer_->intern(func_decl->name);
            }

            // Check for collisions: same name AND same arity in this namespace
            func_decl->unit = current_unit();
            auto& overloads = ns_data->functions[func_decl->name_id];
            for (auto it = overloads.begin(); it != overloads.end(); ++it) {
                if ((*it)->parameters.size() == func_decl->parameters.size()) {
                    // Collision detected!
                    if (!func_decl->is_override) {
                        // func_decl->name_id already interned at line 8216
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Function '{0}' already exists in namespace '{1}'. Use 'override' keyword to replace it.",
                            func_decl->name_id, decl->name_id);
                    }
                    // Override is specified - remove old definition
                    overloads.erase(it);
                    break;
                }
            }

            // Check if this namespace name matches a class name
            // If so, check for collision with class static methods
            if (!func_decl->is_override) {
                auto [class_var_id, class_var_name] = string_symbolizer_->get_class_var_id_with_view(decl->name_id);
                auto class_result = environment_->get(class_var_id);
                if (class_result && class_result.value().is_object()) {
                    script_value class_var = std::move(class_result.value());
                    {
                        auto obj_holder = class_var.get_object_holder();
                        if (obj_holder && obj_holder->type_id == class_definition_type_id_) {
                            auto class_def = std::static_pointer_cast<class_definition>(obj_holder->data);

                            // Arity-aware collision check
                            if (class_def->has_static_method_with_arity(func_decl->name_id, func_decl->parameters.size())) {
                                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                                    "Function '{0}' in namespace '{1}' collides with static method. Use 'override' to override.",
                                    func_decl->name_id, decl->name_id);
                            }
                        }
                    }
                }
            }

            // Store function declaration
            overloads.emplace_back(std::make_shared<function_decl>(*func_decl));

        } else if (member_decl->get_type() == node_type::variable_decl) {
            auto* var_decl = static_cast<variable_decl*>(member_decl.get());
            // Intern the variable name if not already done
            if (var_decl->name_id == UINT64_MAX) {
                var_decl->name_id = string_symbolizer_->intern(var_decl->name);
            }

            // The declaration runs in the namespace's scope (visit_variable_decl's every path): its cell lives there
            // and its initializer sees the members declared before it. KEEP BYTE-PARALLEL with vm_backend::exec_namespace_variable.
            struct scope_swap { std::shared_ptr<environment>& slot; std::shared_ptr<environment> saved; ~scope_swap() { slot = std::move(saved); } } swap{environment_, environment_};
            environment_ = detail::namespace_scope(*engine_, *ns_data);
            if (!var_decl->is_inline && !environment_->plain_declare(var_decl->name_id)) [[unlikely]] {
                return detail::inline_redefinition(*string_symbolizer_, var_decl->name_id, var_decl);
            }
            JAISCRIPT_TRY(visit_variable_decl(var_decl));
            if (is_unwinding_) {
                return {};
            }

        } else if (member_decl->get_type() == node_type::class_decl) {
            auto* class_decl_ptr = static_cast<class_decl*>(member_decl.get());
            // Intern the class name if not already done
            if (class_decl_ptr->name_id == UINT64_MAX) {
                class_decl_ptr->name_id = string_symbolizer_->intern(class_decl_ptr->name);
            }

            // Process class declaration normally to register it globally
            // Then also store reference in namespace
            JAISCRIPT_TRY(dispatch_decl(class_decl_ptr));

            // Look up the registered class definition using __class_ prefix (cached)
            auto [class_var_id, class_var_name] = string_symbolizer_->get_class_var_id_with_view(class_decl_ptr->name_id);
            if (auto* class_def_var = environment_->get_value_ptr(class_var_id)) {
                if (class_def_var->is_object()) {
                    auto obj_holder = class_def_var->get_object_holder();
                    if (obj_holder && obj_holder->type_id == class_definition_type_id_) {
                        auto class_def = std::static_pointer_cast<class_definition>(obj_holder->data);
                        ns_data->classes[class_decl_ptr->name_id] = class_def;
                    }
                }
            }

        } else {
            // Other declaration types (expressions, includes, etc.) - execute normally
            JAISCRIPT_TRY(dispatch_decl(member_decl.get()));
        }
    }

    return {};
}

// The block-level statement form (the parser wraps every `expr;` inside a block as an
// expression_decl): the value stays on the stack for a top-level implicit return
template<bool Replaying>
checked_result<void> interpreter::visit_expression_decl_t(expression_decl* decl) {
    // A statement's value is not consumed: no read-back for an assignment expression (the
    // top-level implicit return hands the host the right-hand side, like the vm)
    struct restore_root { const expression*& root; const expression* saved; ~restore_root() { root = saved; } } restore{ unconsumed_root_, unconsumed_root_ };
    unconsumed_root_ = decl->expression.get();
    JAISCRIPT_TRY_ROOT(decl, decl->expression.get());
    return {};
}

// One include body for BOTH positions: statement (include_decl, value discarded by the
// caller) and expression (include_expr, value IS the expression) — identical path
// resolution, execution, and errors by construction.
checked_result<void> interpreter::include_into_value_stack(expression* path_expr_node, const std::string& literal_path) {
    // Get the engine reference
    auto engine_ptr = engine_;
    if (!engine_ptr) {
        // [ErrorText] Engine reference expired during include processing
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    // Get the path either from literal or expression
    std::string path;
    if (path_expr_node) {
        // Evaluate the expression to get the path
        JAISCRIPT_TRY(dispatch_expr(path_expr_node));
        script_value path_value = pop_value();

        // Convert to string
        if (path_value.type() != script_value_type::jai_string_type) {
            // [ErrorText] Include path expression must evaluate to a string
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
        }
        path = path_value.as<std::string>();
    } else {
        // Use the literal path
        path = literal_path;
    }

    // Resolve the file path
    auto resolve_result = resolve_include_path(path, engine_ptr);
    if (!resolve_result) {
        return resolve_result.error_value();
    }
    std::string resolved_path = std::move(resolve_result.value());

    // Read the file contents
    std::ifstream file(resolved_path);
    if (!file.is_open()) {
        // [ErrorText] Failed to open include file
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Execute the included file. Pass the resolved path so every node in the included
    // unit is stamped with its real filename (stack traces / breakpoints name the file).
    std::string scope;
    if (!call_stack_.empty()) {
        const std::string_view fn = call_stack_.back().function_name;
        scope = fn.empty() || fn.front() == '<' ? std::string("a lambda") : "function '" + std::string(fn) + "'";
    } else if (environment_ != get_global_environment()) {
        scope = "a block";
    }
    nested_failure_.parked = false;
    try {
        push_value(engine_ptr->execute_include_source(resolved_path, content, scope));
    } catch (const std::exception& e) {
        return raise_nested_failure(e);
    }
    return {};
}

checked_result<void> interpreter::raise_native_exception(const std::exception& e) {
    active_exception_value_ = make_value(std::string(e.what()));
    if (const auto* thrown = dynamic_cast<const script_exception*>(&e)) {
        current_exception_ = *thrown;
    } else {
        current_exception_ = script_exception(e.what());
    }
    is_unwinding_ = true;
    push_value(make_value());
    return {};
}

// A failure of a nested run (include/import) re-raised in this run as if the nested code ran here: its runtime error
// itself (caught with its text alone), or its thrown value; a load failure (parse, static check, missing file) raises its
// text. KEEP BYTE-PARALLEL with vm_backend::raise_nested_failure.
checked_result<void> interpreter::raise_nested_failure(const std::exception& e) {
    if (!nested_failure_.parked) {
        active_exception_value_ = make_value(std::string(e.what()));
        current_exception_ = script_exception(e.what());
        is_unwinding_ = true;
        push_value(make_value());
        return {};
    }
    absorb_nested_trace();
    nested_failure_.parked = false;
    if (nested_failure_.error) { return *nested_failure_.error; }
    active_exception_value_ = nested_failure_.value ? std::move(nested_failure_.value) : std::optional<script_value>(make_value());
    current_exception_ = nested_failure_.exception ? std::move(nested_failure_.exception) : std::optional<script_exception>(script_exception(e.what()));
    is_unwinding_ = true;
    push_value(make_value());
    return {};
}

checked_result<void> interpreter::visit_include_decl(include_decl* decl) {
    return include_into_value_stack(decl->path_expr.get(), decl->path);
}

checked_result<void> interpreter::visit_include_expr(include_expr* expr) {
    return include_into_value_stack(expr->path_expr.get(), expr->path);
}

// The import step both forms take (KEEP BYTE-PARALLEL with vm_backend::exec_import): the engine's check pass decides
// whether this reach looks at the disk; the value is whether a module (re)ran (the statement form's is null)
checked_result<void> interpreter::import_step(import_site& site, const std::vector<std::string>* literal, const std::string* single,
                                              const std::vector<expression_ptr>& computed, bool force, bool yields) {
    if (!engine_) {
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }
    std::vector<std::string> paths;
    if (!computed.empty()) {
        const size_t n = computed.size();
        for (size_t i = 0; i < n; ++i) {   // every argument evaluates before any is read (the vm's order)
            JAISCRIPT_TRY(dispatch_expr(computed[i].get()));
            if (is_unwinding_) {
                for (size_t k = 0; k <= i; ++k) { valueStack_.discard(); }
                push_value(make_value());
                return {};
            }
        }
        const script_value* args[8];
        if (!force && n <= 8) {
            for (size_t i = 0; i < n; ++i) { args[i] = &valueStack_.peek(n - 1 - i); }
            if (engine_->import_skips(site, args, n)) {
                for (size_t i = 0; i < n; ++i) { valueStack_.discard(); }
                push_value(yields ? make_value(false) : make_value());
                return {};
            }
        }
        checked_result<void> added;
        for (size_t i = 0; i < n && added; ++i) { added = detail::append_import_paths(valueStack_.peek(n - 1 - i), paths, engine_); }
        for (size_t i = 0; i < n; ++i) { valueStack_.discard(); }
        if (!added) { return added; }
    } else {
        if (!force && engine_->import_skips(site)) {
            push_value(yields ? make_value(false) : make_value());
            return {};
        }
        if (literal) { paths = *literal; } else { paths.push_back(*single); }   // built past the skip: an idle reach allocates nothing
    }
    nested_failure_.parked = false;
    try {
        auto ran = engine_->import_modules(site, paths, force);
        if (!ran) { return ran.error_value(); }
        push_value(yields ? make_value(ran.value()) : make_value());
    } catch (const std::exception& e) {
        return raise_nested_failure(e);
    }
    return {};
}

checked_result<void> interpreter::visit_import_decl(import_decl* decl) {
    if (decl->path_expr) {
        return import_step(decl->site, nullptr, nullptr, std::vector<expression_ptr>{decl->path_expr}, false, false);
    }
    return import_step(decl->site, nullptr, &decl->path, {}, false, false);
}

checked_result<void> interpreter::visit_import_expr(import_expr* expr) {
    return import_step(expr->site, &expr->paths, nullptr, expr->path_exprs, expr->force, true);
}

// Execute a method AST with a given environment
checked_result<script_value> interpreter::execute_method_ast(std::shared_ptr<function_decl> ast,
                                           std::shared_ptr<environment> method_env,
                                           const std::vector<script_value>& args) {
    // Coroutine methods mint a handle instead of executing (free-coroutine parity).
    // The handle pins its OWN method env carrying 'this' — the caller recycles
    // method_env back to the pool right after this returns — plus the receiver, so
    // the instance stays alive while suspended and 'this' resolves identically
    // across resumes. KEEP BYTE-PARALLEL with vm_backend::execute_method_ast.
    if (ast->is_coroutine) {
        const uint64_t this_id = string_symbolizer_->get_this_id();
        script_value this_obj(std::monostate{}, engine_);
        if (auto this_result = method_env->get(this_id)) {
            this_obj = std::move(this_result.value());
        }
        auto static_class = method_env->get_class_definition();
        auto coro_env = static_class
            ? std::make_shared<environment>(method_env->get_parent(), string_symbolizer_, static_class)
            : std::make_shared<environment>(method_env->get_parent(), string_symbolizer_, this_obj);
        coro_env->set_access_context(method_env->get_access_context());
        coro_env->define(this_id, this_obj);
        auto handle = std::make_shared<coroutine_handle>(engine_);
        handle->set_function(ast, args, coro_env);
        handle->set_receiver(std::move(this_obj));
        return make_coroutine_object(engine_, coroutine_handle_type_id_, handle);
    }

    // Create a script_defined_function with the method environment (parameter storage
    // shared from the node - no per-call vector copy)
    script_defined_function script_func(
        ast->name,
        shared_parameters_for(ast.get()),
        ast->return_type,
        ast->body,
        method_env,  // Method environment with 'this'
        ast->local_count
    );
    script_func.unit = ast->unit;

    // Execute method with the arguments - propagate errors to caller
    return call_function(script_func, args);
}

// Evaluate field initializers for a script class instance at construction time
// If skip_parent_recursion is true, only evaluate THIS class's field initializers (parents already handled)
checked_result<void> interpreter::evaluate_field_initializers(std::shared_ptr<class_instance> instance,
                                             std::shared_ptr<script_class_definition> class_def,
                                             std::shared_ptr<environment> init_env,
                                             bool skip_parent_recursion) {
    const unit_stamp class_unit = class_def->unit();   // the initializers are the class's code (no frame of their own)
    unit_override_scope unitScope(this, &class_unit);
    // First, evaluate parent class field initializers (if any)
    // Support multiple inheritance by iterating over all parent classes
    // Skip this if parents were already processed (e.g., by multi-level super() chain handling)
    if (!skip_parent_recursion) {
        for (const auto& parent : class_def->get_parent_classes()) {
            auto parent_script_class = std::dynamic_pointer_cast<script_class_definition>(parent);
            if (parent_script_class) {
                // Recursively evaluate parent field initializers
                auto initialized = evaluate_field_initializers(instance, parent_script_class, init_env, false);
                if (!initialized) { return initialized.error_value(); }
            }
        }
    }

    // Now evaluate this class's field initializers
    const auto& field_initializers = class_def->get_field_initializer_asts();

    // Temporarily switch to the init environment for evaluation. Field initializers
    // run in the new instance's member scope (no call frame is pushed here), so the
    // shadowing ladder needs an explicit stamp - earlier fields and members of the
    // class under construction outrank same-named globals, exactly like ctor bodies.
    struct initializer_scope_restore {
        interpreter* self;
        std::shared_ptr<environment> previous_env;
        detail::member_scope_ctx previous_scope;
        ~initializer_scope_restore() {
            self->environment_ = std::move(previous_env);
            self->member_scope_ = previous_scope;
        }
    } scope_restore{this, environment_, member_scope_};
    environment_ = init_env;
    member_scope_ = {};
    if (script_value* init_this = init_env->method_this_storage()) {
        member_scope_.state = detail::member_scope_state::instance;
        member_scope_.this_storage = init_this;
        member_scope_.floor = init_env->parent_raw();
    }

    for (const auto& [field_id, initializer_ast] : field_initializers) {
        if (initializer_ast) {
            // Evaluate the initializer expression
            auto result = dispatch_expr(initializer_ast.get());
            if (!result) {
                if (result.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
                    return result.error_value();
                }
                throw runtime_error("Failed to evaluate field initializer for '" + std::string(string_symbolizer_->get_string(field_id)) + "'");
            }
            script_value field_value = initializer_store_value(pop_value(), initializer_ast.get());

            // Ensure the field value has an engine reference
            if (!field_value.has_valid_engine() && engine_) {
                field_value.set_engine(engine_);
            }

            // Set the field on the instance using the field_id directly
            instance->set_field(field_id, field_value);
        }
    }

    return {};
}

void interpreter::apply_field_initializers(const std::shared_ptr<class_instance>& instance,
                                           const std::shared_ptr<class_definition>& class_def) {
    auto script_def = std::dynamic_pointer_cast<script_class_definition>(class_def);
    if (!script_def || !instance) { return; }
    auto this_value = script_value::make_class_object(*class_def, instance, engine_);
    auto init_env = std::make_shared<environment>(get_global_environment(), string_symbolizer_);
    init_env->define("this", this_value);
    init_env->set_access_context(class_def.get());
    auto initialized = evaluate_field_initializers(instance, script_def, init_env, false);
    if (!initialized) { throw runtime_error(format_error(initialized, *string_symbolizer_)); }
}

checked_result<script_value> interpreter::execute_callable(const script_callable& payload, const std::vector<script_value>& args) {
    // A destructor a throw's unwinding released runs under its own exception state and the throw resumes after it (C++:
    // destructors run during unwinding); what the destructor raised dies with it (KEEP BYTE-PARALLEL with vm_backend::execute_callable)
    struct unwind_park {
        interpreter* self;
        std::optional<script_exception> exception;
        std::optional<script_value> value;
        std::vector<stack_trace_entry> trace;
        bool trace_captured = false;
        uint64_t catch_var = 0;
        explicit unwind_park(interpreter* in) : self(in) {
            exception = std::move(self->current_exception_);
            self->current_exception_.reset();
            value = std::move(self->active_exception_value_);
            self->active_exception_value_.reset();
            trace = std::move(self->captured_trace_);
            self->captured_trace_.clear();
            trace_captured = self->trace_captured_;
            self->trace_captured_ = false;
            catch_var = self->current_catch_var_id_;
            self->current_catch_var_id_ = 0;
            self->is_unwinding_ = false;
        }
        ~unwind_park() {
            self->current_exception_ = std::move(exception);
            self->active_exception_value_ = std::move(value);
            self->captured_trace_ = std::move(trace);
            self->trace_captured_ = trace_captured;
            self->current_catch_var_id_ = catch_var;
            self->is_unwinding_ = true;
        }
    };
    std::optional<unwind_park> park;
    if (is_unwinding_) [[unlikely]] { park.emplace(this); }
    switch (payload.kind) {
        case script_callable::kind_type::function:
            return call_function(*payload.fn, args);
        case script_callable::kind_type::method: {
            auto method_env = get_pooled_method_environment(payload.definition_env, *payload.this_obj, payload.cls.get());
            method_env->define("this", *payload.this_obj);
            auto result = execute_method_ast(payload.ast, method_env, args);
            // Always release the method environment back to the pool (even on error!)
            release_environment(method_env, false);
            return result;
        }
        case script_callable::kind_type::static_method: {
            auto static_env = std::make_shared<environment>(payload.definition_env, string_symbolizer_, payload.cls);
            return execute_method_ast(payload.ast, static_env, args);
        }
        case script_callable::kind_type::constructor: {
            auto script_cls = std::dynamic_pointer_cast<script_class_definition>(payload.cls);
            if (!script_cls) {
                return checked_result<script_value>(make_error_code(runtime_error_code::internal_error), "Constructor payload without script class definition");
            }
            if (script_cls->get_constructor_asts().empty()) {
                return construct_default_instance(script_cls, args);
            }
            return construct_instance(script_cls, payload.definition_env, args);
        }
    }
    return checked_result<script_value>(make_error_code(runtime_error_code::internal_error), "Unknown callable kind");
}

checked_result<script_value> interpreter::initialize_members(const std::shared_ptr<script_class_definition>& class_def,
                                                             const std::shared_ptr<environment>& definition_env,
                                                             const std::shared_ptr<class_instance>& instance,
                                                             const script_value& this_value,
                                                             const std::shared_ptr<function_decl>& matching_ctor,
                                                             const std::vector<script_value>& args,
                                                             std::optional<detail::call_depth_guard>& initializer_depth,
                                                             size_t delegation_depth,
                                                             std::vector<script_value>* captured_out) {
    if (delegation_depth > detail::k_max_constructor_delegation_depth || detail::native_stack_low()) {
        return detail::raise_call_depth();   // a delegation cycle (ill-formed in C++) raises instead of recursing without end
    }
    // Create a regular environment for field initializers and constructor initializer arguments
    // Use the captured definition environment as the parent
    auto init_env = std::make_shared<environment>(definition_env, string_symbolizer_);
    init_env->define("this", this_value);
    init_env->set_access_context(class_def.get());   // field initializers are class-body code

    // Bind constructor parameters so they're available in initializer expressions
    // NOTE: Do NOT clone here - these params are just for field initializer evaluation
    // The actual parameter binding with proper value/reference semantics happens in
    // call_function (which also evaluates trailing defaults for omitted args; omitted
    // params are simply absent here, like C++ field-inits that can't see ctor params)
    if (!arity_accepts(matching_ctor->parameters, args.size())) {
        return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
            "Constructor parameter count mismatch");
    }
    // the construction runs code ahead of the body (an initializer list, a non-literal field initializer): the call's
    // arguments are taken first, as the body's parameters (KEEP BYTE-PARALLEL with vm_backend::initialize_members); a
    // delegated-to constructor's arguments were taken as they evaluated
    const std::vector<script_value>* argv = &args;
    if (captured_out && detail::construction_runs_code(*class_def, *matching_ctor) &&
        detail::later_capture_candidate(0, args.size(), [&](size_t j) -> const script_value& { return args[j]; })) {
        *captured_out = args;
        for (size_t j = 0; j < captured_out->size(); ++j) {
            detail::capture_argument((*captured_out)[j], detail::transient_read_marker::arg_capture_full, detail::parameters_binding(matching_ctor->parameters, j));
        }
        argv = captured_out;
    }
    for (size_t i = 0; i < matching_ctor->parameters.size() && i < argv->size(); ++i) {
        init_env->define(matching_ctor->parameters[i].name, detail::argument_for_binding((*argv)[i], detail::parameters_binding(matching_ctor->parameters, i)));
    }

    // Track whether the iterative multi-level loop handled parent field initializers
    bool handled_parent_init = false;
    // Track `: this(...)` delegation. C++ delegating-ctor semantics: the
    // target constructor constructs the members; the delegating ctor must NOT
    // re-run field initializers afterward (that re-applies declared defaults
    // and clobbers what the target set).
    bool delegated_to_this = false;

    // Process constructor initializers (: super(args), : this(args))
    for (const auto& initializer : matching_ctor->initializers) {
        if (initializer.target == "super") {
            // Call base class constructor
            if (class_def->get_parent()) {
                // Evaluate initializer arguments in init environment
                std::vector<script_value> init_args;
                init_args.reserve(initializer.arguments.size());
                
                // Temporarily switch to init environment for argument evaluation
                auto old_env = environment_;
                environment_ = init_env;

                for (const auto& arg_expr : initializer.arguments) {
                    auto result = dispatch_expr(arg_expr.get());
                    if (!result) {
                        // Restore environment before returning error
                        environment_ = old_env;
                        if (result.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
                            return result.error_value();
                        }
                        return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
                            "Failed to evaluate constructor initializer argument");
                    }
                    init_args.push_back(pop_value());
                    detail::capture_initializer_argument(initializer, init_args.size() - 1, !class_def->chain_initializers_all_literal(), init_args.back(),
                                                         [&] { return detail::constructor_binding(class_def->get_parent().get(), init_args.size() - 1); });
                }

                // Restore environment
                environment_ = old_env;
                
                // Call parent constructor to initialize parent fields
                auto parent_class = class_def->get_parent();
                if (parent_class) {
                    // Check if parent is a script class
                    auto parent_script_class = std::dynamic_pointer_cast<script_class_definition>(parent_class);
                    if (parent_script_class) {
                        // Find matching parent constructor (fewest defaults used wins)
                        const auto& parent_ctor_asts = parent_script_class->get_constructor_asts();
                        std::shared_ptr<function_decl> parent_ctor = pick_best_overload(parent_ctor_asts, init_args);   // typed exact > conversion > var, else the arity match
                        size_t parent_defaults = SIZE_MAX;
                        if (!parent_ctor) for (const auto& ctor_ast : parent_ctor_asts) {
                            if (arity_accepts(ctor_ast->parameters, init_args.size()) &&
                                ctor_ast->parameters.size() - init_args.size() < parent_defaults) {
                                parent_ctor = ctor_ast;
                                parent_defaults = ctor_ast->parameters.size() - init_args.size();
                            }
                        }

                        if (parent_ctor) {
                            // === Multi-level inheritance support ===
                            // We need to process the entire super() chain to find and call the C++ base constructor.
                            // Constructor bodies are executed AFTER field initializers by the outer flow.
                            //
                            // Algorithm (iterative):
                            // 1. Walk up super() calls, evaluating arguments at each level
                            // 2. When we hit a C++ class, call its constructor to get _cpp_object
                            // 3. Store constructor info for later body execution
                            // 4. Execute bodies from root to leaf, interleaved with field initializers

                            struct CtorChainEntry {
                                std::shared_ptr<script_class_definition> script_class;
                                std::shared_ptr<function_decl> ctor;
                                std::vector<script_value> args;
                            };

                            std::vector<CtorChainEntry> ctor_chain;
                            ctor_chain.push_back({parent_script_class, parent_ctor, init_args});

                            // Walk up the inheritance chain, collecting constructors and evaluating args
                            size_t chain_idx = 0;
                            while (chain_idx < ctor_chain.size()) {
                                auto& entry = ctor_chain[chain_idx];

                                // Create environment for this level's argument evaluation
                                auto level_env = std::make_shared<environment>(definition_env, string_symbolizer_);
                                level_env->define("this", this_value);
                                level_env->set_access_context(entry.script_class.get());
                                for (size_t pi = 0; pi < entry.ctor->parameters.size() && pi < entry.args.size(); ++pi) {
                                    level_env->define(entry.ctor->parameters[pi].name, entry.args[pi]);
                                }

                                // Look for super() in this constructor's initializers
                                for (const auto& init : entry.ctor->initializers) {
                                    if (init.target == "super") {
                                        auto ancestor = entry.script_class->get_parent();
                                        if (ancestor) {
                                            // Evaluate super() arguments in this level's environment
                                            std::vector<script_value> ancestor_args;
                                            auto old_env = environment_;
                                            environment_ = level_env;
                                            for (const auto& arg_expr : init.arguments) {
                                                auto r = dispatch_expr(arg_expr.get());
                                                if (!r) {
                                                    environment_ = old_env;
                                                    if (r.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
                                                        return r.error_value();
                                                    }
                                                    return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
                                                        "Failed to evaluate super() argument");
                                                }
                                                ancestor_args.push_back(pop_value());
                                                detail::capture_initializer_argument(init, ancestor_args.size() - 1, !class_def->chain_initializers_all_literal(), ancestor_args.back(),
                                                                                     [&] { return detail::constructor_binding(ancestor.get(), ancestor_args.size() - 1); });
                                            }
                                            environment_ = old_env;

                                            auto ancestor_script = std::dynamic_pointer_cast<script_class_definition>(ancestor);
                                            if (ancestor_script) {
                                                // Ancestor is a script class - find matching constructor and add to chain
                                                // (fewest defaults used wins)
                                                const auto& ancestor_ctors = ancestor_script->get_constructor_asts();
                                                std::shared_ptr<function_decl> ancestor_ctor = pick_best_overload(ancestor_ctors, ancestor_args);   // typed exact > conversion > var, else the arity match
                                                size_t ancestor_defaults = SIZE_MAX;
                                                if (!ancestor_ctor) for (const auto& ac : ancestor_ctors) {
                                                    if (arity_accepts(ac->parameters, ancestor_args.size()) &&
                                                        ac->parameters.size() - ancestor_args.size() < ancestor_defaults) {
                                                        ancestor_ctor = ac;
                                                        ancestor_defaults = ac->parameters.size() - ancestor_args.size();
                                                    }
                                                }
                                                if (ancestor_ctor) {
                                                    ctor_chain.push_back({ancestor_script, ancestor_ctor, std::move(ancestor_args)});
                                                } else if (!ancestor_ctors.empty()) {
                                                    return detail::overload_miss(make_error_code(runtime_error_code::no_constructor_found),
                                                        ancestor_script->get_type_id(), ancestor_args.data(), ancestor_args.size(), engine_, true);
                                                }
                                            } else {
                                                // Ancestor is a C++ class - call its constructor NOW
                                                auto cpp_name = ancestor->get_name();
                                                auto cpp_ctor_result = environment_->get(cpp_name);
                                                if (cpp_ctor_result && cpp_ctor_result.value().is_function()) {
                                                    auto cpp_result = cpp_ctor_result.value().as_function()(ancestor_args);
                                                    if (!cpp_result) {
                                                        return checked_result<script_value>(make_error_code(runtime_error_code::constructor_failed),
                                                            "Failed to call C++ ancestor constructor");
                                                    }
                                                    script_value cpp_obj = std::move(cpp_result.value());

                                                    // Copy _cpp_object from C++ instance to our instance
                                                    if (cpp_obj.is_object()) {
                                                        auto cpp_instance = cpp_obj.as<std::shared_ptr<class_instance>>();
                                                        if (cpp_instance) {
                                                            uint64_t src_id = cpp_instance->get_cpp_object_field_id();
                                                            uint64_t dst_id = instance->get_cpp_object_field_id();
                                                            if (cpp_instance->has_field(src_id)) {
                                                                instance->set_field(dst_id, cpp_instance->get_field(src_id));
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        break; // Only one super() per constructor
                                    }
                                }
                                chain_idx++;
                            }

                            // Execute field initializers and constructor bodies from root to leaf
                            // This ensures proper initialization order:
                            // 1. Grandparent field defaults, then grandparent body
                            // 2. Parent field defaults, then parent body
                            // 3. (Current class handled by outer code after this block)
                            for (auto it = ctor_chain.rbegin(); it != ctor_chain.rend(); ++it) {
                                // Create init environment for field initializers
                                auto level_init_env = std::make_shared<environment>(definition_env, string_symbolizer_);
                                level_init_env->define("this", this_value);
                                level_init_env->set_access_context(it->script_class.get());
                                for (size_t pi = 0; pi < it->ctor->parameters.size() && pi < it->args.size(); ++pi) {
                                    level_init_env->define(it->ctor->parameters[pi].name, it->args[pi]);
                                }

                                // Evaluate THIS class's field initializers only (not parents - they're handled by their own iteration)
                                const auto& field_initializers = it->script_class->get_field_initializer_asts();
                                auto old_env = environment_;
                                environment_ = level_init_env;
                                for (const auto& [field_id, initializer_ast] : field_initializers) {
                                    if (initializer_ast) {
                                        auto r = dispatch_expr(initializer_ast.get());
                                        if (r) {
                                            instance->set_field(field_id, initializer_store_value(pop_value(), initializer_ast.get()));
                                        } else if (r.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
                                            return r.error_value();
                                        }
                                    }
                                }
                                environment_ = old_env;

                                // Execute constructor body
                                scoped_method_environment method_env(
                                    this,
                                    definition_env,
                                    this_value,
                                    it->script_class.get()
                                );
                                initializer_depth.reset();
                                auto ctor_result = execute_method_ast(it->ctor, method_env.get(), it->args);
                                if (!ctor_result) return ctor_result.error_value();
                                initializer_depth.emplace(current_call_depth_);
                            }
                            // Mark that we handled all parent field initializers
                            handled_parent_init = true;
                        } else if (parent_ctor_asts.empty() && init_args.empty()) {
                            // Parent has no explicit constructors (only default constructor)
                            // and super() called with no arguments - this is valid, nothing to do
                            // The parent fields will be initialized with their default values
                        } else {
                            return checked_result<script_value>(make_error_code(runtime_error_code::no_constructor_found),
                                "No matching parent constructor found for super()");
                        }
                    } else {
                        // Parent is a C++ class - call its constructor
                        try {
                            // Get the C++ class constructor function
                            auto parent_name = parent_class->get_name();
                            auto ctor_result = environment_->get(parent_name);
                            if (ctor_result && ctor_result.value().is_function()) {
                                script_value cpp_ctor = std::move(ctor_result.value());
                                // Call C++ constructor with init_args
                                auto result = cpp_ctor.as_function()(init_args);
                                if (!result) {
                                    // Constructor failed - propagate error
                                    return result.error_value();
                                }
                                script_value cpp_obj = std::move(result.value());

                                // Extract the C++ object and store it in _cpp_object field
                                if (cpp_obj.is_object()) {
                                    auto cpp_instance = cpp_obj.as<std::shared_ptr<class_instance>>();
                                    if (cpp_instance) {
                                        // Use each instance's own field ID getter to ensure consistency
                                        uint64_t src_field_id = cpp_instance->get_cpp_object_field_id();
                                        uint64_t dst_field_id = instance->get_cpp_object_field_id();
                                        if (cpp_instance->has_field(src_field_id)) {
                                            // Copy _cpp_object from parent to derived instance
                                            auto src_value = cpp_instance->get_field(src_field_id);
                                            instance->set_field(dst_field_id, src_value);
                                        }
                                    }
                                }
                            }
                        } catch (const runtime_error&) {
                            return checked_result<script_value>(make_error_code(runtime_error_code::constructor_failed),
                                "Failed to call C++ parent constructor");
                        }
                    }
                }
            } else {
                return checked_result<script_value>(make_error_code(runtime_error_code::no_constructor_found),
                    "Cannot call super() - class has no base class");
            }
        } else if (initializer.target == "this") {
            // Delegate to another constructor in the same class
            // Evaluate initializer arguments in constructor environment
            std::vector<script_value> init_args;
            init_args.reserve(initializer.arguments.size());
            
            // Temporarily switch to init environment for argument evaluation
            auto old_env = environment_;
            environment_ = init_env;

            for (const auto& arg_expr : initializer.arguments) {
                auto result = dispatch_expr(arg_expr.get());
                if (!result) {
                    // Restore environment before returning error
                    environment_ = old_env;
                    if (result.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
                        return result.error_value();
                    }
                    return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
                        "Failed to evaluate constructor initializer argument");
                }
                init_args.push_back(pop_value());
                detail::capture_initializer_argument(initializer, init_args.size() - 1, !class_def->chain_initializers_all_literal(), init_args.back(),
                                                     [&] { return detail::constructor_binding(class_def.get(), init_args.size() - 1); });
            }

            // Restore environment
            environment_ = old_env;
            
            // Find matching constructor in same class (fewest defaults used wins)
            const auto& ctor_asts = class_def->get_constructor_asts();
            // the delegation target through the one scorer (typed exact > conversion > var), else the arity match
            std::vector<std::shared_ptr<function_decl>> other_ctors;
            for (const auto& ctor_ast : ctor_asts) { if (ctor_ast != matching_ctor) { other_ctors.push_back(ctor_ast); } }
            std::shared_ptr<function_decl> target_ctor = pick_best_overload(other_ctors, init_args);
            size_t target_defaults = SIZE_MAX;
            if (!target_ctor) for (const auto& ctor_ast : ctor_asts) {
                if (ctor_ast != matching_ctor && arity_accepts(ctor_ast->parameters, init_args.size()) &&
                    ctor_ast->parameters.size() - init_args.size() < target_defaults) {
                    target_ctor = ctor_ast;
                    target_defaults = ctor_ast->parameters.size() - init_args.size();
                }
            }
            
            if (!target_ctor) {
                return detail::overload_miss(make_error_code(runtime_error_code::no_constructor_found),
                    class_def->get_type_id(), init_args.data(), init_args.size(), engine_, true);
            }
            
            // C++ delegation: the target constructor runs whole - its own initializer list (a base constructor or a further
            // delegation), the members, then its body; the delegating constructor re-runs no field initializer afterward
            auto target_members = initialize_members(class_def, definition_env, instance, this_value, target_ctor, init_args,
                                                     initializer_depth, delegation_depth + 1, nullptr);
            if (!target_members) { return target_members; }
            delegated_to_this = true;

            // Call the target constructor on this instance with method environment
            // Use definition_env as parent
            scoped_method_environment target_method_env(
                this,
                definition_env,
                this_value,
                class_def.get()
            );

            initializer_depth.reset();
            auto target_result = execute_method_ast(target_ctor, target_method_env.get(), init_args);
            if (!target_result) return target_result.error_value();
            initializer_depth.emplace(current_call_depth_);
        }
    }

    // Evaluate field initializers BEFORE executing the constructor body
    // Field initializers can access constructor parameters via init_env
    // If the iterative multi-level loop already handled parent field initializers,
    // skip recursive parent processing to avoid re-evaluating defaults.
    // Skip entirely when this ctor delegated via `: this(...)` — the target
    // already initialized the fields and re-running would clobber them (#24).
    if (!delegated_to_this &&
        (!instance->seeded_from_plan() || handled_parent_init || !class_def->chain_initializers_all_literal())) {
        // A literal-only chain's initializers ARE the defaults create_instance's plan just
        // seeded (post-conversion); the re-run only matters after a parent ctor body ran
        auto initialized = evaluate_field_initializers(instance, class_def, init_env, handled_parent_init);
        if (!initialized) { return initialized.error_value(); }
    }

    return this_value;
}

checked_result<script_value> interpreter::construct_instance(std::shared_ptr<script_class_definition> class_def,
                                                              std::shared_ptr<environment> definition_env,
                                                              const std::vector<script_value>& args) {
    // Initializers execute before call_function owns the constructor's activation.
    // Hand the same depth back before each body call rather than counting it twice.
    if (current_call_depth_ >= limits_->interpreter_call_depth_limit || detail::native_stack_low()) {
        return detail::raise_call_depth();
    }
    std::optional<detail::call_depth_guard> initializer_depth;
    initializer_depth.emplace(current_call_depth_);
    struct construction_env_restore {
        interpreter* self;
        std::shared_ptr<environment> previous_env;
        ~construction_env_restore() { self->environment_ = std::move(previous_env); }
    } env_restore{this, environment_};

    // Get all constructor ASTs
    const auto& ctor_asts = class_def->get_constructor_asts();

    // The class's constructor overloads through the one script-overload scorer (calls, methods and constructors rank alike:
    // typed exact > conversion > var; KEEP BYTE-PARALLEL with vm_backend's constructor selection)
    std::shared_ptr<function_decl> matching_ctor = pick_best_overload(ctor_asts, args);
    if (!matching_ctor) {   // no viable overload: the arity match (fewest defaults) raises its parameter's own text at the binding
        size_t fewest_defaults = SIZE_MAX;
        for (const auto& ctor_ast : ctor_asts) {
            if (arity_accepts(ctor_ast->parameters, args.size()) && ctor_ast->parameters.size() - args.size() < fewest_defaults) {
                matching_ctor = ctor_ast;
                fewest_defaults = ctor_ast->parameters.size() - args.size();
            }
        }
    }

    if (!matching_ctor) {
        return detail::overload_miss(make_error_code(runtime_error_code::no_constructor_found),
            class_def->get_type_id(), args.data(), args.size(), engine_, true);
    }

    // Create instance
    auto instance = class_def->create_instance();
    instance->construction_pending(true);   // KEEP BYTE-PARALLEL with vm_backend::construct_instance_pre_body

    // Create 'this' value using the class_def's registered name and type_id
    // This ensures we use the exact name/id that was registered (e.g., with namespace)
    // Note: is_class_instance_wrapper=true because instance is a class_instance object (script_class_instance inherits from class_instance)
    auto this_value = script_value::make_class_object(*class_def, instance, engine_);

    std::vector<script_value> captured;
    auto members = initialize_members(class_def, definition_env, instance, this_value, matching_ctor, args, initializer_depth, 0, &captured);
    if (!members) { return members; }

    // Execute the matching constructor with method environment
    // Create a method environment that provides implicit 'this' field access
    // Use definition_env as parent
    scoped_method_environment method_env(
        this,
        definition_env,
        this_value,
        class_def.get()
    );

    // Execute constructor as a method so it has access to 'this' and fields
    // The constructor implicitly returns 'this', so use that return value
    // instead of creating a new script_value (which would be a duplicate reference)
    initializer_depth.reset();
    auto result = execute_method_ast(matching_ctor, method_env.get(), captured.empty() ? args : captured);
    if (!result) { return result; }
    if (!is_unwinding_) { instance->construction_pending(false); }
    return this_value;   // a constructor call yields its instance (a bare `return;` ends the body early)
}

checked_result<script_value> interpreter::construct_default_instance(std::shared_ptr<script_class_definition> class_def,
                                                                      const std::vector<script_value>& args) {

    // a class without a constructor takes no arguments: family C's constructor text, the checker's too
    if (!args.empty()) {
        return detail::overload_miss(make_error_code(runtime_error_code::no_constructor_found),
            class_def->get_type_id(), args.data(), args.size(), engine_, true);
    }

    if (current_call_depth_ >= limits_->interpreter_call_depth_limit || detail::native_stack_low()) {
        return detail::raise_call_depth();
    }
    detail::call_depth_guard initializer_depth(current_call_depth_);
    
    // Create instance using inherited create_instance()!
    auto instance = class_def->create_instance();
    // Default constructor instance created

    // Create 'this' value for field initializer evaluation
    // Use class_def's registered name and type_id to handle namespaces correctly
    // Note: is_class_instance_wrapper=true because instance is a class_instance object (script_class_instance inherits from class_instance)
    auto this_value = script_value::make_class_object(*class_def, instance, engine_);

    // Find the root (global) environment with cycle detection
    std::unordered_set<environment*> visited;
    auto current_env = environment_;
    while (current_env && current_env->get_parent()) {
        // Cycle detection
        if (visited.count(current_env.get()) > 0) {
            // Cycle detected! Log and break
            std::cerr << "WARNING: Environment cycle detected at " << current_env.get()
                      << " (type: " << typeid(*current_env).name() << ")\n";
            std::cerr << "  Visited " << visited.size() << " environments before cycle\n";
            break;
        }
        visited.insert(current_env.get());
        current_env = current_env->get_parent();
    }
    auto global_env = current_env ? current_env : environment_;

    // Evaluate field initializers with a regular environment that has 'this'
    if (!instance->seeded_from_plan() || !class_def->chain_initializers_all_literal()) {
        auto init_env = std::make_shared<environment>(global_env, string_symbolizer_);
        init_env->define("this", this_value);
        init_env->set_access_context(class_def.get());
        auto initialized = evaluate_field_initializers(instance, class_def, init_env);
        if (!initialized) { return initialized.error_value(); }
    }

    // Default constructor object wrapped
    return this_value;
}

// RAII wrapper for method environments
scoped_method_environment::scoped_method_environment(
    interpreter* interp,
    std::shared_ptr<environment> parent,
    const script_value& this_obj,
    class_definition* access_ctx)
    : interp_(interp)
    , env_(interp->get_pooled_method_environment(parent, this_obj, access_ctx))
{
    // Define 'this' as a regular variable for compatibility
    // method_environment::get() also has special handling for 'this'
    env_->define("this", this_obj);
}

scoped_method_environment::~scoped_method_environment() {
    // Always clear immediately to release 'this' reference
    interp_->release_environment(env_, true);
}

// Determine parameter binding semantics based on C++ rules
interpreter::parameter_semantics interpreter::get_parameter_semantics(
    const parameter& param,
    const script_value& arg) const
{
    // Reference parameters always share
    if (param.is_reference) {
        return parameter_semantics::reference;
    }

    // shared_ptr<T> parameter type means reference semantics
    if (param.type && param.type->base_type == script_value_type::jai_shared_ptr_type) {
        return parameter_semantics::reference;
    }

    // shared_ptr<T> argument means preserve reference semantics
    if (arg.is_shared_ptr_type()) {
        return parameter_semantics::reference;
    }

    // Default: value semantics (clone)
    return parameter_semantics::value;
}

// Bind parameter with proper value/reference semantics
script_value interpreter::bind_parameter(
    const parameter& param,
    const script_value& arg) const
{
    auto semantics = get_parameter_semantics(param, arg);
    return (semantics == parameter_semantics::value) ? clone_for_assignment(arg) : arg;
}

// Ref-param bind against the caller's variable storage: shares an existing reference
// holder (cells alias), or boxes the value on demand (cell) when the variable predates
// its escape mark. KEEP BYTE-PARALLEL with vm_backend::bind_reference_to_storage.
checked_result<void> interpreter::bind_reference_to_storage(script_value& storage, size_t frame_index, size_t param_slot) {
    if (storage.is_reference() && !storage.get_reference_holder()) {
        return checked_result<void>(
            make_error_code(runtime_error_code::invalid_reference),
            "Reference target is null");
    }
    // Share the holder (zero alloc; cells alias the same box, element/field refs keep
    // their container/instance re-resolution), boxing on demand when the variable
    // predates its escape mark (cross-execute global, C++ define, dynamic shape)
    call_stack_[frame_index].set_local(param_slot, share_boxed_env_storage(storage, engine_));
    return {};
}

// KEEP BYTE-PARALLEL with vm_backend::bind_bare_cpp_property_ref: the member scope's
// receiver, else the env's 'this' (lambdas made in methods reach it through the chain).
// `env` = a CALLER env at a ref-arg bind (the current frame's scope is the callee's).
std::optional<checked_result<script_value>> interpreter::bind_bare_cpp_property_ref(uint64_t symbol_id, bool global_qualified,
                                                                                    environment* env) {
    if (global_qualified) {
        return std::nullopt;
    }
    script_value* this_ptr = !env && member_scope_.state == detail::member_scope_state::instance && member_scope_.this_storage
        ? member_scope_.this_storage
        : (env ? env : environment_.get())->get_value_ptr(string_symbolizer_->get_this_id());
    if (!this_ptr || !this_ptr->is_object() || this_ptr->is_null()) {
        return std::nullopt;
    }
    auto instance = this_ptr->get_class_instance();
    if (!instance || !detail::cpp_property_getter_id(instance.get(), symbol_id)) {
        return std::nullopt;
    }
    return detail::bind_cpp_property_ref(*this_ptr, symbol_id, engine_, string_symbolizer_);
}

// Stateless ref-param binding straight from the call-site arg expression. Kept out of
// call_function so its locals stay off the per-recursion-level native frame (Debug
// stack ceiling). KEEP BYTE-PARALLEL with vm_backend::bind_parameters' ref branch.
checked_result<void> interpreter::bind_reference_parameter(const parameter& param, size_t frame_index,
                                                           const expression* argExpr,
                                                           const std::shared_ptr<environment>& caller_env,
                                                           const script_value* evaluated_arg,
                                                           unit_stamp caller_unit,
                                                           const script_defined_function* callee, size_t param_index) {
    // the argument is the caller's code: its identifier resolves under the caller's unit (docs/global_shadowing.md); the
    // callee's unit, entered by the call ahead of the binding, resumes when the binding returns
    unit_sync_scope unitSync{ this };   // the callee's unit again once the argument is bound
    enter_unit(caller_unit);
    call_frame* caller_locals = frame_index >= 1 ? &call_stack_[frame_index - 1] : nullptr;

    // C::s and ns::x are lvalues: the reference binds their storage (KEEP BYTE-PARALLEL with vm_backend::bind_parameters)
    if (argExpr && argExpr->get_type() == node_type::member_expr && caller_env) {
        if (auto cell = detail::static_member_lvalue(*engine_, static_cast<const member_expr*>(argExpr), *caller_env, *string_symbolizer_,
                                                     caller_env->find_access_context())) {
            if (!*cell) { return cell->error_value(); }
            return bind_reference_to_storage(*cell->value(), frame_index, param.slot_index);
        }
    }

    if (argExpr && argExpr->get_type() == node_type::identifier_expr) {
        auto* identExpr = static_cast<const identifier_expr*>(argExpr);
        uint64_t symbol_id = identExpr->symbol_id;
        if (symbol_id == UINT64_MAX) {
            symbol_id = string_symbolizer_->intern(identExpr->name);
            const_cast<identifier_expr*>(identExpr)->symbol_id = symbol_id;
        }

        // ::name ref args bind against the GLOBAL env only
        if (identExpr->global_qualified) {
            script_value* g = get_global_environment()->get_value_ptr(symbol_id);
            if (!g) {
                return checked_result<void>(
                    make_error_code(runtime_error_code::undefined_variable),
                    environment_->undefined_text(symbol_id, "Cannot take reference of undefined variable"), symbol_id
                );
            }
            return bind_reference_to_storage(*g, frame_index, param.slot_index);
        }

        // Caller frame-slot local (env lookup can't see slot locals and would walk to
        // an unrelated same-named outer variable)
        if (identExpr->slot_index != SIZE_MAX && caller_locals) {
            if (script_value* slotPtr = caller_locals->get_local(identExpr->slot_index)) {
                return bind_reference_to_storage(*slotPtr, frame_index, param.slot_index);
            }
        }

        // Caller member scope: a field/static outranks a same-named global as a ref
        // target (bound as raw storage, exactly like the old post-walk fallback bound
        // the same pointer when no global collided).
        const auto caller_state = caller_locals
            ? static_cast<detail::member_scope_state>(caller_locals->scope_state)
            : detail::member_scope_state::none;
        if (caller_state == detail::member_scope_state::instance ||
            caller_state == detail::member_scope_state::statics) {
            if (script_value* own = caller_env
                    ? detail::own_scope_lookup(caller_env.get(), caller_locals->scope_floor, symbol_id)
                    : nullptr) {
                return bind_reference_to_storage(*own, frame_index, param.slot_index);
            }
            auto member = detail::probe_member_scope(caller_state, caller_locals->scope_this,
                                                     caller_locals->scope_static, symbol_id,
                                                     /*want_methods*/ false);
            if (member) {
                JAISCRIPT_TRY(detail::bare_member_access(member, symbol_id,
                                                         caller_env ? caller_env->find_access_context() : nullptr));
            }
            if (member.value_ptr) {
                return bind_reference_to_storage(*member.value_ptr, frame_index, param.slot_index);
            }
            // A C++ property binds the member itself / its accessors (reference_model.md)
            if (member.what == detail::member_probe_hit::kind::cpp_property) {
                auto minted = detail::bind_cpp_property_ref(*caller_locals->scope_this, symbol_id, engine_, string_symbolizer_);
                if (!minted) {
                    return minted.error_value();
                }
                call_stack_[frame_index].set_local(param.slot_index, std::move(minted.value()));
                return {};
            }
            // A member METHOD shadows (never a global)
            if (detail::member_scope_has_method(caller_state, caller_locals->scope_this,
                                                caller_locals->scope_static, symbol_id)) {
                return checked_result<void>(
                    make_error_code(runtime_error_code::undefined_variable),
                    "Cannot take reference of undefined variable"
                );
            }
            script_value* beyond = caller_locals->scope_floor
                ? caller_locals->scope_floor->get_value_ptr(symbol_id)
                : (caller_env ? caller_env->get_value_ptr(symbol_id) : nullptr);
            if (!beyond) {
                return checked_result<void>(
                    make_error_code(runtime_error_code::undefined_variable),
                    environment_->undefined_text(symbol_id, "Cannot take reference of undefined variable"), symbol_id
                );
            }
            return bind_reference_to_storage(*beyond, frame_index, param.slot_index);
        }

        script_value* argPtr = caller_env ? caller_env->get_value_ptr(symbol_id) : nullptr;
        if (!argPtr) {
            // A hybrid's C++ property reached through the env's 'this' (lambdas made in
            // methods); KEEP BYTE-PARALLEL with vm_backend::bind_parameters
            if (auto minted = caller_env ? bind_bare_cpp_property_ref(symbol_id, false, caller_env.get()) : std::nullopt) {
                if (!*minted) {
                    return minted->error_value();
                }
                call_stack_[frame_index].set_local(param.slot_index, std::move(minted->value()));
                return {};
            }
            return checked_result<void>(
                make_error_code(runtime_error_code::undefined_variable),
                environment_->undefined_text(symbol_id, "Cannot take reference of undefined variable"), symbol_id
            );
        }
        return bind_reference_to_storage(*argPtr, frame_index, param.slot_index);
    }

    // Field/subscript/chain lvalue argument: resolve through the shared helper into
    // an owner-pinned reference (Tier 1)
    if (argExpr && detail::is_ref_bindable_lvalue(argExpr)) {
        auto resolved = detail::resolve_ref_lvalue(argExpr, detail::caller_frame_view{caller_locals},
                                                   caller_env.get(), engine_, string_symbolizer_);
        if (resolved) {
            call_stack_[frame_index].set_local(param.slot_index, std::move(resolved.value()));
            return {};
        }
        // The kernel's generic non-lvalue verdict falls through to Tier 2 (identifier-
        // keyed map entries classify Tier 1 but only evaluation can resolve them);
        // access enforcement and other specific errors propagate
        if (resolved.error() != make_error_code(runtime_error_code::invalid_reference)) {
            return resolved.error_value();
        }
    }

    // Tier 2 (general lvalues; Dev ruling 2026-07-12): the argument already evaluated
    // in normal left-to-right order, and subscript/map reads over lvalue bases mint
    // owner-pinned references with full index generality - computed indices
    // (grid[y+1][x]), member-expr indices (arr[o.idx]), map keys (m["k"]). A reference
    // VALUE is an lvalue: share its holder, exactly like the external-invocation path
    // always has. Nothing runs at the bind point, so the fast-path constraint holds.
    // (KEEP BYTE-PARALLEL with vm_backend::bind_parameters' ref branch)
    if (evaluated_arg && evaluated_arg->is_reference() && evaluated_arg->get_reference_holder()) {
        call_stack_[frame_index].set_local(param.slot_index, script_value(*evaluated_arg));
        return {};
    }
    // a temporary binds a parameter a declared function never writes through (detail/ref_param_writes.hpp); a lambda keeps
    // needing a variable
    if (evaluated_arg && callee && callee->name != "<lambda>" &&
        detail::ref_parameter_takes_temporary(*engine_, callee->parameters(), callee->body.get(), param_index, !parallel_worker_)) {
        call_stack_[frame_index].set_local(param.slot_index, script_value::make_cell_reference(script_value(evaluated_arg->deref()), engine_));
        return {};
    }

    return checked_result<void>(
        make_error_code(runtime_error_code::invalid_reference),
        "Cannot pass non-lvalue to reference parameter"
    );
}

// Function call implementation - returns checked_result for consistent error handling
// Uses stack-based call frames for fast parameter access instead of hash map environments
// Debugger: collect (slot, name) for every local a function body declares. Mirrors the shape
// of collect_outer_slots_stmt; stops at nested function/lambda/class bodies (separate frames)
// and expression statements (no locals). Runs only when a debugger asks for locals at a stop.
static void collect_frame_slot_names(const statement* s,
                                     std::vector<std::pair<size_t, std::string_view>>& out) {
    if (!s) return;
    switch (s->get_type()) {
    case node_type::variable_decl: {
        auto* vd = static_cast<const variable_decl*>(s);
        if (vd->slot_index != SIZE_MAX) out.emplace_back(vd->slot_index, vd->name);
        break;
    }
    case node_type::statement_decl:
        collect_frame_slot_names(static_cast<const statement_decl*>(s)->statement.get(), out);
        break;
    case node_type::block_stmt:
        for (const auto& d : static_cast<const block_stmt*>(s)->declarations)
            collect_frame_slot_names(d.get(), out);
        break;
    case node_type::if_stmt: {
        auto* is = static_cast<const if_stmt*>(s);
        collect_frame_slot_names(is->then_statement.get(), out);
        if (is->else_statement) collect_frame_slot_names(is->else_statement.get(), out);
        break;
    }
    case node_type::while_stmt:
        collect_frame_slot_names(static_cast<const while_stmt*>(s)->body.get(), out);
        break;
    case node_type::for_stmt: {
        auto* fs = static_cast<const for_stmt*>(s);
        if (fs->initializer) collect_frame_slot_names(fs->initializer.get(), out);
        collect_frame_slot_names(fs->body.get(), out);
        break;
    }
    case node_type::range_for_stmt: {
        auto* rf = static_cast<const range_for_stmt*>(s);
        if (rf->variable_slot_index != SIZE_MAX)
            out.emplace_back(rf->variable_slot_index, rf->variable_name);
        collect_frame_slot_names(rf->body.get(), out);
        break;
    }
    case node_type::parallel_for_stmt:
        // Body slots belong to the WORKER frame's own numbering - naming them here
        // would alias this frame's slots
        break;
    case node_type::switch_stmt: {
        auto* sw = static_cast<const switch_stmt*>(s);
        for (const auto& c : sw->cases)
            for (const auto& st : c->body) collect_frame_slot_names(st.get(), out);
        if (sw->default_case)
            for (const auto& st : sw->default_case->body) collect_frame_slot_names(st.get(), out);
        break;
    }
    case node_type::try_stmt: {
        auto* tr = static_cast<const try_stmt*>(s);
        if (tr->try_block) collect_frame_slot_names(tr->try_block.get(), out);
        if (tr->catch_slot_index != SIZE_MAX) out.emplace_back(tr->catch_slot_index, tr->catch_var);
        if (tr->catch_block) collect_frame_slot_names(tr->catch_block.get(), out);
        break;
    }
    default: break;   // expressions, nested functions/lambdas/classes: not this frame's locals
    }
}

std::vector<std::pair<std::string, script_value>> interpreter::get_current_frame_locals() const {
    std::vector<std::pair<std::string, script_value>> out;
    if (call_stack_.empty()) return out;                 // global scope: caller falls back to env
    const call_frame& frame = call_stack_.back();
    const script_defined_function* fn = frame.function;
    if (!fn) return out;

    // slot -> name for this frame: parameters, then every declaration reachable in the body.
    std::vector<std::pair<size_t, std::string_view>> slot_names;
    for (const auto& p : fn->parameters())
        if (p.slot_index != SIZE_MAX) slot_names.emplace_back(p.slot_index, p.name);
    collect_frame_slot_names(fn->body.get(), slot_names);

    // Emit in slot order; only slots that are live (assigned/reached) in this frame show up,
    // so an unentered branch's not-yet-declared locals are naturally excluded.
    std::sort(slot_names.begin(), slot_names.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    size_t last = SIZE_MAX;
    for (const auto& [slot, name] : slot_names) {
        if (slot == last) continue;                      // dedupe (slots are unique anyway)
        last = slot;
        const script_value* v = frame.get_local(slot);
        if (!v) continue;                                // reserved capacity only: not live yet
        out.emplace_back(std::string(name), *v);
    }
    detail::fold_lowered_locals(out, engine_);
    return out;
}

checked_result<script_value> interpreter::call_function(const script_defined_function& function, const std::vector<script_value>& args,
                                                        const method_call_override* method_ctx) {
    // Check recursion depth limit FIRST
    if (current_call_depth_ >= limits_->interpreter_call_depth_limit) {
        return detail::raise_call_depth();
    }
    // Wide native frames (constructor chains especially) can exhaust the native stack
    // long before the depth cap - fail catchably instead of dying on 0xC00000FD.
    // Same message as the depth cap: the vm never trips this on script recursion
    // (in-loop frames), so a distinct text here breaks backend error parity.
    if (detail::native_stack_low()) [[unlikely]] {
        return detail::raise_call_depth();
    }

    if (execution_limit_exhausted()) [[unlikely]] {
        return execution_limit_failure();
    }

    // RAII guard for call depth tracking - ensures decrement even on early return
    detail::call_depth_guard depth_guard(current_call_depth_);
    // A callee's own statements decide whether their assignment expressions are consumed; the
    // caller's statement position (an expression statement clears the flag) must not leak in
    struct unconsumed_root_scope { const expression*& root; const expression* saved; ~unconsumed_root_scope() { root = saved; } } root_scope{ unconsumed_root_, unconsumed_root_ };
    unconsumed_root_ = nullptr;

    // Consume the pending call-site context (set by visit_call around the invoke): ref
    // params bind against the caller's variables through it. Consumed exactly once -
    // nested calls set their own; external (C++) invocations arrive with none.
    const detail::call_site_context* call_ctx = pending_call_ctx_;
    pending_call_ctx_ = nullptr;

    // Parse-time argument proofs belong to the ONE callee the site resolved to: body
    // identity is the callee check (docs/type_proofs_design.md I-C1)
    const uint8_t* arg_proofs = nullptr;
    if (call_ctx && call_ctx->site && call_ctx->site->proven_callee_body == function.body.get() &&
        call_ctx->site->static_arg_proofs.size() == function.parameters().size() &&
        args.size() == function.parameters().size()) {
        arg_proofs = call_ctx->site->static_arg_proofs.data();
    }

    if (args.size() != function.parameters().size() && !arity_accepts(function.parameters(), args.size())) {
        return detail::arity_error(function.name, function.parameters().size(), args.size(), arity_count_ids_, *string_symbolizer_);
    }

    // ============================================================
    // CALL FRAME OPTIMIZATION: Push a call frame for parameters
    // ============================================================
    // Parameters are stored in the call frame for O(n) lookup where n is small.
    // This avoids hash map overhead for the most frequently accessed variables.
    // The environment is still used for:
    // - 'this' object in methods
    // - Static class fields
    // - Closure captures
    // - Variables defined in the function body (int x = 0; etc.)

    // Push call frame and remember its index (NOT a reference!)
    // IMPORTANT: Using index instead of reference to avoid invalidation when
    // parameter conversion calls constructors that push more frames onto call_stack_
    call_stack_.emplace_back();
    const size_t frame_index = call_stack_.size() - 1;
    call_stack_[frame_index].function_name = function.name;
    call_stack_[frame_index].function = &function;   // the return site + debugger read it

    // Pre-allocate locals for all slots (params + local variables)
    // Slot indices are assigned by the parser at parse time
    call_stack_[frame_index].reserve_locals(function.local_count);

    // Set up closure environment for non-local lookups
    auto previousEnv = environment_;
    const detail::member_scope_ctx previousScope = member_scope_;
    unit_sync_scope unitSync{ this };   // the caller's unit again once this frame is gone
    const unit_stamp caller_unit = frame_index >= 1 && call_stack_[frame_index - 1].function ? call_stack_[frame_index - 1].function->unit : string_symbolizer_->current_unit();   // a by-reference argument resolves under it
    enter_unit(function.unit);
    member_scope_ = {};

    // Determine closure environment and method context; each branch stamps the
    // frame's member scope (detail/member_scope.hpp) - the C++ shadowing ladder
    // needs the callee KIND, which only these branches know (a plain function's
    // env parents on the CALLER's chain, so the chain alone cannot distinguish
    // lexical member scope from dynamic parenting).
    // NOTE: Re-access call_stack_[frame_index] each time instead of caching reference
    if (method_ctx) {
        // Direct method dispatch: same net effect as the closure_env->is_method_env()
        // branch below (this in the frame, method scope env on definition_env), minus
        // the carrier method env and its per-call define("this")
        call_stack_[frame_index].set_this(*method_ctx->this_obj);
        call_stack_[frame_index].closure_env = *method_ctx->definition_env;
        environment_ = get_pooled_method_environment(*method_ctx->definition_env, *method_ctx->this_obj,
                                                     method_ctx->access_ctx);
        member_scope_.state = detail::member_scope_state::instance;
        member_scope_.this_storage = call_stack_[frame_index].this_object_ptr.get();
        member_scope_.floor = method_ctx->definition_env->get();
    } else if (function.closure_env) {
        if (function.closure_env->is_method_env()) {
            // Method call - store 'this' in the call frame
            auto this_obj = function.closure_env->get_this_object();
            call_stack_[frame_index].set_this(this_obj);  // Uses set_this which sets is_method = true
            call_stack_[frame_index].closure_env = function.closure_env->get_parent();
            // Set environment to a method environment for 'this' field lookups in body
            environment_ = get_pooled_method_environment(function.closure_env->get_parent(), this_obj,
                                                         function.closure_env->get_access_context());
            member_scope_.state = detail::member_scope_state::instance;
            member_scope_.this_storage = call_stack_[frame_index].this_object_ptr.get();
            member_scope_.floor = function.closure_env->parent_raw();
        } else if (function.closure_env->is_static_method_env()) {
            // Static method - store class definition in frame
            call_stack_[frame_index].static_class_def = function.closure_env->get_class_definition();
            call_stack_[frame_index].is_static_method = true;
            call_stack_[frame_index].closure_env = function.closure_env->get_parent();
            // Create static method environment for static field access
            environment_ = std::make_shared<environment>(
                function.closure_env->get_parent(),
                env_symbolizer_,
                function.closure_env->get_class_definition()
            );
            member_scope_.state = detail::member_scope_state::statics;
            member_scope_.statics = call_stack_[frame_index].static_class_def.get();
            member_scope_.floor = function.closure_env->parent_raw();
        } else {
            // Regular closure
            call_stack_[frame_index].closure_env = function.closure_env;
            environment_ = get_pooled_environment(function.closure_env);
            // A closure created inside a method sees that method's class scope
            // (lexically, through its captured chain) between its own scopes and
            // the globals - the boundary env's OWN storage (the enclosing call's
            // escaped locals) stays on the far side of the members.
            if (environment* boundary = detail::find_member_boundary(function.closure_env.get())) {
                if (boundary->is_method_env()) {
                    if (script_value* this_storage = boundary->method_this_storage()) {
                        member_scope_.state = detail::member_scope_state::instance;
                        member_scope_.this_storage = this_storage;
                        member_scope_.floor = (boundary == function.closure_env.get())
                                                  ? boundary->parent_raw() : boundary;
                    }
                } else if (auto boundary_class = boundary->get_class_definition()) {
                    member_scope_.state = detail::member_scope_state::statics;
                    member_scope_.statics = boundary_class.get();
                    member_scope_.floor = (boundary == function.closure_env.get())
                                              ? boundary->parent_raw() : boundary;
                }
            }
        }
    } else {
        // Regular function: the caller's chain past its block scopes (C++: a callee never sees a block's declarations)
        auto parent = environment::call_parent(previousEnv, function.block_defined);
        call_stack_[frame_index].closure_env = parent;
        environment_ = get_pooled_environment(std::move(parent));
    }

    // Mirror the member scope onto the frame: the shared ref-lvalue kernel
    // (detail/ref_lvalue.hpp) reads it through caller_frame_view, and the vm reads
    // the same call_frame fields (its frames compute them lazily instead).
    call_stack_[frame_index].scope_state = static_cast<uint8_t>(member_scope_.state);
    call_stack_[frame_index].scope_floor = member_scope_.floor;
    call_stack_[frame_index].scope_this = member_scope_.this_storage;
    call_stack_[frame_index].scope_static = member_scope_.statics;

    // Store previous return state
    bool previousHasReturn = hasReturnValue_;
    std::optional<script_value> previousReturn = returnValue_;
    hasReturnValue_ = false;

    // Helper lambda to cleanup and restore state
    auto cleanup = [&](bool clear_this = true) {
        // capture before pop_back: deeper frames are still present
        if (is_unwinding_ && !trace_captured_) capture_stack_trace();
        // Move the frame out and destroy it LAST: releasing its locals can fire script
        // destructors that reenter call_function and grow call_stack_, which must never
        // happen mid-pop_back (vector reentrancy UB). Mirrors the VM's stable-record
        // teardown: destructors fire after the caller env/state is restored.
        call_frame dying_frame = std::move(call_stack_.back());
        call_stack_.pop_back();

        auto function_env = environment_;
        if (clear_this) {
            // Only the CALLEE's own method scope env is cleared (pooled-env hygiene).
            // A plain callee's env may be parented on the CALLER's method env - clearing
            // through the parent poisoned the caller's this-binding (demoreel finding 1).
            if (function_env->is_method_env()) {
                function_env->clear_this_reference();
            }
        }
        environment_ = previousEnv;
        member_scope_ = previousScope;
        release_environment(function_env, false);
        hasReturnValue_ = previousHasReturn;
        returnValue_ = previousReturn;
    };

    // ============================================================
    // BIND PARAMETERS TO CALL FRAME
    // ============================================================
    // Parameters go into the call frame for fast lookup.
    // Reference parameters still need special handling.

    std::vector<script_value> held_args;
    const std::vector<script_value>* argv = &args;
    // A native exception unwinding through this activation (a C++ callable, a removed-element reference, a bare rethrow
    // with nothing to rethrow) pops the frame first and converts at the caller's call boundary (docs/EXCEPTION_DESIGN.md)
    size_t last_body_idx = 0;
    try {
    for (size_t i = 0; i < function.parameters().size(); ++i) {
        const auto& param = function.parameters()[i];

        if (i >= args.size()) {
            // Must have a default value (validated by argument count check above)
            if (param.default_value) {
                auto default_result = dispatch_expr(param.default_value.get());
                if (!default_result) {
                    cleanup();
                    return default_result.error_value();
                }
                script_value default_val = pop_value();
                if (param.ref_escaping && !default_val.is_reference()) {
                    default_val = script_value::make_cell_reference(std::move(default_val), engine_);
                }
                call_stack_[frame_index].set_local(param.slot_index, std::move(default_val));
                continue;
            }
        }

        const auto& arg = detail::argument_for_binding((*argv)[i], detail::parameter_binding(param));

        // Use pre-cached symbol ID (parameter binding optimization)
        // Symbol IDs are cached at function definition time in visit_function_decl
        if (param.is_reference) {
            if (call_ctx && i < call_ctx->arg_exprs->size()) {
                auto bound = bind_reference_parameter(param, frame_index,
                                                      (*call_ctx->arg_exprs)[i].get(), previousEnv, &arg, caller_unit, &function, i);
                if (!bound) {
                    cleanup();
                    return bound.error_value();
                }
            } else {
                // No metadata: external (C++) invocation. Object values are handles, so a
                // shallow copy aliases the same underlying object - reference semantics hold.
                // A reference VALUE is already an lvalue: copying it shares the holder, so
                // the parameter aliases the same storage (parallel_for binds each owned
                // element this way; C++ callers can pass make_*_reference values too).
                auto arg_type = arg.current_type();
                if (arg.is_reference()) {
                    call_stack_[frame_index].set_local(param.slot_index, script_value(arg));
                } else if (arg_type == script_value_type::jai_object_type ||
                    arg_type == script_value_type::jai_shared_ptr_type) {
                    call_stack_[frame_index].set_local(param.slot_index, script_value(arg));
                } else if (function.name != "<lambda>" &&
                           detail::ref_parameter_takes_temporary(*engine_, function.parameters(), function.body.get(), i, !parallel_worker_)) {
                    call_stack_[frame_index].set_local(param.slot_index, script_value::make_cell_reference(script_value(arg), engine_));
                } else {
                    cleanup();
                    return checked_result<script_value>(
                        make_error_code(runtime_error_code::invalid_reference),
                        "Cannot pass non-lvalue to reference parameter"
                    );
                }
            }
        } else {
            // Escape-marked value params box into a cell so downstream ref binds share it
            auto boxed_param = [&](script_value&& v) -> script_value {
                if (param.ref_escaping && !v.is_reference()) {
                    return script_value::make_cell_reference(std::move(v), engine_);
                }
                return std::move(v);
            };

            // Exact-class match: a pointer-identical interned type_info (with the
            // non-empty name the identity branch requires) replicates try_convert's
            // exact-name branch (which returns the derefed arg) without the conversion
            // machinery; should_share is false for objects, so the clone matches the
            // full path
            {
                const script_value& derefed = arg.deref();
                if (param.type && param.type->base_type == script_value_type::jai_object_type &&
                    derefed.storage_type() == script_value_type::jai_object_type &&
                    derefed.get_type_info().get() == param.type.get() && !param.type->type_name.empty()) {
                    // Read-only parameter elision: a proven parameter shares the argument's handle
                    // (KEEP BYTE-PARALLEL with vm_backend::bind_parameters)
                    if (!param.ref_escaping && detail::parameter_alias_allows(derefed) && detail::readonly_parameter_alias(*engine_, function, i, !parallel_worker_)) {
                        if (!parallel_worker_) { engine_->note_parameter_alias_bind(); }
                        call_stack_[frame_index].set_local(param.slot_index, script_value(derefed));
                        continue;
                    }
                    call_stack_[frame_index].set_local(param.slot_index, boxed_param(derefed.clone()));
                    continue;
                }
            }

            // Non-reference parameter - try to convert the argument to the parameter type if needed
            // (cleanup before propagating: an early return here would leak the callee frame/env)
            script_value converted_arg = make_value();
            const bool untyped_param = !param.type || param.type->base_type == script_value_type::jai_any_type;
            bool proven_identity = arg_proofs && arg_proofs[i] == proof_same_type;
            if (proven_identity && param.type) {
                const script_value& probe = arg.deref();   // try_convert_for_parameter's own deref order
                proven_identity = untyped_param || detail::same_type_payload(probe, param.type.get());
            }
            if (proven_identity) {
                // try_convert_for_parameter's identity returns: the arg itself, the derefed arg
                // for exact-name objects (I-C1)
                converted_arg = (!untyped_param && param.type->base_type == script_value_type::jai_object_type)
                                ? script_value(arg.deref()) : script_value(arg);
            } else {
                if (argv == &args && i + 1 < args.size() && detail::binding_may_run_code(arg, param.type.get()) &&
                    detail::later_capture_candidate(i + 1, args.size(), [&](size_t j) -> const script_value& { return args[j]; })) [[unlikely]] {
                    held_args = args;
                    for (size_t j = i + 1; j < held_args.size(); ++j) {
                        detail::capture_argument(held_args[j], detail::transient_read_marker::arg_capture_full, detail::parameters_binding(function.parameters(), j));
                    }
                    argv = &held_args;
                }
                auto convert_result = try_convert_for_parameter(arg, param.type,
                    detail::slot_site{detail::slot_kind::parameter, param.name, function.name});
                if (!convert_result) {
                    cleanup();
                    return convert_result.error_value();
                }
                converted_arg = std::move(convert_result.value());
            }

            // Decide between value semantics (clone) or reference semantics (share).
            // Element-read args arrive as reference wrappers - probe the referent
            // (deref() returns *this for plain values) so the shared_ptr marker is
            // visible (KEEP BYTE-PARALLEL with vm_backend::bind_parameters)
            bool should_share = false;
            const script_value& shared_probe = converted_arg.deref();

            // Check if parameter type is shared_ptr<T>
            if (param.type && param.type->base_type == script_value_type::jai_shared_ptr_type) {
                should_share = true;
            }

            // Check if argument is shared_ptr<T> (preserve reference semantics)
            if (shared_probe.get_type_info() && shared_probe.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
                should_share = true;
            }

            // Add parameter to call frame using slot index (O(1) access)
            // IMPORTANT: Use call_stack_[frame_index] not a cached reference (vector may have reallocated)
            if (should_share) {
                // Shallow copy - share ownership (reference semantics)
                call_stack_[frame_index].set_local(param.slot_index, boxed_param(script_value(shared_probe)));
            } else {
                // Deep copy - value semantics (C++-like default); a proven read-only parameter
                // keeps the converted value as is (an identity conversion shares the argument,
                // a converting one is already a fresh value - neither needs the clone)
                const bool heavy = shared_probe.is_object() || shared_probe.is_array() || shared_probe.is_map();
                if (heavy && !param.ref_escaping && detail::parameter_alias_allows(shared_probe) && detail::readonly_parameter_alias(*engine_, function, i, !parallel_worker_)) {
                    if (!parallel_worker_) { engine_->note_parameter_alias_bind(); }
                    call_stack_[frame_index].set_local(param.slot_index, script_value(shared_probe));
                    continue;
                }
                call_stack_[frame_index].set_local(param.slot_index, boxed_param(converted_arg.clone()));
            }
        }
    }
    // A T& parameter binds a T, judged on the bound lvalue so a non-lvalue argument keeps its own text (KEEP BYTE-PARALLEL with
    // vm_backend::bind_parameters)
    for (size_t i = 0; i < function.parameters().size() && i < args.size(); ++i) {
        const auto& param = function.parameters()[i];
        if (!param.is_reference || !param.type) { continue; }
        const script_value* bound = call_stack_[frame_index].get_local(param.slot_index);
        if (bound && !detail::reference_binding_admits(param.type.get(), *bound, engine_)) {
            auto rejection = detail::typed_slot_rejection(bound->deref(), *param.type, detail::slot_site{detail::slot_kind::parameter, param.name, function.name}, engine_);
            cleanup();
            return rejection;
        }
    }

    // Execute function body without creating another environment
    // (since we already created one for the function call)
    // Track which declaration index we're at (needed for coroutine yield)
    for (size_t body_idx = 0; body_idx < function.body->declarations.size(); ++body_idx) {
        last_body_idx = body_idx;
        const auto& decl = function.body->declarations[body_idx];
        auto result = dispatch_decl(decl.get());

        // Check for error codes - propagate errors
        if (!result && result.error() != std::error_code()) {
            if (!trace_captured_) capture_stack_trace();
            cleanup();
            // Propagate the error with static message - nice formatting at API boundary
            return result.error_value();
        }

        // Check if we hit a return statement, yield, or a script throw and break early.
        // Without the is_unwinding_ check, statements after a top-level `throw` in the
        // function body would still execute (blocks check the flag; this loop must too).
        if (hasReturnValue_ || hasYieldRequest_ || is_unwinding_) {
            break;
        }
    }
    } catch (...) {
        if (call_stack_.size() > frame_index) {
            while (call_stack_.size() > frame_index + 1) { call_stack_.pop_back(); }   // frames an unwound callee left behind
            cleanup();
        }
        throw;
    }

    // Get return value
    script_value result = make_value();

    // If yielding, save state for coroutine resume instead of cleaning up. The flag is this
    // frame's suspension only when the body running is the coroutine's own (a callee can no
    // longer be entered with it set - the yield unwinds its expression first).
    if (hasYieldRequest_ && active_coroutine_ &&
        active_coroutine_->get_function() && active_coroutine_->get_function()->body == function.body) {
        {
            result = active_coroutine_->last_value();
            auto& coro_state = coroutine_state(*active_coroutine_);
            // Record where to resume in the function body.
            // If inner constructs pushed continuations, re-enter this statement.
            // If no inner continuations, the yield was a direct child, skip it.
            size_t resume_idx = coro_state.has_continuations() ? last_body_idx : last_body_idx + 1;
            coro_state.push_continuation(function.body.get(), resume_idx);
            // Save interpreter state into the coroutine for later resume
            // The visit_* methods have already pushed continuations onto the coroutine
            // and left the environment chain intact (no releases on yield path).
            // Only the coroutine's OWN frames move into the suspended state - a resume
            // made inside a running script function must leave the caller's frames on
            // the live stack, or the rest of the caller's body executes frameless.
            coro_state.saved_environment = environment_;
            coro_state.saved_call_stack.clear();
            coro_state.saved_call_stack.reserve(call_stack_.size() - frame_index);
            for (size_t fi = frame_index; fi < call_stack_.size(); ++fi) {
                coro_state.saved_call_stack.push_back(std::move(call_stack_[fi]));
            }
            coro_state.saved_return_value = std::move(returnValue_);
            coro_state.saved_has_return = hasReturnValue_;
            coro_state.saved_member_scope = member_scope_;
        }
        call_stack_.erase(call_stack_.begin() + frame_index, call_stack_.end());
        // Restore caller's environment WITHOUT releasing
        // (the coroutine now owns its environment chain and call frames)
        environment_ = previousEnv;
        member_scope_ = previousScope;
        hasReturnValue_ = previousHasReturn;
        returnValue_ = previousReturn;
        return result;
    }

    // The return site (visit_return_stmt) validated a reference return / materialized the
    // by-value result (own slot moved, lvalue reads copied) and ran the typed conversion
    // inside the callee's try regions; a stray reference still flattens BEFORE cleanup
    // destroys the call frame
    if (hasReturnValue_ && !is_unwinding_) {
        if (function.return_type && function.return_type->base_type == script_value_type::jai_reference_type) {
            result = std::move(returnValue_.value());
        } else if (returnValue_.value().is_reference()) {
            // Copy the target - can't move since target might be in outer scope
            result = returnValue_.value().deref();
        } else {
            result = std::move(returnValue_.value());
        }
    } else {
        // Check if this is a constructor (method with no explicit return)
        // Constructors implicitly return 'this'
        if (active_coroutine_ && active_coroutine_->get_function() &&
            active_coroutine_->get_function()->body == function.body) {
            // Coroutine body activation ran off the end: completes with null (vm
            // run_fiber parity) - the implicit-'this' method rule must not apply.
            result = make_value();
        } else if (call_stack_[frame_index].is_method) {
            result = call_stack_[frame_index].get_this();
        } else {
            // Check environment for method context (for closures inside methods)
            auto function_env = environment_;
            if (function_env->is_method_env()) {
                result = function_env->get_this_object();
            } else if (function_env->get_parent() && function_env->get_parent()->is_method_env()) {
                result = function_env->get_parent()->get_this_object();
            } else {
                // Regular function with no return statement returns null
                result = make_value();
            }
        }
    }

    // Cleanup and return success
    cleanup();
    return result;
}

bool interpreter::can_convert_to_type(const script_value& source, type_info_ptr target_type) const {
    if (!target_type) return true;  // No type specified = any type accepted

    auto source_type = source.type();
    auto target_base_type = target_type->base_type;

    // Any type accepts anything
    if (target_base_type == script_value_type::jai_any_type) return true;

    // Exact match - no conversion needed
    if (source_type == target_base_type) {
        // For objects, also check the type name
        if (source_type == script_value_type::jai_object_type) {
            // First, try to get the type name from type_info
            auto source_type_info = source.get_type_info();
            if (source_type_info && !source_type_info->type_name.empty() &&
                source_type_info->type_name == target_type->type_name) {
                return true;
            }
            // Also check the resolved class name (script classes, cpp_bound references)
            auto resolved = resolve_member_target(source);
            if (resolved && resolved.class_name() == target_type->type_name) {
                return true;
            }
            // Different object types - might still be convertible via constructor
        } else {
            return true;  // Non-object types match
        }
    }

    // shared_ptr<T> handling - storage is object_holder but type_info marks as shared_ptr
    auto source_type_info = source.get_type_info();
    if (source_type_info && source_type_info->base_type == script_value_type::jai_shared_ptr_type) {
        // shared_ptr<T> -> shared_ptr<T>
        if (target_base_type == script_value_type::jai_shared_ptr_type) {
            if (!source_type_info->type_name.empty() && source_type_info->type_name == target_type->type_name) {
                return true;  // Inner types match
            }
            auto resolved = resolve_member_target(source);
            if (resolved && resolved.class_name() == target_type->type_name) {
                return true;
            }
        }
        // shared_ptr<T> -> T
        if (target_base_type == script_value_type::jai_object_type) {
            if (!source_type_info->type_name.empty() && source_type_info->type_name == target_type->type_name) {
                return true;
            }
            auto resolved = resolve_member_target(source);
            if (resolved && resolved.class_name() == target_type->type_name) {
                return true;
            }
        }
    }

    // For object target types, check if constructor conversion is available
    if (target_base_type == script_value_type::jai_object_type && !target_type->type_name.empty()) {
        // Look up the class definition
        auto eng = engine_;
        if (eng) {
            auto class_def = eng->get_class_definition(target_type->type_name);
            if (class_def) {
                // Check if there's a constructor that takes 1 argument
                // For script classes, check constructor ASTs
                auto script_class = std::dynamic_pointer_cast<script_class_definition>(class_def);
                if (script_class) {
                    const auto& ctor_asts = script_class->get_constructor_asts();
                    for (const auto& ctor_ast : ctor_asts) {
                        if (ctor_ast->parameters.size() == 1) {
                            // Has a single-argument constructor - conversion might be possible
                            // Further type checking could be done here if parameters have types
                            return true;
                        }
                    }
                }
                // For C++ classes, the constructor would be in the environment
                // Try looking it up
                auto ctor_result = environment_->get(target_type->type_name);
                if (ctor_result && ctor_result.value().is_function()) {
                    return true;  // Constructor exists
                }
            }
        }
    }

    return false;
}

// The one typed-slot table (detail::convert_for_typed_slot); this site owns the parameter /
// return texts and the identity return shapes (the arg itself; the derefed arg for exact-name
// objects - the I-C1 lane and the exact-class lane replicate them).
// KEEP BYTE-PARALLEL with vm_backend::try_convert_for_parameter.
checked_result<script_value> interpreter::try_convert_for_parameter(const script_value& arg, type_info_ptr target_type,
                                                                    const detail::slot_site& site) {
    if (!target_type) {
        return arg;  // No type specified = any type accepted
    }

    // deref() follows chains to a non-reference or throws - no residual-ref case
    const script_value& derefed_arg = arg.deref();
    const auto target_base_type = target_type->base_type;
    if (target_base_type == script_value_type::jai_any_type) return arg;
    switch (derefed_arg.raw_storage_index()) {   // same-storage scalars and handles bind as-is (the table's identity / handle rows)
        case script_value::TYPEID_INT: if (target_base_type == script_value_type::jai_int_type) return arg; break;
        case script_value::TYPEID_FLOAT: if (target_base_type == script_value_type::jai_float_type) return arg; break;
        case script_value::TYPEID_STRING: if (target_base_type == script_value_type::jai_string_type) return arg; break;
        case script_value::TYPEID_BOOL: if (target_base_type == script_value_type::jai_bool_type) return arg; break;
        case script_value::TYPEID_CHAR: if (target_base_type == script_value_type::jai_char_type) return arg; break;
        case script_value::TYPEID_SHARED_PTR: if (target_base_type == script_value_type::jai_shared_ptr_type) return arg; break;
        case script_value::TYPEID_WEAK_PTR: if (target_base_type == script_value_type::jai_weak_ptr_type) return arg; break;
        default: break;
    }

    auto outcome = detail::convert_for_typed_slot(arg, target_type, engine_);
    if (!outcome) {
        return outcome.error_value();
    }
    using verdict = detail::slot_conversion::verdict;
    detail::slot_conversion& out = outcome.value();
    switch (out.v) {
        case verdict::identity:
            return arg;
        case verdict::identity_exact:
            return derefed_arg;
        case verdict::converted:
            return std::move(*out.value);
        case verdict::container_mismatch: {
            auto* sym = engine_->get_symbolizer();
            return checked_result<script_value>(
                make_error_code(runtime_error_code::array_element_type_mismatch),
                detail::container_boundary_mismatch_text,
                sym->intern(out.mismatch->first), sym->intern(out.mismatch->second));
        }
        case verdict::handle_target: {
            // shared_ptr<T> / weak_ptr<T> parameters and returns (outside the table): a same-kind
            // payload, a matching or derived handle binds as-is
            if (derefed_arg.storage_type() == target_base_type) {
                return arg;
            }
            auto source_type_info = derefed_arg.get_type_info();
            if (source_type_info && source_type_info->base_type == script_value_type::jai_shared_ptr_type &&
                target_base_type == script_value_type::jai_shared_ptr_type &&
                detail::shared_handle_admits(derefed_arg, target_type, engine_)) {
                // R-q6: the parameter or return value has the declared static type (KEEP BYTE-PARALLEL with vm_backend)
                if (arg.is_reference() || source_type_info.get() == target_type.get()) { return arg; }
                script_value bound = arg;
                detail::keep_handle_slot_type(bound, target_type);
                return bound;
            }
            break;
        }
        default:
            break;
    }

    if (target_base_type == script_value_type::jai_object_type && !target_type->type_name.empty() &&
        (out.v == verdict::reject || out.v == verdict::reject_class)) {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::type_mismatch),
            "Cannot convert {0} to {1} (no suitable single-argument constructor)",
            string_symbolizer_->intern(detail::slot_source_type_name(derefed_arg, engine_)), target_type->id);
    }
    return detail::typed_slot_rejection(derefed_arg, *target_type, site, engine_);
}

script_value interpreter::make_coroutine_object(engine* eng, uint64_t type_id, std::shared_ptr<coroutine_handle> handle) {
    return script_value::make_coroutine_handle(type_id, std::static_pointer_cast<void>(handle), eng);
}

checked_result<script_value> interpreter::resume_coroutine(coroutine_handle& handle) {
    // Host-level resume is its own budgeted entry; a resume nested inside an
    // executing script keeps the outer deadline (and the outer limit state).
    // Depth alone is not enough: a TOP-LEVEL script statement calling resume() also
    // runs at call depth 0, and re-arming there let `for(..){ h.resume(); }` push the
    // deadline forever (fuzz livelock seeds 3507/8285/1213/8356/8391).
    if (current_call_depth_ == 0 && execute_depth_ == 0) {
        arm_execution_deadline();
        limits_->reset_for_execute();
    }

    // Check before activating or moving any coroutine state. A first resume is
    // counted by call_function; continuation replay below owns its own guard.
    if (current_call_depth_ >= limits_->interpreter_call_depth_limit || detail::native_stack_low()) {
        return detail::raise_call_depth();
    }
    // the body resolves in the unit that declared the coroutine on every resume (continuation replay runs it without call_function)
    auto declared = handle.get_function();
    const unit_stamp replay_unit = declared ? declared->unit : current_unit();
    unit_override_scope unitScope(this, &replay_unit);

    auto& state = coroutine_state(handle);

    coroutine_handle* prev_coroutine = active_coroutine_;
    bool prev_yield_request = hasYieldRequest_;
    active_coroutine_ = &handle;
    hasYieldRequest_ = false;

    auto prev_status = handle.get_status();
    handle.set_status(coroutine_handle::status::running);

    // Every exit hands the resumer back its coroutine and yield request; a C++ exception escaping the body fails the
    // handle (vm run_fiber parity). Declared ahead of the exception swap, so it runs after it.
    struct resume_exit {
        interpreter* in;
        coroutine_handle& handle;
        coroutine_handle* prev_coroutine;
        bool prev_yield_request;
        int uncaught = std::uncaught_exceptions();
        ~resume_exit() {
            if (std::uncaught_exceptions() > uncaught) { handle.set_status(coroutine_handle::status::failed); }
            in->active_coroutine_ = prev_coroutine;
            in->hasYieldRequest_ = prev_yield_request;
        }
    } exit_guard{this, handle, prev_coroutine, prev_yield_request};

    // The exception trio follows the frames (KEEP BYTE-PARALLEL with vm_backend::run_fiber's
    // fiber_exception_swap): the resumer's is saved here and comes back on every exit (a
    // throw escaping the body keeps its exception pair live for the resumer's catch); a
    // suspend parks the body's own for the next resume. A fresh coroutine starts with no
    // active exception.
    struct coroutine_exception_swap {
        interpreter* in;
        interpreter_coroutine_state* st;
        coroutine_handle* handle;
        std::optional<script_exception> resumer_exception;
        std::optional<script_value> resumer_exception_value;
        uint64_t resumer_marker;
        coroutine_exception_swap(interpreter* i, interpreter_coroutine_state* s, coroutine_handle* h, bool fresh)
            : in(i), st(s), handle(h), resumer_exception(std::move(i->current_exception_)),
              resumer_exception_value(std::move(i->active_exception_value_)), resumer_marker(i->current_catch_var_id_) {
            if (fresh) {
                in->current_exception_.reset();
                in->active_exception_value_.reset();
                in->current_catch_var_id_ = 0;
            } else {
                in->current_exception_ = std::move(st->parked_exception);
                in->active_exception_value_ = std::move(st->parked_exception_value);
                in->current_catch_var_id_ = st->parked_catch_marker;
            }
        }
        ~coroutine_exception_swap() {
            const bool park = handle->get_status() == coroutine_handle::status::suspended;
            if (park) {
                st->parked_exception = std::move(in->current_exception_);
                st->parked_exception_value = std::move(in->active_exception_value_);
                st->parked_catch_marker = in->current_catch_var_id_;
            }
            in->current_catch_var_id_ = resumer_marker;
            if (park || !in->is_unwinding_) {
                in->current_exception_ = std::move(resumer_exception);
                in->active_exception_value_ = std::move(resumer_exception_value);
            }
        }
    } exception_guard(this, &state, &handle, prev_status == coroutine_handle::status::created);

    // A runtime error in the (re)started segment must reach the caller as an error;
    // falling through to `return handle.yield_value_` would hand back the PREVIOUS
    // yield as a bogus success and silently swallow the failure.
    std::optional<checked_result<script_value>> error_result;

    auto function = handle.get_function();

    if (prev_status == coroutine_handle::status::created) {
        // On yield, call_function saves environment/call_stack into this coroutine
        // (via active_coroutine_) and returns the yield value WITHOUT running cleanup;
        // on normal return it cleans up and returns the result.
        // Coroutine parameters always bind by VALUE (VM parity): resume runs far from
        // the creation-site lvalues, so reference binding is meaningless here.
        std::vector<parameter> value_params = function->parameters;
        for (auto& p : value_params) { p.is_reference = false; }
        // Owned by the coroutine state, not this stack: the saved frames point at it
        // across every later resume (return-site conversion, debugger locals)
        state.function.emplace(
            function->name,
            value_params,
            function->return_type,
            function->body,
            handle.get_closure_env(),
            function->local_count
        );
        state.function->unit = function->unit;

        // A first resume nested inside another call's argument list must not see that
        // call's in-flight arg metadata (a zero-arg .resume() never saves/clears it)
        auto call_result = call_function(*state.function, handle.get_args());

        if (hasYieldRequest_) {
            // call_function already saved state into this coroutine
            // (saved_environment, saved_call_stack, etc.)
            handle.set_status(coroutine_handle::status::suspended);
        } else if (!call_result) {
            handle.set_status(coroutine_handle::status::failed);
            error_result.emplace(std::move(call_result));
        } else if (is_unwinding_) {
            // Script throw escaped the body: the handle is failed, the throw itself
            // keeps propagating to the caller through the unwinding flag
            handle.set_status(coroutine_handle::status::failed);
        } else {
            handle.set_status(coroutine_handle::status::completed);
            handle.yield_value_ = std::move(call_result.value());
        }

    } else {
        detail::call_depth_guard depth_guard(current_call_depth_);
        // Detached frames die LAST (after the caller state is restored): releasing their
        // locals can fire destructors that reenter call_function
        std::vector<call_frame> dying_frames;
        struct caller_state_restore {
            interpreter* in;
            std::shared_ptr<environment> env;
            std::optional<script_value> return_value;
            bool has_return;
            detail::member_scope_ctx member_scope;
            ~caller_state_restore() {
                in->environment_ = std::move(env);
                in->returnValue_ = std::move(return_value);
                in->hasReturnValue_ = has_return;
                in->member_scope_ = member_scope;
            }
        } caller_state{this, environment_, std::move(returnValue_), hasReturnValue_, member_scope_};

        // The coroutine's frames go ABOVE the resumer's (vm frames_ parity): a trace taken
        // inside the segment names the whole resume chain
        const size_t frame_base = call_stack_.size();
        for (auto& saved_frame : state.saved_call_stack) {
            call_stack_.push_back(std::move(saved_frame));
        }
        state.saved_call_stack.clear();
        auto detach_frames = [&](std::vector<call_frame>& into) {
            into.assign(std::make_move_iterator(call_stack_.begin() + frame_base),
                        std::make_move_iterator(call_stack_.end()));
            call_stack_.erase(call_stack_.begin() + frame_base, call_stack_.end());
        };
        // A C++ exception escaping the segment takes the coroutine's frames down with it, ahead of the caller state
        struct frames_on_throw {
            interpreter* in;
            size_t base;
            std::vector<call_frame>& dying;
            int uncaught = std::uncaught_exceptions();
            ~frames_on_throw() {
                if (std::uncaught_exceptions() > uncaught && in->call_stack_.size() > base) {
                    dying.assign(std::make_move_iterator(in->call_stack_.begin() + base), std::make_move_iterator(in->call_stack_.end()));
                    in->call_stack_.erase(in->call_stack_.begin() + base, in->call_stack_.end());
                }
            }
        } frames_guard{this, frame_base, dying_frames};

        environment_ = state.saved_environment;
        returnValue_ = std::move(state.saved_return_value);
        hasReturnValue_ = state.saved_has_return;
        member_scope_ = state.saved_member_scope;

        // The continuations are consumed LIFO (outermost first).
        // The outermost continuation is for the function body (call_function's loop).
        // Pop it to get the statement index where yield occurred.
        auto* body_cont = state.peek_continuation(function->body.get());
        size_t start_index = 0;
        if (body_cont) {
            start_index = body_cont->index;
            state.pop_continuation();
        }
        bool had_error = false;
        size_t last_body_idx = start_index;

        for (size_t i = start_index; i < function->body->declarations.size(); ++i) {
            last_body_idx = i;
            // For the first statement (start_index), the inner continuations
            // are still on the stack and will be consumed by visit_block_stmt,
            // visit_if_stmt, visit_while_stmt, etc. to fast-forward to the
            // exact point where yield occurred.
            checked_result<void> decl_result = i == start_index
                ? dispatch_decl_resuming(function->body->declarations[i].get())
                : dispatch_decl(function->body->declarations[i].get());
            // Statement boundary (visit_block_stmt parity): partial operands never
            // accumulate across the resumed body
            if (valueStack_.size() > 0) {
                valueStack_.clear();
            }

            if (!decl_result) {
                if (decl_result.error() != std::error_code()) {
                    if (!trace_captured_) capture_stack_trace();
                    handle.set_status(coroutine_handle::status::failed);
                    had_error = true;
                    error_result.emplace(decl_result.error_value());
                    break;
                }
            }

            if (hasReturnValue_ || hasYieldRequest_ || is_unwinding_) {
                break;
            }
        }

        if (had_error) {
            detach_frames(dying_frames);
        } else {
            if (is_unwinding_) {
                // Script throw escaped the resumed segment: fail the handle; the throw
                // keeps propagating to the caller through the unwinding flag
                if (!trace_captured_) capture_stack_trace();
                handle.set_status(coroutine_handle::status::failed);
                state.saved_environment.reset();
                detach_frames(dying_frames);
            } else if (hasYieldRequest_) {
                // Record where to resume in the function body.
                // If inner constructs pushed continuations, re-enter this statement.
                // If no inner continuations, the yield was a direct child, skip it.
                size_t resume_idx = state.has_continuations() ? last_body_idx : last_body_idx + 1;
                state.push_continuation(function->body.get(), resume_idx);
                state.saved_environment = environment_;
                detach_frames(state.saved_call_stack);
                state.saved_return_value = std::move(returnValue_);
                state.saved_has_return = hasReturnValue_;
                state.saved_member_scope = member_scope_;
                handle.set_status(coroutine_handle::status::suspended);
            } else if (hasReturnValue_) {
                handle.set_status(coroutine_handle::status::completed);
                if (returnValue_) {
                    // call_function epilogue parity: the return site already converted; deref
                    handle.yield_value_ = returnValue_.value().is_reference()
                        ? returnValue_.value().deref()
                        : std::move(returnValue_.value());
                }
                state.saved_environment.reset();
                detach_frames(dying_frames);
            } else {
                // Ran past the final yield: completion yields null (vm run_fiber parity),
                // never a replay of the stale last-yielded value.
                handle.set_status(coroutine_handle::status::completed);
                handle.yield_value_ = make_value();
                state.saved_environment.reset();
                detach_frames(dying_frames);
            }
        }
    }

    if (error_result) {
        return std::move(*error_result);
    }
    return handle.yield_value_;
}

// Function call optimization helpers
std::shared_ptr<environment> interpreter::get_block_environment(std::shared_ptr<environment> parent) {
    auto env = get_pooled_environment(std::move(parent));
    env->set_block_scope(true);
    return env;
}

std::shared_ptr<environment> interpreter::get_pooled_environment(std::shared_ptr<environment> parent) {
    if (environment_pool_index_ < environment_pool_.size()) {
        // Reuse existing environment from pool
        auto env = environment_pool_[environment_pool_index_++];
        env->reset(parent);
        return env;
    } else {
        // Pool is exhausted, create new environment and add to pool. env_symbolizer_ is
        // the epoch sink: == string_symbolizer_ normally; a parallel worker's private
        // instance, so worker env churn never bumps the shared engine epoch.
        auto newEnv = std::make_shared<environment>(parent, env_symbolizer_);
        environment_pool_.emplace_back(newEnv);
        ++environment_pool_index_;
        return newEnv;
    }
}

// Release an environment back to the pool.
// Lifetime ruling (2026-07, flat-stack stage 1): a freed pool slot holds NOTHING — a dead
// frame's env must not keep its values or parent chain (a lambda's capture env!) alive
// until slot reuse; weak_ptr expiry is script-observable. The epilogue flattens/derefs
// return values BEFORE release, and references are owner-pinned (cells), so resetting
// here is lawful — the old defer-to-reuse behavior was a pre-cells relic. Escaped envs
// (still referenced beyond the pool + the caller's handle: an escaping closure_env, a
// suspended coroutine chain) keep SOLE ownership and their slot is re-minted, so they
// are never wiped by later reuse either — same escape rule reset_environment_pool
// applies between executes (vm release_scope_env parity).
// Parameter is const&: unshared == use_count() of exactly 2 (pool entry + the caller's
// handle). Callers must pass a named handle, never a fresh temporary copy (a temp adds
// a count and demotes the release to the escape path — safe, but re-mints needlessly).
void interpreter::release_environment(const std::shared_ptr<environment>& env, bool clear_now) {
    if (!env) return;

    // Block scopes clear eagerly (pre-existing; script dtor ORDER is pinned on this
    // happening before the slot frees, and dtor reentrancy sees the slot as in-use)
    if (clear_now) {
        env->reset(nullptr);
    }

    // Only the pool TOP may be released. Decrementing for anything else (a non-pooled
    // make_shared env, or an out-of-order release) hands the caller's still-live scope
    // to the next get_pooled_environment, which reset()s it into a self-parent cycle.
    if (environment_pool_index_ > 0 && environment_pool_[environment_pool_index_ - 1] == env) {
        if (env.use_count() <= 2) {
            if (!clear_now) {
                // Lifetime ruling (2026-07): a freed pool slot must not pin a PARENT
                // CHAIN until slot reuse — that retention was exactly the dead-lambda
                // capture leak (the freed frame env's parent was the capture env).
                // ONLY the parent link (+ its stale pointer cache) drops here: the
                // env's values/kind/this/bound-method interiors deliberately survive
                // until reuse-time reset — the call machinery holds transient pointers
                // into them for a few instructions after release, and clearing values
                // here detonated as a use-after-reset AV (Debug 0xDD). clear_now
                // block scopes were already fully reset above, pre-existing behavior
                // (script dtor order is pinned on it).
                env->set_parent(nullptr);
                env->clear_parent_cache();
            }
            --environment_pool_index_;
        } else {
            // Escaped: the outside holder owns it from now on; re-mint the freed slot
            --environment_pool_index_;
            environment_pool_[environment_pool_index_] = std::make_shared<environment>(nullptr, env_symbolizer_);
        }
        return;
    }

    // Non-pooled env: clear when the caller vouches it is dead (block scopes); a live
    // escapee just drops the caller's handle and its refcount governs.
    if (clear_now && env.use_count() <= 1) {
        env->reset(nullptr);
    }
}

std::shared_ptr<environment> interpreter::get_pooled_method_environment(std::shared_ptr<environment> parent, script_value this_obj, class_definition* access_ctx) {
    // Use unified pool - get an environment and reset it as a method environment
    if (environment_pool_index_ < environment_pool_.size()) {
        // Reuse existing environment from pool
        auto env = environment_pool_[environment_pool_index_++];
        // Reset as method environment with this object
        env->reset_as_method(parent, std::move(this_obj));
        env->set_access_context(access_ctx);
        return env;
    } else {
        // Pool is exhausted, create new method environment and add to pool
        auto newEnv = std::make_shared<environment>(parent, env_symbolizer_, std::move(this_obj));
        newEnv->set_access_context(access_ctx);
        environment_pool_.emplace_back(newEnv);
        ++environment_pool_index_;
        return newEnv;
    }
}

void interpreter::reset_environment_pool() {
    environment_pool_index_ = 0;

    // A suspended coroutine keeps its env chain alive across executes; resetting those
    // would orphan its variables (undefined variable on resume). Any env still referenced
    // outside the pool escapes it - the holder owns it from now on (vm parity).
    std::erase_if(environment_pool_, [](const std::shared_ptr<environment>& env) {
        return env.use_count() > 1;
    });

    // Clear all environments in the unified pool to release references
    for (auto& env : environment_pool_) {
        // Reset the environment by clearing its parent, values, and kind-specific fields
        env->reset(nullptr);
    }
}

// ============================================================
// SWITCH-BASED DISPATCH (faster than virtual calls)
checked_result<void> interpreter::visit_enum_decl(enum_decl* decl) {
    auto enum_map = script_value::make_map(
        engine_->get_type_info_string(),
        engine_->get_type_info_int(),
        engine_);
    auto& map_ref = const_cast<script_map&>(enum_map.as_map());

    for (size_t i = 0; i < decl->values.size(); ++i) {
        auto key = script_value(std::string(decl->values[i].first), engine_);
        map_ref[std::move(key)] = make_int_fast(static_cast<script_int>(i));
    }

    environment_->define(decl->name_id, std::move(enum_map));
    return {};
}

// extern declares nothing: the name resolves to whatever defines it, and a use while nothing does raises the
// extern's own text. KEEP BYTE-PARALLEL with vm_backend::exec_extern_decl.
checked_result<void> interpreter::visit_extern_decl(extern_decl* decl) {
    environment_->note_extern(decl->name_id, decl->is_function);
    return {};
}

template<bool Replaying>
checked_result<void> interpreter::visit_destructuring_decl_t(destructuring_decl* decl) {
    JAISCRIPT_TRY_ROOT(decl, decl->initializer.get());
    script_value source = pop_value();

    if (!source.is_array()) {
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
            "Destructuring requires an array on the right-hand side");
    }

    const auto& node = *get_array_storage(source);

    for (size_t i = 0; i < decl->names.size(); ++i) {
        script_value val = i < node.size()
            ? (node.is_typed() ? node.get(i, engine_) : clone_for_assignment(node.values()[i]))
            : make_value();

        if (decl->slot_indices[i] != SIZE_MAX && !call_stack_.empty()) {
            call_stack_.back().set_local(decl->slot_indices[i], std::move(val));
        } else if (!environment_->define_variable(decl->names[i].second, std::move(val))) [[unlikely]] {
            return detail::inline_redefinition(*string_symbolizer_, decl->names[i].second, nullptr);
        }
    }

    return {};
}

// These functions use a switch on node_type enum instead of
// virtual method dispatch, eliminating vtable lookup overhead.
// ============================================================

void interpreter::park_operands(ast_node* node, uint32_t phase, size_t index, std::vector<script_value> partial) {
    if (active_coroutine_) {
        coroutine_state(*active_coroutine_).push_replay(node, phase, index, std::move(partial));
    }
}

// A statement whose expression suspended: it owns the replay root, so the enclosing block
// re-enters THIS statement on resume (has_continuations() is true) and hands it to
// dispatch_decl_resuming. A bare `yield x;` parks nothing - the block skips it as before.
void interpreter::note_statement_yield(ast_node* stmt, expression* root) {
    if (!active_coroutine_) return;
    auto& coro_state = coroutine_state(*active_coroutine_);
    const node_type kind = stmt->get_type();
    if ((kind == node_type::expression_stmt || kind == node_type::expression_decl) &&
        root->get_type() == node_type::yield_expr && coro_state.back_node() != root) {
        return;   // the statement IS the yield and its value is discarded: nothing to continue
    }
    coro_state.push_replay(stmt, rp_root, 0, {});
}

// Resume side: run the suspended child of a parked expression node. A child with no frame
// of its own is the recorded yield - consumed exactly once, its operand never re-evaluated.
checked_result<void> interpreter::replay_expression(expression* expr) {
    auto& coro_state = coroutine_state(*active_coroutine_);
    if (coro_state.back_node() != expr) {
        if (expr->get_type() == node_type::yield_expr) {
            push_value(make_value());   // vm exec_yield: the yield expression is null on resume
            return {};
        }
        return checked_result<void>(make_error_code(runtime_error_code::coroutine_suspended));
    }
    const replay_point frame = *coro_state.peek_continuation(expr);
    coro_state.pop_continuation();
    switch (expr->get_type()) {
        case node_type::binary_expr:        return visit_binary_expr_t<true>(static_cast<binary_expr*>(expr), &frame);
        case node_type::unary_expr:         return visit_unary_expr_t<true>(static_cast<unary_expr*>(expr), &frame);
        case node_type::assignment_expr:    return visit_assignment_expr_t<true>(static_cast<assignment_expr*>(expr), &frame);
        case node_type::call_expr:          return visit_call_expr_t<true>(static_cast<call_expr*>(expr), &frame);
        case node_type::member_expr:        return visit_member_expr_t<true>(static_cast<member_expr*>(expr), &frame);
        case node_type::new_expr:           return visit_new_expr_t<true>(static_cast<new_expr*>(expr), &frame);
        case node_type::ternary_expr:       return visit_ternary_expr_t<true>(static_cast<ternary_expr*>(expr), &frame);
        case node_type::array_literal_expr: return visit_array_literal_expr_t<true>(static_cast<array_literal_expr*>(expr), &frame);
        case node_type::map_literal_expr:   return visit_map_literal_expr_t<true>(static_cast<map_literal_expr*>(expr), &frame);
        case node_type::cast_expr:          return visit_cast_expr_t<true>(static_cast<cast_expr*>(expr), &frame);
        case node_type::throw_expr:         return visit_throw_expr_t<true>(static_cast<throw_expr*>(expr), &frame);
        case node_type::yield_expr:         return visit_yield_expr_t<true>(static_cast<yield_expr*>(expr), &frame);
        default:
            return checked_result<void>(make_error_code(runtime_error_code::coroutine_suspended));
    }
}

// The one statement/declaration a resume re-enters: it consumes its own replay root, every
// other statement runs cold
checked_result<void> interpreter::dispatch_stmt_resuming(statement* stmt) {
    if (!active_coroutine_) return dispatch_stmt(stmt);
    auto& coro_state = coroutine_state(*active_coroutine_);
    auto* cont = coro_state.peek_continuation(stmt);
    if (!cont || cont->phase != rp_root) return dispatch_stmt(stmt);
    coro_state.pop_continuation();
    switch (stmt->get_type()) {
        case node_type::expression_stmt: return visit_expression_stmt_t<true>(static_cast<expression_stmt*>(stmt));
        case node_type::return_stmt:     return visit_return_stmt_t<true>(static_cast<return_stmt*>(stmt));
        default:
            return checked_result<void>(make_error_code(runtime_error_code::coroutine_suspended));
    }
}

checked_result<void> interpreter::dispatch_decl_resuming(declaration* decl) {
    if (!active_coroutine_) return dispatch_decl(decl);
    if (decl->get_type() == node_type::statement_decl) {
        return dispatch_stmt_resuming(static_cast<statement_decl*>(decl)->statement.get());
    }
    auto& coro_state = coroutine_state(*active_coroutine_);
    auto* cont = coro_state.peek_continuation(decl);
    if (!cont || cont->phase != rp_root) return dispatch_decl(decl);
    coro_state.pop_continuation();
    switch (decl->get_type()) {
        case node_type::variable_decl:        return visit_variable_decl_t<true>(static_cast<variable_decl*>(decl));
        case node_type::destructuring_decl:   return visit_destructuring_decl_t<true>(static_cast<destructuring_decl*>(decl));
        case node_type::expression_decl:      return visit_expression_decl_t<true>(static_cast<expression_decl*>(decl));
        default:
            return checked_result<void>(make_error_code(runtime_error_code::coroutine_suspended));
    }
}

checked_result<void> interpreter::visit_binary_expr(binary_expr* expr) { return visit_binary_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_unary_expr(unary_expr* expr) { return visit_unary_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_assignment_expr(assignment_expr* expr) { return visit_assignment_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_call_expr(call_expr* expr) { return visit_call_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_member_expr(member_expr* expr) { return visit_member_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_new_expr(new_expr* expr) { return visit_new_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_ternary_expr(ternary_expr* expr) { return visit_ternary_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_array_literal_expr(array_literal_expr* expr) { return visit_array_literal_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_map_literal_expr(map_literal_expr* expr) { return visit_map_literal_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_cast_expr(cast_expr* expr) { return visit_cast_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_throw_expr(throw_expr* expr) { return visit_throw_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_yield_expr(yield_expr* expr) { return visit_yield_expr_t<false>(expr, nullptr); }
checked_result<void> interpreter::visit_expression_stmt(expression_stmt* stmt) { return visit_expression_stmt_t<false>(stmt); }
checked_result<void> interpreter::visit_return_stmt(return_stmt* stmt) { return visit_return_stmt_t<false>(stmt); }
checked_result<void> interpreter::visit_variable_decl(variable_decl* decl) { return visit_variable_decl_t<false>(decl); }
checked_result<void> interpreter::visit_destructuring_decl(destructuring_decl* decl) { return visit_destructuring_decl_t<false>(decl); }
checked_result<void> interpreter::visit_expression_decl(expression_decl* decl) { return visit_expression_decl_t<false>(decl); }

checked_result<void> interpreter::dispatch_expr(expression* expr) {
    switch (expr->get_type()) {
        case node_type::literal_expr:
            return visit_literal_expr(static_cast<literal_expr*>(expr));
        case node_type::identifier_expr:
            return visit_identifier_expr(static_cast<identifier_expr*>(expr));
        case node_type::binary_expr:
            return visit_binary_expr(static_cast<binary_expr*>(expr));
        case node_type::unary_expr:
            return visit_unary_expr(static_cast<unary_expr*>(expr));
        case node_type::assignment_expr:
            return visit_assignment_expr(static_cast<assignment_expr*>(expr));
        case node_type::call_expr:
            return visit_call_expr(static_cast<call_expr*>(expr));
        case node_type::member_expr:
            return visit_member_expr(static_cast<member_expr*>(expr));
        case node_type::lambda_expr:
            return visit_lambda_expr(static_cast<lambda_expr*>(expr));
        case node_type::new_expr:
            return visit_new_expr(static_cast<new_expr*>(expr));
        case node_type::include_expr:
            return visit_include_expr(static_cast<include_expr*>(expr));
        case node_type::import_expr:
            return visit_import_expr(static_cast<import_expr*>(expr));
        case node_type::ternary_expr:
            return visit_ternary_expr(static_cast<ternary_expr*>(expr));
        case node_type::array_literal_expr:
            return visit_array_literal_expr(static_cast<array_literal_expr*>(expr));
        case node_type::map_literal_expr:
            return visit_map_literal_expr(static_cast<map_literal_expr*>(expr));
        case node_type::this_expr:
            return visit_this_expr(static_cast<this_expr*>(expr));
        case node_type::super_expr:
            return visit_super_expr(static_cast<super_expr*>(expr));
        case node_type::throw_expr:
            return visit_throw_expr(static_cast<throw_expr*>(expr));
        case node_type::yield_expr:
            return visit_yield_expr(static_cast<yield_expr*>(expr));
        case node_type::cast_expr:
            return visit_cast_expr(static_cast<cast_expr*>(expr));
        default:
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                "Unknown expression type in dispatch_expr");
    }
}

void interpreter::capture_stack_trace() {
    captured_trace_.clear();
    for (auto it = call_stack_.rbegin(); it != call_stack_.rend(); ++it) {
        const ast_node* node = it->current_node;
        captured_trace_.push_back({
            it->function_name.empty() ? std::string("<anonymous>") : std::string(it->function_name),
            node ? node->location.filename : std::string(),
            node ? node->location.line : 0,
            node ? node->location.column : 0
        });
    }
    captured_trace_.push_back({
        "<script>",
        top_level_node_ ? top_level_node_->location.filename : std::string(),
        top_level_node_ ? top_level_node_->location.line : 0,
        top_level_node_ ? top_level_node_->location.column : 0
    });
    trace_captured_ = true;
}

void interpreter::note_clock_sample(std::chrono::steady_clock::time_point now) {
    if (engine_ && !parallel_worker_ && !engine_->parallel_region_active()) { engine_->execution_limits().note_clock_sample(now); }
}

// KEEP BYTE-PARALLEL with vm_backend::park_nested_failure / absorb_nested_trace
void interpreter::park_nested_failure() {
    nested_failure_.parked = true;
    nested_failure_.error = run_error_;
    nested_failure_.value = active_exception_value_;
    nested_failure_.exception = current_exception_;
    nested_failure_.trace = captured_trace_;
}

void interpreter::absorb_nested_trace() {
    if (!nested_failure_.parked) { return; }
    std::vector<stack_trace_entry> inner = std::move(nested_failure_.trace);
    nested_failure_.trace.clear();
    capture_stack_trace();
    captured_trace_.insert(captured_trace_.begin(), inner.begin(), inner.end());
}

std::string interpreter::current_source_file() const {
    for (auto it = call_stack_.rbegin(); it != call_stack_.rend(); ++it) {
        if (it->current_node && !it->current_node->location.filename.empty()) return it->current_node->location.filename;
    }
    return top_level_node_ ? top_level_node_->location.filename : std::string();
}

std::string interpreter::format_stack_trace() const {
    std::string out;
    for (const auto& e : captured_trace_) {
        out += "  at " + e.function + " (" +
            (e.file.empty() ? std::string("<script>") : e.file) + ":" + std::to_string(e.line) + ")\n";
    }
    return out;
}

checked_result<void> interpreter::dispatch_stmt(statement* stmt) {
    if (!call_stack_.empty()) call_stack_.back().current_node = stmt;
    else top_level_node_ = stmt;
    if (debug_hook_) [[unlikely]] {
        if (debug_hook_->wants_statement(static_cast<uint32_t>(stmt->location.line)))
            debug_hook_->on_statement(stmt, static_cast<int>(call_stack_.size()));
    }
    switch (stmt->get_type()) {
        case node_type::expression_stmt:
            return visit_expression_stmt(static_cast<expression_stmt*>(stmt));
        case node_type::block_stmt:
            return visit_block_stmt(static_cast<block_stmt*>(stmt));
        case node_type::if_stmt:
            return visit_if_stmt(static_cast<if_stmt*>(stmt));
        case node_type::while_stmt:
            return visit_while_stmt(static_cast<while_stmt*>(stmt));
        case node_type::for_stmt:
            return visit_for_stmt(static_cast<for_stmt*>(stmt));
        case node_type::range_for_stmt:
            return visit_range_for_stmt(static_cast<range_for_stmt*>(stmt));
        case node_type::parallel_for_stmt:
            return visit_parallel_for_stmt(static_cast<parallel_for_stmt*>(stmt));
        case node_type::return_stmt:
            return visit_return_stmt(static_cast<return_stmt*>(stmt));
        case node_type::break_stmt:
            return visit_break_stmt(static_cast<break_stmt*>(stmt));
        case node_type::continue_stmt:
            return visit_continue_stmt(static_cast<continue_stmt*>(stmt));
        case node_type::try_stmt:
            return visit_try_stmt(static_cast<try_stmt*>(stmt));
        case node_type::switch_stmt:
            return visit_switch_stmt(static_cast<switch_stmt*>(stmt));
        case node_type::case_stmt:
            return visit_case_stmt(static_cast<case_stmt*>(stmt));
        case node_type::default_stmt:
            return visit_default_stmt(static_cast<default_stmt*>(stmt));
        case node_type::fallthrough_stmt:
            return visit_fallthrough_stmt(static_cast<fallthrough_stmt*>(stmt));
        default:
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                "Unknown statement type in dispatch_stmt");
    }
}

checked_result<void> interpreter::dispatch_decl(declaration* decl) {
    if (!call_stack_.empty()) call_stack_.back().current_node = decl;
    else top_level_node_ = decl;
    if (debug_hook_) [[unlikely]] {
        // statement_decl is a pass-through wrapper: the inner dispatch_stmt fires the
        // hook — firing here too would stop twice on one statement (and a second park
        // after the resume is a join hang for anyone driving the controller).
        if (decl->get_type() != node_type::statement_decl &&
            debug_hook_->wants_statement(static_cast<uint32_t>(decl->location.line)))
            debug_hook_->on_statement(decl, static_cast<int>(call_stack_.size()));
    }
    switch (decl->get_type()) {
        case node_type::variable_decl:
            return visit_variable_decl(static_cast<variable_decl*>(decl));
        case node_type::function_decl:
            return visit_function_decl(static_cast<function_decl*>(decl));
        case node_type::class_decl:
            return visit_class_decl(static_cast<class_decl*>(decl));
        case node_type::namespace_decl:
            return visit_namespace_decl(static_cast<namespace_decl*>(decl));
        case node_type::expression_decl:
            return visit_expression_decl(static_cast<expression_decl*>(decl));
        case node_type::include_decl:
            return visit_include_decl(static_cast<include_decl*>(decl));
        case node_type::import_decl:
            return visit_import_decl(static_cast<import_decl*>(decl));
        case node_type::enum_decl:
            return visit_enum_decl(static_cast<enum_decl*>(decl));
        case node_type::destructuring_decl:
            return visit_destructuring_decl(static_cast<destructuring_decl*>(decl));
        case node_type::extern_decl:
            return visit_extern_decl(static_cast<extern_decl*>(decl));
        case node_type::statement_decl:
            return dispatch_stmt(static_cast<statement_decl*>(decl)->statement.get());
        default:
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                "Unknown declaration type in dispatch_decl");
    }
}

} // namespace jai
