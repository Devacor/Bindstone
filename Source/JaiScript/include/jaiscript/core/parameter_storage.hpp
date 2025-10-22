#pragma once

#include <array>
#include <any>
#include <memory>

namespace jai {
namespace detail {

/**
 * Storage pool for temporary objects during function calls.
 * Allocated on the stack, passed via thread_local pointer.
 */
class parameter_storage {
public:
    static constexpr size_t MAX_TEMPS = 32;
    
private:
    std::array<std::any, MAX_TEMPS> storage_;
    size_t index_ = 0;
    
    // Thread-local pointer to current storage (managed by engine)
    static thread_local parameter_storage* current_;
    
public:
    parameter_storage() = default;
    ~parameter_storage() = default;
    
    // Non-copyable, non-movable (stack-based)
    parameter_storage(const parameter_storage&) = delete;
    parameter_storage& operator=(const parameter_storage&) = delete;
    parameter_storage(parameter_storage&&) = delete;
    parameter_storage& operator=(parameter_storage&&) = delete;
    
    /**
     * Allocate storage for a temporary of type T.
     * Returns a reference valid for the lifetime of this storage.
     */
    template<typename T>
    T& allocate() {
        if (index_ >= MAX_TEMPS) {
            throw std::runtime_error("Parameter storage exhausted");
        }
        
        storage_[index_] = T{};
        T& result = std::any_cast<T&>(storage_[index_]);
        index_++;
        return result;
    }
    
    /**
     * Get the current storage (may be null).
     */
    static parameter_storage* current() {
        return current_;
    }
    
    /**
     * RAII guard to set/restore current storage.
     */
    class scope_guard {
    private:
        parameter_storage* previous_;
        
    public:
        explicit scope_guard(parameter_storage* storage) 
            : previous_(current_) {
            current_ = storage;
        }
        
        ~scope_guard() {
            current_ = previous_;
        }
        
        // Non-copyable, non-movable
        scope_guard(const scope_guard&) = delete;
        scope_guard& operator=(const scope_guard&) = delete;
        scope_guard(scope_guard&&) = delete;
        scope_guard& operator=(scope_guard&&) = delete;
    };
};

// Definition of static member
inline thread_local parameter_storage* parameter_storage::current_ = nullptr;

} // namespace detail
} // namespace jai