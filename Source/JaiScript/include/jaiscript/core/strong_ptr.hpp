#pragma once

#ifndef __JAISCRIPT_CORE_STRONG_PTR_HPP__
#define __JAISCRIPT_CORE_STRONG_PTR_HPP__

#include <memory>
#include <type_traits>
#include <utility>

namespace jai {

// Forward declarations
template<typename T> class strong_ptr;
template<typename T> class weaker_ptr;

namespace detail {

/**
 * @brief Control block for strong_ptr/weaker_ptr reference counting
 *
 * Purely non-atomic for maximum single-threaded performance.
 * Thread safety is handled at a higher level (critical sections, partitioning, etc.)
 *
 * For cross-thread object sharing, extract std::shared_ptr<T> from script_value.
 */
struct control_block_base {
    size_t strong_count = 1;
    size_t weak_count = 1;  // Strong family's collective weak ref prevents premature CB deletion

    control_block_base() = default;
    control_block_base(const control_block_base&) = delete;
    control_block_base& operator=(const control_block_base&) = delete;

    void add_strong() noexcept { ++strong_count; }
    bool release_strong() noexcept { return --strong_count == 0; }
    void add_weak() noexcept { ++weak_count; }
    bool release_weak() noexcept { return --weak_count == 0 && strong_count == 0; }

    bool try_add_strong() noexcept {
        if (strong_count == 0) return false;
        ++strong_count;
        return true;
    }

    size_t get_strong_count() const noexcept { return strong_count; }
};

/**
 * @brief Typed control block that includes the destructor function
 */
template<typename T>
struct control_block : control_block_base {
    using destructor_fn = void(*)(void*);
    destructor_fn destructor = nullptr;

    static void default_destructor(void* ptr) {
        delete static_cast<T*>(ptr);
    }
};

} // namespace detail

/**
 * @brief Non-atomic reference-counted smart pointer for single-threaded use
 *
 * strong_ptr provides shared ownership semantics like std::shared_ptr but uses
 * non-atomic reference counting for maximum performance.
 *
 * Thread safety is the caller's responsibility (critical sections, partitioning, etc.)
 * For cross-thread object sharing, extract std::shared_ptr<T> from script_value instead.
 *
 * Usage:
 *   auto ptr = make_strong<Foo>(args...);
 *   ptr->method();
 */
template<typename T>
class strong_ptr {
public:
    using element_type = T;
    using control_block_type = detail::control_block<T>;

    // ===== Constructors =====

    /// Default constructor - creates empty strong_ptr
    constexpr strong_ptr() noexcept : ptr_(nullptr), cb_(nullptr) {}

    /// Nullptr constructor
    constexpr strong_ptr(std::nullptr_t) noexcept : ptr_(nullptr), cb_(nullptr) {}

    /// Construct from raw pointer (takes ownership)
    /// Private - use make_strong<T>() instead

    /// Copy constructor
    strong_ptr(const strong_ptr& other) noexcept
        : ptr_(other.ptr_), cb_(other.cb_) {
        if (cb_) {
            cb_->add_strong();
        }
    }

