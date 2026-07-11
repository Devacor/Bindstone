#pragma once

#ifndef __JAISCRIPT_CORE_STRONG_PTR_HPP__
#define __JAISCRIPT_CORE_STRONG_PTR_HPP__

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#ifdef JAISCRIPT_DIAG_ATOMIC_RC
#include <atomic>
#endif
#ifdef JAISCRIPT_DIAG_XTHREAD_RC
#include <cstdlib>
#include <thread>
#endif

namespace jai {

template<typename T> class strong_ptr;
template<typename T> class weaker_ptr;

namespace detail {

#ifndef NDEBUG
inline constexpr uint32_t cb_magic_live = 0x5B0CB10C;
inline constexpr uint32_t cb_magic_dead = 0x0DEADCB0;
#endif

/**
 * @brief Base control block with reference counts and type-erased deallocation
 *
 * Purely non-atomic for maximum single-threaded performance.
 * Thread safety is handled at a higher level (critical sections, partitioning, etc.)
 *
 * The dealloc_fn pointer enables type-safe deallocation through control_block_base*
 * without virtual dispatch or reinterpret_cast. Set once at construction, called
 * only when the last weak reference is released (cold path).
 */
struct control_block_base {
#ifdef JAISCRIPT_DIAG_ATOMIC_RC
    // Diagnostic build: atomic counts. If a flaky corruption VANISHES under this flag,
    // it is a cross-thread refcount race; if it persists, look for a wild write.
    std::atomic<size_t> strong_count{1};
    std::atomic<size_t> weak_count{1};
#else
    size_t strong_count = 1;
    size_t weak_count = 1;  // Strong family's collective weak ref prevents premature CB deletion
#endif
    void (*dealloc_fn)(control_block_base*) = nullptr;
#ifndef NDEBUG
    uint32_t magic = cb_magic_live;  // asserted at every cb derivation, scrambled in dealloc (D8)
#endif
#ifdef JAISCRIPT_DIAG_XTHREAD_RC
    // Canary build: blocks tagged single-thread (AST literal strings) abort AT the
    // cross-thread refcount touch — the abort backtrace names the racer, not the victim
    std::thread::id diag_owner{};   // default = untagged
    void diag_tag() noexcept { diag_owner = std::this_thread::get_id(); }
    void diag_check() const noexcept {
        if (diag_owner != std::thread::id{} && diag_owner != std::this_thread::get_id()) { std::abort(); }
    }
#endif

    control_block_base() = default;
    control_block_base(const control_block_base&) = delete;
    control_block_base& operator=(const control_block_base&) = delete;

#ifdef JAISCRIPT_DIAG_XTHREAD_RC
    void add_strong() noexcept { diag_check(); ++strong_count; }
    bool release_strong() noexcept { diag_check(); return --strong_count == 0; }
#else
    void add_strong() noexcept { ++strong_count; }
    bool release_strong() noexcept { return --strong_count == 0; }
#endif
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
 *
 * Trade-off: Memory not freed until all weaker_ptrs are gone (same as make_shared).
 */
template<typename T>
struct control_block : control_block_base {
    alignas(T) unsigned char storage[sizeof(T)] = {};

    control_block() {
        dealloc_fn = [](control_block_base* base) {
#ifndef NDEBUG
            base->magic = cb_magic_dead;
#endif
            delete static_cast<control_block<T>*>(base);
        };
    }

