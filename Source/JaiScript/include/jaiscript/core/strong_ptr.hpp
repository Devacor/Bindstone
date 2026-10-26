#pragma once

#ifndef __JAISCRIPT_CORE_STRONG_PTR_HPP__
#define __JAISCRIPT_CORE_STRONG_PTR_HPP__

#include <jaiscript/jaiscript_fwd.hpp>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <functional>
#include <atomic>
#include <cstring>
#ifdef JAISCRIPT_DIAG_ATOMIC_RC
#include <atomic>
#endif
#ifdef JAISCRIPT_DIAG_XTHREAD_RC
#include <cstdlib>
#include <thread>
#endif

#ifdef JAISCRIPT_VM_COVERAGE
// the copy census (detail/copy_census.hpp): every strong copy, and every node's death
namespace jai::census { void rc_add() noexcept; void node_freed(const void* node) noexcept; }
#define JAI_SP_CENSUS_ADD() ::jai::census::rc_add()
#define JAI_SP_CENSUS_FREED(p) ::jai::census::node_freed(p)
#else
#define JAI_SP_CENSUS_ADD() ((void)0)
#define JAI_SP_CENSUS_FREED(p) ((void)0)
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
    // Canary build: blocks tagged single-thread (AST literal strings) report AT the
    // cross-thread refcount touch — the hook (installed by the test runner) prints the
    // racer's backtrace directly; signal-based capture loses concurrent aborts
    std::thread::id diag_owner{};   // default = untagged
    void diag_tag() noexcept { diag_owner = std::this_thread::get_id(); }
    void diag_check() const noexcept;
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

#ifdef JAISCRIPT_DIAG_XTHREAD_RC
// The hook owns termination: the winning reporter prints and aborts; racing losers
// RETURN and continue (parking them deadlocks — they abort mid-CRT-operation holding
// locks the winner's printer needs). Without a hook, plain abort.
inline void (*diag_xthread_report)() = nullptr;
inline void control_block_base::diag_check() const noexcept {
    if (diag_owner != std::thread::id{} && diag_owner != std::this_thread::get_id()) {
        if (diag_xthread_report) { diag_xthread_report(); } else { std::abort(); }
    }
}
#endif

struct adopt_tag {};

// How a strong family counts. Plain counts by default; a type whose objects a parallel region's workers can reach together (a
// script class instance: a C++ object's script layer, reached through a host call) counts atomically while the region runs.
template<typename T>
struct strong_counts {
    static void add_strong(control_block_base* cb) noexcept { cb->add_strong(); }
    static bool release_strong(control_block_base* cb) noexcept { return cb->release_strong(); }
    static void add_weak(control_block_base* cb) noexcept { cb->add_weak(); }
    static bool release_weak(control_block_base* cb) noexcept { return cb->release_weak(); }
    static bool try_add_strong(control_block_base* cb) noexcept { return cb->try_add_strong(); }
    static size_t strong_count(const control_block_base* cb) noexcept { return cb->get_strong_count(); }
};

