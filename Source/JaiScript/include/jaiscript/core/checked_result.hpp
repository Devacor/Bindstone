#pragma once

#ifndef __JAISCRIPT_CORE_CHECKED_RESULT_HPP__
#define __JAISCRIPT_CORE_CHECKED_RESULT_HPP__

#include <system_error>
#include <type_traits>
#include <utility>
#include <string_view>

namespace jai {

// Forward declaration
template<typename T> class checked_result;

/**
 * error_propagator - Helper type for clean error propagation across checked_result types
 *
 * This type can implicitly convert to any checked_result<T>, preserving both the
 * error code and message. This enables the pattern:
 *   if (!result) return result.error_value();
 *
 * which works regardless of whether the caller returns checked_result<void>,
 * checked_result<script_value>, or any other checked_result<T>.
 */
struct error_propagator {
    std::error_code ec;
    std::string_view static_msg;  // Must be a static/literal string - no allocation
    uint64_t symbol_id = 0;       // Optional interned symbol (e.g., variable name)
    uint64_t symbol_id2 = 0;      // Optional second symbol (e.g., type name, class name)

    // Implicit conversion to any checked_result<T> - declared here, defined after checked_result
    template<typename T>
    inline operator checked_result<T>() const;
};

/**
 * checked_result<T> - Lightweight result type that forces error checking at compile-time
 *
 * Similar to Rust's Result<T, E> or C++23's std::expected<T, E>, but optimized for JaiScript's needs.
 * Uses [[nodiscard]] to ensure errors cannot be silently ignored.
 *
 * Performance characteristics:
 * - Success path: RVO-optimized, compilers inline automatically
 * - Error path: Zero allocation - uses string_view for static message + interned symbol ID
 * - Size: checked_result<void> is ~32 bytes (error_code + string_view + uint64_t)
 * - Zero overhead: Simple bool check optimizes away in release builds
 */
template<typename T>
class [[nodiscard]] checked_result {
private:
    union {
        T value_;
        std::error_code error_;
    };
    bool has_value_;
    std::string_view static_msg_;  // Static error message (no allocation)
    uint64_t symbol_id_ = 0;       // Optional interned symbol for context (e.g., variable name)
    uint64_t symbol_id2_ = 0;      // Optional second symbol (e.g., type name, class name)

public:
    // Success constructor - templated to allow implicit conversion from derived types
    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U, T>>>
    checked_result(U&& value) noexcept(std::is_nothrow_constructible_v<T, U>)
        : value_(std::forward<U>(value)), has_value_(true) {}

    checked_result(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
        : value_(value), has_value_(true) {}

    // Error constructor without message
    checked_result(std::error_code ec) noexcept
        : error_(ec), has_value_(false) {}

    // Error constructor with static message (zero allocation)
    checked_result(std::error_code ec, std::string_view msg, uint64_t sym_id = 0, uint64_t sym_id2 = 0) noexcept
        : error_(ec), has_value_(false), static_msg_(msg), symbol_id_(sym_id), symbol_id2_(sym_id2) {}

    // Copy constructor
    checked_result(const checked_result& other)
        : has_value_(other.has_value_), static_msg_(other.static_msg_), symbol_id_(other.symbol_id_), symbol_id2_(other.symbol_id2_) {
        if (has_value_) {
            new (&value_) T(other.value_);
        } else {
            new (&error_) std::error_code(other.error_);
        }
    }

    // Move constructor
    checked_result(checked_result&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : has_value_(other.has_value_), static_msg_(other.static_msg_), symbol_id_(other.symbol_id_), symbol_id2_(other.symbol_id2_) {
        if (has_value_) {
            new (&value_) T(std::move(other.value_));
        } else {
            new (&error_) std::error_code(other.error_);
        }
    }

    // Destructor
    ~checked_result() {
        if (has_value_) {
            value_.~T();
        } else {
            error_.~error_code();
        }
    }

    // Check if successful
    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value_;
    }

    [[nodiscard]] bool has_value() const noexcept {
        return has_value_;
    }

    [[nodiscard]] bool has_error() const noexcept {
        return !has_value_;
    }

    // Get value (undefined behavior if error - use operator bool() to check first!)
    [[nodiscard]] T& value() & noexcept {
        return value_;
    }

    [[nodiscard]] const T& value() const& noexcept {
        return value_;
    }

    [[nodiscard]] T&& value() && noexcept {
        return std::move(value_);
    }

    // Get error
    [[nodiscard]] std::error_code error() const noexcept {
        return error_;
    }

    // Get static error message
    [[nodiscard]] std::string_view static_message() const noexcept {
        return static_msg_;
    }

    // Get symbol ID for additional context (e.g., variable name)
    [[nodiscard]] uint64_t symbol_id() const noexcept {
        return symbol_id_;
    }

    // Get second symbol ID for additional context (e.g., type name)
    [[nodiscard]] uint64_t symbol_id2() const noexcept {
        return symbol_id2_;
    }

    // Legacy compatibility - returns static message (no allocation)
    [[nodiscard]] std::string_view message() const noexcept {
        return static_msg_;
    }

    // Monadic operations for convenience

    // Propagate error to caller (useful pattern)
    [[nodiscard]] checked_result<T> propagate() && {
        return std::move(*this);
    }

    // Get error as propagator for clean cross-type error propagation
    // Usage: if (!result) return result.error_value();
    [[nodiscard]] error_propagator error_value() const noexcept {
        return error_propagator{error_, static_msg_, symbol_id_, symbol_id2_};
    }
};

/**
 * Partial specialization for reference types - uses std::reference_wrapper internally
 * This allows checked_result to work with reference types despite the union limitation
 */
template<typename T>
class [[nodiscard]] checked_result<T&> {
private:
    union {
        std::reference_wrapper<T> value_;
        std::error_code error_;
    };
    bool has_value_;
    std::string_view static_msg_;
    uint64_t symbol_id_ = 0;
    uint64_t symbol_id2_ = 0;

public:
    // Success constructor from reference
    checked_result(T& value) noexcept
        : value_(std::ref(value)), has_value_(true) {}

