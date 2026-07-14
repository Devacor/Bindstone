#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/containers.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/detail/integer_ops.hpp>   // kCheckedOverflow + jai::ints overflow policy
#include <jaiscript/detail/body_walker.hpp>   // nested-coroutine capture analysis
#include <jaiscript/detail/math_intrinsics.hpp>   // math:: language intrinsics (shared kernel)
#include <jaiscript/detail/ref_lvalue.hpp>    // shared ref-param lvalue binding (vm parity)
#include <jaiscript/detail/transient_read.hpp>   // callout_free (subscript-compound fast path)
#include <jaiscript/detail/char_promotion.hpp>   // char operands promote to int64 0..255
#include <jaiscript/detail/container_enforce.hpp> // typed-container boundary kernel (vm parity)
#include <jaiscript/detail/parallel_transform.hpp>   // parallel_borrow_subscript_read (captured reads)
#include <jaiscript/detail/ast_serializer.hpp>   // structural_node_key (hot-reload identity)
#include <jaiscript/debug/controller.hpp>     // step-debugger statement hook
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <filesystem>
#include <fstream>

namespace jai {

namespace {
    // Local helper: error result for an overflowing integer op in the inlined
    // fast paths below (mirrors interpreter_dispatch.cpp's handlers).
    inline checked_result<void> int_overflow_v(const char* msg) {
        return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand), msg);
    }
    inline checked_result<script_value> int_overflow_sv(const char* msg) {
        return checked_result<script_value>(make_error_code(runtime_error_code::invalid_numeric_operand), msg);
    }

    // Twin-parity zero-divisor check for cpp-bound operands: index 14 skips the raw-index
    // gates of visit_binary_expr's fast paths, which would move a bound-zero divisor's error
    // from the fast-path surface onto the dispatch handlers' bare "Division by zero". Each
    // shape passes the exact (op x type-pair) coverage of its own inline zero checks so bound
    // operands keep the plain twin's error surface. Byte-parallel with vm_backend.cpp's copy.
    struct bound_zero_divisor_error { runtime_error_code code; const char* text; };
    inline std::optional<bound_zero_divisor_error> bound_fastpath_zero_divisor(
            token_type op, const script_value& left, const script_value& right,
            bool float_div_covered, bool float_mod_covered) {
        if (op != token_type::slash && op != token_type::percent) return std::nullopt;
        if (left.is_int() && right.is_int()) {
            if (right.unchecked_as_int() != 0) return std::nullopt;
            return op == token_type::slash
                ? bound_zero_divisor_error{runtime_error_code::division_by_zero, "Division by zero in integer operation"}
                : bound_zero_divisor_error{runtime_error_code::modulo_by_zero, "Modulo by zero in integer operation"};
        }
        if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
            if (!(op == token_type::slash ? float_div_covered : float_mod_covered)) return std::nullopt;
            const script_float rf = right.is_int() ? static_cast<script_float>(right.unchecked_as_int()) : right.unchecked_as_float();
            if (rf != 0.0) return std::nullopt;
            return op == token_type::slash
                ? bound_zero_divisor_error{runtime_error_code::division_by_zero, "Division by zero in float operation"}
                : bound_zero_divisor_error{runtime_error_code::modulo_by_zero, "Modulo by zero in float operation"};
        }
        return std::nullopt;
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
}

// Element compatibility/conversion live in the shared kernel (detail/container_enforce.hpp,
// used verbatim by both backends and the field kernel). These forwards keep the
// historical local names push/subscript-store call.
static bool is_element_type_compatible(
    const script_value& element,
    type_info_ptr element_type,
    script_value& /*array_owner*/
) {
    return detail::container_element_compatible(element, element_type);
}

static script_value convert_array_element(
    engine* eng,
    const script_value& element,
    type_info_ptr element_type
) {
    return detail::convert_container_element(eng, element, element_type);
}

// Capture semantics: C++-backed objects (make_object wrappers, raw cpp holders) are
// reference types in script — capturing one shares the underlying object, mirroring
// shared_ptr capture. Pure script objects keep capture-by-value (deep copy).
static script_value clone_for_capture(const script_value& value, string_symbolizer* symbolizer) {
    auto type_info = value.get_type_info();
    if (type_info && type_info->base_type == script_value_type::jai_shared_ptr_type) {
        return value;
    }
    if (value.is_object()) {
        auto holder = const_cast<script_value&>(value).get_object_holder();
        if (holder) {
            if (!holder->is_class_instance_wrapper) {
                return value;  // raw C++ object holder: share
            }
            auto instance = std::static_pointer_cast<class_instance>(holder->data);
            if (instance && symbolizer && instance->has_field(symbolizer->intern(class_constants::CPP_OBJECT_FIELD))) {
                return value;  // class_instance wrapping a C++ object: share
            }
        }
    }
    return value.clone();
}

// Helper function to convert script_value_type to a human-readable string
static std::string get_type_name(script_value_type type) {
    switch (type) {
        case script_value_type::jai_null_type: return "null";
        case script_value_type::jai_int_type: return "int";
        case script_value_type::jai_float_type: return "float";
        case script_value_type::jai_string_type: return "string";
        case script_value_type::jai_char_type: return "char";
        case script_value_type::jai_bool_type: return "bool";
        case script_value_type::jai_array_type: return "array";
        case script_value_type::jai_map_type: return "map";
        case script_value_type::jai_object_type: return "object";
        case script_value_type::jai_function_type: return "function";
        case script_value_type::jai_reference_type: return "reference";
        case script_value_type::jai_shared_ptr_type: return "shared_ptr";
        case script_value_type::jai_weak_ptr_type: return "weak_ptr";
        case script_value_type::jai_any_type: return "any";
        case script_value_type::jai_invalid_type: return "invalid";
        default: return "unknown";
    }
}

// Helper to get a human-readable type name from a script_value
static std::string get_value_type_name(const script_value& val) {
    auto type_info = val.get_type_info();
    if (type_info && !type_info->type_name.empty()) {
        return type_info->type_name;
    }
    return get_type_name(val.type());
}

// Helper to get type name from a type_info, falling back to base type
static std::string get_type_info_name(type_info_ptr info) {
    if (info && !info->type_name.empty()) {
        return info->type_name;
    }
    if (info) {
        return get_type_name(info->base_type);
    }
    return "unknown";
}

// Forward declaration for mutual recursion
static checked_result<void> validate_container_homogeneous(const script_value& container, const std::string& path = "");

// Helper to validate map value homogeneity for auto declarations
// Returns: error_code if values have different types, success otherwise
static checked_result<void> validate_map_homogeneous(const script_value& map_value, const std::string& path) {
    if (!map_value.is_map()) {
        return {};  // Not a map, nothing to validate
    }

    const auto& map = map_value.as_map();
    if (map.size() <= 1) {
        // Empty or single-value maps are trivially homogeneous
        // But still need to recursively validate the single value if it's a container
        if (map.size() == 1) {
            const script_value& first_val = map.begin()->second.is_reference() ?
                map.begin()->second.deref() : map.begin()->second;
            JAISCRIPT_TRY(validate_container_homogeneous(first_val, path + "[0]"));
        }
        return {};
    }

    // Get the type of the first value (deduced type)
    auto first_it = map.begin();
    const script_value& first_val = first_it->second.is_reference() ? first_it->second.deref() : first_it->second;
    script_value_type deduced_type = first_val.type();
    std::string deduced_type_name = get_type_name(deduced_type);

    // For object types, also track class name
    std::string deduced_class_name;
    if (deduced_type == script_value_type::jai_object_type) {
        auto type_info = first_val.get_type_info();
        if (type_info) {
            deduced_class_name = type_info->type_name;
        }
    }

    // Recursively validate the first value if it's a container
    JAISCRIPT_TRY(validate_container_homogeneous(first_val, path + "[first]"));

    // Check all other values match
    size_t idx = 1;
    for (auto it = ++map.begin(); it != map.end(); ++it, ++idx) {
        const script_value& val = it->second.is_reference() ? it->second.deref() : it->second;
        script_value_type val_type = val.type();

        // Check base type match
        if (val_type != deduced_type) {
            return checked_result<void>(
                make_error_code(runtime_error_code::map_value_type_mismatch),
                "Map 'auto' requires homogeneous values - type mismatch at index {0}",
                static_cast<uint64_t>(idx), val.type_id());
        }

        // For object types, also check class name match
        if (deduced_type == script_value_type::jai_object_type && !deduced_class_name.empty()) {
            auto val_type_info = val.get_type_info();
            std::string val_class_name = val_type_info ? val_type_info->type_name : "";
            if (val_class_name != deduced_class_name) {
                return checked_result<void>(
                    make_error_code(runtime_error_code::map_value_type_mismatch),
                    "Map 'auto' requires homogeneous class types - mismatch at index {0}",
                    static_cast<uint64_t>(idx), val.type_id());
            }
        }

        // Recursively validate this value if it's a container
        JAISCRIPT_TRY(validate_container_homogeneous(val, path + "[" + std::to_string(idx) + "]"));
    }

    return {};
}

// Helper to validate array homogeneity for auto/array<auto> declarations
// Returns: error_code if elements have different types, success otherwise
// For auto x = [...] and array<auto> x = [...], all elements must have the same type
// Recursively validates nested arrays and maps to arbitrary depth
static checked_result<void> validate_array_homogeneous(const script_value& array_value, const std::string& path = "") {
    if (!array_value.is_array()) {
        return {};  // Not an array, nothing to validate
    }

    const auto& elements = array_value.as_array();
    if (elements.size() <= 1) {
        // Empty or single-element arrays are trivially homogeneous
        // But still need to recursively validate the single element if it's a container
        if (elements.size() == 1) {
            const script_value& first = elements[0].is_reference() ? elements[0].deref() : elements[0];
            JAISCRIPT_TRY(validate_container_homogeneous(first, path + "[0]"));
        }
        return {};
    }

    // Get the type of the first element (deduced type)
    const script_value& first = elements[0].is_reference() ? elements[0].deref() : elements[0];
    script_value_type deduced_type = first.type();
    std::string deduced_type_name = get_type_name(deduced_type);

    // For object types, also track class name
    std::string deduced_class_name;
    if (deduced_type == script_value_type::jai_object_type) {
        auto type_info = first.get_type_info();
        if (type_info) {
            deduced_class_name = type_info->type_name;
        }
    }

    // Recursively validate the first element if it's a container
    JAISCRIPT_TRY(validate_container_homogeneous(first, path + "[0]"));

    // Check all other elements match
    for (size_t i = 1; i < elements.size(); ++i) {
        const script_value& elem = elements[i].is_reference() ? elements[i].deref() : elements[i];
        script_value_type elem_type = elem.type();

        // Check base type match
        if (elem_type != deduced_type) {
            return checked_result<void>(
                make_error_code(runtime_error_code::array_element_type_mismatch),
                "Array 'auto' requires homogeneous elements - type mismatch at index {0}",
                static_cast<uint64_t>(i), elem.type_id());
        }

        // For object types, also check class name match
        if (deduced_type == script_value_type::jai_object_type && !deduced_class_name.empty()) {
            auto elem_type_info = elem.get_type_info();
            std::string elem_class_name = elem_type_info ? elem_type_info->type_name : "";
            if (elem_class_name != deduced_class_name) {
                return checked_result<void>(
                    make_error_code(runtime_error_code::array_element_type_mismatch),
                    "Array 'auto' requires homogeneous class types - mismatch at index {0}",
                    static_cast<uint64_t>(i), elem.type_id());
            }
        }

        // Recursively validate this element if it's a container
        JAISCRIPT_TRY(validate_container_homogeneous(elem, path + "[" + std::to_string(i) + "]"));
    }

    return {};  // All elements are the same type
}

// Unified container homogeneity validation - handles both arrays and maps recursively
static checked_result<void> validate_container_homogeneous(const script_value& container, const std::string& path) {
    if (container.is_array()) {
        return validate_array_homogeneous(container, path);
    } else if (container.is_map()) {
        return validate_map_homogeneous(container, path);
    }
    return {};  // Not a container, nothing to validate
}

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
        if (!is_element_type_compatible(args[0], element_type, self)) {
            // Get type names for error message
            std::string value_type = get_value_type_name(args[0]);
            std::string expected_type = get_type_info_name(element_type);
            // Intern the type names for the error message
            uint64_t value_type_id = ctx.get_string_symbolizer()->intern(value_type);
            uint64_t expected_type_id = ctx.get_string_symbolizer()->intern(expected_type);
            return checked_result<script_value>(
                make_error_code(runtime_error_code::array_element_type_mismatch),
                "Cannot push '{0}' to array<{1}>",
                value_type_id, expected_type_id);
        }

        // Convert element if needed (e.g., int -> float for array<float>)
        script_value converted = convert_array_element(ctx.get_engine(), args[0], element_type);

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
        const script_array* node = self.unchecked_array_node();
        for (size_t i = 0; i < node->size(); ++i) {
            if (node->get(i, ctx.get_engine()) == args[0]) {
                return ctx.make_value(static_cast<script_int>(i));
            }
        }
        return ctx.make_value(static_cast<script_int>(-1));
    }},

        {symbolizer->intern("has"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "has() takes exactly one argument");
        }
        const script_array* node = self.unchecked_array_node();
        for (size_t i = 0; i < node->size(); ++i) {
            if (node->get(i, ctx.get_engine()) == args[0]) {
                return ctx.make_value(true);
            }
        }
        return ctx.make_value(false);
    }},

        {symbolizer->intern("contains"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "contains() takes exactly one argument");
        }
        const script_array* node = self.unchecked_array_node();
        for (size_t i = 0; i < node->size(); ++i) {
            if (node->get(i, ctx.get_engine()) == args[0]) {
                return ctx.make_value(true);
            }
        }
        return ctx.make_value(false);
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

        script_value result = script_value::make_array(nullptr, ctx.get_engine());
        auto& resultPtr = result.unchecked_get_array_storage()->values();
        for (script_int i = start; i < end; ++i) {
            resultPtr.push_back(node->get(static_cast<size_t>(i), ctx.get_engine()).clone());
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
        if (node.is_typed() && args.empty()) {
            // raw buffer sort: no comparator, so no user script can mutate mid-sort
            if (node.kind() == script_array::kind_t::i64) { std::sort(node.ints().begin(), node.ints().end()); }
            else { std::sort(node.floats().begin(), node.floats().end()); }
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
            // Default order: a strict weak ordering for ALL element types. Numerics
            // compare by value (int/float mixed too); everything else falls back to
            // script_value's total <=> order.
            std::sort(tmp.begin(), tmp.end(), [](const script_value& a, const script_value& b) {
                const bool an = a.is_int() || a.is_float();
                const bool bn = b.is_int() || b.is_float();
                if (an && bn) {
                    double av = a.is_int() ? static_cast<double>(a.unchecked_as_int()) : a.unchecked_as_float();
                    double bv = b.is_int() ? static_cast<double>(b.unchecked_as_int()) : b.unchecked_as_float();
                    return av < bv;
                }
                return a < b;  // total order via operator<=> (numerics have lowest type ids)
            });
        } else {
            // Custom comparator
            if (!args[0].is_function()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "sort() comparator must be a function");
            }
            const auto& comparator = args[0].unchecked_as_function();
            std::sort(tmp.begin(), tmp.end(), [&comparator](const script_value& a, const script_value& b) {
                auto result = comparator({a, b});
                if (!result) return false;
                return result.value().is_bool() && result.value().unchecked_as_bool();
            });
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

        std::string sep = "";
        if (args.size() > 0) {
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "join() argument must be a string");
            }
            sep = args[0].unchecked_as_string();
        }

        const script_array* node = self.unchecked_array_node();
        std::string result;

        bool first = true;
        for (size_t join_i = 0; join_i < node->size(); ++join_i) {
            const script_value elem = node->get(join_i, ctx.get_engine());
            if (!first) {
                result += sep;
            }
            first = false;

            // Convert element to string
            if (elem.is_string()) {
                result += elem.unchecked_as_string();
            } else if (elem.is_int()) {
                result += std::to_string(elem.unchecked_as_int());
            } else if (elem.is_float()) {
                result += std::to_string(elem.unchecked_as_float());
            } else if (elem.is_bool()) {
                result += elem.unchecked_as_bool() ? "true" : "false";
            } else if (elem.is_char()) {
                result += elem.unchecked_as_char();
            } else if (elem.is_null()) {
                result += "null";
            } else {
                result += "[object]";
            }
        }

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
            return ctx.make_value(static_cast<script_int>(self.unchecked_as_string().size()));
        }},

        {symbolizer->intern("size"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "size() takes no arguments");
            }
            return ctx.make_value(static_cast<script_int>(self.unchecked_as_string().size()));
        }},

        {symbolizer->intern("empty"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "empty() takes no arguments");
            }
            return ctx.make_value(self.unchecked_as_string().empty());
        }},

        {symbolizer->intern("at"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "at() takes exactly one argument");
            }
            if (!args[0].is_int()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "at() argument must be an integer");
            }
            const auto& str = self.unchecked_as_string();
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
            const auto& str = self.unchecked_as_string();
            if (str.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::index_out_of_bounds), "front() called on empty string");
            }
            return ctx.make_value(std::string(1, str.front()));
        }},

        {symbolizer->intern("back"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "back() takes no arguments");
            }
            const auto& str = self.unchecked_as_string();
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
            const auto& str = self.unchecked_as_string();
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
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find() first argument must be a string");
            }
            const auto& str = self.unchecked_as_string();
            const auto& search = args[0].unchecked_as_string();
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
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "rfind() first argument must be a string");
            }
            const auto& str = self.unchecked_as_string();
            const auto& search = args[0].unchecked_as_string();
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
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_first_of() first argument must be a string");
            }
            const auto& str = self.unchecked_as_string();
            const auto& chars = args[0].unchecked_as_string();
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
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_last_of() first argument must be a string");
            }
            const auto& str = self.unchecked_as_string();
            const auto& chars = args[0].unchecked_as_string();
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
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_first_not_of() first argument must be a string");
            }
            const auto& str = self.unchecked_as_string();
            const auto& chars = args[0].unchecked_as_string();
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
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "find_last_not_of() first argument must be a string");
            }
            const auto& str = self.unchecked_as_string();
            const auto& chars = args[0].unchecked_as_string();
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
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "contains() argument must be a string");
            }
            const auto& str = self.unchecked_as_string();
            const auto& search = args[0].unchecked_as_string();
            return ctx.make_value(str.find(search) != std::string::npos);
        }},

        {symbolizer->intern("starts_with"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "starts_with() takes exactly one argument");
            }
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "starts_with() argument must be a string");
            }
            const auto& str = self.unchecked_as_string();
            const auto& prefix = args[0].unchecked_as_string();
            if (prefix.size() > str.size()) {
                return ctx.make_value(false);
            }
            return ctx.make_value(str.compare(0, prefix.size(), prefix) == 0);
        }},

        {symbolizer->intern("ends_with"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "ends_with() takes exactly one argument");
            }
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "ends_with() argument must be a string");
            }
            const auto& str = self.unchecked_as_string();
            const auto& suffix = args[0].unchecked_as_string();
            if (suffix.size() > str.size()) {
                return ctx.make_value(false);
            }
            return ctx.make_value(str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0);
        }},

        {symbolizer->intern("count"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "count() takes exactly one argument");
            }
            if (!args[0].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "count() argument must be a string");
            }
            const auto& str = self.unchecked_as_string();
            const auto& search = args[0].unchecked_as_string();

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

            const auto& str = self.unchecked_as_string();
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

            const auto& str = self.unchecked_as_string();
            try {
                size_t pos = 0;
                double val = std::stod(str, &pos);
                // Check if entire string was consumed (ignoring trailing whitespace).
                // isspace requires an unsigned-char value; a negative char is UB.
                while (pos < str.size() && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
                if (pos != str.size()) {
                    return default_val;
                }
                return ctx.make_value(val);
            } catch (...) {
                return default_val;
            }
        }},

        // ============================================================
        // Mutating methods (modify in place, return self for chaining)
        // ============================================================

        {symbolizer->intern("to_lower"), [](const builtin_method_context& ctx, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
            if (!args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "to_lower() takes no arguments");
            }
            auto& str = self.unchecked_as_string_ref();
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
            auto& str = self.unchecked_as_string_ref();
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
                if (!args[0].is_string()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "trim() argument must be a string");
                }
                chars = args[0].unchecked_as_string();
            }
            if (args.size() > 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "trim() takes at most 1 argument");
            }

            auto& str = self.unchecked_as_string_ref();
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
                if (!args[0].is_string()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "trim_left() argument must be a string");
                }
                chars = args[0].unchecked_as_string();
            }
            if (args.size() > 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "trim_left() takes at most 1 argument");
            }

            auto& str = self.unchecked_as_string_ref();
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
                if (!args[0].is_string()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "trim_right() argument must be a string");
                }
                chars = args[0].unchecked_as_string();
            }
            if (args.size() > 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "trim_right() takes at most 1 argument");
            }

            auto& str = self.unchecked_as_string_ref();
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
                } else if (args[1].is_string()) {
                    const auto& pad_str = args[1].unchecked_as_string();
                    if (pad_str.empty()) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "pad_left() padding character cannot be empty");
                    }
                    pad_char = pad_str[0];
                } else {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "pad_left() second argument must be a char or string");
                }
            }

            auto& str = self.unchecked_as_string_ref();
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
                } else if (args[1].is_string()) {
                    const auto& pad_str = args[1].unchecked_as_string();
                    if (pad_str.empty()) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "pad_right() padding character cannot be empty");
                    }
                    pad_char = pad_str[0];
                } else {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "pad_right() second argument must be a char or string");
                }
            }

            auto& str = self.unchecked_as_string_ref();
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
                } else if (args[1].is_string()) {
                    const auto& pad_str = args[1].unchecked_as_string();
                    if (pad_str.empty()) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "pad_center() padding character cannot be empty");
                    }
                    pad_char = pad_str[0];
                } else {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "pad_center() second argument must be a char or string");
                }
            }

            auto& str = self.unchecked_as_string_ref();
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
            if (!args[0].is_string() || !args[1].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "replace_first() arguments must be strings");
            }
            const auto& old_str = args[0].unchecked_as_string();
            const auto& new_str = args[1].unchecked_as_string();

            if (old_str.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "replace_first() search string cannot be empty");
            }

            auto& str = self.unchecked_as_string_ref();
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
            if (!args[0].is_string() || !args[1].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "replace_last() arguments must be strings");
            }
            const auto& old_str = args[0].unchecked_as_string();
            const auto& new_str = args[1].unchecked_as_string();

            if (old_str.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "replace_last() search string cannot be empty");
            }

            auto& str = self.unchecked_as_string_ref();
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
            if (!args[0].is_string() || !args[1].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "replace_all() arguments must be strings");
            }
            const auto& old_str = args[0].unchecked_as_string();
            const auto& new_str = args[1].unchecked_as_string();

            if (old_str.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "replace_all() search string cannot be empty");
            }

            auto& str = self.unchecked_as_string_ref();
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
            if (!args[1].is_string()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "insert() second argument must be a string");
            }

            auto& str = self.unchecked_as_string_ref();
            script_int pos = args[0].unchecked_as_int();
            const auto& text = args[1].unchecked_as_string();
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

            auto& str = self.unchecked_as_string_ref();
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

            auto& str = self.unchecked_as_string_ref();
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

            auto& str = self.unchecked_as_string_ref();
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
            auto& str = self.unchecked_as_string_ref();
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
                self.unchecked_as_string_ref().clear();
                return self;
            }

            auto& str = self.unchecked_as_string_ref();
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
            if (args.size() > 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "split() takes 0 or 1 argument (delimiter)");
            }

            std::string delim = "";
            if (args.size() > 0) {
                if (!args[0].is_string()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "split() argument must be a string");
                }
                delim = args[0].unchecked_as_string();
            }

            const auto& str = self.unchecked_as_string();

            // Create array with string element type
            auto eng = ctx.get_engine();
            if (!eng) {
                return checked_result<script_value>(make_error_code(runtime_error_code::internal_error), "Engine reference expired");
            }

            script_value array_val = script_value::make_array(eng->get_type_info_string(), eng);
            auto& result = array_val.as_array();

            if (delim.empty()) {
                // Split into individual characters
                for (char c : str) {
                    result.push_back(ctx.make_value(std::string(1, c)));
                }
            } else {
                size_t start = 0;
                size_t end;
                while ((end = str.find(delim, start)) != std::string::npos) {
                    result.push_back(ctx.make_value(str.substr(start, end - start)));
                    start = end + delim.size();
                }
                result.push_back(ctx.make_value(str.substr(start)));
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
    build_index(out.array_methods, out.array_by_index, out.array_index);
    build_index(out.map_methods, out.map_by_index, out.map_index);
    build_index(out.string_methods, out.string_by_index, out.string_index);
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
    weak_ptr_methods_ = std::move(registries.weak_ptr_methods);
    shared_ptr_methods_ = std::move(registries.shared_ptr_methods);
}

