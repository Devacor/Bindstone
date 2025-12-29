#include "../../include/jaiscript/detail/interpreter.hpp"
#include <cmath>

namespace jai {

// Initialize the binary operator dispatch table
void interpreter::init_dispatch_table() {
    // Arithmetic operators
    binary_dispatch_table_[token_type::plus] = &interpreter::handle_add;
    binary_dispatch_table_[token_type::minus] = &interpreter::handle_subtract;
    binary_dispatch_table_[token_type::star] = &interpreter::handle_multiply;
    binary_dispatch_table_[token_type::slash] = &interpreter::handle_divide;
    binary_dispatch_table_[token_type::percent] = &interpreter::handle_modulo;

    // Comparison operators
    binary_dispatch_table_[token_type::less] = &interpreter::handle_less;
    binary_dispatch_table_[token_type::less_equal] = &interpreter::handle_less_equal;
    binary_dispatch_table_[token_type::greater] = &interpreter::handle_greater;
    binary_dispatch_table_[token_type::greater_equal] = &interpreter::handle_greater_equal;
    binary_dispatch_table_[token_type::equal_equal] = &interpreter::handle_equal;
    binary_dispatch_table_[token_type::bang_equal] = &interpreter::handle_not_equal;
    binary_dispatch_table_[token_type::spaceship] = &interpreter::handle_spaceship;

    // Bitwise operators
    binary_dispatch_table_[token_type::ampersand] = &interpreter::handle_bitwise_and;
    binary_dispatch_table_[token_type::pipe] = &interpreter::handle_bitwise_or;
    binary_dispatch_table_[token_type::caret] = &interpreter::handle_bitwise_xor;
    binary_dispatch_table_[token_type::left_shift] = &interpreter::handle_left_shift;
    binary_dispatch_table_[token_type::right_shift] = &interpreter::handle_right_shift;
}

// Optimized binary operation handlers with cached type indices
// Uses script_value::TYPEID_* constants for fast type checking
// Returns checked_result for zero-allocation error handling
checked_result<script_value> interpreter::handle_add(const script_value& left, const script_value& right) {
    // Try transparent wrapper unwrapping first for object types
    script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
    script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

    const size_t li = unwrapped_left.raw_storage_index();
    const size_t ri = unwrapped_right.raw_storage_index();

    // Fast path for integer addition
    if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
        return make_value(unwrapped_left.unchecked_as_int() + unwrapped_right.unchecked_as_int());
    }

    // Fast path for numeric addition (int/float combinations)
    if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
        (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
        script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
        return make_value(lf + rf);
    }

    // String concatenation - check for to_string() method on objects
    if (li == script_value::TYPEID_STRING || ri == script_value::TYPEID_STRING) {
        return make_value(value_to_string_with_method(unwrapped_left) + value_to_string_with_method(unwrapped_right));
    }

    // Check for custom operator+ method on objects (using original values for method lookup)
    auto custom_result = object_arithmetic_via_method(left, right, op_plus_id_);
    if (custom_result.has_value()) {
        return custom_result.value();
    }

    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for + operator");
}

checked_result<script_value> interpreter::handle_subtract(const script_value& left, const script_value& right) {
    // Try transparent wrapper unwrapping first for object types
    script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
    script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

    const size_t li = unwrapped_left.raw_storage_index();
    const size_t ri = unwrapped_right.raw_storage_index();

    if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
        return make_value(unwrapped_left.unchecked_as_int() - unwrapped_right.unchecked_as_int());
    }

    if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
        (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
        script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
        return make_value(lf - rf);
    }

    // Check for custom operator- method on objects
    auto custom_result = object_arithmetic_via_method(left, right, op_minus_id_);
    if (custom_result.has_value()) {
        return custom_result.value();
    }

    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for - operator");
}

checked_result<script_value> interpreter::handle_multiply(const script_value& left, const script_value& right) {
    // Try transparent wrapper unwrapping first for object types
    script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
    script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

    const size_t li = unwrapped_left.raw_storage_index();
    const size_t ri = unwrapped_right.raw_storage_index();

    if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
        return make_value(unwrapped_left.unchecked_as_int() * unwrapped_right.unchecked_as_int());
    }

    if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
        (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
        script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
        return make_value(lf * rf);
    }

    // Check for custom operator* method on objects
    auto custom_result = object_arithmetic_via_method(left, right, op_star_id_);
    if (custom_result.has_value()) {
        return custom_result.value();
    }

    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for * operator");
}

