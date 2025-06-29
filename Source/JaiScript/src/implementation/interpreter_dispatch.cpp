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

// Optimized binary operation handlers
script_value interpreter::handle_add(const script_value& left, const script_value& right) {
    // Fast path for integer addition
    if (left.is_int() && right.is_int()) {
        return script_value(left.as_int() + right.as_int());
    }
    
    // Fast path for float addition
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        script_float lf = left.is_int() ? script_float(left.as_int()) : left.as_float();
        script_float rf = right.is_int() ? script_float(right.as_int()) : right.as_float();
        return script_value(lf + rf);
    }
    
    // String concatenation
    if (left.is_string() || right.is_string()) {
        return script_value(left.to_string() + right.to_string());
    }
    
    throw runtime_error("Invalid operands for + operator");
}

script_value interpreter::handle_subtract(const script_value& left, const script_value& right) {
    if (left.is_int() && right.is_int()) {
        return script_value(left.as_int() - right.as_int());
    }
    
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        script_float lf = left.is_int() ? script_float(left.as_int()) : left.as_float();
        script_float rf = right.is_int() ? script_float(right.as_int()) : right.as_float();
        return script_value(lf - rf);
    }
    
    throw runtime_error("Invalid operands for - operator");
}

script_value interpreter::handle_multiply(const script_value& left, const script_value& right) {
    if (left.is_int() && right.is_int()) {
        return script_value(left.as_int() * right.as_int());
    }
    
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        script_float lf = left.is_int() ? script_float(left.as_int()) : left.as_float();
        script_float rf = right.is_int() ? script_float(right.as_int()) : right.as_float();
        return script_value(lf * rf);
    }
    
    throw runtime_error("Invalid operands for * operator");
}

script_value interpreter::handle_divide(const script_value& left, const script_value& right) {
    if (left.is_int() && right.is_int()) {
        if (right.as_int() == 0) throw runtime_error("Division by zero");
        return script_value(left.as_int() / right.as_int());
    }
    
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        script_float rf = right.is_int() ? script_float(right.as_int()) : right.as_float();
        if (rf == 0.0) throw runtime_error("Division by zero");
        script_float lf = left.is_int() ? script_float(left.as_int()) : left.as_float();
        return script_value(lf / rf);
    }
    
    throw runtime_error("Invalid operands for / operator");
}

script_value interpreter::handle_modulo(const script_value& left, const script_value& right) {
    if (left.is_int() && right.is_int()) {
        if (right.as_int() == 0) throw runtime_error("Division by zero");
        return script_value(left.as_int() % right.as_int());
    }
    
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        script_float rf = right.is_int() ? script_float(right.as_int()) : right.as_float();
        if (rf == 0.0) throw runtime_error("Division by zero");
        script_float lf = left.is_int() ? script_float(left.as_int()) : left.as_float();
        return script_value(std::fmod(lf, rf));
    }
    
    throw runtime_error("Invalid operands for % operator");
}

script_value interpreter::handle_less(const script_value& left, const script_value& right) {
    if (left.is_int() && right.is_int()) {
        return script_value(left.as_int() < right.as_int());
    }
    
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        script_float lf = left.is_int() ? script_float(left.as_int()) : left.as_float();
        script_float rf = right.is_int() ? script_float(right.as_int()) : right.as_float();
        return script_value(lf < rf);
    }
    
    if (left.is_string() && right.is_string()) {
        return script_value(left.as_string() < right.as_string());
    }
    
    throw runtime_error("Invalid operands for < operator");
}

script_value interpreter::handle_less_equal(const script_value& left, const script_value& right) {
    if (left.is_int() && right.is_int()) {
        return script_value(left.as_int() <= right.as_int());
    }
    
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        script_float lf = left.is_int() ? script_float(left.as_int()) : left.as_float();
        script_float rf = right.is_int() ? script_float(right.as_int()) : right.as_float();
        return script_value(lf <= rf);
    }
    
    if (left.is_string() && right.is_string()) {
        return script_value(left.as_string() <= right.as_string());
    }
    
    throw runtime_error("Invalid operands for <= operator");
}