// interpreter implementation

// Helper to resolve include/import paths
checked_result<std::string> resolve_include_path(const std::string& path, engine* engine_ptr) {
    // First, try the path as-is (for absolute paths)
    if (std::filesystem::exists(path)) {
        return std::filesystem::canonical(path).string();
    }

    // Get the include paths from the engine
    auto include_paths = engine_ptr->get_include_paths();

    // Try each include path
    for (const auto& include_path : include_paths) {
        auto full_path = std::filesystem::path(include_path) / path;
        if (std::filesystem::exists(full_path)) {
            return std::filesystem::canonical(full_path).string();
        }
    }

    // Path not found
    return checked_result<std::string>(make_error_code(runtime_error_code::file_not_found), "Could not find include/import file");
}

// Note: Direct engine implementation access removed
// Import tracking will be handled by the engine's public API

// Helper to create a bound method - binds 'this' as the first argument
script_value interpreter::create_bound_method(const script_value& this_obj, const script_value& method) {
    return make_bound_method(this_obj, method);
}

// Helper to check if an expression is an lvalue (existing object that should be cloned)
// Lvalues: identifiers, member access, subscript access
// Non-lvalues (temporaries): function calls, constructors, literals, operators
bool interpreter::is_lvalue_expression(expression* e) const {
    if (!e) return false;

    // Direct identifier - always an lvalue
    if (e->get_type() == node_type::identifier_expr) {
        return true;
    }

    // Member access - always an lvalue
    if (e->get_type() == node_type::member_expr) {
        return true;
    }

    // Array/map subscript (binary expr with left_bracket) - lvalue
    if (e->get_type() == node_type::binary_expr) {
        auto* bin = static_cast<binary_expr*>(e);
        if (bin->op.type == token_type::left_bracket) {
            return true;
        }
    }

    // Everything else (function calls, constructors, literals, etc.) is a temporary
    return false;
}

script_value interpreter::member_target::method(uint64_t id) const {
    if (class_def) {
        return class_def->get_method(id, false);
    }
    return script_value::make_invalid(nullptr);
}

bool interpreter::member_target::has_field(uint64_t id) const {
    return instance && instance->has_field(id);
}

const script_value& interpreter::member_target::get_field(uint64_t id) const {
    return instance->get_field(id);
}

const std::string& interpreter::member_target::class_name() const {
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
        }
    } else if (engine_) {
        target.engine_def = holder->type_id != UINT64_MAX
            ? engine_->get_class_definition(holder->type_id)
            : engine_->get_class_definition(holder->type_name);
        target.class_def = target.engine_def.get();
    }
    return target;
}