    T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }
    const T* ptr() const noexcept { return std::launder(reinterpret_cast<const T*>(storage)); }
};

// Conditionally-supported offsetof on a non-standard-layout type: fine on MSVC/clang/gcc.
// NEVER hardcode this — over-aligned T (align > 8) shifts storage past the counts, and the
// Debug-only magic member changes the base size between configs.
template<typename T>
inline constexpr size_t cb_storage_offset = offsetof(control_block<T>, storage);

// Derive the control block from an object pointer. Sound because make_strong and
// adopt_pooled are the ONLY producers of a non-null handle (raw ctor deleted, converting
// ctors deleted, no aliasing ctor; lock() copies an existing ptr_), and both set
// ptr_ = &control_block<T>::storage (pooled blocks derive control_block<T> without
// moving storage's offset).
// A live weaker_ptr holds weak_count >= 1 and dealloc runs only when weak_count -> 0, so
// the combined block outlives every non-null handle — the derivation stays valid for the
// handle's whole life, including expired weaks (which touch only counts, never object bytes).
template<typename T>
inline control_block_base* cb_from_object(T* p) noexcept {
    if (!p) return nullptr;
    auto* cb = static_cast<control_block_base*>(reinterpret_cast<control_block<T>*>(
        reinterpret_cast<unsigned char*>(p) - cb_storage_offset<T>));
    assert(cb->magic == cb_magic_live && "strong_ptr control-block derivation hit a dead or foreign block");
    return cb;
}

struct adopt_tag {};

// Reserved for engine-owned block pools (script_value's reference_holder pool,
// value.cpp): adopt a strong count already established on a reused control_block<T>.
// The single-pointer derivation invariant holds because pooled blocks ARE
// control_block<T> (derived without moving storage's offset) and `ptr` is exactly
// the placement-new result at control_block<T>::storage's head.
template<typename T>
strong_ptr<T> adopt_pooled(T* ptr) noexcept;

} // namespace detail

/**
 * @brief Non-atomic reference-counted smart pointer for single-threaded use
 *
 * strong_ptr provides shared ownership semantics like std::shared_ptr but uses
 * non-atomic reference counting for maximum performance.
 *
 * Objects are created via make_strong<T>() which uses combined allocation
 * (object + control block in single allocation) for optimal cache locality.
 * The handle is a SINGLE object pointer; the control block lives at a fixed
 * negative offset from the object (derived, never stored).
 *
 * There is deliberately NO Derived -> Base converting constructor: single-pointer
 * derivation requires ptr_ to be exactly control_block<T>::storage's head, and a
 * base conversion would both adjust the pointer and slice the static-type destroy.
 * Cross-type sharing goes through object_holder / std::shared_ptr instead (D7).
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

    // ===== Constructors =====

    constexpr strong_ptr() noexcept = default;
    constexpr strong_ptr(std::nullptr_t) noexcept {}

    /// Deleted: use make_strong<T>() instead (enforces combined allocation)
    strong_ptr(T*) = delete;

    strong_ptr(const strong_ptr& other) noexcept
        : ptr_(other.ptr_) {
        if (ptr_) {
            cb()->add_strong();
        }
    }

    strong_ptr(strong_ptr&& other) noexcept
        : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    // ===== Destructor =====

    ~strong_ptr() {
        // CRITICAL: Save to a local before any operations that might
        // trigger re-entrant destruction via variant storage reuse.
        auto* local_ptr = ptr_;

        // Null out the member FIRST to prevent re-entrancy issues
        ptr_ = nullptr;

        if (local_ptr) {
            // Derive from the local: ~T() never changes the address, and the memory is
            // still allocated because this handle held live strong+weak counts.
            auto* local_cb = detail::cb_from_object(local_ptr);
            if (local_cb->release_strong()) {
                // Last strong reference - destroy object (but don't free memory yet)
                local_ptr->~T();

                // Release the strong family's collective weak reference.
                // Memory is only freed when all weaker_ptrs are also gone.
                if (local_cb->release_weak()) {
                    local_cb->dealloc_fn(local_cb);
                }
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
    }

    // ===== Observers =====

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }

    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] size_t use_count() const noexcept {
        return ptr_ ? cb()->get_strong_count() : 0;
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

    detail::control_block_base* cb() const noexcept { return detail::cb_from_object(ptr_); }

    // Adopts a count already established by the caller (make_strong's initial 1,
    // or weaker_ptr::lock()'s try_add_strong).
    strong_ptr(T* ptr, detail::adopt_tag) noexcept : ptr_(ptr) {}

    template<typename U> friend class weaker_ptr;
    template<typename U, typename... Args> friend strong_ptr<U> make_strong(Args&&...);
    template<typename U> friend strong_ptr<U> detail::adopt_pooled(U*) noexcept;
};

template<typename T>
strong_ptr<T> detail::adopt_pooled(T* ptr) noexcept {
    return strong_ptr<T>(ptr, detail::adopt_tag{});
}

/**
 * @brief Weak reference to an object managed by strong_ptr
 *
 * weaker_ptr does not prevent the object from being destroyed.
 * Call lock() to get a strong_ptr if the object is still alive.
 *
 * Note: With combined allocation, the memory (including destroyed object's storage)
 * remains allocated until all weaker_ptrs are gone. This is the same behavior as
 * std::weak_ptr with std::make_shared — and it is what keeps the negative-offset
 * control-block derivation valid even for expired weaks.
 */
template<typename T>
class weaker_ptr {
public:
    using element_type = T;

