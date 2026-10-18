#include "../../include/jaiscript/detail/interpreter.hpp"
#include <jaiscript/detail/binary_handlers.hpp>

namespace jai {

// Initialize the flat binary operator handler array (indexed by detail::op_slot)
void interpreter::init_dispatch_table() {
    auto set = [this](detail::op_slot s, binary_op_handler h) {
        binary_handlers_[static_cast<size_t>(s)] = h;
    };
    // Arithmetic operators
    set(detail::op_slot::plus, &interpreter::handle_add);
    set(detail::op_slot::minus, &interpreter::handle_subtract);
    set(detail::op_slot::star, &interpreter::handle_multiply);
    set(detail::op_slot::slash, &interpreter::handle_divide);
    set(detail::op_slot::percent, &interpreter::handle_modulo);

    // Comparison operators
    set(detail::op_slot::less, &interpreter::handle_less);
    set(detail::op_slot::less_equal, &interpreter::handle_less_equal);
    set(detail::op_slot::greater, &interpreter::handle_greater);
    set(detail::op_slot::greater_equal, &interpreter::handle_greater_equal);
    set(detail::op_slot::equal_equal, &interpreter::handle_equal);
    set(detail::op_slot::bang_equal, &interpreter::handle_not_equal);
    set(detail::op_slot::spaceship, &interpreter::handle_spaceship);

    // Bitwise operators
    set(detail::op_slot::ampersand, &interpreter::handle_bitwise_and);
    set(detail::op_slot::pipe, &interpreter::handle_bitwise_or);
    set(detail::op_slot::caret, &interpreter::handle_bitwise_xor);
    set(detail::op_slot::left_shift, &interpreter::handle_left_shift);
    set(detail::op_slot::right_shift, &interpreter::handle_right_shift);
}

struct interpreter::binary_host {
    interpreter& in;
    script_value make_int(script_int v) const noexcept { return in.make_int_fast(v); }
    script_value make_float(script_float v) const noexcept { return in.make_float_fast(v); }
    script_value make_bool(bool v) const noexcept { return in.make_bool_fast(v); }
    jai::engine* engine() const noexcept { return in.engine_; }
    checked_result<script_value> concat(const script_value& l, const script_value& r) { return in.concat_with_methods(l, r); }
    std::optional<checked_result<script_value>> object_arithmetic(const script_value& l, const script_value& r, uint64_t id) { return in.object_arithmetic_via_method(l, r, id); }
    bool operator_takes(const script_value& l, const script_value& r, uint64_t id) {
        if (in.parallel_worker_) [[unlikely]] { return detail::worker_operator_takes(l, r, id); }
        auto target = in.resolve_member_target(l);
        return target && binary_operator_accepts(target.class_def, id, r);
    }
    std::optional<checked_result<bool>> object_comparison(const script_value& l, const script_value& r, uint64_t id) { return in.object_comparison_via_method(l, r, id); }
    std::optional<checked_result<bool>> object_equality(const script_value& l, const script_value& r) { return in.object_equality_via_method(l, r); }
    std::optional<checked_result<bool>> object_inequality(const script_value& l, const script_value& r) { return in.object_inequality_via_method(l, r); }
    std::optional<checked_result<bool>> object_comparison_reversed(const script_value& l, const script_value& r, uint64_t id) { return in.object_comparison_reversed_via_method(l, r, id); }
    std::optional<checked_result<script_value>> object_spaceship_reversed(const script_value& l, const script_value& r) { return in.object_spaceship_reversed_via_method(l, r); }
    std::optional<checked_result<bool>> object_spaceship_equality(const script_value& l, const script_value& r) { return in.object_spaceship_equality_via_method(l, r); }
    bool defines_custom_equality(const script_value& v) const { return in.object_defines_custom_equality(v); }
    bool unwinding() const noexcept { return in.is_unwinding_; }
    bool parallel_worker() const noexcept { return in.parallel_worker_; }
    size_t& equality_depth() noexcept { return in.structural_equality_depth_; }
    checked_result<script_value> spaceship_member(const script_value& a, const script_value& b, detail::order_walk* walk) {
        return detail::spaceship_operands(*this, in.op_spaceship_id_, a, b, walk);
    }
    checked_result<script_value> equal(const script_value& l, const script_value& r) { return in.handle_equal(l, r); }
};

checked_result<script_value> interpreter::handle_add(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_arithmetic<detail::arith_op::add>(h, op_plus_id_, left, right);
}
checked_result<script_value> interpreter::handle_subtract(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_arithmetic<detail::arith_op::sub>(h, op_minus_id_, left, right);
}
checked_result<script_value> interpreter::handle_multiply(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_arithmetic<detail::arith_op::mul>(h, op_star_id_, left, right);
}
checked_result<script_value> interpreter::handle_divide(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_arithmetic<detail::arith_op::div>(h, op_slash_id_, left, right);
}
checked_result<script_value> interpreter::handle_modulo(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_arithmetic<detail::arith_op::mod>(h, op_percent_id_, left, right);
}
checked_result<script_value> interpreter::handle_less(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_ordering<detail::order_op::less>(h, op_less_id_, left, right);
}
checked_result<script_value> interpreter::handle_less_equal(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_ordering<detail::order_op::less_equal>(h, op_less_equal_id_, left, right);
}
checked_result<script_value> interpreter::handle_greater(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_ordering<detail::order_op::greater>(h, op_greater_id_, left, right);
}
checked_result<script_value> interpreter::handle_greater_equal(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_ordering<detail::order_op::greater_equal>(h, op_greater_equal_id_, left, right);
}
checked_result<script_value> interpreter::handle_equal(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_equal(h, left, right);
}
checked_result<script_value> interpreter::handle_not_equal(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_not_equal(h, left, right);
}
checked_result<script_value> interpreter::handle_spaceship(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_spaceship(h, op_spaceship_id_, left, right);
}
checked_result<script_value> interpreter::handle_bitwise_and(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_bitwise<detail::bit_op::bit_and>(h, op_ampersand_id_, left, right);
}
checked_result<script_value> interpreter::handle_bitwise_or(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_bitwise<detail::bit_op::bit_or>(h, op_pipe_id_, left, right);
}
checked_result<script_value> interpreter::handle_bitwise_xor(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_bitwise<detail::bit_op::bit_xor>(h, op_caret_id_, left, right);
}
checked_result<script_value> interpreter::handle_left_shift(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_bitwise<detail::bit_op::shift_left>(h, op_left_shift_id_, left, right);
}
checked_result<script_value> interpreter::handle_right_shift(const script_value& left, const script_value& right) {
    binary_host h{*this};
    return detail::binary_bitwise<detail::bit_op::shift_right>(h, op_right_shift_id_, left, right);
}

} // namespace jai