// Helper for is_truthy() to check for to_bool() method on objects
bool interpreter::object_to_bool_via_method(const script_value& value) {
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
    if (result.has_value() && result.value().is_bool()) {
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

std::optional<bool> interpreter::object_equality_via_method(const script_value& left, const script_value& right) {
    // Safe-mode workers never dispatch operator methods (increment B twin of the vm's
    // gate; handle_equal raises the verdict before the structural fallback diverges)
    if (parallel_worker_ && !engine_->allow_unsafe_parallel()) [[unlikely]] { return std::nullopt; }
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
    if (result.has_value() && result.value().is_bool()) {
        return result.value().unchecked_as_bool();
    }
    // Method didn't return a valid bool - fall back to reference comparison
    return std::nullopt;
}

// Generic helper for comparison operators (<, <=, >, >=) via custom methods
std::optional<bool> interpreter::object_comparison_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id) {
    // Safe-mode workers: no operator dispatch (increment B; type-mismatch fallback is loud)
    if (parallel_worker_ && !engine_->allow_unsafe_parallel()) [[unlikely]] { return std::nullopt; }
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
    if (result.has_value() && result.value().is_bool()) {
        return result.value().unchecked_as_bool();
    }
    return std::nullopt;
}

// Generic helper for arithmetic operators (+, -, *, /, %) via custom methods
std::optional<script_value> interpreter::object_arithmetic_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id) {
    // Safe-mode workers: no operator dispatch (increment B; type-mismatch fallback is loud)
    if (parallel_worker_ && !engine_->allow_unsafe_parallel()) [[unlikely]] { return std::nullopt; }
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
    if (result.has_value()) {
        return result.value();
    }
    return std::nullopt;
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
    if (execute_depth_ != 0) {
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
    environment_ = get_pooled_environment(environment_);  // Use pool instead of make_shared!
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

script_value interpreter::execute(const std::vector<declaration_ptr>& declarations) {
    // Reentrant execute (a host callback firing mid-script, e.g. hot reload): the
    // nested program must run at TOP LEVEL - global environment, fresh call/value
    // stacks - and restore the in-flight run's state afterwards. Without this,
    // top-level definitions land in the running frame's pooled env (and evaporate
    // with it), the outer run's value stack gets cleared, and the pool reset below
    // recycles environments the outer frames still hold.
    struct reentry_isolation {
        interpreter* self;
        bool reentrant;
        std::shared_ptr<environment> saved_env;
        std::vector<call_frame> saved_call_stack;
        script_valueStack saved_values;
        std::optional<script_value> saved_return;
        bool saved_has_return = false;

        explicit reentry_isolation(interpreter* interp) : self(interp), reentrant(interp->execute_depth_ != 0) {
            if (reentrant) {
                saved_env = self->environment_;
                saved_call_stack = std::move(self->call_stack_);
                self->call_stack_.clear();
                saved_values = std::move(self->valueStack_);
                self->valueStack_.clear();
                saved_return = std::move(self->returnValue_);
                saved_has_return = self->hasReturnValue_;
                self->environment_ = self->get_global_environment();
            }
            // Counted, not a flag (Dev ruling): a nested execute's exit decrements
            // symmetrically and can never clear the outer run's in-flight state
            ++self->execute_depth_;
        }
        ~reentry_isolation() {
            if (reentrant) {
                self->environment_ = std::move(saved_env);
                self->call_stack_ = std::move(saved_call_stack);
                self->valueStack_ = std::move(saved_values);
                self->returnValue_ = std::move(saved_return);
                self->hasReturnValue_ = saved_has_return;
            }
            --self->execute_depth_;
        }
    } isolation(this);

    script_value last_script_value = make_value();
    hasReturnValue_ = false;  // Reset return value state
    captured_trace_.clear();
    trace_captured_ = false;
    top_level_node_ = nullptr;

    for (size_t i = 0; i < declarations.size(); i++) {
        const auto& decl = declarations[i];
        // std::cerr << "  Declaration " << i << " type: " << typeid(*decl).name() << "\n";

        // Execute declaration with exception handling
        try {
            // std::cerr << "  About to visit declaration " << i << "\n";
            auto result = dispatch_decl(decl.get());
            if (!result) [[unlikely]] {
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
            auto* expr_decl = static_cast<expression_decl*>(decl.get());
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
            push_value(make_value(*storage.get<strong_ptr<script_string>>()));
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
    if (expr->symbol_id == getValue_id_) {
    }
    // Check if this identifier is the current catch variable (fast symbol_id comparison)
    if (current_catch_var_id_ != 0 && expr->symbol_id == current_catch_var_id_) {
        push_value(active_exception_value_.value());
        return checked_result<void>();
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

                    // Check instance fields first
                    if (instance->has_field(name_id)) {
                        push_value(instance->get_field(name_id));
                        return checked_result<void>();
                    }

                    // Check for methods (returns bound method)
                    script_value method = instance->get_method(name_id, false);
                    if (!method.is_invalid()) {
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
            "Undefined variable '{0}'", name_id);
    }
    return checked_result<void>();
}

checked_result<void> interpreter::visit_binary_expr(binary_expr* expr) {
    // Track if we've pre-fetched values to avoid duplicate lookups
    bool already_have_values = false;
    std::optional<script_value> pre_fetched_left, pre_fetched_right;

	// FAST PATH: identifier + identifier (e.g., "a + b", "sum + i") - eliminates 2 virtual calls
	// Skip logical operators - they need short-circuit evaluation
	// Skip if we're in a catch block - catch variables need special handling
	if (expr->op.type != token_type::ampersand_ampersand && expr->op.type != token_type::pipe_pipe && current_catch_var_id_ == 0) {
		if (expr->left->get_type() == node_type::identifier_expr) {
			auto* leftId = static_cast<identifier_expr*>(expr->left.get());
			if (expr->right->get_type() == node_type::identifier_expr) {
				auto* rightId = static_cast<identifier_expr*>(expr->right.get());
				// Both operands are simple identifiers - direct variable lookup without AST traversal
				auto leftResult = resolve_variable_required(leftId->slot_index, leftId->symbol_id);
				if (!leftResult) return leftResult.error_value();
				const script_value& leftVal = leftResult.value()->deref();

				auto rightResult = resolve_variable_required(rightId->slot_index, rightId->symbol_id);
				if (!rightResult) return rightResult.error_value();
				const script_value& rightVal = rightResult.value()->deref();

				// Fast path for integer arithmetic (most common in loops)
				if (can_use_fast_path(expr->op.type)) {
					// Gate on STORAGE, not declared type: a typed-but-uninitialized variable
					// (`int x;`) holds monostate and unchecked_* access would deref null.
					const size_t leftIdx = leftVal.raw_storage_index();
					const size_t rightIdx = rightVal.raw_storage_index();

					if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_INT) {
						script_int leftInt = leftVal.unchecked_as_int();
						script_int rightInt = rightVal.unchecked_as_int();

						switch (expr->op.type) {
						case token_type::plus:
							{ script_int rr; if (!ints::try_add(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '+'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::minus:
							{ script_int rr; if (!ints::try_sub(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '-'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::star:
							{ script_int rr; if (!ints::try_mul(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '*'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::slash:
							if (rightInt == 0) {
								return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation");
							}
							{ script_int rr; if (!ints::try_div(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '/'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::percent:
							if (rightInt == 0) {
								return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation");
							}
							push_value(make_int_fast(ints::mod(leftInt, rightInt)));
							return {};
						case token_type::less:
							push_value(make_bool_fast(leftInt < rightInt));
							return {};
						case token_type::less_equal:
							push_value(make_bool_fast(leftInt <= rightInt));
							return {};
						case token_type::greater:
							push_value(make_bool_fast(leftInt > rightInt));
							return {};
						case token_type::greater_equal:
							push_value(make_bool_fast(leftInt >= rightInt));
							return {};
						case token_type::equal_equal:
							push_value(make_bool_fast(leftInt == rightInt));
							return {};
						case token_type::bang_equal:
							push_value(make_bool_fast(leftInt != rightInt));
							return {};
						default:
							break; // Fall through to normal path
						}
					}
					// Fast path for float/mixed arithmetic
					else if ((leftIdx == script_value::TYPEID_INT || leftIdx == script_value::TYPEID_FLOAT) &&
						(rightIdx == script_value::TYPEID_INT || rightIdx == script_value::TYPEID_FLOAT)) {
						script_float leftFloat = leftIdx == script_value::TYPEID_INT ?
							static_cast<script_float>(leftVal.unchecked_as_int()) : leftVal.unchecked_as_float();
						script_float rightFloat = rightIdx == script_value::TYPEID_INT ?
							static_cast<script_float>(rightVal.unchecked_as_int()) : rightVal.unchecked_as_float();

						switch (expr->op.type) {
						case token_type::plus:
							push_value(make_float_fast(leftFloat + rightFloat));
							return {};
						case token_type::minus:
							push_value(make_float_fast(leftFloat - rightFloat));
							return {};
						case token_type::star:
							push_value(make_float_fast(leftFloat * rightFloat));
							return {};
						case token_type::slash:
							if (rightFloat == 0.0) {
								return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in float operation");
							}
							push_value(make_float_fast(leftFloat / rightFloat));
							return {};
						case token_type::percent:
							if (rightFloat == 0.0) {
								return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in float operation");
							}
							push_value(make_float_fast(std::fmod(leftFloat, rightFloat)));
							return {};
						case token_type::less:
							push_value(make_bool_fast(leftFloat < rightFloat));
							return {};
						case token_type::less_equal:
							push_value(make_bool_fast(leftFloat <= rightFloat));
							return {};
						case token_type::greater:
							push_value(make_bool_fast(leftFloat > rightFloat));
							return {};
						case token_type::greater_equal:
							push_value(make_bool_fast(leftFloat >= rightFloat));
							return {};
						case token_type::equal_equal:
							push_value(make_bool_fast(leftFloat == rightFloat));
							return {};
						case token_type::bang_equal:
							push_value(make_bool_fast(leftFloat != rightFloat));
							return {};
						default:
							break;
						}
					}
					// Fast path for string concatenation (common operation)
					else if (expr->op.type == token_type::plus &&
						leftIdx == script_value::TYPEID_STRING &&
						rightIdx == script_value::TYPEID_STRING) {
						const script_string& leftStr = leftVal.unchecked_as_string();
						const script_string& rightStr = rightVal.unchecked_as_string();
						// engine::memory_cap chokepoint: deny the concat result before it exists
						if (!limits_->memory_charge(sizeof(script_value) + leftStr.size() + rightStr.size())) [[unlikely]] {
							return detail::raise_memory_cap(*limits_);
						}
						push_value(make_value(leftStr + rightStr));
						return {};
					}
					// cpp-bound operands (index 14) skip the gates above: keep the zero-divisor
					// error surface identical to this shape's plain fast path (twin parity)
					else if (leftIdx == script_value::TYPEID_CPP_BOUND || rightIdx == script_value::TYPEID_CPP_BOUND) {
						if (auto z = bound_fastpath_zero_divisor(expr->op.type, leftVal, rightVal, true, true)) {
							return checked_result<void>(make_error_code(z->code), z->text);
						}
					}
				}

				// Fast path didn't handle it - copy values for fallback dispatch
				pre_fetched_left = leftVal;
				pre_fetched_right = rightVal;
				already_have_values = true;
			}
			// FAST PATH 2: identifier + literal (e.g., "i < 100", "x + 5") - most common loop condition!
			else if (expr->right->get_type() == node_type::literal_expr) {
				auto* rightLit = static_cast<literal_expr*>(expr->right.get());
				auto leftResult = resolve_variable_required(leftId->slot_index, leftId->symbol_id);
				if (!leftResult) return leftResult.error_value();
				const script_value& leftVal = leftResult.value()->deref();
				const script_value& rightVal = rightLit->value;  // Direct access - no lookup!

				// Ultra-fast integer path (most common for loop conditions)
				if (can_use_fast_path(expr->op.type)) {
					// Use raw_storage_index for fastest type check
					size_t leftIdx = leftVal.raw_storage_index();
					size_t rightIdx = rightVal.raw_storage_index();

					if (leftIdx == 1 && rightIdx == 1) {  // Both ints
						script_int leftInt = leftVal.unchecked_as_int();
						script_int rightInt = rightVal.unchecked_as_int();

						switch (expr->op.type) {
						case token_type::less:
							push_value(make_bool_fast(leftInt < rightInt));
							return {};
						case token_type::less_equal:
							push_value(make_bool_fast(leftInt <= rightInt));
							return {};
						case token_type::greater:
							push_value(make_bool_fast(leftInt > rightInt));
							return {};
						case token_type::greater_equal:
							push_value(make_bool_fast(leftInt >= rightInt));
							return {};
						case token_type::equal_equal:
							push_value(make_bool_fast(leftInt == rightInt));
							return {};
						case token_type::bang_equal:
							push_value(make_bool_fast(leftInt != rightInt));
							return {};
						case token_type::plus:
							{ script_int rr; if (!ints::try_add(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '+'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::minus:
							{ script_int rr; if (!ints::try_sub(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '-'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::star:
							{ script_int rr; if (!ints::try_mul(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '*'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::slash:
							if (rightInt == 0) return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation");
							{ script_int rr; if (!ints::try_div(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '/'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::percent:
							if (rightInt == 0) return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation");
							push_value(make_int_fast(ints::mod(leftInt, rightInt)));
							return {};
						default:
							break;
						}
					}
					// Float/mixed numeric path
					else if ((leftIdx == 1 || leftIdx == 2) && (rightIdx == 1 || rightIdx == 2)) {
						script_float leftFloat = leftIdx == 1 ?
							static_cast<script_float>(leftVal.unchecked_as_int()) : leftVal.unchecked_as_float();
						script_float rightFloat = rightIdx == 1 ?
							static_cast<script_float>(rightVal.unchecked_as_int()) : rightVal.unchecked_as_float();

						switch (expr->op.type) {
						case token_type::less:
							push_value(make_bool_fast(leftFloat < rightFloat));
							return {};
						case token_type::less_equal:
							push_value(make_bool_fast(leftFloat <= rightFloat));
							return {};
						case token_type::greater:
							push_value(make_bool_fast(leftFloat > rightFloat));
							return {};
						case token_type::greater_equal:
							push_value(make_bool_fast(leftFloat >= rightFloat));
							return {};
						case token_type::plus:
							push_value(make_float_fast(leftFloat + rightFloat));
							return {};
						case token_type::minus:
							push_value(make_float_fast(leftFloat - rightFloat));
							return {};
						case token_type::star:
							push_value(make_float_fast(leftFloat * rightFloat));
							return {};
						case token_type::slash:
							if (rightFloat == 0.0) return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in float operation");
							push_value(make_float_fast(leftFloat / rightFloat));
							return {};
						case token_type::percent:
							if (rightFloat == 0.0) return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in float operation");
							push_value(make_float_fast(std::fmod(leftFloat, rightFloat)));
							return {};
						default:
							break;
						}
					}
					// twin parity for bound operands: float / and % are covered inline in this
					// shape (vm op_binary_fused), so bound pairs keep the same error surface
					else if (leftIdx == script_value::TYPEID_CPP_BOUND || rightIdx == script_value::TYPEID_CPP_BOUND) {
						if (auto z = bound_fastpath_zero_divisor(expr->op.type, leftVal, rightVal, true, true)) {
							return checked_result<void>(make_error_code(z->code), z->text);
						}
					}
				}
				// Fast path didn't fully handle - copy for slow path (literal needs hydration)
				pre_fetched_left = leftVal;
				pre_fetched_right = rightVal.has_valid_engine() ? rightVal : hydrate_literal(rightVal);
				already_have_values = true;
			}
		}
		// FAST PATH 3: literal + identifier (e.g., "100 > i", "5 + x")
		else if (expr->left->get_type() == node_type::literal_expr) {
			auto* leftLit = static_cast<literal_expr*>(expr->left.get());
			if (expr->right->get_type() == node_type::identifier_expr) {
				auto* rightId = static_cast<identifier_expr*>(expr->right.get());
				const script_value& leftVal = leftLit->value;  // Direct access!
				auto rightResult = resolve_variable_required(rightId->slot_index, rightId->symbol_id);
				if (!rightResult) return rightResult.error_value();
				script_value rightVal = rightResult.value()->deref();

				// Ultra-fast integer path
				if (can_use_fast_path(expr->op.type)) {
					size_t leftIdx = leftVal.raw_storage_index();
					size_t rightIdx = rightVal.raw_storage_index();

					if (leftIdx == 1 && rightIdx == 1) {  // Both ints
						script_int leftInt = leftVal.unchecked_as_int();
						script_int rightInt = rightVal.unchecked_as_int();

						switch (expr->op.type) {
						case token_type::less:
							push_value(make_bool_fast(leftInt < rightInt));
							return {};
						case token_type::less_equal:
							push_value(make_bool_fast(leftInt <= rightInt));
							return {};
						case token_type::greater:
							push_value(make_bool_fast(leftInt > rightInt));
							return {};
						case token_type::greater_equal:
							push_value(make_bool_fast(leftInt >= rightInt));
							return {};
						case token_type::equal_equal:
							push_value(make_bool_fast(leftInt == rightInt));
							return {};
						case token_type::bang_equal:
							push_value(make_bool_fast(leftInt != rightInt));
							return {};
						case token_type::plus:
							{ script_int rr; if (!ints::try_add(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '+'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::minus:
							{ script_int rr; if (!ints::try_sub(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '-'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::star:
							{ script_int rr; if (!ints::try_mul(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '*'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::slash:
							if (rightInt == 0) return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation");
							{ script_int rr; if (!ints::try_div(leftInt, rightInt, rr)) return int_overflow_v("Integer overflow in '/'"); push_value(make_int_fast(rr)); }
							return {};
						case token_type::percent:
							if (rightInt == 0) return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation");
							push_value(make_int_fast(ints::mod(leftInt, rightInt)));
							return {};
						default:
							break;
						}
					}
					// Float/mixed numeric path (vm op_binary_fused parity: informative
					// zero-divisor texts live on this shape's fast-path surface too)
					else if ((leftIdx == 1 || leftIdx == 2) && (rightIdx == 1 || rightIdx == 2)) {
						script_float leftFloat = leftIdx == 1 ?
							static_cast<script_float>(leftVal.unchecked_as_int()) : leftVal.unchecked_as_float();
						script_float rightFloat = rightIdx == 1 ?
							static_cast<script_float>(rightVal.unchecked_as_int()) : rightVal.unchecked_as_float();

						switch (expr->op.type) {
						case token_type::less:
							push_value(make_bool_fast(leftFloat < rightFloat));
							return {};
						case token_type::less_equal:
							push_value(make_bool_fast(leftFloat <= rightFloat));
							return {};
						case token_type::greater:
							push_value(make_bool_fast(leftFloat > rightFloat));
							return {};
						case token_type::greater_equal:
							push_value(make_bool_fast(leftFloat >= rightFloat));
							return {};
						case token_type::plus:
							push_value(make_float_fast(leftFloat + rightFloat));
							return {};
						case token_type::minus:
							push_value(make_float_fast(leftFloat - rightFloat));
							return {};
						case token_type::star:
							push_value(make_float_fast(leftFloat * rightFloat));
							return {};
						case token_type::slash:
							if (rightFloat == 0.0) return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in float operation");
							push_value(make_float_fast(leftFloat / rightFloat));
							return {};
						case token_type::percent:
							if (rightFloat == 0.0) return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in float operation");
							push_value(make_float_fast(std::fmod(leftFloat, rightFloat)));
							return {};
						default:
							break;
						}
					}
					// twin parity for bound operands: float / and % are covered inline in this
					// shape (vm op_binary_fused), so bound pairs keep the same error surface
					else if (leftIdx == script_value::TYPEID_CPP_BOUND || rightIdx == script_value::TYPEID_CPP_BOUND) {
						if (auto z = bound_fastpath_zero_divisor(expr->op.type, leftVal, rightVal, true, true)) {
							return checked_result<void>(make_error_code(z->code), z->text);
						}
					}
				}
				// Fast path didn't fully handle - save for slow path (literal needs hydration)
				pre_fetched_left = leftVal.has_valid_engine() ? leftVal : hydrate_literal(leftVal);
				pre_fetched_right = std::move(rightVal);
				already_have_values = true;
			}
		}
	}

    // Handle logical operators specially for short-circuit evaluation
    // Return proper boolean values (not JavaScript-style operand values)
    if (expr->op.type == token_type::ampersand_ampersand || expr->op.type == token_type::pipe_pipe) {
        JAISCRIPT_TRY(dispatch_expr(expr->left.get()));
        script_value left = pop_value();

        bool leftTruthy = is_truthy(left);

        if (expr->op.type == token_type::ampersand_ampersand) {
            if (!leftTruthy) {
                push_value(make_value(false));  // Short-circuit: left is falsy, return false
                return {};
            }
            // Left is truthy, evaluate right and return its boolean value
            JAISCRIPT_TRY(dispatch_expr(expr->right.get()));
            script_value right = pop_value();
            push_value(make_value(is_truthy(right)));
            return {};
        } else { // pipe_pipe
            if (leftTruthy) {
                push_value(make_value(true));  // Short-circuit: left is truthy, return true
                return {};
            }
            // Left is falsy, evaluate right and return its boolean value
            JAISCRIPT_TRY(dispatch_expr(expr->right.get()));
            script_value right = pop_value();
            push_value(make_value(is_truthy(right)));
            return {};
        }
    }

    // Capture whether THIS binary expression is the outermost assignment-target
    // subscript, then clear the flag so any nested subscripts evaluated while
    // computing the operands below are treated as reads (no map auto-insert).
    const bool want_lvalue_write = lvalue_target_context_;
    lvalue_target_context_ = false;

    // Evaluate operands once and use them throughout (or use pre-fetched values)
    std::optional<script_value> left_raw_opt, left_opt, right_opt;

    if (already_have_values) {
        // Use pre-fetched values from fast path (already dereferenced)
        left_opt = std::move(*pre_fetched_left);
        left_raw_opt = *left_opt;  // Already dereferenced
        right_opt = std::move(*pre_fetched_right);
    } else {
        // Normal path: evaluate via AST traversal
        JAISCRIPT_TRY(dispatch_expr(expr->left.get()));
        left_raw_opt = pop_value();  // Keep raw value for subscript handling
        left_opt = left_raw_opt->deref();  // Dereferenced version for most operations

        JAISCRIPT_TRY(dispatch_expr(expr->right.get()));
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
                    // insert a missing key — reading must not grow the map.
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
            // Read-only char subscript (C++-familiar s[i]). Writes stay an error:
            // strings share storage under copy, so subscript writes would need
            // copy-on-write - future work, a clear error instead of a silent trap.
            // (KEEP BYTE-PARALLEL with vm_backend::exec_index)
            if (!right.is_int()) {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_index_type), "String index must be an integer");
            }
            if (want_lvalue_write) {
                return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                    "Strings are read-only through subscript: use substr()/+ to build a new string");
            }
            const auto& str = left.unchecked_as_string();
            script_int index = right.unchecked_as_int();
            if (index < 0 || index >= static_cast<script_int>(str.size())) {
                return checked_result<void>(make_error_code(runtime_error_code::index_out_of_bounds),
                    "String index {0} out of bounds for string of size {1}",
                    string_symbolizer_->intern(std::to_string(index)),
                    string_symbolizer_->intern(std::to_string(str.size())));
            }
            push_value(script_value(static_cast<script_char>(str[static_cast<size_t>(index)]), engine_));
        } else {
            if (left.is_object()) {
                // Safe-mode workers: operator[]/global-operator dispatch copies shared
                // method values — verdict (increment B; KEEP BYTE-PARALLEL with the vm)
                if (parallel_worker_ && !engine_->allow_unsafe_parallel()) [[unlikely]] {
                    return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                        "custom operator dispatch on class instances is not admitted in a parallel body (engine::allow_unsafe_parallel(true) overrides)");
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
checked_result<void> interpreter::visit_unary_expr(unary_expr* expr) {
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
    JAISCRIPT_TRY(dispatch_expr(expr->operand.get()));
    // Operand raised a script exception (e.g. ++obj.missing member read): propagate it
    // instead of clobbering it with invalid_assignment_target below (vm parity)
    if (is_unwinding_) {
        return {};
    }
    script_value operand = pop_value();

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
            } else {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand), "Unary minus requires numeric operand");
            }
            break;
        }

        case token_type::bang:
            push_value(make_value(!is_truthy(operand)));
            break;

        case token_type::tilde:
            // Bitwise NOT
            if (oi != script_value::TYPEID_INT) {
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
                script_value* varPtr = resolve_local_or_env(identifier->slot_index, identifier->symbol_id);
                if (varPtr) {
                    // TYPED array element ref: deref hands back the holder scratch - the
                    // in-place mutation below must commit it to the raw buffer (KEEP
                    // BYTE-PARALLEL with the VM exec_incdec twin)
                    auto* typed_elem = varPtr->typed_element_holder();
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
                        return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand));
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
                            if (typed_elem) [[unlikely]] { typed_elem->commit_typed_element_scratch(); }
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
                            if (typed_elem) [[unlikely]] { typed_elem->commit_typed_element_scratch(); }
                            return {};
                        }
                        default:
                            return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand));
                    }
                } else {
                    // Fallback: identifier is an implicit this.member access
                    // Check if 'this' exists and has this field
                    script_value* this_ptr = environment_->get_value_ptr(string_symbolizer_->get_this_id());
                    if (this_ptr && this_ptr->is_object()) {
                        script_value& this_val = *this_ptr;
                        std::shared_ptr<class_instance> instance = this_val.get_class_instance();

                        if (instance && instance->has_field(identifier->symbol_id)) {
                            script_value currentVal = instance->get_field(identifier->symbol_id);
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
                                instance->set_field(identifier->symbol_id, make_value(newVal));
                                push_value(make_value(expr->is_postfix ? oldVal : newVal));
                                return {};
                            } else if (ti == script_value::TYPEID_FLOAT) {
                                script_float oldVal = currentVal.unchecked_as_float();
                                script_float newVal = isIncrement ? oldVal + 1.0 : oldVal - 1.0;
                                instance->set_field(identifier->symbol_id, make_value(newVal));
                                push_value(make_value(expr->is_postfix ? oldVal : newVal));
                                return {};
                            } else {
                                return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand));
                            }
                        }
                    }
                    uint64_t name_id = string_symbolizer_->intern(identifier->name);
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        "Undefined variable '{0}'", name_id);
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

checked_result<void> interpreter::assign_member_value(const script_value& objectValue, member_expr* memberExpr, const script_value& value) {
    // Worker direct-field store (parallel_for v1): the setter probe below COPIES a
    // shared method value (cross-worker count race); a backing field's enforce+set is
    // exactly what the auto-setter does, on exclusively-owned instance storage. Any
    // non-field member store needs method machinery - wall it.
    if (parallel_worker_ && objectValue.raw_storage_index() == script_value::TYPEID_OBJECT &&
        !engine_->allow_unsafe_parallel()) [[unlikely]] {
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
                "only direct field stores on class instances are allowed in a parallel body (engine::allow_unsafe_parallel(true) overrides)");
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
    script_value setter = target.method(setter_id);
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
        std::string member_str(memberExpr->member);
        active_exception_value_ = make_value("Cannot assign to non-existent member '" + member_str + "'");
        current_exception_ = script_exception("Cannot assign to non-existent member '" + member_str + "'", memberExpr->location);
        is_unwinding_ = true;
        return {};
    }
    return {};
}

checked_result<void> interpreter::visit_assignment_expr(assignment_expr* expr) {

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
            script_value* varPtr = resolve_local_or_env(identifier->slot_index, identifier->symbol_id);
            if (varPtr) {
                // Constrained element/field ref (Tier 1 bind): route through the shared
                // helper so the compound result honors the constraint like subscript
                // compounds do (vm parity)
                if (varPtr->is_reference()) {
                    const auto* refHolder = varPtr->get_reference_holder();
                    if (refHolder && refHolder->container_element_type) {
                        JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
                        if (is_unwinding_) {
                            push_value(make_value());
                            return {};
                        }
                        script_value rightValue = pop_value();
                        token_type op;
                        const char* opName;
                        switch (expr->op.type) {
                            case token_type::minus_equal: op = token_type::minus; opName = "-"; break;
                            case token_type::star_equal: op = token_type::star; opName = "*"; break;
                            case token_type::slash_equal: op = token_type::slash; opName = "/"; break;
                            case token_type::percent_equal: op = token_type::percent; opName = "%"; break;
                            default: op = token_type::plus; opName = "+"; break;
                        }
                        auto result = detail::ref_compound_store_constrained(*varPtr, rightValue, op, opName,
                            operator_table_, engine_, string_symbolizer_,
                            [this](const script_value& l, token_type o, const script_value& r) {
                                return evaluate_arithmetic(l, o, r);
                            });
                        if (!result) {
                            return result.error_value();
                        }
                        push_value(std::move(result.value()));
                        return {};
                    }
                }
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
                        script_value* rhs_ptr = resolve_local_or_env(rhs_id->slot_index, rhs_id->symbol_id);
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
                                    script_value* left_ptr = resolve_local_or_env(left_id->slot_index, left_id->symbol_id);
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

                // Evaluate the right-hand side (general path)
                JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
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

                // Check for custom operators first (rare path; flat table, no env probe)
                if (has_custom_numeric_ops_) [[unlikely]] {
                    detail::op_slot op_slot = detail::op_slot::none;
                    switch (expr->op.type) {
                        case token_type::plus_equal: op_slot = detail::op_slot::plus; break;
                        case token_type::minus_equal: op_slot = detail::op_slot::minus; break;
                        case token_type::star_equal: op_slot = detail::op_slot::star; break;
                        default: break;
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
                    switch (expr->op.type) {
                        case token_type::plus_equal: op_symbol_id = op_plus_id_; break;
                        case token_type::minus_equal: op_symbol_id = op_minus_id_; break;
                        case token_type::star_equal: op_symbol_id = op_star_id_; break;
                        case token_type::slash_equal: op_symbol_id = op_slash_id_; break;
                        case token_type::percent_equal: op_symbol_id = op_percent_id_; break;
                        default: break;
                    }
                    if (op_symbol_id != 0) {
                        auto custom_result = object_arithmetic_via_method(target, rightValue, op_symbol_id);
                        if (custom_result.has_value()) {
                            target = std::move(custom_result.value());
                            push_value(expression_result_needed_ ? target.clone() : target);
                            return {};
                        }
                    }
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
                                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                                }
                            } else if (target.is_float()) {
                                target.assign_through(make_value(target.unchecked_as_float() + derefRight.as_float()));
                            } else if (target.is_string() && rIdx == script_value::TYPEID_STRING) {
                                // engine::memory_cap chokepoint: deny the append before it exists
                                if (!limits_->memory_charge(derefRight.unchecked_as_string().size())) [[unlikely]] {
                                    return detail::raise_memory_cap(*limits_);
                                }
                                target.unchecked_as_string_ref() += derefRight.unchecked_as_string();
                            } else {
                                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
                                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                                }
                            } else if (target.is_float()) {
                                target.assign_through(make_value(target.unchecked_as_float() - derefRight.as_float()));
                            } else {
                                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
                                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                                }
                            } else if (target.is_float()) {
                                target.assign_through(make_value(target.unchecked_as_float() * derefRight.as_float()));
                            } else {
                                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
                                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                                }
                            } else if (target.is_float()) {
                                target.assign_through(make_value(target.unchecked_as_float() / derefRight.as_float()));
                            } else {
                                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
                // Integral promotion (char_promotion.hpp): a char operand leaves the in-place
                // ladder and takes the general arithmetic path as int64 0..255, storing back
                // like the %= case does. KEEP BYTE-PARALLEL with the VM exec_compound_store.
                if (detail::char_operands_promote(leftIdx, rightIdx)) [[unlikely]] {
                    token_type baseOp = token_type::plus;
                    switch (expr->op.type) {
                        case token_type::plus_equal: baseOp = token_type::plus; break;
                        case token_type::minus_equal: baseOp = token_type::minus; break;
                        case token_type::star_equal: baseOp = token_type::star; break;
                        case token_type::slash_equal: baseOp = token_type::slash; break;
                        case token_type::percent_equal: baseOp = token_type::percent; break;
                        default: return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));
                    }
                    JAISCRIPT_TRY_ASSIGN(script_value promoted, evaluate_arithmetic(target, baseOp, derefRight));
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
                            target.unchecked_as_float_ref() += derefRight.as_float();
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
                            JAISCRIPT_TRY_ASSIGN(script_value joined, evaluate_arithmetic(target, token_type::plus, derefRight));
                            JAISCRIPT_TRY(compound_typed_store_back(target, std::move(joined), identifier->name));
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
                            target.unchecked_as_float_ref() -= derefRight.as_float();
                        } else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
                            JAISCRIPT_TRY(compound_typed_store_back(target, make_value(target.unchecked_as_int() - derefRight.unchecked_as_float()), identifier->name));
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
                            target.unchecked_as_float_ref() *= derefRight.as_float();
                        } else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
                            JAISCRIPT_TRY(compound_typed_store_back(target, make_value(target.unchecked_as_int() * derefRight.unchecked_as_float()), identifier->name));
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
                            target.unchecked_as_float_ref() /= derefRight.as_float();
                        } else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
                            JAISCRIPT_TRY(compound_typed_store_back(target, make_value(target.unchecked_as_int() / derefRight.unchecked_as_float()), identifier->name));
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                        }
                        break;
                    }

                    case token_type::percent_equal: {
                        if (rightIdx == script_value::TYPEID_INT && derefRight.unchecked_as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Modulo by zero");
                        }
                        JAISCRIPT_TRY_ASSIGN(script_value promoted, evaluate_arithmetic(target, token_type::percent, derefRight));
                        JAISCRIPT_TRY(compound_typed_store_back(target, std::move(promoted), identifier->name));
                        break;
                    }

                    default:
                        return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));
                }

                push_value(expression_result_needed_ ? target.clone() : target);
            } else {
                // VM parity (C++17 sequencing): the rhs of a compound assignment is
                // evaluated BEFORE the target resolves, so rhs errors and side effects
                // (e.g. an undefined rhs identifier) surface first
                JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
                if (is_unwinding_) {
                    push_value(make_value());
                    return {};
                }
                script_value rightValue = pop_value();

                // Fallback: identifier is an implicit this.member access
                // Check if 'this' exists and has this field
                script_value* this_ptr = environment_->get_value_ptr(string_symbolizer_->get_this_id());
                if (!this_ptr || !this_ptr->is_object()) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        "Undefined variable '{0}' (no 'this' in scope)", identifier->symbol_id);
                }

                script_value& this_val = *this_ptr;
                std::shared_ptr<class_instance> instance = this_val.get_class_instance();

                if (!instance || !instance->has_field(identifier->symbol_id)) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        "Undefined variable '{0}' (not a field of 'this')", identifier->symbol_id);
                }

                // Get current field value
                script_value currentValue = instance->get_field(identifier->symbol_id);

                // Check for custom arithmetic operators on class instances
                if (currentValue.is_object()) {
                    uint64_t op_symbol_id = 0;
                    switch (expr->op.type) {
                        case token_type::plus_equal: op_symbol_id = op_plus_id_; break;
                        case token_type::minus_equal: op_symbol_id = op_minus_id_; break;
                        case token_type::star_equal: op_symbol_id = op_star_id_; break;
                        case token_type::slash_equal: op_symbol_id = op_slash_id_; break;
                        case token_type::percent_equal: op_symbol_id = op_percent_id_; break;
                        default: break;
                    }
                    if (op_symbol_id != 0) {
                        auto custom_result = object_arithmetic_via_method(currentValue, rightValue, op_symbol_id);
                        if (custom_result.has_value()) {
                            auto enforced = instance->enforce_field_write(identifier->symbol_id, clone_for_assignment(custom_result.value()));
                            if (!enforced) {
                                return enforced.error_value();
                            }
                            instance->set_field_unchecked(identifier->symbol_id, enforced.value());
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
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                        }
                        break;
                    case token_type::slash_equal:
                        if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                        }
                        if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
                            (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
                            script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
                            script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
                            resultValue = make_value(cf / rf);
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                        }
                        break;
                    case token_type::percent_equal: {
                        if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Modulo by zero");
                        }
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::percent, rightValue));
                        break;
                    }
                    default:
                        return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));
                }

                // Store the enforced (declared-type converted) value; the expression result
                // stays the promoted value, matching member/element compound semantics
                auto enforced = instance->enforce_field_write(identifier->symbol_id, clone_for_assignment(resultValue));
                if (!enforced) {
                    return enforced.error_value();
                }
                instance->set_field_unchecked(identifier->symbol_id, enforced.value());
                push_value(std::move(resultValue));
            }
        } else if (expr->target->get_type() == node_type::member_expr) {
            auto* memberExpr = static_cast<member_expr*>(expr->target.get());
            // Handle compound assignment to member expression (e.g., obj.value += 10)
            // First, get the current value of the property
            JAISCRIPT_TRY(dispatch_expr(memberExpr));
            script_value currentValue = pop_value().deref();

            // Evaluate the right-hand side
            JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
            if (is_unwinding_) {
                // RHS raised: surface the original exception, don't run the compound
                // arithmetic on the placeholder (KEEP BYTE-PARALLEL with the vm)
                push_value(make_value());
                return {};
            }
            script_value rightValue = pop_value();

            // Check for custom arithmetic operators on class instances
            if (currentValue.is_object()) {
                uint64_t op_symbol_id = 0;
                switch (expr->op.type) {
                    case token_type::plus_equal: op_symbol_id = op_plus_id_; break;
                    case token_type::minus_equal: op_symbol_id = op_minus_id_; break;
                    case token_type::star_equal: op_symbol_id = op_star_id_; break;
                    case token_type::slash_equal: op_symbol_id = op_slash_id_; break;
                    case token_type::percent_equal: op_symbol_id = op_percent_id_; break;
                    default: break;
                }
                if (op_symbol_id != 0) {
                    auto custom_result = object_arithmetic_via_method(currentValue, rightValue, op_symbol_id);
                    if (custom_result.has_value()) {
                        // Assign the result back to the property
                        JAISCRIPT_TRY(dispatch_expr(memberExpr->object.get()));
                        script_value objectValue = pop_value().deref();
                        if (objectValue.is_object()) {
                            JAISCRIPT_TRY(assign_member_value(objectValue, memberExpr, custom_result.value()));
                            if (is_unwinding_) {
                                push_value(make_value());
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
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Invalid operands for +=");
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
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Invalid operands for -=");
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
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Invalid operands for *=");
                    }
                    break;
                }
                case token_type::slash_equal: {
                    if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
                        return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                            "Division by zero");
                    } else if (ri == script_value::TYPEID_FLOAT && rightValue.unchecked_as_float() == 0.0) {
                        return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                            "Division by zero");
                    }
                    if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
                        (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
                        script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
                        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
                        resultValue = make_value(cf / rf);
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Invalid operands for /=");
                    }
                    break;
                }
                case token_type::percent_equal: {
                    if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
                        return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                            "Modulo by zero");
                    }
                    JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::percent, rightValue));
                    break;
                }
                default:
                    return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                        "Unsupported compound assignment operator");
            }
            
            // Now assign the result back to the property
            // We need to evaluate the object again to get a fresh reference
            JAISCRIPT_TRY(dispatch_expr(memberExpr->object.get()));
            script_value objectValue = pop_value().deref();

            // Check if it's an object
            if (!objectValue.is_object()) {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to member of non-object type");
                current_exception_ = script_exception("Cannot assign to member of non-object type", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return {};
            }

            JAISCRIPT_TRY(assign_member_value(objectValue, memberExpr, resultValue));
            if (is_unwinding_) {
                push_value(make_value());
                return {};
            }

            push_value(std::move(resultValue));
        } else {
            // Numeric subscript-compound fast path (a[i] op= v, the GLOOM store shape):
            // compute + write in place - no reference mint, no synthetic assignment
            // replay. Bails to the general path below for ANY shape it can't prove
            // identical (semantics + error text stay the general path's). Sound to bail
            // AFTER probing because every probed expression is side-effect-free: base
            // ident, index ident/int-literal, and a callout_free RHS re-evaluate safely.
            if (expr->target->get_type() == node_type::binary_expr && !has_custom_numeric_ops_) {
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
                    script_value* base_ptr = resolve_local_or_env(base_ident->slot_index, base_ident->symbol_id);
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
                            if (script_value* idx_ptr = resolve_local_or_env(idx_ident->slot_index, idx_ident->symbol_id)) {
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
                            // RHS after target probing (general path's order); callout_free
                            JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
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
                                JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(*target_ptr, op, rhs));
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

            // First, evaluate the target expression to get current value
            JAISCRIPT_TRY(dispatch_expr(expr->target.get()));
            script_value currentValue = pop_value();

            // Evaluate the right-hand side
            JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
            if (is_unwinding_) {
                // RHS raised: surface the original exception, don't run the compound
                // arithmetic on the placeholder (KEEP BYTE-PARALLEL with the vm)
                push_value(make_value());
                return {};
            }
            script_value rightValue = pop_value();

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
                default: break;
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
                        if (currentValue.is_string() || rightValue.is_string()) {
                            resultValue = make_value(currentValue.to_string() + rightValue.to_string());
                        } else {
                            JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::plus, rightValue));
                        }
                        break;
                    }
                    case token_type::minus_equal: {
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::minus, rightValue));
                        break;
                    }
                    case token_type::star_equal: {
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::star, rightValue));
                        break;
                    }
                    case token_type::slash_equal: {
                        const size_t ri = rightValue.raw_storage_index();
                        if ((ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) ||
                            (ri == script_value::TYPEID_FLOAT && rightValue.unchecked_as_float() == 0.0)) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                                "Division by zero");
                        }
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::slash, rightValue));
                        break;
                    }
                    case token_type::percent_equal: {
                        if (rightValue.raw_storage_index() == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                                "Modulo by zero");
                        }
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::percent, rightValue));
                        break;
                    }
                    default:
                        return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                            "Unknown compound assignment operator");
                }
            }
            
            // Directly assign the result without creating new AST nodes (optimization)
            if (expr->target->get_type() == node_type::identifier_expr) {
                auto* identifier = static_cast<identifier_expr*>(expr->target.get());
                // Fast path for simple identifier assignment
                if (identifier->symbol_id == UINT64_MAX) {
                    identifier->symbol_id = string_symbolizer_->intern(identifier->name);
                }

                // FIX #5: Enforce type compatibility for compound assignments
                script_value* currentVal = environment_->get_value_ptr(identifier->symbol_id);
                if (currentVal) {
                    type_info_ptr target_type = currentVal->get_type_info();
                    if (target_type && target_type->base_type != script_value_type::jai_any_type) {
                        auto enforced = enforce_type_compatibility(std::move(resultValue), target_type, identifier->name);
                        if (!enforced) {
                            return enforced.error_value();
                        }
                        resultValue = std::move(enforced.value());
                    }
                }

                // §12.1: identifier compound on a bound VARIABLE stays write-through instead of
                // silently replacing the binding with a detached value
                if (currentVal && currentVal->is_cpp_bound()) {
                    currentVal->assign_through(resultValue);
                    push_value(std::move(resultValue));
                    return {};
                }

                // FIX #3/#4: Save result before moving to avoid returning moved-from value
                script_value returnValue = resultValue.clone();
                JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, std::move(resultValue)));
                push_value(std::move(returnValue));
            } else {
                // Fall back to AST creation for complex lvalues
                auto regularAssignment = std::make_shared<assignment_expr>(
                    expr->location,
                    expr->target,
                    token(token_type::equal, "=", expr->op.location),
                    std::make_shared<literal_expr>(expr->location, resultValue)
                );
                JAISCRIPT_TRY(dispatch_expr(regularAssignment.get()));
            }
        }
    } else {
        // Regular assignment

        JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
        // Check if we're unwinding due to an exception in the value expression
        if (is_unwinding_) {
            // Don't try to pop a value that wasn't pushed due to the exception
            return {};
        }
        script_value value = pop_value();

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
                        if (slot_type && (slot_type->base_type != value.type() ||
                                          slot_type->base_type == script_value_type::jai_object_type)) {
                            auto enforced = enforce_type_compatibility(std::move(value), slot_type, identifier->name);
                            if (!enforced) {
                                return enforced.error_value();
                            }
                            value = std::move(enforced.value());
                            // var slots keep a shared_ptr-tagged rhs's marker (decl
                            // parity; flattening to 'any' would detach at the copy)
                            if (slot_type->base_type == script_value_type::jai_any_type &&
                                (!value.get_type_info() ||
                                 value.get_type_info()->base_type != script_value_type::jai_shared_ptr_type)) {
                                value.set_type_info(slot_type);
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

            // Get the current value to check if it's a reference (environment path)
            if (environment_->contains(identifier->symbol_id)) {
                script_value* currentVal = environment_->get_value_ptr(identifier->symbol_id);
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
                    return environment_->assign(identifier->symbol_id, std::move(v));
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
                                        uint64_t expected_id = expected_type->id;
                                        uint64_t actual_id = value_type_info->element_type()
                                            ? value_type_info->element_type()->id
                                            : value_type_info->id;
                                        return checked_result<void>(
                                            make_error_code(runtime_error_code::type_mismatch),
                                            "Cannot assign shared_ptr<{}> to weak_ptr<{}>: type must match or be a subclass",
                                            actual_id, expected_id);
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
                    } else if (value.type() == script_value_type::jai_object_type) {
                        // Helpful error for value-semantic objects
                        auto type_info = currentVal->get_type_info();
                        uint64_t weak_type_id = (type_info && !type_info->type_params.empty())
                            ? type_info->type_params[0]->id : 0;
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign value-semantic object to weak_ptr<{}>: use shared_ptr<T>",
                            weak_type_id);
                    } else {
                        auto type_info = value.get_type_info();
                        uint64_t actual_type_id = type_info ? type_info->id : 0;
                        auto weak_type_info = currentVal->get_type_info();
                        uint64_t weak_type_id = (weak_type_info && !weak_type_info->type_params.empty())
                            ? weak_type_info->type_params[0]->id : 0;
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign {} to weak_ptr<{}>: use shared_ptr<T>",
                            actual_type_id, weak_type_id);
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
                            std::string actual_type_name = value_element ? value_element->type_name : "";

                            // Check type compatibility (same type or derived)
                            bool compatible = (actual_type_name == expected_type_name);
                            if (!compatible && !actual_type_name.empty()) {
                                if (auto eng = engine_) {
                                    auto actual_class = eng->get_class_definition(actual_type_name);
                                    if (actual_class && actual_class->is_subtype_of(expected_type_name)) {
                                        compatible = true;
                                    }
                                }
                            }

                            if (!compatible) {
                                uint64_t actual_id = value_element ? value_element->id : 0;
                                uint64_t expected_id = expected_type ? expected_type->id : 0;
                                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                                    "Cannot assign shared_ptr<{0}> to shared_ptr<{1}>", actual_id, expected_id);
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
                            if (auto* any_ti = engine_->get_type_info_any()) {
                                rebound.set_type_info(any_ti);
                            }
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
                                        // Copy all fields from source to target
                                        instance->copy_fields_from(*source_instance);
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
                                // Operator= succeeded - underlying object was modified in place
                                return checked_result<void>();
                            }
                            return checked_result<void>(result.error(), "operator= failed");
                        }

                        // No operator= found - type error
                        auto type_info = value.get_type_info();
                        uint64_t type_id = type_info ? type_info->id : 0;
                        uint64_t expected_id = expected_type ? expected_type->id : 0;
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign {0} to shared_ptr<{1}>: no operator=({0}) defined", type_id, expected_id);
                    }
                } else {
                    // Regular variable assignment with STRONG TYPES enforcement
                    type_info_ptr target_type = currentVal->get_type_info();

                    // Try assignment operator lookup for class types before type enforcement
                    if (target_type && target_type->base_type == script_value_type::jai_object_type) {
                        // Check if currentVal has an operator= method for the source type
                        auto source_type_info = value.get_type_info();
                        std::string source_type_name = source_type_info ? source_type_info->type_name : "unknown";

                        // Only try operator= if types are different (same type just uses regular assignment)
                        if (source_type_name != target_type->type_name) {
                            auto instance_result = currentVal->checked_as<std::shared_ptr<class_instance>>();
                            if (instance_result) {
                                auto instance = instance_result.value();
                                // Look for operator= method using cached symbol ID
                                script_value method = instance->get_method(assign_operator_id_, false);
                                if (method.is_function()) {
                                    // Found operator= method - call it with the source value
                                    // Script methods expect 'this' as the first argument
                                    const script_function& func = method.as_function();
                                    std::vector<script_value> args;
                                    args.push_back(*currentVal);  // 'this' - the instance being assigned to
                                    args.push_back(std::move(value));  // the value being assigned
                                    auto result = func(args);
                                    if (result) {
                                        // Operator= succeeded - return without replacing the variable
                                        // The operator= method should have modified the instance in place
                                        return checked_result<void>();
                                    }
                                    // FIX #10: Propagate operator= errors instead of silent fallthrough
                                    return checked_result<void>(result.error(), "operator= failed");
                                }
                            }
                        }
                    }

                    // Enforce type compatibility
                    auto enforced = enforce_type_compatibility(std::move(value), target_type, identifier->name);
                    if (!enforced) {
                        return enforced.error_value();
                    }
                    value = std::move(enforced.value());

                    // Handle first assignment for uninitialized auto variables
                    if (!target_type) {
                        // Lock the variable's type to the assigned value's type
                        // The value keeps its own type_info
                    }
                    // For any_type (var), keep the any_type on the variable. Exception
                    // (mirrors the decl path, Dev ruling 2026-07): a shared_ptr-tagged
                    // rhs keeps its marker - flattening to 'any' would silently detach
                    // the handle at the copy below
                    else if (target_type->base_type == script_value_type::jai_any_type &&
                             (!value.get_type_info() ||
                              value.get_type_info()->base_type != script_value_type::jai_shared_ptr_type)) {
                        value.set_type_info(target_type);  // Keep any_type marker
                    }
                    else if (target_type->base_type == script_value_type::jai_any_type &&
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
                        JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, std::move(value)));
                        // Get the stored value back for the expression result
                        // Use shallow copy (shares object_holder) - like C++ returning lvalue reference
                        script_value* stored = environment_->get_value_ptr(identifier->symbol_id);
                        push_value(stored ? *stored : make_value());  // Shallow copy, not clone
                    }
                }
            } else {
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
                                // Check if this is a C++ parent property that needs setter method
                                auto class_def = instance->get_class_definition();
                                if (class_def) {
                                    auto cpp_base = class_def->get_cpp_base_class();
                                    if (cpp_base) {
                                        // Check if field has a pre-registered setter ID (fast path)
                                        uint64_t setter_id = cpp_base->get_property_setter_id(identifier->symbol_id);
                                        if (setter_id != 0) {
                                            // This is a C++ property - call the setter method
                                            auto setter = cpp_base->get_method(setter_id, false);
                                            if (setter.is_function()) {
                                                // Call setter with this and value
                                                std::vector<script_value> args = {this_val, value};
                                                auto result = setter.as_function()(args);
                                                if (!result) {
                                                    return result.error_value();
                                                }
                                                assigned_to_member = true;
                                            }
                                        }
                                    }
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
                            // Then try static fields
                            else {
                                auto class_def = instance->get_class_definition();
                                if (class_def) {
                                    // All-detach (#12): implicit static store snapshots too
                                    if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
                                        value = value.detached_for_store();
                                    }
                                    if (is_lvalue_read) {
                                        if (class_def->set_static_field(identifier->symbol_id, clone_for_assignment(value))) {
                                            assigned_to_member = true;
                                        }
                                    } else {
                                        if (class_def->set_static_field(identifier->symbol_id, std::move(value))) {
                                            value = class_def->get_static_field(identifier->symbol_id);  // Get back
                                            assigned_to_member = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
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
            // Check if this is a static member assignment
            if (memberExpr->is_static) {
                // For static assignment, get the class definition
                if (memberExpr->object->get_type() != node_type::identifier_expr) {
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                        "Static member assignment requires a class name");
                }
                auto* ident_expr = static_cast<identifier_expr*>(memberExpr->object.get());

                uint64_t class_name_id = ident_expr->symbol_id;
                auto [class_var_id, class_var_name] = string_symbolizer_->get_class_var_id_with_view(class_name_id);
                auto class_result = environment_->get(class_var_id);
                if (!class_result) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        "Class '{0}' not found", class_name_id);
                }
                script_value class_var = std::move(class_result.value());

                if (!class_var.is_object()) {
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                        "'{0}' is not a class", class_name_id);
                }

                auto objHolder = class_var.get_object_holder();
                if (!objHolder || objHolder->type_name != "class_definition") {
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                        "'{0}' is not a valid class", class_name_id);
                }

                auto class_def = std::static_pointer_cast<class_definition>(objHolder->data);

                if (class_def->chain_has_nonpublic()) [[unlikely]] {
                    JAISCRIPT_TRY(detail::enforce_member_access(class_def.get(), memberExpr->member_id,
                                                                environment_->find_access_context()));
                }

                // Evaluate the value
                JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
                script_value value = pop_value();

                // Set the static field
                if (!class_def->set_static_field(memberExpr->member_id, clone_for_assignment(value))) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        "Cannot assign to static member: field '{0}' not found", memberExpr->member_id);
                }


                push_value(std::move(value));
                return {};
            }

            // Regular member assignment - evaluate the object
            JAISCRIPT_TRY(dispatch_expr(memberExpr->object.get()));
            script_value objectValue = pop_value();

            // Dereference if it's a reference (e.g., from array[index])
            script_value dereferenced = objectValue.deref();

            // Unwrap shared_ptr if needed
            // After refactor: shared_ptr<T> uses same storage, no unwrapping needed

            // Check if it's an object
            if (!dereferenced.is_object()) {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to member of non-object type");
                current_exception_ = script_exception("Cannot assign to member of non-object type", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return {};
            }

            JAISCRIPT_TRY(assign_member_value(dereferenced, memberExpr, value));
            if (is_unwinding_) {
                push_value(make_value());
                return {};
            }

            push_value(std::move(value));  // Assignment expressions return the assigned value
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
                auto target_eval = dispatch_expr(expr->target.get());
                lvalue_target_context_ = false;
                JAISCRIPT_TRY(target_eval);
                script_value target_ref = pop_value();
                
                // Check if we got a reference
                if (target_ref.is_reference()) {
                    // Mode-based re-resolution (never a cached address): realloc/erase
                    // between mint and write can't corrupt the heap. TYPED elements:
                    // scratch target + buffer commit (KEEP BYTE-PARALLEL with
                    // vm_backend::exec_index_assign)
                    auto refHolder = target_ref.get_reference_holder();
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
                        // Validate element type compatibility
                        if (!is_element_type_compatible(value, element_type, *target_ptr)) {
                            // Get type names for error message
                            std::string value_type = get_value_type_name(value);
                            std::string expected_type = get_type_info_name(element_type);
                            // Intern the type names for the error message
                            uint64_t value_type_id = string_symbolizer_->intern(value_type);
                            uint64_t expected_type_id = string_symbolizer_->intern(expected_type);
                            return checked_result<void>(
                                make_error_code(runtime_error_code::array_element_type_mismatch),
                                "Cannot assign '{0}' to element of type '{1}'",
                                value_type_id, expected_type_id);
                        }
                        // Convert element if needed (e.g., int -> float for array<float>)
                        script_value converted = convert_array_element(engine_, value, element_type);
                        *target_ptr = std::move(converted);
                    } else {
                        // No element type constraint - values deep-copy, shared_ptr
                        // handles share
                        *target_ptr = clone_for_assignment(value);
                    }
                    if (typed_element) {
                        refHolder->container->set(refHolder->container_index, *target_ptr);
                    }
                    push_value(std::move(value));  // Assignment expressions return the assigned value
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

// statement visitors
checked_result<void> interpreter::visit_expression_stmt(expression_stmt* stmt) {
    // Tell assignment expressions they can skip deep-cloning the result
    bool prev = expression_result_needed_;
    expression_result_needed_ = false;

    JAISCRIPT_TRY(dispatch_expr(stmt->expression.get()));

    expression_result_needed_ = prev;

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
        environment_ = get_pooled_environment(environment_);
    }

    try {
        for (size_t i = start_index; i < stmt->declarations.size(); ++i) {
            auto result = dispatch_decl(stmt->declarations[i].get());

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

checked_result<void> interpreter::visit_variable_decl(variable_decl* decl) {
    // Helper to store value in slot (local) or environment (global)
    auto define_variable = [&](script_value value) {
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
            environment_->define(decl->name_id, std::move(value));
        }
    };

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
            define_variable(std::move(weak));
        } else {
            // Evaluate initializer
            JAISCRIPT_TRY(dispatch_expr(decl->initializer.get()));
            script_value value = pop_value();

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
                define_variable(std::move(weak));
            } else if (value.is_weak_ptr()) {
                // Initialize with another weak_ptr - copy it
                define_variable(std::move(value));
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
                                uint64_t expected_id = expected_type->id;
                                uint64_t actual_id = value_type_info->element_type()
                                    ? value_type_info->element_type()->id
                                    : value_type_info->id;
                                return checked_result<void>(
                                    make_error_code(runtime_error_code::type_mismatch),
                                    "Cannot initialize weak_ptr<{}> from shared_ptr<{}>: type must match or be a subclass",
                                    expected_id, actual_id);
                            }
                        }
                    }
                }

                // Initialize with shared_ptr - create weak_ptr from it
                auto weak_result = script_value::make_weak_ptr(value, engine_);
                if (!weak_result) {
                    return weak_result.error_value();
                }
                define_variable(std::move(weak_result.value()));
            } else if (value.type() == script_value_type::jai_object_type) {
                // Helpful error for value-semantic objects
                auto type_info = decl->type;
                uint64_t weak_type_id = (type_info && !type_info->type_params.empty())
                    ? type_info->type_params[0]->id : 0;
                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                    "Cannot initialize weak_ptr<{}> from value-semantic object: use shared_ptr<T>",
                    weak_type_id);
            } else {
                auto type_info = value.get_type_info();
                uint64_t actual_type_id = type_info ? type_info->id : 0;
                auto weak_type_info = decl->type;
                uint64_t weak_type_id = (weak_type_info && !weak_type_info->type_params.empty())
                    ? weak_type_info->type_params[0]->id : 0;
                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                    "Cannot initialize weak_ptr<{}> with {}: use shared_ptr<T>",
                    weak_type_id, actual_type_id);
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
            define_variable(std::move(null_ptr));
        } else {
            // Evaluate initializer
            JAISCRIPT_TRY(dispatch_expr(decl->initializer.get()));
            script_value value = pop_value();

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
                define_variable(std::move(value));
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
                    define_variable(std::move(value));
                    return {};
                }
                // Dev ruling (2026-07): typed shared_ptr declarations ENFORCE their
                // pointee class (wrong-class init used to alias silently). Same class
                // and derived->base (script chains + host upcasts) stay legal;
                // unresolvable classes stay lenient (opaque host flows).
                auto expected_type = decl->type->element_type();
                if (expected_type && !expected_type->type_name.empty() &&
                    expected_type->base_type != script_value_type::jai_any_type) {
                    const std::string& expected_class = expected_type->type_name;
                    std::string actual_class;
                    if (auto instance = value.get_class_instance()) {
                        actual_class = instance->get_class_name();
                    }
                    if (!actual_class.empty() && actual_class != expected_class) {
                        bool is_subtype = false;
                        if (auto eng = engine_) {
                            auto actual_def = eng->get_class_definition(actual_class);
                            is_subtype = actual_def && actual_def->is_subtype_of(expected_class);
                        }
                        if (!is_subtype) {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                                "Cannot initialize shared_ptr<{0}> with {1}: type must match or be a subclass",
                                expected_type->id, string_symbolizer_->intern(actual_class));
                        }
                    }
                }
                // Initialize with object/shared_ptr - that's fine, objects are already shared_ptr
                // Mark the type as shared_ptr to ensure reference semantics
                value.set_type_info(decl->type);
                define_variable(std::move(value));
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
            script_value* targetPtr = resolve_local_or_env(identExpr->slot_index, targetSymbolId);
            if (!targetPtr) {
                return checked_result<void>(make_error_code(runtime_error_code::undefined_variable), "Cannot take reference of undefined variable", targetSymbolId);
            }
            
            // Check if the target is itself a reference
            if (targetPtr->is_reference()) {
                auto aliased = decl_ref_alias(*targetPtr, engine_);
                if (!aliased) {
                    return aliased.error_value();
                }
                define_variable(std::move(aliased.value()));
            } else {
                // Box the variable's storage into a cell and alias it (escape-legal:
                // the ref keeps the cell alive after the env dies)
                define_variable(share_boxed_env_storage(*targetPtr, engine_));
            }
        } else {
            // For other expressions, evaluate them and check if they return a reference
            JAISCRIPT_TRY(dispatch_expr(decl->initializer.get()));
            script_value result = pop_value();
            
            // If the result is a reference, alias it (share the holder)
            if (result.is_reference()) {
                auto aliased = decl_ref_alias(result, engine_);
                if (!aliased) {
                    return aliased.error_value();
                }
                define_variable(std::move(aliased.value()));
            } else if (detail::is_member_final_ref_lvalue(decl->initializer.get())) {
                // Member-final initializer (auto& p = G.player): field reads evaluate to
                // COPIES, so the evaluated result can't alias - resolve the chain through
                // the shared kernel into an owner-pinned FIELD reference instead (route-
                // independence with ref params/returns; Dev ruling 2026-07-12). Access/
                // removed-field errors surface as-is; the kernel's generic non-lvalue
                // verdict (computed property, C++-backed field, non-instance receiver)
                // keeps the decl's own error text below.
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
                    define_variable(std::move(aliased.value()));
                } else {
                    return checked_result<void>(make_error_code(runtime_error_code::invalid_reference),
                        "Cannot take reference of non-lvalue expression (var&/auto& declarations bind locals, "
                        "subscript chains like arr[i] or grid[y][x], map entries, and object fields like obj.field; "
                        "computed properties and C++-backed members are not ref-bindable)");
                }
            } else {
                // KEEP BYTE-PARALLEL with the vm's exec_decl_ref_value
                return checked_result<void>(make_error_code(runtime_error_code::invalid_reference),
                    "Cannot take reference of non-lvalue expression (var&/auto& declarations bind locals, "
                    "subscript chains like arr[i] or grid[y][x], map entries, and object fields like obj.field; "
                    "computed properties and C++-backed members are not ref-bindable)");
            }
        }
    } else {
        // Regular variable declaration
        script_value value = make_value();
        if (decl->initializer) {
            JAISCRIPT_TRY(dispatch_expr(decl->initializer.get()));
            value = std::move(pop_value());

            // Decl fast path (construction-stamped flags): a scalar payload whose
            // storage index already matches the declared type stores straight into the
            // slot - enforce/clone/homogeneity are provably identity for
            // int/float/bool/char (clone of a scalar is a copy, so is_lvalue_expression
            // is irrelevant here). Anything else - references, mismatched payloads,
            // env-resident decls, escape-boxed decls - falls through to the untouched
            // full path below. (KEEP BYTE-PARALLEL with vm_backend::exec_decl_var)
            if ((decl->decl_fast_flags & variable_decl::decl_fast_slot_store) == variable_decl::decl_fast_slot_store &&
                !decl->ref_escaping && decl->slot_index != SIZE_MAX && !call_stack_.empty()) {
                size_t vi = value.raw_storage_index();
                // Subscript-initialized decls arrive as rhs-lvalue reference wrappers
                // (the dominant profiled shape): the decl consumes the VALUE, so a
                // matching scalar pointee takes the same identity store - copy out
                // BEFORE overwriting `value` (its holder OWNS the deref target).
                const script_value* seen = &value;
                if (vi == script_value::TYPEID_REFERENCE) {
                    seen = &value.deref();
                    vi = seen->raw_storage_index();
                }
                if (vi == script_value::TYPEID_INT || vi == script_value::TYPEID_FLOAT ||
                    vi == script_value::TYPEID_BOOL || vi == script_value::TYPEID_CHAR) {
                    const script_value_type bt = decl->type ? decl->type->base_type : script_value_type::jai_any_type;
                    const bool matches = bt == script_value_type::jai_any_type ||
                        (vi == script_value::TYPEID_INT && bt == script_value_type::jai_int_type) ||
                        (vi == script_value::TYPEID_FLOAT && bt == script_value_type::jai_float_type) ||
                        (vi == script_value::TYPEID_BOOL && bt == script_value_type::jai_bool_type) ||
                        (vi == script_value::TYPEID_CHAR && bt == script_value_type::jai_char_type);
                    if (matches) {
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
                    JAISCRIPT_TRY(validate_container_homogeneous(value, ""));
                }
            }
        }
        // If no initializer, value remains null

        // === STRONG TYPES: Set type_info based on declaration ===
        // - Explicit type (int, float, var, etc.): Use declared type (locks the variable)
        // - auto with initializer: Infer type from value (locks to inferred type)
        // - auto without initializer: Keep nullptr (first assignment will lock type)
        if (decl->type) {
            // Explicit type declaration (int x, float y, var z, etc.)
            // Typed declarations convert their initializer exactly like assignment does
            // (int d = 4.7 truncates instead of storing a mistagged float payload)
            if (decl->initializer) {
                auto enforced = enforce_type_compatibility(std::move(value), decl->type, decl->name);
                if (!enforced) {
                    return enforced.error_value();
                }
                value = std::move(enforced.value());
            }
            // Set declared type - this locks the variable's type. Exception: a 'var' decl
            // keeps a shared_ptr-tagged initializer's marker — `var p = new P()` IS
            // `shared_ptr<P> p = P()` (Dev ruling 2026-07), so the reference-semantics
            // tag must survive the decl instead of flattening to 'any'
            if (decl->type->base_type != script_value_type::jai_any_type ||
                !value.get_type_info() ||
                value.get_type_info()->base_type != script_value_type::jai_shared_ptr_type) {
                value.set_type_info(decl->type);
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
        }
        // else: auto without initializer - type_info remains nullptr (uninitialized)
        // First assignment will lock the type

        // Define in slot (local) or environment (global)
        define_variable(std::move(value));
        // After move, value is in moved-from state, so don't access it
    }
    return {};
}

// === STRONG TYPES: Type enforcement for assignment ===
checked_result<script_value> interpreter::enforce_type_compatibility(
    script_value value,
    type_info_ptr target_type,
    std::string_view var_name
) {
    // Case 1: Uninitialized variable (auto x;) - lock to source type
    if (!target_type) {
        // First assignment locks the type - value already has its own type
        return std::move(value);
    }

    // Case 2: Any type (var x) - allow anything
    if (target_type->base_type == script_value_type::jai_any_type) {
        // Keep any_type on the variable, but store the value
        // The value's internal type is preserved for operations
        return std::move(value);
    }

    // Case 3: Locked type - enforce compatibility
    auto source_type = value.type();
    auto target = target_type->base_type;

    // Fast path: same type (but NOT for object types - need to compare class names)
    if (source_type == target && target != script_value_type::jai_object_type) {
        // Typed-container boundary: array/map payloads are element-checked with push's
        // rules before they may sit behind an array<T>/map<K,V> tag (shared kernel,
        // KEEP BYTE-PARALLEL with vm_backend::enforce_type_compatibility)
        if (target == script_value_type::jai_array_type || target == script_value_type::jai_map_type) {
            auto outcome = detail::enforce_container_boundary(std::move(value), target_type, engine_);
            if (!outcome.value) {
                auto* sym = engine_->get_symbolizer();
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::array_element_type_mismatch),
                    detail::container_boundary_mismatch_text,
                    sym->intern(outcome.offending), sym->intern(outcome.expected));
            }
            return std::move(*outcome.value);
        }
        return std::move(value);
    }

    // Numeric conversions (int <-> float)
    if (target == script_value_type::jai_int_type) {
        if (source_type == script_value_type::jai_float_type) {
            // float -> int (truncate)
            return make_value(static_cast<script_int>(value.unchecked_as_float()));
        }
        if (source_type == script_value_type::jai_bool_type) {
            // bool -> int
            return make_value(static_cast<script_int>(value.unchecked_as_bool() ? 1 : 0));
        }
    }

    if (target == script_value_type::jai_float_type) {
        if (source_type == script_value_type::jai_int_type) {
            // int -> float (widening)
            return make_value(static_cast<script_float>(value.unchecked_as_int()));
        }
        if (source_type == script_value_type::jai_bool_type) {
            // bool -> float
            return make_value(static_cast<script_float>(value.unchecked_as_bool() ? 1.0 : 0.0));
        }
    }

    if (target == script_value_type::jai_bool_type) {
        // Truthy conversion to bool
        return make_value(is_truthy(value));
    }

    if (target == script_value_type::jai_string_type) {
        // to_string conversion
        return make_value(value.to_string());
    }

    // Null can be assigned to object/shared_ptr/weak_ptr types
    // Keep the target type_info so the variable remains typed
    if (source_type == script_value_type::jai_null_type) {
        if (target == script_value_type::jai_object_type ||
            target == script_value_type::jai_shared_ptr_type ||
            target == script_value_type::jai_weak_ptr_type) {
            // Return null but preserve the target type for future assignments
            script_value null_val = make_value();
            null_val.set_type_info(target_type);
            return null_val;
        }
    }

    // Class type compatibility for object types (nominal typing)
    if (target == script_value_type::jai_object_type &&
        source_type == script_value_type::jai_object_type) {

        auto source_type_info = value.get_type_info();
        if (source_type_info && target_type) {
            // Compare class names - must match exactly or source must be subclass of target
            const std::string& source_class_name = source_type_info->type_name;
            const std::string& target_class_name = target_type->type_name;

            // Same class name - compatible
            if (source_class_name == target_class_name) {
                return std::move(value);
            }

            // Check inheritance - source must be derived from target (child -> parent is OK)
            // We need to get the actual class instance to check the inheritance chain
            try {
                auto instance = value.as<std::shared_ptr<class_instance>>();
                if (instance) {
                    class_definition* class_def = instance->get_class_definition();
                    if (class_def) {
                        // Walk up the inheritance chain looking for target class
                        class_definition* current = class_def;
                        while (current) {
                            for (const auto& parent : current->get_parent_classes()) {
                                if (parent && parent->get_name() == target_class_name) {
                                    // Source is derived from target - compatible
                                    return std::move(value);
                                }
                            }
                            // Move up to first parent for next iteration (single inheritance path)
                            current = current->get_parent().get();
                        }
                    }
                }
            } catch (...) {
                // Not a class_instance or extraction failed - fall through to error
            }

            // Classes are incompatible - try constructor-based conversion
            // Look for a constructor Target(Source) in the target class.
            // Ruling: a defaulted-param ctor is NOT a converting ctor - implicit
            // conversion needs a TRUE 1-param ctor (delegating overload opts in);
            // without one, skip the dispatcher (whose explicit path keeps the window).
            auto conv_class_def = engine_ ? engine_->get_class_definition(target_class_name) : nullptr;
            if (conv_class_def && !has_true_single_param_conversion_ctor(conv_class_def)) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Incompatible class types", target_type->id);
            }
            auto ctor_result = environment_->get(target_class_name);
            if (ctor_result && ctor_result.value().is_function()) {
                const script_function& ctor = ctor_result.value().as_function();
                std::vector<script_value> ctor_args;
                ctor_args.push_back(value);

                try {
                    auto result = ctor(ctor_args);
                    if (result.has_value()) {
                        return std::move(result.value());
                    }
                } catch (const runtime_error& e) {
                    std::string error_msg = e.what();
                    if (error_msg.find("No constructor found") == std::string::npos) {
                        // Constructor exists but failed - propagate the error
                        return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Constructor conversion failed", target_type->id);
                    }
                    // No matching constructor - fall through to type mismatch error
                }
            }

            // No suitable constructor found - incompatible types
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Incompatible class types", target_type->id);
        }
    }

    // Primitive-to-object conversion via constructor
    // Try to convert primitives (int, float, string, bool, char) to object types via constructors
    if (target == script_value_type::jai_object_type && target_type && !target_type->type_name.empty()) {
        bool is_primitive = (source_type == script_value_type::jai_int_type ||
                            source_type == script_value_type::jai_float_type ||
                            source_type == script_value_type::jai_string_type ||
                            source_type == script_value_type::jai_bool_type ||
                            source_type == script_value_type::jai_char_type);

        if (is_primitive) {
            const std::string& target_class_name = target_type->type_name;

            // Ruling: implicit primitive->object conversion needs a TRUE 1-param ctor
            // (a defaulted-param ctor is not a converting ctor; delegating overload opts
            // in) - same error text as the incompatible-types tail below
            auto conv_class_def = engine_ ? engine_->get_class_definition(target_class_name) : nullptr;
            if (conv_class_def && !has_true_single_param_conversion_ctor(conv_class_def)) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Type mismatch in assignment", target_type->id);
            }
            // Try to find and call the constructor with the primitive value
            auto ctor_result = environment_->get(target_class_name);
            if (ctor_result && ctor_result.value().is_function()) {
                const script_function& ctor = ctor_result.value().as_function();
                std::vector<script_value> ctor_args;
                ctor_args.push_back(value);

                try {
                    auto result = ctor(ctor_args);
                    if (result.has_value()) {
                        return std::move(result.value());
                    }
                } catch (const runtime_error& e) {
                    std::string error_msg = e.what();
                    if (error_msg.find("No constructor found") == std::string::npos) {
                        // Constructor exists but failed - propagate the error
                        return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Primitive conversion failed", target_type->id);
                    }
                    // No matching constructor - fall through to type mismatch error
                }
            }
        }
    }

    // Incompatible types - error
    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Type mismatch in assignment", target_type ? target_type->id : 0);
}

