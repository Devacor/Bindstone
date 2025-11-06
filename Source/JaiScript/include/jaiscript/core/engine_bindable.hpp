#pragma once

#include <memory>
#include <concepts>

namespace jai {

// Forward declaration
class engine;

/**
 * @brief Base class for objects that can receive engine reference injection
 *
 * Classes that need access to the JaiScript engine can inherit from this base.
 * The engine reference is automatically injected during:
 * - Script-side instantiation via registered constructors
 * - C++ instantiation bound via engine::add_global
 * - C++ instantiation via engine::make_object
 * - Deserialization via archive_reader
 *
 * Example usage:
 * @code
 * class my_class : public engine_bindable {
 * public:
 *     void do_something() {
 *         if (auto eng = get_engine()) {
 *             // Use engine...
 *         }
 *     }
 * };
 * @endcode
 */
class engine_bindable {
public:
    engine_bindable() = default;
    virtual ~engine_bindable() = default;

    // Copy/move operations
    engine_bindable(const engine_bindable&) = default;
    engine_bindable(engine_bindable&&) = default;
    engine_bindable& operator=(const engine_bindable&) = default;
    engine_bindable& operator=(engine_bindable&&) = default;

    /**
     * @brief Inject an engine reference into this object
     * @param eng Weak pointer to the engine instance
     */
    virtual void bind_to_engine(std::weak_ptr<engine> eng) {
        engine_ref_ = eng;
    }

    /**
     * @brief Get the engine reference
     * @return Weak pointer to the engine, or expired weak_ptr if not bound
     */
    std::weak_ptr<engine> get_engine() const {
        return engine_ref_;
    }

    /**
     * @brief Check if this object is bound to a valid engine
     * @return true if engine reference is valid, false otherwise
     */
    bool has_engine() const {
        return !engine_ref_.expired();
    }

private:
    std::weak_ptr<engine> engine_ref_;
};

/**
 * @brief Concept for detecting types that can receive engine injection
 */
template<typename T>
concept is_engine_bindable = std::is_base_of_v<engine_bindable, T>;

/**
 * @brief Helper function to inject engine into objects that support it
 * @param obj Object to inject engine into
 * @param eng Engine reference to inject
 *
 * This function uses SFINAE to only compile for types derived from engine_bindable.
 */
template<typename T>
void inject_engine_if_needed(T& obj, std::weak_ptr<engine> eng) {
    if constexpr (is_engine_bindable<T>) {
        obj.bind_to_engine(eng);
    }
}

/**
 * @brief Helper function to inject engine into shared_ptr objects that support it
 */
template<typename T>
void inject_engine_if_needed(std::shared_ptr<T>& obj, std::weak_ptr<engine> eng) {
    if (obj && is_engine_bindable<T>) {
        obj->bind_to_engine(eng);
    }
}

} // namespace jai
