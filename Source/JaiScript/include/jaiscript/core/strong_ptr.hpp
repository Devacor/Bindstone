#pragma once

#ifndef __JAISCRIPT_CORE_STRONG_PTR_HPP__
#define __JAISCRIPT_CORE_STRONG_PTR_HPP__

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace jai {

// Forward declarations
template<typename T> class strong_ptr;
template<typename T> class weaker_ptr;

namespace detail {

/**
 * @brief Base control block with reference counts only
 *
 * Purely non-atomic for maximum single-threaded performance.
 * Thread safety is handled at a higher level (critical sections, partitioning, etc.)
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
 * @brief Control block with embedded object storage (combined allocation)
 *
 * Like std::make_shared, object and control block are allocated together.
 * Benefits:
 * - Single allocation instead of two
 * - Better cache locality (refcounts adjacent to object)
 * - Smaller control block (no destructor pointer needed)
 *
 * Trade-off: Memory not freed until all weaker_ptrs are gone (same as make_shared).
 */
template<typename T>
struct control_block : control_block_base {
    alignas(T) unsigned char storage[sizeof(T)];

    T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }
    const T* ptr() const noexcept { return std::launder(reinterpret_cast<const T*>(storage)); }
};

} // namespace detail

/**
 * @brief Non-atomic reference-counted smart pointer for single-threaded use
 *
 * strong_ptr provides shared ownership semantics like std::shared_ptr but uses
 * non-atomic reference counting for maximum performance.
 *
 * Objects are created via make_strong<T>() which uses combined allocation
 * (object + control block in single allocation) for optimal cache locality.
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
    constexpr strong_ptr() noexcept = default;

    /// Nullptr constructor
    constexpr strong_ptr(std::nullptr_t) noexcept {}

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
        // CRITICAL: Save to locals before any operations that might
        // trigger re-entrant destruction via variant storage reuse.
        auto* local_cb = cb_;
        auto* local_ptr = ptr_;

        // Null out members FIRST to prevent re-entrancy issues
        cb_ = nullptr;
        ptr_ = nullptr;

        if (local_cb && local_cb->release_strong()) {
            // Last strong reference - destroy object (but don't free memory yet)
            local_ptr->~T();

            // Release the strong family's collective weak reference.
            // Memory is only freed when all weaker_ptrs are also gone.
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

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }

    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] size_t use_count() const noexcept {
        return cb_ ? cb_->get_strong_count() : 0;
    }

    [[nodiscard]] bool unique() const noexcept {
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
    T* ptr_ = nullptr;
    control_block_type* cb_ = nullptr;

    // Private constructor for make_strong
    strong_ptr(T* ptr, control_block_type* cb) noexcept : ptr_(ptr), cb_(cb) {}

    // Friend declarations
    template<typename U> friend class strong_ptr;
    template<typename U> friend class weaker_ptr;
    template<typename U, typename... Args> friend strong_ptr<U> make_strong(Args&&...);
};

/**
 * @brief Weak reference to an object managed by strong_ptr
 *
 * weaker_ptr does not prevent the object from being destroyed.
 * Call lock() to get a strong_ptr if the object is still alive.
 *
 * Note: With combined allocation, the memory (including destroyed object's storage)
 * remains allocated until all weaker_ptrs are gone. This is the same behavior as
 * std::weak_ptr with std::make_shared.
 */
template<typename T>
class weaker_ptr {
public:
    using element_type = T;
    using control_block_type = detail::control_block<T>;

    // ===== Constructors =====

    constexpr weaker_ptr() noexcept = default;

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

    [[nodiscard]] size_t use_count() const noexcept {
        return cb_ ? cb_->get_strong_count() : 0;
    }

    [[nodiscard]] bool expired() const noexcept {
        return use_count() == 0;
    }

    /**
     * @brief Attempt to get a strong_ptr to the managed object
     * @return strong_ptr<T> if object is still alive, empty strong_ptr otherwise
     */
    [[nodiscard]] strong_ptr<T> lock() const noexcept {
        if (cb_ && cb_->try_add_strong()) {
            strong_ptr<T> result;
            result.ptr_ = ptr_;
            result.cb_ = cb_;
            return result;
        }
        return strong_ptr<T>();
    }

private:
    T* ptr_ = nullptr;
    control_block_type* cb_ = nullptr;
};

/**
 * @brief Create a strong_ptr managing a new object (combined allocation)
 *
 * Object and control block are allocated together in a single allocation.
 * This provides:
 * - Better cache locality (object adjacent to refcounts)
 * - Single allocation instead of two
 * - Reduced memory fragmentation
 *
 * @tparam T The type of object to create
 * @tparam Args Constructor argument types
 * @param args Arguments forwarded to T's constructor
 * @return strong_ptr<T> owning the new object
 */
template<typename T, typename... Args>
[[nodiscard]] strong_ptr<T> make_strong(Args&&... args) {
    // Single allocation for control block + object storage
    auto* cb = new detail::control_block<T>();

    // Construct object in-place using placement new
    T* ptr = new (cb->storage) T(std::forward<Args>(args)...);

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