// Compound store-back: locked targets convert the promoted result like '=' does; var
// targets keep the promoted result and re-tag it 'any' so the variable stays dynamic.
// KEEP BYTE-PARALLEL with vm_backend::compound_typed_store_back.
checked_result<void> interpreter::compound_typed_store_back(script_value& target, script_value promoted, std::string_view var_name) {
    type_info_ptr tag = target.get_type_info();
    if (tag && tag->base_type != script_value_type::jai_any_type) {
        auto enforced = enforce_type_compatibility(std::move(promoted), tag, var_name);
        if (!enforced) {
            return enforced.error_value();
        }
        promoted = std::move(enforced.value());
        promoted.set_type_info(tag);
    } else if (tag) {
        promoted.set_type_info(tag);
    }
    target = std::move(promoted);
    return {};
}

// Helper to convert value to string, checking for to_string() method on objects
std::string interpreter::value_to_string_with_method(const script_value& val) {
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
                if (result.has_value() && result.value().is_string()) {
                    return result.value().unchecked_as_string();
                }
            }
        }
    }
    return val.to_string();
}

// Binary operation helpers
checked_result<script_value> interpreter::evaluate_arithmetic(const script_value& left_in, token_type op, const script_value& right_in) {
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
                                   ri == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);

    // Integral promotion: char operands enter arithmetic as int64 0..255 (char_promotion.hpp)
    if (detail::char_operands_promote(li, ri)) [[unlikely]]
        return evaluate_arithmetic(detail::char_promoted(left, engine_), op, detail::char_promoted(right, engine_));

    // Special case for string concatenation (use move to avoid copying the temporary)
    // Check for to_string() method on objects before falling back to default
    if (op == token_type::plus && (li == script_value::TYPEID_STRING || ri == script_value::TYPEID_STRING)) {
        script_string joined = value_to_string_with_method(left) + value_to_string_with_method(right);
        // engine::memory_cap chokepoint: deny the concat result before it exists
        if (!limits_->memory_charge(sizeof(script_value) + joined.size())) [[unlikely]] {
            return detail::raise_memory_cap(*limits_);
        }
        return make_value(std::move(joined));
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
                        "Division by zero");
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
        return checked_result<script_value>(
            make_error_code(runtime_error_code::invalid_numeric_operand),
            "Left operand must be numeric");
    }

    if (ri == script_value::TYPEID_INT) {
        rightNum = static_cast<script_float>(right.unchecked_as_int());
    } else if (ri == script_value::TYPEID_FLOAT) {
        rightNum = right.unchecked_as_float();
    } else {
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
                    "Division by zero");
            }
            return make_value(std::fmod(leftNum, rightNum));
        default:
            return checked_result<script_value>(
                make_error_code(runtime_error_code::unknown_operator),
                "Unknown arithmetic operator");
    }
}


