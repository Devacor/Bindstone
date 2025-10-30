#pragma once

#ifndef __JAISCRIPT_DETAIL_TRACKED_SHARED_PTR_HPP__
#define __JAISCRIPT_DETAIL_TRACKED_SHARED_PTR_HPP__

#include <jaiscript/debug_config.hpp>
#include <memory>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <source_location>

namespace jai {
namespace detail {

#if defined(__cpp_lib_source_location)
    using src_loc = std::source_location;
#else
    // Fallback for compilers without source_location
    struct src_loc {
        constexpr const char* file_name() const { return "unknown"; }
        constexpr int line() const { return 0; }
        constexpr const char* function_name() const { return "unknown"; }
        static constexpr src_loc current() { return {}; }
    };
#endif

// Reference tracking registry (only compiled when tracking is enabled)
template<typename T>
class reference_tracker {
public:
    struct ref_info {
        const char* file;
        int line;
        const char* function;
        void* ptr_address;
    };

    static reference_tracker& instance() {
        static reference_tracker tracker;
        return tracker;
    }

    void register_ref(void* ptr_addr, const char* file, int line, const char* func, void* obj) {
        std::lock_guard<std::mutex> lock(mutex_);
        ref_info info{file, line, func, obj};
        refs_[ptr_addr] = info;
    }

    void unregister_ref(void* ptr_addr) {
        std::lock_guard<std::mutex> lock(mutex_);
        refs_.erase(ptr_addr);
    }

    void dump_refs_for_object(void* obj) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cerr << "=== Active references for object " << obj << " ===\n";
        int count = 0;
        for (const auto& [ptr_addr, info] : refs_) {
            if (info.ptr_address == obj) {
                std::cerr << "  [" << ++count << "] " << info.file << ":" << info.line
                          << " in " << info.function << " (ptr @ " << ptr_addr << ")\n";
            }
        }
        if (count == 0) {
            std::cerr << "  No active references found.\n";
        }
        std::cerr << "=== End of references ===\n";
    }

private:
    std::mutex mutex_;
    std::unordered_map<void*, ref_info> refs_;
};

// Tracked shared_ptr wrapper - only tracks when debug flag is enabled
template<typename T>
class tracked_shared_ptr {
private:
    std::shared_ptr<T> ptr_;

    void track_ref(const src_loc& loc = src_loc::current()) {
        if constexpr (debug::TRACK_OBJECT_REFERENCES) {
            if (ptr_) {
                reference_tracker<T>::instance().register_ref(
                    (void*)this,
                    loc.file_name(),
                    loc.line(),
                    loc.function_name(),
                    ptr_.get()
                );
            }
        }
    }

    void untrack_ref() {
        if constexpr (debug::TRACK_OBJECT_REFERENCES) {
            reference_tracker<T>::instance().unregister_ref((void*)this);
        }
    }

public:
    // Default constructor
    tracked_shared_ptr() = default;

    // Nullptr constructor
    tracked_shared_ptr(std::nullptr_t) : ptr_(nullptr) {}

    // Constructor from raw pointer
    template<typename U>
    explicit tracked_shared_ptr(U* p, const src_loc& loc = src_loc::current()) : ptr_(p) {
        track_ref(loc);
    }

    // Constructor from raw pointer with deleter
    template<typename U, typename Deleter>
    tracked_shared_ptr(U* p, Deleter d, const src_loc& loc = src_loc::current()) : ptr_(p, d) {
        track_ref(loc);
    }

    // Copy constructor
    tracked_shared_ptr(const tracked_shared_ptr& other, const src_loc& loc = src_loc::current())
        : ptr_(other.ptr_) {
        track_ref(loc);
    }

    // Copy constructor from std::shared_ptr
    tracked_shared_ptr(const std::shared_ptr<T>& other, const src_loc& loc = src_loc::current())
        : ptr_(other) {
        track_ref(loc);
    }

    // Move constructor
    tracked_shared_ptr(tracked_shared_ptr&& other, const src_loc& loc = src_loc::current()) noexcept
        : ptr_(std::move(other.ptr_)) {
        other.untrack_ref();
        track_ref(loc);
    }

    // Destructor
    ~tracked_shared_ptr() {
        untrack_ref();
    }

    // Copy assignment
    tracked_shared_ptr& operator=(const tracked_shared_ptr& other) {
        untrack_ref();
        ptr_ = other.ptr_;
        track_ref();
        return *this;
    }

    // Move assignment
    tracked_shared_ptr& operator=(tracked_shared_ptr&& other) noexcept {
        untrack_ref();
        ptr_ = std::move(other.ptr_);
        other.untrack_ref();
        track_ref();
        return *this;
    }

    // Assignment from std::shared_ptr
    tracked_shared_ptr& operator=(const std::shared_ptr<T>& other) {
        untrack_ref();
        ptr_ = other;
        track_ref();
        return *this;
    }

    // Conversion to std::shared_ptr
    operator std::shared_ptr<T>() const { return ptr_; }

    // Access operators
    T* operator->() const { return ptr_.operator->(); }
    T& operator*() const { return *ptr_; }
    T* get() const { return ptr_.get(); }

    // Comparison operators
    bool operator==(const tracked_shared_ptr& other) const { return ptr_ == other.ptr_; }
    bool operator!=(const tracked_shared_ptr& other) const { return ptr_ != other.ptr_; }
    bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
    bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

    // Boolean conversion
    explicit operator bool() const { return ptr_ != nullptr; }

    // Reference count
    long use_count() const { return ptr_.use_count(); }

    // Dump all active references to this object
    void dump_references() const {
        if constexpr (debug::TRACK_OBJECT_REFERENCES) {
            if (ptr_) {
                reference_tracker<T>::instance().dump_refs_for_object(ptr_.get());
            }
        }
    }

    // Get underlying shared_ptr
    const std::shared_ptr<T>& get_shared_ptr() const { return ptr_; }
    std::shared_ptr<T>& get_shared_ptr() { return ptr_; }
};

} // namespace detail
} // namespace jai

#endif // __JAISCRIPT_DETAIL_TRACKED_SHARED_PTR_HPP__