// Atomic operations on a plain count (libc++ before 19 has no std::atomic_ref)
struct shared_count_ops {
#if defined(_MSC_VER) && !defined(__clang__)
    static size_t fetch_add(size_t& c, size_t v) noexcept { return std::atomic_ref<size_t>(c).fetch_add(v, std::memory_order_relaxed); }
    static size_t fetch_sub(size_t& c, size_t v) noexcept { return std::atomic_ref<size_t>(c).fetch_sub(v, std::memory_order_acq_rel); }
    static size_t load(const size_t& c) noexcept { return std::atomic_ref<size_t>(const_cast<size_t&>(c)).load(std::memory_order_acquire); }
    static bool cas(size_t& c, size_t& seen, size_t want) noexcept {
        return std::atomic_ref<size_t>(c).compare_exchange_weak(seen, want, std::memory_order_acq_rel, std::memory_order_relaxed);
    }
#else
    static size_t fetch_add(size_t& c, size_t v) noexcept { return __atomic_fetch_add(&c, v, __ATOMIC_RELAXED); }
    static size_t fetch_sub(size_t& c, size_t v) noexcept { return __atomic_fetch_sub(&c, v, __ATOMIC_ACQ_REL); }
    static size_t load(const size_t& c) noexcept { return __atomic_load_n(&c, __ATOMIC_ACQUIRE); }
    static bool cas(size_t& c, size_t& seen, size_t want) noexcept {
        return __atomic_compare_exchange_n(&c, &seen, want, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
    }
#endif
};

// A class instance's block keeps, just before its control block, the flag that says its engine runs a parallel region
// (internal/object_layout.md): while it is set the counts are atomic, so workers reaching one instance count it safely
template<>
struct strong_counts<class_instance> {
    static const bool* region_flag(const control_block_base* cb) noexcept {
        const bool* flag;
        std::memcpy(&flag, reinterpret_cast<const unsigned char*>(cb) - sizeof(flag), sizeof(flag));
        return flag;
    }
    static bool shared(const control_block_base* cb) noexcept {
        const bool* flag = region_flag(cb);
        return flag && *flag;
    }
#ifdef JAISCRIPT_DIAG_ATOMIC_RC
    static void add_strong(control_block_base* cb) noexcept { cb->add_strong(); }
    static bool release_strong(control_block_base* cb) noexcept { return cb->release_strong(); }
    static void add_weak(control_block_base* cb) noexcept { cb->add_weak(); }
    static bool release_weak(control_block_base* cb) noexcept { return cb->release_weak(); }
    static bool try_add_strong(control_block_base* cb) noexcept { return cb->try_add_strong(); }
    static size_t strong_count(const control_block_base* cb) noexcept { return cb->get_strong_count(); }
#else
    static void add_strong(control_block_base* cb) noexcept {
        if (shared(cb)) [[unlikely]] { shared_count_ops::fetch_add(cb->strong_count, 1); return; }
        cb->add_strong();
    }
    static bool release_strong(control_block_base* cb) noexcept {
        if (shared(cb)) [[unlikely]] { return shared_count_ops::fetch_sub(cb->strong_count, 1) == 1; }
        return cb->release_strong();
    }
    static void add_weak(control_block_base* cb) noexcept {
        if (shared(cb)) [[unlikely]] { shared_count_ops::fetch_add(cb->weak_count, 1); return; }
        cb->add_weak();
    }
    static bool release_weak(control_block_base* cb) noexcept {
        if (shared(cb)) [[unlikely]] {
            return shared_count_ops::fetch_sub(cb->weak_count, 1) == 1 && shared_count_ops::load(cb->strong_count) == 0;
        }
        return cb->release_weak();
    }
    static bool try_add_strong(control_block_base* cb) noexcept {
        if (shared(cb)) [[unlikely]] {
            size_t seen = shared_count_ops::load(cb->strong_count);
            while (seen != 0) {
                if (shared_count_ops::cas(cb->strong_count, seen, seen + 1)) { return true; }
            }
            return false;
        }
        return cb->try_add_strong();
    }
    static size_t strong_count(const control_block_base* cb) noexcept {
        if (shared(cb)) [[unlikely]] { return shared_count_ops::load(cb->strong_count); }
        return cb->get_strong_count();
    }
#endif
};

// What the last strong release does: the object dies, then the strong family's collective weak reference goes. A type whose
// death is more than its destructor (a script class instance: its destructor chain, the release machine) specializes it.
template<typename T>
struct strong_last_release {
    static void run(T* object, control_block_base* cb) noexcept {
        object->~T();
        if (strong_counts<T>::release_weak(cb)) { cb->dealloc_fn(cb); }
    }
};
// A script class instance's death runs its destructor chain on the release machine (class_registry.cpp)
template<>
struct strong_last_release<class_instance> {
    static void run(class_instance* object, control_block_base* cb) noexcept;
};

// Reserved for engine-owned block pools (script_value's reference_holder pool,
// value.cpp): adopt a strong count already established on a reused control_block<T>.
// The single-pointer derivation invariant holds because pooled blocks ARE
// control_block<T> (derived without moving storage's offset) and `ptr` is exactly
// the placement-new result at control_block<T>::storage's head.
template<typename T>
strong_ptr<T> adopt_pooled(T* ptr) noexcept;
template<typename T>
weaker_ptr<T> weak_from_object(T* object) noexcept;

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

    JAI_FORCEINLINE strong_ptr(const strong_ptr& other) noexcept
        : ptr_(other.ptr_) {
        if (ptr_) {
            detail::strong_counts<T>::add_strong(cb());
            JAI_SP_CENSUS_ADD();
        }
    }

    JAI_FORCEINLINE strong_ptr(strong_ptr&& other) noexcept
        : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    // ===== Destructor =====