// Placeholder implementations for remaining visitors
checked_result<void> interpreter::visit_call_expr(call_expr* expr) {
    // math:: language intrinsics (detail/math_intrinsics.hpp): evaluate args, one
    // shared-kernel call - no namespace lookup, no call machinery (KEEP BYTE-PARALLEL
    // with vm_backend::exec_math; parity by the shared kernel).
    if (expr->callee->get_type() == node_type::member_expr) {
        auto* member_callee = static_cast<member_expr*>(expr->callee.get());
        if (member_callee->is_static) {
            const detail::math_fn fn = detail::math_intrinsic_for_call(member_callee);
            if (fn != detail::math_fn::none && expr->arguments.size() <= 8) {
                const size_t argc = expr->arguments.size();
                for (const auto& arg : expr->arguments) {
                    JAISCRIPT_TRY(dispatch_expr(arg.get()));
                    if (is_unwinding_) {
                        push_value(make_value());
                        return {};
                    }
                }
                // Args sit on the value stack in order; point the kernel at them deref'd
                const script_value* argp[8] = {};
                for (size_t i = 0; i < argc; ++i) {
                    argp[i] = &valueStack_.peek(argc - 1 - i).deref();
                }
                auto result = detail::eval_math_intrinsic(fn, argp, argc, engine_, engine_->math_rng());
                if (!result) {
                    return result.error_value();
                }
                script_value out = std::move(result.value());
                for (size_t i = 0; i < argc; ++i) {
                    valueStack_.discard();
                }
                push_value(std::move(out));
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

    // Special handling for string method calls on variables - mutate in place
    // Pattern: identifier.method() where identifier is a string variable
    if (expr->callee->get_type() == node_type::member_expr) {
        auto* member = static_cast<member_expr*>(expr->callee.get());
        if (!member->is_static && member->object->get_type() == node_type::identifier_expr) {
            auto* ident = static_cast<identifier_expr*>(member->object.get());

            // Try to get a mutable reference to the variable
            auto ref_result = environment_->get_ref(ident->symbol_id);
            if (ref_result && ref_result.value().get().is_string()) {
                // Found a string variable - check if method exists
                auto methodIt = string_methods_.find(member->member_id);
                if (methodIt != string_methods_.end()) {
                    // Evaluate arguments
                    std::vector<script_value> arguments;
                    arguments.reserve(expr->arguments.size());
                    for (const auto& argExpr : expr->arguments) {
                        JAISCRIPT_TRY(dispatch_expr(argExpr.get()));
                        arguments.emplace_back(std::move(pop_value()));
                        if (is_unwinding_) {
                            push_value(make_value());
                            return {};
                        }
                    }

                    // Call method directly on the variable reference. Resolve through
                    // references (ref decls/cells): string builtins read self's storage
                    // RAW, and in-place mutation must land in the target
                    // (KEEP BYTE-PARALLEL with vm_backend::exec_call_method)
                    script_value& var_ref = ref_result.value().get().deref();
                    detail::deref_builtin_args_in_place(arguments);
                    auto result = methodIt->second(builtin_method_context{engine_, string_symbolizer_}, var_ref, arguments);
                    if (!result) {
                        return result.error_value();
                    }
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
                    if (parallel_worker_ && cls_raw && !engine_->allow_unsafe_parallel()) [[unlikely]] {
                        const detail::parallel_method_pin* pin = parallel_method_pins_
                            ? parallel_method_pins_->find(cls_raw, member->member_id) : nullptr;
                        if (!pin) {
                            return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                                "method '{0}' was not admitted in this parallel body (admitted: single-overload "
                                "public methods on the element's own class touching only element, local, or "
                                "captured-read state; engine::allow_unsafe_parallel(true) overrides)", member->member_id);
                        }
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
                for (const auto& argExpr : expr->arguments) {
                    try {
                        JAISCRIPT_TRY(dispatch_expr(argExpr.get()));
                        arguments.emplace_back(std::move(pop_value()));
                        if (is_unwinding_) {
                            push_value(make_value());
                            return {};
                        }
                    } catch (const script_exception& e) {
                        active_exception_value_ = make_value(std::string(e.what()));
                        current_exception_ = e;
                        is_unwinding_ = true;
                        push_value(make_value());
                        return {};
                    } catch (const std::runtime_error& e) {
                        active_exception_value_ = make_value(std::string(e.what()));
                        current_exception_ = script_exception(e.what());
                        is_unwinding_ = true;
                        push_value(make_value());
                        return {};
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
                            std::string member_str(member->member);
                            active_exception_value_ = make_value("Object has no member '" + member_str + "'");
                            current_exception_ = script_exception("Object has no member '" + member_str + "'", member->location);
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
                    method_call_override mctx{&def_env, &objectValue, cls_pin.get()};
                    detail::call_site_context ctx{&expr->arguments};
                    const detail::call_site_context* saved_ctx = pending_call_ctx_;
                    pending_call_ctx_ = expr->arguments.empty() ? saved_ctx : &ctx;
                    std::optional<checked_result<script_value>> callOutcome;
                    try {
                        callOutcome.emplace(call_function(script_func, arguments, &mctx));
                    } catch (const script_exception& e) {
                        pending_call_ctx_ = saved_ctx;
                        active_exception_value_ = make_value(std::string(e.what()));
                        current_exception_ = e;
                        is_unwinding_ = true;
                        push_value(make_value());
                        return {};
                    } catch (const std::exception& e) {
                        pending_call_ctx_ = saved_ctx;
                        active_exception_value_ = make_value(std::string(e.what()));
                        current_exception_ = script_exception(e.what());
                        is_unwinding_ = true;
                        push_value(make_value());
                        return {};
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

    // Evaluate the callee expression
    JAISCRIPT_TRY(dispatch_expr(expr->callee.get()));
    script_value callee = pop_value();
    if (is_unwinding_) {
        push_value(make_value());
        return {};
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
        return checked_result<void>(make_error_code(runtime_error_code::not_a_function));  // [ErrorText] Not a function
    }
    
    // Use a local vector for arguments to avoid issues with nested calls
    std::vector<script_value> arguments;
    arguments.reserve(expr->arguments.size());

    for (const auto& argExpr : expr->arguments) {
        // Evaluate argument with exception handling
        try {
            JAISCRIPT_TRY(dispatch_expr(argExpr.get()));
            arguments.emplace_back(std::move(pop_value()));
            if (is_unwinding_) {
                push_value(make_value());
                return {};
            }
        } catch (const script_exception& e) {
            // Convert to interpreter exception state
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = e;
            is_unwinding_ = true;
            push_value(make_value());  // Push null for the failed call
            return {};
        } catch (const std::runtime_error& e) {
            // Convert runtime errors to script exceptions
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = script_exception(e.what());
            is_unwinding_ = true;
            push_value(make_value());  // Push null for the failed call
            return {};
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
    detail::call_site_context ctx{&expr->arguments};
    const detail::call_site_context* saved_ctx = pending_call_ctx_;
    pending_call_ctx_ = expr->arguments.empty() ? saved_ctx : &ctx;
    std::optional<checked_result<script_value>> callOutcome;
    try {
        if (direct_script_call) {
            callOutcome.emplace(call_function(*callee_thunk->payload.fn, arguments));
        } else {
            callOutcome.emplace(func(arguments));
        }
    } catch (const script_exception& e) {
        pending_call_ctx_ = saved_ctx;
        active_exception_value_ = make_value(std::string(e.what()));
        current_exception_ = e;
        is_unwinding_ = true;
        push_value(make_value());
        return {};
    } catch (const std::exception& e) {
        pending_call_ctx_ = saved_ctx;
        active_exception_value_ = make_value(std::string(e.what()));
        current_exception_ = script_exception(e.what());
        is_unwinding_ = true;
        push_value(make_value());
        return {};
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

checked_result<void> interpreter::visit_member_expr(member_expr* expr) {
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
                auto mint_env = environment_;
                script_function namespace_func = [eng, mint_env, overloads, namespace_id, fallback_class, member_id](const std::vector<script_value>& args) -> checked_result<script_value> {
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
                            // Create an environment with namespace variables accessible
                            auto ns_env = std::make_shared<environment>(mint_env, eng->get_symbolizer());

                            // Add all namespace variables to this environment
                            auto& namespaces = eng->script_namespaces();
                            auto ns_it = namespaces.find(namespace_id);
                            if (ns_it != namespaces.end()) {
                                for (const auto& [var_id, var_value] : ns_it->second->variables) {
                                    ns_env->define(var_id, var_value);
                                }
                            }

                            // Found matching arity - create script_defined_function with namespace environment
                            script_callable payload;
                            payload.kind = script_callable::kind_type::function;
                            payload.fn = std::make_shared<script_defined_function>(
                                func_decl->name,
                                shared_parameters_for(func_decl.get()),
                                func_decl->return_type,
                                func_decl->body,
                                ns_env  // Environment with namespace variables
                            );
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
                    return checked_result<script_value>(make_error_code(runtime_error_code::not_a_function),
                        "No matching overload in namespace '{0}' for {1} arguments",
                        namespace_id, static_cast<uint64_t>(args.size()));
                };

                push_value(script_value::make_function(namespace_func, engine_));
                return {};
            }

            // Look for variable in namespace
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
            // Class not found
            return class_var_result.error_value();
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

        // Try static field access
        // get_static_field returns null if not found
        script_value static_value = class_def->get_static_field(expr->member_id);
        if (!static_value.is_null()) {
            push_value(std::move(static_value));
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

        // Class has no static member
        return checked_result<void>(make_error_code(runtime_error_code::static_member_not_found),
            "Class '{0}' has no static member '{1}'", name_id, expr->member_id);
    }

    // Check if this is a super:: member access
    bool is_super_access = expr->object->get_type() == node_type::super_expr;

    // Evaluate the object expression
    JAISCRIPT_TRY(dispatch_expr(expr->object.get()));
    script_value objectValue = pop_value();

    // Dereference if needed - subscript access returns references
    objectValue = objectValue.deref();

    // Handle null-safe member access (?.)
    if (expr->null_safe && objectValue.is_null()) {
        push_value(make_value());  // push null
        return {};
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
        script_value method = parent_def->get_method(expr->member_id);
        if (method.is_null()) {
            // Parent class has no method
            return checked_result<void>(make_error_code(runtime_error_code::member_not_found),
                "Parent class has no method '{0}'", expr->member_id);
        }

        // Return a bound method that calls the parent's implementation
        push_value(create_bound_method(objectValue, method));
        return {};
    }

    // Worker direct-field read (parallel_for v1): probing getters/methods below
    // COPIES shared method values out of class_definition state - a cross-worker
    // count race on the non-atomic strong_ptr. Fields live on the (exclusively
    // owned) instance; anything else needs method machinery - wall it.
    if (parallel_worker_ && objectValue.raw_storage_index() == script_value::TYPEID_OBJECT &&
        !engine_->allow_unsafe_parallel()) [[unlikely]] {
        auto& holder = objectValue.unchecked_get_object_storage();
        if (holder && holder->is_class_instance_wrapper && holder->data) {
            auto* inst = static_cast<class_instance*>(holder->data.get());
            if (script_value* field = inst->find_field_value(expr->member_id)) {
                push_value(field->is_reference() ? field->deref() : *field);
                return {};
            }
            return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                "only direct field access on class instances is allowed in a parallel body (engine::allow_unsafe_parallel(true) overrides)");
        }
    }

    // Handle coroutine_handle methods
    if (objectValue.is_object()) {
        auto objHolder = objectValue.get_object_holder();
        if (objHolder && objHolder->type_id == coroutine_handle_type_id_) {
            auto handle = std::static_pointer_cast<coroutine_handle>(objHolder->data);
            if (expr->member_id == resume_id_) {
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
    if (objectValue.is_string()) {
        auto methodIt = string_methods_.find(expr->member_id);
        if (methodIt != string_methods_.end()) {
            // Found the method in the registry
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the string value by moving it
            script_function boundMethod = [ctx = builtin_method_context{engine_, string_symbolizer_}, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                std::vector<script_value> scratch;
                return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
            };

            push_value(script_value::make_function(boundMethod, engine_));
            return {};
        }
        else {
            // Set exception state instead of throwing
            std::string member_str(expr->member);
            active_exception_value_ = make_value("String has no method '" + member_str + "'");
            current_exception_ = script_exception("String has no method '" + member_str + "'", expr->location);
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
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the array value by moving it
            script_function boundMethod = [ctx = builtin_method_context{engine_, string_symbolizer_}, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                std::vector<script_value> scratch;
                return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
            };

            push_value(script_value::make_function(boundMethod, engine_));
            return {};
        }
        else {
            // Set exception state instead of throwing
            std::string member_str(expr->member);
            active_exception_value_ = make_value("Array has no method '" + member_str + "'");
            current_exception_ = script_exception("Array has no method '" + member_str + "'", expr->location);
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
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the map value by moving it
            script_function boundMethod = [ctx = builtin_method_context{engine_, string_symbolizer_}, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
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
                "Map has no method or key '{0}'", expr->member_id);
        }
    }

    // Handle weak_ptr methods
    if (objectValue.is_weak_ptr()) {
        auto methodIt = weak_ptr_methods_.find(expr->member_id);
        if (methodIt != weak_ptr_methods_.end()) {
            // Found the method in the registry
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the weak_ptr value by moving it
            script_function boundMethod = [ctx = builtin_method_context{engine_, string_symbolizer_}, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                std::vector<script_value> scratch;
                return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
            };

            push_value(script_value::make_function(boundMethod, engine_));
            return {};
        }
        else {
            // weak_ptr has no method
            return checked_result<void>(make_error_code(runtime_error_code::member_not_found),
                "weak_ptr has no method '{0}'", expr->member_id);
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
                const builtin_method& method = methodIt->second;

                // Create a wrapper function that captures the shared_ptr value by moving it
                script_function boundMethod = [ctx = builtin_method_context{engine_, string_symbolizer_}, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
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

    // Check if it's an object (only objects have members/methods)
    if (!objectValue.is_object()) {
        // Cannot access member on non-object type
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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

    // Check for property getter method (C++ properties and inherited properties)
    // OPTIMIZATION 1: Skip getter lookup entirely if class has no property getters
    // OPTIMIZATION 2: Cache getter_id on AST node to avoid repeated string allocation + interning
    if (target.class_def && target.class_def->has_property_getters()) {
        try {
            uint64_t getter_id = expr->getter_id;
            if (getter_id == UINT64_MAX) {
                auto [id, _] = string_symbolizer_->get_getter_id_with_view(member_id);
                getter_id = id;
                expr->getter_id = getter_id;
            }
            script_value getter = target.method(getter_id);
            if (!getter.is_null() && !getter.is_invalid() && getter.is_function()) {
                // Call the getter with 'this' as argument
                const script_function& func = getter.as_function();
                std::vector<script_value> args = {objectValue};
                auto result = func(args);
                if (!result) {
                    return result.error_value();
                }
                // Getters for registered-class members return non-owning cpp_bound
                // references into the receiver - pin the receiver's owning data (same
                // anchor create_bound_method applies to chained method returns) so
                // makeFoo().member doesn't dangle when the temporary receiver drops.
                script_value& getterResult = result.value();
                if (getterResult.is_non_owning_object()) {
                    auto resultHolder = getterResult.get_object_holder();
                    auto receiverHolder = objectValue.get_object_holder();
                    if (resultHolder && receiverHolder) {
                        resultHolder->keep_alive = receiverHolder->data ? receiverHolder->data
                                                                        : receiverHolder->keep_alive;
                    }
                }
                push_value(std::move(result.value()));
                return {};
            }
        } catch (const std::exception&) {
            // If the getter fails (e.g., class definition expired), fall back to field access
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
                    push_value(create_bound_method(unwrapped, unwrapped_method));
                    return {};
                }
            }
        }
    }

    // Set exception state instead of throwing
    std::string member_str(expr->member);
    active_exception_value_ = make_value("Object has no member '" + member_str + "'");
    current_exception_ = script_exception("Object has no member '" + member_str + "'", expr->location);
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
                    // Check environment first, then call-frame slots (locals live there).
                    if (environment_->contains(var_id)) {
                        if (capture_by_ref) {
                            script_value* targetPtr = environment_->get_value_ptr(var_id);
                            if (targetPtr) {
                                // Cell share (escape-legal): the capture stays live after
                                // the captured env dies
                                captureEnv->define(var_id, share_boxed_env_storage(*targetPtr, engine_));
                            }
                        } else {
                            auto capture_result = environment_->get(var_id);
                            if (capture_result) {
                                captureEnv->define(var_id, clone_for_capture(capture_result.value(), string_symbolizer_));
                            }
                        }
                    } else if (!call_stack_.empty()) {
                        // Local lives in the outer call frame (slot-based storage).
                        // Find it via the outer_slot_refs we already collected — the
                        // slot-scan above already resolved outer slots to symbol_ids.
                        for (const auto& ref : outer_slot_refs) {
                            if (ref.symbol_id == var_id) {
                                script_value* slot_val = call_stack_.back().get_local(ref.outer_slot);
                                if (slot_val) {
                                    if (capture_by_ref) {
                                        // Cell share (escape-legal, C++ [&] semantics):
                                        // the slot boxes in place and the capture shares
                                        // the cell - writes land in the original local
                                        // and the cell outlives the frame
                                        captureEnv->define(var_id, share_boxed_env_storage(*slot_val, engine_));
                                    } else {
                                        captureEnv->define(var_id, clone_for_capture(slot_val->deref(), string_symbolizer_));
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        // Process explicit captures
        for (const auto& capture : expr->captures) {
            // Special handling for 'this' - method_environment provides it via get() override
            // even though contains() might return false
            // Check environment first; if not there check the outer call-frame
            // (locals with slot_index live there, not in environment_).
            bool can_capture = environment_->contains(capture.symbol_id);
            bool capture_from_slot = false;
            script_value* slot_val = nullptr;

            if (!can_capture && capture.symbol_id == this_id) {
                if (environment_->get_value_ptr(this_id)) can_capture = true;
            }

            if (!can_capture && !call_stack_.empty()) {
                // Walk outer_slot_refs to find this symbol's outer slot
                for (const auto& ref : outer_slot_refs) {
                    if (ref.symbol_id == capture.symbol_id) {
                        slot_val = call_stack_.back().get_local(ref.outer_slot);
                        if (slot_val) { can_capture = true; capture_from_slot = true; }
                        break;
                    }
                }
            }

            if (can_capture) {
                if (capture_from_slot) {
                    if (capture.by_reference) {
                        // Cell share (escape-legal, C++ [&x] semantics): the slot boxes
                        // in place; the closure and the original local share the cell
                        captureEnv->define(capture.symbol_id, share_boxed_env_storage(*slot_val, engine_));
                    } else {
                        captureEnv->define(capture.symbol_id, clone_for_capture(slot_val->deref(), string_symbolizer_));
                    }
                } else if (capture.by_reference) {
                    script_value* targetPtr = environment_->get_value_ptr(capture.symbol_id);
                    if (targetPtr) {
                        // Cell share (escape-legal): the capture stays live after the
                        // captured env dies
                        captureEnv->define(capture.symbol_id, share_boxed_env_storage(*targetPtr, engine_));
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::capture_reference_failed),
                            "Cannot capture variable '{0}' by reference", capture.symbol_id);
                    }
                } else {
                    auto capture_result = environment_->get(capture.symbol_id);
                    if (!capture_result) return capture_result.error_value();
                    captureEnv->define(capture.symbol_id, clone_for_capture(capture_result.value(), string_symbolizer_));
                }
            } else {
                return checked_result<void>(make_error_code(runtime_error_code::capture_undefined_variable),
                    "Cannot capture undefined variable '{0}'", capture.symbol_id);
            }
        }

        // Materialise outer-frame locals into the capture env and patch their
        // slot_index to SIZE_MAX so every subsequent call resolves via the env
        // (O(1) hash lookup) rather than the wrong call-frame slot.
        if (!call_stack_.empty()) {
            for (auto& ref : outer_slot_refs) {
                // Skip if already captured explicitly / by default-capture above
                // (ref.node is null when replaying a stored plan — already patched then)
                if (captureEnv->contains(ref.symbol_id)) {
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
                        captureEnv->define(ref.symbol_id, clone_for_capture(slot_val->deref(), string_symbolizer_));
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

            // Copy all captured variables (except 'this') into the method_environment
            for (const auto& capture : expr->captures) {
                if (capture.symbol_id != this_id && captureEnv->contains(capture.symbol_id)) {
                    auto capture_result = captureEnv->get(capture.symbol_id);
                    if (capture_result) {
                        method_env->define(capture.symbol_id, std::move(capture_result.value()));
                    }
                }
            }

            // Also copy default-captured variables
            if (has_default_capture) {
                for (const auto& varName : used_variables) {
                    uint64_t var_id = string_symbolizer_->intern(varName);
                    if (var_id != this_id && captureEnv->contains(var_id)) {
                        auto var_result = captureEnv->get(var_id);
                        if (var_result) {
                            method_env->define(var_id, std::move(var_result.value()));
                        }
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
    std::shared_ptr<script_defined_function> lambdaFunc = needs_capture_env ? nullptr : expr->mint_cache;
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
            needs_capture_env ? final_closure_env : nullptr  // Only use closure env if we have captures
        );
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

checked_result<void> interpreter::visit_new_expr(new_expr* expr) {
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
            JAISCRIPT_TRY(dispatch_expr(expr->arguments[0].get()));
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
        for (const auto& argExpr : expr->arguments) {
            JAISCRIPT_TRY(dispatch_expr(argExpr.get()));
            args.push_back(std::move(pop_value()));
        }

        // Look up T's constructor
        auto ctor_result = environment_->get(innerTypeName);
        if (!ctor_result || !ctor_result.value().is_function()) {
            return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                "No constructor found for class '{0}'", inner_type->id);
        }

        script_value constructorFunc = std::move(ctor_result.value());
        const script_function& func = constructorFunc.as_function();
        auto result = func(args);
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
    for (const auto& argExpr : expr->arguments) {
        JAISCRIPT_TRY(dispatch_expr(argExpr.get()));
        args.push_back(std::move(pop_value()));
    }

    // Look for a constructor function registered with this class name
    // The class builder registers constructors as overloaded functions
    auto ctor_result = environment_->get(className);
    if (ctor_result && ctor_result.value().is_function()) {
        script_value constructorFunc = std::move(ctor_result.value());
        const script_function& func = constructorFunc.as_function();
        auto result = func(args);
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

checked_result<void> interpreter::visit_ternary_expr(ternary_expr* expr) {
    // Evaluate the condition
    JAISCRIPT_TRY(dispatch_expr(expr->condition.get()));
    script_value conditionValue = pop_value();

    // OPTIMIZATION: If condition is guaranteed to return bool, skip type dispatch
    bool conditionIsTruthy;
    if (expression_returns_bool(expr->condition.get())) {
        conditionIsTruthy = conditionValue.unchecked_as_bool();
    } else {
        conditionIsTruthy = is_truthy(conditionValue);
    }

    // Evaluate only the selected branch (short-circuit evaluation)
    if (conditionIsTruthy) {
        JAISCRIPT_TRY(dispatch_expr(expr->then_expression.get()));
    } else {
        JAISCRIPT_TRY(dispatch_expr(expr->else_expression.get()));
    }
    return {};
}

checked_result<void> interpreter::visit_array_literal_expr(array_literal_expr* expr) {
    // Create array script_value with no element type constraint
    // Untyped array literals (e.g., [1, 2, 3]) allow any element type
    // The type will be set by the variable declaration if one exists (e.g., array<int> arr = [...])
    type_info_ptr element_type = nullptr;  // No constraint - allows any type
    script_value arrayValue = script_value::make_array(element_type, engine_);

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
    for (const auto& element : expr->elements) {
        JAISCRIPT_TRY(dispatch_expr(element.get()));
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

checked_result<void> interpreter::visit_map_literal_expr(map_literal_expr* expr) {
    // Create map script_value with no key/value type constraints
    // Untyped map literals (e.g., {"a": 1}) allow any key/value types
    // The type will be set by the variable declaration if one exists (e.g., map<string,int> m = {...})
    type_info_ptr keyType = nullptr;    // No constraint - allows any key type
    type_info_ptr valueType = nullptr;  // No constraint - allows any value type
    script_value mapValue = script_value::make_map(keyType, valueType, engine_);

    // Get the internal map to populate
    auto& map = const_cast<script_map&>(mapValue.as_map());

    // Evaluate each key-value pair and add to map
    for (const auto& entry : expr->entries) {
        // Evaluate key
        JAISCRIPT_TRY(dispatch_expr(entry.first.get()));
        script_value key = pop_value();

        // Evaluate value
        JAISCRIPT_TRY(dispatch_expr(entry.second.get()));
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

checked_result<void> interpreter::visit_throw_expr(throw_expr* expr) {
    if (expr->value) {
        // Evaluate the expression to throw
        JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
        script_value val = pop_value();

        // Store the exception value and convert to string for exception message
        active_exception_value_ = val;
        std::string message = val.to_string();
        current_exception_ = script_exception(message, expr->location);
    } else {
        // Re-throw current exception
        if (!current_exception_) {
            // No exception to re-throw (note: this uses exception unwinding, not error codes)
            throw script_exception("No exception to re-throw", expr->location);
        }
        // Keep the existing active_exception_value_
    }

    is_unwinding_ = true;
    return {};
}

checked_result<void> interpreter::visit_yield_expr(yield_expr* expr) {
    if (!active_coroutine_) {
        return checked_result<void>(make_error_code(runtime_error_code::evaluation_failed),
            "yield used outside of coroutine");
    }

    // Evaluate the yield value (if any)
    script_value value = make_value();
    if (expr->value) {
        JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
        value = pop_value();
    }

    // Always yield - store value and set the request flag
    active_coroutine_->do_yield(std::move(value));
    hasYieldRequest_ = true;

    // Push null onto value stack (yield is an expression, result on resume is null)
    push_value(make_value());
    return {};
}

checked_result<void> interpreter::visit_if_stmt(if_stmt* stmt) {
    bool is_true;
    bool resuming = false;

    // Check if we're resuming into a branch from a coroutine yield
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        auto* cont = coro_state.peek_continuation(stmt);
        if (cont) {
            is_true = (cont->index == 0);  // 0=then branch, 1=else branch
            coro_state.pop_continuation();
            resuming = true;
        }
    }

    if (!resuming) {
        // Evaluate the condition
        JAISCRIPT_TRY(dispatch_expr(stmt->condition.get()));
        script_value conditionValue = pop_value();

        // OPTIMIZATION: If condition is guaranteed to return bool, skip type dispatch
        if (expression_returns_bool(stmt->condition.get())) {
            is_true = conditionValue.unchecked_as_bool();
        } else {
            is_true = is_truthy(conditionValue);
        }
    }

    // Execute appropriate branch based on truthiness
    if (is_true) {
        JAISCRIPT_TRY(dispatch_stmt(stmt->then_statement.get()));
        if (hasYieldRequest_ && active_coroutine_) {
            coroutine_state(*active_coroutine_).push_continuation(stmt, 0);  // then branch
        }
    } else if (stmt->else_statement) {
        JAISCRIPT_TRY(dispatch_stmt(stmt->else_statement.get()));
        if (hasYieldRequest_ && active_coroutine_) {
            coroutine_state(*active_coroutine_).push_continuation(stmt, 1);  // else branch
        }
    }
    return {};
}

checked_result<void> interpreter::visit_while_stmt(while_stmt* stmt) {
    // OPTIMIZATION: Pre-check if condition is guaranteed to return bool
    const bool condition_returns_bool = expression_returns_bool(stmt->condition.get());

    // Check if we're resuming into this while loop from a coroutine yield
    bool skip_first_condition = false;
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        auto* cont = coro_state.peek_continuation(stmt);
        if (cont) {
            skip_first_condition = true;
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
            JAISCRIPT_TRY(dispatch_expr(stmt->condition.get()));

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
        auto result = dispatch_stmt(stmt->body.get());
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
                coroutine_state(*active_coroutine_).push_continuation(stmt, 0);
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
                else if (update_assign->value->get_type() == node_type::identifier_expr) {
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
                else if (update_assign->value->get_type() == node_type::identifier_expr) {
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
                } else if (cond_binary->right->get_type() == node_type::identifier_expr) {
                    auto* cond_end_id = static_cast<identifier_expr*>(cond_binary->right.get());
                    // End is a variable - we'll bind to its pointer
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
                            auto loop_env = get_pooled_environment(previous);
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
                                body_env = get_pooled_environment(loop_env);
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
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        auto* cont = coro_state.peek_continuation(stmt);
        if (cont) {
            resuming_loop = true;
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
        auto loop_env = get_pooled_environment(environment_);
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
        }
    }

    // Error capture for lambdas (avoid throwing from hot path)
    std::optional<checked_result<void>> error;

    // OPTIMIZATION: Pre-check if condition is guaranteed to return bool
    // This allows us to skip is_truthy() type dispatch entirely
    const bool condition_returns_bool = stmt->condition && expression_returns_bool(stmt->condition.get());

    // Lambda helpers (ChaiScript-style) for cleaner, more optimizable code
    auto eval_condition = [&]() -> bool {
        if (!stmt->condition) return true;  // No condition = infinite loop

        auto result = dispatch_expr(stmt->condition.get());
        if (!result) {
            error = result;  // Capture error, signal loop termination
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

        auto result = dispatch_expr(stmt->update.get());
        if (!result) {
            error = result;  // Capture error, loop will terminate on next condition check
            return;
        }

        // Discard the update result if it leaves a value on the stack (optimization)
        if (!valueStack_.empty()) {
            valueStack_.discard();
        }
    };

    // Skip first condition if resuming (we were mid-iteration when yield happened)
    bool skip_condition = resuming_loop;

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
        if (error) break;

        // Execute the loop body
        auto result = dispatch_stmt(stmt->body.get());
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
                coroutine_state(*active_coroutine_).push_continuation(stmt, 0, environment_);
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

    // Release the loop environment to destroy all loop variables (only if we created it)
    if (owns_scope) {
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
    // Container evaluates in the enclosing scope on this thread; the region kernel
    // owns everything else (admission, element proof, capture provisioning, fork-join).
    JAISCRIPT_TRY(dispatch_expr(stmt->container.get()));
    script_value container = pop_value();
    return detail::run_parallel_for(*engine_, stmt, container, environment_);
}

checked_result<void> interpreter::visit_range_for_stmt(range_for_stmt* stmt) {
    // Coroutine resume replay: continue the SUSPENDED iteration - restoring the
    // evaluated container and position - instead of re-evaluating the container
    // (which would re-mint an inner coroutine or restart the sequence).
    bool replaying = false;
    size_t resume_index = 0;
    script_value container = make_value();
    if (active_coroutine_) {
        auto& coro_state = coroutine_state(*active_coroutine_);
        if (auto* cont = coro_state.peek_continuation(stmt)) {
            if (cont->saved_container) {
                replaying = true;
                resume_index = cont->index;
                container = *cont->saved_container;
            }
            coro_state.pop_continuation();
        }
    }
    if (!replaying) {
        // Evaluate the container expression
        JAISCRIPT_TRY(dispatch_expr(stmt->container.get()));
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
                    *loop_var_ptr = script_value::make_element_reference(array_storage, i, engine_, element_constraint);
                } else {
                    // Copy binding: values deep-copy per iteration, shared_ptr
                    // elements share (for (auto e : registry) e.hurt() mutates)
                    *loop_var_ptr = array_storage->is_typed()
                        ? array_storage->get(i, engine_)   // raw buffer read
                        : clone_for_assignment(array_storage->values()[i]);
                }

                // Execute loop body
                auto body_result = dispatch_stmt(stmt->body.get());
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
                        coroutine_state(*active_coroutine_).push_continuation(stmt, i, nullptr, container);
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
                        // Copy binding: keys stay detached snapshots (a shared key
                        // handle would let mutation break map ordering); values
                        // deep-copy, shared_ptr values share
                        args.push_back(it->first.clone());
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
                auto body_result = dispatch_stmt(stmt->body.get());
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
                        coroutine_state(*active_coroutine_).push_continuation(stmt, entry_index, nullptr, container);
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

                    *loop_var_ptr = std::move(resume_result.value());
                }

                auto body_result = dispatch_stmt(stmt->body.get());
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
                        coroutine_state(*active_coroutine_).push_continuation(stmt, 0, nullptr, container);
                    }
                    return {};
                }
            }
        } else {
            pop_scope();
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
        }
    } else {
        pop_scope();
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    // Pop the loop scope (only on normal exit, not yield)
    pop_scope();
    return {};
}

checked_result<void> interpreter::visit_return_stmt(return_stmt* stmt) {
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
                if (ident->slot_index != SIZE_MAX && !call_stack_.empty()) {
                    storage = call_stack_.back().get_local(ident->slot_index);
                }
                if (!storage) {
                    storage = environment_->get_value_ptr(ident->symbol_id);
                }
                if (!storage) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        "Cannot take reference of undefined variable", ident->symbol_id);
                }
                returnValue_ = share_boxed_env_storage(*storage, engine_);
                hasReturnValue_ = true;
                return {};
            }
            if (detail::is_ref_bindable_lvalue(stmt->value.get())) {
                auto resolved = detail::resolve_ref_lvalue(stmt->value.get(),
                    detail::caller_frame_view{call_stack_.empty() ? nullptr : &call_stack_.back()},
                    environment_.get(), engine_, string_symbolizer_);
                if (!resolved) {
                    return resolved.error_value();
                }
                returnValue_ = std::move(resolved.value());
                hasReturnValue_ = true;
                return {};
            }
            // Fall through: calls that already yield a reference pass; anything else
            // rejects at the call_function epilogue's ref-return branch
        }
        // Evaluate the return expression
        JAISCRIPT_TRY(dispatch_expr(stmt->value.get()));
        returnValue_ = std::move(pop_value());
    } else {
        // Return null if no expression
        returnValue_ = make_value();
    }

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

    // Reset state for try block
    // Don't reset exception state if we're in a catch block (allows re-throw)
    if (current_catch_var_id_ == 0) {
        current_exception_.reset();
        active_exception_value_ = make_value();
    }
    is_unwinding_ = false; trace_captured_ = false;
    current_catch_var_id_ = 0;

    // Execute try block
    auto try_result = dispatch_stmt(stmt->try_block.get());

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
    bool caught_error = false;

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
        // Reset unwinding flag
        is_unwinding_ = false; trace_captured_ = false;

        // Set the current catch variable ID so identifier lookup can find it (symbolize once here)
        if (auto eng = engine_) {
            current_catch_var_id_ = eng->symbolize(stmt->catch_var);
        }

        // Execute catch block
        auto catch_result = dispatch_stmt(stmt->catch_block.get());
        // Propagate errors from catch block
        if (!catch_result) {
            current_catch_var_id_ = saved_catch_var_id;
            return catch_result;
        }

        // Clear catch variable
        current_catch_var_id_ = 0;

        // Only clear exception if it wasn't re-thrown
        if (!is_unwinding_) {
            current_exception_.reset();
            active_exception_value_ = make_value();
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
    // Evaluate the switch condition
    JAISCRIPT_TRY(dispatch_expr(stmt->condition.get()));
    script_value switch_value = pop_value();

    // Save and set switch state
    bool old_in_switch = in_switch_;
    bool old_should_fallthrough = should_fallthrough_;
    in_switch_ = true;
    should_fallthrough_ = false;

    try {
        bool matched = false;
        bool executed_case = false;

        // Check each case
        for (const auto& case_stmt : stmt->cases) {
            // Evaluate case value
            auto result = dispatch_expr(case_stmt->value.get());
            if (!result) {
                in_switch_ = old_in_switch;
                should_fallthrough_ = old_should_fallthrough;
                return result;
            }
            script_value case_value = pop_value();

            // Check if values match using operator==
            bool case_matches = false;
            try {
                case_matches = (switch_value == case_value);
            } catch (const std::exception&) {
                // If comparison fails, treat as non-match
                case_matches = false;
            }

            // Execute case if it matches OR if we're falling through from a previous case
            if (case_matches || (executed_case && should_fallthrough_)) {
                matched = true;
                executed_case = true;

                // Reset fallthrough flag for this case (will be set again if case contains fallthrough statement)
                should_fallthrough_ = false;

                // Create a new scope for the case body (like an if statement)
                auto previous = environment_;
                auto case_env = get_pooled_environment(environment_);  // Use pool!
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
                    if (!hasYieldRequest_) {
                        release_environment(case_env);
                    }
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
            auto default_env = get_pooled_environment(environment_);  // Use pool!
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
                if (!hasYieldRequest_) {
                    release_environment(default_env);
                }
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

checked_result<void> interpreter::visit_case_stmt(case_stmt* stmt) {
    // Execute all statements in the case body
    // Note: The scope is created by visit_switch_stmt when it decides to execute this case
    for (const auto& s : stmt->body) {
        auto result = dispatch_stmt(s.get());
        if (!result) return result;

        // Stop the case body on return, exception unwind, OR a break/continue request:
        // break terminates the switch, continue targets an enclosing loop; either way
        // the rest of the case body must not run.
        if (hasReturnValue_ || is_unwinding_ || hasBreakRequest_ || hasContinueRequest_) {
            break;
        }
    }
    return {};
}

checked_result<void> interpreter::visit_default_stmt(default_stmt* stmt) {
    // Execute all statements in the default body
    // Note: The scope is created by visit_switch_stmt when it decides to execute the default
    for (const auto& s : stmt->body) {
        auto result = dispatch_stmt(s.get());
        if (!result) return result;

        // See visit_case_stmt: stop on break/continue as well as return/unwind.
        if (hasReturnValue_ || is_unwinding_ || hasBreakRequest_ || hasContinueRequest_) {
            break;
        }
    }
    return {};
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
        if (!func_decl_ptr) {
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
                            captureEnv->define(sym, clone_for_capture(slot_val->deref(), string_symbolizer_));
                        }
                    }
                }
                if (captureEnv) {
                    closure_env = captureEnv;
                }
            }
        }
        engine* eng = engine_;
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
    // the mint node-cacheable: every execution of this declaration is identical)
    auto& scriptFunc = decl->mint_cache;
    if (!scriptFunc) {
        scriptFunc = std::make_shared<script_defined_function>(
            decl->name,
            shared_parameters_for(decl),
            decl->return_type,
            decl->body,
            nullptr,  // No closure needed - environment stack handles everything
            decl->local_count  // Slot count for stack allocation
        );
    }

    // Create wrapper function resolving the live backend at call time. Named thunk so
    // callers can recover the payload via target<script_callable_thunk>() - vm parity.
    script_callable payload;
    payload.kind = script_callable::kind_type::function;
    payload.fn = scriptFunc;
    script_value functionValue = script_value::make_function(
        script_function(script_callable_thunk{engine_, std::move(payload)}), engine_);

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
    
    // Collect new field defaults and methods (using uint64_t IDs for performance)
    std::unordered_map<uint64_t, script_value> new_field_defaults;
    std::unordered_map<uint64_t, type_info_ptr> new_field_types; // declared types (typed fields enforce like locals)
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
                    default_val = pop_value();

                    // Ensure the default value has an engine reference
                    if (!default_val.has_valid_engine() && engine_) {
                        default_val.set_engine(engine_);
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
                
            } else if (method_name.size() > 0 && method_name[0] == '~') {
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
        engine* ctor_eng = engine_;
        auto ctor_dispatcher = [ctor_eng, ctor_payload](const std::vector<script_value>& args) -> checked_result<script_value> {
            execution_backend* backend = ctor_eng ? ctor_eng->get_execution_backend() : nullptr;
            if (!backend) {
                return checked_result<script_value>(make_error_code(runtime_error_code::engine_destroyed), "Interpreter was destroyed before constructor call");
            }
            return backend->execute_callable(ctor_payload, args);
        };

        // Register the dispatcher in global environment (constructors are always global)
        get_global_environment()->define(decl->name_id, script_value::make_function(ctor_dispatcher, engine_));
    }

    // If no constructor was found, create a default constructor
    else {
        // Create a default constructor that just initializes the instance
        script_callable ctor_payload;
        ctor_payload.kind = script_callable::kind_type::constructor;
        ctor_payload.cls = class_def;
        engine* ctor_eng = engine_;
        auto default_ctor_func = [ctor_eng, ctor_payload](const std::vector<script_value>& args) -> checked_result<script_value> {
            execution_backend* backend = ctor_eng ? ctor_eng->get_execution_backend() : nullptr;
            if (!backend) {
                return checked_result<script_value>(make_error_code(runtime_error_code::internal_error),
                    "Interpreter was destroyed before constructor call");
            }
            return backend->execute_callable(ctor_payload, args);
        };

        // Register default constructor in global environment (constructors are always global)
        get_global_environment()->define(decl->name_id, script_value::make_function(default_ctor_func, engine_));
        // std::cerr << "DEBUG: Registered default constructor for class: " << decl->name << std::endl;
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
                evaluated_value = pop_value();
            }

            // Ensure the value has an engine reference
            if (!evaluated_value.has_valid_engine() && engine_) {
                evaluated_value.set_engine(engine_);
            }

            field_defaults_with_engine[field_id] = evaluated_value;
        }

        // Generate getter and setter methods for all fields (including new ones)
        // This is needed for hot reload to work properly with property access
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
            new_methods[getter_id] = script_value::make_function(getter, engine_);

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
            new_methods[setter_id] = script_value::make_function(setter, engine_);
        }
        
        
        // Declared types install first so redefine_class flags a retype as fields_changed
        // and migrate_fields converts against the NEW types (retype ruling)
        class_def->replace_field_declared_types(std::move(new_field_types), true);

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
            class_def->add_method_by_id(getter_id, getter, true);  // true = is_property_getter

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
            class_def->add_method_by_id(setter_id, setter, true);  // true = synthesized accessor
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

    // reflect:: is engine truth — script declarations into it are impersonation
    if (is_reserved_namespace_name(decl->name)) {
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
            "Namespace '{0}' is reserved for the engine (reflect:: is C++-registered only)",
            decl->name_id);
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

            // Evaluate variable initializer and store value
            if (var_decl->initializer) {
                JAISCRIPT_TRY(dispatch_expr(var_decl->initializer.get()));
                script_value value = pop_value();
                ns_data->variables[var_decl->name_id] = value;
            } else {
                // No initializer - store null
                ns_data->variables[var_decl->name_id] = make_value();
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

checked_result<void> interpreter::visit_expression_decl(expression_decl* decl) {
    // Evaluate the expression and leave the result on the stack
    // This allows top-level expressions to return values
    return dispatch_expr(decl->expression.get());
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
    auto result = engine_ptr->execute_source(content, jai::instance_variables{}, resolved_path);

    // Push the result onto the value stack
    push_value(std::move(result));
    return {};
}

checked_result<void> interpreter::visit_include_decl(include_decl* decl) {
    return include_into_value_stack(decl->path_expr.get(), decl->path);
}

checked_result<void> interpreter::visit_include_expr(include_expr* expr) {
    return include_into_value_stack(expr->path_expr.get(), expr->path);
}

checked_result<void> interpreter::visit_import_decl(import_decl* decl) {
    // Get the engine reference
    auto engine_ptr = engine_;
    if (!engine_ptr) {
        // [ErrorText] Engine reference expired during import processing
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    // Get the path either from literal or expression
    std::string path;
    if (decl->path_expr) {
        // Evaluate the expression to get the path
        JAISCRIPT_TRY(dispatch_expr(decl->path_expr.get()));
        script_value path_value = pop_value();

        // Convert to string
        if (path_value.type() != script_value_type::jai_string_type) {
            // [ErrorText] Import path expression must evaluate to a string
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
        }
        path = path_value.as<std::string>();
    } else {
        // Use the literal path
        path = decl->path;
    }

    // Resolve the file path
    auto resolve_result = resolve_include_path(path, engine_ptr);
    if (!resolve_result) {
        return resolve_result.error_value();
    }
    std::string resolved_path = std::move(resolve_result.value());

    // Use the engine's public API to handle import with tracking
    auto result = engine_ptr->execute_import(resolved_path);

    // Push the result onto the value stack
    push_value(std::move(result));
    return {};
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
        auto coro_env = std::make_shared<environment>(method_env->get_parent(), string_symbolizer_, this_obj);
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

    // Execute method with the arguments - propagate errors to caller
    return call_function(script_func, args);
}

// Evaluate field initializers for a script class instance at construction time
// If skip_parent_recursion is true, only evaluate THIS class's field initializers (parents already handled)
void interpreter::evaluate_field_initializers(std::shared_ptr<class_instance> instance,
                                             std::shared_ptr<script_class_definition> class_def,
                                             std::shared_ptr<environment> init_env,
                                             bool skip_parent_recursion) {
    // First, evaluate parent class field initializers (if any)
    // Support multiple inheritance by iterating over all parent classes
    // Skip this if parents were already processed (e.g., by multi-level super() chain handling)
    if (!skip_parent_recursion) {
        for (const auto& parent : class_def->get_parent_classes()) {
            auto parent_script_class = std::dynamic_pointer_cast<script_class_definition>(parent);
            if (parent_script_class) {
                // Recursively evaluate parent field initializers
                evaluate_field_initializers(instance, parent_script_class, init_env, false);
            }
        }
    }

    // Now evaluate this class's field initializers
    const auto& field_initializers = class_def->get_field_initializer_asts();

    // Temporarily switch to the init environment for evaluation
    auto old_env = environment_;
    environment_ = init_env;

    for (const auto& [field_id, initializer_ast] : field_initializers) {
        if (initializer_ast) {
            // Evaluate the initializer expression
            auto result = dispatch_expr(initializer_ast.get());
            if (!result) {
                // Restore environment before throwing
                environment_ = old_env;
                throw runtime_error("Failed to evaluate field initializer for '" + std::string(string_symbolizer_->get_string(field_id)) + "'");
            }
            script_value field_value = pop_value();

            // Ensure the field value has an engine reference
            if (!field_value.has_valid_engine() && engine_) {
                field_value.set_engine(engine_);
            }

            // Set the field on the instance using the field_id directly
            instance->set_field(field_id, field_value);
        }
    }

    // Restore environment
    environment_ = old_env;
}

checked_result<script_value> interpreter::execute_callable(const script_callable& payload, const std::vector<script_value>& args) {
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

checked_result<script_value> interpreter::construct_instance(std::shared_ptr<script_class_definition> class_def,
                                                              std::shared_ptr<environment> definition_env,
                                                              const std::vector<script_value>& args) {
    
    // Get all constructor ASTs
    const auto& ctor_asts = class_def->get_constructor_asts();

    // Find constructor with matching parameter count AND types (trailing defaults open
    // the arity window; within a tier the ctor using FEWER defaults wins)
    // Priority: 1) exact type match, 2) numeric conversion match, 3) untyped fallback
    std::shared_ptr<function_decl> exact_match_ctor;
    std::shared_ptr<function_decl> convertible_match_ctor;
    std::shared_ptr<function_decl> arity_match_ctor;  // Fallback for untyped params
    size_t exact_defaults = SIZE_MAX, convertible_defaults = SIZE_MAX, arity_defaults = SIZE_MAX;

    for (const auto& ctor_ast : ctor_asts) {
        if (!arity_accepts(ctor_ast->parameters, args.size())) {
            continue;
        }
        const size_t defaults_used = ctor_ast->parameters.size() - args.size();

        if (defaults_used < arity_defaults) {
            arity_match_ctor = ctor_ast;
            arity_defaults = defaults_used;
        }

        // Check if all parameter types match exactly
        bool exact_match = true;
        bool convertible_match = true;
        for (size_t i = 0; i < args.size() && (exact_match || convertible_match); ++i) {
            const auto& param = ctor_ast->parameters[i];
            if (param.type && !param.type->type_name.empty()) {
                // Parameter has explicit type - check if arg is compatible
                auto arg_type = args[i].type();
                if (arg_type == script_value_type::jai_object_type) {
                    // For objects, check class name
                    auto resolved = resolve_member_target(args[i]);
                    if (resolved) {
                        if (resolved.class_name() != param.type->type_name) {
                            exact_match = false;
                            convertible_match = false;
                        }
                    } else {
                        exact_match = false;
                        convertible_match = false;
                    }
                } else {
                    // For primitives, check base type
                    if (arg_type != param.type->base_type) {
                        exact_match = false;
                        // Check if numeric conversion is allowed
                        bool is_numeric_conversion =
                            (arg_type == script_value_type::jai_int_type &&
                             param.type->base_type == script_value_type::jai_float_type) ||
                            (arg_type == script_value_type::jai_float_type &&
                             param.type->base_type == script_value_type::jai_int_type);
                        if (!is_numeric_conversion) {
                            convertible_match = false;
                        }
                    }
                }
            }
            // If param has no type, it accepts anything
        }

        if (exact_match && defaults_used < exact_defaults) {
            exact_match_ctor = ctor_ast;
            exact_defaults = defaults_used;
        }
        if (convertible_match && defaults_used < convertible_defaults) {
            convertible_match_ctor = ctor_ast;
            convertible_defaults = defaults_used;
        }
    }

    // Select best matching constructor: exact > convertible > arity fallback
    std::shared_ptr<function_decl> matching_ctor = exact_match_ctor;
    if (!matching_ctor) {
        matching_ctor = convertible_match_ctor;
    }
    if (!matching_ctor) {
        matching_ctor = arity_match_ctor;
    }

    if (!matching_ctor) {
        return checked_result<script_value>(make_error_code(runtime_error_code::no_constructor_found),
            "No constructor found with matching arguments");
    }
    
    // Create instance
    auto instance = class_def->create_instance();
    // Instance created

    // Create 'this' value using the class_def's registered name and type_id
    // This ensures we use the exact name/id that was registered (e.g., with namespace)
    // Note: is_class_instance_wrapper=true because instance is a class_instance object (script_class_instance inherits from class_instance)
    auto this_value = script_value::make_object(class_def->get_name(), class_def->get_type_id(), instance, engine_, true);

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
    for (size_t i = 0; i < matching_ctor->parameters.size() && i < args.size(); ++i) {
        init_env->define(matching_ctor->parameters[i].name, args[i]);
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
                        return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
                            "Failed to evaluate constructor initializer argument");
                    }
                    init_args.push_back(pop_value());
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
                        std::shared_ptr<function_decl> parent_ctor;
                        size_t parent_defaults = SIZE_MAX;
                        for (const auto& ctor_ast : parent_ctor_asts) {
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
                                                    return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
                                                        "Failed to evaluate super() argument");
                                                }
                                                ancestor_args.push_back(pop_value());
                                            }
                                            environment_ = old_env;

                                            auto ancestor_script = std::dynamic_pointer_cast<script_class_definition>(ancestor);
                                            if (ancestor_script) {
                                                // Ancestor is a script class - find matching constructor and add to chain
                                                // (fewest defaults used wins)
                                                const auto& ancestor_ctors = ancestor_script->get_constructor_asts();
                                                std::shared_ptr<function_decl> ancestor_ctor;
                                                size_t ancestor_defaults = SIZE_MAX;
                                                for (const auto& ac : ancestor_ctors) {
                                                    if (arity_accepts(ac->parameters, ancestor_args.size()) &&
                                                        ac->parameters.size() - ancestor_args.size() < ancestor_defaults) {
                                                        ancestor_ctor = ac;
                                                        ancestor_defaults = ac->parameters.size() - ancestor_args.size();
                                                    }
                                                }
                                                if (ancestor_ctor) {
                                                    ctor_chain.push_back({ancestor_script, ancestor_ctor, std::move(ancestor_args)});
                                                } else if (!ancestor_ctors.empty()) {
                                                    return checked_result<script_value>(make_error_code(runtime_error_code::no_constructor_found),
                                                        "No matching constructor for ancestor class");
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
                                            script_value field_value = pop_value();
                                            // field_id is already the interned ID (map is keyed by ID)
                                            instance->set_field(field_id, std::move(field_value));
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
                                auto ctor_result = execute_method_ast(it->ctor, method_env.get(), it->args);
                                if (!ctor_result) return ctor_result.error_value();
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
                    return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
                        "Failed to evaluate constructor initializer argument");
                }
                init_args.push_back(pop_value());
            }

            // Restore environment
            environment_ = old_env;
            
            // Find matching constructor in same class (fewest defaults used wins)
            const auto& ctor_asts = class_def->get_constructor_asts();
            std::shared_ptr<function_decl> target_ctor;
            size_t target_defaults = SIZE_MAX;
            for (const auto& ctor_ast : ctor_asts) {
                if (ctor_ast != matching_ctor && arity_accepts(ctor_ast->parameters, init_args.size()) &&
                    ctor_ast->parameters.size() - init_args.size() < target_defaults) {
                    target_ctor = ctor_ast;
                    target_defaults = ctor_ast->parameters.size() - init_args.size();
                }
            }
            
            if (!target_ctor) {
                return checked_result<script_value>(make_error_code(runtime_error_code::no_constructor_found),
                    "No matching constructor found for this() delegation");
            }
            
            // Apply this class's field initializers (declared defaults) BEFORE the
            // delegated-to ctor body, so the target can override them. Suppress the
            // post-loop re-run below so they are not applied twice (the clobber bug).
            evaluate_field_initializers(instance, class_def, init_env, handled_parent_init);
            delegated_to_this = true;

            // Call the target constructor on this instance with method environment
            // Use definition_env as parent
            scoped_method_environment target_method_env(
                this,
                definition_env,
                this_value,
                class_def.get()
            );

            auto target_result = execute_method_ast(target_ctor, target_method_env.get(), init_args);
            if (!target_result) return target_result.error_value();
        }
    }

    // Evaluate field initializers BEFORE executing the constructor body
    // Field initializers can access constructor parameters via init_env
    // If the iterative multi-level loop already handled parent field initializers,
    // skip recursive parent processing to avoid re-evaluating defaults.
    // Skip entirely when this ctor delegated via `: this(...)` — the target
    // already initialized the fields and re-running would clobber them (#24).
    if (!delegated_to_this) {
        evaluate_field_initializers(instance, class_def, init_env, handled_parent_init);
    }

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
    auto result = execute_method_ast(matching_ctor, method_env.get(), args);

    // Constructor executed and returned 'this'
    return result;
}

checked_result<script_value> interpreter::construct_default_instance(std::shared_ptr<script_class_definition> class_def,
                                                                      const std::vector<script_value>& args) {

    // Default constructor shouldn't have arguments
    if (!args.empty()) {
        return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
            "Default constructor takes no arguments");
    }
    
    // Create instance using inherited create_instance()!
    auto instance = class_def->create_instance();
    // Default constructor instance created

    // Create 'this' value for field initializer evaluation
    // Use class_def's registered name and type_id to handle namespaces correctly
    // Note: is_class_instance_wrapper=true because instance is a class_instance object (script_class_instance inherits from class_instance)
    auto this_value = script_value::make_object(class_def->get_name(), class_def->get_type_id(), instance, engine_, true);

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
    auto init_env = std::make_shared<environment>(global_env, string_symbolizer_);
    init_env->define("this", this_value);
    init_env->set_access_context(class_def.get());
    evaluate_field_initializers(instance, class_def, init_env);

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

// Stateless ref-param binding straight from the call-site arg expression. Kept out of
// call_function so its locals stay off the per-recursion-level native frame (Debug
// stack ceiling). KEEP BYTE-PARALLEL with vm_backend::bind_parameters' ref branch.
checked_result<void> interpreter::bind_reference_parameter(const parameter& param, size_t frame_index,
                                                           const expression* argExpr,
                                                           const std::shared_ptr<environment>& caller_env,
                                                           const script_value* evaluated_arg) {
    call_frame* caller_locals = frame_index >= 1 ? &call_stack_[frame_index - 1] : nullptr;

    if (argExpr && argExpr->get_type() == node_type::identifier_expr) {
        auto* identExpr = static_cast<const identifier_expr*>(argExpr);
        uint64_t symbol_id = identExpr->symbol_id;
        if (symbol_id == UINT64_MAX) {
            symbol_id = string_symbolizer_->intern(identExpr->name);
            const_cast<identifier_expr*>(identExpr)->symbol_id = symbol_id;
        }

        // Caller frame-slot local (env lookup can't see slot locals and would walk to
        // an unrelated same-named outer variable)
        if (identExpr->slot_index != SIZE_MAX && caller_locals) {
            if (script_value* slotPtr = caller_locals->get_local(identExpr->slot_index)) {
                return bind_reference_to_storage(*slotPtr, frame_index, param.slot_index);
            }
        }

        script_value* argPtr = caller_env ? caller_env->get_value_ptr(symbol_id) : nullptr;
        if (!argPtr) {
            return checked_result<void>(
                make_error_code(runtime_error_code::undefined_variable),
                "Cannot take reference of undefined variable"
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
    const auto* fn = static_cast<const script_defined_function*>(frame.debug_function);
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
    return out;
}

checked_result<script_value> interpreter::call_function(const script_defined_function& function, const std::vector<script_value>& args,
                                                        const method_call_override* method_ctx) {
    // Check recursion depth limit FIRST
    if (current_call_depth_ >= JAI_MAX_CALL_DEPTH) {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::max_recursion_depth),
            JAI_MAX_CALL_DEPTH_MESSAGE);
    }
    // Wide native frames (constructor chains especially) can exhaust the native stack
    // long before the depth cap - fail catchably instead of dying on 0xC00000FD.
    // Same message as the depth cap: the vm never trips this on script recursion
    // (in-loop frames), so a distinct text here breaks backend error parity.
    if (detail::native_stack_low()) [[unlikely]] {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::max_recursion_depth),
            JAI_MAX_CALL_DEPTH_MESSAGE);
    }

    if (execution_limit_exhausted()) [[unlikely]] {
        return execution_limit_failure();
    }

    // RAII guard for call depth tracking - ensures decrement even on early return
    struct call_depth_guard {
        int& depth;
        call_depth_guard(int& d) : depth(d) { ++depth; }
        ~call_depth_guard() { --depth; }
    } depth_guard(current_call_depth_);

    // Consume the pending call-site context (set by visit_call around the invoke): ref
    // params bind against the caller's variables through it. Consumed exactly once -
    // nested calls set their own; external (C++) invocations arrive with none.
    const detail::call_site_context* call_ctx = pending_call_ctx_;
    pending_call_ctx_ = nullptr;

    {
        size_t required_params = 0;
        for (const auto& p : function.parameters()) {
            if (!p.default_value) {
                ++required_params;
            } else {
                break;  // All parameters after first default also have defaults (parser validated)
            }
        }
        if (args.size() < required_params || args.size() > function.parameters().size()) {
            return checked_result<script_value>(
                make_error_code(runtime_error_code::argument_count_mismatch),
                "Function expected {0} arguments but got {1}",
                static_cast<uint64_t>(function.parameters().size()), static_cast<uint64_t>(args.size())
            );
        }
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
    call_stack_[frame_index].debug_function = &function;   // for naming slot locals at a breakpoint

    // Pre-allocate locals for all slots (params + local variables)
    // Slot indices are assigned by the parser at parse time
    call_stack_[frame_index].reserve_locals(function.local_count);

    // Set up closure environment for non-local lookups
    auto previousEnv = environment_;

    // Determine closure environment and method context
    // NOTE: Re-access call_stack_[frame_index] each time instead of caching reference
    if (method_ctx) {
        // Direct method dispatch: same net effect as the closure_env->is_method_env()
        // branch below (this in the frame, method scope env on definition_env), minus
        // the carrier method env and its per-call define("this")
        call_stack_[frame_index].set_this(*method_ctx->this_obj);
        call_stack_[frame_index].closure_env = *method_ctx->definition_env;
        environment_ = get_pooled_method_environment(*method_ctx->definition_env, *method_ctx->this_obj,
                                                     method_ctx->access_ctx);
    } else if (function.closure_env) {
        if (function.closure_env->is_method_env()) {
            // Method call - store 'this' in the call frame
            auto this_obj = function.closure_env->get_this_object();
            call_stack_[frame_index].set_this(this_obj);  // Uses set_this which sets is_method = true
            call_stack_[frame_index].closure_env = function.closure_env->get_parent();
            // Set environment to a method environment for 'this' field lookups in body
            environment_ = get_pooled_method_environment(function.closure_env->get_parent(), this_obj,
                                                         function.closure_env->get_access_context());
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
        } else {
            // Regular closure
            call_stack_[frame_index].closure_env = function.closure_env;
            environment_ = get_pooled_environment(function.closure_env);
        }
    } else {
        // Regular function - use current environment for closures
        call_stack_[frame_index].closure_env = previousEnv;
        environment_ = get_pooled_environment(previousEnv);
    }

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
        release_environment(function_env, false);
        hasReturnValue_ = previousHasReturn;
        returnValue_ = previousReturn;
    };

    // ============================================================
    // BIND PARAMETERS TO CALL FRAME
    // ============================================================
    // Parameters go into the call frame for fast lookup.
    // Reference parameters still need special handling.

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

        const auto& arg = args[i];

        // Use pre-cached symbol ID (parameter binding optimization)
        // Symbol IDs are cached at function definition time in visit_function_decl
        if (param.is_reference) {
            if (call_ctx && i < call_ctx->arg_exprs->size()) {
                auto bound = bind_reference_parameter(param, frame_index,
                                                      (*call_ctx->arg_exprs)[i].get(), previousEnv, &arg);
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
                    call_stack_[frame_index].set_local(param.slot_index, boxed_param(derefed.clone()));
                    continue;
                }
            }

            // Non-reference parameter - try to convert the argument to the parameter type if needed
            // (cleanup before propagating: an early return here would leak the callee frame/env)
            script_value converted_arg = make_value();
            {
                auto convert_result = try_convert_for_parameter(arg, param.type);
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
                // Deep copy - value semantics (C++-like default)
                call_stack_[frame_index].set_local(param.slot_index, boxed_param(converted_arg.clone()));
            }
        }
    }

    // Execute function body without creating another environment
    // (since we already created one for the function call)
    // Track which declaration index we're at (needed for coroutine yield)
    size_t last_body_idx = 0;
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

    // Get return value
    script_value result = make_value();

    // If yielding, save state for coroutine resume instead of cleaning up
    if (hasYieldRequest_) {
        if (active_coroutine_) {
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
        }
        call_stack_.erase(call_stack_.begin() + frame_index, call_stack_.end());
        // Restore caller's environment WITHOUT releasing
        // (the coroutine now owns its environment chain and call frames)
        environment_ = previousEnv;
        hasReturnValue_ = previousHasReturn;
        returnValue_ = previousReturn;
        return result;
    }

    // Unwinding through a return statement leaves a junk return slot: running the typed
    // return conversion on it would replace the in-flight exception (VM parity: unwinding
    // frames complete with their implicit result, conversion skipped)
    if (hasReturnValue_ && !is_unwinding_) {
        if (function.return_type && function.return_type->base_type == script_value_type::jai_reference_type) {
            // Reference return (int& f()): the HANDLE passes through - the holder's
            // owner pin is the lifetime, so escaping the frame is legal. KEEP BYTE-
            // PARALLEL with the vm's convert_return_value ref branch.
            auto passed = detail::ref_return_pass_through(std::move(returnValue_.value()),
                                                          function.return_type, string_symbolizer_);
            if (!passed) {
                cleanup();
                return passed.error_value();
            }
            result = std::move(passed.value());
        } else {
            // Value returns flatten BEFORE cleanup destroys the call frame: a plain
            // function's result is a copy, never an alias.
            if (returnValue_.value().is_reference()) {
                // Copy the target - can't move since target might be in outer scope
                result = returnValue_.value().deref();
            } else {
                // Not a reference - safe to move directly
                result = std::move(returnValue_.value());
            }

            // Apply return type conversion if needed
            // Skip conversion for void, auto, or any type (implicit return type accepts any value)
            if (function.return_type && !function.return_type->type_name.empty() &&
                function.return_type->type_name != "void" &&
                function.return_type->type_name != "auto" &&
                function.return_type->base_type != script_value_type::jai_any_type) {
                // cleanup before propagating: leaking hasReturnValue_/frame/env here poisons the
                // rest of the run once the caller catches (vm pop_script_frame_core ordering)
                auto convert_result = try_convert_for_parameter(result, function.return_type);
                if (!convert_result) {
                    cleanup();
                    return convert_result.error_value();
                }
                result = std::move(convert_result.value());
            }
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

void interpreter::validate_function_arguments(const std::vector<parameter>& params, const std::vector<script_value>& args) {
    if (params.size() != args.size()) {
        throw runtime_error("Function expected " + std::to_string(params.size()) +
                         " arguments but got " + std::to_string(args.size()));
    }

    // Type checking is now done in call_function via try_convert_for_parameter
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

checked_result<script_value> interpreter::try_convert_for_parameter(const script_value& arg, type_info_ptr target_type) {
    if (!target_type) {
        return arg;  // No type specified = any type accepted
    }

    // Dereference if the argument is a reference to get the actual value type
    // (deref() follows chains to a non-reference or throws - no residual-ref case)
    const script_value& derefed_arg = arg.deref();
    auto source_type = derefed_arg.storage_type();
    auto target_base_type = target_type->base_type;

    // Any type accepts anything - no conversion needed
    if (target_base_type == script_value_type::jai_any_type) return arg;

    // Exact match - no conversion needed
    if (source_type == target_base_type) {
        // For objects, also check the type name (with inheritance support)
        if (source_type == script_value_type::jai_object_type) {
            // First, try to get the type name from type_info
            auto source_type_info = derefed_arg.get_type_info();
            if (source_type_info && !source_type_info->type_name.empty() &&
                source_type_info->type_name == target_type->type_name) {
                return derefed_arg;  // Same object type - no conversion needed
            }
            // Also check the resolved class name (script classes, cpp_bound references)
            auto resolved = resolve_member_target(derefed_arg);
            if (resolved) {
                if (resolved.class_name() == target_type->type_name) {
                    return arg;  // Same object type - no conversion needed
                }
                // Check inheritance - derived types are compatible with base types
                if (resolved.class_def && resolved.class_def->is_subtype_of(target_type->type_name)) {
                    return arg;  // Derived type - compatible without conversion
                }
            }
            // Different object types - fall through to try constructor conversion
        } else {
            return arg;  // Non-object types match - no conversion needed
        }
    }

    // Null can be assigned to object types without conversion
    if (source_type == script_value_type::jai_null_type &&
        target_base_type == script_value_type::jai_object_type) {
        return arg;
    }

    // shared_ptr<T> handling
    auto source_type_info = derefed_arg.get_type_info();
    if (source_type_info && source_type_info->base_type == script_value_type::jai_shared_ptr_type) {
        // shared_ptr<T> -> shared_ptr<T> - same shared_ptr type
        if (target_base_type == script_value_type::jai_shared_ptr_type) {
            // Check if inner types match
            if (!source_type_info->type_name.empty() && source_type_info->type_name == target_type->type_name) {
                return arg;  // shared_ptr<T> -> shared_ptr<T> with matching inner type
            }
            // Also check via resolved class (with inheritance support)
            auto resolved = resolve_member_target(derefed_arg);
            if (resolved) {
                if (resolved.class_name() == target_type->type_name) {
                    return arg;
                }
                // Check inheritance
                if (resolved.class_def && resolved.class_def->is_subtype_of(target_type->type_name)) {
                    return arg;  // Derived type compatible with base
                }
            }
        }
        // shared_ptr<T> -> T - unwrap to regular object
        if (target_base_type == script_value_type::jai_object_type) {
            // Check if the inner type matches the target type (with inheritance)
            auto resolved = resolve_member_target(derefed_arg);
            if (resolved) {
                if (resolved.class_name() == target_type->type_name) {
                    return arg;  // shared_ptr<T> -> T is allowed (preserves reference semantics)
                }
                // Check inheritance
                if (resolved.class_def && resolved.class_def->is_subtype_of(target_type->type_name)) {
                    return arg;  // Derived type compatible with base
                }
            }
            // Check type_info's inner type name
            if (!source_type_info->type_name.empty() && source_type_info->type_name == target_type->type_name) {
                return arg;
            }
        }
    }

    // For object target types, try constructor-based conversion
    if (target_base_type == script_value_type::jai_object_type && !target_type->type_name.empty()) {
        const std::string& target_class_name = target_type->type_name;

        // First, check if there's a constructor that directly accepts the source type
        // This prevents chained conversions (A->B->C)
        auto eng = engine_;
        if (eng) {
            auto class_def = eng->get_class_definition(target_class_name);
            if (class_def) {
                auto script_class = std::dynamic_pointer_cast<script_class_definition>(class_def);
                if (script_class) {
                    // Check constructor ASTs for one that accepts the source type
                    const auto& ctor_asts = script_class->get_constructor_asts();
                    bool has_matching_ctor = false;

                    // Get source type info (name and class_def for inheritance)
                    std::string source_type_name;
                    class_definition* source_class_def = nullptr;
                    if (source_type == script_value_type::jai_object_type) {
                        auto resolved = resolve_member_target(derefed_arg);
                        if (resolved) {
                            source_type_name = resolved.class_name();
                            source_class_def = resolved.class_def;
                        }
                    }

                    for (const auto& ctor_ast : ctor_asts) {
                        if (ctor_ast->parameters.size() != 1) continue;

                        const auto& param = ctor_ast->parameters[0];
                        if (!param.type || param.type->type_name.empty()) {
                            // Untyped parameter - accepts anything
                            has_matching_ctor = true;
                            break;
                        }

                        // Check if parameter type matches source type (with inheritance)
                        if (source_type == script_value_type::jai_object_type) {
                            if (param.type->type_name == source_type_name) {
                                has_matching_ctor = true;
                                break;
                            }
                            // Check inheritance - derived types are accepted
                            if (source_class_def && source_class_def->is_subtype_of(param.type->type_name)) {
                                has_matching_ctor = true;
                                break;
                            }
                        } else {
                            // For primitives, check base_type match or numeric conversion
                            if (param.type->base_type == source_type ||
                                (source_type == script_value_type::jai_int_type &&
                                 param.type->base_type == script_value_type::jai_float_type) ||
                                (source_type == script_value_type::jai_float_type &&
                                 param.type->base_type == script_value_type::jai_int_type)) {
                                has_matching_ctor = true;
                                break;
                            }
                        }
                    }

                    if (!has_matching_ctor) {
                        // No constructor directly accepts the source type - don't allow chained conversion
                        return checked_result<script_value>(
                            make_error_code(runtime_error_code::type_mismatch),
                            "Cannot convert {0} to {1} (no suitable single-argument constructor)",
                            derefed_arg.type_id(), target_type->id);
                    }
                }
            }
        }

        // Try to find and call the constructor with the source value
        // Use global environment since constructors are always registered at global scope
        auto ctor_result = get_global_environment()->get(target_class_name);
        if (ctor_result && ctor_result.value().is_function()) {
            const script_function& ctor = ctor_result.value().as_function();

            // Call constructor with the source value as argument
            std::vector<script_value> ctor_args;
            ctor_args.push_back(arg);

            auto result = ctor(ctor_args);
            if (result.has_value()) {
                return std::move(result.value());
            }
            // Constructor returned invalid result - fall through to error
        }

        // No suitable constructor found
        return checked_result<script_value>(
            make_error_code(runtime_error_code::type_mismatch),
            "Cannot convert {0} to {1} (no suitable single-argument constructor)",
            derefed_arg.type_id(), target_type->id);
    }

    // Built-in type conversions
    // Int <-> Float implicit conversions
    if (source_type == script_value_type::jai_int_type &&
        target_base_type == script_value_type::jai_float_type) {
        return script_value(static_cast<script_float>(derefed_arg.unchecked_as_int()), engine_);
    }
    if (source_type == script_value_type::jai_float_type &&
        target_base_type == script_value_type::jai_int_type) {
        return script_value(static_cast<script_int>(derefed_arg.unchecked_as_float()), engine_);
    }

    // Object -> primitive via to_X() methods
    if (source_type == script_value_type::jai_object_type) {
        std::string method_name;
        if (target_base_type == script_value_type::jai_int_type) {
            method_name = "to_int";
        } else if (target_base_type == script_value_type::jai_float_type) {
            method_name = "to_float";
        } else if (target_base_type == script_value_type::jai_string_type) {
            method_name = "to_string";
        } else if (target_base_type == script_value_type::jai_bool_type) {
            method_name = "to_bool";
        } else if (target_base_type == script_value_type::jai_char_type) {
            method_name = "to_char";
        }

        if (!method_name.empty()) {
            // Use get_class_instance() which safely returns nullptr if not a class instance
            auto instance = const_cast<script_value&>(derefed_arg).get_class_instance();
            if (instance) {
                auto method_id = string_symbolizer_->intern(method_name);
                auto method_val = instance->get_method(method_id, false);
                if (!method_val.is_null() && !method_val.is_invalid() && method_val.is_function()) {
                    // Create a bound method with the object as 'this'
                    script_value bound = create_bound_method(arg, method_val);
                    const script_function& method = bound.as_function();
                    std::vector<script_value> no_args;
                    auto result = method(no_args);
                    if (result.has_value()) {
                        return std::move(result.value());
                    }
                }
            }
        }
    }

    // Type mismatch - return error
    return checked_result<script_value>(
        make_error_code(runtime_error_code::type_mismatch),
        "Type mismatch: expected {0} but got {1}",
        target_type->id, derefed_arg.type_id());
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

    auto& state = coroutine_state(handle);

    coroutine_handle* prev_coroutine = active_coroutine_;
    bool prev_yield_request = hasYieldRequest_;
    active_coroutine_ = &handle;
    hasYieldRequest_ = false;

    auto prev_status = handle.get_status();
    handle.set_status(coroutine_handle::status::running);

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
        script_defined_function scriptFunc(
            function->name,
            value_params,
            function->return_type,
            function->body,
            handle.get_closure_env(),
            function->local_count
        );

        // A first resume nested inside another call's argument list must not see that
        // call's in-flight arg metadata (a zero-arg .resume() never saves/clears it)
        auto call_result = call_function(scriptFunc, handle.get_args());

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
        auto caller_env = environment_;
        auto caller_call_stack = std::move(call_stack_);
        auto caller_return_value = std::move(returnValue_);
        bool caller_has_return = hasReturnValue_;

        environment_ = state.saved_environment;
        call_stack_ = std::move(state.saved_call_stack);
        returnValue_ = std::move(state.saved_return_value);
        hasReturnValue_ = state.saved_has_return;

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
            checked_result<void> decl_result = dispatch_decl(function->body->declarations[i].get());

            if (!decl_result) {
                if (decl_result.error() != std::error_code()) {
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

        if (!had_error) {
            if (is_unwinding_) {
                // Script throw escaped the resumed segment: fail the handle; the throw
                // keeps propagating to the caller through the unwinding flag
                handle.set_status(coroutine_handle::status::failed);
                state.saved_environment.reset();
                state.saved_call_stack.clear();
            } else if (hasYieldRequest_) {
                // Record where to resume in the function body.
                // If inner constructs pushed continuations, re-enter this statement.
                // If no inner continuations, the yield was a direct child, skip it.
                size_t resume_idx = state.has_continuations() ? last_body_idx : last_body_idx + 1;
                state.push_continuation(function->body.get(), resume_idx);
                state.saved_environment = environment_;
                state.saved_call_stack = std::move(call_stack_);
                state.saved_return_value = std::move(returnValue_);
                state.saved_has_return = hasReturnValue_;
                handle.set_status(coroutine_handle::status::suspended);
            } else if (hasReturnValue_) {
                handle.set_status(coroutine_handle::status::completed);
                if (returnValue_) {
                    // call_function epilogue parity: deref, then apply the typed-return
                    // conversion the replay used to skip (vm converts the final return)
                    script_value final_result = returnValue_.value().is_reference()
                        ? returnValue_.value().deref()
                        : std::move(returnValue_.value());
                    if (function->return_type && !function->return_type->type_name.empty() &&
                        function->return_type->type_name != "void" &&
                        function->return_type->type_name != "auto" &&
                        function->return_type->base_type != script_value_type::jai_any_type) {
                        auto convert_result = try_convert_for_parameter(final_result, function->return_type);
                        if (!convert_result) {
                            handle.set_status(coroutine_handle::status::failed);
                            error_result.emplace(convert_result.error_value());
                        } else {
                            handle.yield_value_ = std::move(convert_result.value());
                        }
                    } else {
                        handle.yield_value_ = std::move(final_result);
                    }
                }
                state.saved_environment.reset();
                state.saved_call_stack.clear();
            } else {
                // Ran past the final yield: completion yields null (vm run_fiber parity),
                // never a replay of the stale last-yielded value.
                handle.set_status(coroutine_handle::status::completed);
                handle.yield_value_ = make_value();
                state.saved_environment.reset();
                state.saved_call_stack.clear();
            }
        }

        environment_ = caller_env;
        call_stack_ = std::move(caller_call_stack);
        returnValue_ = std::move(caller_return_value);
        hasReturnValue_ = caller_has_return;
    }

    active_coroutine_ = prev_coroutine;
    hasYieldRequest_ = prev_yield_request;

    if (error_result) {
        return std::move(*error_result);
    }
    return handle.yield_value_;
}

// Function call optimization helpers
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

checked_result<void> interpreter::visit_destructuring_decl(destructuring_decl* decl) {
    JAISCRIPT_TRY(dispatch_expr(decl->initializer.get()));
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
        } else {
            environment_->define(decl->names[i].second, std::move(val));
        }
    }

    return {};
}

// These functions use a switch on node_type enum instead of
// virtual method dispatch, eliminating vtable lookup overhead.
// ============================================================

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
            node ? node->location.line : 0
        });
    }
    captured_trace_.push_back({
        "<script>",
        top_level_node_ ? top_level_node_->location.filename : std::string(),
        top_level_node_ ? top_level_node_->location.line : 0
    });
    trace_captured_ = true;
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
        case node_type::statement_decl:
            return dispatch_stmt(static_cast<statement_decl*>(decl)->statement.get());
        default:
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                "Unknown declaration type in dispatch_decl");
    }
}

} // namespace jai