    // ===== Constructors =====

    constexpr weaker_ptr() noexcept = default;

    weaker_ptr(const weaker_ptr& other) noexcept
        : ptr_(other.ptr_) {
        if (ptr_) {
            cb()->add_weak();
        }
    }

    weaker_ptr(weaker_ptr&& other) noexcept
        : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    weaker_ptr(const strong_ptr<T>& other) noexcept
        : ptr_(other.get()) {
        if (ptr_) {
            cb()->add_weak();
        }
    }

    // ===== Destructor =====

    ~weaker_ptr() {
        // Save to a local and null out first (consistent with strong_ptr pattern)
        auto* local_ptr = ptr_;
        ptr_ = nullptr;

        if (local_ptr) {
            auto* local_cb = detail::cb_from_object(local_ptr);
            if (local_cb->release_weak()) {
                local_cb->dealloc_fn(local_cb);
            }
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
    }

    // ===== Observers =====

    [[nodiscard]] size_t use_count() const noexcept {
        return ptr_ ? cb()->get_strong_count() : 0;
    }

    [[nodiscard]] bool expired() const noexcept {
        return use_count() == 0;
    }

    /**
     * @brief Attempt to get a strong_ptr to the managed object
     * @return strong_ptr<T> if object is still alive, empty strong_ptr otherwise
     */
    [[nodiscard]] strong_ptr<T> lock() const noexcept {
        if (ptr_ && cb()->try_add_strong()) {
            return strong_ptr<T>(ptr_, detail::adopt_tag{});
        }
        return strong_ptr<T>();
    }

private:
    T* ptr_ = nullptr;

    detail::control_block_base* cb() const noexcept { return detail::cb_from_object(ptr_); }
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
 * make_strong (and the pool-reuse twin detail::adopt_pooled) are the ONLY producers of
 * a non-null strong_ptr — the single-pointer control-block derivation depends on that
 * (see detail::cb_from_object).
 *
 * @tparam T The type of object to create
 * @tparam Args Constructor argument types
 * @param args Arguments forwarded to T's constructor
 * @return strong_ptr<T> owning the new object
 */
template<typename T, typename... Args>
[[nodiscard]] strong_ptr<T> make_strong(Args&&... args) {
    auto* cb = new detail::control_block<T>();
    T* ptr = new (cb->storage) T(std::forward<Args>(args)...);

    return strong_ptr<T>(ptr, detail::adopt_tag{});
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

namespace detail { struct sp_layout_probe { void* a; }; }
static_assert(sizeof(strong_ptr<detail::sp_layout_probe>) == sizeof(void*),
              "strong_ptr must stay a single object pointer (rung 2a of the thin-value fold)");
static_assert(sizeof(weaker_ptr<detail::sp_layout_probe>) == sizeof(void*),
              "weaker_ptr must stay a single object pointer (rung 2a of the thin-value fold)");

} // namespace jai

#endif // __JAISCRIPT_CORE_STRONG_PTR_HPP__