script_value interpreter::handle_greater(const script_value& left, const script_value& right) {
    if (left.is_int() && right.is_int()) {
        return script_value(left.as_int() > right.as_int());
    }
    
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        script_float lf = left.is_int() ? script_float(left.as_int()) : left.as_float();
        script_float rf = right.is_int() ? script_float(right.as_int()) : right.as_float();
        return script_value(lf > rf);
    }
    
    if (left.is_string() && right.is_string()) {
        return script_value(left.as_string() > right.as_string());
    }
    
    throw runtime_error("Invalid operands for > operator");
}

script_value interpreter::handle_greater_equal(const script_value& left, const script_value& right) {
    if (left.is_int() && right.is_int()) {
        return script_value(left.as_int() >= right.as_int());
    }
    
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        script_float lf = left.is_int() ? script_float(left.as_int()) : left.as_float();
        script_float rf = right.is_int() ? script_float(right.as_int()) : right.as_float();
        return script_value(lf >= rf);
    }
    
    if (left.is_string() && right.is_string()) {
        return script_value(left.as_string() >= right.as_string());
    }
    
    throw runtime_error("Invalid operands for >= operator");
}

script_value interpreter::handle_equal(const script_value& left, const script_value& right) {
    // Type mismatch = not equal
    if (left.type() != right.type()) {
        return script_value(false);
    }
    
    if (left.is_null()) return script_value(true);
    if (left.is_int()) return script_value(left.as_int() == right.as_int());
    if (left.is_float()) return script_value(left.as_float() == right.as_float());
    if (left.is_string()) return script_value(left.as_string() == right.as_string());
    if (left.is_bool()) return script_value(left.as_bool() == right.as_bool());
    if (left.is_char()) return script_value(left.as_char() == right.as_char());
    
    // For complex types, default to false
    return script_value(false);
}

script_value interpreter::handle_not_equal(const script_value& left, const script_value& right) {
    // Just negate the equality result
    return script_value(!handle_equal(left, right).as_bool());
}

script_value interpreter::handle_spaceship(const script_value& left, const script_value& right) {
    // Fast path for integer spaceship - avoid function calls
    if (left.type() == script_value_type::jai_int_type && right.type() == script_value_type::jai_int_type) {
        // Direct storage access, single C++20 spaceship operation
        auto cmp = std::get<script_int>(left.storage_) <=> std::get<script_int>(right.storage_);
        return script_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)));
    }
    
    // Mixed numeric types
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        script_float lf = left.is_int() ? script_float(left.as_int()) : left.as_float();
        script_float rf = right.is_int() ? script_float(right.as_int()) : right.as_float();
        auto cmp = lf <=> rf;
        return script_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)));
    }
    
    // String comparison
    if (left.is_string() && right.is_string()) {
        int cmp = left.as_string().compare(right.as_string());
        return script_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)));
    }
    
    throw runtime_error("Invalid operands for <=> operator");
}

script_value interpreter::handle_bitwise_and(const script_value& left, const script_value& right) {
    if (!left.is_int() || !right.is_int()) {
        throw runtime_error("Bitwise & requires integer operands");
    }
    return script_value(left.as_int() & right.as_int());
}

script_value interpreter::handle_bitwise_or(const script_value& left, const script_value& right) {
    if (!left.is_int() || !right.is_int()) {
        throw runtime_error("Bitwise | requires integer operands");
    }
    return script_value(left.as_int() | right.as_int());
}

script_value interpreter::handle_bitwise_xor(const script_value& left, const script_value& right) {
    if (!left.is_int() || !right.is_int()) {
        throw runtime_error("Bitwise ^ requires integer operands");
    }
    return script_value(left.as_int() ^ right.as_int());
}

script_value interpreter::handle_left_shift(const script_value& left, const script_value& right) {
    if (!left.is_int() || !right.is_int()) {
        throw runtime_error("Left shift requires integer operands");
    }
    return script_value(left.as_int() << right.as_int());
}

script_value interpreter::handle_right_shift(const script_value& left, const script_value& right) {
    if (!left.is_int() || !right.is_int()) {
        throw runtime_error("Right shift requires integer operands");
    }
    return script_value(left.as_int() >> right.as_int());
}

} // namespace jai