checked_result<script_value> interpreter::handle_divide(const script_value& left, const script_value& right) {
    // Try transparent wrapper unwrapping first for object types
    script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
    script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

    const size_t li = unwrapped_left.raw_storage_index();
    const size_t ri = unwrapped_right.raw_storage_index();

    if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
        if (unwrapped_right.unchecked_as_int() == 0) {
            return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
        }
        return make_value(unwrapped_left.unchecked_as_int() / unwrapped_right.unchecked_as_int());
    }

    if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
        (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
        if (rf == 0.0) {
            return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
        }
        script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
        return make_value(lf / rf);
    }

    // Check for custom operator/ method on objects
    auto custom_result = object_arithmetic_via_method(left, right, op_slash_id_);
    if (custom_result.has_value()) {
        return custom_result.value();
    }

    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for / operator");
}

checked_result<script_value> interpreter::handle_modulo(const script_value& left, const script_value& right) {
    // Try transparent wrapper unwrapping first for object types
    script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
    script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

    const size_t li = unwrapped_left.raw_storage_index();
    const size_t ri = unwrapped_right.raw_storage_index();

    if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
        if (unwrapped_right.unchecked_as_int() == 0) {
            return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
        }
        return make_value(unwrapped_left.unchecked_as_int() % unwrapped_right.unchecked_as_int());
    }

    if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
        (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
        if (rf == 0.0) {
            return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
        }
        script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
        return make_value(std::fmod(lf, rf));
    }

    // Check for custom operator% method on objects
    auto custom_result = object_arithmetic_via_method(left, right, op_percent_id_);
    if (custom_result.has_value()) {
        return custom_result.value();
    }

    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for % operator");
}

checked_result<script_value> interpreter::handle_less(const script_value& left, const script_value& right) {
    // Try transparent wrapper unwrapping first for object types
    script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
    script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

    const size_t li = unwrapped_left.raw_storage_index();
    const size_t ri = unwrapped_right.raw_storage_index();

    if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
        return make_value(unwrapped_left.unchecked_as_int() < unwrapped_right.unchecked_as_int());
    }

    if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
        (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
        script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
        return make_value(lf < rf);
    }

    if (li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
        return make_value(unwrapped_left.unchecked_as_string() < unwrapped_right.unchecked_as_string());
    }

    // Check for custom operator< method on objects
    auto custom_result = object_comparison_via_method(left, right, op_less_id_);
    if (custom_result.has_value()) {
        return make_value(custom_result.value());
    }

    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for < operator");
}

checked_result<script_value> interpreter::handle_less_equal(const script_value& left, const script_value& right) {
    // Try transparent wrapper unwrapping first for object types
    script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
    script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

    const size_t li = unwrapped_left.raw_storage_index();
    const size_t ri = unwrapped_right.raw_storage_index();

    if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
        return make_value(unwrapped_left.unchecked_as_int() <= unwrapped_right.unchecked_as_int());
    }

    if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
        (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
        script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
        return make_value(lf <= rf);
    }

    if (li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
        return make_value(unwrapped_left.unchecked_as_string() <= unwrapped_right.unchecked_as_string());
    }

    // Check for custom operator<= method on objects
    auto custom_result = object_comparison_via_method(left, right, op_less_equal_id_);
    if (custom_result.has_value()) {
        return make_value(custom_result.value());
    }

    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for <= operator");
}

checked_result<script_value> interpreter::handle_greater(const script_value& left, const script_value& right) {
    // Try transparent wrapper unwrapping first for object types
    script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
    script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

    const size_t li = unwrapped_left.raw_storage_index();
    const size_t ri = unwrapped_right.raw_storage_index();

    if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
        return make_value(unwrapped_left.unchecked_as_int() > unwrapped_right.unchecked_as_int());
    }

    if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
        (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
        script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
        return make_value(lf > rf);
    }

    if (li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
        return make_value(unwrapped_left.unchecked_as_string() > unwrapped_right.unchecked_as_string());
    }

    // Check for custom operator> method on objects
    auto custom_result = object_comparison_via_method(left, right, op_greater_id_);
    if (custom_result.has_value()) {
        return make_value(custom_result.value());
    }

    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for > operator");
}

