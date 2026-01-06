#pragma once

#ifndef __JAISCRIPT_CORE_BOUND_CPP_VECTOR_REGISTRATION_HPP__
#define __JAISCRIPT_CORE_BOUND_CPP_VECTOR_REGISTRATION_HPP__

#include "bound_cpp_vector.hpp"
#include "dynamic_binder.hpp"
#include <string>

namespace jai {

// Forward declaration
class engine;

/**
 * @brief Register bound_cpp_vector<T> with JaiScript engine
 *
 * This registers the bound_cpp_vector wrapper class so script code can call
 * methods like .size(), .push(), etc. on C++ vector properties.
 *
 * @tparam T The element type of the vector
 * @param eng Engine to register with
 * @param type_name Name to use for the bound type (e.g., "bound_cpp_vector<Cat>")
 */
template<typename T>
void register_bound_cpp_vector(engine& eng, const std::string& type_name) {
    dynamic_binder<bound_cpp_vector<T>>(eng, type_name)
        // Array-like methods
        .method("size", &bound_cpp_vector<T>::size)
        .method("empty", &bound_cpp_vector<T>::empty)
        .method("clear", &bound_cpp_vector<T>::clear)
        .method("push_back", static_cast<void(bound_cpp_vector<T>::*)(const T&)>(&bound_cpp_vector<T>::push_back))
        .method("push", static_cast<void(bound_cpp_vector<T>::*)(const T&)>(&bound_cpp_vector<T>::push_back)) // Alias
        .method("pop_back", &bound_cpp_vector<T>::pop_back)
        .method("pop", &bound_cpp_vector<T>::pop_back) // Alias
        .method("front", static_cast<T&(bound_cpp_vector<T>::*)()>(&bound_cpp_vector<T>::front))
        .method("back", static_cast<T&(bound_cpp_vector<T>::*)()>(&bound_cpp_vector<T>::back))
        // Indexing via at() method (operator[] needs special handling)
        .method("at", static_cast<T&(bound_cpp_vector<T>::*)(size_t)>(&bound_cpp_vector<T>::at))
        .build();
}

/**
 * @brief Helper to get or register bound_cpp_vector<T> for a type
 *
 * Checks if bound_cpp_vector<T> is already registered, and registers it if not.
 * Returns the type name used for registration.
 *
 * @tparam T The element type
 * @param eng Engine reference
 * @return Type name used for bound_cpp_vector<T>
 */
template<typename T>
std::string ensure_bound_cpp_vector_registered(engine& eng) {
    std::string type_name = std::string("bound_cpp_vector<") + typeid(T).name() + ">";

    // Check if already registered by trying to get class definition
    auto existing = eng.get_class_definition_by_type(std::type_index(typeid(bound_cpp_vector<T>)));
    if (!existing) {
        // Not registered yet - register it now
        register_bound_cpp_vector<T>(eng, type_name);
    }

    return type_name;
}

} // namespace jai

#endif // __JAISCRIPT_CORE_BOUND_CPP_VECTOR_REGISTRATION_HPP__