    /// Move constructor
    strong_ptr(strong_ptr&& other) noexcept
        : ptr_(other.ptr_), cb_(other.cb_) {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    /// Converting copy constructor (for derived types)
    template<typename U>
    requires std::is_convertible_v<U*, T*>
    strong_ptr(const strong_ptr<U>& other) noexcept
        : ptr_(other.ptr_), cb_(reinterpret_cast<control_block_type*>(other.cb_)) {
        if (cb_) {
            cb_->add_strong();
        }
    }

    /// Converting move constructor (for derived types)
    template<typename U>
    requires std::is_convertible_v<U*, T*>
    strong_ptr(strong_ptr<U>&& other) noexcept
        : ptr_(other.ptr_), cb_(reinterpret_cast<control_block_type*>(other.cb_)) {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    // ===== Destructor =====

    ~strong_ptr() {
        // CRITICAL: Save cb_ to a local before any operations that might
        // trigger re-entrant destruction (e.g., deleting an object whose
        // destructor modifies this strong_ptr via variant storage reuse).
        auto* local_cb = cb_;
        auto* local_ptr = ptr_;

        // Null out members FIRST to prevent re-entrancy issues
        cb_ = nullptr;
        ptr_ = nullptr;

        if (local_cb && local_cb->release_strong()) {
            // Last strong reference - delete the object
            if (local_cb->destructor) {
                local_cb->destructor(local_ptr);
            } else {
                delete local_ptr;
            }

            // Release the strong family's collective weak reference.
            // This was initialized to 1 in make_strong(). Even if weaker_ptrs are
            // destroyed during object destruction, they can't delete the control
            // block because this weak reference is still held.
            if (local_cb->release_weak()) {
                delete local_cb;
            }
        }
    }

    // ===== Assignment =====

    strong_ptr& operator=(const strong_ptr& other) noexcept {
        if (this != &other) {
            strong_ptr tmp(other);
            swap(tmp);
        }
        return *this;
    }

    strong_ptr& operator=(strong_ptr&& other) noexcept {
        if (this != &other) {
            strong_ptr tmp(std::move(other));
            swap(tmp);
        }
        return *this;
    }

    strong_ptr& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    // ===== Modifiers =====

    void reset() noexcept {
        strong_ptr().swap(*this);
    }

    void swap(strong_ptr& other) noexcept {
        std::swap(ptr_, other.ptr_);
        std::swap(cb_, other.cb_);
    }

    // ===== Observers =====

    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    size_t use_count() const noexcept {
        return cb_ ? cb_->get_strong_count() : 0;
    }

    bool unique() const noexcept {
        return use_count() == 1;
    }

    // ===== Comparison =====

    bool operator==(const strong_ptr& other) const noexcept { return ptr_ == other.ptr_; }
    bool operator!=(const strong_ptr& other) const noexcept { return ptr_ != other.ptr_; }
    bool operator<(const strong_ptr& other) const noexcept { return ptr_ < other.ptr_; }
    bool operator<=(const strong_ptr& other) const noexcept { return ptr_ <= other.ptr_; }
    bool operator>(const strong_ptr& other) const noexcept { return ptr_ > other.ptr_; }
    bool operator>=(const strong_ptr& other) const noexcept { return ptr_ >= other.ptr_; }

    bool operator==(std::nullptr_t) const noexcept { return ptr_ == nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return ptr_ != nullptr; }

private:
    T* ptr_;
    control_block_type* cb_;

    // Private constructor for make_strong
    strong_ptr(T* ptr, control_block_type* cb) noexcept : ptr_(ptr), cb_(cb) {}

    // Friend declarations
    template<typename U> friend class strong_ptr;
    template<typename U> friend class weaker_ptr;
    template<typename U, typename... Args> friend strong_ptr<U> make_strong(Args&&...);
};

/**
 * @brief Weak reference to an object managed by strong_ptr, weaker than an std::weaker_ptr hence the cute name.
 *
 * weaker_ptr does not prevent the object from being destroyed.
 * Call lock() to get a strong_ptr if the object is still alive.
 */
template<typename T>
class weaker_ptr {
public:
    using element_type = T;
    using control_block_type = detail::control_block<T>;

    // ===== Constructors =====

    constexpr weaker_ptr() noexcept : ptr_(nullptr), cb_(nullptr) {}

    weaker_ptr(const weaker_ptr& other) noexcept
        : ptr_(other.ptr_), cb_(other.cb_) {
        if (cb_) {
            cb_->add_weak();
        }
    }

    weaker_ptr(weaker_ptr&& other) noexcept
        : ptr_(other.ptr_), cb_(other.cb_) {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    /// Construct from strong_ptr
    weaker_ptr(const strong_ptr<T>& other) noexcept
        : ptr_(other.ptr_), cb_(other.cb_) {
        if (cb_) {
            cb_->add_weak();
        }
    }

    // ===== Destructor =====

    ~weaker_ptr() {
        // Save to local and null out first (consistent with strong_ptr pattern)
        auto* local_cb = cb_;
        cb_ = nullptr;
        ptr_ = nullptr;

        if (local_cb && local_cb->release_weak()) {
            // Last reference (weak AND strong) - delete control block
            delete local_cb;
        }
    }

    // ===== Assignment =====

    weaker_ptr& operator=(const weaker_ptr& other) noexcept {
        weaker_ptr tmp(other);
        swap(tmp);
        return *this;
    }

    weaker_ptr& operator=(weaker_ptr&& other) noexcept {
        weaker_ptr tmp(std::move(other));
        swap(tmp);
        return *this;
    }

    weaker_ptr& operator=(const strong_ptr<T>& other) noexcept {
        weaker_ptr tmp(other);
        swap(tmp);
        return *this;
    }

    // ===== Modifiers =====

    void reset() noexcept {
        weaker_ptr().swap(*this);
    }

    void swap(weaker_ptr& other) noexcept {
        std::swap(ptr_, other.ptr_);
        std::swap(cb_, other.cb_);
    }

    // ===== Observers =====

    size_t use_count() const noexcept {
        return cb_ ? cb_->get_strong_count() : 0;
    }

    bool expired() const noexcept {
        return use_count() == 0;
    }

    /**
     * @brief Attempt to get a strong_ptr to the managed object
     * @return strong_ptr<T> if object is still alive, empty strong_ptr otherwise
     */
    strong_ptr<T> lock() const noexcept {
        if (cb_ && cb_->try_add_strong()) {
            strong_ptr<T> result;
            result.ptr_ = ptr_;
            result.cb_ = cb_;
            return result;
        }
        return strong_ptr<T>();
    }

private:
    T* ptr_;
    control_block_type* cb_;
};

/**
 * @brief Create a strong_ptr managing a new object
 *
 * Similar to std::make_shared, this creates the object and control block.
 *
 * @tparam T The type of object to create
 * @tparam Args Constructor argument types
 * @param args Arguments forwarded to T's constructor
 * @return strong_ptr<T> owning the new object
 */
template<typename T, typename... Args>
strong_ptr<T> make_strong(Args&&... args) {
    auto* ptr = new T(std::forward<Args>(args)...);
    auto* cb = new detail::control_block<T>();
    cb->destructor = detail::control_block<T>::default_destructor;
    return strong_ptr<T>(ptr, cb);
}

// ===== Free functions =====

template<typename T>
void swap(strong_ptr<T>& lhs, strong_ptr<T>& rhs) noexcept {
    lhs.swap(rhs);
}

template<typename T>
void swap(weaker_ptr<T>& lhs, weaker_ptr<T>& rhs) noexcept {
    lhs.swap(rhs);
}

} // namespace jai

#endif // __JAISCRIPT_CORE_STRONG_PTR_HPP__