    JAI_FORCEINLINE ~strong_ptr() {
        // CRITICAL: Save to a local before any operations that might
        // trigger re-entrant destruction via variant storage reuse.
        auto* local_ptr = ptr_;

        // Null out the member FIRST to prevent re-entrancy issues
        ptr_ = nullptr;

        // Derive from the local: ~T() never changes the address, and the memory is
        // still allocated because this handle held live strong+weak counts.
        if (local_ptr && detail::strong_counts<T>::release_strong(detail::cb_from_object(local_ptr))) {
            destroy_last(local_ptr);
        }
    }

    // ===== Assignment =====

    JAI_FORCEINLINE strong_ptr& operator=(const strong_ptr& other) noexcept {
        if (this != &other) {
            strong_ptr tmp(other);
            swap(tmp);
        }
        return *this;
    }

    JAI_FORCEINLINE strong_ptr& operator=(strong_ptr&& other) noexcept {
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

    // Gives up the handle without dropping its count: the caller now owns that count (detail::object_ref adopts it)
    [[nodiscard]] T* release() noexcept {
        T* held = ptr_;
        ptr_ = nullptr;
        return held;
    }

    // ===== Observers =====

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }

    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] size_t use_count() const noexcept {
        return ptr_ ? detail::strong_counts<T>::strong_count(cb()) : 0;
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

    // The last strong reference: the object dies, then the strong family's collective weak reference
    // (memory is freed when every weaker_ptr is gone too)
    JAI_NOINLINE static void destroy_last(T* local_ptr) noexcept {
        auto* local_cb = detail::cb_from_object(local_ptr);
        JAI_SP_CENSUS_FREED(local_ptr);
        detail::strong_last_release<T>::run(local_ptr, local_cb);
    }

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

namespace detail {
// A new strong handle to an object some strong_ptr already owns (an object naming itself: the strong_ptr form of
// shared_from_this); empty once the object is dying
template<typename T>
strong_ptr<T> strong_from_object(T* object) noexcept {
    if (!object || !strong_counts<T>::try_add_strong(cb_from_object(object))) { return strong_ptr<T>(); }
    return adopt_pooled(object);
}
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
            detail::strong_counts<T>::add_weak(cb());
        }
    }

    weaker_ptr(weaker_ptr&& other) noexcept
        : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    weaker_ptr(const strong_ptr<T>& other) noexcept
        : ptr_(other.get()) {
        if (ptr_) {
            detail::strong_counts<T>::add_weak(cb());
        }
    }

    // ===== Destructor =====

    ~weaker_ptr() {
        // Save to a local and null out first (consistent with strong_ptr pattern)
        auto* local_ptr = ptr_;
        ptr_ = nullptr;

        if (local_ptr) {
            auto* local_cb = detail::cb_from_object(local_ptr);
            if (detail::strong_counts<T>::release_weak(local_cb)) {
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
        return ptr_ ? detail::strong_counts<T>::strong_count(cb()) : 0;
    }

	[[nodiscard]] const void* owner_identity() const noexcept { return ptr_; }
	[[nodiscard]] bool owner_before(const weaker_ptr& a_other) const noexcept {
		return std::less<const void*>{}(ptr_, a_other.ptr_);
	}

    [[nodiscard]] bool expired() const noexcept {
        return use_count() == 0;
    }

    /**
     * @brief Attempt to get a strong_ptr to the managed object
     * @return strong_ptr<T> if object is still alive, empty strong_ptr otherwise
     */
    [[nodiscard]] strong_ptr<T> lock() const noexcept {
        if (ptr_ && detail::strong_counts<T>::try_add_strong(cb())) {
            return strong_ptr<T>(ptr_, detail::adopt_tag{});
        }
        return strong_ptr<T>();
    }

private:
    T* ptr_ = nullptr;

    detail::control_block_base* cb() const noexcept { return detail::cb_from_object(ptr_); }

    template<typename U> friend weaker_ptr<U> detail::weak_from_object(U*) noexcept;
};

namespace detail {
// A weak handle to an object a strong_ptr owns or owned (its control block is still allocated: the object is alive, or its
// strong family still holds the collective weak reference)
template<typename T>
weaker_ptr<T> weak_from_object(T* object) noexcept {
    weaker_ptr<T> result;
    if (object) {
        strong_counts<T>::add_weak(cb_from_object(object));
        result.ptr_ = object;
    }
    return result;
}
}

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
              "strong_ptr must stay a single object pointer");
static_assert(sizeof(weaker_ptr<detail::sp_layout_probe>) == sizeof(void*),
              "weaker_ptr must stay a single object pointer");

} // namespace jai

#endif // __JAISCRIPT_CORE_STRONG_PTR_HPP__