    // Error constructor without message
    checked_result(std::error_code ec) noexcept
        : error_(ec), has_value_(false) {}

    // Error constructor with static message
    checked_result(std::error_code ec, std::string_view msg, uint64_t sym_id = 0, uint64_t sym_id2 = 0) noexcept
        : error_(ec), has_value_(false), static_msg_(msg), symbol_id_(sym_id), symbol_id2_(sym_id2) {}

    // Copy constructor
    checked_result(const checked_result& other)
        : has_value_(other.has_value_), static_msg_(other.static_msg_), symbol_id_(other.symbol_id_), symbol_id2_(other.symbol_id2_) {
        if (has_value_) {
            new (&value_) std::reference_wrapper<T>(other.value_);
        } else {
            new (&error_) std::error_code(other.error_);
        }
    }

    // Move constructor
    checked_result(checked_result&& other) noexcept
        : has_value_(other.has_value_), static_msg_(other.static_msg_), symbol_id_(other.symbol_id_), symbol_id2_(other.symbol_id2_) {
        if (has_value_) {
            new (&value_) std::reference_wrapper<T>(other.value_);
        } else {
            new (&error_) std::error_code(other.error_);
        }
    }

    // Destructor
    ~checked_result() {
        if (has_value_) {
            value_.~reference_wrapper<T>();
        } else {
            error_.~error_code();
        }
    }

    // Check if successful
    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value_;
    }

    [[nodiscard]] bool has_value() const noexcept {
        return has_value_;
    }

    [[nodiscard]] bool has_error() const noexcept {
        return !has_value_;
    }

    // Get value (returns actual reference, not reference_wrapper)
    [[nodiscard]] T& value() const noexcept {
        return value_.get();
    }

    // Get error
    [[nodiscard]] std::error_code error() const noexcept {
        return error_;
    }

    // Get static error message
    [[nodiscard]] std::string_view static_message() const noexcept {
        return static_msg_;
    }

    // Get symbol ID for additional context
    [[nodiscard]] uint64_t symbol_id() const noexcept {
        return symbol_id_;
    }

    // Get second symbol ID for additional context
    [[nodiscard]] uint64_t symbol_id2() const noexcept {
        return symbol_id2_;
    }

    // Legacy compatibility
    [[nodiscard]] std::string_view message() const noexcept {
        return static_msg_;
    }

    // Propagate error to caller
    [[nodiscard]] checked_result<T&> propagate() && {
        return std::move(*this);
    }

