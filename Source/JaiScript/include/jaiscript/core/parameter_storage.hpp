#pragma once

#include <array>
#include <any>
#include <memory>
#include <stdexcept>

namespace jai {

// Forward declaration to break circular dependency
class engine;

namespace detail {

/**
 * Storage pool for temporary objects during function calls.
 * Allocated on the stack, managed via engine's current_parameter_storage_ member.
 * The scope_guard RAII class ensures proper cleanup.
 */
class parameter_storage {
public:
    static constexpr size_t MAX_TEMPS = 32;

private:
    std::array<std::any, MAX_TEMPS> storage_;
    size_t index_ = 0;

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
     * RAII guard to set/restore current storage on an engine.
     * Implementation is in engine_impl.hpp where engine is fully defined.
     */
    class scope_guard {
    private:
        engine* engine_;
        parameter_storage* previous_;

    public:
        // Declaration only - implementation in engine_impl.hpp
        scope_guard(engine* eng, parameter_storage* storage);
        ~scope_guard();

        // Non-copyable, non-movable
        scope_guard(const scope_guard&) = delete;
        scope_guard& operator=(const scope_guard&) = delete;
        scope_guard(scope_guard&&) = delete;
        scope_guard& operator=(scope_guard&&) = delete;
    };
};

/**
 * Non-member accessor function to get current parameter storage from engine.
 * Breaks circular dependency: function_binder.hpp can call this without
 * needing full engine definition.
 * Implementation is in engine.cpp.
 */
parameter_storage* get_engine_parameter_storage(engine* eng);

} // namespace detail
} // namespace jai