checked_result<script_value> interpreter::handle_greater_equal(const script_value& left, const script_value& right) {
    // Try transparent wrapper unwrapping first for object types
    script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
    script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

    const size_t li = unwrapped_left.raw_storage_index();
    const size_t ri = unwrapped_right.raw_storage_index();

    if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
        return make_value(unwrapped_left.unchecked_as_int() >= unwrapped_right.unchecked_as_int());
    }

    if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
        (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
        script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
        script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
        return make_value(lf >= rf);
    }

    if (li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
        return make_value(unwrapped_left.unchecked_as_string() >= unwrapped_right.unchecked_as_string());
    }

    // Check for custom operator>= method on objects
    auto custom_result = object_comparison_via_method(left, right, op_greater_equal_id_);
    if (custom_result.has_value()) {
        return make_value(custom_result.value());
    }

    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for >= operator");
}

checked_result<script_value> interpreter::handle_equal(const script_value& left, const script_value& right) {
    // Handle weak_ptr comparisons with null
    if ((left.is_weak_ptr() && right.is_null()) || (left.is_null() && right.is_weak_ptr())) {
        // For weak_ptr, null comparison checks if expired
        bool is_expired = false;
        if (left.is_weak_ptr()) {
            if (left.is_weak_ptr()) {
                auto weak_ptr = left.get_weak_ptr();
                // Check if weak_ptr is expired (includes default-constructed)
                is_expired = weak_ptr.expired();
            } else if ((left.get_object_holder() != nullptr)) {
                // weak_ptr_holder type - check if it contains an actual value
                auto holder = left.get_object_holder();
                is_expired = (holder->type_name == "weak_ptr_holder" && !holder->data);
            } else {
                // Other cases - consider expired
                is_expired = true;
            }
        } else {
            // right is weak_ptr
            if (right.is_weak_ptr()) {
                auto weak_ptr = right.get_weak_ptr();
                // Check if weak_ptr is expired (includes default-constructed)
                is_expired = weak_ptr.expired();
            } else if ((right.get_object_holder() != nullptr)) {
                // weak_ptr_holder type - check if it contains an actual value
                auto holder = right.get_object_holder();
                is_expired = (holder->type_name == "weak_ptr_holder" && !holder->data);
            } else {
                // Other cases - consider expired
                is_expired = true;
            }
        }

        return make_value(is_expired);  // weak == null is true if expired
    }

    // Try transparent wrapper unwrapping for object types
    script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
    script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

    // Handle numeric type comparison (int vs float should compare by value)
    if ((unwrapped_left.is_int() || unwrapped_left.is_float()) && (unwrapped_right.is_int() || unwrapped_right.is_float())) {
        // Convert both to float for comparison to handle 5 == 5.0 correctly
        script_float lf = unwrapped_left.is_int() ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
        script_float rf = unwrapped_right.is_int() ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
        return make_value(lf == rf);
    }

    // Type mismatch = not equal (except for numeric and weak_ptr vs null handled above)
    if (unwrapped_left.type() != unwrapped_right.type()) {
        return make_value(false);
    }

    if (unwrapped_left.is_null()) return make_value(true);
    // Note: int and float are already handled above in mixed-type comparison
    if (unwrapped_left.is_string()) return make_value(unwrapped_left.unchecked_as_string() == unwrapped_right.unchecked_as_string());
    if (unwrapped_left.is_bool()) return make_value(unwrapped_left.unchecked_as_bool() == unwrapped_right.unchecked_as_bool());
    if (unwrapped_left.is_char()) return make_value(unwrapped_left.unchecked_as_char() == unwrapped_right.unchecked_as_char());

    // Array equality - compare by reference (same array instance)
    if (left.is_array() && right.is_array()) {
        auto& left_arr = const_cast<script_value&>(left).get_array_storage();
        auto& right_arr = const_cast<script_value&>(right).get_array_storage();
        return make_value(left_arr.get() == right_arr.get());
    }

    // Map equality - compare by reference (same map instance)
    if (left.is_map() && right.is_map()) {
        auto& left_map = const_cast<script_value&>(left).get_map_storage();
        auto& right_map = const_cast<script_value&>(right).get_map_storage();
        return make_value(left_map.get() == right_map.get());
    }

    // Object/class instance equality
    // First check for custom operator== or equals() method
    auto custom_result = object_equality_via_method(left, right);
    if (custom_result.has_value()) {
        return make_value(custom_result.value());
    }

    // Fall back to reference equality (same object_holder)
    // This enables: obj == obj to return true (self-comparison)
    auto left_holder = const_cast<script_value&>(left).get_object_holder();
    auto right_holder = const_cast<script_value&>(right).get_object_holder();
    if (left_holder && right_holder) {
        return make_value(left_holder == right_holder);
    }

    // For other complex types without holders, default to false
    return make_value(false);
}