    // Get error as propagator for clean cross-type error propagation
    [[nodiscard]] error_propagator error_value() const noexcept {
        return error_propagator{error_, static_msg_, symbol_id_, symbol_id2_};
    }
};

/**
 * Specialization for void - represents success/failure without a value
 * Optimized to just store error_code (default constructed = success)
 *
 * Performance optimizations:
 * - Trivial functions inline automatically
 * - [[unlikely]] on error branches in macros
 * - Zero allocation for error messages (string_view + interned symbol ID)
 * - RVO (Return Value Optimization) on success path
 */
template<>
class [[nodiscard]] checked_result<void> {
private:
    std::error_code error_;
    std::string_view static_msg_;
    uint64_t symbol_id_ = 0;
    uint64_t symbol_id2_ = 0;

public:
    // Success constructor - optimize for RVO
    checked_result() noexcept : error_() {}

    // Error constructor without message
    checked_result(std::error_code ec) noexcept : error_(ec) {}

    // Error constructor with static message
    checked_result(std::error_code ec, std::string_view msg, uint64_t sym_id = 0, uint64_t sym_id2 = 0) noexcept
        : error_(ec), static_msg_(msg), symbol_id_(sym_id), symbol_id2_(sym_id2) {}

    // Check if successful (hot path - inline hint)
    [[nodiscard]] inline explicit operator bool() const noexcept {
        return !static_cast<bool>(error_);
    }

    [[nodiscard]] inline bool has_value() const noexcept {
        return !static_cast<bool>(error_);
    }

    [[nodiscard]] inline bool has_error() const noexcept {
        return static_cast<bool>(error_);
    }

    // Get error (cold path - only called when has_error)
    [[nodiscard]] inline std::error_code error() const noexcept {
        return error_;
    }

    // Get static error message
    [[nodiscard]] inline std::string_view static_message() const noexcept {
        return static_msg_;
    }

    // Get symbol ID for additional context
    [[nodiscard]] inline uint64_t symbol_id() const noexcept {
        return symbol_id_;
    }

    // Get second symbol ID for additional context
    [[nodiscard]] inline uint64_t symbol_id2() const noexcept {
        return symbol_id2_;
    }

    // Legacy compatibility
    [[nodiscard]] inline std::string_view message() const noexcept {
        return static_msg_;
    }

    // Propagate error to caller
    [[nodiscard]] checked_result<void> propagate() && noexcept {
        return std::move(*this);
    }

    // Get error as propagator for clean cross-type error propagation
    [[nodiscard]] error_propagator error_value() const noexcept {
        return error_propagator{error_, static_msg_, symbol_id_, symbol_id2_};
    }
};

// Define error_propagator conversion operator now that checked_result is fully defined
template<typename T>
inline error_propagator::operator checked_result<T>() const {
    return checked_result<T>(ec, static_msg, symbol_id, symbol_id2);
}

// Convenience macro for propagating errors in visitor methods
// Usage: JAISCRIPT_TRY(expr->left->accept(this));
#define JAISCRIPT_TRY(expr) \
    do { \
        auto __result = (expr); \
        if (!__result) [[unlikely]] { \
            return __result.error_value(); \
        } \
    } while(0)

// Convenience macro for early return on error with value extraction
// Usage: JAISCRIPT_TRY_ASSIGN(auto x, some_function());
// Uses double indirection to properly expand __LINE__ before token pasting (works on MSVC/GCC/Clang)
#define JAISCRIPT_CONCAT_IMPL(a, b) a##b
#define JAISCRIPT_CONCAT(a, b) JAISCRIPT_CONCAT_IMPL(a, b)
#define JAISCRIPT_TRY_ASSIGN(var, expr) \
    auto JAISCRIPT_CONCAT(__temp_result_, __LINE__) = (expr); \
    if (!JAISCRIPT_CONCAT(__temp_result_, __LINE__)) [[unlikely]] { \
        return JAISCRIPT_CONCAT(__temp_result_, __LINE__).error_value(); \
    } \
    var = std::move(JAISCRIPT_CONCAT(__temp_result_, __LINE__).value());

// Convenience macro for propagating errors in loops where we need to break instead of return
// Usage: JAISCRIPT_TRY_BREAK(expr->left->accept(this));
#define JAISCRIPT_TRY_BREAK(expr) \
    do { \
        auto __result = (expr); \
        if (!__result) [[unlikely]] { \
            break; \
        } \
    } while(0)

} // namespace jai

#endif // __JAISCRIPT_CORE_CHECKED_RESULT_HPP__