checked_result<script_value> interpreter::handle_not_equal(const script_value& left, const script_value& right) {
    // Handle weak_ptr comparisons with null
    if ((left.is_weak_ptr() && right.is_null()) || (left.is_null() && right.is_weak_ptr())) {
        // For weak_ptr, null comparison checks if expired
        bool is_expired = false;
        if (left.is_weak_ptr()) {
            if (left.is_weak_ptr()) {
                auto weak_ptr = left.get_weak_ptr();
                // Check if weak_ptr is expired (includes default-constructed)
                is_expired = weak_ptr.expired();
            } else if ((left.get_object_holder() != nullptr)) {
                // weak_ptr_holder type - check if it contains an actual value
                auto holder = left.get_object_holder();
                is_expired = (holder->type_name == "weak_ptr_holder" && !holder->data);
            } else {
                // Other cases - consider expired
                is_expired = true;
            }
        } else {
            // right is weak_ptr
            if (right.is_weak_ptr()) {
                auto weak_ptr = right.get_weak_ptr();
                // Check if weak_ptr is expired (includes default-constructed)
                is_expired = weak_ptr.expired();
            } else if ((right.get_object_holder() != nullptr)) {
                // weak_ptr_holder type - check if it contains an actual value
                auto holder = right.get_object_holder();
                is_expired = (holder->type_name == "weak_ptr_holder" && !holder->data);
            } else {
                // Other cases - consider expired
                is_expired = true;
            }
        }

        return make_value(!is_expired);  // weak != null is true if not expired
    }

    // For other cases, just negate the equality result
    auto eq_result = handle_equal(left, right);
    if (!eq_result) [[unlikely]] {
        return eq_result.error_value();
    }
    return make_value(!eq_result.value().unchecked_as_bool());
}

checked_result<script_value> interpreter::handle_spaceship(const script_value& left, const script_value& right) {
    // Fast path for integer spaceship - avoid function calls
    if (left.type() == script_value_type::jai_int_type && right.type() == script_value_type::jai_int_type) {
        // Direct storage access, single C++20 spaceship operation
        auto cmp = left.unchecked_as_int() <=> right.unchecked_as_int();
        return make_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)));
    }

    // Mixed numeric types
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        script_float lf = left.is_int() ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
        script_float rf = right.is_int() ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
        auto cmp = lf <=> rf;
        return make_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)));
    }

    // String comparison
    if (left.is_string() && right.is_string()) {
        int cmp = left.unchecked_as_string().compare(right.unchecked_as_string());
        return make_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)));
    }

    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for <=> operator");
}

checked_result<script_value> interpreter::handle_bitwise_and(const script_value& left, const script_value& right) {
    if (!left.is_int() || !right.is_int()) {
        return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Bitwise & requires integer operands");
    }
    return make_value(left.unchecked_as_int() & right.unchecked_as_int());
}

checked_result<script_value> interpreter::handle_bitwise_or(const script_value& left, const script_value& right) {
    if (!left.is_int() || !right.is_int()) {
        return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Bitwise | requires integer operands");
    }
    return make_value(left.unchecked_as_int() | right.unchecked_as_int());
}

checked_result<script_value> interpreter::handle_bitwise_xor(const script_value& left, const script_value& right) {
    if (!left.is_int() || !right.is_int()) {
        return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Bitwise ^ requires integer operands");
    }
    return make_value(left.unchecked_as_int() ^ right.unchecked_as_int());
}

checked_result<script_value> interpreter::handle_left_shift(const script_value& left, const script_value& right) {
    if (!left.is_int() || !right.is_int()) {
        return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Left shift requires integer operands");
    }
    return make_value(left.unchecked_as_int() << right.unchecked_as_int());
}

checked_result<script_value> interpreter::handle_right_shift(const script_value& left, const script_value& right) {
    if (!left.is_int() || !right.is_int()) {
        return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Right shift requires integer operands");
    }
    return make_value(left.unchecked_as_int() >> right.unchecked_as_int());
}

} // namespace jai
